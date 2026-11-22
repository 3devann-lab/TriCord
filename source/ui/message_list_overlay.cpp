#include "ui/message_list_overlay.h"
#include "core/i18n.h"
#include "discord/discord_client.h"
#include "ui/screen_manager.h"
#include "utils/markdown.h"
#include "utils/message_utils.h"
#include "utils/redraw.h"
#include <algorithm>
#include <cmath>

namespace UI {

namespace {

const float HEADER_H = 26.0f;
const float LIST_TOP = HEADER_H + 4.0f;
const float LINE_H = 13.0f;
const float TEXT_SCALE = 0.42f;
const int PREVIEW_LINES = 2;

// displayContent wraps mentions as \x01fg;bg;text\x02 for the renderer; keep just the text.
std::string plainText(const std::string &display) {
	std::string out;
	size_t i = 0;
	while (i < display.size()) {
		if (display[i] == '\x01') {
			size_t first = display.find(';', i);
			size_t second = first == std::string::npos ? first : display.find(';', first + 1);
			size_t end = display.find('\x02', i);
			if (second != std::string::npos && end != std::string::npos && second < end) {
				out += display.substr(second + 1, end - second - 1);
				i = end + 1;
				continue;
			}
		}
		out += display[i++];
	}
	return Utils::Markdown::stripFormatting(out);
}

std::string authorName(const Discord::Message &msg) {
	return msg.author.global_name.empty() ? msg.author.username : msg.author.global_name;
}

std::string bodyText(const Discord::Message &msg) {
	std::string text = plainText(msg.displayContent.empty() ? msg.content : msg.displayContent);
	if (text.empty() && !msg.attachments.empty()) {
		text = Core::I18n::format(TR("pins.attachments"), std::to_string(msg.attachments.size()));
	} else if (text.empty() && !msg.stickers.empty()) {
		text = TR("notify.sticker");
	} else if (text.empty() && !msg.embeds.empty()) {
		text = TR("pins.embed");
	}
	return text;
}

} // namespace

MessageListOverlay::MessageListOverlay(Mode mode, const std::string &channelId, const std::string &guildId,
                                       const std::string &channelName, JumpCallback onJump)
    : mode(mode), channelId(channelId), guildId(guildId), channelName(channelName), onJump(std::move(onJump)),
      alive(std::make_shared<bool>(true)) {
	if (mode == Mode::PINS) {
		requestPins();
	}
}

MessageListOverlay::~MessageListOverlay() {
	std::lock_guard<std::recursive_mutex> lock(mutex);
	*alive = false;
}

MessageListOverlay::Row MessageListOverlay::makeRow(const Discord::Message &msg) const {
	Row row;
	row.message = msg;
	row.author = authorName(msg);
	row.when = MessageUtils::formatTimestamp(msg.timestamp);
	if (mode == Mode::SEARCH && wholeServer) {
		if (const Discord::Channel *ch = Discord::DiscordClient::getInstance().getChannelPtr(msg.channelId)) {
			row.where = "#" + ch->name;
		}
	}
	std::vector<std::string> lines = MessageUtils::wrapText(bodyText(msg), 370.0f, TEXT_SCALE);
	if ((int)lines.size() > PREVIEW_LINES) {
		lines.resize(PREVIEW_LINES);
		lines.back() = getTruncatedText(lines.back() + "...", 370.0f, TEXT_SCALE, TEXT_SCALE);
	}
	row.lines = lines;
	return row;
}

float MessageListOverlay::rowHeight(const Row &row) const { return 24.0f + std::max<size_t>(1, row.lines.size()) * LINE_H; }

void MessageListOverlay::requestPins() {
	loading = true;
	failed = false;
	std::shared_ptr<bool> token = alive;
	Discord::DiscordClient::getInstance().fetchPinnedMessages(
	    channelId, [this, token](bool ok, int total, const std::vector<Discord::Message> &found, const std::string &err) {
		    std::lock_guard<std::recursive_mutex> lock(mutex);
		    if (!*token) {
			    return;
		    }
		    onResults(ok, total, found, err, false);
	    });
}

bool MessageListOverlay::askQuery() {
	KeyboardInput input = runKeyboard(TR("search.hint"), query, false, 200, TR("search.button"));
	if (!input.confirmed || input.text.empty()) {
		return false;
	}
	query = input.text;
	return true;
}

void MessageListOverlay::requestSearch(bool nextPage) {
	if (query.empty()) {
		return;
	}
	loading = true;
	failed = false;
	int offset = nextPage ? (int)rows.size() : 0;
	if (!nextPage) {
		rows.clear();
		totalResults = 0;
		selected = 0;
		scroll = targetScroll = 0.0f;
	}
	std::shared_ptr<bool> token = alive;
	std::string scopeChannel = wholeServer ? "" : channelId;
	Discord::DiscordClient::getInstance().searchMessages(
	    guildId, scopeChannel, query, offset,
	    [this, token, nextPage](bool ok, int total, const std::vector<Discord::Message> &found, const std::string &err) {
		    std::lock_guard<std::recursive_mutex> lock(mutex);
		    if (!*token) {
			    return;
		    }
		    onResults(ok, total, found, err, nextPage);
	    });
}

void MessageListOverlay::onResults(bool ok, int total, const std::vector<Discord::Message> &found,
                                   const std::string &error, bool append) {
	loading = false;
	if (!ok) {
		if (error == "indexing" && retries < 5) {
			retries++;
			retryFrames = 180;
			loading = true;
			return;
		}
		failed = true;
		errorHint = error;
		Utils::requestRedraw();
		return;
	}
	retries = 0;
	if (!append) {
		rows.clear();
	}
	for (const auto &msg : found) {
		rows.push_back(makeRow(msg));
	}
	totalResults = std::max(total, (int)rows.size());
	selected = std::clamp(selected, 0, std::max(0, (int)rows.size() - 1));
	Utils::requestRedraw();
}

void MessageListOverlay::keepSelectionVisible() {
	float y = 0.0f;
	for (int i = 0; i < selected && i < (int)rows.size(); i++) {
		y += rowHeight(rows[i]);
	}
	float h = selected < (int)rows.size() ? rowHeight(rows[selected]) : 0.0f;
	float view = 240.0f - LIST_TOP - 4.0f;
	if (y < targetScroll) {
		targetScroll = y;
	} else if (y + h > targetScroll + view) {
		targetScroll = y + h - view;
	}
}

bool MessageListOverlay::isAnimating() const {
	std::lock_guard<std::recursive_mutex> lock(mutex);
	return loading || std::fabs(scroll - targetScroll) > 0.5f;
}

void MessageListOverlay::update(u32 kDown, u32 kHeld) {
	std::unique_lock<std::recursive_mutex> lock(mutex);
	spinner += 0.15f;

	if (mode == Mode::SEARCH && query.empty() && !loading) {
		lock.unlock();
		bool asked = askQuery();
		lock.lock();
		if (!asked) {
			finish();
			return;
		}
		requestSearch(false);
		return;
	}

	if (retryFrames > 0 && --retryFrames == 0) {
		if (mode == Mode::SEARCH) {
			requestSearch(!rows.empty());
		} else {
			requestPins();
		}
	}

	if (kDown & KEY_B) {
		finish();
		return;
	}

	int count = (int)rows.size();
	int step = 0;
	if (kDown & KEY_DOWN) {
		step = 1;
		repeatTimer = 18;
	} else if (kDown & KEY_UP) {
		step = -1;
		repeatTimer = 18;
	} else if ((kHeld & (KEY_DOWN | KEY_UP | KEY_CSTICK_DOWN | KEY_CSTICK_UP)) && --repeatTimer <= 0) {
		step = (kHeld & (KEY_DOWN | KEY_CSTICK_DOWN)) ? 1 : -1;
		repeatTimer = 5;
	}
	if (step != 0 && count > 0) {
		selected = std::clamp(selected + step, 0, count - 1);
		keepSelectionVisible();
	}

	// The last row loaded asks for the next page.
	if (mode == Mode::SEARCH && !loading && !failed && count > 0 && selected >= count - 2 && count < totalResults) {
		requestSearch(true);
	}

	if ((kDown & KEY_A) && selected >= 0 && selected < count) {
		Discord::Message chosen = rows[selected].message;
		JumpCallback jump = onJump;
		finish();
		lock.unlock();
		if (jump) {
			jump(chosen);
		}
		return;
	}

	if (mode == Mode::SEARCH && (kDown & KEY_Y)) {
		lock.unlock();
		bool asked = askQuery();
		lock.lock();
		if (asked) {
			retries = 0;
			requestSearch(false);
		}
	}
	bool inGuild = !guildId.empty() && guildId != "DM";
	if (mode == Mode::SEARCH && inGuild && (kDown & KEY_X) && !loading) {
		wholeServer = !wholeServer;
		retries = 0;
		requestSearch(false);
	}
	if (mode == Mode::PINS && failed && (kDown & KEY_Y)) {
		requestPins();
	}

	scroll += (targetScroll - scroll) * 0.4f;
	if (std::fabs(targetScroll - scroll) < 0.5f) {
		scroll = targetScroll;
	}
}

void MessageListOverlay::renderTop() {
	std::lock_guard<std::recursive_mutex> lock(mutex);
	C2D_DrawRectSolid(0, 0, 0.0f, 400.0f, 240.0f, ScreenManager::colorBackground());

	std::string title;
	std::string right;
	if (mode == Mode::PINS) {
		title = Core::I18n::format(TR("pins.title"), channelName);
		if (!rows.empty()) {
			right = std::to_string(rows.size());
		}
	} else {
		title = query.empty() ? TR("search.title") : Core::I18n::format(TR("search.title_query"), query);
		if (!rows.empty() || (!loading && !failed && !query.empty())) {
			right = Core::I18n::format(TR("search.results"), std::to_string(totalResults));
		}
	}

	float y = LIST_TOP - scroll;
	for (int i = 0; i < (int)rows.size(); i++) {
		const Row &row = rows[i];
		float h = rowHeight(row);
		if (y + h < HEADER_H || y > 240.0f) {
			y += h;
			continue;
		}
		if (i == selected) {
			drawRoundedRect(6.0f, y, 0.3f, 388.0f, h - 3.0f, 6.0f, ScreenManager::colorBackgroundLight());
			C2D_DrawRectSolid(6.0f, y + 4.0f, 0.31f, 3.0f, h - 11.0f, ScreenManager::colorAccent());
		}
		float x = 14.0f;
		drawText(x, y + 4.0f, 0.4f, 0.45f, 0.45f, ScreenManager::colorText(), getTruncatedText(row.author, 180.0f, 0.45f, 0.45f));
		float ax = x + std::min(180.0f, measureText(row.author, 0.45f, 0.45f)) + 8.0f;
		std::string meta = row.when + (row.where.empty() ? "" : "  " + row.where);
		drawText(ax, y + 6.0f, 0.4f, 0.36f, 0.36f, ScreenManager::colorTextMuted(),
		         getTruncatedText(meta, 390.0f - ax, 0.36f, 0.36f));
		float ly = y + 20.0f;
		for (const auto &line : row.lines) {
			drawText(x, ly, 0.4f, TEXT_SCALE, TEXT_SCALE, ScreenManager::colorText(), line);
			ly += LINE_H;
		}
		y += h;
	}

	if (rows.empty()) {
		std::string msg;
		if (loading) {
			msg = retries > 0 ? TR("search.indexing") : TR("common.loading");
		} else if (failed) {
			msg = errorHint == "forbidden" ? TR("pins.forbidden") : TR("pins.failed");
		} else if (mode == Mode::PINS) {
			msg = TR("pins.empty");
		} else if (!query.empty()) {
			msg = TR("search.none");
		}
		if (!msg.empty()) {
			drawCenteredText(112.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(), msg, 400.0f);
		}
	} else if (loading) {
		float cx = 200.0f, cy = 228.0f;
		for (int i = 0; i < 8; i++) {
			float a = spinner + i * 0.785f;
			u32 c = (ScreenManager::colorAccent() & 0x00FFFFFF) | ((u32)(40 + i * 26) << 24);
			drawCircle(cx + cosf(a) * 6.0f, cy + sinf(a) * 6.0f, 0.5f, 1.6f, c);
		}
	}

	drawOverlayHeader(title, right);
}

void MessageListOverlay::renderBottom() {
	std::lock_guard<std::recursive_mutex> lock(mutex);
	C2D_DrawRectSolid(0, 0, 0.0f, 320.0f, 240.0f, ScreenManager::colorBackgroundDark());

	if (selected >= 0 && selected < (int)rows.size()) {
		const Row &row = rows[selected];
		drawText(10.0f, 8.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorText(), getTruncatedText(row.author, 300.0f, 0.5f, 0.5f));
		std::string meta = row.when + (row.where.empty() ? "" : "  " + row.where);
		drawText(10.0f, 24.0f, 0.5f, 0.38f, 0.38f, ScreenManager::colorTextMuted(), getTruncatedText(meta, 300.0f, 0.38f, 0.38f));
		C2D_DrawRectSolid(10.0f, 40.0f, 0.5f, 300.0f, 1.0f, ScreenManager::colorSeparator());
		std::vector<std::string> lines = MessageUtils::wrapText(bodyText(row.message), 300.0f, 0.45f);
		float y = 46.0f;
		for (size_t i = 0; i < lines.size() && y < 240.0f - 40.0f; i++) {
			drawText(10.0f, y, 0.5f, 0.45f, 0.45f, ScreenManager::colorText(), lines[i]);
			y += 14.0f;
		}
	} else if (mode == Mode::SEARCH) {
		std::string scope = wholeServer ? TR("search.scope_server") : TR("search.scope_channel");
		drawCenteredText(100.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), scope, 320.0f);
	}

	std::string hints = ": " + TR("pins.jump") + "  : " + TR("common.close");
	if (mode == Mode::SEARCH) {
		hints += "  : " + TR("search.new");
		if (!guildId.empty() && guildId != "DM") {
			hints += "  : " + (wholeServer ? TR("search.scope_channel_short") : TR("search.scope_server_short"));
		}
	} else if (failed) {
		hints += "  : " + TR("common.retry");
	}
	drawKeyHints(hints);
}

} // namespace UI
