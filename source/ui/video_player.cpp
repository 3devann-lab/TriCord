#include "ui/video_player.h"
#include "core/config.h"
#include "core/i18n.h"
#include "log.h"
#include "network/network_manager.h"
#include "ui/image_manager.h"
#include "ui/screen_manager.h"
#include "utils/file_utils.h"
#include "utils/message_utils.h"
#include "utils/redraw.h"
#include "utils/system_utils.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace UI {

namespace {

constexpr int TEX_W = 512;
constexpr int TEX_H = 256;
constexpr float BAR_X = 16.0f;
constexpr float BAR_Y = 150.0f;
constexpr float BAR_W = 288.0f;
constexpr float BAR_H = 8.0f;


// The last remote video, so replaying it skips the download.
std::mutex cacheMutex;
std::string cachedUrl;
std::string cachedPath;

std::string cacheDirectory() { return std::string(CONFIG_DIR_PATH) + "/cache"; }

std::string downloadsDirectory() { return std::string(CONFIG_DIR_PATH) + "/downloads"; }

// Present while hardware decoding runs. Finding it when a player opens means the last one never
// closed, i.e. the app went down mid-video, so hardware decoding is the first suspect.
std::string crashGuardPath() { return cacheDirectory() + "/video.guard"; }

void setCrashGuard(bool on) {
	if (on) {
		Utils::File::ensureDirectory(cacheDirectory());
		Utils::File::writeFile(crashGuardPath(), std::string("hw"));
	} else {
		remove(crashGuardPath().c_str());
	}
}

std::string extensionFor(const std::string &url, const std::string &fileName) {
	std::string ext = Utils::File::extension(fileName);
	if (ext.empty()) {
		ext = Utils::File::extension(url.substr(0, url.find('?')));
	}
	if (ext.empty() || ext.size() > 4) {
		ext = "mp4";
	}
	return ext;
}

void drawPlayIcon(float cx, float cy, float size, u32 color, bool paused) {
	if (paused) {
		C2D_DrawTriangle(cx - size * 0.4f, cy - size * 0.5f, color, cx - size * 0.4f, cy + size * 0.5f, color,
		                 cx + size * 0.5f, cy, color, 0.7f);
	} else {
		C2D_DrawRectSolid(cx - size * 0.4f, cy - size * 0.5f, 0.7f, size * 0.3f, size, color);
		C2D_DrawRectSolid(cx + size * 0.1f, cy - size * 0.5f, 0.7f, size * 0.3f, size, color);
	}
}

} // namespace

VideoPlayer::VideoPlayer(const Source &source) : source(source), saveState(std::make_shared<std::atomic<int>>(0)) {
	hardware = Config::getInstance().isHardwareVideoEnabled();
	volume = Config::getInstance().getVideoVolume() / 100.0f;

	if (Utils::File::exists(crashGuardPath())) {
		Logger::log("[Video] The last hardware-decoded video did not close cleanly; turning hardware decoding off");
		setCrashGuard(false);
		if (hardware) {
			hardware = false;
			Config::getInstance().setHardwareVideoEnabled(false);
			showOsd(TR("video.hw_disabled_after_crash"));
			osdTimer = 300;
		}
	}

	if (!Media::VideoDecoder::available()) {
		stage = Stage::FAILED;
		failure = TR("video.unavailable");
		return;
	}

	if (!source.localPath.empty()) {
		path = source.localPath;
		startPlayback(0.0);
		return;
	}

	{
		std::lock_guard<std::mutex> lock(cacheMutex);
		if (!cachedUrl.empty() && cachedUrl == source.url && Utils::File::exists(cachedPath)) {
			path = cachedPath;
		}
	}
	if (!path.empty()) {
		startPlayback(0.0);
	} else {
		startDownload();
	}
}

VideoPlayer::~VideoPlayer() {
	if (progress) {
		progress->cancel = true;
	}
	decoder.stop();
	setCrashGuard(false);
	if (saveThread.joinable()) {
		saveThread.join();
	}
	if (tex) {
		C3D_TexDelete(tex);
		free(tex);
		tex = nullptr;
	}
}

void VideoPlayer::startDownload() {
	stage = Stage::DOWNLOADING;
	Utils::File::ensureDirectory(cacheDirectory());
	const std::string target = cacheDirectory() + "/video." + extensionFor(source.url, source.fileName);

	{
		// The previous video's file goes, so the cache never holds more than one.
		std::lock_guard<std::mutex> lock(cacheMutex);
		if (!cachedPath.empty() && cachedPath != target) {
			remove(cachedPath.c_str());
		}
		cachedUrl.clear();
		cachedPath.clear();
	}

	progress = std::make_shared<Network::TransferProgress>();
	download = std::make_shared<Download>();
	std::shared_ptr<Download> state = download;
	const std::string url = source.url;
	Network::downloadFile(source.url, target, progress,
	                      [state, url, target](bool success, long, const std::string &error) {
		                      if (success) {
			                      std::lock_guard<std::mutex> lock(cacheMutex);
			                      cachedUrl = url;
			                      cachedPath = target;
		                      }
		                      state->error = error;
		                      state->ok = success;
		                      state->done = true;
		                      Utils::requestRedraw();
	                      });
	path = target;
}

void VideoPlayer::startPlayback(double at) {
	stage = Stage::PLAYING;
	started = true;
	haveFrame = false;
	// MVD's work buffer and the frame texture both come out of linear memory, which the image cache fills.
	ImageManager::getInstance().clearRemote();

	if (!tex) {
		tex = (C3D_Tex *)malloc(sizeof(C3D_Tex));
		if (!tex || !C3D_TexInit(tex, TEX_W, TEX_H, GPU_RGB565)) {
			free(tex);
			tex = nullptr;
			stage = Stage::FAILED;
			failure = TR("video.no_memory");
			return;
		}
		C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
		memset(tex->data, 0, tex->size);
		GSPGPU_FlushDataCache(tex->data, tex->size);
	}

	setCrashGuard(hardware && Media::VideoDecoder::hardwareAvailable());
	decoder.start(path, at, hardware, source.loop);
	decoder.setVolume(volume);
}

void VideoPlayer::uploadFrame(const uint16_t *tiled, int width, int height) {
	if (!tex) {
		return;
	}
	// The decoder already laid the frame out for the GPU; this is a plain copy.
	memcpy(tex->data, tiled, (size_t)TEX_W * TEX_H * sizeof(uint16_t));
	GSPGPU_FlushDataCache(tex->data, tex->size);
	frameW = std::min(width, TEX_W);
	frameH = std::min(height, TEX_H);
	haveFrame = true;
	redraw = true;
}

void VideoPlayer::showOsd(const std::string &text) {
	osdText = text;
	osdTimer = 90;
}

void VideoPlayer::seekBy(double seconds) {
	const double target = std::max(0.0, decoder.position() + seconds);
	decoder.seek(target);
	showOsd((seconds < 0 ? "« " : "» ") + formatDuration(target));
}

void VideoPlayer::save() {
	if (path.empty() || source.url.empty() || saveThread.joinable() || *saveState == 1) {
		return;
	}
	Utils::File::ensureDirectory(downloadsDirectory());
	std::string name = source.fileName.empty() ? Utils::File::baseName(path) : source.fileName;
	const std::string target = Utils::File::uniquePath(downloadsDirectory(), name);
	savedPath = target;
	*saveState = 1;
	std::shared_ptr<std::atomic<int>> state = saveState;
	const std::string from = path;
	saveThread.start(
	    [state, from, target] {
		    *state = Utils::File::copyFile(from, target) ? 2 : 3;
		    Utils::requestRedraw();
	    },
	    4);
}

bool VideoPlayer::progressBarHit(const touchPosition &touch, float &fraction) const {
	if (touch.py < BAR_Y - 14 || touch.py > BAR_Y + BAR_H + 14 || touch.px < BAR_X - 6 || touch.px > BAR_X + BAR_W + 6) {
		return false;
	}
	fraction = std::clamp((touch.px - BAR_X) / BAR_W, 0.0f, 1.0f);
	return true;
}

void VideoPlayer::update(u32 kDown, u32 kHeld) {
	if (osdTimer > 0) {
		osdTimer--;
	}
	if (saveThread.joinable() && *saveState >= 2) {
		saveThread.join();
		ScreenManager::getInstance().showToast(*saveState == 2
		                                           ? Core::I18n::format(TR("video.saved"), savedPath)
		                                           : TR("video.save_failed"));
		*saveState = 0;
	}

	if (kDown & KEY_B) {
		finish();
		return;
	}

	if (stage == Stage::DOWNLOADING) {
		if (download && download->done) {
			if (download->ok) {
				startPlayback(0.0);
			} else {
				stage = Stage::FAILED;
				failure = Core::I18n::format(TR("video.download_failed"), download->error);
			}
		}
		return;
	}
	if (stage == Stage::FAILED) {
		return;
	}

	const Media::PlayerState state = decoder.state();
	if (state == Media::PlayerState::FAILED) {
		stage = Stage::FAILED;
		failure = decoder.error();
		return;
	}

	const Media::VideoInfo info = decoder.info();
	decoder.consumeFrame([this](const uint16_t *tiled, int width, int height) { uploadFrame(tiled, width, height); });

	if (!hdHintShown && info.hasVideo && !info.hardware) {
		hdHintShown = true;
		const int64_t pixels = (int64_t)info.width * info.height;
		if (Media::VideoDecoder::hardwareAvailable() && info.videoCodec == "h264" && pixels > 640LL * 480) {
			showOsd(TR("video.hint_try_hw"));
			osdTimer = 240;
		} else if (!Media::VideoDecoder::hardwareAvailable() && pixels > 640LL * 360) {
			showOsd(TR("video.hint_hd_slow"));
			osdTimer = 240;
		}
	}

	if (kDown & KEY_A) {
		if (state == Media::PlayerState::ENDED) {
			decoder.seek(0.0);
			decoder.setPaused(false);
		} else {
			decoder.setPaused(!decoder.paused());
		}
		showOsd(decoder.paused() ? TR("video.paused") : TR("video.playing"));
	}

	const u32 dirs = KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN;
	u32 step = 0;
	if (kDown & dirs) {
		step = kDown & dirs;
		repeatKey = step;
		repeatTimer = 25;
	} else if (repeatKey && (kHeld & repeatKey)) {
		if (--repeatTimer <= 0) {
			step = repeatKey;
			repeatTimer = 8;
		}
	} else {
		repeatKey = 0;
	}
	if (step & KEY_LEFT) {
		seekBy(-5.0);
	} else if (step & KEY_RIGHT) {
		seekBy(5.0);
	}
	if (step & (KEY_UP | KEY_DOWN)) {
		volume = std::clamp(volume + ((step & KEY_UP) ? 0.1f : -0.1f), 0.0f, 2.0f);
		decoder.setVolume(volume);
		Config::getInstance().setVideoVolume((int)std::lround(volume * 100.0f));
		showOsd(Core::I18n::format(TR("video.volume"), std::to_string((int)std::lround(volume * 100.0f)) + "%"));
	}
	if (kDown & KEY_L) {
		seekBy(-30.0);
	} else if (kDown & KEY_R) {
		seekBy(30.0);
	}

	if ((kDown & KEY_X) && Media::VideoDecoder::hardwareAvailable() && info.videoCodec == "h264") {
		hardware = !hardware;
		Config::getInstance().setHardwareVideoEnabled(hardware);
		const double at = decoder.position();
		decoder.stop();
		startPlayback(at);
		showOsd(hardware ? TR("video.decoder_hw") : TR("video.decoder_sw"));
	}
	if ((kDown & KEY_Y) && !source.url.empty()) {
		save();
	}

	if (kDown & KEY_TOUCH) {
		touchPosition touch;
		hidTouchRead(&touch);
		float fraction = 0.0f;
		if (progressBarHit(touch, fraction) && info.duration > 0) {
			decoder.seek(fraction * info.duration);
			showOsd(formatDuration(fraction * info.duration));
		} else if (touch.py > 60 && touch.py < 130) {
			decoder.setPaused(!decoder.paused());
			showOsd(decoder.paused() ? TR("video.paused") : TR("video.playing"));
		}
	}
}

bool VideoPlayer::isAnimating() const {
	if (stage != Stage::PLAYING || redraw || osdTimer > 0 || !haveFrame) {
		return true;
	}
	// Enough to keep the clock on the bottom screen ticking.
	return osGetTime() - lastClockRedraw > 500;
}

void VideoPlayer::renderTop() {
	redraw = false;
	C2D_DrawRectSolid(0, 0, 0.1f, 400, 240, C2D_Color32(0, 0, 0, 255));

	if (stage == Stage::PLAYING && haveFrame && tex && frameW > 0 && frameH > 0) {
		const float scale = std::min(400.0f / frameW, 240.0f / frameH);
		const float w = frameW * scale;
		const float h = frameH * scale;
		Tex3DS_SubTexture subtex = {(u16)frameW, (u16)frameH, 0.0f, 1.0f, (float)frameW / TEX_W,
		                            1.0f - (float)frameH / TEX_H};
		C2D_Image img = {tex, &subtex};
		C2D_DrawImageAt(img, (400.0f - w) / 2.0f, (240.0f - h) / 2.0f, 0.5f, nullptr, scale, scale);
	} else if (stage == Stage::PLAYING) {
		drawCenteredText(110.0f, 0.5f, 0.5f, 0.5f, C2D_Color32(200, 200, 200, 255), TR("common.loading"), 400.0f);
	} else if (stage == Stage::DOWNLOADING) {
		drawCenteredText(96.0f, 0.5f, 0.5f, 0.5f, C2D_Color32(230, 230, 230, 255), TR("video.downloading"), 400.0f);
		const int64_t done = progress ? progress->done.load() : 0;
		const int64_t total = progress ? progress->total.load() : 0;
		const float fraction = total > 0 ? (float)done / total : 0.0f;
		drawProgressBar(80.0f, 122.0f, 240.0f, 8.0f, fraction, 0.6f);
		std::string amount = Utils::File::formatSize(done);
		if (total > 0) {
			amount += " / " + Utils::File::formatSize(total);
		}
		drawCenteredText(136.0f, 0.5f, 0.42f, 0.42f, C2D_Color32(170, 170, 170, 255), amount, 400.0f);
	} else {
		const auto lines = MessageUtils::wrapText(failure, 360.0f, 0.45f);
		float y = 120.0f - lines.size() * 8.0f;
		for (const auto &line : lines) {
			drawCenteredText(y, 0.5f, 0.45f, 0.45f, ScreenManager::colorError(), line, 400.0f);
			y += 16.0f;
		}
	}

	if (osdTimer > 0 && !osdText.empty()) {
		const float w = measureText(osdText, 0.45f, 0.45f) + 20.0f;
		drawRoundedRect((400.0f - w) / 2.0f, 10.0f, 0.8f, w, 22.0f, 11.0f, C2D_Color32(0, 0, 0, 170));
		drawCenteredText(14.0f, 0.85f, 0.45f, 0.45f, C2D_Color32(255, 255, 255, 255), osdText, 400.0f);
	}
}

void VideoPlayer::renderBottom() {
	lastClockRedraw = osGetTime();
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, ScreenManager::colorBackgroundDark());
	const std::string title = source.title.empty() ? Utils::File::baseName(path) : source.title;
	drawText(10.0f, 8.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorText(), getTruncatedText(title, 300.0f, 0.5f, 0.5f));
	C2D_DrawRectSolid(10, 30, 0.5f, 300, 1, ScreenManager::colorSeparator());

	if (stage != Stage::PLAYING) {
		std::string hints = ": " + TR("common.close");
		drawKeyHints(hints);
		return;
	}

	const Media::VideoInfo info = decoder.info();
	const double position = decoder.position();
	const bool paused = decoder.paused() || decoder.state() == Media::PlayerState::ENDED;

	drawCircle(160.0f, 95.0f, 0.6f, 26.0f, ScreenManager::colorBackgroundLight());
	drawPlayIcon(160.0f, 95.0f, 22.0f, ScreenManager::colorText(), paused);

	const float fraction = info.duration > 0 ? (float)(position / info.duration) : 0.0f;
	drawProgressBar(BAR_X, BAR_Y, BAR_W, BAR_H, fraction, 0.6f);
	drawCircle(BAR_X + BAR_W * std::clamp(fraction, 0.0f, 1.0f), BAR_Y + BAR_H / 2.0f, 0.65f, 6.0f,
	           ScreenManager::colorWhite());
	drawText(BAR_X, BAR_Y + 14.0f, 0.6f, 0.42f, 0.42f, ScreenManager::colorText(), formatDuration(position));
	const std::string total = info.duration > 0 ? formatDuration(info.duration) : "--:--";
	drawText(BAR_X + BAR_W - measureText(total, 0.42f, 0.42f), BAR_Y + 14.0f, 0.6f, 0.42f, 0.42f,
	         ScreenManager::colorText(), total);

	std::string details;
	if (info.hasVideo) {
		details = std::to_string(info.width) + "×" + std::to_string(info.height) + " " + info.videoCodec + " (" +
		          (info.hardware ? TR("video.hw_short") : TR("video.sw_short")) + ")";
	} else if (decoder.state() != Media::PlayerState::OPENING) {
		details = TR("video.audio_only");
	}
	if (info.hasAudio) {
		details += (details.empty() ? "" : "  ·  ") + info.audioCodec + " " +
		           std::to_string((int)std::lround(volume * 100.0f)) + "%";
	}
	drawText(10.0f, 36.0f, 0.6f, 0.38f, 0.38f, ScreenManager::colorTextMuted(),
	         getTruncatedText(details, 300.0f, 0.38f, 0.38f));

	std::string hints = ": " + TR("video.play_pause") + "  : ±5s  : " +
	                    TR("video.volume_short");
	if (!source.url.empty()) {
		hints += "  : " + TR("common.save");
	}
	if (Media::VideoDecoder::hardwareAvailable() && info.videoCodec == "h264") {
		hints += "  : " + (info.hardware ? TR("video.decoder_sw_short") : TR("video.decoder_hw_short"));
	}
	drawText(10.0f, 196.0f, 0.6f, 0.36f, 0.36f, ScreenManager::colorTextMuted(),
	         getTruncatedText("L/R: ±30s", 300.0f, 0.36f, 0.36f));
	drawKeyHints(hints + "  : " + TR("common.close"));
}

} // namespace UI
