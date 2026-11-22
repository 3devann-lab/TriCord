#ifndef CHANNEL_EDITOR_H
#define CHANNEL_EDITOR_H

#include "discord/discord_client.h"
#include <3ds.h>
#include <memory>
#include <mutex>
#include <string>

namespace UI {

// A popup over the channel list that edits a channel's name, topic, slowmode and NSFW flag.
class ChannelEditor {
  public:
	bool isOpen() const { return open; }
	void show(const Discord::Channel &channel);
	void update(u32 kDown, u32 kHeld);
	void render();
	// True once after a save went through, so the list can be rebuilt.
	bool consumeSaved();

  private:
	struct Result {
		std::mutex mutex;
		bool done = false;
		bool ok = false;
		std::string message;
	};

	enum Row { NAME, TOPIC, SLOWMODE, NSFW, SAVE };

	bool open = false;
	bool saving = false;
	bool saved = false;
	bool textChannel = true;
	std::string channelId;
	Discord::DiscordClient::ChannelEdit edit;
	int row = 0;
	std::shared_ptr<Result> result;

	int rowCount() const;
	Row rowAt(int index) const;
	void stepSlowmode(int delta);
	void save();
};

} // namespace UI

#endif // CHANNEL_EDITOR_H
