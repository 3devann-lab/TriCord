#include "ui/command_picker.h"
#include "core/i18n.h"
#include "discord/discord_client.h"
#include "ui/screen_manager.h"
#include "utils/message_utils.h"
#include "utils/redraw.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <mutex>

namespace UI {

namespace {

constexpr float ROW_H = 24.0f;
constexpr float LIST_TOP = 30.0f;

// Commands rarely change, so each channel's list is kept for the session.
std::mutex cacheMutex;
std::map<std::string, std::vector<Discord::ApplicationCommand>> cache;

std::string lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}

bool isNumeric(const std::string &text, bool allowFraction) {
	if (text.empty()) {
		return false;
	}
	char *end = nullptr;
	if (allowFraction) {
		strtod(text.c_str(), &end);
	} else {
		strtoll(text.c_str(), &end, 10);
	}
	return end && *end == '\0';
}

} // namespace

struct CommandPicker::Shared {
	std::mutex mutex;
	bool loading = true;
	bool failed = false;
	std::vector<Discord::ApplicationCommand> commands;
};

CommandPicker::CommandPicker(const std::string &channelId, People people, RunCallback onRun)
    : channelId(channelId), people(std::move(people)), onRun(std::move(onRun)), shared(std::make_shared<Shared>()) {
	guildId = Discord::DiscordClient::getInstance().getGuildIdFromChannel(channelId);
	{
		std::lock_guard<std::mutex> lock(cacheMutex);
		auto it = cache.find(channelId);
		if (it != cache.end()) {
			shared->commands = it->second;
			shared->loading = false;
			return;
		}
	}
	std::shared_ptr<Shared> state = shared;
	const std::string key = channelId;
	Discord::DiscordClient::getInstance().fetchApplicationCommands(
	    channelId, [state, key](bool success, const std::vector<Discord::ApplicationCommand> &commands) {
		    {
			    std::lock_guard<std::mutex> lock(state->mutex);
			    state->loading = false;
			    state->failed = !success;
			    state->commands = commands;
		    }
		    if (success) {
			    std::lock_guard<std::mutex> lock(cacheMutex);
			    cache[key] = commands;
		    }
		    Utils::requestRedraw();
	    });
}

CommandPicker::~CommandPicker() = default;

bool CommandPicker::isAnimating() const {
	std::lock_guard<std::mutex> lock(shared->mutex);
	return shared->loading;
}

std::vector<Discord::ApplicationCommand> CommandPicker::filtered() const {
	std::vector<Discord::ApplicationCommand> out;
	const std::string needle = lower(filter);
	std::lock_guard<std::mutex> lock(shared->mutex);
	for (const auto &cmd : shared->commands) {
		if (needle.empty() || lower(cmd.name).find(needle) != std::string::npos ||
		    lower(cmd.applicationName).find(needle) != std::string::npos) {
			out.push_back(cmd);
		}
	}
	return out;
}

std::string CommandPicker::typeLabel(int type) {
	using namespace Discord::CommandOptionType;
	switch (type) {
	case STRING:
		return TR("commands.type.text");
	case INTEGER:
		return TR("commands.type.integer");
	case NUMBER:
		return TR("commands.type.number");
	case BOOLEAN:
		return TR("commands.type.boolean");
	case USER:
		return TR("commands.type.user");
	case CHANNEL:
		return TR("commands.type.channel");
	case ROLE:
		return TR("commands.type.role");
	case MENTIONABLE:
		return TR("commands.type.mentionable");
	case ATTACHMENT:
		return TR("commands.type.attachment");
	default:
		return "";
	}
}

void CommandPicker::pickCommand(const Discord::ApplicationCommand &picked) {
	command = picked;
	chain.clear();
	enterOptions(command.options);
}

void CommandPicker::enterOptions(const std::vector<Discord::CommandOption> &options) {
	using namespace Discord::CommandOptionType;
	const bool hasSub = std::any_of(options.begin(), options.end(), [](const Discord::CommandOption &o) {
		return o.type == SUB_COMMAND || o.type == SUB_COMMAND_GROUP;
	});
	cursor = 0;
	scroll = 0;
	if (hasSub) {
		subOptions = options;
		mode = Mode::SUBCOMMAND;
		return;
	}
	form = options;
	// Required options first, as Discord lists them.
	std::stable_sort(form.begin(), form.end(), [](const Discord::CommandOption &a, const Discord::CommandOption &b) {
		return a.required && !b.required;
	});
	values.assign(form.size(), "");
	labels.assign(form.size(), "");
	mode = Mode::FORM;
}

int CommandPicker::rowCount() const {
	switch (mode) {
	case Mode::LIST:
		return (int)filtered().size();
	case Mode::SUBCOMMAND:
		return (int)subOptions.size();
	case Mode::FORM:
		return (int)form.size() + 1;
	case Mode::CHOICE:
		return (int)choices.size();
	}
	return 0;
}

void CommandPicker::move(int delta, int count) {
	if (count <= 0) {
		cursor = 0;
		return;
	}
	cursor = std::clamp(cursor + delta, 0, count - 1);
	if (cursor < scroll) {
		scroll = cursor;
	} else if (cursor >= scroll + VISIBLE_ROWS) {
		scroll = cursor - VISIBLE_ROWS + 1;
	}
}

void CommandPicker::openChoices(int row) {
	using namespace Discord::CommandOptionType;
	const Discord::CommandOption &option = form[row];
	choices.clear();
	auto &client = Discord::DiscordClient::getInstance();

	if (!option.choices.empty()) {
		for (const auto &c : option.choices) {
			choices.push_back({c.name, c.value, ""});
		}
	} else if (option.type == BOOLEAN) {
		choices.push_back({TR("common.yes"), "true", ""});
		choices.push_back({TR("common.no"), "false", ""});
	} else {
		// Every id-valued option can also be typed in.
		choices.push_back({TR("commands.enter_id"), "", ""});
		if (option.type == USER || option.type == MENTIONABLE) {
			for (const auto &person : people) {
				choices.push_back({person.second, person.first, TR("commands.type.user")});
			}
		}
		if ((option.type == CHANNEL) && !guildId.empty() && guildId != "DM") {
			std::lock_guard<std::recursive_mutex> lock(client.getMutex());
			if (const Discord::Guild *guild = client.getGuildPtr(guildId)) {
				for (const auto &ch : guild->channels) {
					const bool allowed = option.channelTypes.empty()
					                         ? ch.type != 4
					                         : std::find(option.channelTypes.begin(), option.channelTypes.end(),
					                                     ch.type) != option.channelTypes.end();
					if (allowed) {
						choices.push_back({"#" + ch.name, ch.id, ""});
					}
				}
			}
		}
		if ((option.type == ROLE || option.type == MENTIONABLE) && !guildId.empty() && guildId != "DM") {
			std::lock_guard<std::recursive_mutex> lock(client.getMutex());
			if (const Discord::Guild *guild = client.getGuildPtr(guildId)) {
				for (const auto &role : guild->roles) {
					choices.push_back({"@" + role.name, role.id, TR("commands.type.role")});
				}
			}
		}
	}
	choiceRow = row;
	mode = Mode::CHOICE;
	cursor = 0;
	scroll = 0;
}

void CommandPicker::editRow(int row) {
	using namespace Discord::CommandOptionType;
	if (row == (int)form.size()) {
		run();
		return;
	}
	const Discord::CommandOption &option = form[row];
	if (option.type == ATTACHMENT) {
		ScreenManager::getInstance().showToast(TR("commands.attachment_unsupported"));
		return;
	}
	const bool picker = !option.choices.empty() || option.type == BOOLEAN || option.type == USER ||
	                    option.type == CHANNEL || option.type == ROLE || option.type == MENTIONABLE;
	if (picker) {
		openChoices(row);
		return;
	}
	KeyboardInput input = runKeyboard(option.description.empty() ? option.name : option.description, values[row],
	                                  option.type == STRING, option.type == STRING ? 2000 : 32, TR("common.ok"));
	if (!input.confirmed) {
		return;
	}
	if ((option.type == INTEGER || option.type == NUMBER) && !input.text.empty() &&
	    !isNumeric(input.text, option.type == NUMBER)) {
		ScreenManager::getInstance().showToast(TR("commands.not_a_number"));
		return;
	}
	values[row] = input.text;
	labels[row] = input.text;
}

void CommandPicker::run() {
	using namespace Discord::CommandOptionType;
	for (size_t i = 0; i < form.size(); i++) {
		if (form[i].required && values[i].empty()) {
			ScreenManager::getInstance().showToast(Core::I18n::format(TR("commands.missing"), form[i].name));
			cursor = (int)i;
			return;
		}
	}
	std::vector<Discord::CommandValue> leaf;
	for (size_t i = 0; i < form.size(); i++) {
		if (!values[i].empty()) {
			leaf.push_back({form[i].type, form[i].name, values[i], {}});
		}
	}
	// Wrap in the subcommand and group picked on the way here, innermost last.
	std::vector<Discord::CommandValue> options = leaf;
	for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
		options = {{it->type, it->name, "", options}};
	}
	finish();
	if (onRun) {
		onRun(command, options);
	}
}

void CommandPicker::update(u32 kDown, u32 kHeld) {
	const int count = rowCount();
	u32 step = 0;
	if (kDown & (KEY_UP | KEY_DOWN)) {
		step = kDown & (KEY_UP | KEY_DOWN);
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
	if (step) {
		move((step & KEY_DOWN) ? 1 : -1, count);
	}
	if (kDown & KEY_LEFT) {
		move(-VISIBLE_ROWS, count);
	} else if (kDown & KEY_RIGHT) {
		move(VISIBLE_ROWS, count);
	}

	switch (mode) {
	case Mode::LIST: {
		if (kDown & KEY_B) {
			finish();
			return;
		}
		if (kDown & KEY_Y) {
			KeyboardInput input = runKeyboard(TR("commands.search_hint"), filter, false, 64, TR("common.search"));
			if (input.confirmed) {
				filter = input.text;
				cursor = scroll = 0;
			}
			return;
		}
		const auto list = filtered();
		if ((kDown & KEY_A) && cursor < (int)list.size()) {
			pickCommand(list[cursor]);
		}
		return;
	}
	case Mode::SUBCOMMAND:
		if (kDown & KEY_B) {
			if (chain.empty()) {
				mode = Mode::LIST;
			} else {
				chain.pop_back();
				enterOptions(chain.empty() ? command.options : chain.back().options);
			}
			cursor = scroll = 0;
			return;
		}
		if ((kDown & KEY_A) && cursor < (int)subOptions.size()) {
			const Discord::CommandOption picked = subOptions[cursor];
			chain.push_back(picked);
			enterOptions(picked.options);
		}
		return;
	case Mode::FORM:
		if (kDown & KEY_B) {
			if (chain.empty()) {
				mode = Mode::LIST;
			} else {
				chain.pop_back();
				enterOptions(chain.empty() ? command.options : chain.back().options);
			}
			cursor = scroll = 0;
			return;
		}
		if (kDown & KEY_A) {
			editRow(cursor);
			return;
		}
		if ((kDown & KEY_X) && cursor < (int)form.size()) {
			values[cursor].clear();
			labels[cursor].clear();
		}
		if (kDown & KEY_Y) {
			run();
		}
		return;
	case Mode::CHOICE:
		if (kDown & KEY_B) {
			mode = Mode::FORM;
			cursor = choiceRow;
			scroll = std::max(0, choiceRow - VISIBLE_ROWS + 1);
			return;
		}
		if ((kDown & KEY_A) && cursor < (int)choices.size()) {
			const Choice picked = choices[cursor];
			if (picked.value.empty()) {
				KeyboardInput input =
				    runKeyboard(TR("commands.enter_id"), values[choiceRow], false, 32, TR("common.ok"));
				if (!input.confirmed || !isNumeric(input.text, false)) {
					return;
				}
				values[choiceRow] = input.text;
				labels[choiceRow] = input.text;
			} else {
				values[choiceRow] = picked.value;
				labels[choiceRow] = picked.label;
			}
			mode = Mode::FORM;
			cursor = choiceRow;
			scroll = std::max(0, choiceRow - VISIBLE_ROWS + 1);
		}
		return;
	}
}

void CommandPicker::renderTop() {
	C2D_DrawRectSolid(0, 0, 0.1f, 400, 240, ScreenManager::colorBackground());

	std::string title = TR("commands.title");
	if (mode != Mode::LIST) {
		title = "/" + command.name;
		for (const auto &part : chain) {
			title += " " + part.name;
		}
	}
	drawOverlayHeader(title, mode == Mode::LIST && !filter.empty() ? "\"" + filter + "\"" : "");

	const int count = rowCount();
	if (mode == Mode::LIST && count == 0) {
		bool loading = false;
		bool failed = false;
		{
			std::lock_guard<std::mutex> lock(shared->mutex);
			loading = shared->loading;
			failed = shared->failed;
		}
		const std::string text = loading  ? TR("common.loading")
		                         : failed ? TR("commands.load_failed")
		                                  : TR("commands.none");
		drawCenteredText(110.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(), text, 400.0f);
		return;
	}

	const auto list = mode == Mode::LIST ? filtered() : std::vector<Discord::ApplicationCommand>();
	for (int i = scroll; i < count && i < scroll + VISIBLE_ROWS; i++) {
		const float y = LIST_TOP + (i - scroll) * ROW_H;
		const bool selected = i == cursor;
		if (selected) {
			drawRoundedRect(6.0f, y, 0.3f, 388.0f, ROW_H - 2.0f, 6.0f, ScreenManager::colorBackgroundLight());
			drawRoundedRect(6.0f, y + 4.0f, 0.35f, 3.0f, ROW_H - 10.0f, 1.5f, ScreenManager::colorSelection());
		}
		const u32 main = selected ? ScreenManager::colorText() : ScreenManager::colorTextMuted();
		std::string left;
		std::string right;
		u32 rightColor = ScreenManager::colorTextMuted();
		if (mode == Mode::LIST) {
			left = "/" + list[i].name;
			right = list[i].applicationName;
		} else if (mode == Mode::SUBCOMMAND) {
			left = subOptions[i].name;
			right = subOptions[i].description;
		} else if (mode == Mode::FORM) {
			if (i == (int)form.size()) {
				drawCenteredText(y + 4.0f, 0.5f, 0.48f, 0.48f, ScreenManager::colorAccent(),
				                 "▶ " + TR("commands.run"), 400.0f);
				continue;
			}
			left = form[i].name + (form[i].required ? " *" : "");
			right = labels[i].empty() ? typeLabel(form[i].type) : labels[i];
			rightColor = labels[i].empty() ? ScreenManager::colorTextMuted() : ScreenManager::colorLink();
		} else {
			left = choices[i].label;
			right = choices[i].detail;
		}
		drawText(16.0f, y + 4.0f, 0.5f, 0.46f, 0.46f, main, getTruncatedText(left, 190.0f, 0.46f, 0.46f));
		const std::string shownRight = getTruncatedText(right, 180.0f, 0.4f, 0.4f);
		drawText(388.0f - measureText(shownRight, 0.4f, 0.4f), y + 5.0f, 0.5f, 0.4f, 0.4f, rightColor, shownRight);
	}
}

void CommandPicker::renderBottom() {
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, ScreenManager::colorBackgroundDark());
	drawText(10.0f, 8.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorText(), TR("commands.title"));
	C2D_DrawRectSolid(10, 30, 0.5f, 300, 1, ScreenManager::colorSeparator());

	std::string heading;
	std::string description;
	std::string hints;
	if (mode == Mode::LIST) {
		const auto list = filtered();
		if (cursor < (int)list.size()) {
			heading = "/" + list[cursor].name;
			description = list[cursor].description;
		}
		hints = ": " + TR("common.select") + "  : " + TR("common.search") + "  : " +
		        TR("common.close");
	} else if (mode == Mode::SUBCOMMAND) {
		if (cursor < (int)subOptions.size()) {
			heading = subOptions[cursor].name;
			description = subOptions[cursor].description;
		}
		hints = ": " + TR("common.select") + "  : " + TR("common.back");
	} else if (mode == Mode::FORM) {
		heading = "/" + command.name;
		description = command.description;
		if (cursor < (int)form.size()) {
			heading = form[cursor].name + (form[cursor].required ? " (" + TR("commands.required") + ")" : "");
			description = form[cursor].description;
			if (form[cursor].autocomplete) {
				description += "\n" + TR("commands.autocomplete_note");
			}
		}
		hints = ": " + TR("commands.edit") + "  : " + TR("commands.clear") + "  : " +
		        TR("commands.run") + "  : " + TR("common.back");
	} else {
		if (choiceRow >= 0 && choiceRow < (int)form.size()) {
			heading = form[choiceRow].name;
			description = form[choiceRow].description;
		}
		hints = ": " + TR("common.select") + "  : " + TR("common.back");
	}

	drawText(10.0f, 38.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorText(), getTruncatedText(heading, 300.0f, 0.5f, 0.5f));
	float y = 58.0f;
	for (const auto &line : MessageUtils::wrapText(description, 300.0f, 0.42f)) {
		if (y > 190.0f) {
			break;
		}
		drawText(10.0f, y, 0.5f, 0.42f, 0.42f, ScreenManager::colorTextMuted(), line);
		y += 14.0f;
	}
	drawKeyHints(hints);
}

} // namespace UI
