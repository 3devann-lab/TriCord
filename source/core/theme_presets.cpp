#include "core/theme_presets.h"
#include "utils/color_utils.h"

namespace ThemePresets {

namespace {

struct Palette {
	const char *name;
	const char *author;
	bool light;
	const char *bg;
	const char *bgDark;
	const char *bgLight;
	const char *accent;
	const char *separator;
	const char *text;
	const char *muted;
	const char *link;
	const char *embed;
	const char *reaction;
	const char *reactionMe;
	const char *input;
	const char *success;
	const char *error;
};

// clang-format off
const Palette PALETTES[] = {
	{"AMOLED Black", "TriCord", false, "#000000", "#000000", "#1A1A1A", "#5865F2", "#3A3A3A", "#FFFFFF", "#8E9297", "#00A8FC", "#0D0D0D", "#1A1A1A", "#2B3A67", "#111111", "#3BA55D", "#ED4245"},
	{"Midnight", "TriCord", false, "#1E2130", "#171A26", "#2A2F45", "#7AA2F7", "#3B4261", "#E6E9F2", "#8C93AD", "#7DCFFF", "#171A26", "#2A2F45", "#34446E", "#141722", "#73DACA", "#F7768E"},
	{"Dracula", "TriCord", false, "#282A36", "#21222C", "#44475A", "#BD93F9", "#6272A4", "#F8F8F2", "#A7ABBE", "#8BE9FD", "#21222C", "#44475A", "#5A4B82", "#1E1F29", "#50FA7B", "#FF5555"},
	{"Nord", "TriCord", false, "#2E3440", "#272C36", "#3B4252", "#88C0D0", "#4C566A", "#ECEFF4", "#9AA5B8", "#81A1C1", "#272C36", "#3B4252", "#3F5A6B", "#242933", "#A3BE8C", "#BF616A"},
	{"Solarized Dark", "TriCord", false, "#002B36", "#00212B", "#073642", "#268BD2", "#586E75", "#EEE8D5", "#93A1A1", "#2AA198", "#00212B", "#073642", "#0E4D6B", "#001E26", "#859900", "#DC322F"},
	{"Solarized Light", "TriCord", true, "#FDF6E3", "#EEE8D5", "#E4DDC8", "#268BD2", "#93A1A1", "#073642", "#657B83", "#2AA198", "#F5EFDC", "#E4DDC8", "#CFE3F2", "#EEE8D5", "#859900", "#DC322F"},
	{"Gruvbox", "TriCord", false, "#282828", "#1D2021", "#3C3836", "#FE8019", "#665C54", "#EBDBB2", "#A89984", "#83A598", "#1D2021", "#3C3836", "#5A4632", "#1A1A1A", "#B8BB26", "#FB4934"},
	{"Catppuccin Mocha", "TriCord", false, "#1E1E2E", "#181825", "#313244", "#CBA6F7", "#45475A", "#CDD6F4", "#9399B2", "#89B4FA", "#181825", "#313244", "#4A3F6B", "#11111B", "#A6E3A1", "#F38BA8"},
	{"Catppuccin Latte", "TriCord", true, "#EFF1F5", "#E6E9EF", "#DCE0E8", "#8839EF", "#9CA0B0", "#4C4F69", "#6C6F85", "#1E66F5", "#E6E9EF", "#DCE0E8", "#E3D4FB", "#DCE0E8", "#40A02B", "#D20F39"},
	{"Tokyo Night", "TriCord", false, "#1A1B26", "#16161E", "#292E42", "#7AA2F7", "#3B4261", "#C0CAF5", "#787C99", "#7DCFFF", "#16161E", "#292E42", "#34406B", "#13131A", "#9ECE6A", "#F7768E"},
	{"Monokai", "TriCord", false, "#272822", "#1E1F1C", "#3E3D32", "#F92672", "#75715E", "#F8F8F2", "#A59F85", "#66D9EF", "#1E1F1C", "#3E3D32", "#5E2A40", "#1A1B17", "#A6E22E", "#FD5F5F"},
	{"One Dark", "TriCord", false, "#282C34", "#21252B", "#3A3F4B", "#61AFEF", "#4B5263", "#DCDFE4", "#9DA5B4", "#56B6C2", "#21252B", "#3A3F4B", "#2F4A66", "#1E2227", "#98C379", "#E06C75"},
	{"Rose Pine", "TriCord", false, "#191724", "#13111E", "#26233A", "#EBBCBA", "#403D52", "#E0DEF4", "#908CAA", "#9CCFD8", "#1F1D2E", "#26233A", "#4A3A4E", "#13111E", "#9CCFD8", "#EB6F92"},
	{"Tokyo Day", "TriCord", true, "#E1E2E7", "#D5D6DB", "#C4C8DA", "#2E7DE9", "#A8AECB", "#3760BF", "#6172B0", "#007197", "#D8D9DE", "#C4C8DA", "#C2D4F5", "#D0D2DA", "#587539", "#F52A65"},
	{"Sakura", "TriCord", true, "#FFF5F8", "#FCE8EE", "#F7D6E0", "#E75480", "#D9A5B5", "#3D2B33", "#8C6B78", "#C2185B", "#FDEFF3", "#F7D6E0", "#F9C6D6", "#F7DDE5", "#4CAF50", "#D32F2F"},
	{"Ocean", "TriCord", false, "#0F2537", "#0B1C2A", "#183850", "#00B4D8", "#2C5470", "#E0F4FF", "#8BB3C9", "#48CAE4", "#0B1C2A", "#183850", "#0E4D66", "#08161F", "#2EC4B6", "#FF6B6B"},
	{"Forest", "TriCord", false, "#1E2A22", "#17201A", "#2C3D31", "#6FCF97", "#3F5546", "#E8F3EC", "#9DB5A5", "#8FD9A8", "#17201A", "#2C3D31", "#2F5A3E", "#131A15", "#6FCF97", "#EB5757"},
	{"Sunset", "TriCord", false, "#2B1D2E", "#221725", "#3D2A40", "#FF7E5F", "#5A3F5C", "#FFEDE6", "#C29FA8", "#FEB47B", "#221725", "#3D2A40", "#6B3A3A", "#1C121E", "#7BD389", "#FF5470"},
	{"Mint", "TriCord", true, "#F2FBF7", "#E3F5EC", "#D2EDE0", "#20A67A", "#9CC9B5", "#1F3A30", "#5E7D71", "#138A62", "#EAF8F1", "#D2EDE0", "#C2EBD9", "#DDF1E7", "#20A67A", "#D9534F"},
	{"Lavender", "TriCord", false, "#2A2438", "#221D2E", "#3A3250", "#B39DDB", "#524670", "#F1ECFA", "#ABA1C2", "#CE93D8", "#221D2E", "#3A3250", "#4E3F75", "#1C1826", "#81C784", "#EF5350"},
	{"Coffee", "TriCord", false, "#2C2420", "#231D1A", "#3D332D", "#C8A27C", "#5C4D44", "#F2E8DF", "#B3A396", "#E0B98C", "#231D1A", "#3D332D", "#5E4A36", "#1D1815", "#8DB580", "#E06F5C"},
	{"Retro Green", "TriCord", true, "#9BBC0F", "#8BAC0F", "#7FA00E", "#306230", "#306230", "#0F380F", "#306230", "#0F380F", "#8BAC0F", "#7FA00E", "#6B8F0E", "#8BAC0F", "#306230", "#0F380F"},
	{"Crimson", "TriCord", false, "#2A1A1C", "#211416", "#3D2529", "#E3202E", "#5A3A3F", "#FBEDEE", "#B99A9E", "#FF8A80", "#211416", "#3D2529", "#6B2A31", "#1B1012", "#66BB6A", "#FF5252"},
	{"Cyberpunk", "TriCord", false, "#0D0221", "#090118", "#1F0A3D", "#F706CF", "#3A1C6B", "#F5F5FF", "#9D8FC4", "#00F0FF", "#090118", "#1F0A3D", "#4B0A5E", "#070114", "#00FF9F", "#FF2A6D"},
	{"Sand", "TriCord", true, "#FAF6EE", "#F1EADB", "#E6DCC6", "#C47F3A", "#BFAF92", "#3B3024", "#7A6A55", "#A35D1F", "#F5EFE2", "#E6DCC6", "#F0D9BC", "#EFE6D3", "#5E9E4D", "#C9483C"},
	{"High Contrast", "TriCord", false, "#000000", "#000000", "#333333", "#FFD400", "#FFFFFF", "#FFFFFF", "#E0E0E0", "#00E5FF", "#111111", "#333333", "#5C4D00", "#1A1A1A", "#00E676", "#FF1744"},
};
// clang-format on

constexpr int FIRST_INDEX = 2;
constexpr int PALETTE_COUNT = sizeof(PALETTES) / sizeof(PALETTES[0]);

} // namespace

int count() { return PALETTE_COUNT; }

std::string name(int index) {
	const int i = index - FIRST_INDEX;
	return (i >= 0 && i < PALETTE_COUNT) ? PALETTES[i].name : "";
}

Theme build(int index) {
	const int i = index - FIRST_INDEX;
	const Palette &p = PALETTES[(i >= 0 && i < PALETTE_COUNT) ? i : 0];
	auto c = [](const char *hex) { return Utils::Color::hexToColor(hex); };

	Theme t;
	t.name = p.name;
	t.author = p.author;
	t.light = p.light;
	t.bg = c(p.bg);
	t.bg_dark = c(p.bgDark);
	t.bg_light = c(p.bgLight);
	t.accent = c(p.accent);
	t.selection = c(p.accent);
	t.separator = c(p.separator);
	t.header_border = p.light ? 0x1E000000 : 0x1EFFFFFF;
	t.overlay = 0x96000000;
	t.pure_white = 0xFFFFFFFF;
	t.text = c(p.text);
	t.text_muted = c(p.muted);
	t.link = c(p.link);
	t.embed_bg = c(p.embed);
	t.embed_media_bg = c(p.bg);
	t.reaction_bg = c(p.reaction);
	t.reaction_me_bg = c(p.reactionMe);
	t.input_bg = c(p.input);
	t.success = c(p.success);
	t.error = c(p.error);
	return t;
}

} // namespace ThemePresets
