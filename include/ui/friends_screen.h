#ifndef FRIENDS_SCREEN_H
#define FRIENDS_SCREEN_H

#include "discord/types.h"
#include "ui/screen_manager.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace UI {

// Friends and friend requests: message, add, accept, decline and remove.
class FriendsScreen : public Screen {
  public:
	FriendsScreen();

	void onEnter() override;
	void update() override;
	void renderTop(C3D_RenderTarget *target) override;
	void renderBottom(C3D_RenderTarget *target) override;
	bool isAnimating() const override;

  private:
	struct Shared {
		std::mutex mutex;
		bool loading = false;
		bool failed = false;
		std::vector<Discord::Relationship> list;
		std::string toast;
		std::string openChannelId;
	};

	enum class Tab { FRIENDS, PENDING, BLOCKED };

	std::shared_ptr<Shared> shared;
	Tab tab = Tab::FRIENDS;
	int selected = 0;
	int scroll = 0;
	bool confirming = false;
	int repeatTimer = 0;
	u32 repeatKey = 0;

	void refresh();
	static void reload(const std::shared_ptr<Shared> &state);
	std::vector<Discord::Relationship> visible() const;
	void addFriendPrompt();
	void act(bool primary);
	static std::string displayName(const Discord::Relationship &rel);
	static constexpr int VISIBLE_ROWS = 5;
};

} // namespace UI

#endif // FRIENDS_SCREEN_H
