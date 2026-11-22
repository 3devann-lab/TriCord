#ifndef THEME_PRESETS_H
#define THEME_PRESETS_H

#include "core/config.h"
#include <string>
#include <vector>

namespace ThemePresets {

// Built-in themes after Dark (0) and Light (1), which Config keeps itself. Index 2 is the first one here.
int count();
std::string name(int index);
Theme build(int index);

} // namespace ThemePresets

#endif // THEME_PRESETS_H
