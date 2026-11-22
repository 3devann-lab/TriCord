#ifndef LOCAL_PREVIEW_H
#define LOCAL_PREVIEW_H

#include "utils/image_utils.h"
#include <citro2d.h>
#include <memory>
#include <mutex>
#include <string>

namespace UI {

// A still of an image on the SD card, decoded off the main thread.
class LocalPreview {
  public:
	LocalPreview() = default;
	~LocalPreview();
	LocalPreview(const LocalPreview &) = delete;
	LocalPreview &operator=(const LocalPreview &) = delete;

	// Starts loading path, dropping whatever was shown. Same path again is a no-op.
	void load(const std::string &path, int maxDim = 256);
	void clear();
	// Turns a finished decode into a texture; call from the main thread.
	void update();

	bool ready() const { return tex != nullptr; }
	bool loading() const;
	bool failed() const;
	const std::string &path() const { return currentPath; }
	int imageWidth() const { return width; }
	int imageHeight() const { return height; }

	// Fits the image in the box, centered.
	void draw(float x, float y, float maxW, float maxH, float z) const;

  private:
	struct Job {
		std::mutex mutex;
		bool done = false;
		bool cancelled = false;
		Utils::Image::TiledData tiled;
		int sourceW = 0;
		int sourceH = 0;
	};

	std::shared_ptr<Job> job;
	std::string currentPath;
	C3D_Tex *tex = nullptr;
	int width = 0;
	int height = 0;
	bool loadFailed = false;

	void releaseTexture();
};

} // namespace UI

#endif // LOCAL_PREVIEW_H
