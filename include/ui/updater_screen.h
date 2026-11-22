#ifndef UPDATER_SCREEN_H
#define UPDATER_SCREEN_H

#include "ui/screen_manager.h"

namespace UI {

// Settings > Check for Updates: compares this build with the newest TriCord.cia on GitHub,
// lists what changed, then downloads and installs it.
class UpdaterScreen : public Screen {
  public:
	void update() override;
	void renderTop(C3D_RenderTarget *target) override;
	void renderBottom(C3D_RenderTarget *target) override;
	void onEnter() override;
	bool hidesMenu() const override { return true; }
	bool isAnimating() const override;

  private:
	float spinner = 0.0f;
	bool confirmInstall = false;
};

} // namespace UI

#endif // UPDATER_SCREEN_H
