#include "core/system_status.h"
#include "core/config.h"
#include "core/log.h"
#include "discord/discord_client.h"
#include "utils/redraw.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>

namespace Core {
namespace SystemStatus {

namespace {

std::mutex notifyMutex;
// Connected on a background thread, so these are only read once servicesReady is set.
std::atomic<bool> ptmuReady{false};
std::atomic<bool> mcuReady{false};
std::atomic<bool> lcdReady{false};
std::atomic<bool> newsReady{false};
std::atomic<bool> acReady{false};
std::atomic<bool> servicesReady{false};
bool settingsApplied = false;
// The lid only counts as shut after it has been seen open and then read shut a few times running.
bool lidSeenOpen = false;
int closedReads = 0;

std::atomic<int> battery{-1};
std::atomic<bool> charging{false};
std::atomic<int> bars{0};
std::atomic<bool> wifiConnected{false};

std::atomic<bool> lidClosed{false};
std::atomic<bool> suspended{false};
std::atomic<bool> asleep{false};
std::atomic<bool> ledOn{false};
std::atomic<u64> lastInputMs{0};

bool sleepBlocked = false;
int pollCountdown = 0;
int lidCountdown = 0;

u64 lastNewsMs = 0;
std::string lastNewsTitle;

aptHookCookie hookCookie;

// Two soft pulses of Discord blurple, then a pause, repeating until cleared.
void writeLed(bool on) {
	if (!mcuReady) {
		return;
	}
	InfoLedPattern pattern;
	memset(&pattern, 0, sizeof(pattern));
	if (on) {
		static const u8 wave[] = {0, 90, 200, 255, 200, 90, 0, 0, 90, 200, 255, 200, 90, 0};
		pattern.delay = 2;
		pattern.smoothing = 0x30;
		pattern.loopDelay = 0x20;
		for (size_t i = 0; i < sizeof(wave); i++) {
			pattern.redPattern[i] = (u8)(wave[i] * 0x58 / 255);
			pattern.greenPattern[i] = (u8)(wave[i] * 0x65 / 255);
			pattern.bluePattern[i] = (u8)(wave[i] * 0xF2 / 255);
		}
	} else {
		pattern.loopDelay = 0xFF;
	}
	if (R_FAILED(MCUHWC_SetInfoLedPattern(&pattern))) {
		Logger::log("[System] Could not set the notification light");
	}
}

void setBacklights(bool on) {
	if (!lcdReady) {
		return;
	}
	if (on) {
		GSPLCD_PowerOnAllBacklights();
	} else {
		GSPLCD_PowerOffAllBacklights();
	}
}

void handleLid(bool closed) {
	if (lidClosed.load() == closed) {
		return;
	}
	lidClosed.store(closed);
	Logger::log("[System] Lid %s", closed ? "closed" : "opened");
	setBacklights(!closed);
	Discord::DiscordClient::getInstance().setAutoIdle(closed);
	if (!closed) {
		clearLed();
		lastInputMs.store(osGetTime());
		Utils::requestRedraw();
	}
}

void pollPower() {
	int percent = -1;
	if (mcuReady) {
		u8 level = 0;
		if (R_SUCCEEDED(MCUHWC_GetBatteryLevel(&level))) {
			percent = level > 100 ? 100 : level;
		}
	}
	if (percent < 0 && ptmuReady) {
		u8 level = 0;
		if (R_SUCCEEDED(PTMU_GetBatteryLevel(&level))) {
			percent = level * 20;
		}
	}
	bool isCharging = false;
	if (ptmuReady) {
		u8 state = 0;
		if (R_SUCCEEDED(PTMU_GetBatteryChargeState(&state))) {
			isCharging = state != 0;
		}
	}

	int strength = osGetWifiStrength();
	bool connected = strength > 0;
	if (acReady) {
		u32 status = 0;
		if (R_SUCCEEDED(ACU_GetWifiStatus(&status))) {
			connected = status != 0;
		}
	}

	bool changed = percent != battery.load() || isCharging != charging.load() || strength != bars.load() ||
	               connected != wifiConnected.load();
	battery.store(percent);
	charging.store(isCharging);
	bars.store(strength);
	wifiConnected.store(connected);
	if (changed && Config::getInstance().isStatusBarEnabled()) {
		Utils::requestRedraw();
	}
}

void aptCallback(APT_HookType hook, void *) {
	switch (hook) {
	case APTHOOK_ONSUSPEND:
		suspended.store(true);
		break;
	case APTHOOK_ONRESTORE:
		suspended.store(false);
		clearLed();
		lastInputMs.store(osGetTime());
		break;
	case APTHOOK_ONSLEEP:
		asleep.store(true);
		break;
	case APTHOOK_ONWAKEUP:
		asleep.store(false);
		break;
	default:
		break;
	}
}

// Notification titles are at most 32 UTF-16 units; messages are kept short as well.
size_t toUtf16(const std::string &text, u16 *out, size_t maxUnits) {
	ssize_t units = utf8_to_utf16(out, (const uint8_t *)text.c_str(), maxUnits);
	if (units < 0) {
		units = 0;
	}
	if ((size_t)units > maxUnits) {
		units = maxUnits;
	}
	// Never end on half of a surrogate pair.
	if (units > 0 && out[units - 1] >= 0xD800 && out[units - 1] <= 0xDBFF) {
		units--;
	}
	out[units] = 0;
	return (size_t)units;
}

} // namespace

// Optional services: if one is missing or already fully in use (Rosalina keeps some open),
// srvGetServiceHandle would otherwise wait forever, so this runs on its own thread and asks
// the service manager to fail instead of waiting.
void connectServices(void *) {
	srvSetBlockingPolicy(true);
	auto connect = [](const char *name, Result (*fn)(), std::atomic<bool> &flag) {
		bool registered = false;
		if (R_FAILED(srvIsServiceRegistered(&registered, name)) || !registered) {
			Logger::log("[System] %s is not available", name);
			return;
		}
		Result res = fn();
		flag.store(R_SUCCEEDED(res));
		if (R_FAILED(res)) {
			Logger::log("[System] %s: %08lX", name, (unsigned long)res);
		}
	};
	connect("ptm:u", ptmuInit, ptmuReady);
	connect("mcu::HWC", mcuHwcInit, mcuReady);
	connect("gsp::Lcd", gspLcdInit, lcdReady);
	connect("news:u", newsInit, newsReady);
	connect("ac:u", acInit, acReady);
	srvSetBlockingPolicy(false);
	Logger::log("[System] ptm=%d mcu=%d lcd=%d news=%d ac=%d", ptmuReady.load(), mcuReady.load(), lcdReady.load(),
	            newsReady.load(), acReady.load());
	servicesReady.store(true);
	Utils::requestRedraw();
}

void init() {
	aptHook(&hookCookie, aptCallback, nullptr);
	lastInputMs.store(osGetTime());
	s32 priority = 0x30;
	svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
	if (!threadCreate(connectServices, nullptr, 16 * 1024, std::min(0x3F, (int)priority + 1), -2, true)) {
		Logger::log("[System] Could not start the service thread");
	}
}

void exit() {
	aptUnhook(&hookCookie);
	// Still connecting (or stuck): leave the services alone.
	if (!servicesReady.load()) {
		return;
	}
	clearLed();
	if (lidClosed.load()) {
		setBacklights(true);
	}
	if (sleepBlocked) {
		aptSetSleepAllowed(true);
		sleepBlocked = false;
	}
	if (acReady.exchange(false)) {
		acExit();
	}
	if (newsReady.exchange(false)) {
		newsExit();
	}
	if (lcdReady.exchange(false)) {
		gspLcdExit();
	}
	if (mcuReady.exchange(false)) {
		mcuHwcExit();
	}
	if (ptmuReady.exchange(false)) {
		ptmuExit();
	}
}

void applySettings() {
	// Staying awake with the lid shut is what lets TriCord go Idle, keep the
	// connection, blink the light and post notifications while closed.
	bool block = Config::getInstance().isIdleOnLidCloseEnabled() && ptmuReady;
	if (block != sleepBlocked) {
		aptSetSleepAllowed(!block);
		sleepBlocked = block;
		if (!block) {
			handleLid(false);
		}
	}
}

void update() {
	u64 now = osGetTime();
	bool input = hidKeysDown() || hidKeysHeld();
	if (input) {
		lastInputMs.store(now);
		if (ledOn.load() && !lidClosed.load()) {
			clearLed();
		}
	}
	// Buttons or the touch screen in use: the lid is open, whatever was read before.
	if (input && lidClosed.load()) {
		closedReads = 0;
		handleLid(false);
	}

	if (!servicesReady.load()) {
		return;
	}
	if (!settingsApplied) {
		settingsApplied = true;
		applySettings();
		pollPower();
	}

	if (sleepBlocked && --lidCountdown <= 0) {
		lidCountdown = 10;
		u8 shell = 1;
		if (R_SUCCEEDED(PTMU_GetShellState(&shell))) {
			if (shell != 0) {
				lidSeenOpen = true;
				closedReads = 0;
				handleLid(false);
			} else if (lidSeenOpen && ++closedReads >= 3) {
				handleLid(true);
			}
		}
	}

	if (--pollCountdown <= 0) {
		pollCountdown = 120;
		pollPower();
	}
}

int batteryPercent() { return battery.load(); }
bool isCharging() { return charging.load(); }
int wifiBars() { return bars.load(); }
bool isWifiConnected() { return wifiConnected.load(); }
bool isLidClosed() { return lidClosed.load(); }
bool isInBackground() { return suspended.load() || asleep.load() || lidClosed.load(); }

void notifyMessage(const std::string &title, const std::string &body) {
	std::lock_guard<std::mutex> lock(notifyMutex);
	if (!servicesReady.load()) {
		return;
	}
	u64 now = osGetTime();
	bool away = isInBackground();
	// Left untouched for a minute counts as away too, for the light only.
	bool idle = now - lastInputMs.load() > 60000;

	if (Config::getInstance().isNotifyLedEnabled() && mcuReady && (away || idle) && !ledOn.load()) {
		writeLed(true);
		ledOn.store(true);
	}

	if (!Config::getInstance().isHomeNotificationsEnabled() || !newsReady || !away) {
		return;
	}
	// A busy DM posts one notification, not one per message.
	if (title == lastNewsTitle && now - lastNewsMs < 20000) {
		return;
	}
	lastNewsTitle = title;
	lastNewsMs = now;

	u16 title16[33];
	u16 body16[257];
	size_t titleLen = toUtf16(title, title16, 32);
	size_t bodyLen = toUtf16(body, body16, 256);
	if (R_FAILED(NEWS_AddNotification(title16, titleLen, body16, bodyLen, nullptr, 0, false))) {
		Logger::log("[System] Could not post a HOME Menu notification");
	}
}

void clearLed() {
	if (ledOn.exchange(false)) {
		writeLed(false);
	}
}

} // namespace SystemStatus
} // namespace Core
