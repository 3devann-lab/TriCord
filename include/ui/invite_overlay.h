#pragma once

#include "discord/types.h"
#include "ui/overlay.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace UI {

// Finds a discord.gg / discord.com/invite code in text; empty when there is none.
std::string findInviteCode(const std::string &text);

// Either previews an invite someone posted (with Join / Open), or creates one for a channel.
class InviteOverlay : public Overlay {
  public:
	// Opens a channel the user can now see (after joining or when already a member).
	using OpenChannel = std::function<void(const std::string &channelId)>;

	static std::unique_ptr<InviteOverlay> preview(const std::string &code, OpenChannel open);
	static std::unique_ptr<InviteOverlay> create(const std::string &channelId, const std::string &channelName);
	~InviteOverlay() override;

	void update(u32 kDown, u32 kHeld) override;
	void renderTop() override;
	void renderBottom() override;
	bool isAnimating() const override;

  private:
	enum class State { LOADING, READY, FAILED, JOINING, JOINED, CHOOSING, CREATING, CREATED };

	InviteOverlay() : alive(std::make_shared<bool>(true)) {}
	void makeQr(const std::string &text);
	void drawQr(float x, float y, float size) const;
	bool alreadyMember() const;

	bool creating = false;
	std::shared_ptr<bool> alive;
	mutable std::recursive_mutex mutex;
	State state = State::LOADING;
	std::string error;
	Discord::InviteInfo info;
	OpenChannel open;

	std::string channelId;
	std::string channelName;
	int ageIndex = 5;
	int usesIndex = 0;
	std::string createdCode;
	std::vector<uint8_t> qr;
	int qrSize = 0;
	float spinner = 0.0f;
};

} // namespace UI
