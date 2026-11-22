#include "ui/doodle_pad.h"
#include "core/config.h"
#include "core/i18n.h"
#include "ui/screen_manager.h"
#include "utils/file_utils.h"
#include "utils/png_writer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace UI {

namespace {

const int TEX_W = 512;
const int TEX_H = 256;
const int MORTON[] = {0,  1,  4,  5,  16, 17, 20, 21, 2,  3,  6,  7,  18, 19, 22, 23, 8,  9,  12, 13, 24, 25,
                      28, 29, 10, 11, 14, 15, 26, 27, 30, 31, 32, 33, 36, 37, 48, 49, 52, 53, 34, 35, 38, 39,
                      50, 51, 54, 55, 40, 41, 44, 45, 56, 57, 60, 61, 42, 43, 46, 47, 58, 59, 62, 63};

struct Paint {
	uint8_t r, g, b;
	const char *key;
};

const Paint PALETTE[] = {
    {20, 20, 24, "doodle.color.black"},    {255, 255, 255, "doodle.color.white"},  {237, 66, 69, "doodle.color.red"},
    {250, 145, 40, "doodle.color.orange"}, {254, 231, 92, "doodle.color.yellow"}, {87, 242, 135, "doodle.color.green"},
    {52, 152, 219, "doodle.color.blue"},   {88, 101, 242, "doodle.color.blurple"}, {155, 89, 182, "doodle.color.purple"},
    {235, 69, 158, "doodle.color.pink"},   {139, 90, 43, "doodle.color.brown"},    {149, 165, 166, "doodle.color.gray"},
};
const int PALETTE_SIZE = sizeof(PALETTE) / sizeof(PALETTE[0]);
const int SIZES[] = {1, 2, 4, 7, 12, 20};
const int SIZE_COUNT = sizeof(SIZES) / sizeof(SIZES[0]);
const size_t UNDO_DEPTH = 8;
const uint16_t WHITE565 = 0xFFFF;

uint16_t to565(uint8_t r, uint8_t g, uint8_t b) { return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }

} // namespace

DoodlePad::DoodlePad(SendCallback onSend) : onSend(std::move(onSend)), canvas(WIDTH * HEIGHT, WHITE565) {
	texReady = C3D_TexInit(&tex, TEX_W, TEX_H, GPU_RGB565);
	if (texReady) {
		C3D_TexSetFilter(&tex, GPU_NEAREST, GPU_NEAREST);
		memset(tex.data, 0xFF, tex.size);
	}
}

DoodlePad::~DoodlePad() {
	if (texReady) {
		C3D_TexDelete(&tex);
	}
}

uint16_t DoodlePad::currentColor() const {
	if (eraser) {
		return WHITE565;
	}
	const Paint &p = PALETTE[colorIndex];
	return to565(p.r, p.g, p.b);
}

void DoodlePad::stamp(int cx, int cy, int radius, uint16_t color) {
	const int r2 = radius * radius + radius;
	for (int y = std::max(0, cy - radius); y <= std::min(HEIGHT - 1, cy + radius); y++) {
		const int dy = y - cy;
		for (int x = std::max(0, cx - radius); x <= std::min(WIDTH - 1, cx + radius); x++) {
			const int dx = x - cx;
			if (dx * dx + dy * dy <= r2) {
				canvas[y * WIDTH + x] = color;
			}
		}
	}
	dirty = true;
}

void DoodlePad::strokeTo(int x, int y) {
	const int radius = SIZES[sizeIndex];
	const uint16_t color = currentColor();
	const float dist = std::hypot((float)(x - lastX), (float)(y - lastY));
	// Stamps close enough together to read as a line however fast the stylus moves.
	const int steps = std::max(1, (int)(dist / std::max(1.0f, radius * 0.5f)));
	for (int i = 1; i <= steps; i++) {
		int px = lastX + (x - lastX) * i / steps;
		int py = lastY + (y - lastY) * i / steps;
		stamp(px, py, radius, color);
	}
	lastX = x;
	lastY = y;
}

void DoodlePad::pushUndo() {
	undo.push_back(canvas);
	if (undo.size() > UNDO_DEPTH) {
		undo.pop_front();
	}
}

void DoodlePad::upload() {
	if (!texReady || !dirty) {
		return;
	}
	uint16_t *data = (uint16_t *)tex.data;
	for (int y = 0; y < HEIGHT; y++) {
		uint16_t *row = data + ((size_t)(y >> 3) * (TEX_W >> 3) << 6);
		const int *morton = &MORTON[(y & 7) << 3];
		const uint16_t *src = &canvas[y * WIDTH];
		for (int x = 0; x < WIDTH; x++) {
			row[((x >> 3) << 6) + morton[x & 7]] = src[x];
		}
	}
	C3D_TexFlush(&tex);
	dirty = false;
}

bool DoodlePad::save(std::string &path) const {
	std::vector<uint8_t> rgb(WIDTH * HEIGHT * 3);
	for (int i = 0; i < WIDTH * HEIGHT; i++) {
		uint16_t c = canvas[i];
		uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
		rgb[i * 3] = (uint8_t)((r << 3) | (r >> 2));
		rgb[i * 3 + 1] = (uint8_t)((g << 2) | (g >> 4));
		rgb[i * 3 + 2] = (uint8_t)((b << 3) | (b >> 2));
	}
	std::string png = Utils::Png::encodeRgb(rgb.data(), WIDTH, HEIGHT);
	if (png.empty()) {
		return false;
	}
	path = std::string(CONFIG_DIR_PATH) + "/cache/doodle.png";
	Utils::File::ensureDirectory(Utils::File::parentDirectory(path));
	return Utils::File::writeFile(path, png);
}

void DoodlePad::update(u32 kDown, u32 kHeld) {
	if (confirmTimer > 0 && --confirmTimer == 0) {
		confirmKey = 0;
	}

	touchPosition touch;
	hidTouchRead(&touch);
	if (kDown & KEY_TOUCH) {
		pushUndo();
		drawing = true;
		drewAnything = true;
		lastX = touch.px;
		lastY = touch.py;
		stamp(lastX, lastY, SIZES[sizeIndex], currentColor());
	} else if ((kHeld & KEY_TOUCH) && drawing) {
		strokeTo(touch.px, touch.py);
	} else {
		drawing = false;
	}

	auto confirmed = [&](u32 key) {
		if (confirmKey == key && confirmTimer > 0) {
			confirmKey = 0;
			confirmTimer = 0;
			return true;
		}
		confirmKey = key;
		confirmTimer = 120;
		return false;
	};

	if (kDown & KEY_B) {
		if (!drewAnything || confirmed(KEY_B)) {
			finish();
		}
		return;
	}
	if (kDown & KEY_R) {
		colorIndex = (colorIndex + 1) % PALETTE_SIZE;
		eraser = false;
		dirty = true;
	} else if (kDown & KEY_L) {
		colorIndex = (colorIndex + PALETTE_SIZE - 1) % PALETTE_SIZE;
		eraser = false;
		dirty = true;
	}
	if (kDown & KEY_UP) {
		sizeIndex = std::min(SIZE_COUNT - 1, sizeIndex + 1);
		dirty = true;
	} else if (kDown & KEY_DOWN) {
		sizeIndex = std::max(0, sizeIndex - 1);
		dirty = true;
	}
	if (kDown & KEY_X) {
		eraser = !eraser;
		dirty = true;
	}
	if ((kDown & KEY_Y) && !undo.empty()) {
		canvas = undo.back();
		undo.pop_back();
		dirty = true;
	}
	if ((kDown & KEY_LEFT) && drewAnything && confirmed(KEY_LEFT)) {
		pushUndo();
		std::fill(canvas.begin(), canvas.end(), WHITE565);
		dirty = true;
	}
	if ((kDown & KEY_A) && drewAnything && onSend) {
		std::string path;
		if (save(path)) {
			onSend(path, this);
		} else {
			confirmKey = KEY_A;
			confirmTimer = 120;
		}
	}
}

void DoodlePad::renderTop() {
	C2D_DrawRectSolid(0, 0, 0.1f, 400, 240, ScreenManager::colorBackground());
	drawOverlayHeader(TR("doodle.title"));

	// The palette, with the current colour ringed.
	const float sw = 24.0f, gap = 6.0f;
	const float startX = (400.0f - (PALETTE_SIZE * sw + (PALETTE_SIZE - 1) * gap)) / 2.0f;
	for (int i = 0; i < PALETTE_SIZE; i++) {
		float cx = startX + i * (sw + gap) + sw / 2.0f;
		if (i == colorIndex && !eraser) {
			drawCircle(cx, 52.0f, 0.4f, sw / 2.0f + 3.0f, ScreenManager::colorText());
		}
		drawCircle(cx, 52.0f, 0.45f, sw / 2.0f, C2D_Color32(PALETTE[i].r, PALETTE[i].g, PALETTE[i].b, 255));
	}

	std::string tool = eraser ? TR("doodle.eraser") : TR(PALETTE[colorIndex].key);
	drawCenteredText(74.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorText(), tool, 400.0f);

	// Brush size preview.
	const float px = 200.0f, py = 112.0f;
	drawCircle(px, py, 0.4f, 24.0f, ScreenManager::colorBackgroundLight());
	u32 brush = eraser ? C2D_Color32(255, 255, 255, 255)
	                   : C2D_Color32(PALETTE[colorIndex].r, PALETTE[colorIndex].g, PALETTE[colorIndex].b, 255);
	drawCircle(px, py, 0.45f, std::max(1.5f, (float)SIZES[sizeIndex]), brush);

	std::vector<std::string> lines = {
	    "L/R: " + TR("doodle.hint.color") + "    : " + TR("doodle.hint.size"),
	    ": " + TR("doodle.hint.eraser") + "    : " + TR("doodle.hint.undo") + "    : " + TR("doodle.hint.clear"),
	    ": " + TR("doodle.hint.send") + "    : " + TR("common.cancel")};
	float y = 160.0f;
	for (const auto &line : lines) {
		drawCenteredText(y, 0.5f, 0.42f, 0.42f, ScreenManager::colorTextMuted(), line, 400.0f);
		y += 18.0f;
	}
	if (confirmTimer > 0) {
		std::string ask = confirmKey == KEY_B      ? TR("doodle.confirm_discard")
		                  : confirmKey == KEY_LEFT ? TR("doodle.confirm_clear")
		                                           : TR("doodle.save_failed");
		float w = measureText(ask, 0.45f, 0.45f) + 20.0f;
		drawRoundedRect((400.0f - w) / 2.0f, 214.0f, 0.8f, w, 20.0f, 10.0f, ScreenManager::colorError());
		drawCenteredText(217.0f, 0.81f, 0.45f, 0.45f, ScreenManager::colorWhite(), ask, 400.0f);
	}
}

void DoodlePad::renderBottom() {
	upload();
	if (texReady) {
		Tex3DS_SubTexture sub = {(u16)WIDTH, (u16)HEIGHT, 0.0f, 1.0f, (float)WIDTH / TEX_W, 1.0f - (float)HEIGHT / TEX_H};
		C2D_Image img = {&tex, &sub};
		C2D_DrawImageAt(img, 0.0f, 0.0f, 0.5f, nullptr, 1.0f, 1.0f);
	} else {
		C2D_DrawRectSolid(0, 0, 0.5f, 320, 240, C2D_Color32(255, 255, 255, 255));
	}
}

} // namespace UI
