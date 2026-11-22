#ifndef UPLOAD_CONFIRM_H
#define UPLOAD_CONFIRM_H

#include "ui/local_preview.h"
#include "ui/overlay.h"
#include <functional>
#include <string>

namespace UI {

struct UploadChoice {
	std::string path;
	std::string caption;
	bool spoiler = false;
};

// Shows a file before it is sent, with a caption and the spoiler flag.
class UploadConfirm : public Overlay {
  public:
	using SendCallback = std::function<void(const UploadChoice &choice)>;
	using PathCallback = std::function<void(const std::string &path)>;

	UploadConfirm(const std::string &path, long long sizeLimit, const std::string &caption, SendCallback onSend,
	              std::function<void()> onBack, PathCallback onPlay = nullptr);

	void update(u32 kDown, u32 kHeld) override;
	void renderTop() override;
	void renderBottom() override;

  private:
	UploadChoice choice;
	long long size = 0;
	long long limit = 0;
	bool video = false;
	bool image = false;
	LocalPreview preview;
	SendCallback onSend;
	std::function<void()> onBack;
	PathCallback onPlay;
};

} // namespace UI

#endif // UPLOAD_CONFIRM_H
