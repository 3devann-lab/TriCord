#include "utils/video_decoder.h"
#include "log.h"
#include "utils/redraw.h"
#include "utils/sound_player.h"
#include "utils/system_utils.h"
#include "utils/worker_thread.h"
#include <3ds.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

#ifdef TRICORD_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/display.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#endif

namespace Media {

#ifndef TRICORD_HAVE_FFMPEG

struct VideoDecoder::Impl {};

bool VideoDecoder::available() { return false; }
bool VideoDecoder::hardwareAvailable() { return false; }
VideoDecoder::VideoDecoder() = default;
VideoDecoder::~VideoDecoder() = default;
void VideoDecoder::start(const std::string &, double, bool, bool) {}
void VideoDecoder::stop() {}
PlayerState VideoDecoder::state() const { return PlayerState::FAILED; }
std::string VideoDecoder::error() const { return "Video playback is not included in this build."; }
VideoInfo VideoDecoder::info() const { return VideoInfo(); }
bool VideoDecoder::consumeFrame(const std::function<void(const uint16_t *, int, int)> &) { return false; }
double VideoDecoder::position() const { return 0.0; }
void VideoDecoder::setPaused(bool) {}
bool VideoDecoder::paused() const { return false; }
void VideoDecoder::seek(double) {}
void VideoDecoder::setVolume(float) {}
void VideoDecoder::setLoop(bool) {}

#else

namespace {

constexpr int AUDIO_CHANNEL = 2;
constexpr int WAVEBUF_COUNT = 6;
constexpr int WAVEBUF_SAMPLES = 4096;
constexpr int FRAME_SLOTS = 3;
constexpr size_t MAX_VIDEO_PACKETS = 90;
constexpr size_t MAX_AUDIO_PACKETS = 400;
constexpr size_t MVD_INPUT_SIZE = 2 * 1024 * 1024;
constexpr int MVD_MAX_ERRORS = 8;
constexpr int SCREEN_W = 400;
constexpr int SCREEN_H = 240;
// A frame this late is dropped instead of shown.
constexpr double LATE_DROP = 0.08;
// Further behind than this, B-frames stop being decoded.
constexpr double LATE_SKIP_NONREF = 0.3;
// Further behind than this, everything up to the next keyframe is skipped.
constexpr double LATE_SKIP_TO_KEY = 1.5;

double wallSeconds() { return osGetTime() / 1000.0; }

double toSeconds(int64_t ts, AVRational tb) {
	if (ts == AV_NOPTS_VALUE) {
		return NAN;
	}
	return ts * av_q2d(tb);
}

int roundRotation(double degrees) {
	if (!std::isfinite(degrees)) {
		return 0;
	}
	// The display matrix turns counterclockwise; the frame needs the opposite.
	int clockwise = -(int)std::lround(degrees);
	clockwise %= 360;
	if (clockwise < 0) {
		clockwise += 360;
	}
	return ((clockwise + 45) / 90 * 90) % 360;
}

void fitFrame(double width, double height, int boxW, int boxH, int &outW, int &outH) {
	const double scale = std::min({(double)boxW / width, (double)boxH / height, 1.0});
	outW = std::max(2, (int)(width * scale) & ~1);
	outH = std::max(2, (int)(height * scale) & ~1);
}

} // namespace

struct VideoDecoder::Impl {
	std::string path;
	double startAt = 0.0;
	bool allowHardware = true;
	std::atomic<bool> loop{false};

	Utils::WorkerThread demuxThread;
	Utils::WorkerThread audioThread;
	std::atomic<bool> stopping{false};

	mutable std::mutex mutex;
	std::condition_variable wake;

	PlayerState state = PlayerState::OPENING;
	std::string error;
	VideoInfo info;

	struct Frame {
		std::vector<uint16_t> pixels;
		double pts = 0.0;
	};
	std::deque<Frame> readyFrames;
	std::vector<std::vector<uint16_t>> freeBuffers;
	int buffersMade = 0;

	bool clockRunning = false;
	double clockBase = 0.0;
	double clockStart = 0.0;
	bool isPaused = false;

	bool seekPending = false;
	double seekTarget = 0.0;
	int generation = 0;

	std::deque<AVPacket *> audioPackets;
	bool audioEof = false;
	bool audioDrained = false;
	float volume = 1.0f;

	// Demux thread only, except the audio context and resampler, which the audio thread owns once it starts.
	AVFormatContext *fmt = nullptr;
	int videoStream = -1;
	int audioStream = -1;
	AVCodecContext *videoCtx = nullptr;
	AVCodecContext *audioCtx = nullptr;
	SwsContext *sws = nullptr;
	SwrContext *swr = nullptr;
	AVBSFContext *bsf = nullptr;
	AVFrame *videoFrame = nullptr;
	double lastVideoPts = 0.0;
	bool needKeyframe = false;

	bool mvdInited = false;
	bool mvdActive = false;
	bool mvdFailed = false;
	int mvdPackets = 0;
	int mvdErrors = 0;
	int mvdFrames = 0;
	u8 *mvdIn = nullptr;
	u8 *mvdOut = nullptr;
	int mvdW = 0;
	int mvdH = 0;
	MVDSTD_Config mvdConfig;
	std::vector<int> scaleCols;

	ndspWaveBuf waveBufs[WAVEBUF_COUNT];
	int16_t *waveData = nullptr;
	int sampleRate = 0;

	~Impl() { stop(); }

	void stop() {
		stopping = true;
		wake.notify_all();
		if (demuxThread.joinable()) {
			demuxThread.join();
		}
	}

	double clockLocked() const {
		if (!clockRunning || isPaused) {
			return clockBase;
		}
		return clockBase + (wallSeconds() - clockStart);
	}

	void fail(const std::string &message) {
		Logger::log("[Video] %s", message.c_str());
		std::lock_guard<std::mutex> lock(mutex);
		state = PlayerState::FAILED;
		error = message;
		Utils::requestRedraw();
	}

	// ---- opening ----

	bool openSoftwareVideo(AVStream *stream) {
		const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
		if (!codec) {
			return false;
		}
		videoCtx = avcodec_alloc_context3(codec);
		if (!videoCtx || avcodec_parameters_to_context(videoCtx, stream->codecpar) < 0) {
			return false;
		}
		videoCtx->pkt_timebase = stream->time_base;
		videoCtx->thread_count = 1;
		videoCtx->flags2 |= AV_CODEC_FLAG2_FAST;
		// Deblocking is a large share of H.264 decoding, and the Old 3DS cannot spare it.
		videoCtx->skip_loop_filter = Utils::System::isNew3DS() ? AVDISCARD_NONREF : AVDISCARD_ALL;
		if (avcodec_open2(videoCtx, codec, nullptr) < 0) {
			avcodec_free_context(&videoCtx);
			return false;
		}
		info.hardware = false;
		return true;
	}

	bool openMvd(AVStream *stream) {
		const AVCodecParameters *par = stream->codecpar;
		mvdW = (par->width + 15) & ~15;
		mvdH = (par->height + 15) & ~15;
		if (mvdW <= 0 || mvdH <= 0 || mvdW > 2048 || mvdH > 2048) {
			return false;
		}

		const double pixels = (double)mvdW * mvdH;
		std::vector<u32> sizes;
		if (pixels > 1280.0 * 736.0) {
			sizes.push_back((u32)(MVD_DEFAULT_WORKBUF_SIZE * std::min(2.5, pixels / (1280.0 * 720.0))));
		}
		sizes.push_back(MVD_DEFAULT_WORKBUF_SIZE);

		Result res = -1;
		for (u32 size : sizes) {
			res = mvdstdInit(MVDMODE_VIDEOPROCESSING, MVD_INPUT_H264, MVD_OUTPUT_BGR565, size, nullptr);
			if (R_SUCCEEDED(res)) {
				Logger::log("[Video] MVD up with a %lu KB work buffer", (unsigned long)(size / 1024));
				break;
			}
		}
		if (R_FAILED(res)) {
			Logger::log("[Video] mvdstdInit failed: 0x%08lx", (unsigned long)res);
			return false;
		}
		mvdInited = true;

		mvdIn = (u8 *)linearMemAlign(MVD_INPUT_SIZE, 0x40);
		mvdOut = (u8 *)linearMemAlign((size_t)mvdW * mvdH * 2, 0x40);
		const AVBitStreamFilter *filter = av_bsf_get_by_name("h264_mp4toannexb");
		if (!mvdIn || !mvdOut || !filter || av_bsf_alloc(filter, &bsf) < 0 ||
		    avcodec_parameters_copy(bsf->par_in, par) < 0) {
			closeMvd();
			return false;
		}
		bsf->time_base_in = stream->time_base;
		if (av_bsf_init(bsf) < 0) {
			closeMvd();
			return false;
		}

		memset(&mvdConfig, 0, sizeof(mvdConfig));
		mvdstdGenerateDefaultConfig(&mvdConfig, mvdW, mvdH, mvdW, mvdH, nullptr, (u32 *)mvdOut, (u32 *)mvdOut);
		mvdActive = true;
		mvdFailed = false;
		mvdErrors = 0;
		mvdFrames = 0;
		mvdPackets = 0;
		info.hardware = true;
		return true;
	}

	void closeMvd() {
		if (mvdInited) {
			mvdstdExit();
			mvdInited = false;
		}
		mvdActive = false;
		if (bsf) {
			av_bsf_free(&bsf);
		}
		if (mvdIn) {
			linearFree(mvdIn);
			mvdIn = nullptr;
		}
		if (mvdOut) {
			linearFree(mvdOut);
			mvdOut = nullptr;
		}
	}

	bool openAudio(AVStream *stream) {
		const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
		if (!codec) {
			return false;
		}
		audioCtx = avcodec_alloc_context3(codec);
		if (!audioCtx || avcodec_parameters_to_context(audioCtx, stream->codecpar) < 0) {
			avcodec_free_context(&audioCtx);
			return false;
		}
		audioCtx->pkt_timebase = stream->time_base;
		if (avcodec_open2(audioCtx, codec, nullptr) < 0 || audioCtx->sample_rate <= 0) {
			avcodec_free_context(&audioCtx);
			return false;
		}
		sampleRate = audioCtx->sample_rate;

		AVChannelLayout mono;
		av_channel_layout_default(&mono, 1);
		if (swr_alloc_set_opts2(&swr, &mono, AV_SAMPLE_FMT_S16, sampleRate, &audioCtx->ch_layout,
		                        audioCtx->sample_fmt, sampleRate, 0, nullptr) < 0 ||
		    swr_init(swr) < 0) {
			swr_free(&swr);
			avcodec_free_context(&audioCtx);
			return false;
		}

		waveData = (int16_t *)linearAlloc(sizeof(int16_t) * WAVEBUF_SAMPLES * WAVEBUF_COUNT);
		if (!waveData) {
			swr_free(&swr);
			avcodec_free_context(&audioCtx);
			return false;
		}
		memset(waveBufs, 0, sizeof(waveBufs));
		for (int i = 0; i < WAVEBUF_COUNT; i++) {
			waveBufs[i].data_pcm16 = waveData + i * WAVEBUF_SAMPLES;
			waveBufs[i].status = NDSP_WBUF_DONE;
		}

		ndspChnReset(AUDIO_CHANNEL);
		ndspChnSetInterp(AUDIO_CHANNEL, NDSP_INTERP_LINEAR);
		ndspChnSetRate(AUDIO_CHANNEL, (float)sampleRate);
		ndspChnSetFormat(AUDIO_CHANNEL, NDSP_FORMAT_MONO_PCM16);
		applyVolume();
		return true;
	}

	void applyVolume() {
		float mix[12];
		memset(mix, 0, sizeof(mix));
		mix[0] = volume;
		mix[1] = volume;
		ndspChnSetMix(AUDIO_CHANNEL, mix);
	}

	bool open() {
		av_log_set_level(AV_LOG_QUIET);

		// "sdmc:" would otherwise be taken for a protocol name.
		const std::string url = "file:" + path;
		if (avformat_open_input(&fmt, url.c_str(), nullptr, nullptr) < 0) {
			fail("Could not open the video file.");
			return false;
		}
		if (avformat_find_stream_info(fmt, nullptr) < 0) {
			fail("Could not read the video file.");
			return false;
		}

		videoStream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
		audioStream = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
		if (videoStream < 0 && audioStream < 0) {
			fail("This file has no video or audio this player can read.");
			return false;
		}

		VideoInfo opened;
		if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0) {
			opened.duration = fmt->duration / (double)AV_TIME_BASE;
		}

		std::string videoProblem;
		if (videoStream >= 0) {
			AVStream *stream = fmt->streams[videoStream];
			const AVCodecParameters *par = stream->codecpar;
			opened.width = par->width;
			opened.height = par->height;
			opened.videoCodec = avcodec_get_name(par->codec_id);
			opened.frameRate = stream->avg_frame_rate.den > 0 ? av_q2d(stream->avg_frame_rate) : 0.0;

			const AVPacketSideData *matrix = av_packet_side_data_get(par->coded_side_data, par->nb_coded_side_data,
			                                                         AV_PKT_DATA_DISPLAYMATRIX);
			if (matrix && matrix->size >= 9 * sizeof(int32_t)) {
				opened.rotation = roundRotation(av_display_rotation_get((const int32_t *)matrix->data));
			}

			bool ok = false;
			if (allowHardware && par->codec_id == AV_CODEC_ID_H264 && VideoDecoder::hardwareAvailable()) {
				ok = openMvd(stream);
			}
			if (!ok) {
				ok = openSoftwareVideo(stream);
			}
			if (!ok || par->width <= 0 || par->height <= 0) {
				videoProblem = "Unsupported video format (" + opened.videoCodec + ").";
				videoStream = -1;
			}
		}

		if (videoStream >= 0) {
			const AVCodecParameters *par = fmt->streams[videoStream]->codecpar;
			double width = par->width;
			const AVRational sar = av_guess_sample_aspect_ratio(fmt, fmt->streams[videoStream], nullptr);
			if (sar.num > 0 && sar.den > 0) {
				width = width * sar.num / sar.den;
			}
			const bool sideways = opened.rotation == 90 || opened.rotation == 270;
			fitFrame(width, par->height, sideways ? SCREEN_H : SCREEN_W, sideways ? SCREEN_W : SCREEN_H,
			         opened.frameWidth, opened.frameHeight);
			opened.hasVideo = true;
			opened.hardware = info.hardware;
		}

		if (audioStream >= 0) {
			AVStream *stream = fmt->streams[audioStream];
			opened.audioCodec = avcodec_get_name(stream->codecpar->codec_id);
			if (Utils::SoundPlayer::getInstance().isReady() && openAudio(stream)) {
				opened.hasAudio = true;
			} else {
				audioStream = -1;
			}
		}

		if (videoStream < 0 && audioStream < 0) {
			fail(videoProblem.empty() ? "Unsupported video format." : videoProblem);
			return false;
		}

		for (unsigned i = 0; i < fmt->nb_streams; i++) {
			fmt->streams[i]->discard =
			    ((int)i == videoStream || (int)i == audioStream) ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
		}

		videoFrame = av_frame_alloc();
		{
			std::lock_guard<std::mutex> lock(mutex);
			info = opened;
			state = PlayerState::PLAYING;
			clockBase = startAt;
		}
		Logger::log("[Video] %dx%d %s%s, audio %s, %.1fs, rotation %d", opened.width, opened.height,
		            opened.videoCodec.c_str(), opened.hardware ? " (hardware)" : "", opened.audioCodec.c_str(),
		            opened.duration, opened.rotation);
		Utils::requestRedraw();
		return true;
	}

	void closeAll() {
		closeMvd();
		if (videoCtx) {
			avcodec_free_context(&videoCtx);
		}
		if (audioCtx) {
			avcodec_free_context(&audioCtx);
		}
		if (swr) {
			swr_free(&swr);
		}
		if (sws) {
			sws_freeContext(sws);
			sws = nullptr;
		}
		if (videoFrame) {
			av_frame_free(&videoFrame);
		}
		if (fmt) {
			avformat_close_input(&fmt);
		}
		if (waveData) {
			ndspChnWaveBufClear(AUDIO_CHANNEL);
			linearFree(waveData);
			waveData = nullptr;
		}
		std::lock_guard<std::mutex> lock(mutex);
		for (AVPacket *packet : audioPackets) {
			av_packet_free(&packet);
		}
		audioPackets.clear();
	}

	// ---- frames ----

	bool hasFreeBuffer() {
		std::lock_guard<std::mutex> lock(mutex);
		return !freeBuffers.empty() || buffersMade < FRAME_SLOTS;
	}

	// Waits for somewhere to put a frame of this pts. Empty when the frame is late or playback moved on.
	std::vector<uint16_t> reserveFrame(double pts, int seekGeneration) {
		std::unique_lock<std::mutex> lock(mutex);
		if (clockRunning && !isPaused && pts < clockLocked() - LATE_DROP) {
			return {};
		}
		wake.wait(lock, [&] {
			return stopping || seekPending || generation != seekGeneration || !freeBuffers.empty() ||
			       buffersMade < FRAME_SLOTS;
		});
		if (stopping || seekPending || generation != seekGeneration) {
			return {};
		}
		if (!freeBuffers.empty()) {
			std::vector<uint16_t> buffer = std::move(freeBuffers.back());
			freeBuffers.pop_back();
			return buffer;
		}
		buffersMade++;
		return std::vector<uint16_t>((size_t)info.frameWidth * info.frameHeight);
	}

	void publishFrame(std::vector<uint16_t> &&pixels, double pts, int seekGeneration) {
		std::lock_guard<std::mutex> lock(mutex);
		if (generation != seekGeneration) {
			freeBuffers.push_back(std::move(pixels));
			return;
		}
		if (!clockRunning) {
			clockRunning = true;
			clockBase = pts;
			clockStart = wallSeconds();
		}
		readyFrames.push_back({std::move(pixels), pts});
		Utils::requestRedraw();
	}

	double lateness(double pts) {
		std::lock_guard<std::mutex> lock(mutex);
		if (!clockRunning || isPaused || !std::isfinite(pts)) {
			return 0.0;
		}
		return clockLocked() - pts;
	}

	void outputSoftwareFrame(AVFrame *frame, int seekGeneration) {
		double pts = toSeconds(frame->best_effort_timestamp, fmt->streams[videoStream]->time_base);
		if (!std::isfinite(pts)) {
			const double step = info.frameRate > 0 ? 1.0 / info.frameRate : 1.0 / 30.0;
			pts = lastVideoPts + step;
		}
		lastVideoPts = pts;

		const double late = lateness(pts);
		videoCtx->skip_frame = late > LATE_SKIP_NONREF ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;

		std::vector<uint16_t> pixels = reserveFrame(pts, seekGeneration);
		if (pixels.empty()) {
			return;
		}
		const int outW = info.frameWidth;
		const int outH = info.frameHeight;
		const int flags = Utils::System::isNew3DS() ? SWS_FAST_BILINEAR : SWS_POINT;
		sws = sws_getCachedContext(sws, frame->width, frame->height, (AVPixelFormat)frame->format, outW, outH,
		                           AV_PIX_FMT_RGB565, flags, nullptr, nullptr, nullptr);
		if (!sws) {
			std::lock_guard<std::mutex> lock(mutex);
			freeBuffers.push_back(std::move(pixels));
			return;
		}
		uint8_t *dst[4] = {(uint8_t *)pixels.data(), nullptr, nullptr, nullptr};
		int dstStride[4] = {outW * 2, 0, 0, 0};
		sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
		publishFrame(std::move(pixels), pts, seekGeneration);
	}

	void decodeSoftware(AVPacket *packet, int seekGeneration) {
		if (avcodec_send_packet(videoCtx, packet) < 0 && packet) {
			return;
		}
		while (!stopping) {
			const int res = avcodec_receive_frame(videoCtx, videoFrame);
			if (res < 0) {
				break;
			}
			outputSoftwareFrame(videoFrame, seekGeneration);
			av_frame_unref(videoFrame);
		}
	}

	// Nearest-neighbour shrink of the decoded MVD frame into the output size.
	void outputMvdFrame(double pts, int seekGeneration) {
		std::vector<uint16_t> pixels = reserveFrame(pts, seekGeneration);
		if (pixels.empty()) {
			return;
		}
		const AVCodecParameters *par = fmt->streams[videoStream]->codecpar;
		const int srcW = std::min(par->width, mvdW);
		const int srcH = std::min(par->height, mvdH);
		const int outW = info.frameWidth;
		const int outH = info.frameHeight;
		if ((int)scaleCols.size() != outW) {
			scaleCols.resize(outW);
			for (int x = 0; x < outW; x++) {
				scaleCols[x] = std::min(srcW - 1, (int)((int64_t)x * srcW / outW));
			}
		}
		GSPGPU_InvalidateDataCache(mvdOut, (u32)mvdW * mvdH * 2);
		const uint16_t *src = (const uint16_t *)mvdOut;
		for (int y = 0; y < outH; y++) {
			const uint16_t *row = src + (size_t)std::min(srcH - 1, (int)((int64_t)y * srcH / outH)) * mvdW;
			uint16_t *out = pixels.data() + (size_t)y * outW;
			for (int x = 0; x < outW; x++) {
				out[x] = row[scaleCols[x]];
			}
		}
		publishFrame(std::move(pixels), pts, seekGeneration);
	}

	void feedMvd(const AVPacket *packet, int seekGeneration) {
		const uint8_t *data = packet->data;
		const int size = packet->size;
		bool frameReady = false;

		// Walk the Annex B start codes; each NAL unit goes to MVD on its own, prefixed with 00 00 01.
		int pos = 0;
		while (pos + 3 <= size) {
			int start = -1;
			for (int i = pos; i + 3 <= size; i++) {
				if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
					start = i + 3;
					break;
				}
			}
			if (start < 0) {
				break;
			}
			int end = size;
			for (int i = start; i + 3 <= size; i++) {
				if (data[i] == 0 && data[i + 1] == 0 && (data[i + 2] == 1 || (data[i + 2] == 0 && i + 3 < size &&
				                                                              data[i + 3] == 1))) {
					end = i;
					break;
				}
			}
			pos = end;
			const int nalSize = end - start;
			if (nalSize <= 0) {
				continue;
			}
			const int type = data[start] & 0x1F;
			if (type != 1 && type != 5 && type != 7 && type != 8) {
				continue;
			}
			if ((size_t)nalSize + 3 > MVD_INPUT_SIZE) {
				mvdErrors++;
				continue;
			}
			mvdIn[0] = 0;
			mvdIn[1] = 0;
			mvdIn[2] = 1;
			memcpy(mvdIn + 3, data + start, nalSize);
			GSPGPU_FlushDataCache(mvdIn, nalSize + 3);
			const Result res = mvdstdProcessVideoFrame(mvdIn, nalSize + 3, 0, nullptr);
			if (!MVD_CHECKNALUPROC_SUCCESS(res)) {
				Logger::log("[Video] MVD rejected NAL type %d: 0x%08lx", type, (unsigned long)res);
				mvdErrors++;
				continue;
			}
			if ((type == 1 || type == 5) && res != MVD_STATUS_PARAMSET && res != MVD_STATUS_INCOMPLETEPROCESSING) {
				frameReady = true;
			}
		}

		if (frameReady) {
			const Result res = mvdstdRenderVideoFrame(&mvdConfig, true);
			if (res != MVD_STATUS_OK) {
				Logger::log("[Video] MVD render failed: 0x%08lx", (unsigned long)res);
				mvdErrors++;
			} else {
				mvdFrames++;
				const AVRational tb = fmt->streams[videoStream]->time_base;
				double pts = toSeconds(packet->dts != AV_NOPTS_VALUE ? packet->dts : packet->pts, tb);
				if (!std::isfinite(pts)) {
					pts = lastVideoPts + (info.frameRate > 0 ? 1.0 / info.frameRate : 1.0 / 30.0);
				}
				lastVideoPts = pts;
				outputMvdFrame(pts, seekGeneration);
			}
		}
		if (mvdErrors >= MVD_MAX_ERRORS && mvdFrames == 0) {
			mvdFailed = true;
		}
	}

	void decodeHardware(AVPacket *packet, int seekGeneration) {
		// Accepting NAL units but never producing a picture counts as failing too.
		if (++mvdPackets > 90 && mvdFrames == 0) {
			mvdFailed = true;
			return;
		}
		if (av_bsf_send_packet(bsf, packet) < 0) {
			return;
		}
		AVPacket *filtered = av_packet_alloc();
		while (!stopping && av_bsf_receive_packet(bsf, filtered) == 0) {
			feedMvd(filtered, seekGeneration);
			av_packet_unref(filtered);
		}
		av_packet_free(&filtered);
	}

	// ---- control ----

	void clearQueues(std::deque<AVPacket *> &videoPackets) {
		for (AVPacket *packet : videoPackets) {
			av_packet_free(&packet);
		}
		videoPackets.clear();
	}

	void performSeek(double target, std::deque<AVPacket *> &videoPackets) {
		target = std::max(0.0, target);
		const int64_t ts = (int64_t)(target * AV_TIME_BASE);
		if (av_seek_frame(fmt, -1, ts, AVSEEK_FLAG_BACKWARD) < 0) {
			av_seek_frame(fmt, -1, ts, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
		}
		if (videoCtx) {
			avcodec_flush_buffers(videoCtx);
			videoCtx->skip_frame = AVDISCARD_DEFAULT;
		}
		if (bsf) {
			av_bsf_flush(bsf);
		}
		clearQueues(videoPackets);
		needKeyframe = false;
		lastVideoPts = target;

		std::lock_guard<std::mutex> lock(mutex);
		for (AVPacket *packet : audioPackets) {
			av_packet_free(&packet);
		}
		audioPackets.clear();
		for (Frame &frame : readyFrames) {
			freeBuffers.push_back(std::move(frame.pixels));
		}
		readyFrames.clear();
		clockRunning = false;
		clockBase = target;
		generation++;
		audioEof = false;
		audioDrained = false;
		if (state == PlayerState::ENDED) {
			state = PlayerState::PLAYING;
		}
		wake.notify_all();
	}

	void switchToSoftware(std::deque<AVPacket *> &videoPackets) {
		Logger::log("[Video] Hardware decoding failed, switching to software");
		closeMvd();
		AVStream *stream = fmt->streams[videoStream];
		if (!openSoftwareVideo(stream)) {
			fail("Unsupported video format (" + info.videoCodec + ").");
			return;
		}
		{
			std::lock_guard<std::mutex> lock(mutex);
			info.hardware = false;
		}
		performSeek(lastVideoPts > 0.5 ? lastVideoPts - 0.5 : 0.0, videoPackets);
	}

	void demuxMain() {
		if (!open()) {
			closeAll();
			return;
		}
		if (audioCtx) {
			// Relative to this thread, which sits two below the UI: the audio thread goes one above it.
			audioThread.start([this] { audioMain(); }, -3, 64 * 1024, false);
		}

		std::deque<AVPacket *> videoPackets;
		AVPacket *packet = av_packet_alloc();
		bool eof = false;
		bool videoFlushed = false;

		if (startAt > 0.0) {
			performSeek(startAt, videoPackets);
		}

		while (!stopping) {
			bool doSeek = false;
			double target = 0.0;
			int seekGeneration = 0;
			{
				std::lock_guard<std::mutex> lock(mutex);
				if (seekPending) {
					seekPending = false;
					doSeek = true;
					target = seekTarget;
				}
				seekGeneration = generation;
				if (state == PlayerState::FAILED) {
					break;
				}
			}
			if (doSeek) {
				performSeek(target, videoPackets);
				eof = false;
				videoFlushed = false;
				continue;
			}
			if (mvdFailed) {
				switchToSoftware(videoPackets);
				eof = false;
				videoFlushed = false;
				continue;
			}

			bool worked = false;
			size_t audioQueued = 0;
			{
				std::lock_guard<std::mutex> lock(mutex);
				audioQueued = audioPackets.size();
			}
			if (!eof && videoPackets.size() < MAX_VIDEO_PACKETS && audioQueued < MAX_AUDIO_PACKETS) {
				const int res = av_read_frame(fmt, packet);
				if (res < 0) {
					eof = true;
					std::lock_guard<std::mutex> lock(mutex);
					audioEof = true;
					wake.notify_all();
				} else if (packet->stream_index == videoStream) {
					videoPackets.push_back(av_packet_clone(packet));
				} else if (packet->stream_index == audioStream) {
					std::lock_guard<std::mutex> lock(mutex);
					audioPackets.push_back(av_packet_clone(packet));
					wake.notify_all();
				}
				av_packet_unref(packet);
				worked = true;
			}

			if (videoStream >= 0 && hasFreeBuffer()) {
				if (!videoPackets.empty()) {
					AVPacket *next = videoPackets.front();
					videoPackets.pop_front();
					const bool key = next->flags & AV_PKT_FLAG_KEY;
					const AVRational tb = fmt->streams[videoStream]->time_base;
					const double when = toSeconds(next->dts != AV_NOPTS_VALUE ? next->dts : next->pts, tb);
					if (!info.hardware && !key && lateness(when) > LATE_SKIP_TO_KEY) {
						needKeyframe = true;
					}
					if (needKeyframe && !key) {
						// Too far behind to catch up frame by frame.
					} else {
						needKeyframe = false;
						if (mvdActive) {
							decodeHardware(next, seekGeneration);
						} else if (videoCtx) {
							decodeSoftware(next, seekGeneration);
						}
					}
					av_packet_free(&next);
					worked = true;
				} else if (eof && !videoFlushed) {
					if (videoCtx && !mvdActive) {
						decodeSoftware(nullptr, seekGeneration);
					}
					videoFlushed = true;
					worked = true;
				}
			}

			if (eof && videoPackets.empty() && (videoStream < 0 || videoFlushed)) {
				bool finished = false;
				{
					std::lock_guard<std::mutex> lock(mutex);
					finished = readyFrames.empty() && (!audioCtx || audioDrained);
				}
				if (finished) {
					if (loop) {
						performSeek(0.0, videoPackets);
						if (videoCtx) {
							// A drained decoder only takes packets again after a flush.
							avcodec_flush_buffers(videoCtx);
						}
						eof = false;
						videoFlushed = false;
						continue;
					}
					std::unique_lock<std::mutex> lock(mutex);
					if (state != PlayerState::ENDED) {
						state = PlayerState::ENDED;
						Utils::requestRedraw();
					}
					wake.wait_for(lock, std::chrono::milliseconds(50), [&] { return stopping || seekPending; });
					continue;
				}
			}

			if (!worked) {
				std::unique_lock<std::mutex> lock(mutex);
				wake.wait_for(lock, std::chrono::milliseconds(5));
			}
		}

		av_packet_free(&packet);
		clearQueues(videoPackets);
		stopping = true;
		wake.notify_all();
		if (audioThread.joinable()) {
			audioThread.join();
		}
		closeAll();
	}

	// ---- audio ----

	double queuedAudioSeconds() {
		int samples = 0;
		for (int i = 0; i < WAVEBUF_COUNT; i++) {
			if (waveBufs[i].status == NDSP_WBUF_QUEUED || waveBufs[i].status == NDSP_WBUF_PLAYING) {
				samples += waveBufs[i].nsamples;
			}
		}
		samples -= (int)ndspChnGetSamplePos(AUDIO_CHANNEL);
		return std::max(0, samples) / (double)sampleRate;
	}

	bool audioInterrupted(int localGeneration) {
		std::lock_guard<std::mutex> lock(mutex);
		return stopping || generation != localGeneration;
	}

	// Copies samples into wave buffers as they free up, submitting each one once full.
	void queueSamples(const int16_t *samples, int count, int &current, int &fill, int localGeneration) {
		while (count > 0) {
			if (current < 0) {
				for (int tries = 0; current < 0; tries++) {
					for (int i = 0; i < WAVEBUF_COUNT; i++) {
						const int candidate = (i + tries) % WAVEBUF_COUNT;
						if (waveBufs[candidate].status == NDSP_WBUF_DONE ||
						    waveBufs[candidate].status == NDSP_WBUF_FREE) {
							current = candidate;
							break;
						}
					}
					if (current >= 0) {
						break;
					}
					if (audioInterrupted(localGeneration)) {
						return;
					}
					svcSleepThread(4 * 1000000LL);
				}
				fill = 0;
			}
			const int room = WAVEBUF_SAMPLES - fill;
			const int take = std::min(room, count);
			memcpy(waveBufs[current].data_pcm16 + fill, samples, take * sizeof(int16_t));
			fill += take;
			samples += take;
			count -= take;
			if (fill == WAVEBUF_SAMPLES) {
				submitWaveBuf(current, fill);
				current = -1;
				fill = 0;
			}
		}
	}

	void submitWaveBuf(int index, int samples) {
		waveBufs[index].nsamples = samples;
		DSP_FlushDataCache(waveBufs[index].data_pcm16, samples * sizeof(int16_t));
		ndspChnWaveBufAdd(AUDIO_CHANNEL, &waveBufs[index]);
	}

	void audioMain() {
		AVFrame *frame = av_frame_alloc();
		std::vector<int16_t> converted;
		int localGeneration = -1;
		int current = -1;
		int fill = 0;
		const AVRational tb = fmt->streams[audioStream]->time_base;

		while (true) {
			AVPacket *packet = nullptr;
			bool flush = false;
			bool drain = false;
			{
				std::unique_lock<std::mutex> lock(mutex);
				wake.wait_for(lock, std::chrono::milliseconds(20), [&] {
					return stopping || generation != localGeneration || !audioPackets.empty() ||
					       (audioEof && !audioDrained);
				});
				if (stopping) {
					break;
				}
				if (generation != localGeneration) {
					localGeneration = generation;
					flush = true;
				} else if (!audioPackets.empty()) {
					packet = audioPackets.front();
					audioPackets.pop_front();
					wake.notify_all();
				} else if (audioEof && !audioDrained) {
					drain = true;
				}
			}

			if (flush) {
				ndspChnWaveBufClear(AUDIO_CHANNEL);
				for (int i = 0; i < WAVEBUF_COUNT; i++) {
					waveBufs[i].status = NDSP_WBUF_DONE;
				}
				avcodec_flush_buffers(audioCtx);
				swr_close(swr);
				swr_init(swr);
				current = -1;
				fill = 0;
				continue;
			}
			if (drain) {
				if (current >= 0 && fill > 0) {
					submitWaveBuf(current, fill);
					current = -1;
					fill = 0;
				}
				if (queuedAudioSeconds() <= 0.0) {
					std::lock_guard<std::mutex> lock(mutex);
					audioDrained = true;
					wake.notify_all();
				} else {
					svcSleepThread(10 * 1000000LL);
				}
				continue;
			}
			if (!packet) {
				continue;
			}

			if (avcodec_send_packet(audioCtx, packet) >= 0) {
				while (avcodec_receive_frame(audioCtx, frame) >= 0) {
					const double pts = toSeconds(frame->best_effort_timestamp, tb);
					const double length = frame->nb_samples / (double)sampleRate;

					// Audio follows the clock: late frames are dropped, early ones wait their turn.
					bool drop = false;
					while (!audioInterrupted(localGeneration)) {
						double clock = 0.0;
						bool running = false;
						{
							std::lock_guard<std::mutex> lock(mutex);
							running = clockRunning && !isPaused;
							if (!clockRunning && videoStream < 0 && std::isfinite(pts)) {
								clockRunning = true;
								clockBase = pts;
								clockStart = wallSeconds();
								running = !isPaused;
							}
							clock = clockLocked();
						}
						if (!running || !std::isfinite(pts)) {
							// Before the first video frame, or paused: hold the audio back.
							if (!std::isfinite(pts)) {
								break;
							}
							svcSleepThread(5 * 1000000LL);
							continue;
						}
						const double startsAt = clock + queuedAudioSeconds();
						if (pts + length < startsAt - 0.15) {
							drop = true;
							break;
						}
						if (pts > startsAt + 0.15) {
							svcSleepThread(5 * 1000000LL);
							continue;
						}
						break;
					}
					if (audioInterrupted(localGeneration)) {
						break;
					}
					if (drop) {
						continue;
					}

					const int maxOut = swr_get_out_samples(swr, frame->nb_samples);
					if (maxOut <= 0) {
						continue;
					}
					converted.resize(maxOut);
					uint8_t *out[1] = {(uint8_t *)converted.data()};
					const int got =
					    swr_convert(swr, out, maxOut, (const uint8_t **)frame->extended_data, frame->nb_samples);
					if (got > 0) {
						queueSamples(converted.data(), got, current, fill, localGeneration);
					}
				}
			}
			av_packet_free(&packet);
		}

		ndspChnWaveBufClear(AUDIO_CHANNEL);
		av_frame_free(&frame);
	}
};

bool VideoDecoder::available() { return true; }

bool VideoDecoder::hardwareAvailable() { return Utils::System::isNew3DS(); }

VideoDecoder::VideoDecoder() : impl(std::make_unique<Impl>()) {}

VideoDecoder::~VideoDecoder() { stop(); }

void VideoDecoder::start(const std::string &path, double startSeconds, bool allowHardware, bool loop) {
	stop();
	impl = std::make_unique<Impl>();
	impl->path = path;
	impl->startAt = std::max(0.0, startSeconds);
	impl->allowHardware = allowHardware;
	impl->loop = loop;
	Impl *raw = impl.get();
	// FFmpeg's H.264 decoder wants a deep stack.
	impl->demuxThread.start([raw] { raw->demuxMain(); }, 2, 256 * 1024, true);
}

void VideoDecoder::stop() {
	if (impl) {
		impl->stop();
	}
}

PlayerState VideoDecoder::state() const {
	std::lock_guard<std::mutex> lock(impl->mutex);
	return impl->state;
}

std::string VideoDecoder::error() const {
	std::lock_guard<std::mutex> lock(impl->mutex);
	return impl->error;
}

VideoInfo VideoDecoder::info() const {
	std::lock_guard<std::mutex> lock(impl->mutex);
	return impl->info;
}

bool VideoDecoder::consumeFrame(const std::function<void(const uint16_t *, int, int)> &sink) {
	Impl::Frame shown;
	bool have = false;
	int width = 0;
	int height = 0;
	{
		std::lock_guard<std::mutex> lock(impl->mutex);
		const double now = impl->clockLocked();
		while (!impl->readyFrames.empty() && impl->readyFrames.front().pts <= now + 0.005) {
			if (have) {
				impl->freeBuffers.push_back(std::move(shown.pixels));
			}
			shown = std::move(impl->readyFrames.front());
			impl->readyFrames.pop_front();
			have = true;
		}
		width = impl->info.frameWidth;
		height = impl->info.frameHeight;
	}
	if (!have) {
		return false;
	}
	sink(shown.pixels.data(), width, height);
	{
		std::lock_guard<std::mutex> lock(impl->mutex);
		impl->freeBuffers.push_back(std::move(shown.pixels));
	}
	impl->wake.notify_all();
	return true;
}

double VideoDecoder::position() const {
	std::lock_guard<std::mutex> lock(impl->mutex);
	double pos = impl->clockLocked();
	if (impl->info.duration > 0) {
		pos = std::min(pos, impl->info.duration);
	}
	return std::max(0.0, pos);
}

void VideoDecoder::setPaused(bool paused) {
	{
		std::lock_guard<std::mutex> lock(impl->mutex);
		if (impl->isPaused == paused) {
			return;
		}
		if (paused) {
			impl->clockBase = impl->clockLocked();
			impl->isPaused = true;
		} else {
			impl->clockStart = wallSeconds();
			impl->isPaused = false;
		}
	}
	if (impl->waveData) {
		ndspChnSetPaused(AUDIO_CHANNEL, paused);
	}
	impl->wake.notify_all();
}

bool VideoDecoder::paused() const {
	std::lock_guard<std::mutex> lock(impl->mutex);
	return impl->isPaused;
}

void VideoDecoder::seek(double seconds) {
	{
		std::lock_guard<std::mutex> lock(impl->mutex);
		if (impl->info.duration > 0) {
			seconds = std::min(seconds, std::max(0.0, impl->info.duration - 0.5));
		}
		impl->seekPending = true;
		impl->seekTarget = std::max(0.0, seconds);
		// Shown right away, so the bar does not jump back while the seek runs.
		impl->clockRunning = false;
		impl->clockBase = impl->seekTarget;
	}
	impl->wake.notify_all();
}

void VideoDecoder::setVolume(float volume) {
	impl->volume = std::clamp(volume, 0.0f, 2.0f);
	if (impl->waveData) {
		impl->applyVolume();
	}
}

void VideoDecoder::setLoop(bool loop) { impl->loop = loop; }

#endif

} // namespace Media
