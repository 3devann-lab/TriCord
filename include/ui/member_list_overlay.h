#pragma once

#include "discord/types.h"
#include "ui/overlay.h"
#include <3ds.h>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UI {

// Who is in a server channel (grouped by hoisted role, online first, as Discord sends it)
// or in a DM, with their status. A opens a DM, Y adds them as a friend.
class MemberListOverlay : public Overlay {
  public:
	using OpenDm = std::function<void(const std::string &userId)>;

	MemberListOverlay(const std::string &guildId, const std::string &channelId, const std::string &channelName,
	                  OpenDm openDm);
	~MemberListOverlay() override;

	void update(u32 kDown, u32 kHeld) override;
	void renderTop() override;
	void renderBottom() override;
	bool isAnimating() const override;

  private:
	struct Row {
		bool header = false;
		bool placeholder = false;
		std::string title;
		Discord::User user;
		std::string name;
		u32 nameColor = 0;
		std::vector<std::string> roleNames;
		std::vector<u32> roleColors;
	};

	void rebuild();
	float rowHeight(const Row &row) const { return row.header ? 22.0f : 32.0f; }
	void moveSelection(int step);
	void keepSelectionVisible();

	std::string guildId;
	std::string channelId;
	std::string channelName;
	OpenDm openDm;
	std::shared_ptr<bool> alive;
	bool isDm = false;

	std::vector<Row> rows;
	uint32_t seenVersion = UINT32_MAX;
	int onlineCount = 0;
	int memberCount = 0;
	int requestedUpTo = 99;
	int selected = -1;
	float scroll = 0.0f;
	float targetScroll = 0.0f;
	int repeatTimer = 0;
	int refreshTimer = 0;
	std::shared_ptr<std::atomic<int>> friendResult = std::make_shared<std::atomic<int>>(0);
	std::string toast;
	int toastFrames = 0;
};

} // namespace UI
