#include "ui/camera_capture.h"
#include "core/config.h"
#include "core/i18n.h"
#include "log.h"
#include "ui/screen_manager.h"
#include "utils/file_utils.h"
#include "utils/redraw.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <turbojpeg.h>

namespace UI {

namespace {

constexpr int PREVIEW_W = CameraCapture::WIDTH / 2;
constexpr int PREVIEW_H = CameraCapture::HEIGHT / 2;
constexpr int TEX_W = 512;
constexpr int TEX_H = 256;
constexpr u32 FRAME_BYTES = CameraCapture::WIDTH * CameraCapture::HEIGHT * 2;
constexpr s64 WAIT_NS = 1000000000LL;

const int mortonTable[] = {0,  1,  4,  5,  16, 17, 20, 21, 2,  3,  6,  7,  18, 19, 22, 23, 8,  9,  12, 13, 24, 25,
                           28, 29, 10, 11, 14, 15, 26, 27, 30, 31, 32, 33, 36, 37, 48, 49, 52, 53, 34, 35, 38, 39,
                           50, 51, 54, 55, 40, 41, 44, 45, 56, 57, 60, 61, 42, 43, 46, 47, 58, 59, 62, 63};

std::string photosDirectory() { return std::string(CONFIG_DIR_PATH) + "/photos"; }

} // namespace

CameraCapture::CameraCapture(CaptureCallback onCapture) : onCapture(std::move(onCapture)) {
	tex = (C3D_Tex *)malloc(sizeof(C3D_Tex));
	if (!tex || !C3D_TexInit(tex, TEX_W, TEX_H, GPU_RGB565)) {
		free(tex);
		tex = nullptr;
		failed = true;
		return;
	}
	C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
	latest.resize((size_t)WIDTH * HEIGHT);
	worker.start([this] { captureLoop(); }, 1, 16 * 1024, false);
}

CameraCapture::~CameraCapture() {
	stopping = true;
	if (worker.joinable()) {
		worker.join();
	}
	if (tex) {
		C3D_TexDelete(tex);
		free(tex);
	}
}

void CameraCapture::captureLoop() {
	if (R_FAILED(camInit())) {
		Logger::log("[Camera] camInit failed");
		failed = true;
		return;
	}

	const u32 both = SELECT_IN1_OUT1;
	CAMU_SetSize(both, SIZE_VGA, CONTEXT_A);
	CAMU_SetOutputFormat(both, OUTPUT_RGB_565, CONTEXT_A);
	CAMU_SetFrameRate(both, FRAME_RATE_15);
	CAMU_SetNoiseFilter(both, true);
	CAMU_SetAutoExposure(both, true);
	CAMU_SetAutoWhiteBalance(both, true);
	CAMU_SetTrimming(PORT_CAM1, false);

	u32 transferUnit = 0;
	CAMU_GetMaxBytes(&transferUnit, WIDTH, HEIGHT);
	CAMU_SetTransferBytes(PORT_CAM1, transferUnit, WIDTH, HEIGHT);

	std::vector<uint16_t> buffer((size_t)WIDTH * HEIGHT);
	Handle events[2] = {0, 0};
	CAMU_GetBufferErrorInterruptEvent(&events[0], PORT_CAM1);

	auto begin = [&](bool useInner) {
		CAMU_Activate(useInner ? SELECT_IN1 : SELECT_OUT1);
		CAMU_ClearBuffer(PORT_CAM1);
		CAMU_StartCapture(PORT_CAM1);
	};
	begin(inner);

	while (!stopping) {
		if (switchRequested.exchange(false)) {
			CAMU_StopCapture(PORT_CAM1);
			if (events[1]) {
				svcCloseHandle(events[1]);
				events[1] = 0;
			}
			begin(inner);
		}
		if (!events[1]) {
			CAMU_SetReceiving(&events[1], buffer.data(), PORT_CAM1, FRAME_BYTES, (s16)transferUnit);
		}
		s32 index = -1;
		if (R_FAILED(svcWaitSynchronizationN(&index, events, 2, false, WAIT_NS))) {
			continue;
		}
		if (index == 0) {
			// Buffer error: the transfer fell behind, so start over.
			svcCloseHandle(events[1]);
			events[1] = 0;
			CAMU_StartCapture(PORT_CAM1);
			continue;
		}
		if (index == 1) {
			svcCloseHandle(events[1]);
			events[1] = 0;
			{
				std::lock_guard<std::mutex> lock(frameMutex);
				latest.swap(buffer);
				haveFrame = true;
			}
			frameReady = true;
			Utils::requestRedraw();
		}
	}

	CAMU_StopCapture(PORT_CAM1);
	CAMU_Activate(SELECT_NONE);
	for (Handle &event : events) {
		if (event) {
			svcCloseHandle(event);
		}
	}
	camExit();
}

void CameraCapture::uploadPreview() {
	if (!tex || !frameReady.exchange(false)) {
		return;
	}
	std::lock_guard<std::mutex> lock(frameMutex);
	u16 *dst = (u16 *)tex->data;
	const int tilesPerRow = TEX_W >> 3;
	const bool mirror = inner;
	for (int y = 0; y < PREVIEW_H; y++) {
		const uint16_t *row = latest.data() + (size_t)(y * 2) * WIDTH;
		const int *morton = &mortonTable[(y & 7) << 3];
		u16 *tileRow = dst + ((size_t)(y >> 3) * tilesPerRow << 6);
		for (int x = 0; x < PREVIEW_W; x++) {
			// The selfie camera reads naturally as a mirror.
			const int sx = mirror ? (WIDTH - 2 - x * 2) : x * 2;
			tileRow[((x >> 3) << 6) + morton[x & 7]] = row[sx];
		}
	}
	GSPGPU_FlushDataCache(tex->data, tex->size);
	texReady = true;
}

std::string CameraCapture::savePhoto() {
	std::vector<unsigned char> rgb((size_t)WIDTH * HEIGHT * 3);
	{
		std::lock_guard<std::mutex> lock(frameMutex);
		if (!haveFrame) {
			return "";
		}
		for (size_t i = 0; i < (size_t)WIDTH * HEIGHT; i++) {
			const uint16_t px = latest[i];
			const unsigned r = (px >> 11) & 0x1F;
			const unsigned g = (px >> 5) & 0x3F;
			const unsigned b = px & 0x1F;
			rgb[i * 3] = (unsigned char)((r << 3) | (r >> 2));
			rgb[i * 3 + 1] = (unsigned char)((g << 2) | (g >> 4));
			rgb[i * 3 + 2] = (unsigned char)((b << 3) | (b >> 2));
		}
	}

	tjhandle handle = tjInitCompress();
	if (!handle) {
		return "";
	}
	unsigned char *jpeg = nullptr;
	unsigned long jpegSize = 0;
	const int res = tjCompress2(handle, rgb.data(), WIDTH, WIDTH * 3, HEIGHT, TJPF_RGB, &jpeg, &jpegSize, TJSAMP_420,
	                            90, TJFLAG_FASTDCT);
	tjDestroy(handle);
	if (res != 0 || !jpeg) {
		if (jpeg) {
			tjFree(jpeg);
		}
		return "";
	}

	Utils::File::ensureDirectory(photosDirectory());
	char name[64];
	const time_t now = time(nullptr);
	struct tm tm;
	gmtime_r(&now, &tm);
	snprintf(name, sizeof(name), "IMG_%04d%02d%02d_%02d%02d%02d.jpg", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
	         tm.tm_hour, tm.tm_min, tm.tm_sec);
	const std::string path = Utils::File::uniquePath(photosDirectory(), name);

	bool ok = false;
	if (FILE *file = fopen(path.c_str(), "wb")) {
		ok = fwrite(jpeg, 1, jpegSize, file) == jpegSize;
		ok = (fclose(file) == 0) && ok;
	}
	tjFree(jpeg);
	if (!ok) {
		remove(path.c_str());
		return "";
	}
	Logger::log("[Camera] Saved %s (%lu bytes)", path.c_str(), jpegSize);
	return path;
}

void CameraCapture::update(u32 kDown, u32 kHeld) {
	(void)kHeld;
	uploadPreview();

	if (kDown & KEY_B) {
		finish();
		return;
	}
	if (failed) {
		return;
	}
	if (kDown & (KEY_X | KEY_Y)) {
		inner = !inner;
		switchRequested = true;
		texReady = false;
	}
	if (kDown & (KEY_A | KEY_R)) {
		const std::string path = savePhoto();
		if (path.empty()) {
			ScreenManager::getInstance().showToast(TR("camera.save_failed"));
			return;
		}
		finish();
		if (onCapture) {
			onCapture(path);
		}
	}
}

void CameraCapture::renderTop() {
	C2D_DrawRectSolid(0, 0, 0.1f, 400, 240, C2D_Color32(0, 0, 0, 255));
	if (failed) {
		drawCenteredText(110.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorError(), TR("camera.unavailable"), 400.0f);
		return;
	}
	if (!texReady) {
		drawCenteredText(110.0f, 0.5f, 0.5f, 0.5f, C2D_Color32(200, 200, 200, 255), TR("common.loading"), 400.0f);
		return;
	}
	Tex3DS_SubTexture subtex = {(u16)PREVIEW_W, (u16)PREVIEW_H, 0.0f, 1.0f, (float)PREVIEW_W / TEX_W,
	                            1.0f - (float)PREVIEW_H / TEX_H};
	C2D_Image img = {tex, &subtex};
	C2D_DrawImageAt(img, (400.0f - PREVIEW_W) / 2.0f, 0.0f, 0.5f, nullptr, 1.0f, 1.0f);
}

void CameraCapture::renderBottom() {
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, ScreenManager::colorBackgroundDark());
	drawText(10.0f, 8.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorText(), TR("camera.title"));
	C2D_DrawRectSolid(10, 30, 0.5f, 300, 1, ScreenManager::colorSeparator());
	drawText(10.0f, 40.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(),
	         inner ? TR("camera.inner") : TR("camera.outer"));
	drawText(10.0f, 60.0f, 0.5f, 0.4f, 0.4f, ScreenManager::colorTextMuted(),
	         Core::I18n::format(TR("camera.saved_to"), photosDirectory()));

	drawCircle(160.0f, 140.0f, 0.5f, 30.0f, ScreenManager::colorBackgroundLight());
	drawCircle(160.0f, 140.0f, 0.55f, 22.0f, ScreenManager::colorWhite());

	drawKeyHints("/R: " + TR("camera.shoot") + "  : " + TR("camera.switch") + "  : " +
	             TR("common.cancel"));
}

} // namespace UI
