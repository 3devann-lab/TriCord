#ifndef VIDEO_PLAYER_H
#define VIDEO_PLAYER_H

#include "ui/overlay.h"
#include "utils/video_decoder.h"
#include "utils/worker_thread.h"
#include <atomic>
#include <citro2d.h>
#include <memory>
#include <string>

namespace Network {
struct TransferProgress;
}

namespace UI {

// Plays a video from Discord or the SD card. Remote videos are downloaded to the SD card first.
class VideoPlayer : public Overlay {
  public:
	struct Source {
		std::string title;
		// Downloaded first when set.
		std::string url;
		// Played directly when set.
		std::string localPath;
		// What a saved copy is called.
		std::string fileName;
		// GIF-like clips play on repeat.
		bool loop = false;
	};

	explicit VideoPlayer(const Source &source);
	~VideoPlayer() override;

	void update(u32 kDown, u32 kHeld) override;
	void renderTop() override;
	void renderBottom() override;
	bool isAnimating() const override { return true; }

  private:
	enum class Stage { DOWNLOADING, PLAYING, FAILED };

	struct Download {
		std::atomic<bool> done{false};
		std::atomic<bool> ok{false};
		std::string error;
	};

	Source source;
	Stage stage = Stage::DOWNLOADING;
	std::string path;
	std::string failure;
	std::shared_ptr<Network::TransferProgress> progress;
	std::shared_ptr<Download> download;
	Media::VideoDecoder decoder;
	bool started = false;
	bool hardware = true;
	float volume = 1.0f;
	int repeatTimer = 0;
	u32 repeatKey = 0;
	int osdTimer = 0;
	std::string osdText;

	C3D_Tex *tex = nullptr;
	int frameW = 0;
	int frameH = 0;
	bool haveFrame = false;

	Utils::WorkerThread saveThread;
	std::shared_ptr<std::atomic<int>> saveState;
	std::string savedPath;

	void startDownload();
	void startPlayback(double at);
	void uploadFrame(const uint16_t *pixels, int width, int height, int rotation);
	void seekBy(double seconds);
	void showOsd(const std::string &text);
	void save();
	bool progressBarHit(const touchPosition &touch, float &fraction) const;
};

} // namespace UI

#endif // VIDEO_PLAYER_H
