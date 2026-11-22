#pragma once

#include <3ds.h>
#include <string>

namespace Core {
namespace SystemStatus {

void init();
void exit();

// Main thread, once per frame after hidScanInput().
void update();

// Re-reads the settings that change system behaviour (sleep on lid close).
void applySettings();

// -1 while unknown.
int batteryPercent();
bool isCharging();
// 0..3; only meaningful while connected.
int wifiBars();
bool isWifiConnected();

// True while the lid is shut and TriCord is keeping itself awake.
bool isLidClosed();
// Suspended to the HOME Menu, asleep, or lid closed: nobody is looking at the app.
bool isInBackground();

// A DM or mention arrived. Any thread. Blinks the notification light and, when the
// app is in the background, posts it to the HOME Menu's Notifications applet.
void notifyMessage(const std::string &title, const std::string &body);

// Stops the notification light (the user is looking now).
void clearLed();

} // namespace SystemStatus
} // namespace Core
