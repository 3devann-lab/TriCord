#ifndef IMAGE_VIEWER_H
#define IMAGE_VIEWER_H

#include "ui/overlay.h"
#include <atomic>
#include <citro2d.h>
#include <memory>
#include <string>
#include <vector>

namespace UI {

// Shows an image full screen at up to 1024 pixels, with zoom and pan.
class ImageViewer : public Overlay {
  public:
	struct Image {
		std::string url;
		std::string title;
		int width = 0;
		int height = 0;
	};

	// Starts on images[index]; left and right step through the rest.
	ImageViewer(std::vector<Image> images, size_t index);
	~ImageViewer() override;

	void update(u32 kDown, u32 kHeld) override;
	void renderTop() override;
	void renderBottom() override;
	bool isAnimating() const override;

  private:
	struct Load;

	std::vector<Image> images;
	size_t index = 0;
	std::shared_ptr<Load> load;
	C3D_Tex *tex = nullptr;
	int imgW = 0;
	int imgH = 0;
	bool failed = false;
	float zoom = 1.0f;
	float panX = 0.0f;
	float panY = 0.0f;
	touchPosition lastTouch = {0, 0};
	bool dragging = false;

	void open(size_t which);
	void release();
	float fitZoom() const;
	void clampPan();
};

} // namespace UI

#endif // IMAGE_VIEWER_H
