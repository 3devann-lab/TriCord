#include "ui/voice_recorder.h"
#include "core/config.h"
#include "core/i18n.h"
#include "discord/voice_client.h"
#include "log.h"
#include "ui/screen_manager.h"
#include "utils/base64_utils.h"
#include "utils/file_utils.h"
#include "utils/message_utils.h"
#include "utils/ogg_opus.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UI {

namespace {

const int FRAMES_PER_SECOND = 50;
const int MAX_FRAMES = VoiceRecorder::MAX_SECONDS * FRAMES_PER_SECOND;
// 20 ms at 48 kHz, the clock Ogg Opus granule positions count in.
const uint32_t FRAME_48K = 960;

} // namespace

VoiceRecorder::VoiceRecorder(SendCallback onSend, PreviewCallback onPreview)
    : onSend(std::move(onSend)), onPreview(std::move(onPreview)) {
	path = std::string(CONFIG_DIR_PATH) + "/cache/voice-message.ogg";
}

VoiceRecorder::~VoiceRecorder() { stopRecording(); }

double VoiceRecorder::seconds() const {
	std::lock_guard<std::mutex> lock(dataMutex);
	return packets.size() / (double)FRAMES_PER_SECOND;
}

void VoiceRecorder::startRecording() {
	Discord::VoiceState voice = Discord::VoiceClient::getInstance().getState();
	if (voice != Discord::VoiceState::DISCONNECTED && voice != Discord::VoiceState::FAILED) {
		failure = TR("voice_msg.in_call");
		state = State::FAILED;
		return;
	}
	{
		std::lock_guard<std::mutex> lock(dataMutex);
		packets.clear();
		peaks.clear();
	}
	if (!capture.start()) {
		failure = TR("voice_msg.mic_failed");
		state = State::FAILED;
		return;
	}
	capturing = true;
	state = State::RECORDING;
	worker.start([this] { captureLoop(); }, -1, 64 * 1024);
}

void VoiceRecorder::stopRecording() {
	if (!capturing.exchange(false) && !worker.joinable()) {
		return;
	}
	if (worker.joinable()) {
		worker.join();
	}
	capture.stop();
}

void VoiceRecorder::captureLoop() {
	int16_t pcm[Discord::VoiceCapture::FRAME_SAMPLES];
	uint8_t encoded[1275];
	while (capturing.load()) {
		uint64_t wait = capture.nanosUntilFrame();
		if (wait > 0) {
			svcSleepThread(std::min<uint64_t>(wait, 5000000ULL));
			continue;
		}
		if (!capture.readFrame(pcm)) {
			svcSleepThread(2000000ULL);
			continue;
		}
		int bytes = capture.encode(pcm, encoded, sizeof(encoded));
		if (bytes <= 0) {
			continue;
		}
		std::lock_guard<std::mutex> lock(dataMutex);
		packets.emplace_back((const char *)encoded, (size_t)bytes);
		peaks.push_back(capture.lastPeak());
		if ((int)packets.size() >= MAX_FRAMES) {
			capturing = false;
		}
	}
}

bool VoiceRecorder::writeFile() {
	std::vector<std::string> copy;
	std::vector<int> levels;
	{
		std::lock_guard<std::mutex> lock(dataMutex);
		copy = packets;
		levels = peaks;
	}
	// The encoder's 6.5 ms lookahead at 16 kHz, in 48 kHz samples.
	std::string file = Utils::OggOpus::build(copy, Discord::VoiceCapture::ENCODE_RATE, 312, FRAME_48K);
	Utils::File::ensureDirectory(Utils::File::parentDirectory(path));
	if (!Utils::File::writeFile(path, file)) {
		return false;
	}
	waveform = Utils::OggOpus::waveform(levels);
	std::vector<unsigned char> bytes = Utils::Base64::decode(waveform);
	waveformBytes.assign(bytes.begin(), bytes.end());
	return true;
}

void VoiceRecorder::update(u32 kDown, u32 kHeld) {
	(void)kHeld;
	pulse += 0.12f;

	// Hitting the two-minute limit stops by itself.
	if (state == State::RECORDING && !capturing.load()) {
		stopRecording();
		state = writeFile() ? State::RECORDED : State::FAILED;
		if (state == State::FAILED) {
			failure = TR("voice_msg.save_failed");
		}
	}

	if (kDown & KEY_B) {
		stopRecording();
		finish();
		return;
	}

	switch (state) {
	case State::READY:
	case State::FAILED:
		if (kDown & KEY_A) {
			startRecording();
		}
		break;
	case State::RECORDING:
		if (kDown & KEY_A) {
			stopRecording();
			if (seconds() < 0.5) {
				state = State::READY;
			} else {
				state = writeFile() ? State::RECORDED : State::FAILED;
				if (state == State::FAILED) {
					failure = TR("voice_msg.save_failed");
				}
			}
		}
		break;
	case State::RECORDED:
		if (kDown & KEY_A) {
			UploadChoice choice;
			choice.path = path;
			choice.voiceMessage = true;
			choice.durationSecs = std::round(seconds() * 100.0) / 100.0;
			choice.waveform = waveform;
			SendCallback send = onSend;
			finish();
			if (send) {
				send(choice);
			}
			return;
		}
		if ((kDown & KEY_X) && onPreview) {
			onPreview(path, waveformBytes);
		}
		if (kDown & KEY_Y) {
			startRecording();
		}
		break;
	}
}

void VoiceRecorder::renderTop() {
	C2D_DrawRectSolid(0, 0, 0.1f, 400, 240, ScreenManager::colorBackground());
	drawOverlayHeader(TR("voice_msg.title"));

	const float cx = 200.0f, cy = 100.0f;
	float level = 0.0f;
	if (state == State::RECORDING) {
		level = std::min(1.0f, std::sqrt(capture.lastPeak() / 32768.0f) * 1.4f);
	}
	u32 accent = state == State::RECORDING ? ScreenManager::colorError() : ScreenManager::colorAccent();
	float ring = 34.0f + level * 18.0f + (state == State::RECORDING ? 2.0f * sinf(pulse) : 0.0f);
	drawCircle(cx, cy, 0.4f, ring, (accent & 0x00FFFFFF) | 0x40000000);
	drawCircle(cx, cy, 0.45f, 30.0f, accent);
	// A simple microphone: capsule, stand and base.
	const u32 white = ScreenManager::colorWhite();
	drawRoundedRect(cx - 7.0f, cy - 17.0f, 0.5f, 14.0f, 22.0f, 7.0f, white);
	C2D_DrawRectSolid(cx - 1.5f, cy + 6.0f, 0.5f, 3.0f, 7.0f, white);
	C2D_DrawRectSolid(cx - 8.0f, cy + 12.0f, 0.5f, 16.0f, 3.0f, white);

	std::string clock = formatDuration(seconds()) + " / " + formatDuration(MAX_SECONDS);
	drawCenteredText(150.0f, 0.5f, 0.55f, 0.55f, ScreenManager::colorText(), clock, 400.0f);

	// The take so far as bars, newest on the right.
	std::vector<int> levels;
	{
		std::lock_guard<std::mutex> lock(dataMutex);
		size_t from = peaks.size() > 240 ? peaks.size() - 240 : 0;
		levels.assign(peaks.begin() + from, peaks.end());
	}
	const int BARS = 60;
	const float barW = 3.0f, gap = 2.5f;
	float x0 = (400.0f - (BARS * barW + (BARS - 1) * gap)) / 2.0f;
	for (int i = 0; i < BARS; i++) {
		int peak = 0;
		size_t from = levels.size() * i / BARS, to = std::max(from + 1, levels.size() * (i + 1) / BARS);
		for (size_t k = from; k < to && k < levels.size(); k++) {
			peak = std::max(peak, levels[k]);
		}
		float h = 3.0f + std::sqrt(peak / 32768.0f) * 36.0f;
		drawRoundedRect(x0 + i * (barW + gap), 196.0f - h / 2.0f, 0.5f, barW, h, 1.5f,
		                levels.empty() ? ScreenManager::colorBackgroundLight() : ScreenManager::colorAccent());
	}
}

void VoiceRecorder::renderBottom() {
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, ScreenManager::colorBackgroundDark());
	std::string message;
	std::string hints;
	switch (state) {
	case State::READY:
		message = TR("voice_msg.ready");
		hints = ": " + TR("voice_msg.record") + "  : " + TR("common.cancel");
		break;
	case State::RECORDING:
		message = TR("voice_msg.recording");
		hints = ": " + TR("voice_msg.stop") + "  : " + TR("common.cancel");
		break;
	case State::RECORDED:
		message = TR("voice_msg.recorded");
		hints = ": " + TR("voice_msg.send") + "  : " + TR("voice_msg.listen") + "  : " +
		        TR("voice_msg.redo") + "  : " + TR("voice_msg.discard");
		break;
	case State::FAILED:
		message = failure;
		hints = ": " + TR("common.retry") + "  : " + TR("common.close");
		break;
	}
	auto lines = MessageUtils::wrapText(message, 290.0f, 0.45f);
	float y = 90.0f - lines.size() * 8.0f;
	for (const auto &line : lines) {
		drawCenteredText(y, 0.5f, 0.45f, 0.45f, state == State::FAILED ? ScreenManager::colorError() : ScreenManager::colorText(),
		                 line, 320.0f);
		y += 16.0f;
	}
	drawKeyHints(hints);
}

} // namespace UI
