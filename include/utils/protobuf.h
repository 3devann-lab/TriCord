#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace Utils {
namespace Proto {

// Just enough of the protobuf wire format to walk Discord's settings protos.
class Reader {
  public:
	enum WireType : uint32_t { VARINT = 0, FIXED64 = 1, LENGTH = 2, FIXED32 = 5 };

	Reader(const uint8_t *data, size_t size) : pos(data), end(data + size) {}

	bool atEnd() const { return pos >= end; }
	bool failed() const { return error; }

	// Reads the next field's tag. False at the end of the message or on malformed input.
	bool next(uint32_t &field, uint32_t &wireType);

	bool readVarint(uint64_t &value);
	bool readBytes(const uint8_t *&data, size_t &size);
	bool readString(std::string &out);
	bool skip(uint32_t wireType);

  private:
	const uint8_t *pos;
	const uint8_t *end;
	bool error = false;
};

} // namespace Proto
} // namespace Utils
