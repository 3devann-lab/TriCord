#include "ui/friends_screen.h"
#include "core/i18n.h"
#include "discord/avatar_cache.h"
#include "discord/discord_client.h"
#include "ui/overlay.h"
#include "utils/redraw.h"
#include "utils/utf8_utils.h"
#include <algorithm>
#include <cctype>

namespace UI {

namespace {

constexpr float ROW_H = 40.0f;
constexpr float LIST_TOP = 52.0f;

std::string lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}

} // namespace

FriendsScreen::FriendsScreen() : shared(std::make_shared<Shared>()) {}

void FriendsScreen::onEnter() {
	{
		std::lock_guard<std::mutex> lock(shared->mutex);
		shared->list = Discord::DiscordClient::getInstance().getRelationships();
	}
	refresh();
}

bool FriendsScreen::isAnimating() const {
	std::lock_guard<std::mutex> lock(shared->mutex);
	return shared->loading;
}

std::string FriendsScreen::displayName(const Discord::Relationship &rel) {
	if (!rel.nickname.empty()) {
		return rel.nickname;
	}
	if (!rel.user.global_name.empty()) {
		return rel.user.global_name;
	}
	return rel.user.username.empty() ? rel.id : rel.user.username;
}

void FriendsScreen::reload(const std::shared_ptr<Shared> &state) {
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		if (state->loading) {
			return;
		}
		state->loading = true;
	}
	Discord::DiscordClient::getInstance().fetchRelationships(
	    [state](bool success, const std::vector<Discord::Relationship> &list) {
		    std::lock_guard<std::mutex> lock(state->mutex);
		    state->loading = false;
		    state->failed = !success;
		    if (success) {
			    state->list = list;
		    }
		    Utils::requestRedraw();
	    });
}

void FriendsScreen::refresh() { reload(shared); }

std::vector<Discord::Relationship> FriendsScreen::visible() const {
	std::vector<Discord::Relationship> out;
	{
		std::lock_guard<std::mutex> lock(shared->mutex);
		for (const auto &rel : shared->list) {
			const bool match = (tab == Tab::FRIENDS && rel.type == Discord::RelationshipType::FRIEND) ||
			                   (tab == Tab::PENDING && (rel.type == Discord::RelationshipType::INCOMING ||
			                                            rel.type == Discord::RelationshipType::OUTGOING)) ||
			                   (tab == Tab::BLOCKED && rel.type == Discord::RelationshipType::BLOCKED);
			if (match) {
				out.push_back(rel);
			}
		}
	}
	std::sort(out.begin(), out.end(), [](const Discord::Relationship &a, const Discord::Relationship &b) {
		// Requests waiting on you come first.
		if (a.type != b.type) {
			return a.type == Discord::RelationshipType::INCOMING;
		}
		return lower(displayName(a)) < lower(displayName(b));
	});
	return out;
}

void FriendsScreen::addFriendPrompt() {
	KeyboardInput input = runKeyboard(TR("friends.add_hint"), "", false, 64, TR("friends.send_request"));
	if (!input.confirmed || input.text.empty()) {
		return;
	}
	std::shared_ptr<Shared> state = shared;
	const std::string name = input.text;
	Discord::DiscordClient::getInstance().sendFriendRequest(name, [state, name](bool ok, const std::string &message) {
		{
			std::lock_guard<std::mutex> lock(state->mutex);
			state->toast = ok ? Core::I18n::format(TR("friends.request_sent"), name)
			                  : Core::I18n::format(TR("friends.request_failed"), message);
		}
		if (ok) {
			reload(state);
		}
	});
}

void FriendsScreen::act(bool primary) {
	const auto list = visible();
	if (selected < 0 || selected >= (int)list.size()) {
		return;
	}
	const Discord::Relationship rel = list[selected];
	std::shared_ptr<Shared> state = shared;
	auto &client = Discord::DiscordClient::getInstance();

	auto report = [state](bool ok, const std::string &message, const std::string &doneKey) {
		std::lock_guard<std::mutex> lock(state->mutex);
		state->toast = ok ? TR(doneKey) : Core::I18n::format(TR("friends.action_failed"), message);
	};

	if (primary) {
		if (rel.type == Discord::RelationshipType::FRIEND) {
			client.openDirectMessage(rel.id, [state](const std::string &channelId) {
				std::lock_guard<std::mutex> lock(state->mutex);
				if (channelId.empty()) {
					state->toast = TR("friends.dm_failed");
				} else {
					state->openChannelId = channelId;
				}
				Utils::requestRedraw();
			});
		} else if (rel.type == Discord::RelationshipType::INCOMING) {
			client.addFriend(rel.id, [state, report](bool ok, const std::string &message) {
				report(ok, message, "friends.accepted");
				if (ok) {
					reload(state);
				}
			});
		}
		return;
	}

	const std::string doneKey = rel.type == Discord::RelationshipType::FRIEND    ? "friends.removed"
	                            : rel.type == Discord::RelationshipType::BLOCKED ? "friends.unblocked"
	                                                                             : "friends.declined";
	client.removeRelationship(rel.id, [state, report, doneKey, id = rel.id](bool ok, const std::string &message) {
		report(ok, message, doneKey);
		if (ok) {
			std::lock_guard<std::mutex> lock(state->mutex);
			state->list.erase(std::remove_if(state->list.begin(), state->list.end(),
			                                 [&](const Discord::Relationship &r) { return r.id == id; }),
			                  state->list.end());
		}
		Utils::requestRedraw();
	});
}

void FriendsScreen::update() {
	u32 kDown = hidKeysDown();
	u32 kHeld = hidKeysHeld();

	std::string toast;
	std::string openChannel;
	{
		std::lock_guard<std::mutex> lock(shared->mutex);
		toast.swap(shared->toast);
		openChannel.swap(shared->openChannelId);
	}
	if (!toast.empty()) {
		ScreenManager::getInstance().showToast(toast);
	}
	if (!openChannel.empty()) {
		Discord::DiscordClient::getInstance().setSelectedChannelId(openChannel);
		ScreenManager::getInstance().pushScreen(ScreenType::MESSAGES);
		return;
	}

	const auto list = visible();
	selected = std::clamp(selected, 0, std::max(0, (int)list.size() - 1));

	if (confirming) {
		if (kDown & KEY_A) {
			confirming = false;
			act(false);
		} else if (kDown & KEY_B) {
			confirming = false;
		}
		return;
	}

	if (kDown & KEY_B) {
		ScreenManager::getInstance().returnToPreviousScreen();
		return;
	}
	if (kDown & (KEY_L | KEY_R)) {
		const int count = 3;
		int next = ((int)tab + ((kDown & KEY_R) ? 1 : count - 1)) % count;
		tab = (Tab)next;
		selected = 0;
		scroll = 0;
		return;
	}
	if (kDown & KEY_Y) {
		addFriendPrompt();
		return;
	}
	if (kDown & KEY_A) {
		act(true);
		return;
	}
	if ((kDown & KEY_X) && !list.empty()) {
		confirming = true;
		return;
	}
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
	if (step && !list.empty()) {
		selected = std::clamp(selected + ((step & KEY_DOWN) ? 1 : -1), 0, (int)list.size() - 1);
		if (selected < scroll) {
			scroll = selected;
		} else if (selected >= scroll + VISIBLE_ROWS) {
			scroll = selected - VISIBLE_ROWS + 1;
		}
	}
}

void FriendsScreen::renderTop(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackground());
	C2D_SceneBegin(target);

	const float headerH = 26.0f;
	C2D_DrawRectSolid(0, 0, 0.9f, 400.0f, headerH, ScreenManager::colorHeaderGlass());
	C2D_DrawRectSolid(0, headerH - 1.0f, 0.91f, 400.0f, 1.0f, ScreenManager::colorHeaderBorder());
	drawCenteredText(4.0f, 0.95f, 0.52f, 0.52f, ScreenManager::colorText(), TR("friends.title"), 400.0f);

	const char *tabKeys[] = {"friends.tab_friends", "friends.tab_pending", "friends.tab_blocked"};
	float tabX = 10.0f;
	for (int i = 0; i < 3; i++) {
		const std::string label = TR(tabKeys[i]);
		const float w = measureText(label, 0.45f, 0.45f) + 20.0f;
		const bool active = (int)tab == i;
		drawRoundedRect(tabX, 29.0f, 0.5f, w, 19.0f, 9.5f,
		                active ? ScreenManager::colorSelection() : ScreenManager::colorBackgroundLight());
		drawText(tabX + 10.0f, 31.0f, 0.55f, 0.45f, 0.45f,
		         active ? ScreenManager::colorWhite() : ScreenManager::colorTextMuted(), label);
		tabX += w + 6.0f;
	}

	const auto list = visible();
	bool loading = false;
	bool failed = false;
	{
		std::lock_guard<std::mutex> lock(shared->mutex);
		loading = shared->loading;
		failed = shared->failed;
	}
	if (list.empty()) {
		const std::string text = loading  ? TR("common.loading")
		                         : failed ? TR("friends.load_failed")
		                         : tab == Tab::FRIENDS ? TR("friends.none")
		                         : tab == Tab::PENDING ? TR("friends.no_pending")
		                                               : TR("friends.no_blocked");
		drawCenteredText(120.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(), text, 400.0f);
		return;
	}

	for (int i = scroll; i < (int)list.size() && i < scroll + VISIBLE_ROWS; i++) {
		const auto &rel = list[i];
		const float y = LIST_TOP + (i - scroll) * (ROW_H + 4.0f);
		const bool isSelected = i == selected;
		drawRoundedRect(8.0f, y, 0.4f, 384.0f, ROW_H, 8.0f,
		                isSelected ? ScreenManager::colorBackgroundLight() : ScreenManager::colorBackgroundDark());
		if (isSelected) {
			drawRoundedRect(8.0f, y + 6.0f, 0.45f, 4.0f, ROW_H - 12.0f, 2.0f, ScreenManager::colorSelection());
		}

		const float avatar = 28.0f;
		C3D_Tex *tex = Discord::AvatarCache::getInstance().getAvatar(rel.user.id, rel.user.avatar,
		                                                             rel.user.discriminator);
		if (tex) {
			Tex3DS_SubTexture subtex = {(u16)tex->width, (u16)tex->height, 0.0f, 1.0f, 1.0f, 0.0f};
			C2D_Image img = {tex, &subtex};
			C2D_DrawImageAt(img, 18.0f, y + 6.0f, 0.5f, nullptr, avatar / tex->width, avatar / tex->height);
		} else {
			Discord::AvatarCache::getInstance().prefetchAvatar(rel.user.id, rel.user.avatar, rel.user.discriminator);
			drawCircle(18.0f + avatar / 2.0f, y + 6.0f + avatar / 2.0f, 0.5f, avatar / 2.0f,
			           ScreenManager::colorAccent());
			drawText(18.0f + 9.0f, y + 11.0f, 0.55f, 0.5f, 0.5f, ScreenManager::colorWhite(),
			         Utils::Utf8::getFirstChar(displayName(rel)));
		}

		const u32 nameColor = isSelected ? ScreenManager::colorText() : ScreenManager::colorTextMuted();
		drawText(56.0f, y + 5.0f, 0.55f, 0.48f, 0.48f, nameColor,
		         getTruncatedText(displayName(rel), 230.0f, 0.48f, 0.48f));
		drawText(56.0f, y + 21.0f, 0.55f, 0.38f, 0.38f, ScreenManager::colorTextMuted(),
		         getTruncatedText("@" + rel.user.username, 230.0f, 0.38f, 0.38f));

		std::string badge;
		if (rel.type == Discord::RelationshipType::INCOMING) {
			badge = TR("friends.incoming");
		} else if (rel.type == Discord::RelationshipType::OUTGOING) {
			badge = TR("friends.outgoing");
		}
		if (!badge.empty()) {
			const float w = measureText(badge, 0.36f, 0.36f) + 12.0f;
			drawRoundedRect(384.0f - w, y + 12.0f, 0.55f, w, 16.0f, 8.0f,
			                rel.type == Discord::RelationshipType::INCOMING ? ScreenManager::colorSuccess()
			                                                                : ScreenManager::colorBackgroundLight());
			drawText(390.0f - w, y + 13.5f, 0.6f, 0.36f, 0.36f, ScreenManager::colorWhite(), badge);
		}
	}

	if (confirming && selected < (int)list.size()) {
		const auto &rel = list[selected];
		const float w = 300.0f, h = 90.0f, x = 50.0f, y = 75.0f;
		drawOverlay(0.85f);
		drawPopupBackground(x, y, w, h, 0.9f, 12.0f);
		const std::string key = rel.type == Discord::RelationshipType::FRIEND    ? "friends.confirm_remove"
		                        : rel.type == Discord::RelationshipType::BLOCKED ? "friends.confirm_unblock"
		                        : rel.type == Discord::RelationshipType::INCOMING ? "friends.confirm_decline"
		                                                                          : "friends.confirm_cancel";
		drawCenteredText(y + 16.0f, 0.92f, 0.48f, 0.48f, ScreenManager::colorText(),
		                 getTruncatedText(Core::I18n::format(TR(key), displayName(rel)), w - 20.0f, 0.48f, 0.48f),
		                 400.0f);
		drawCenteredText(y + h - 26.0f, 0.92f, 0.42f, 0.42f, ScreenManager::colorError(),
		                 ": " + TR("common.confirm") + "  : " + TR("common.cancel"), 400.0f);
	}
}

void FriendsScreen::renderBottom(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackgroundDark());
	C2D_SceneBegin(target);

	drawText(35.0f, 10.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorText(), TR("friends.title"));
	C2D_DrawRectSolid(10, 32, 0.5f, 300, 1, ScreenManager::colorSeparator());

	const auto list = visible();
	int friendCount = 0;
	int incoming = 0;
	{
		std::lock_guard<std::mutex> lock(shared->mutex);
		for (const auto &rel : shared->list) {
			friendCount += rel.type == Discord::RelationshipType::FRIEND;
			incoming += rel.type == Discord::RelationshipType::INCOMING;
		}
	}
	drawText(10.0f, 40.0f, 0.5f, 0.42f, 0.42f, ScreenManager::colorTextMuted(),
	         Core::I18n::format(TR("friends.counts"), std::to_string(friendCount), std::to_string(incoming)));

	std::string actions;
	if (selected < (int)list.size()) {
		const auto &rel = list[selected];
		drawText(10.0f, 62.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorText(),
		         getTruncatedText(displayName(rel), 300.0f, 0.5f, 0.5f));
		drawText(10.0f, 80.0f, 0.5f, 0.4f, 0.4f, ScreenManager::colorTextMuted(),
		         getTruncatedText("@" + rel.user.username, 300.0f, 0.4f, 0.4f));
		if (rel.type == Discord::RelationshipType::FRIEND) {
			actions = ": " + TR("friends.message") + "  : " + TR("friends.remove");
		} else if (rel.type == Discord::RelationshipType::INCOMING) {
			actions = ": " + TR("friends.accept") + "  : " + TR("friends.decline");
		} else if (rel.type == Discord::RelationshipType::OUTGOING) {
			actions = ": " + TR("friends.cancel_request");
		} else if (rel.type == Discord::RelationshipType::BLOCKED) {
			actions = ": " + TR("friends.unblock");
		}
	}
	drawText(10.0f, 150.0f, 0.5f, 0.42f, 0.42f, ScreenManager::colorText(), actions);
	drawText(10.0f, 168.0f, 0.5f, 0.42f, 0.42f, ScreenManager::colorText(),
	         ": " + TR("friends.add") + "  L/R: " + TR("friends.switch_tab"));
	drawKeyHints("\uE079\uE07A: " + TR("common.navigate") + "  \uE001: " + TR("common.back"));
}

} // namespace UI
