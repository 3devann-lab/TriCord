#pragma once

#include <string>

namespace Core {
namespace Boot {

// Decides whether this launch is a safe start: SELECT is held, or the last launch never
// finished starting (its marker file is still there). Writes the marker for this launch.
// Call once the SD card is usable, before the settings are loaded.
bool begin();
bool isSafeStart();
// What safe start switched off, for a message once the app is up. Empty when not safe.
std::string safeStartNote();

// Draws a plain "Starting TriCord: <step>" frame, before the real screens exist, so a
// startup that stops somewhere says where.
void show(const char *step);
// Frees what show() used; the screen manager draws from here on.
void endScreens();

// Called every frame from the main loop; once the app has clearly started (connected for a
// few seconds, or a minute of running), the launch marker is removed.
void tick(bool connected);

} // namespace Boot
} // namespace Core
