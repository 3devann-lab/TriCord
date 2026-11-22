#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include "ui/local_preview.h"
#include "ui/overlay.h"
#include <functional>
#include <string>
#include <vector>

namespace UI {

// Browses the SD card for a file to send.
class FileBrowser : public Overlay {
  public:
	using ChooseCallback = std::function<void(const std::string &path)>;

	explicit FileBrowser(ChooseCallback onChoose);

	void update(u32 kDown, u32 kHeld) override;
	void renderTop() override;
	void renderBottom() override;

	// Where the next browser opens, so picking another file starts where the last one was.
	static std::string lastDirectory();

  private:
	struct Entry {
		std::string name;
		bool directory = false;
		long long size = 0;
	};

	ChooseCallback onChoose;
	std::string directory;
	std::vector<Entry> entries;
	bool mediaOnly = true;
	int selected = 0;
	int scroll = 0;
	int repeatTimer = 0;
	u32 repeatKey = 0;
	std::string error;
	LocalPreview preview;

	void open(const std::string &dir, const std::string &selectName = "");
	void goUp();
	void activate();
	void move(int delta);
	void updatePreview();
	std::string selectedPath() const;
	static constexpr int VISIBLE_ROWS = 9;
};

} // namespace UI

#endif // FILE_BROWSER_H
