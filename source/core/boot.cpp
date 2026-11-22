#include "core/boot.h"
#include "core/config.h"
#include "log.h"
#include "utils/file_utils.h"
#include <3ds.h>
#include <citro2d.h>
#include <cstdio>

namespace Core {
namespace Boot {

namespace {

const std::string MARKER = std::string(CONFIG_DIR_PATH) + "/starting.flag";
bool safe = false;
bool heldSelect = false;
bool markerCleared = false;
u64 loopStart = 0;
u64 connectedSince = 0;

C3D_RenderTarget *top = nullptr;
C3D_RenderTarget *bottom = nullptr;
C2D_TextBuf textBuf = nullptr;

void drawLine(const char *text, float y, float scale, u32 color, float width) {
	C2D_Text t;
	if (!C2D_TextParse(&t, textBuf, text)) {
		return;
	}
	C2D_TextOptimize(&t);
	C2D_DrawText(&t, C2D_WithColor | C2D_AlignCenter, width / 2.0f, y, 0.5f, scale, scale, color);
}

} // namespace

bool begin() {
	hidScanInput();
	heldSelect = (hidKeysHeld() & KEY_SELECT) != 0;
	bool unfinished = Utils::File::exists(MARKER);
	safe = heldSelect || unfinished;
	Utils::File::ensureDirectory(CONFIG_DIR_PATH);
	Utils::File::writeFile(MARKER, std::string("starting"));
	if (safe) {
		Logger::log("[Boot] Safe start (%s)", heldSelect ? "SELECT held" : "last launch did not finish starting");
	}
	return safe;
}

bool isSafeStart() { return safe; }

std::string safeStartNote() {
	if (!safe) {
		return "";
	}
	return heldSelect ? "Safe start: Google DNS, SD caches and the battery/light/lid features are off. Turn them back "
	                    "on in Settings."
	                  : "The last launch didn't finish starting, so Google DNS, SD caches and the battery/light/lid "
	                    "features were turned off. Turn them back on in Settings.";
}

void show(const char *step) {
	Logger::log("[Boot] %s", step);
	if (!top) {
		top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
		bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
		textBuf = C2D_TextBufNew(1024);
	}
	if (!top || !bottom || !textBuf) {
		return;
	}
	C2D_TextBufClear(textBuf);
	const u32 bg = C2D_Color32(30, 31, 34, 255);
	const u32 white = C2D_Color32(242, 243, 245, 255);
	const u32 muted = C2D_Color32(148, 155, 164, 255);
	const u32 blurple = C2D_Color32(88, 101, 242, 255);

	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
	C2D_TargetClear(top, bg);
	C2D_SceneBegin(top);
	C2D_DrawRectSolid(150.0f, 70.0f, 0.4f, 100.0f, 6.0f, blurple);
	drawLine("TriCord", 90.0f, 1.0f, white, 400.0f);
	char line[160];
	snprintf(line, sizeof(line), "Starting: %s", step);
	drawLine(line, 140.0f, 0.5f, muted, 400.0f);
	if (safe) {
		drawLine("Safe start", 165.0f, 0.5f, blurple, 400.0f);
	}

	C2D_TargetClear(bottom, bg);
	C2D_SceneBegin(bottom);
	drawLine("If TriCord stops here, note the step", 96.0f, 0.45f, muted, 320.0f);
	drawLine("shown on the top screen.", 112.0f, 0.45f, muted, 320.0f);
	drawLine("Hold SELECT while starting for safe start.", 140.0f, 0.45f, muted, 320.0f);
	C3D_FrameEnd(0);
}

void endScreens() {
	if (textBuf) {
		C2D_TextBufDelete(textBuf);
		textBuf = nullptr;
	}
	if (top) {
		C3D_RenderTargetDelete(top);
		top = nullptr;
	}
	if (bottom) {
		C3D_RenderTargetDelete(bottom);
		bottom = nullptr;
	}
}

void tick(bool connected) {
	if (markerCleared) {
		return;
	}
	u64 now = osGetTime();
	if (!loopStart) {
		loopStart = now;
	}
	if (connected && !connectedSince) {
		connectedSince = now;
	}
	if ((connectedSince && now - connectedSince > 5000) || now - loopStart > 60000) {
		markerCleared = true;
		remove(MARKER.c_str());
		Logger::log("[Boot] Started");
	}
}

} // namespace Boot
} // namespace Core
