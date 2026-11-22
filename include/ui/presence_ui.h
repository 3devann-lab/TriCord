#ifndef PRESENCE_UI_H
#define PRESENCE_UI_H

#include "discord/types.h"
#include <3ds.h>
#include <string>

namespace UI {

std::string statusLabel(const Discord::Presence &presence);
// Discord's status badge, centered on (cx, cy), with a ring in ringColor so it stands off an avatar.
void drawStatusBadge(float cx, float cy, float size, const Discord::Presence &presence, u32 ringColor, float z);
// The activity if there is one, else the custom status; empty when neither.
std::string presenceLine(const Discord::Presence &presence);

} // namespace UI

#endif // PRESENCE_UI_H
