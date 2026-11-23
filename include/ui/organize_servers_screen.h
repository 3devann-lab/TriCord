#ifndef ORGANIZE_SERVERS_SCREEN_H
#define ORGANIZE_SERVERS_SCREEN_H

#include "discord/types.h"
#include "ui/screen_manager.h"
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace UI {

// Rearranges the server list: move servers and folders, put servers into folders or take
// them out, create, rename, recolour and remove folders. Saved to the Discord account.
class OrganizeServersScreen : public Screen {
  public:
	void onEnter() override;
	void update() override;
	void renderTop(C3D_RenderTarget *target) override;
	void renderBottom(C3D_RenderTarget *target) override;
	bool hidesMenu() const override { return true; }
	bool isAnimating() const override { return true; }

  private:
	// The list as a token stream: a folder is START, its servers, END. Moving a server one
	// step past START or END moves it into or out of that folder.
	enum class Kind { GUILD, FOLDER_START, FOLDER_END };
	struct Token {
		Kind kind;
		std::string guildId;
		// Folders only.
		std::string folderId;
		std::string name;
		int color = 0;
	};

	void load();
	std::vector<Discord::GuildFolder> toFolders() const;
	bool insideFolder(int index) const;
	int folderEndOf(int start) const;
	int folderStartOf(int index) const;
	void moveSelected(int dir);
	void step(int dir);
	void newFolder();
	void renameFolder(int start);
	void removeFolder(int start);
	void save();
	std::string guildName(const std::string &id) const;

	std::vector<Token> tokens;
	int selected = 0;
	bool holding = false;
	bool dirty = false;
	bool confirmLeave = false;
	float scroll = 0.0f;
	int repeatTimer = 0;
	std::shared_ptr<std::atomic<int>> saveState = std::make_shared<std::atomic<int>>(0);
	std::string status;
};

} // namespace UI

#endif // ORGANIZE_SERVERS_SCREEN_H
