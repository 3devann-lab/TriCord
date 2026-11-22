#include "ui/presence_ui.h"
#include "core/i18n.h"
#include "ui/image_manager.h"
#include "ui/screen_manager.h"

namespace UI {

namespace {

const char *iconFor(Discord::UserStatus status, bool mobile) {
	switch (status) {
	case Discord::UserStatus::ONLINE:
		return mobile ? "romfs:/discord-icons/status/mobile.png" : "romfs:/discord-icons/status/online.png";
	case Discord::UserStatus::IDLE:
		return "romfs:/discord-icons/status/idle.png";
	case Discord::UserStatus::DND:
		return "romfs:/discord-icons/status/donotdisturb.png";
	default:
		return "romfs:/discord-icons/status/offline.png";
	}
}

} // namespace

std::string statusLabel(const Discord::Presence &presence) {
	switch (presence.status) {
	case Discord::UserStatus::ONLINE:
		return presence.mobile ? TR("presence.online_mobile") : TR("status.online");
	case Discord::UserStatus::IDLE:
		return TR("status.idle");
	case Discord::UserStatus::DND:
		return TR("status.dnd");
	default:
		return TR("presence.offline");
	}
}

std::string presenceLine(const Discord::Presence &presence) {
	return presence.activity.empty() ? presence.customStatus : presence.activity;
}

void drawStatusBadge(float cx, float cy, float size, const Discord::Presence &presence, u32 ringColor, float z) {
	drawCircle(cx, cy, z, size / 2.0f + 1.5f, ringColor);
	C3D_Tex *tex = ImageManager::getInstance().getLocalImage(iconFor(presence.status, presence.mobile));
	if (!tex) {
		return;
	}
	Tex3DS_SubTexture subtex = {(u16)tex->width, (u16)tex->height, 0.0f, 1.0f, 1.0f, 0.0f};
	C2D_Image img = {tex, &subtex};
	C2D_DrawImageAt(img, cx - size / 2.0f, cy - size / 2.0f, z + 0.001f, nullptr, size / tex->width,
	                size / tex->height);
}

} // namespace UI
