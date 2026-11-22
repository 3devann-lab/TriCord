#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace Media {

enum class PlayerState { OPENING, PLAYING, ENDED, FAILED };

struct VideoInfo {
	int width = 0;
	int height = 0;
	// Frames come out already scaled to fit the top screen, at this size.
	int frameWidth = 0;
	int frameHeight = 0;
	// Clockwise degrees the frames need turning by: 0, 90, 180 or 270. Phones record portrait video sideways.
	int rotation = 0;
	double duration = 0.0;
	double frameRate = 0.0;
	bool hasVideo = false;
	bool hasAudio = false;
	bool hardware = false;
	std::string videoCodec;
	std::string audioCodec;
};

// Plays a video file from the SD card: demuxing and decoding on worker threads, audio straight to
// NDSP, and frames handed to the UI as RGB565 once they are due. H.264 goes through the New 3DS
// hardware decoder when it can, everything else through FFmpeg.
class VideoDecoder {
  public:
	// False when this build was made without FFmpeg.
	static bool available();
	static bool hardwareAvailable();

	VideoDecoder();
	~VideoDecoder();
	VideoDecoder(const VideoDecoder &) = delete;
	VideoDecoder &operator=(const VideoDecoder &) = delete;

	void start(const std::string &path, double startSeconds, bool allowHardware, bool loop);
	void stop();

	PlayerState state() const;
	std::string error() const;
	VideoInfo info() const;

	// Hands the newest due frame to sink, if one arrived since the last call. Pixels are RGB565,
	// frameWidth x frameHeight with no row padding, before rotation.
	bool consumeFrame(const std::function<void(const uint16_t *pixels, int width, int height)> &sink);

	double position() const;
	void setPaused(bool paused);
	bool paused() const;
	void seek(double seconds);
	void setVolume(float volume);
	void setLoop(bool loop);

	struct Impl;

  private:
	std::unique_ptr<Impl> impl;
};

} // namespace Media

#endif // VIDEO_DECODER_H
