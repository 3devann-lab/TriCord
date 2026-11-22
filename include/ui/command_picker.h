#ifndef COMMAND_PICKER_H
#define COMMAND_PICKER_H

#include "discord/types.h"
#include "ui/overlay.h"
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace UI {

// Lists the slash commands usable in a channel, fills in a command's options and runs it.
class CommandPicker : public Overlay {
  public:
	using RunCallback =
	    std::function<void(const Discord::ApplicationCommand &command, const std::vector<Discord::CommandValue> &)>;
	// People to offer for user options, e.g. recent authors in the channel: id and display name.
	using People = std::vector<std::pair<std::string, std::string>>;

	CommandPicker(const std::string &channelId, People people, RunCallback onRun);
	~CommandPicker() override;

	void update(u32 kDown, u32 kHeld) override;
	void renderTop() override;
	void renderBottom() override;
	bool isAnimating() const override;
	void setFilter(const std::string &text) { filter = text; }

  private:
	struct Shared;
	struct Choice {
		std::string label;
		std::string value;
		std::string detail;
	};
	enum class Mode { LIST, SUBCOMMAND, FORM, CHOICE };

	std::string channelId;
	std::string guildId;
	People people;
	RunCallback onRun;
	std::shared_ptr<Shared> shared;

	Mode mode = Mode::LIST;
	std::string filter;
	int cursor = 0;
	int scroll = 0;
	int repeatTimer = 0;
	u32 repeatKey = 0;

	Discord::ApplicationCommand command;
	// Subcommand group and subcommand picked so far, outermost first.
	std::vector<Discord::CommandOption> chain;
	std::vector<Discord::CommandOption> subOptions;
	std::vector<Discord::CommandOption> form;
	std::vector<std::string> values;
	std::vector<std::string> labels;
	std::vector<Choice> choices;
	int choiceRow = -1;

	std::vector<Discord::ApplicationCommand> filtered() const;
	void pickCommand(const Discord::ApplicationCommand &picked);
	void enterOptions(const std::vector<Discord::CommandOption> &options);
	void editRow(int row);
	void openChoices(int row);
	void run();
	int rowCount() const;
	void move(int delta, int count);
	static std::string typeLabel(int type);
	static constexpr int VISIBLE_ROWS = 8;
};

} // namespace UI

#endif // COMMAND_PICKER_H
