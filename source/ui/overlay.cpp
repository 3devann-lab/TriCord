#include "ui/overlay.h"
#include "core/i18n.h"
#include "ui/screen_manager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UI {

KeyboardInput runKeyboard(const std::string &hint, const std::string &initialText, bool multiline, int maxLength,
                          const std::string &confirmLabel) {
	SwkbdState swkbd;
	char buffer[2000];
	buffer[0] = '\0';
	swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, maxLength);
	u32 features =
	    SWKBD_PREDICTIVE_INPUT | SWKBD_DARKEN_TOP_SCREEN | SWKBD_ALLOW_HOME | SWKBD_ALLOW_RESET | SWKBD_ALLOW_POWER;
	if (multiline) {
		features |= SWKBD_MULTILINE;
	}
	swkbdSetFeatures(&swkbd, features);
	if (!initialText.empty()) {
		swkbdSetInitialText(&swkbd, initialText.c_str());
	}
	swkbdSetHintText(&swkbd, hint.c_str());
	swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, TR("common.cancel").c_str(), true);
	const std::string rightLabel = confirmLabel.empty() ? TR("common.ok") : confirmLabel;
	swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, rightLabel.c_str(), true);

	KeyboardInput result;
	result.confirmed = swkbdInputText(&swkbd, buffer, sizeof(buffer)) == SWKBD_BUTTON_RIGHT;
	std::string text = buffer;
	const size_t first = text.find_first_not_of(" \n\r\t");
	if (first == std::string::npos) {
		text.clear();
	} else {
		text = text.substr(first, text.find_last_not_of(" \n\r\t") - first + 1);
	}
	result.text = text;
	return result;
}

void drawOverlayHeader(const std::string &title, const std::string &right) {
	const float headerH = 26.0f;
	C2D_DrawRectSolid(0, 0, 0.9f, 400.0f, headerH, ScreenManager::colorHeaderGlass());
	C2D_DrawRectSolid(0, headerH - 1.0f, 0.91f, 400.0f, 1.0f, ScreenManager::colorHeaderBorder());
	float rightW = 0.0f;
	if (!right.empty()) {
		rightW = measureText(right, 0.42f, 0.42f);
		drawText(400.0f - 10.0f - rightW, 6.0f, 0.95f, 0.42f, 0.42f, ScreenManager::colorTextMuted(), right);
	}
	const std::string shown = getTruncatedText(title, 400.0f - 30.0f - rightW, 0.5f, 0.5f);
	drawText(10.0f, 5.0f, 0.95f, 0.5f, 0.5f, ScreenManager::colorText(), shown);
}

void drawProgressBar(float x, float y, float w, float h, float fraction, float z) {
	fraction = std::clamp(fraction, 0.0f, 1.0f);
	drawRoundedRect(x, y, z, w, h, h / 2.0f, ScreenManager::colorBackgroundLight());
	if (fraction > 0.0f) {
		drawRoundedRect(x, y, z + 0.01f, std::max(h, w * fraction), h, h / 2.0f, ScreenManager::colorAccent());
	}
}

void drawKeyHints(const std::string &hints) {
	C2D_DrawRectSolid(0, 240.0f - 22.0f, 0.8f, 320.0f, 22.0f, ScreenManager::colorBackgroundDark());
	drawText(8.0f, 240.0f - 18.0f, 0.85f, 0.4f, 0.4f, ScreenManager::colorTextMuted(),
	         getTruncatedText(hints, 304.0f, 0.4f, 0.4f));
}

std::string formatDuration(double seconds) {
	if (!std::isfinite(seconds) || seconds < 0) {
		seconds = 0;
	}
	const long total = (long)seconds;
	char buffer[32];
	if (total >= 3600) {
		snprintf(buffer, sizeof(buffer), "%ld:%02ld:%02ld", total / 3600, (total / 60) % 60, total % 60);
	} else {
		snprintf(buffer, sizeof(buffer), "%ld:%02ld", total / 60, total % 60);
	}
	return buffer;
}

} // namespace UI
