#ifndef GIF_PICKER_H
#define GIF_PICKER_H

#include "discord/types.h"
#include "ui/overlay.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UI {

// The user's favorite GIFs, picked from a grid of animated previews.
class GifPicker : public Overlay {
  public:
	using SendCallback = std::function<void(const Discord::FavoriteGif &gif)>;
	using PlayCallback = std::function<void(const Discord::FavoriteGif &gif)>;

	GifPicker(SendCallback onSend, PlayCallback onPlay);
	~GifPicker() override;

	void update(u32 kDown, u32 kHeld) override;
	void renderTop() override;
	void renderBottom() override;
	bool isAnimating() const override { return true; }

  private:
	struct State;

	SendCallback onSend;
	PlayCallback onPlay;
	std::shared_ptr<State> state;
	int selected = 0;
	int firstRow = 0;
	int repeatTimer = 0;
	u32 repeatKey = 0;

	void refresh();
	void move(int delta);
	std::vector<Discord::FavoriteGif> gifs() const;

	static constexpr int COLUMNS = 3;
	static constexpr int ROWS = 3;
};

} // namespace UI

#endif // GIF_PICKER_H
