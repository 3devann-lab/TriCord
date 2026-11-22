#pragma once

#include "discord/types.h"
#include "ui/overlay.h"
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace UI {

// A channel's pinned messages, or message search results, with jump-to.
class MessageListOverlay : public Overlay {
  public:
	enum class Mode { PINS, SEARCH };
	// Called with the chosen message; the overlay closes itself afterwards.
	using JumpCallback = std::function<void(const Discord::Message &)>;

	MessageListOverlay(Mode mode, const std::string &channelId, const std::string &guildId,
	                   const std::string &channelName, JumpCallback onJump);
	~MessageListOverlay() override;

	void update(u32 kDown, u32 kHeld) override;
	void renderTop() override;
	void renderBottom() override;
	bool isAnimating() const override;

  private:
	struct Row {
		Discord::Message message;
		std::string author;
		std::string when;
		std::string where;
		std::vector<std::string> lines;
	};

	void requestPins();
	// Asks for a query; false when the keyboard was cancelled.
	bool askQuery();
	void requestSearch(bool nextPage);
	void onResults(bool ok, int total, const std::vector<Discord::Message> &found, const std::string &error,
	               bool append);
	Row makeRow(const Discord::Message &msg) const;
	float rowHeight(const Row &row) const;
	void keepSelectionVisible();

	Mode mode;
	std::string channelId;
	std::string guildId;
	std::string channelName;
	JumpCallback onJump;
	std::shared_ptr<bool> alive;

	mutable std::recursive_mutex mutex;
	std::vector<Row> rows;
	int totalResults = 0;
	bool loading = false;
	bool failed = false;
	std::string errorHint;
	std::string query;
	bool wholeServer = false;
	int retryFrames = 0;
	int retries = 0;

	int selected = 0;
	float scroll = 0.0f;
	float targetScroll = 0.0f;
	int repeatTimer = 0;
	float spinner = 0.0f;
};

} // namespace UI
