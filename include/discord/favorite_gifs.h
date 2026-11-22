#ifndef FAVORITE_GIFS_H
#define FAVORITE_GIFS_H

#include "discord/types.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Discord {

// Reads FrecencyUserSettings.favorite_gifs out of the settings proto, most recently added first.
std::vector<FavoriteGif> parseFavoriteGifs(const uint8_t *data, size_t size);

} // namespace Discord

#endif // FAVORITE_GIFS_H
