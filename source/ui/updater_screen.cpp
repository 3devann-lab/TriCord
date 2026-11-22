#include "ui/updater_screen.h"
#include "core/i18n.h"
#include "core/updater.h"
#include "ui/overlay.h"
#include "utils/file_utils.h"
#include "utils/message_utils.h"
#include <cmath>

namespace UI {

namespace {

using Core::Updater::State;

std::string shortSha(const std::string &sha) { return sha.empty() ? TR("updater.local_build") : sha.substr(0, 7); }

std::string errorText(const std::string &code) {
	if (code == "rate") {
		return TR("updater.error.rate");
	}
	if (code == "download") {
		return TR("updater.error.download");
	}
	if (code == "corrupt") {
		return TR("updater.error.corrupt");
	}
	if (code == "am" || code == "install") {
		return TR("updater.error.install");
	}
	return TR("updater.error.check");
}

void drawSpinner(float cx, float cy, float t) {
	for (int i = 0; i < 8; i++) {
		float a = t + i * 0.785f;
		u32 c = (ScreenManager::colorAccent() & 0x00FFFFFF) | ((u32)(40 + i * 26) << 24);
		drawCircle(cx + cosf(a) * 9.0f, cy + sinf(a) * 9.0f, 0.5f, 2.2f, c);
	}
}

} // namespace

void UpdaterScreen::onEnter() {
	State state = Core::Updater::status().state;
	if (state != State::DOWNLOADING && state != State::INSTALLING && state != State::INSTALLED) {
		Core::Updater::checkAsync();
	}
}

bool UpdaterScreen::isAnimating() const {
	State state = Core::Updater::status().state;
	return state == State::CHECKING || state == State::DOWNLOADING || state == State::INSTALLING;
}

void UpdaterScreen::update() {
	spinner += 0.15f;
	u32 kDown = hidKeysDown();
	Core::Updater::Status status = Core::Updater::status();
	bool busy = status.state == State::DOWNLOADING || status.state == State::INSTALLING;

	if ((kDown & KEY_B) && !busy) {
		if (confirmInstall) {
			confirmInstall = false;
			return;
		}
		ScreenManager::getInstance().returnToPreviousScreen();
		return;
	}

	switch (status.state) {
	case State::AVAILABLE:
	case State::UP_TO_DATE:
		// Up to date can still reinstall, e.g. to repair an install.
		if (kDown & KEY_A) {
			if (status.state == State::UP_TO_DATE && !confirmInstall) {
				confirmInstall = true;
			} else {
				confirmInstall = false;
				Core::Updater::installAsync();
			}
		}
		if (kDown & KEY_Y) {
			Core::Updater::checkAsync();
		}
		break;
	case State::FAILED:
		if (kDown & KEY_A) {
			Core::Updater::checkAsync();
		}
		break;
	case State::INSTALLED:
		if (kDown & KEY_A) {
			ScreenManager::getInstance().requestAppExit();
		}
		break;
	default:
		break;
	}
}

void UpdaterScreen::renderTop(C3D_RenderTarget *target) {
	(void)target;
	C2D_DrawRectSolid(0, 0, 0.1f, 400, 240, ScreenManager::colorBackground());
	drawOverlayHeader(TR("updater.title"), Core::Updater::repository());
	Core::Updater::Status status = Core::Updater::status();
	const Core::Updater::BuildInfo &build = Core::Updater::build();

	float y = 40.0f;
	drawText(16.0f, y, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), TR("updater.this_build"));
	std::string mine = shortSha(build.sha);
	if (build.time > 0) {
		mine += "  (" + MessageUtils::formatTimestamp(MessageUtils::getISOTimestamp(build.time)) + ")";
	}
	drawText(150.0f, y, 0.5f, 0.45f, 0.45f, ScreenManager::colorText(), mine);
	y += 18.0f;
	if (!status.latestSha.empty()) {
		drawText(16.0f, y, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), TR("updater.latest"));
		std::string latest = status.latestSha;
		if (status.latestTime > 0) {
			latest += "  (" + MessageUtils::formatTimestamp(MessageUtils::getISOTimestamp(status.latestTime)) + ")";
		}
		drawText(150.0f, y, 0.5f, 0.45f, 0.45f, ScreenManager::colorText(), latest);
	}
	y += 26.0f;

	if (status.state == State::AVAILABLE || status.state == State::DOWNLOADING || status.state == State::INSTALLING) {
		drawText(16.0f, y, 0.5f, 0.5f, 0.5f, ScreenManager::colorAccent(), TR("updater.whats_new"));
		y += 20.0f;
		if (status.changes.empty()) {
			drawText(24.0f, y, 0.5f, 0.42f, 0.42f, ScreenManager::colorTextMuted(),
			         build.sha.empty() ? TR("updater.unknown_changes") : TR("common.loading"));
		}
		for (const auto &change : status.changes) {
			if (y > 220.0f) {
				break;
			}
			drawText(18.0f, y, 0.5f, 0.42f, 0.42f, ScreenManager::colorText(),
			         getTruncatedText("• " + change, 366.0f, 0.42f, 0.42f));
			y += 15.0f;
		}
	} else if (status.state == State::UP_TO_DATE) {
		drawCenteredText(130.0f, 0.5f, 0.55f, 0.55f, ScreenManager::colorSuccess(), TR("updater.up_to_date"), 400.0f);
	}
}

void UpdaterScreen::renderBottom(C3D_RenderTarget *target) {
	(void)target;
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, ScreenManager::colorBackgroundDark());
	Core::Updater::Status status = Core::Updater::status();
	std::string message;
	std::string hints = ": " + TR("common.back");
	u32 color = ScreenManager::colorText();

	switch (status.state) {
	case State::IDLE:
	case State::CHECKING:
		message = TR("updater.checking");
		drawSpinner(160.0f, 120.0f, spinner);
		break;
	case State::UP_TO_DATE:
		message = confirmInstall ? TR("updater.confirm_reinstall") : TR("updater.up_to_date_hint");
		hints = ": " + TR(confirmInstall ? "updater.reinstall_confirm" : "updater.reinstall") + "  : " +
		        TR("updater.check_again") + "  " + hints;
		break;
	case State::AVAILABLE:
		message = TR("updater.available");
		color = ScreenManager::colorAccent();
		hints = ": " + TR("updater.install") + "  : " + TR("updater.check_again") + "  " + hints;
		break;
	case State::DOWNLOADING:
	case State::INSTALLING: {
		message = TR(status.state == State::DOWNLOADING ? "updater.downloading" : "updater.installing");
		float fraction = status.total > 0 ? (float)status.done / status.total : 0.0f;
		drawProgressBar(40.0f, 130.0f, 240.0f, 8.0f, fraction, 0.6f);
		std::string amount = Utils::File::formatSize(status.done);
		if (status.total > 0) {
			amount += " / " + Utils::File::formatSize(status.total);
		}
		drawCenteredText(146.0f, 0.5f, 0.42f, 0.42f, ScreenManager::colorTextMuted(), amount, 320.0f);
		hints = TR("updater.keep_open");
		break;
	}
	case State::INSTALLED:
		message = TR("updater.installed");
		color = ScreenManager::colorSuccess();
		hints = ": " + TR("updater.close_app") + "  " + hints;
		break;
	case State::FAILED:
		message = errorText(status.error);
		color = ScreenManager::colorError();
		hints = ": " + TR("common.retry") + "  " + hints;
		break;
	}

	auto lines = MessageUtils::wrapText(message, 290.0f, 0.45f);
	float y = 70.0f - lines.size() * 8.0f;
	for (const auto &line : lines) {
		drawCenteredText(y, 0.5f, 0.45f, 0.45f, color, line, 320.0f);
		y += 16.0f;
	}
	drawKeyHints(hints);
}

} // namespace UI
