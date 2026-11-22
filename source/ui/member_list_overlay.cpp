#include "ui/member_list_overlay.h"
#include "core/i18n.h"
#include "discord/avatar_cache.h"
#include "discord/discord_client.h"
#include "ui/presence_ui.h"
#include "ui/screen_manager.h"
#include "utils/redraw.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>

namespace UI {

namespace {

const float LIST_TOP = 30.0f;

u32 roleColor(int rgb) { return C2D_Color32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255); }

std::string upper(std::string s) {
	for (auto &c : s) {
		if ((unsigned char)c < 0x80) {
			c = (char)std::toupper((unsigned char)c);
		}
	}
	return s;
}

std::string displayName(const Discord::User &user, const std::string &nick) {
	if (!nick.empty()) {
		return nick;
	}
	return user.global_name.empty() ? user.username : user.global_name;
}

void drawAvatar(const Discord::User &user, float x, float y, float size) {
	C3D_Tex *tex = Discord::AvatarCache::getInstance().getAvatar(user.id, user.avatar, user.discriminator);
	if (tex) {
		Tex3DS_SubTexture sub = {(u16)tex->width, (u16)tex->height, 0.0f, 1.0f, 1.0f, 0.0f};
		C2D_Image img = {tex, &sub};
		C2D_DrawImageAt(img, x, y, 0.5f, nullptr, size / tex->width, size / tex->height);
	} else {
		drawCircle(x + size / 2, y + size / 2, 0.5f, size / 2, ScreenManager::colorBackgroundLight());
	}
}

} // namespace

MemberListOverlay::MemberListOverlay(const std::string &guildId, const std::string &channelId,
                                     const std::string &channelName, OpenDm openDm)
    : guildId(guildId), channelId(channelId), channelName(channelName), openDm(std::move(openDm)),
      alive(std::make_shared<bool>(true)) {
	isDm = guildId.empty() || guildId == "DM";
	if (!isDm && Discord::DiscordClient::getInstance().getMemberList(guildId).items.empty()) {
		Discord::DiscordClient::getInstance().sendLazyRequest(guildId, channelId, requestedUpTo);
	}
	rebuild();
}

MemberListOverlay::~MemberListOverlay() { *alive = false; }

void MemberListOverlay::rebuild() {
	Discord::DiscordClient &client = Discord::DiscordClient::getInstance();
	std::string selectedId = (selected >= 0 && selected < (int)rows.size()) ? rows[selected].user.id : "";

	if (isDm) {
		if (!rows.empty()) {
			return;
		}
		Discord::Channel channel = client.getChannel(channelId);
		std::vector<Discord::User> people = channel.recipients;
		people.push_back(client.getCurrentUser());
		Row header;
		header.header = true;
		header.title = Core::I18n::format(TR("members.dm_header"), std::to_string(people.size()));
		rows.push_back(header);
		for (const auto &user : people) {
			Row row;
			row.user = user;
			row.name = displayName(user, "");
			rows.push_back(row);
		}
		memberCount = (int)people.size();
	} else {
		Discord::MemberList list = client.getMemberList(guildId);
		if (list.version == seenVersion) {
			return;
		}
		seenVersion = list.version;
		onlineCount = list.onlineCount;
		memberCount = list.memberCount;

		std::lock_guard<std::recursive_mutex> lock(client.getMutex());
		const Discord::Guild *guild = client.getGuildPtr(guildId);
		auto findRole = [guild](const std::string &id) -> const Discord::Role * {
			if (!guild) {
				return nullptr;
			}
			for (const auto &role : guild->roles) {
				if (role.id == id) {
					return &role;
				}
			}
			return nullptr;
		};

		rows.clear();
		for (const auto &item : list.items) {
			Row row;
			if (item.isGroup) {
				row.header = true;
				std::string title;
				if (item.groupId == "online") {
					title = TR("members.online");
				} else if (item.groupId == "offline") {
					title = TR("members.offline");
				} else if (const Discord::Role *role = findRole(item.groupId)) {
					title = role->name;
				}
				row.title = upper(title) + " — " + std::to_string(item.count);
			} else if (item.user.id.empty()) {
				row.placeholder = true;
			} else {
				row.user = item.user;
				row.user.status = item.status;
				row.name = displayName(item.user, item.nick);
				std::vector<const Discord::Role *> roles;
				for (const auto &id : item.roles) {
					if (const Discord::Role *role = findRole(id)) {
						roles.push_back(role);
					}
				}
				std::sort(roles.begin(), roles.end(),
				          [](const Discord::Role *a, const Discord::Role *b) { return a->position > b->position; });
				for (const auto *role : roles) {
					if (row.nameColor == 0 && role->color != 0) {
						row.nameColor = roleColor(role->color);
					}
					row.roleNames.push_back(role->name);
					row.roleColors.push_back(role->color ? roleColor(role->color) : ScreenManager::colorTextMuted());
				}
			}
			rows.push_back(row);
		}
	}

	// Keep the same person selected as rows move around them.
	selected = -1;
	for (int i = 0; i < (int)rows.size() && !selectedId.empty(); i++) {
		if (rows[i].user.id == selectedId) {
			selected = i;
			break;
		}
	}
	if (selected < 0) {
		for (int i = 0; i < (int)rows.size(); i++) {
			if (!rows[i].header && !rows[i].placeholder) {
				selected = i;
				break;
			}
		}
	}
	Utils::requestRedraw();
}

void MemberListOverlay::moveSelection(int step) {
	for (int i = selected + step; i >= 0 && i < (int)rows.size(); i += step) {
		if (!rows[i].header && !rows[i].placeholder) {
			selected = i;
			keepSelectionVisible();
			return;
		}
	}
	// Past the last loaded member: scroll anyway so headers at the end show.
	keepSelectionVisible();
}

void MemberListOverlay::keepSelectionVisible() {
	if (selected < 0) {
		return;
	}
	float y = 0.0f;
	for (int i = 0; i < selected; i++) {
		y += rowHeight(rows[i]);
	}
	// Show the group header above the first member of a group.
	float top = (selected > 0 && rows[selected - 1].header) ? y - rowHeight(rows[selected - 1]) : y;
	float view = 240.0f - LIST_TOP;
	if (top < targetScroll) {
		targetScroll = top;
	} else if (y + rowHeight(rows[selected]) > targetScroll + view) {
		targetScroll = y + rowHeight(rows[selected]) - view;
	}
}

bool MemberListOverlay::isAnimating() const { return std::fabs(scroll - targetScroll) > 0.5f || toastFrames > 0; }

void MemberListOverlay::update(u32 kDown, u32 kHeld) {
	if (++refreshTimer >= 20) {
		refreshTimer = 0;
		rebuild();
	}
	if (toastFrames > 0) {
		toastFrames--;
	}
	if (int result = friendResult->exchange(0)) {
		toast = TR(result == 1 ? "friends.request_sent_short" : "members.friend_failed");
		toastFrames = 120;
	}

	if (kDown & KEY_B) {
		finish();
		return;
	}

	int step = 0;
	if (kDown & KEY_DOWN) {
		step = 1;
		repeatTimer = 18;
	} else if (kDown & KEY_UP) {
		step = -1;
		repeatTimer = 18;
	} else if ((kHeld & (KEY_DOWN | KEY_UP | KEY_CSTICK_DOWN | KEY_CSTICK_UP)) && --repeatTimer <= 0) {
		step = (kHeld & (KEY_DOWN | KEY_CSTICK_DOWN)) ? 1 : -1;
		repeatTimer = 4;
	} else if (kDown & KEY_RIGHT) {
		step = 8;
	} else if (kDown & KEY_LEFT) {
		step = -8;
	}
	if (step != 0) {
		int dir = step > 0 ? 1 : -1;
		for (int n = 0; n < std::abs(step); n++) {
			moveSelection(dir);
		}
	}

	// Discord sends the list in windows of 100 rows; ask for the next as the selection nears the end.
	if (!isDm && selected >= requestedUpTo - 25 && requestedUpTo < memberCount + 100) {
		requestedUpTo += 100;
		Discord::DiscordClient::getInstance().sendLazyRequest(guildId, channelId, requestedUpTo);
	}

	if (selected >= 0 && selected < (int)rows.size() && !rows[selected].header && !rows[selected].placeholder) {
		const Discord::User &user = rows[selected].user;
		bool self = user.id == Discord::DiscordClient::getInstance().getCurrentUser().id;
		if ((kDown & KEY_A) && !self && !user.bot && openDm) {
			OpenDm go = openDm;
			std::string id = user.id;
			finish();
			go(id);
			return;
		}
		int relation = Discord::DiscordClient::getInstance().getRelationshipType(user.id);
		if ((kDown & KEY_Y) && !self && !user.bot && relation != Discord::RelationshipType::FRIEND &&
		    relation != Discord::RelationshipType::OUTGOING) {
			std::shared_ptr<std::atomic<int>> result = friendResult;
			Discord::DiscordClient::getInstance().addFriend(user.id, [result](bool ok, const std::string &) {
				result->store(ok ? 1 : 2);
				Utils::requestRedraw();
			});
		}
	}

	scroll += (targetScroll - scroll) * 0.4f;
	if (std::fabs(targetScroll - scroll) < 0.5f) {
		scroll = targetScroll;
	}
}

void MemberListOverlay::renderTop() {
	C2D_DrawRectSolid(0, 0, 0.0f, 400.0f, 240.0f, ScreenManager::colorBackground());
	Discord::DiscordClient &client = Discord::DiscordClient::getInstance();

	float y = LIST_TOP - scroll;
	for (int i = 0; i < (int)rows.size(); i++) {
		const Row &row = rows[i];
		float h = rowHeight(row);
		if (y + h < 26.0f || y > 240.0f) {
			y += h;
			continue;
		}
		if (row.header) {
			drawText(12.0f, y + 6.0f, 0.4f, 0.38f, 0.38f, ScreenManager::colorTextMuted(),
			         getTruncatedText(row.title, 376.0f, 0.38f, 0.38f));
		} else if (row.placeholder) {
			drawCircle(26.0f, y + 16.0f, 0.4f, 11.0f, ScreenManager::colorBackgroundLight());
			drawRoundedRect(44.0f, y + 11.0f, 0.4f, 120.0f, 10.0f, 5.0f, ScreenManager::colorBackgroundLight());
		} else {
			if (i == selected) {
				drawRoundedRect(6.0f, y + 1.0f, 0.3f, 388.0f, h - 2.0f, 6.0f, ScreenManager::colorBackgroundLight());
			}
			drawAvatar(row.user, 15.0f, y + 5.0f, 22.0f);
			Discord::Presence presence = client.getPresence(row.user.id);
			if (presence.status == Discord::UserStatus::OFFLINE && row.user.status != Discord::UserStatus::OFFLINE) {
				presence.status = row.user.status;
			}
			drawStatusBadge(33.0f, y + 23.0f, 9.0f, presence,
			                i == selected ? ScreenManager::colorBackgroundLight() : ScreenManager::colorBackground(), 0.55f);
			bool offline = presence.status == Discord::UserStatus::OFFLINE;
			u32 color = row.nameColor ? row.nameColor : ScreenManager::colorText();
			if (offline) {
				color = (color & 0x00FFFFFF) | 0x90000000;
			}
			std::string line = presenceLine(presence);
			float nameY = line.empty() ? y + 9.0f : y + 3.0f;
			drawText(44.0f, nameY, 0.4f, 0.47f, 0.47f, color, getTruncatedText(row.name, 300.0f, 0.47f, 0.47f));
			if (row.user.bot) {
				float nx = 44.0f + std::min(300.0f, measureText(row.name, 0.47f, 0.47f)) + 6.0f;
				drawRoundedRect(nx, nameY + 2.0f, 0.4f, 26.0f, 11.0f, 3.0f, ScreenManager::colorAccent());
				drawText(nx + 4.0f, nameY + 2.0f, 0.41f, 0.32f, 0.32f, ScreenManager::colorWhite(), TR("members.bot"));
			}
			if (!line.empty()) {
				drawText(44.0f, y + 17.0f, 0.4f, 0.36f, 0.36f, ScreenManager::colorTextMuted(),
				         getTruncatedText(line, 340.0f, 0.36f, 0.36f));
			}
		}
		y += h;
	}

	if (rows.empty()) {
		drawCenteredText(110.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(), TR("common.loading"), 400.0f);
	}

	std::string right;
	if (!isDm && memberCount > 0) {
		right = Core::I18n::format(TR("members.online_count"), std::to_string(onlineCount));
	}
	std::string title = isDm ? TR("members.title_dm")
	                         : Core::I18n::format(TR("members.title"), channelName.empty() ? "" : "#" + channelName);
	drawOverlayHeader(title, right);
}

void MemberListOverlay::renderBottom() {
	C2D_DrawRectSolid(0, 0, 0.0f, 320.0f, 240.0f, ScreenManager::colorBackgroundDark());
	std::string hints = ": " + TR("common.close");

	if (selected >= 0 && selected < (int)rows.size() && !rows[selected].header && !rows[selected].placeholder) {
		const Row &row = rows[selected];
		Discord::DiscordClient &client = Discord::DiscordClient::getInstance();
		Discord::Presence presence = client.getPresence(row.user.id);
		if (presence.status == Discord::UserStatus::OFFLINE && row.user.status != Discord::UserStatus::OFFLINE) {
			presence.status = row.user.status;
		}

		drawAvatar(row.user, 12.0f, 12.0f, 48.0f);
		drawStatusBadge(52.0f, 52.0f, 14.0f, presence, ScreenManager::colorBackgroundDark(), 0.55f);
		drawText(70.0f, 14.0f, 0.5f, 0.55f, 0.55f, row.nameColor ? row.nameColor : ScreenManager::colorText(),
		         getTruncatedText(row.name, 240.0f, 0.55f, 0.55f));
		drawText(70.0f, 32.0f, 0.5f, 0.4f, 0.4f, ScreenManager::colorTextMuted(),
		         getTruncatedText("@" + row.user.username, 240.0f, 0.4f, 0.4f));
		drawText(70.0f, 46.0f, 0.5f, 0.4f, 0.4f, ScreenManager::colorTextMuted(), statusLabel(presence));

		float y = 70.0f;
		std::string line = presenceLine(presence);
		if (!line.empty()) {
			drawText(12.0f, y, 0.5f, 0.42f, 0.42f, ScreenManager::colorText(), getTruncatedText(line, 296.0f, 0.42f, 0.42f));
			y += 18.0f;
		}
		if (!row.roleNames.empty()) {
			drawText(12.0f, y, 0.5f, 0.38f, 0.38f, ScreenManager::colorTextMuted(), TR("members.roles"));
			y += 15.0f;
			float x = 12.0f;
			for (size_t i = 0; i < row.roleNames.size() && y < 200.0f; i++) {
				float w = measureText(row.roleNames[i], 0.36f, 0.36f) + 18.0f;
				if (x + w > 308.0f) {
					x = 12.0f;
					y += 17.0f;
					if (y >= 200.0f) {
						break;
					}
				}
				drawRoundedRect(x, y, 0.5f, w, 15.0f, 7.0f, ScreenManager::colorBackgroundLight());
				drawCircle(x + 7.0f, y + 7.5f, 0.51f, 3.0f, row.roleColors[i]);
				drawText(x + 13.0f, y + 2.0f, 0.51f, 0.36f, 0.36f, ScreenManager::colorText(), row.roleNames[i]);
				x += w + 4.0f;
			}
		}

		bool self = row.user.id == client.getCurrentUser().id;
		if (!self && !row.user.bot) {
			hints = ": " + TR("members.message") + "  " + hints;
			int relation = client.getRelationshipType(row.user.id);
			if (relation != Discord::RelationshipType::FRIEND && relation != Discord::RelationshipType::OUTGOING) {
				hints += "  : " + TR("members.add_friend");
			}
		}
	}

	if (toastFrames > 0) {
		float w = measureText(toast, 0.42f, 0.42f) + 20.0f;
		drawRoundedRect((320.0f - w) / 2.0f, 190.0f, 0.7f, w, 20.0f, 10.0f, ScreenManager::colorBackgroundLight());
		drawCenteredText(193.0f, 0.71f, 0.42f, 0.42f, ScreenManager::colorText(), toast, 320.0f);
	}
	drawKeyHints(hints);
}

} // namespace UI
