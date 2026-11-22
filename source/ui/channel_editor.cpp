#include "ui/channel_editor.h"
#include "core/i18n.h"
#include "ui/overlay.h"
#include "ui/screen_manager.h"
#include "utils/redraw.h"
#include <algorithm>

namespace UI {

namespace {

const int SLOWMODES[] = {0, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 21600};
constexpr int SLOWMODE_COUNT = sizeof(SLOWMODES) / sizeof(SLOWMODES[0]);

std::string slowmodeLabel(int seconds) {
	if (seconds <= 0) {
		return TR("channel.edit.slowmode_off");
	}
	if (seconds < 60) {
		return std::to_string(seconds) + "s";
	}
	if (seconds < 3600) {
		return std::to_string(seconds / 60) + "m";
	}
	return std::to_string(seconds / 3600) + "h";
}

} // namespace

void ChannelEditor::show(const Discord::Channel &channel) {
	open = true;
	saving = false;
	saved = false;
	channelId = channel.id;
	// Voice and stage channels have no topic, slowmode or NSFW flag worth editing here.
	textChannel = channel.type == 0 || channel.type == 5 || channel.type == 15;
	edit.name = channel.name;
	edit.topic = channel.topic;
	edit.slowmode = channel.rate_limit_per_user;
	edit.nsfw = channel.nsfw;
	row = 0;
	result.reset();
}

bool ChannelEditor::consumeSaved() {
	const bool was = saved;
	saved = false;
	return was;
}

int ChannelEditor::rowCount() const { return textChannel ? 5 : 2; }

ChannelEditor::Row ChannelEditor::rowAt(int index) const {
	if (!textChannel) {
		return index == 0 ? NAME : SAVE;
	}
	return (Row)index;
}

void ChannelEditor::stepSlowmode(int delta) {
	int index = 0;
	for (int i = 0; i < SLOWMODE_COUNT; i++) {
		if (SLOWMODES[i] <= edit.slowmode) {
			index = i;
		}
	}
	index = std::clamp(index + delta, 0, SLOWMODE_COUNT - 1);
	edit.slowmode = SLOWMODES[index];
}

void ChannelEditor::save() {
	if (edit.name.empty()) {
		ScreenManager::getInstance().showToast(TR("channel.edit.name_required"));
		return;
	}
	saving = true;
	result = std::make_shared<Result>();
	std::shared_ptr<Result> pending = result;
	Discord::DiscordClient::getInstance().editChannel(channelId, edit,
	                                                  [pending](bool ok, const std::string &message) {
		                                                  std::lock_guard<std::mutex> lock(pending->mutex);
		                                                  pending->done = true;
		                                                  pending->ok = ok;
		                                                  pending->message = message;
		                                                  Utils::requestRedraw();
	                                                  });
}

void ChannelEditor::update(u32 kDown, u32 kHeld) {
	(void)kHeld;
	if (!open) {
		return;
	}
	if (saving) {
		if (!result) {
			saving = false;
			return;
		}
		std::lock_guard<std::mutex> lock(result->mutex);
		if (result->done) {
			saving = false;
			if (result->ok) {
				ScreenManager::getInstance().showToast(TR("channel.edit.saved"));
				open = false;
				saved = true;
			} else {
				ScreenManager::getInstance().showToast(Core::I18n::format(TR("channel.edit.failed"), result->message));
			}
		}
		return;
	}

	if (kDown & KEY_B) {
		open = false;
		return;
	}
	if (kDown & KEY_DOWN) {
		row = std::min(row + 1, rowCount() - 1);
	} else if (kDown & KEY_UP) {
		row = std::max(row - 1, 0);
	}

	const Row current = rowAt(row);
	if (kDown & (KEY_LEFT | KEY_RIGHT)) {
		const int delta = (kDown & KEY_RIGHT) ? 1 : -1;
		if (current == SLOWMODE) {
			stepSlowmode(delta);
		} else if (current == NSFW) {
			edit.nsfw = !edit.nsfw;
		}
	}
	if (kDown & KEY_A) {
		if (current == NAME) {
			KeyboardInput input = runKeyboard(TR("channel.edit.name"), edit.name, false, 100, TR("common.ok"));
			if (input.confirmed && !input.text.empty()) {
				edit.name = input.text;
			}
		} else if (current == TOPIC) {
			KeyboardInput input = runKeyboard(TR("channel.edit.topic"), edit.topic, true, 1024, TR("common.ok"));
			if (input.confirmed) {
				edit.topic = input.text;
			}
		} else if (current == SLOWMODE) {
			stepSlowmode(1);
		} else if (current == NSFW) {
			edit.nsfw = !edit.nsfw;
		} else if (current == SAVE) {
			save();
		}
	}
	if (kDown & KEY_Y) {
		save();
	}
}

void ChannelEditor::render() {
	if (!open) {
		return;
	}
	const float w = 330.0f;
	const float rowH = 30.0f;
	const float h = 44.0f + rowCount() * rowH + 26.0f;
	const float x = (400.0f - w) / 2.0f;
	const float y = (240.0f - h) / 2.0f;
	drawOverlay(0.9f);
	drawPopupBackground(x, y, w, h, 0.92f, 12.0f);
	drawCenteredText(y + 10.0f, 0.95f, 0.52f, 0.52f, ScreenManager::colorText(), TR("channel.edit.title"), 400.0f);

	for (int i = 0; i < rowCount(); i++) {
		const Row r = rowAt(i);
		const float ry = y + 36.0f + i * rowH;
		const bool selected = i == row;
		drawPopupMenuItem(x + 8.0f, ry, w - 16.0f, rowH - 4.0f, 0.93f, selected, ScreenManager::colorSelection());
		const u32 color = selected ? ScreenManager::colorWhite() : ScreenManager::colorText();
		std::string label;
		std::string value;
		switch (r) {
		case NAME:
			label = TR("channel.edit.name");
			value = edit.name;
			break;
		case TOPIC:
			label = TR("channel.edit.topic");
			value = edit.topic.empty() ? TR("common.no_topic") : edit.topic;
			break;
		case SLOWMODE:
			label = TR("channel.edit.slowmode");
			value = "< " + slowmodeLabel(edit.slowmode) + " >";
			break;
		case NSFW:
			label = TR("channel.edit.nsfw");
			value = edit.nsfw ? TR("common.enabled") : TR("common.disabled");
			break;
		case SAVE:
			label = saving ? TR("common.loading") : TR("common.save");
			break;
		}
		if (r == SAVE) {
			drawCenteredText(ry + 6.0f, 0.95f, 0.48f, 0.48f, color, label, 400.0f);
			continue;
		}
		drawText(x + 18.0f, ry + 6.0f, 0.95f, 0.45f, 0.45f, color, label);
		const float valueW = w - 130.0f;
		const std::string shown = getTruncatedText(value, valueW, 0.42f, 0.42f);
		drawText(x + w - 18.0f - measureText(shown, 0.42f, 0.42f), ry + 7.0f, 0.95f, 0.42f, 0.42f,
		         selected ? ScreenManager::colorWhite() : ScreenManager::colorTextMuted(), shown);
	}
	drawCenteredText(y + h - 20.0f, 0.95f, 0.38f, 0.38f, ScreenManager::colorTextMuted(),
	                 ": " + TR("common.select") + "  : " + TR("common.save") + "  : " +
	                     TR("common.cancel"),
	                 400.0f);
}

} // namespace UI
