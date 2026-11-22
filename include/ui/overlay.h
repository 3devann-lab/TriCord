#ifndef OVERLAY_H
#define OVERLAY_H

#include <3ds.h>
#include <string>

namespace UI {

// A full-screen panel a screen hands both displays and the input to, such as the
// file browser or the video player. The owner drops it once finished() is true.
class Overlay {
  public:
	virtual ~Overlay() = default;

	virtual void update(u32 kDown, u32 kHeld) = 0;
	virtual void renderTop() = 0;
	virtual void renderBottom() = 0;
	virtual bool isAnimating() const { return false; }

	bool finished() const { return done; }

  protected:
	void finish() { done = true; }

  private:
	bool done = false;
};

struct KeyboardInput {
	bool confirmed = false;
	std::string text;
};

// The system keyboard, blocking until it closes.
KeyboardInput runKeyboard(const std::string &hint, const std::string &initialText, bool multiline, int maxLength,
                          const std::string &confirmLabel);

// The shared look of an overlay's top-screen header bar.
void drawOverlayHeader(const std::string &title, const std::string &right = "");
// A thin progress bar; fraction is clamped to 0..1.
void drawProgressBar(float x, float y, float w, float h, float fraction, float z = 0.6f);
// A key hint line at the bottom of the bottom screen.
void drawKeyHints(const std::string &hints);
// "1:05" or "1:02:05".
std::string formatDuration(double seconds);

} // namespace UI

#endif // OVERLAY_H
