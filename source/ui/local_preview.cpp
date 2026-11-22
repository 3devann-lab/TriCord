#include "ui/local_preview.h"
#include "log.h"
#include "stb_image.h"
#include "ui/image_manager.h"
#include "utils/file_utils.h"
#include "utils/redraw.h"
#include "utils/system_utils.h"
#include <cstdlib>
#include <vector>

namespace UI {

namespace {
constexpr long long MAX_PREVIEW_FILE = 12LL * 1024 * 1024;

// stb decodes the whole image before shrinking it, so the full size has to fit in memory.
long long maxPreviewPixels() { return Utils::System::isNew3DS() ? 9LL * 1000 * 1000 : 4LL * 1000 * 1000; }
} // namespace

LocalPreview::~LocalPreview() { clear(); }

void LocalPreview::releaseTexture() {
	if (tex) {
		C3D_TexDelete(tex);
		free(tex);
		tex = nullptr;
	}
}

void LocalPreview::clear() {
	if (job) {
		std::lock_guard<std::mutex> lock(job->mutex);
		job->cancelled = true;
		if (job->done && job->tiled.pixels) {
			free(job->tiled.pixels);
			job->tiled.pixels = nullptr;
		}
	}
	job.reset();
	releaseTexture();
	currentPath.clear();
	width = height = 0;
	loadFailed = false;
}

bool LocalPreview::loading() const { return job != nullptr; }

bool LocalPreview::failed() const { return loadFailed; }

void LocalPreview::load(const std::string &path, int maxDim) {
	if (path == currentPath) {
		return;
	}
	clear();
	currentPath = path;

	auto pending = std::make_shared<Job>();
	job = pending;
	ImageManager::getInstance().runOnDecoder([pending, path, maxDim]() {
		{
			std::lock_guard<std::mutex> lock(pending->mutex);
			if (pending->cancelled) {
				return;
			}
		}
		Utils::Image::TiledData tiled;
		int w = 0, h = 0, c = 0;
		const long long size = Utils::File::fileSize(path);
		if (size > 0 && size <= MAX_PREVIEW_FILE) {
			std::vector<unsigned char> data = Utils::File::readFileBinary(path);
			if (!data.empty() && stbi_info_from_memory(data.data(), (int)data.size(), &w, &h, &c) &&
			    (long long)w * h <= maxPreviewPixels()) {
				tiled = Utils::Image::decodeToTiled(data.data(), data.size(), maxDim, maxDim);
			}
		}

		std::lock_guard<std::mutex> lock(pending->mutex);
		if (pending->cancelled) {
			if (tiled.pixels) {
				free(tiled.pixels);
			}
			return;
		}
		pending->tiled = tiled;
		pending->sourceW = w;
		pending->sourceH = h;
		pending->done = true;
		Utils::requestRedraw();
	});
}

void LocalPreview::update() {
	if (!job) {
		return;
	}
	Utils::Image::TiledData tiled;
	{
		std::lock_guard<std::mutex> lock(job->mutex);
		if (!job->done) {
			return;
		}
		tiled = job->tiled;
		job->tiled.pixels = nullptr;
	}
	job.reset();

	if (!tiled.pixels) {
		loadFailed = true;
		return;
	}
	tex = Utils::Image::createTexture(tiled);
	free(tiled.pixels);
	if (!tex) {
		loadFailed = true;
		return;
	}
	width = tiled.w;
	height = tiled.h;
	Utils::requestRedraw();
}

void LocalPreview::draw(float x, float y, float maxW, float maxH, float z) const {
	if (!tex || width <= 0 || height <= 0) {
		return;
	}
	const float scale = std::min(maxW / width, maxH / height);
	const float drawW = width * scale;
	const float drawH = height * scale;
	const float uMax = (float)width / tex->width;
	const float vMax = (float)height / tex->height;
	Tex3DS_SubTexture subtex = {(u16)width, (u16)height, 0.0f, 1.0f, uMax, 1.0f - vMax};
	C2D_Image img = {tex, &subtex};
	C2D_DrawImageAt(img, x + (maxW - drawW) / 2.0f, y + (maxH - drawH) / 2.0f, z, nullptr, scale, scale);
}

} // namespace UI
