#pragma once

#include "ui/overlay.h"
#include <3ds.h>
#include <citro2d.h>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace UI {

// Draw on the touch screen and send it as a picture. The bottom screen is the canvas; the
// top screen shows the colour, brush and controls.
class DoodlePad : public Overlay {
  public:
	// Called with the saved PNG; the pad stays open underneath until close() is called,
	// so backing out of the send screen returns to the drawing.
	using SendCallback = std::function<void(const std::string &path, DoodlePad *pad)>;

	explicit DoodlePad(SendCallback onSend);
	~DoodlePad() override;

	void update(u32 kDown, u32 kHeld) override;
	void renderTop() override;
	void renderBottom() override;
	bool isAnimating() const override { return dirty || confirmTimer > 0; }

	void close() { finish(); }

	static constexpr int WIDTH = 320;
	static constexpr int HEIGHT = 240;

  private:
	void stamp(int cx, int cy, int radius, uint16_t color);
	void strokeTo(int x, int y);
	void pushUndo();
	void upload();
	bool save(std::string &path) const;
	uint16_t currentColor() const;

	SendCallback onSend;
	std::vector<uint16_t> canvas;
	std::deque<std::vector<uint16_t>> undo;
	C3D_Tex tex;
	bool texReady = false;
	bool dirty = true;
	bool drawing = false;
	bool drewAnything = false;
	int lastX = 0;
	int lastY = 0;
	int colorIndex = 0;
	int sizeIndex = 1;
	bool eraser = false;
	// Clear and quit with a drawing ask for a second press.
	int confirmTimer = 0;
	u32 confirmKey = 0;
};

} // namespace UI
