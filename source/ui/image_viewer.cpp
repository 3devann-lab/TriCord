#include "ui/image_viewer.h"
#include "core/i18n.h"
#include "log.h"
#include "network/network_manager.h"
#include "ui/image_manager.h"
#include "ui/screen_manager.h"
#include "utils/image_utils.h"
#include "utils/redraw.h"
#include "utils/system_utils.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <mutex>

namespace UI {

namespace {

int viewerMaxDim() { return Utils::System::isNew3DS() ? 1024 : 512; }

// Discord's media proxy shrinks images on request, which saves both the download and the decode.
std::string sizedUrl(const ImageViewer::Image &image) {
	std::string url = image.url;
	const size_t cdn = url.find("cdn.discordapp.com");
	if (cdn != std::string::npos) {
		url.replace(cdn, 18, "media.discordapp.net");
	}
	if (url.find("media.discordapp.net") == std::string::npos && url.find("images-ext-") == std::string::npos) {
		return url;
	}
	const int maxDim = viewerMaxDim();
	int w = image.width;
	int h = image.height;
	if (w <= 0 || h <= 0) {
		w = h = maxDim;
	} else if (w > maxDim || h > maxDim) {
		const float scale = (float)maxDim / std::max(w, h);
		w = std::max(1, (int)(w * scale));
		h = std::max(1, (int)(h * scale));
	}
	const char *sep = url.find('?') == std::string::npos ? "?" : "&";
	url += sep;
	url += "width=" + std::to_string(w) + "&height=" + std::to_string(h);
	if (url.find("format=") == std::string::npos) {
		url += "&format=jpeg";
	}
	return url;
}

} // namespace

struct ImageViewer::Load {
	std::mutex mutex;
	bool done = false;
	bool cancelled = false;
	Utils::Image::TiledData tiled;
};

ImageViewer::ImageViewer(std::vector<Image> images, size_t index) : images(std::move(images)) {
	open(std::min(index, this->images.empty() ? 0 : this->images.size() - 1));
}

ImageViewer::~ImageViewer() { release(); }

void ImageViewer::release() {
	if (load) {
		std::lock_guard<std::mutex> lock(load->mutex);
		load->cancelled = true;
		if (load->tiled.pixels) {
			free(load->tiled.pixels);
			load->tiled.pixels = nullptr;
		}
	}
	load.reset();
	if (tex) {
		C3D_TexDelete(tex);
		free(tex);
		tex = nullptr;
	}
}

void ImageViewer::open(size_t which) {
	release();
	failed = false;
	imgW = imgH = 0;
	zoom = 1.0f;
	panX = panY = 0.0f;
	if (which >= images.size()) {
		failed = true;
		return;
	}
	index = which;

	auto pending = std::make_shared<Load>();
	load = pending;
	const int maxDim = viewerMaxDim();
	Network::NetworkManager::getInstance().enqueue(
	    sizedUrl(images[which]), "GET", "", Network::RequestPriority::INTERACTIVE,
	    [pending, maxDim](Network::HttpResponse &resp) {
		    {
			    std::lock_guard<std::mutex> lock(pending->mutex);
			    if (pending->cancelled) {
				    return;
			    }
		    }
		    std::shared_ptr<std::string> body = std::make_shared<std::string>(std::move(resp.body));
		    const bool ok = resp.success && !body->empty();
		    // Decoding a 1024px image takes a while, so it goes to the image decoder rather than a network thread.
		    ImageManager::getInstance().runOnDecoder([pending, body, ok, maxDim]() {
			    Utils::Image::TiledData tiled;
			    if (ok) {
				    tiled = Utils::Image::decodeToTiled((const unsigned char *)body->data(), body->size(), maxDim,
				                                        maxDim);
			    }
			    std::lock_guard<std::mutex> lock(pending->mutex);
			    if (pending->cancelled) {
				    if (tiled.pixels) {
					    free(tiled.pixels);
				    }
				    return;
			    }
			    pending->tiled = tiled;
			    pending->done = true;
			    Utils::requestRedraw();
		    });
	    });
}

bool ImageViewer::isAnimating() const { return load != nullptr; }

float ImageViewer::fitZoom() const {
	if (imgW <= 0 || imgH <= 0) {
		return 1.0f;
	}
	return std::min(400.0f / imgW, 240.0f / imgH);
}

void ImageViewer::clampPan() {
	const float halfW = std::max(0.0f, (imgW * zoom - 400.0f) / 2.0f);
	const float halfH = std::max(0.0f, (imgH * zoom - 240.0f) / 2.0f);
	panX = std::clamp(panX, -halfW, halfW);
	panY = std::clamp(panY, -halfH, halfH);
}

void ImageViewer::update(u32 kDown, u32 kHeld) {
	if (load) {
		Utils::Image::TiledData tiled;
		bool done = false;
		{
			std::lock_guard<std::mutex> lock(load->mutex);
			done = load->done;
			if (done) {
				tiled = load->tiled;
				load->tiled.pixels = nullptr;
			}
		}
		if (done) {
			load.reset();
			if (tiled.pixels) {
				tex = Utils::Image::createTexture(tiled);
				free(tiled.pixels);
			}
			if (tex) {
				imgW = tiled.w;
				imgH = tiled.h;
				zoom = fitZoom();
			} else {
				failed = true;
			}
		}
	}

	if (kDown & KEY_B) {
		finish();
		return;
	}
	if (images.size() > 1 && (kDown & (KEY_LEFT | KEY_RIGHT)) && zoom <= fitZoom() + 0.001f) {
		const size_t count = images.size();
		open((kDown & KEY_RIGHT) ? (index + 1) % count : (index + count - 1) % count);
		return;
	}
	if (!tex) {
		return;
	}

	const float minZoom = std::min(fitZoom(), 1.0f);
	const float maxZoom = 4.0f;
	if (kHeld & (KEY_R | KEY_X)) {
		zoom = std::min(maxZoom, zoom * 1.04f);
	}
	if (kHeld & (KEY_L | KEY_Y)) {
		zoom = std::max(minZoom, zoom / 1.04f);
	}
	if (kDown & KEY_A) {
		// Toggles between fitting the screen and one image pixel per screen pixel.
		zoom = std::fabs(zoom - fitZoom()) < 0.01f ? 1.0f : fitZoom();
		panX = panY = 0.0f;
	}

	circlePosition circle;
	hidCircleRead(&circle);
	const float speed = 4.0f;
	if (std::abs(circle.dx) > 20) {
		panX -= circle.dx / 156.0f * speed * 2.0f;
	}
	if (std::abs(circle.dy) > 20) {
		panY += circle.dy / 156.0f * speed * 2.0f;
	}
	if (zoom > fitZoom() + 0.001f) {
		if (kHeld & KEY_LEFT) {
			panX += speed * 2.0f;
		}
		if (kHeld & KEY_RIGHT) {
			panX -= speed * 2.0f;
		}
	}
	if (kHeld & KEY_UP) {
		panY += speed * 2.0f;
	}
	if (kHeld & KEY_DOWN) {
		panY -= speed * 2.0f;
	}

	if (kHeld & KEY_TOUCH) {
		touchPosition touch;
		hidTouchRead(&touch);
		if ((kDown & KEY_TOUCH) || !dragging) {
			dragging = true;
		} else {
			panX += (touch.px - lastTouch.px) * 1.5f;
			panY += (touch.py - lastTouch.py) * 1.5f;
		}
		lastTouch = touch;
	} else {
		dragging = false;
	}
	clampPan();
}

void ImageViewer::renderTop() {
	C2D_DrawRectSolid(0, 0, 0.1f, 400, 240, C2D_Color32(0, 0, 0, 255));
	if (tex && imgW > 0 && imgH > 0) {
		const float w = imgW * zoom;
		const float h = imgH * zoom;
		Tex3DS_SubTexture subtex = {(u16)imgW, (u16)imgH, 0.0f, 1.0f, (float)imgW / tex->width,
		                            1.0f - (float)imgH / tex->height};
		C2D_Image img = {tex, &subtex};
		C2D_DrawImageAt(img, (400.0f - w) / 2.0f + panX, (240.0f - h) / 2.0f + panY, 0.5f, nullptr, zoom, zoom);
	} else if (failed) {
		drawCenteredText(110.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorError(), TR("message.image_failed"), 400.0f);
	} else {
		drawCenteredText(110.0f, 0.5f, 0.5f, 0.5f, C2D_Color32(200, 200, 200, 255), TR("common.loading"), 400.0f);
	}
}

void ImageViewer::renderBottom() {
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, ScreenManager::colorBackgroundDark());
	const std::string title = index < images.size() ? images[index].title : "";
	drawText(10.0f, 8.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorText(), getTruncatedText(title, 300.0f, 0.5f, 0.5f));
	C2D_DrawRectSolid(10, 30, 0.5f, 300, 1, ScreenManager::colorSeparator());

	std::string info;
	if (images.size() > 1) {
		info = std::to_string(index + 1) + " / " + std::to_string(images.size()) + "  ·  ";
	}
	if (index < images.size() && images[index].width > 0) {
		info += std::to_string(images[index].width) + "×" + std::to_string(images[index].height) + "  ·  ";
	}
	info += std::to_string((int)std::lround(zoom * 100.0f)) + "%";
	drawText(10.0f, 38.0f, 0.5f, 0.42f, 0.42f, ScreenManager::colorTextMuted(), info);

	// A map of the visible part, once zoomed past the screen.
	if (tex && imgW > 0 && imgH > 0) {
		const float mapScale = std::min(200.0f / imgW, 120.0f / imgH);
		const float mapW = imgW * mapScale;
		const float mapH = imgH * mapScale;
		const float mapX = (320.0f - mapW) / 2.0f;
		const float mapY = 64.0f;
		Tex3DS_SubTexture subtex = {(u16)imgW, (u16)imgH, 0.0f, 1.0f, (float)imgW / tex->width,
		                            1.0f - (float)imgH / tex->height};
		C2D_Image img = {tex, &subtex};
		C2D_DrawImageAt(img, mapX, mapY, 0.5f, nullptr, mapScale, mapScale);
		const float viewW = std::min(mapW, 400.0f / zoom * mapScale);
		const float viewH = std::min(mapH, 240.0f / zoom * mapScale);
		const float viewX = mapX + (mapW - viewW) / 2.0f - panX / zoom * mapScale;
		const float viewY = mapY + (mapH - viewH) / 2.0f - panY / zoom * mapScale;
		const u32 color = ScreenManager::colorAccent();
		C2D_DrawRectSolid(viewX, viewY, 0.6f, viewW, 1.5f, color);
		C2D_DrawRectSolid(viewX, viewY + viewH - 1.5f, 0.6f, viewW, 1.5f, color);
		C2D_DrawRectSolid(viewX, viewY, 0.6f, 1.5f, viewH, color);
		C2D_DrawRectSolid(viewX + viewW - 1.5f, viewY, 0.6f, 1.5f, viewH, color);
	}

	std::string hints = "L/R: " + TR("viewer.zoom") + "  : " + TR("viewer.fit") + "  : " +
	                    TR("viewer.pan");
	if (images.size() > 1) {
		hints += "  : " + TR("viewer.next");
	}
	drawKeyHints(hints + "  : " + TR("common.close"));
}

} // namespace UI
