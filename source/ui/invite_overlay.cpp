#include "ui/invite_overlay.h"
#include "core/i18n.h"
#include "discord/avatar_cache.h"
#include "discord/discord_client.h"
#include "qrcodegen.h"
#include "ui/screen_manager.h"
#include "utils/message_utils.h"
#include "utils/redraw.h"
#include <cmath>
#include <cstring>

namespace UI {

namespace {

const int AGE_SECONDS[] = {1800, 3600, 21600, 43200, 86400, 604800, 0};
const char *AGE_KEYS[] = {"invite.age.30m", "invite.age.1h",  "invite.age.6h", "invite.age.12h",
                          "invite.age.1d",  "invite.age.7d", "invite.age.never"};
const int USES[] = {0, 1, 5, 10, 25, 50, 100};
const int AGE_COUNT = sizeof(AGE_SECONDS) / sizeof(AGE_SECONDS[0]);
const int USES_COUNT = sizeof(USES) / sizeof(USES[0]);

bool isCodeChar(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'; }

std::string usesLabel(int uses) {
	if (uses == 0) {
		return TR("invite.uses.unlimited");
	}
	return Core::I18n::format(TR(uses == 1 ? "invite.uses.one" : "invite.uses.many"), std::to_string(uses));
}

std::string groupedNumber(int n) {
	std::string digits = std::to_string(n);
	std::string out;
	int count = 0;
	for (int i = (int)digits.size() - 1; i >= 0; i--) {
		out.insert(out.begin(), digits[i]);
		if (++count % 3 == 0 && i > 0) {
			out.insert(out.begin(), ',');
		}
	}
	return out;
}

} // namespace

std::string findInviteCode(const std::string &text) {
	static const char *prefixes[] = {"discord.gg/", "discord.com/invite/", "discordapp.com/invite/"};
	for (const char *prefix : prefixes) {
		size_t pos = text.find(prefix);
		while (pos != std::string::npos) {
			size_t start = pos + strlen(prefix);
			size_t end = start;
			while (end < text.size() && isCodeChar(text[end])) {
				end++;
			}
			if (end - start >= 2) {
				return text.substr(start, end - start);
			}
			pos = text.find(prefix, pos + 1);
		}
	}
	return "";
}

std::unique_ptr<InviteOverlay> InviteOverlay::preview(const std::string &code, OpenChannel open) {
	std::unique_ptr<InviteOverlay> overlay(new InviteOverlay());
	overlay->open = std::move(open);
	overlay->info.code = code;
	overlay->state = State::LOADING;
	InviteOverlay *self = overlay.get();
	std::shared_ptr<bool> token = overlay->alive;
	Discord::DiscordClient::getInstance().fetchInvite(code, [self, token](bool ok, const Discord::InviteInfo &info) {
		std::lock_guard<std::recursive_mutex> lock(self->mutex);
		if (!*token) {
			return;
		}
		self->info = info;
		self->state = ok ? State::READY : State::FAILED;
		self->error = ok ? "" : TR("invite.invalid");
		Utils::requestRedraw();
	});
	return overlay;
}

std::unique_ptr<InviteOverlay> InviteOverlay::create(const std::string &channelId, const std::string &channelName) {
	std::unique_ptr<InviteOverlay> overlay(new InviteOverlay());
	overlay->creating = true;
	overlay->channelId = channelId;
	overlay->channelName = channelName;
	overlay->state = State::CHOOSING;
	return overlay;
}

InviteOverlay::~InviteOverlay() {
	std::lock_guard<std::recursive_mutex> lock(mutex);
	*alive = false;
}

bool InviteOverlay::alreadyMember() const {
	return !info.guildId.empty() && Discord::DiscordClient::getInstance().getGuildPtr(info.guildId) != nullptr;
}

bool InviteOverlay::isAnimating() const {
	std::lock_guard<std::recursive_mutex> lock(mutex);
	return state == State::LOADING || state == State::JOINING || state == State::CREATING ||
	       (state == State::JOINED && !alreadyMember());
}

void InviteOverlay::makeQr(const std::string &text) {
	uint8_t code[qrcodegen_BUFFER_LEN_MAX];
	uint8_t temp[qrcodegen_BUFFER_LEN_MAX];
	qr.clear();
	qrSize = 0;
	if (!qrcodegen_encodeText(text.c_str(), temp, code, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
	                          qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) {
		return;
	}
	qrSize = qrcodegen_getSize(code);
	qr.resize(qrSize * qrSize);
	for (int y = 0; y < qrSize; y++) {
		for (int x = 0; x < qrSize; x++) {
			qr[y * qrSize + x] = qrcodegen_getModule(code, x, y) ? 1 : 0;
		}
	}
}

void InviteOverlay::drawQr(float x, float y, float size) const {
	if (qrSize == 0) {
		return;
	}
	const float quiet = 8.0f;
	C2D_DrawRectSolid(x - quiet, y - quiet, 0.5f, size + quiet * 2, size + quiet * 2, C2D_Color32(255, 255, 255, 255));
	float module = size / qrSize;
	for (int row = 0; row < qrSize; row++) {
		for (int col = 0; col < qrSize; col++) {
			if (qr[row * qrSize + col]) {
				C2D_DrawRectSolid(x + col * module, y + row * module, 0.51f, module, module, C2D_Color32(0, 0, 0, 255));
			}
		}
	}
}

void InviteOverlay::update(u32 kDown, u32 kHeld) {
	std::unique_lock<std::recursive_mutex> lock(mutex);
	(void)kHeld;
	spinner += 0.15f;

	if (kDown & KEY_B) {
		finish();
		return;
	}

	if (creating) {
		if (state == State::CHOOSING) {
			if (kDown & KEY_LEFT) {
				ageIndex = (ageIndex + AGE_COUNT - 1) % AGE_COUNT;
			} else if (kDown & KEY_RIGHT) {
				ageIndex = (ageIndex + 1) % AGE_COUNT;
			} else if (kDown & KEY_UP) {
				usesIndex = (usesIndex + USES_COUNT - 1) % USES_COUNT;
			} else if (kDown & KEY_DOWN) {
				usesIndex = (usesIndex + 1) % USES_COUNT;
			} else if (kDown & KEY_A) {
				state = State::CREATING;
				std::shared_ptr<bool> token = alive;
				Discord::DiscordClient::getInstance().createInvite(
				    channelId, AGE_SECONDS[ageIndex], USES[usesIndex], [this, token](bool ok, const std::string &code) {
					    std::lock_guard<std::recursive_mutex> lock(mutex);
					    if (!*token) {
						    return;
					    }
					    if (ok) {
						    createdCode = code;
						    makeQr("https://discord.gg/" + code);
						    state = State::CREATED;
					    } else {
						    error = TR("invite.create_failed");
						    state = State::CHOOSING;
					    }
					    Utils::requestRedraw();
				    });
			}
		} else if (state == State::CREATED && (kDown & KEY_A)) {
			state = State::CHOOSING;
			error.clear();
		}
		return;
	}

	if (state == State::READY && (kDown & KEY_A)) {
		if (alreadyMember()) {
			OpenChannel go = open;
			std::string channel = info.channelId;
			finish();
			lock.unlock();
			if (go) {
				go(channel);
			}
			return;
		}
		state = State::JOINING;
		std::shared_ptr<bool> token = alive;
		Discord::DiscordClient::getInstance().joinInvite(info.code, [this, token](bool ok, const std::string &err) {
			std::lock_guard<std::recursive_mutex> lock(mutex);
			if (!*token) {
				return;
			}
			if (ok) {
				state = State::JOINED;
			} else {
				state = State::READY;
				error = err == "captcha" ? TR("invite.captcha") : TR("invite.join_failed");
			}
			Utils::requestRedraw();
		});
	} else if (state == State::JOINED && (kDown & KEY_A) && alreadyMember()) {
		OpenChannel go = open;
		std::string channel = info.channelId;
		finish();
		lock.unlock();
		if (go) {
			go(channel);
		}
	}
}

void InviteOverlay::renderTop() {
	std::lock_guard<std::recursive_mutex> lock(mutex);
	C2D_DrawRectSolid(0, 0, 0.0f, 400.0f, 240.0f, ScreenManager::colorBackground());

	if (creating) {
		drawOverlayHeader(Core::I18n::format(TR("invite.create_title"), channelName));
		if (state == State::CREATED) {
			std::string link = "discord.gg/" + createdCode;
			drawCenteredText(70.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), TR("invite.share"), 400.0f);
			drawCenteredText(95.0f, 0.5f, 0.85f, 0.85f, ScreenManager::colorText(), link, 400.0f);
			std::string terms = TR(AGE_KEYS[ageIndex]) + "  •  " + usesLabel(USES[usesIndex]);
			drawCenteredText(135.0f, 0.5f, 0.42f, 0.42f, ScreenManager::colorTextMuted(), terms, 400.0f);
			drawCenteredText(170.0f, 0.5f, 0.4f, 0.4f, ScreenManager::colorTextMuted(), TR("invite.scan_hint"), 400.0f);
		} else {
			float y = 60.0f;
			drawText(40.0f, y, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), TR("invite.expires"));
			drawText(220.0f, y, 0.5f, 0.5f, 0.5f, ScreenManager::colorText(), "◀ " + TR(AGE_KEYS[ageIndex]) + " ▶");
			y += 34.0f;
			drawText(40.0f, y, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), TR("invite.max_uses"));
			drawText(220.0f, y, 0.5f, 0.5f, 0.5f, ScreenManager::colorText(), "▲ " + usesLabel(USES[usesIndex]) + " ▼");
			if (state == State::CREATING) {
				drawCenteredText(160.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), TR("common.loading"), 400.0f);
			} else if (!error.empty()) {
				drawCenteredText(160.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorError(), error, 400.0f);
			}
		}
		return;
	}

	drawOverlayHeader(TR("invite.title"));
	if (state == State::LOADING) {
		drawCenteredText(110.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(), TR("common.loading"), 400.0f);
		return;
	}
	if (state == State::FAILED) {
		drawCenteredText(110.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorError(), error, 400.0f);
		return;
	}

	float iconSize = 64.0f;
	float iconX = (400.0f - iconSize) / 2.0f, iconY = 40.0f;
	C3D_Tex *icon = info.guildIcon.empty() ? nullptr
	                                       : Discord::AvatarCache::getInstance().getGuildIcon(info.guildId, info.guildIcon);
	if (icon) {
		Tex3DS_SubTexture sub = {(u16)icon->width, (u16)icon->height, 0.0f, 1.0f, 1.0f, 0.0f};
		C2D_Image img = {icon, &sub};
		C2D_DrawImageAt(img, iconX, iconY, 0.5f, nullptr, iconSize / icon->width, iconSize / icon->height);
	} else {
		drawRoundedRect(iconX, iconY, 0.5f, iconSize, iconSize, 18.0f, ScreenManager::colorAccent());
		std::string initial = info.guildName.empty() ? "?" : info.guildName.substr(0, 1);
		drawCenteredText(iconY + 20.0f, 0.51f, 0.8f, 0.8f, ScreenManager::colorWhite(), initial, 400.0f);
	}

	float y = iconY + iconSize + 8.0f;
	if (!info.inviterName.empty()) {
		drawCenteredText(y, 0.5f, 0.4f, 0.4f, ScreenManager::colorTextMuted(),
		                 Core::I18n::format(TR("invite.invited_by"), info.inviterName), 400.0f);
		y += 16.0f;
	}
	std::string name = info.isGroupDm ? (info.channelName.empty() ? TR("invite.group_dm") : info.channelName) : info.guildName;
	drawCenteredText(y, 0.5f, 0.7f, 0.7f, ScreenManager::colorText(), getTruncatedText(name, 380.0f, 0.7f, 0.7f), 400.0f);
	y += 28.0f;

	if (info.memberCount > 0) {
		std::string online = Core::I18n::format(TR("invite.online"), groupedNumber(info.onlineCount));
		std::string members = Core::I18n::format(TR("invite.members"), groupedNumber(info.memberCount));
		float wOnline = measureText(online, 0.42f, 0.42f), wMembers = measureText(members, 0.42f, 0.42f);
		float total = 12.0f + wOnline + 20.0f + 12.0f + wMembers;
		float x = (400.0f - total) / 2.0f;
		drawCircle(x + 4.0f, y + 7.0f, 0.5f, 4.0f, ScreenManager::colorSuccess());
		drawText(x + 12.0f, y, 0.5f, 0.42f, 0.42f, ScreenManager::colorTextMuted(), online);
		x += 12.0f + wOnline + 20.0f;
		drawCircle(x + 4.0f, y + 7.0f, 0.5f, 4.0f, ScreenManager::colorTextMuted());
		drawText(x + 12.0f, y, 0.5f, 0.42f, 0.42f, ScreenManager::colorTextMuted(), members);
		y += 20.0f;
	}
	if (!info.guildDescription.empty()) {
		auto lines = MessageUtils::wrapText(info.guildDescription, 360.0f, 0.4f);
		for (size_t i = 0; i < lines.size() && i < 3; i++) {
			drawCenteredText(y, 0.5f, 0.4f, 0.4f, ScreenManager::colorTextMuted(), lines[i], 400.0f);
			y += 13.0f;
		}
	}
	if (!info.channelName.empty() && !info.isGroupDm) {
		drawCenteredText(std::max(y + 4.0f, 214.0f), 0.5f, 0.4f, 0.4f, ScreenManager::colorTextMuted(), "#" + info.channelName,
		                 400.0f);
	}
}

void InviteOverlay::renderBottom() {
	std::lock_guard<std::recursive_mutex> lock(mutex);
	C2D_DrawRectSolid(0, 0, 0.0f, 320.0f, 240.0f, ScreenManager::colorBackgroundDark());

	if (creating) {
		if (state == State::CREATED) {
			float size = std::min(170.0f, (float)qrSize * 6.0f);
			drawQr((320.0f - size) / 2.0f, 20.0f, size);
			drawKeyHints(": " + TR("invite.create_another") + "  : " + TR("common.close"));
		} else {
			drawCenteredText(90.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), TR("invite.choose_hint"), 320.0f);
			drawKeyHints(": " + TR("invite.expires") + "  : " + TR("invite.max_uses") + "  : " +
			             TR("invite.create"));
		}
		return;
	}

	std::string label;
	u32 color = ScreenManager::colorSuccess();
	switch (state) {
	case State::READY:
		label = alreadyMember() ? TR("invite.open") : TR("invite.join");
		break;
	case State::JOINING:
		label = TR("invite.joining");
		color = ScreenManager::colorBackgroundLight();
		break;
	case State::JOINED:
		label = alreadyMember() ? TR("invite.open") : TR("invite.joined_waiting");
		color = alreadyMember() ? ScreenManager::colorSuccess() : ScreenManager::colorBackgroundLight();
		break;
	default:
		break;
	}
	if (!label.empty()) {
		drawRoundedRect(40.0f, 80.0f, 0.5f, 240.0f, 40.0f, 10.0f, color);
		drawCenteredText(91.0f, 0.51f, 0.55f, 0.55f, ScreenManager::colorWhite(), label, 320.0f);
	}
	if (!error.empty() && state != State::FAILED) {
		auto lines = MessageUtils::wrapText(error, 290.0f, 0.42f);
		float y = 136.0f;
		for (const auto &line : lines) {
			drawCenteredText(y, 0.5f, 0.42f, 0.42f, ScreenManager::colorError(), line, 320.0f);
			y += 14.0f;
		}
	}
	std::string hints = ": " + TR("common.close");
	if (state == State::READY || (state == State::JOINED && alreadyMember())) {
		hints = ": " + label + "  " + hints;
	}
	drawKeyHints(hints);
}

} // namespace UI
