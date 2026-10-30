// disc_extract.h - unpacks a Wii disc image (ISO, or RVZ using Zstandard or no compression) into the
// sys/ and files/ tree the runtime reads. Kept apart from src/ so it can be built without the game:
// the Android launcher loads it on its own.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace disc {

// Reads `size` bytes at `offset` of the image; false when they aren't all there.
using ReadAt = std::function<bool(uint64_t offset, void* out, size_t size)>;
// Bytes written so far and in all; false stops the extraction.
using Progress = std::function<bool(uint64_t done, uint64_t total)>;

struct Result {
    enum class Status { Ok, Cancelled, Failed };
    Status status = Status::Ok;
    std::string message;  // Failed: why, in words for the player
};

// The first 8 bytes of the disc header: game ID (6) and revision. False with `message` set when the
// file isn't an image this can read.
bool ReadHeader(const ReadAt& read, uint8_t header[8], std::string& message);

// Unpacks the game partition into `outDir`, which must exist. Fails before writing anything if the
// game needs more than `freeBytes`.
Result Extract(const ReadAt& read, const std::string& outDir, uint64_t freeBytes, const Progress& progress);

}  // namespace disc
