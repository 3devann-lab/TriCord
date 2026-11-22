#include "ui/gif_picker.h"
#include "core/i18n.h"
#include "discord/discord_client.h"
#include "ui/image_manager.h"
#include "ui/screen_manager.h"
#include "utils/file_utils.h"
#include "utils/message_utils.h"
#include "utils/redraw.h"
#include <algorithm>
#include <mutex>

namespace UI {

namespace {

constexpr float CELL_W = 128.0f;
constexpr float CELL_H = 66.0f;
constexpr float GAP = 4.0f;
constexpr float GRID_TOP = 30.0f;

// Kept for the session, so reopening the picker is instant.
std::mutex cacheMutex;
std::vector<Discord::FavoriteGif> cachedGifs;
bool cacheValid = false;

std::string shortLabel(const Discord::FavoriteGif &gif) {
	// "https://tenor.com/view/cat-dance-gif-123" reads best as "cat dance".
	std::string url = gif.url;
	const size_t view = url.find("/view/");
	if (view != std::string::npos) {
		std::string slug = url.substr(view + 6);
		const size_t query = slug.find('?');
		if (query != std::string::npos) {
			slug.resize(query);
		}
		const size_t gifWord = slug.rfind("-gif-");
		if (gifWord != std::string::npos) {
			slug.resize(gifWord);
		}
		std::replace(slug.begin(), slug.end(), '-', ' ');
		if (!slug.empty()) {
			return slug;
		}
	}
	return Utils::File::baseName(url.substr(0, url.find('?')));
}

bool isVideoGif(const Discord::FavoriteGif &gif) {
	return gif.format == 2 || Utils::File::isVideoPath(gif.src.substr(0, gif.src.find('?')));
}

} // namespace

struct GifPicker::State {
	std::mutex mutex;
	bool loading = false;
	bool failed = false;
	bool alive = true;
	std::vector<Discord::FavoriteGif> gifs;
};

GifPicker::GifPicker(SendCallback onSend, PlayCallback onPlay)
    : onSend(std::move(onSend)), onPlay(std::move(onPlay)), state(std::make_shared<State>()) {
	bool cached = false;
	{
		std::lock_guard<std::mutex> lock(cacheMutex);
		cached = cacheValid;
		if (cached) {
			state->gifs = cachedGifs;
		}
	}
	if (!cached) {
		refresh();
	}
}

GifPicker::~GifPicker() {
	std::lock_guard<std::mutex> lock(state->mutex);
	state->alive = false;
}

std::vector<Discord::FavoriteGif> GifPicker::gifs() const {
	std::lock_guard<std::mutex> lock(state->mutex);
	return state->gifs;
}

void GifPicker::refresh() {
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		if (state->loading) {
			return;
		}
		state->loading = true;
		state->failed = false;
	}
	std::shared_ptr<State> shared = state;
	Discord::DiscordClient::getInstance().fetchFavoriteGifs(
	    [shared](bool success, const std::vector<Discord::FavoriteGif> &result) {
		    std::lock_guard<std::mutex> lock(shared->mutex);
		    shared->loading = false;
		    shared->failed = !success;
		    if (success) {
			    shared->gifs = result;
			    std::lock_guard<std::mutex> cacheLock(cacheMutex);
			    cachedGifs = result;
			    cacheValid = true;
		    }
		    Utils::requestRedraw();
	    });
}

void GifPicker::move(int delta) {
	const int count = (int)gifs().size();
	if (count == 0) {
		return;
	}
	selected = std::clamp(selected + delta, 0, count - 1);
	const int row = selected / COLUMNS;
	if (row < firstRow) {
		firstRow = row;
	} else if (row >= firstRow + ROWS) {
		firstRow = row - ROWS + 1;
	}
}

void GifPicker::update(u32 kDown, u32 kHeld) {
	if (kDown & KEY_B) {
		finish();
		return;
	}
	if (kDown & KEY_X) {
		refresh();
		return;
	}

	const std::vector<Discord::FavoriteGif> list = gifs();
	if (selected >= (int)list.size()) {
		selected = std::max(0, (int)list.size() - 1);
	}

	if ((kDown & KEY_A) && selected < (int)list.size()) {
		const Discord::FavoriteGif gif = list[selected];
		finish();
		if (onSend) {
			onSend(gif);
		}
		return;
	}
	if ((kDown & KEY_Y) && selected < (int)list.size() && onPlay && isVideoGif(list[selected])) {
		onPlay(list[selected]);
		return;
	}

	const u32 dirs = KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT;
	u32 step = 0;
	if (kDown & dirs) {
		step = kDown & dirs;
		repeatKey = step;
		repeatTimer = 20;
	} else if (repeatKey && (kHeld & repeatKey)) {
		if (--repeatTimer <= 0) {
			step = repeatKey;
			repeatTimer = 5;
		}
	} else {
		repeatKey = 0;
	}
	if (step & KEY_LEFT) {
		move(-1);
	} else if (step & KEY_RIGHT) {
		move(1);
	} else if (step & KEY_UP) {
		move(-COLUMNS);
	} else if (step & KEY_DOWN) {
		move(COLUMNS);
	}
	if (kDown & KEY_L) {
		move(-COLUMNS * ROWS);
	} else if (kDown & KEY_R) {
		move(COLUMNS * ROWS);
	}
}

void GifPicker::renderTop() {
	C2D_DrawRectSolid(0, 0, 0.1f, 400, 240, ScreenManager::colorBackground());
	const std::vector<Discord::FavoriteGif> list = gifs();
	bool loading = false;
	bool failed = false;
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		loading = state->loading;
		failed = state->failed;
	}

	std::string counter = list.empty() ? "" : std::to_string(selected + 1) + " / " + std::to_string(list.size());
	drawOverlayHeader(TR("gif.title"), counter);

	if (list.empty()) {
		const std::string text = loading ? TR("common.loading") : failed ? TR("gif.load_failed") : TR("gif.empty");
		drawCenteredText(110.0f, 0.5f, 0.5f, 0.5f, failed ? ScreenManager::colorError() : ScreenManager::colorTextMuted(),
		                 text, 400.0f);
		return;
	}

	ImageManager &images = ImageManager::getInstance();
	const int first = firstRow * COLUMNS;
	// One row ahead, so scrolling finds the next previews already loaded.
	const int last = std::min((int)list.size(), first + COLUMNS * (ROWS + 1));
	for (int i = first; i < last; i++) {
		const std::string url = Discord::DiscordClient::favoriteGifPreviewUrl(list[i]);
		if (url.empty()) {
			continue;
		}
		const auto info = images.getImageInfo(url);
		if (!info.tex && !info.failed) {
			images.prefetch(url, list[i].width, list[i].height, Network::RequestPriority::INTERACTIVE);
		}
	}

	for (int i = first; i < std::min((int)list.size(), first + COLUMNS * ROWS); i++) {
		const int cell = i - first;
		const float x = GAP + (cell % COLUMNS) * (CELL_W + GAP);
		const float y = GRID_TOP + (cell / COLUMNS) * (CELL_H + GAP);
		const bool isSelected = i == selected;

		if (isSelected) {
			drawRoundedRect(x - 2.0f, y - 2.0f, 0.3f, CELL_W + 4.0f, CELL_H + 4.0f, 6.0f,
			                ScreenManager::colorSelection());
		}
		drawRoundedRect(x, y, 0.35f, CELL_W, CELL_H, 4.0f, ScreenManager::colorBackgroundDark());

		const std::string url = Discord::DiscordClient::favoriteGifPreviewUrl(list[i]);
		const auto info = url.empty() ? ImageManager::ImageInfo() : images.getImageInfo(url);
		if (info.tex && info.originalW > 0 && info.originalH > 0) {
			const float scale = std::min(CELL_W / info.originalW, CELL_H / info.originalH);
			const float w = info.originalW * scale;
			const float h = info.originalH * scale;
			const float uMax = (float)info.originalW / info.tex->width;
			const float vMax = (float)info.originalH / info.tex->height;
			Tex3DS_SubTexture subtex = {(u16)info.originalW, (u16)info.originalH, 0.0f, 1.0f, uMax, 1.0f - vMax};
			C2D_Image img = {info.tex, &subtex};
			C2D_DrawImageAt(img, x + (CELL_W - w) / 2.0f, y + (CELL_H - h) / 2.0f, 0.4f, nullptr, scale, scale);
			images.markDrawn(info.animation);
		} else {
			const std::string label = getTruncatedText(shortLabel(list[i]), CELL_W - 8.0f, 0.35f, 0.35f);
			drawText(x + 4.0f, y + CELL_H / 2.0f - 6.0f, 0.4f, 0.35f, 0.35f, ScreenManager::colorTextMuted(),
			         info.failed || url.empty() ? label : TR("common.loading"));
		}
	}
}

void GifPicker::renderBottom() {
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, ScreenManager::colorBackgroundDark());
	drawText(10.0f, 8.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorText(), TR("gif.title"));
	C2D_DrawRectSolid(10, 30, 0.5f, 300, 1, ScreenManager::colorSeparator());

	const std::vector<Discord::FavoriteGif> list = gifs();
	bool canPlay = false;
	if (selected < (int)list.size()) {
		const Discord::FavoriteGif &gif = list[selected];
		canPlay = onPlay && isVideoGif(gif);
		drawText(10.0f, 38.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorText(),
		         getTruncatedText(shortLabel(gif), 300.0f, 0.45f, 0.45f));
		const auto lines = MessageUtils::wrapText(gif.url, 300.0f, 0.38f);
		float y = 56.0f;
		for (size_t i = 0; i < lines.size() && i < 4; i++) {
			drawText(10.0f, y, 0.5f, 0.38f, 0.38f, ScreenManager::colorLink(), lines[i]);
			y += 13.0f;
		}
		if (gif.width > 0 && gif.height > 0) {
			drawText(10.0f, y + 4.0f, 0.5f, 0.38f, 0.38f, ScreenManager::colorTextMuted(),
			         std::to_string(gif.width) + "×" + std::to_string(gif.height));
		}
	}

	drawText(10.0f, 160.0f, 0.5f, 0.4f, 0.4f, ScreenManager::colorTextMuted(), TR("gif.hint"));

	std::string hints = ": " + TR("common.send") + "  : " + TR("common.close") + "  : " +
	                    TR("gif.refresh");
	if (canPlay) {
		hints += "  : " + TR("video.play");
	}
	drawKeyHints(hints);
}

} // namespace UI
