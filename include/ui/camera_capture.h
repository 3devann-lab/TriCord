#ifndef CAMERA_CAPTURE_H
#define CAMERA_CAPTURE_H

#include "ui/overlay.h"
#include "utils/worker_thread.h"
#include <atomic>
#include <citro2d.h>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace UI {

// Takes a photo with one of the 3DS cameras and saves it as a JPEG on the SD card.
class CameraCapture : public Overlay {
  public:
	using CaptureCallback = std::function<void(const std::string &path)>;

	explicit CameraCapture(CaptureCallback onCapture);
	~CameraCapture() override;

	void update(u32 kDown, u32 kHeld) override;
	void renderTop() override;
	void renderBottom() override;
	bool isAnimating() const override { return true; }

	static constexpr int WIDTH = 640;
	static constexpr int HEIGHT = 480;

  private:
	CaptureCallback onCapture;
	Utils::WorkerThread worker;
	std::atomic<bool> stopping{false};
	std::atomic<bool> inner{false};
	std::atomic<bool> switchRequested{false};
	std::atomic<bool> failed{false};
	std::atomic<bool> frameReady{false};

	std::mutex frameMutex;
	std::vector<uint16_t> latest;
	bool haveFrame = false;
	C3D_Tex *tex = nullptr;
	bool texReady = false;

	void captureLoop();
	void uploadPreview();
	std::string savePhoto();
};

} // namespace UI

#endif // CAMERA_CAPTURE_H
