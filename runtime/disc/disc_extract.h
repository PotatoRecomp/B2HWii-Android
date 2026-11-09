// disc_extract.h - reads a Wii disc image (ISO, or RVZ using Zstandard or no compression): unpacks it
// into the sys/ and files/ tree the runtime reads (the launcher's Extract), or serves the game's
// files straight from the image (Read: GameDisc, used by the runtime). Kept apart from src/ so it
// can be built without the game: the Android launcher loads it on its own.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

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

// One file of the game partition: "sys/boot.bin", "files/linkData.afs"..., and where its bytes are
// in the partition's data.
struct DiscFile {
    std::string path;
    uint64_t offset = 0;
    uint64_t size = 0;
};

// The first 8 bytes of the disc header: game ID (6) and revision. False with `message` set when the
// file isn't an image this can read.
bool ReadHeader(const ReadAt& read, uint8_t header[8], std::string& message);

// Unpacks the game partition into `outDir`, which must exist. Fails before writing anything if the
// game needs more than `freeBytes`.
Result Extract(const ReadAt& read, const std::string& outDir, uint64_t freeBytes, const Progress& progress);

// The game partition of an image, read in place. Recently decoded pieces are kept (a few MiB), so
// reads that hop between files don't decode the same piece again.
class GameDisc {
public:
    virtual ~GameDisc() = default;
    // Null with `message` set when the image can't be read.
    static std::unique_ptr<GameDisc> Open(ReadAt read, std::string& message);
    // sys/ first, then files/ in disc order.
    virtual const std::vector<DiscFile>& Files() const = 0;
    // `size` bytes at `offset` of the partition's data. Safe from any thread; false (with why in
    // `error`) when the image can't be read there, as when the file was moved or damaged.
    virtual bool Read(uint64_t offset, void* out, size_t size, std::string* error = nullptr) = 0;
};

// The image file at `path`, kept open while the returned function (or a copy) lives. Empty when it
// can't be opened. /proc/self/fd/N (IsOpenedFile) is a file the app already opened for the game.
ReadAt OpenFile(const std::filesystem::path& path);

// /proc/self/fd/N: on Android the app opens the file the player chose (maybe only through the
// system picker's grant) and hands the game its descriptor, which is used as is, not opened again.
bool IsOpenedFile(const std::filesystem::path& path);

}  // namespace disc
