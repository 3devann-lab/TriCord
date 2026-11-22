#include "ui/loading_view.h"
#include "core/i18n.h"
#include "discord/discord_client.h"
#include "ui/image_manager.h"
#include "ui/screen_manager.h"
#include "utils/message_utils.h"
#include <3ds.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UI {
namespace LoadingView {

namespace {

constexpr const char *ICON = "romfs:/tricord-icon-128.png";
constexpr int TIP_COUNT = 8;
constexpr double TIP_SECONDS = 5.0;

float smoothed = 0.0f;
u64 lastFrame = 0;

double seconds() { return osGetTime() / 1000.0; }

u32 withAlpha(u32 color, float alpha) {
	const u32 a = (u32)std::clamp(alpha * 255.0f, 0.0f, 255.0f);
	return (color & 0x00FFFFFF) | (a << 24);
}

// Where the connection is, as a fraction of the way to READY, and which step that is.
float targetProgress(Discord::ConnectionState state, const std::string &status, int &step) {
	using S = Discord::ConnectionState;
	switch (state) {
	case S::CONNECTING:
	case S::RECONNECTING:
		step = 1;
		return 0.12f;
	case S::CONNECTED_WS:
		step = 1;
		return 0.22f;
	case S::IDENTIFYING:
		step = 2;
		return 0.32f;
	case S::AUTHENTICATING: {
		step = 3;
		// "Loading guilds (12/40)..." lets the bar move with the guilds as they arrive.
		int done = 0;
		int total = 0;
		const size_t open = status.rfind('(');
		if (open != std::string::npos && sscanf(status.c_str() + open, "(%d/%d)", &done, &total) == 2 && total > 0) {
			return 0.4f + 0.5f * std::min(1.0f, (float)done / total);
		}
		return 0.4f;
	}
	case S::READY:
		step = 4;
		return 1.0f;
	default:
		step = 1;
		return 0.05f;
	}
}

void drawIcon(float cx, float cy, float size, float rotation) {
	C3D_Tex *tex = ImageManager::getInstance().getLocalImage(ICON, true);
	if (!tex) {
		return;
	}
	const auto info = ImageManager::getInstance().getImageInfo(ICON);
	const int w = info.originalW > 0 ? info.originalW : tex->width;
	const int h = info.originalH > 0 ? info.originalH : tex->height;
	Tex3DS_SubTexture subtex = {(u16)w, (u16)h, 0.0f, 1.0f, (float)w / tex->width, 1.0f - (float)h / tex->height};
	C2D_Image img = {tex, &subtex};
	const float scale = size / std::max(w, h);
	C2D_DrawImageAtRotated(img, cx, cy, 0.6f, rotation, nullptr, scale, scale);
}

void drawRing(float cx, float cy, float radius, float thickness, u32 color) {
	// Short segments around the circle, since citro2d has no stroked circle.
	const int segments = 48;
	for (int i = 0; i < segments; i++) {
		const float a = (float)i / segments * 2.0f * (float)M_PI;
		C2D_DrawCircleSolid(cx + std::cos(a) * radius, cy + std::sin(a) * radius, 0.55f, thickness / 2.0f, color);
	}
}

} // namespace

void drawTop() {
	const u64 now = osGetTime();
	const float dt = lastFrame ? std::min(0.1f, (now - lastFrame) / 1000.0f) : 0.0f;
	lastFrame = now;
	const double t = seconds();

	const u32 accent = ScreenManager::colorAccent();
	C2D_DrawRectSolid(0, 0, 0.1f, 400, 240, ScreenManager::colorBackgroundDark());

	// Soft blobs drifting behind everything.
	for (int i = 0; i < 4; i++) {
		const float phase = (float)i * 1.7f;
		const float x = 200.0f + std::sin((float)t * 0.25f + phase) * 170.0f;
		const float y = 120.0f + std::cos((float)t * 0.18f + phase * 1.3f) * 110.0f;
		C2D_DrawCircleSolid(x, y, 0.15f, 70.0f + 20.0f * std::sin((float)t * 0.4f + phase),
		                    withAlpha(accent, 0.07f));
	}

	auto &client = Discord::DiscordClient::getInstance();
	const Discord::ConnectionState state = client.getState();
	std::string status = client.getStatusMessage();
	int step = 1;
	const float target = targetProgress(state, status, step);
	// Eases toward the real progress so the bar never jumps.
	smoothed += (target - smoothed) * std::min(1.0f, dt * 3.0f);
	if (smoothed > target + 0.2f) {
		smoothed = target;
	}

	const float cx = 200.0f;
	const float cy = 92.0f + std::sin((float)t * 2.0f) * 3.0f;

	// A ring that swells out of the icon and fades, once a second.
	const float pulse = (float)std::fmod(t, 1.4) / 1.4f;
	drawRing(cx, cy, 34.0f + pulse * 26.0f, 2.0f, withAlpha(accent, 0.55f * (1.0f - pulse)));

	// Dots chasing each other around the icon.
	const int dots = 10;
	for (int i = 0; i < dots; i++) {
		const float a = (float)t * 3.2f - i * 0.32f;
		const float alpha = 1.0f - (float)i / dots;
		C2D_DrawCircleSolid(cx + std::cos(a) * 48.0f, cy + std::sin(a) * 48.0f, 0.58f, 3.6f - i * 0.25f,
		                    withAlpha(accent, alpha));
	}

	drawIcon(cx, cy, 58.0f, std::sin((float)t * 1.5f) * 0.06f);

	drawCenteredText(150.0f, 0.6f, 0.62f, 0.62f, ScreenManager::colorText(), "TriCord", 400.0f);

	const float barW = 240.0f;
	const float barX = (400.0f - barW) / 2.0f;
	const float barY = 176.0f;
	drawRoundedRect(barX, barY, 0.6f, barW, 8.0f, 4.0f, ScreenManager::colorBackgroundLight());
	const float fill = std::max(8.0f, barW * std::clamp(smoothed, 0.0f, 1.0f));
	drawRoundedRect(barX, barY, 0.61f, fill, 8.0f, 4.0f, accent);
	// A highlight sliding along the filled part.
	const float shine = barX + std::fmod((float)t * 120.0f, std::max(fill, 1.0f));
	C2D_DrawRectSolid(std::min(shine, barX + fill - 6.0f), barY + 1.0f, 0.62f, 6.0f, 6.0f,
	                  withAlpha(ScreenManager::colorWhite(), 0.35f));

	if (status.empty()) {
		status = TR("login.status.authenticating");
	}
	drawCenteredText(192.0f, 0.6f, 0.45f, 0.45f, ScreenManager::colorText(),
	                 getTruncatedText(status, 380.0f, 0.45f, 0.45f), 400.0f);
	drawCenteredText(210.0f, 0.6f, 0.38f, 0.38f, ScreenManager::colorTextMuted(),
	                 Core::I18n::format(TR("loading.step"), std::to_string(step), "4"), 400.0f);
}

void drawBottom() {
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, ScreenManager::colorBackgroundDark());
	const double t = seconds();
	const int tip = (int)(t / TIP_SECONDS) % TIP_COUNT;
	const float phase = (float)std::fmod(t, TIP_SECONDS);
	// Fades in, holds, fades out.
	const float alpha = std::min({1.0f, phase / 0.4f, ((float)TIP_SECONDS - phase) / 0.4f});

	const float cardX = 14.0f, cardY = 22.0f, cardW = 292.0f, cardH = 112.0f;
	drawRoundedRect(cardX, cardY, 0.3f, cardW, cardH, 12.0f, ScreenManager::colorBackground());
	drawRoundedRect(cardX, cardY, 0.31f, 4.0f, cardH, 2.0f, ScreenManager::colorAccent());
	drawText(cardX + 16.0f, cardY + 10.0f, 0.4f, 0.45f, 0.45f, withAlpha(ScreenManager::colorAccent(), alpha),
	         TR("loading.tip_title"));
	char key[32];
	snprintf(key, sizeof(key), "loading.tip.%d", tip + 1);
	float y = cardY + 30.0f;
	for (const auto &line : MessageUtils::wrapText(TR(key), cardW - 32.0f, 0.42f)) {
		drawText(cardX + 16.0f, y, 0.4f, 0.42f, 0.42f, withAlpha(ScreenManager::colorText(), alpha), line);
		y += 15.0f;
	}

	for (int i = 0; i < TIP_COUNT; i++) {
		drawCircle(160.0f - (TIP_COUNT - 1) * 6.0f + i * 12.0f, cardY + cardH + 12.0f, 0.4f, 2.5f,
		           i == tip ? ScreenManager::colorAccent() : ScreenManager::colorBackgroundLight());
	}

	const std::string warnText = TR("login.loading.many_servers_warning");
	std::vector<std::string> lines;
	size_t pos = 0;
	size_t found;
	while ((found = warnText.find('\n', pos)) != std::string::npos) {
		lines.push_back(warnText.substr(pos, found - pos));
		pos = found + 1;
	}
	lines.push_back(warnText.substr(pos));
	float warnY = 240.0f - 8.0f - (float)lines.size() * 13.0f;
	for (const auto &line : lines) {
		drawCenteredText(warnY, 0.5f, 0.36f, 0.36f, ScreenManager::colorTextMuted(), line, 320.0f);
		warnY += 13.0f;
	}
}

} // namespace LoadingView
} // namespace UI
