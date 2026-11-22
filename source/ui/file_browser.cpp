#include "ui/file_browser.h"
#include "core/i18n.h"
#include "log.h"
#include "ui/image_manager.h"
#include "ui/screen_manager.h"
#include "utils/file_utils.h"
#include "utils/redraw.h"
#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <sys/stat.h>

namespace UI {

namespace {

std::string savedDirectory;

constexpr float ROW_H = 22.0f;
constexpr float LIST_TOP = 30.0f;

std::string defaultDirectory() {
	// Photos taken with the 3DS camera live here, which is the most likely thing to send.
	if (Utils::File::exists("sdmc:/DCIM")) {
		return "sdmc:/DCIM";
	}
	return "sdmc:/";
}

std::string joinPath(const std::string &dir, const std::string &name) {
	if (!dir.empty() && dir.back() == '/') {
		return dir + name;
	}
	return dir + "/" + name;
}

std::string lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}

bool isMedia(const std::string &name) { return Utils::File::isImagePath(name) || Utils::File::isVideoPath(name); }

const char *iconFor(const std::string &name, bool directory) {
	if (directory) {
		return "romfs:/discord-icons/folder.png";
	}
	if (Utils::File::isVideoPath(name)) {
		return "romfs:/discord-icons/stage.png";
	}
	if (Utils::File::isImagePath(name)) {
		return "romfs:/discord-icons/object.png";
	}
	return "romfs:/discord-icons/text.png";
}

void drawIcon(const char *path, float x, float y, float size, u32 color) {
	C3D_Tex *tex = ImageManager::getInstance().getLocalImage(path);
	if (!tex) {
		return;
	}
	Tex3DS_SubTexture subtex = {(u16)tex->width, (u16)tex->height, 0.0f, 1.0f, 1.0f, 0.0f};
	C2D_Image img = {tex, &subtex};
	C2D_ImageTint tint;
	C2D_PlainImageTint(&tint, color, 1.0f);
	C2D_DrawImageAt(img, x, y, 0.6f, &tint, size / tex->width, size / tex->height);
}

} // namespace

std::string FileBrowser::lastDirectory() {
	return savedDirectory.empty() ? defaultDirectory() : savedDirectory;
}

FileBrowser::FileBrowser(ChooseCallback onChoose) : onChoose(std::move(onChoose)) {
	open(lastDirectory());
	if (!error.empty() && directory != "sdmc:/") {
		open("sdmc:/");
	}
}

std::string FileBrowser::selectedPath() const {
	if (selected < 0 || selected >= (int)entries.size()) {
		return "";
	}
	return joinPath(directory, entries[selected].name);
}

void FileBrowser::open(const std::string &dir, const std::string &selectName) {
	entries.clear();
	error.clear();
	selected = 0;
	scroll = 0;
	preview.clear();

	DIR *d = opendir(dir.c_str());
	if (!d) {
		error = TR("files.open_failed");
		directory = dir;
		return;
	}
	directory = dir;
	savedDirectory = dir;

	while (struct dirent *ent = readdir(d)) {
		const std::string name = ent->d_name;
		if (name == "." || name == "..") {
			continue;
		}
		Entry entry;
		entry.name = name;
		entry.directory = ent->d_type == DT_DIR;
		if (ent->d_type == DT_UNKNOWN) {
			struct stat st;
			if (stat(joinPath(dir, name).c_str(), &st) == 0) {
				entry.directory = S_ISDIR(st.st_mode);
			}
		}
		if (!entry.directory && mediaOnly && !isMedia(name)) {
			continue;
		}
		entries.push_back(std::move(entry));
	}
	closedir(d);

	std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
		if (a.directory != b.directory) {
			return a.directory;
		}
		return lower(a.name) < lower(b.name);
	});

	// Sizes are filled for files only; a stat per folder would slow big directories down for nothing.
	for (Entry &entry : entries) {
		if (!entry.directory) {
			entry.size = Utils::File::fileSize(joinPath(dir, entry.name));
		}
	}

	if (!selectName.empty()) {
		for (size_t i = 0; i < entries.size(); i++) {
			if (entries[i].name == selectName) {
				selected = (int)i;
				break;
			}
		}
	}
	scroll = std::max(0, std::min(selected - VISIBLE_ROWS / 2, (int)entries.size() - VISIBLE_ROWS));
	updatePreview();
	Utils::requestRedraw();
}

void FileBrowser::goUp() {
	const std::string parent = Utils::File::parentDirectory(directory);
	if (parent == directory || directory == "sdmc:/") {
		finish();
		return;
	}
	open(parent, Utils::File::baseName(directory));
}

void FileBrowser::activate() {
	if (selected < 0 || selected >= (int)entries.size()) {
		return;
	}
	const Entry &entry = entries[selected];
	if (entry.directory) {
		open(joinPath(directory, entry.name));
		return;
	}
	const std::string path = selectedPath();
	finish();
	if (onChoose) {
		onChoose(path);
	}
}

void FileBrowser::move(int delta) {
	if (entries.empty()) {
		return;
	}
	selected = std::clamp(selected + delta, 0, (int)entries.size() - 1);
	if (selected < scroll) {
		scroll = selected;
	} else if (selected >= scroll + VISIBLE_ROWS) {
		scroll = selected - VISIBLE_ROWS + 1;
	}
	updatePreview();
}

void FileBrowser::updatePreview() {
	const std::string path = selectedPath();
	if (!path.empty() && !entries[selected].directory && Utils::File::isImagePath(path)) {
		preview.load(path, 256);
	} else {
		preview.clear();
	}
}

void FileBrowser::update(u32 kDown, u32 kHeld) {
	preview.update();

	if (kDown & KEY_B) {
		goUp();
		return;
	}
	if (kDown & KEY_X) {
		finish();
		return;
	}
	if (kDown & KEY_Y) {
		mediaOnly = !mediaOnly;
		const std::string keep = selected < (int)entries.size() ? entries[selected].name : "";
		open(directory, keep);
		return;
	}
	if (kDown & KEY_A) {
		activate();
		return;
	}

	const u32 dirs = KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT;
	u32 step = 0;
	if (kDown & dirs) {
		step = kDown & dirs;
		repeatKey = step;
		repeatTimer = 20;
	} else if ((kHeld & repeatKey) && repeatKey) {
		if (--repeatTimer <= 0) {
			step = repeatKey;
			repeatTimer = 4;
		}
	} else {
		repeatKey = 0;
	}
	if (step & KEY_UP) {
		move(-1);
	} else if (step & KEY_DOWN) {
		move(1);
	} else if (step & KEY_LEFT) {
		move(-VISIBLE_ROWS);
	} else if (step & KEY_RIGHT) {
		move(VISIBLE_ROWS);
	}

	if (kDown & KEY_L) {
		goUp();
	}
}

void FileBrowser::renderTop() {
	C2D_DrawRectSolid(0, 0, 0.1f, 400, 240, ScreenManager::colorBackground());
	drawOverlayHeader(directory, mediaOnly ? TR("files.filter_media") : TR("files.filter_all"));

	if (!error.empty()) {
		drawCenteredText(110.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorError(), error, 400.0f);
		return;
	}
	if (entries.empty()) {
		drawCenteredText(110.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(), TR("files.empty"), 400.0f);
		return;
	}

	for (int i = scroll; i < (int)entries.size() && i < scroll + VISIBLE_ROWS; i++) {
		const Entry &entry = entries[i];
		const float y = LIST_TOP + (i - scroll) * ROW_H;
		const bool isSelected = i == selected;
		if (isSelected) {
			drawRoundedRect(6.0f, y, 0.3f, 388.0f, ROW_H - 2.0f, 6.0f, ScreenManager::colorBackgroundLight());
			drawRoundedRect(6.0f, y + 4.0f, 0.35f, 3.0f, ROW_H - 10.0f, 1.5f, ScreenManager::colorSelection());
		}
		const u32 color = isSelected ? ScreenManager::colorText() : ScreenManager::colorTextMuted();
		drawIcon(iconFor(entry.name, entry.directory), 14.0f, y + 3.0f, 14.0f, color);

		std::string sizeText = entry.directory ? "" : Utils::File::formatSize(entry.size);
		const float sizeW = sizeText.empty() ? 0.0f : measureText(sizeText, 0.4f, 0.4f);
		const std::string name = getTruncatedText(entry.name, 360.0f - 40.0f - sizeW, 0.45f, 0.45f);
		drawText(36.0f, y + 3.0f, 0.5f, 0.45f, 0.45f, color, name);
		if (!sizeText.empty()) {
			drawText(388.0f - sizeW, y + 4.0f, 0.5f, 0.4f, 0.4f, ScreenManager::colorTextMuted(), sizeText);
		}
	}

	if ((int)entries.size() > VISIBLE_ROWS) {
		const float trackH = VISIBLE_ROWS * ROW_H;
		const float thumbH = std::max(12.0f, trackH * VISIBLE_ROWS / entries.size());
		const float maxScroll = (float)(entries.size() - VISIBLE_ROWS);
		const float thumbY = LIST_TOP + (trackH - thumbH) * (scroll / maxScroll);
		C2D_DrawRectSolid(396.0f, thumbY, 0.6f, 3.0f, thumbH, ScreenManager::colorTextMuted());
	}
}

void FileBrowser::renderBottom() {
	C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, ScreenManager::colorBackgroundDark());
	drawText(10.0f, 8.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorText(), TR("files.title"));
	C2D_DrawRectSolid(10, 30, 0.5f, 300, 1, ScreenManager::colorSeparator());

	if (selected >= 0 && selected < (int)entries.size()) {
		const Entry &entry = entries[selected];
		const std::string name = getTruncatedText(entry.name, 300.0f, 0.45f, 0.45f);
		drawText(10.0f, 36.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorText(), name);
		if (!entry.directory) {
			std::string info = Utils::File::formatSize(entry.size);
			if (preview.ready()) {
				info += "  ·  " + std::to_string(preview.imageWidth()) + "×" + std::to_string(preview.imageHeight());
			}
			drawText(10.0f, 52.0f, 0.5f, 0.4f, 0.4f, ScreenManager::colorTextMuted(), info);
		}

		const float boxX = 10.0f, boxY = 70.0f, boxW = 300.0f, boxH = 140.0f;
		if (preview.ready()) {
			preview.draw(boxX, boxY, boxW, boxH, 0.5f);
		} else if (preview.loading()) {
			drawCenteredText(boxY + boxH / 2.0f - 8.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(),
			                 TR("common.loading"), 320.0f);
		} else if (!entry.directory && Utils::File::isVideoPath(entry.name)) {
			drawCenteredText(boxY + boxH / 2.0f - 8.0f, 0.5f, 0.45f, 0.45f, ScreenManager::colorTextMuted(),
			                 TR("files.video_file"), 320.0f);
		}
	}

	drawKeyHints(": " + TR("common.select") + "  : " + TR("files.up") + "  : " +
	             (mediaOnly ? TR("files.show_all") : TR("files.show_media")) + "  : " + TR("common.close"));
}

} // namespace UI
