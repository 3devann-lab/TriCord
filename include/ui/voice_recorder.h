#pragma once

#include "discord/voice_capture.h"
#include "ui/overlay.h"
#include "ui/upload_confirm.h"
#include "utils/worker_thread.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace UI {

// Records a voice message from the microphone and hands it over as an Ogg Opus file
// with Discord's waveform. A records and stops; afterwards X listens, A sends, Y redoes.
class VoiceRecorder : public Overlay {
  public:
	using SendCallback = std::function<void(const UploadChoice &choice)>;
	// Plays the finished file (the message screen opens its player).
	using PreviewCallback = std::function<void(const std::string &path, const std::vector<uint8_t> &waveform)>;

	VoiceRecorder(SendCallback onSend, PreviewCallback onPreview);
	~VoiceRecorder() override;

	void update(u32 kDown, u32 kHeld) override;
	void renderTop() override;
	void renderBottom() override;
	bool isAnimating() const override { return true; }

	static constexpr int MAX_SECONDS = 120;

  private:
	enum class State { READY, RECORDING, RECORDED, FAILED };

	void startRecording();
	void stopRecording();
	void captureLoop();
	bool writeFile();
	double seconds() const;

	SendCallback onSend;
	PreviewCallback onPreview;
	State state = State::READY;
	std::string failure;

	Discord::VoiceCapture capture;
	Utils::WorkerThread worker;
	std::atomic<bool> capturing{false};
	mutable std::mutex dataMutex;
	std::vector<std::string> packets;
	std::vector<int> peaks;

	std::string path;
	std::string waveform;
	std::vector<uint8_t> waveformBytes;
	float pulse = 0.0f;
};

} // namespace UI
