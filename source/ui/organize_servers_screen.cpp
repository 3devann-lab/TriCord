#include "ui/organize_servers_screen.h"
#include "core/i18n.h"
#include "discord/avatar_cache.h"
#include "discord/discord_client.h"
#include "ui/overlay.h"
#include "utils/message_utils.h"
#include "utils/redraw.h"
#include <algorithm>
#include <unordered_set>

namespace UI {

namespace {

const float ROW_H = 26.0f;
const float LIST_TOP = 32.0f;
const int COLORS[] = {0, 0x5865F2, 0x57F287, 0xFEE75C, 0xEB459E, 0xED4245, 0xF0B232, 0x1ABC9C, 0x9B59B6};
const int COLOR_COUNT = sizeof(COLORS) / sizeof(COLORS[0]);

u32 toColor(int rgb, u32 fallback) {
	return rgb ? C2D_Color32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255) : fallback;
}

} // namespace

void OrganizeServersScreen::onEnter() { load(); }

std::string OrganizeServersScreen::guildName(const std::string &id) const {
	const Discord::Guild *g = Discord::DiscordClient::getInstance().getGuildPtr(id);
	return g ? g->name : id;
}

void OrganizeServersScreen::load() {
	Discord::DiscordClient &client = Discord::DiscordClient::getInstance();
	std::lock_guard<std::recursive_mutex> lock(client.getMutex());
	tokens.clear();
	std::unordered_set<std::string> seen;
	auto exists = [&](const std::string &id) { return client.getGuildPtr(id) != nullptr; };

	for (const auto &f : client.getGuildFolders()) {
		if (f.id.empty()) {
			for (const auto &gid : f.guildIds) {
				if (exists(gid) && seen.insert(gid).second) {
					tokens.push_back({Kind::GUILD, gid});
				}
			}
			continue;
		}
		Token start{Kind::FOLDER_START};
		start.folderId = f.id;
		start.name = f.name;
		start.color = f.color;
		tokens.push_back(start);
		for (const auto &gid : f.guildIds) {
			if (exists(gid) && seen.insert(gid).second) {
				tokens.push_back({Kind::GUILD, gid});
			}
		}
		tokens.push_back({Kind::FOLDER_END});
	}
	// Servers missing from the saved order (joined since) go on top, as Discord puts them.
	std::vector<Token> fresh;
	for (const auto &g : client.getGuilds()) {
		if (seen.insert(g.id).second) {
			fresh.push_back({Kind::GUILD, g.id});
		}
	}
	tokens.insert(tokens.begin(), fresh.begin(), fresh.end());
	selected = 0;
	dirty = false;
}

std::vector<Discord::GuildFolder> OrganizeServersScreen::toFolders() const {
	std::vector<Discord::GuildFolder> out;
	Discord::GuildFolder *open = nullptr;
	for (const auto &t : tokens) {
		if (t.kind == Kind::FOLDER_START) {
			Discord::GuildFolder folder;
			folder.id = t.folderId;
			folder.name = t.name;
			folder.color = t.color;
			out.push_back(folder);
			open = &out.back();
		} else if (t.kind == Kind::FOLDER_END) {
			// Discord has no empty folders.
			if (open && open->guildIds.empty()) {
				out.pop_back();
			}
			open = nullptr;
		} else if (open) {
			open->guildIds.push_back(t.guildId);
		} else {
			Discord::GuildFolder single;
			single.guildIds.push_back(t.guildId);
			out.push_back(single);
		}
	}
	return out;
}

bool OrganizeServersScreen::insideFolder(int index) const {
	int depth = 0;
	for (int i = 0; i < index && i < (int)tokens.size(); i++) {
		if (tokens[i].kind == Kind::FOLDER_START) {
			depth++;
		} else if (tokens[i].kind == Kind::FOLDER_END) {
			depth--;
		}
	}
	return depth > 0;
}

int OrganizeServersScreen::folderEndOf(int start) const {
	for (int i = start + 1; i < (int)tokens.size(); i++) {
		if (tokens[i].kind == Kind::FOLDER_END) {
			return i;
		}
	}
	return (int)tokens.size() - 1;
}

int OrganizeServersScreen::folderStartOf(int index) const {
	for (int i = index; i >= 0; i--) {
		if (tokens[i].kind == Kind::FOLDER_START) {
			return i;
		}
	}
	return -1;
}

// Without holding anything: the next row that can be selected (folder ends are not rows).
void OrganizeServersScreen::step(int dir) {
	for (int i = selected + dir; i >= 0 && i < (int)tokens.size(); i += dir) {
		if (tokens[i].kind != Kind::FOLDER_END) {
			selected = i;
			return;
		}
	}
}

void OrganizeServersScreen::moveSelected(int dir) {
	if (selected < 0 || selected >= (int)tokens.size()) {
		return;
	}
	if (tokens[selected].kind == Kind::GUILD) {
		// One step past a folder's start or end moves the server into or out of it.
		int j = selected + dir;
		if (j < 0 || j >= (int)tokens.size()) {
			return;
		}
		std::swap(tokens[selected], tokens[j]);
		selected = j;
		dirty = true;
		return;
	}
	// A folder moves as a block past the neighbouring server or folder.
	int start = selected;
	int end = folderEndOf(start);
	if (dir < 0) {
		if (start == 0) {
			return;
		}
		int prevStart = tokens[start - 1].kind == Kind::FOLDER_END ? folderStartOf(start - 2) : start - 1;
		std::rotate(tokens.begin() + prevStart, tokens.begin() + start, tokens.begin() + end + 1);
		selected = prevStart;
	} else {
		if (end + 1 >= (int)tokens.size()) {
			return;
		}
		int nextEnd = tokens[end + 1].kind == Kind::FOLDER_START ? folderEndOf(end + 1) : end + 1;
		std::rotate(tokens.begin() + start, tokens.begin() + end + 1, tokens.begin() + nextEnd + 1);
		selected = start + (nextEnd - end);
	}
	dirty = true;
}

void OrganizeServersScreen::newFolder() {
	if (selected < 0 || tokens[selected].kind != Kind::GUILD) {
		return;
	}
	// A server already in a folder comes out of it first, just below.
	if (insideFolder(selected)) {
		Token guild = tokens[selected];
		int end = folderEndOf(folderStartOf(selected));
		tokens.erase(tokens.begin() + selected);
		end--;
		tokens.insert(tokens.begin() + end + 1, guild);
		selected = end + 1;
	}
	KeyboardInput input = runKeyboard(TR("organize.folder_name_hint"), "", false, 100, TR("common.ok"));
	if (!input.confirmed) {
		return;
	}
	Token start{Kind::FOLDER_START};
	start.folderId = std::to_string((int64_t)((osGetTime() * 2654435761ULL) % 2147483647ULL) + 1);
	start.name = input.text;
	tokens.insert(tokens.begin() + selected + 1, Token{Kind::FOLDER_END});
	tokens.insert(tokens.begin() + selected, start);
	dirty = true;
}

void OrganizeServersScreen::renameFolder(int start) {
	KeyboardInput input = runKeyboard(TR("organize.folder_name_hint"), tokens[start].name, false, 100, TR("common.ok"));
	if (input.confirmed) {
		tokens[start].name = input.text;
		dirty = true;
	}
}

void OrganizeServersScreen::removeFolder(int start) {
	int end = folderEndOf(start);
	tokens.erase(tokens.begin() + end);
	tokens.erase(tokens.begin() + start);
	selected = std::min(start, (int)tokens.size() - 1);
	if (selected >= 0 && tokens[selected].kind == Kind::FOLDER_END) {
		step(-1);
	}
	dirty = true;
}

void OrganizeServersScreen::save() {
	status = TR("organize.saving");
	saveState->store(3);
	std::shared_ptr<std::atomic<int>> state = saveState;
	Discord::DiscordClient::getInstance().saveGuildFolders(toFolders(), [state](bool ok, const std::string &) {
		state->store(ok ? 1 : 2);
		Utils::requestRedraw();
	});
}

void OrganizeServersScreen::update() {
	u32 kDown = hidKeysDown();
	u32 kHeld = hidKeysHeld();
	auto &sm = ScreenManager::getInstance();

	int state = saveState->load();
	if (state == 1) {
		saveState->store(0);
		sm.showToast(TR("organize.saved"));
		sm.returnToPreviousScreen();
		return;
	}
	if (state == 2) {
		saveState->store(0);
		status = TR("organize.save_failed");
	}
	if (state == 3) {
		return;
	}

	if (kDown & KEY_B) {
		if (holding) {
			holding = false;
		} else if (dirty && !confirmLeave) {
			confirmLeave = true;
			status = TR("organize.confirm_discard");
		} else {
			sm.returnToPreviousScreen();
		}
		return;
	}
	if (kDown) {
		confirmLeave = confirmLeave && (kDown & KEY_B);
	}

	int dir = 0;
	if (kDown & (KEY_DOWN | KEY_CSTICK_DOWN)) {
		dir = 1;
		repeatTimer = 18;
	} else if (kDown & (KEY_UP | KEY_CSTICK_UP)) {
		dir = -1;
		repeatTimer = 18;
	} else if ((kHeld & (KEY_DOWN | KEY_UP)) && --repeatTimer <= 0) {
		dir = (kHeld & KEY_DOWN) ? 1 : -1;
		repeatTimer = 5;
	}
	if (dir != 0) {
		if (holding) {
			moveSelected(dir);
		} else {
			step(dir);
		}
	}

	if (tokens.empty()) {
		return;
	}
	const bool onFolder = tokens[selected].kind == Kind::FOLDER_START;
	if (kDown & KEY_A) {
		holding = !holding;
	}
	if (kDown & KEY_START) {
		holding = false;
		save();
		return;
	}
	if (holding) {
		return;
	}
	if (kDown & KEY_Y) {
		if (onFolder) {
			renameFolder(selected);
		} else {
			newFolder();
		}
	}
	if (kDown & KEY_X) {
		if (onFolder) {
			removeFolder(selected);
		} else if (insideFolder(selected)) {
			// Out of the folder, just below it.
			Token guild = tokens[selected];
			int end = folderEndOf(folderStartOf(selected));
			tokens.erase(tokens.begin() + selected);
			end--;
			tokens.insert(tokens.begin() + end + 1, guild);
			selected = end + 1;
			dirty = true;
		}
	}
	if (onFolder && (kDown & (KEY_L | KEY_R))) {
		int idx = 0;
		for (int i = 0; i < COLOR_COUNT; i++) {
			if (COLORS[i] == tokens[selected].color) {
				idx = i;
			}
		}
		idx = (idx + ((kDown & KEY_R) ? 1 : COLOR_COUNT - 1)) % COLOR_COUNT;
		tokens[selected].color = COLORS[idx];
		dirty = true;
	}
}

void OrganizeServersScreen::renderTop(C3D_RenderTarget *target) {
	(void)target;
	C2D_DrawRectSolid(0, 0, 0.1f, 400, 240, ScreenManager::colorBackground());

	// Rows: every token but folder ends.
	float y = 0.0f;
	float selectedY = 0.0f;
	for (int i = 0; i < (int)tokens.size(); i++) {
		if (tokens[i].kind == Kind::FOLDER_END) {
			continue;
		}
		if (i == selected) {
			selectedY = y;
		}
		y += ROW_H;
	}
	float view = 240.0f - LIST_TOP;
	if (selectedY - scroll < 0) {
		scroll = selectedY;
	} else if (selectedY + ROW_H - scroll > view) {
		scroll = selectedY + ROW_H - view;
	}

	y = LIST_TOP - scroll;
	int depth = 0;
	u32 folderColor = 0;
	for (int i = 0; i < (int)tokens.size(); i++) {
		const Token &t = tokens[i];
		if (t.kind == Kind::FOLDER_END) {
			depth = 0;
			continue;
		}
		bool visible = y + ROW_H > 26.0f && y < 240.0f;
		bool isSelected = i == selected;
		float x = 12.0f + (depth ? 22.0f : 0.0f);
		if (visible) {
			if (isSelected) {
				u32 bg = holding ? ScreenManager::colorAccent() : ScreenManager::colorBackgroundLight();
				drawRoundedRect(x - 6.0f, y + 1.0f, 0.3f, 394.0f - x, ROW_H - 2.0f, 6.0f, bg);
			}
			if (depth) {
				C2D_DrawRectSolid(18.0f, y, 0.35f, 3.0f, ROW_H, folderColor);
			}
		}
		if (t.kind == Kind::FOLDER_START) {
			folderColor = toColor(t.color, ScreenManager::colorAccent());
			if (visible) {
				drawRoundedRect(x, y + 5.0f, 0.4f, 16.0f, 16.0f, 4.0f, folderColor);
				int count = folderEndOf(i) - i - 1;
				std::string name = t.name.empty() ? TR("organize.unnamed_folder") : t.name;
				std::string label = name + "  (" + std::to_string(count) + ")";
				if (count == 0) {
					label += "  " + TR("organize.empty_folder");
				}
				u32 color = isSelected && holding ? ScreenManager::colorWhite() : ScreenManager::colorText();
				drawText(x + 24.0f, y + 6.0f, 0.4f, 0.48f, 0.48f, color, getTruncatedText(label, 330.0f, 0.48f, 0.48f));
			}
			depth = 1;
		} else if (visible) {
			const Discord::Guild *g = Discord::DiscordClient::getInstance().getGuildPtr(t.guildId);
			C3D_Tex *icon = (g && !g->icon.empty()) ? Discord::AvatarCache::getInstance().getGuildIcon(g->id, g->icon)
			                                       : nullptr;
			if (icon) {
				Tex3DS_SubTexture sub = {(u16)icon->width, (u16)icon->height, 0.0f, 1.0f, 1.0f, 0.0f};
				C2D_Image img = {icon, &sub};
				C2D_DrawImageAt(img, x, y + 4.0f, 0.4f, nullptr, 18.0f / icon->width, 18.0f / icon->height);
			} else {
				drawCircle(x + 9.0f, y + 13.0f, 0.4f, 9.0f, ScreenManager::colorBackgroundDark());
			}
			u32 color = isSelected && holding ? ScreenManager::colorWhite() : ScreenManager::colorText();
			drawText(x + 26.0f, y + 6.0f, 0.4f, 0.45f, 0.45f, color,
			         getTruncatedText(g ? g->name : t.guildId, 360.0f - x, 0.45f, 0.45f));
		}
		y += ROW_H;
	}

	drawOverlayHeader(TR("organize.title"), dirty ? TR("organize.unsaved") : "");
}

void OrganizeServersScreen::renderBottom(C3D_RenderTarget *target) {
	(void)target;
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, ScreenManager::colorBackgroundDark());
	std::vector<std::string> lines;
	if (holding) {
		lines = {TR("organize.help.moving1"), TR("organize.help.moving2")};
	} else if (!tokens.empty() && tokens[selected].kind == Kind::FOLDER_START) {
		lines = {": " + TR("organize.pick_up"), ": " + TR("organize.rename"),
		         "L/R: " + TR("organize.color"), ": " + TR("organize.remove_folder")};
	} else {
		lines = {": " + TR("organize.pick_up"), ": " + TR("organize.new_folder")};
		if (!tokens.empty() && insideFolder(selected)) {
			lines.push_back(": " + TR("organize.take_out"));
		}
	}
	lines.push_back("START: " + TR("organize.save"));
	float y = 20.0f;
	for (const auto &line : lines) {
		drawText(16.0f, y, 0.5f, 0.45f, 0.45f, ScreenManager::colorText(), line);
		y += 20.0f;
	}
	if (!status.empty()) {
		for (const auto &line : MessageUtils::wrapText(status, 290.0f, 0.42f)) {
			drawText(16.0f, y + 8.0f, 0.5f, 0.42f, 0.42f, ScreenManager::colorTextMuted(), line);
			y += 15.0f;
		}
	}
	drawKeyHints(": " + TR(holding ? "organize.drop" : "common.back") + "   " + TR("organize.saved_to_account"));
}

} // namespace UI
