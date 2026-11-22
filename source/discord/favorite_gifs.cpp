#include "discord/favorite_gifs.h"
#include "utils/protobuf.h"
#include <algorithm>

namespace Discord {

namespace {

using Utils::Proto::Reader;

// FavoriteGIF { GIFType format = 1; string src = 2; uint32 width = 3; uint32 height = 4; uint32 order = 5; }
void readGif(const uint8_t *data, size_t size, FavoriteGif &gif) {
	Reader value(data, size);
	uint32_t field = 0;
	uint32_t wire = 0;
	uint64_t number = 0;
	while (value.next(field, wire)) {
		if (field == 2 && wire == Reader::LENGTH) {
			value.readString(gif.src);
		} else if (wire == Reader::VARINT && value.readVarint(number)) {
			if (field == 1) {
				gif.format = (int)number;
			} else if (field == 3) {
				gif.width = (int)number;
			} else if (field == 4) {
				gif.height = (int)number;
			} else if (field == 5) {
				gif.order = (int)number;
			}
		} else if (!value.skip(wire)) {
			break;
		}
	}
}

// FavoriteGIFs { map<string, FavoriteGIF> gifs = 1; bool hide_tooltip = 2; }
void readFavorites(const uint8_t *data, size_t size, std::vector<FavoriteGif> &out) {
	Reader favorites(data, size);
	uint32_t field = 0;
	uint32_t wire = 0;
	while (favorites.next(field, wire)) {
		if (field != 1 || wire != Reader::LENGTH) {
			if (!favorites.skip(wire)) {
				break;
			}
			continue;
		}
		const uint8_t *entryData = nullptr;
		size_t entrySize = 0;
		if (!favorites.readBytes(entryData, entrySize)) {
			break;
		}
		// A map entry is a message with the key as field 1 and the value as field 2.
		FavoriteGif gif;
		Reader entry(entryData, entrySize);
		while (entry.next(field, wire)) {
			if (field == 1 && wire == Reader::LENGTH) {
				entry.readString(gif.url);
			} else if (field == 2 && wire == Reader::LENGTH) {
				const uint8_t *valueData = nullptr;
				size_t valueSize = 0;
				if (!entry.readBytes(valueData, valueSize)) {
					break;
				}
				readGif(valueData, valueSize, gif);
			} else if (!entry.skip(wire)) {
				break;
			}
		}
		if (!gif.url.empty()) {
			out.push_back(std::move(gif));
		}
	}
}

} // namespace

std::vector<FavoriteGif> parseFavoriteGifs(const uint8_t *data, size_t size) {
	std::vector<FavoriteGif> gifs;
	Reader settings(data, size);
	uint32_t field = 0;
	uint32_t wire = 0;
	while (settings.next(field, wire)) {
		if (field == 2 && wire == Reader::LENGTH) {
			const uint8_t *favData = nullptr;
			size_t favSize = 0;
			if (!settings.readBytes(favData, favSize)) {
				break;
			}
			readFavorites(favData, favSize, gifs);
		} else if (!settings.skip(wire)) {
			break;
		}
	}
	std::stable_sort(gifs.begin(), gifs.end(),
	                 [](const FavoriteGif &a, const FavoriteGif &b) { return a.order > b.order; });
	return gifs;
}

} // namespace Discord
