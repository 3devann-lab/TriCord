#include "ui/upload_confirm.h"
#include "core/i18n.h"
#include "ui/screen_manager.h"
#include "utils/file_utils.h"
#include "utils/message_utils.h"

namespace UI {

UploadConfirm::UploadConfirm(const std::string &path, long long sizeLimit, const std::string &caption,
                             SendCallback onSend, std::function<void()> onBack, PathCallback onPlay)
    : limit(sizeLimit), onSend(std::move(onSend)), onBack(std::move(onBack)), onPlay(std::move(onPlay)) {
	choice.path = path;
	choice.caption = caption;
	size = Utils::File::fileSize(path);
	video = Utils::File::isVideoPath(path);
	image = Utils::File::isImagePath(path);
	if (image) {
		preview.load(path, 512);
	}
}

void UploadConfirm::update(u32 kDown, u32 kHeld) {
	(void)kHeld;
	preview.update();

	if (kDown & KEY_B) {
		finish();
		if (onBack) {
			onBack();
		}
		return;
	}
	if (kDown & KEY_X) {
		choice.spoiler = !choice.spoiler;
	}
	if (kDown & KEY_Y) {
		KeyboardInput input = runKeyboard(TR("upload.caption_hint"), choice.caption, true, 2000, TR("common.ok"));
		if (input.confirmed) {
			choice.caption = input.text;
		}
	}
	if ((kDown & KEY_R) && video && onPlay) {
		onPlay(choice.path);
		return;
	}
	if (kDown & KEY_A) {
		if (size < 0) {
			ScreenManager::getInstance().showToast(TR("files.open_failed"));
			return;
		}
		if (limit > 0 && size > limit) {
			ScreenManager::getInstance().showToast(
			    Core::I18n::format(TR("upload.too_large"), Utils::File::formatSize(limit)));
			return;
		}
		finish();
		if (onSend) {
			onSend(choice);
		}
	}
}

void UploadConfirm::renderTop() {
	C2D_DrawRectSolid(0, 0, 0.1f, 400, 240, ScreenManager::colorBackground());
	drawOverlayHeader(TR("upload.title"), Utils::File::formatSize(size < 0 ? 0 : size));

	const float boxX = 10.0f, boxY = 32.0f, boxW = 380.0f, boxH = 180.0f;
	if (preview.ready()) {
		preview.draw(boxX, boxY, boxW, boxH, 0.5f);
		if (choice.spoiler) {
			drawRoundedRect(150.0f, boxY + boxH / 2.0f - 12.0f, 0.6f, 100.0f, 24.0f, 12.0f,
			                ScreenManager::colorOverlay());
			drawCenteredText(boxY + boxH / 2.0f - 7.0f, 0.65f, 0.45f, 0.45f, ScreenManager::colorWhite(),
			                 TR("message.spoiler"), 400.0f);
		}
	} else {
		drawRoundedRect(boxX + 90.0f, boxY + 30.0f, 0.4f, boxW - 180.0f, boxH - 60.0f, 10.0f,
		                ScreenManager::colorBackgroundLight());
		std::string label = video ? TR("files.video_file") : image ? TR("common.loading") : TR("upload.file");
		if (image && preview.failed()) {
			label = TR("upload.no_preview");
		}
		drawCenteredText(boxY + boxH / 2.0f - 8.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(), label, 400.0f);
	}

	const std::string name = getTruncatedText(Utils::File::baseName(choice.path), 380.0f, 0.45f, 0.45f);
	drawCenteredText(240.0f - 24.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorText(), name, 400.0f);
}

void UploadConfirm::renderBottom() {
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, ScreenManager::colorBackgroundDark());
	drawText(10.0f, 8.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorText(), TR("upload.caption"));
	C2D_DrawRectSolid(10, 30, 0.5f, 300, 1, ScreenManager::colorSeparator());

	float y = 38.0f;
	if (choice.caption.empty()) {
		drawText(10.0f, y, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), TR("upload.no_caption"));
		y += 16.0f;
	} else {
		const auto lines = MessageUtils::wrapText(choice.caption, 300.0f, 0.45f);
		for (size_t i = 0; i < lines.size() && i < 6; i++) {
			drawText(10.0f, y, 0.5f, 0.45f, 0.45f, ScreenManager::colorText(), lines[i]);
			y += 15.0f;
		}
	}

	y = std::max(y + 8.0f, 140.0f);
	drawText(10.0f, y, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(), TR("upload.spoiler"));
	const float toggleW = 34.0f, toggleH = 18.0f, toggleX = 320.0f - 20.0f - toggleW;
	drawRoundedRect(toggleX, y - 2.0f, 0.5f, toggleW, toggleH, toggleH / 2.0f,
	                choice.spoiler ? ScreenManager::colorSelection() : ScreenManager::colorSeparator());
	const float knob = 14.0f;
	drawCircle((choice.spoiler ? toggleX + toggleW - knob - 2.0f : toggleX + 2.0f) + knob / 2.0f, y + knob / 2.0f,
	           0.55f, knob / 2.0f, ScreenManager::colorWhite());

	if (limit > 0 && size > limit) {
		drawText(10.0f, y + 24.0f, 0.5f, 0.42f, 0.42f, ScreenManager::colorError(),
		         Core::I18n::format(TR("upload.too_large"), Utils::File::formatSize(limit)));
	}

	std::string hints = ": " + TR("common.send") + "  : " + TR("upload.caption") + "  : " +
	                    TR("upload.spoiler") + "  : " + TR("common.back");
	if (video && onPlay) {
		hints += "  R: " + TR("video.play");
	}
	drawKeyHints(hints);
}

} // namespace UI
