// disc_extract.cpp - see disc_extract.h. The RVZ layout and its junk generator follow Dolphin's
// docs/WiaAndRvz.md and DiscIO/LaggedFibonacciGenerator. Everything read from the image is untrusted:
// sizes are checked before use and file names can't leave the output folder.
#include "disc_extract.h"

#include <zstd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

namespace disc {
namespace {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

constexpr u64 kBlockTotal = 0x8000;
constexpr u64 kBlockData = 0x7C00;
constexpr u64 kBlockHeader = 0x400;
constexpr u64 kBlocksPerGroup = 64;
constexpr u64 kGroupData = kBlocksPerGroup * kBlockData;

constexpr u32 kWiiMagic = 0x5D1C9EA3;
constexpr u32 kGameCubeMagic = 0xC2339F3D;
constexpr const char* kUnreadable = "Couldn't read all of that file. If it is still copying, wait for it to finish, then try again.";
constexpr const char* kDamaged = "That disc image looks damaged. Make the ISO or RVZ again from your disc.";

struct Failure {
    std::string message;
};

[[noreturn]] void Fail(std::string message) { throw Failure{std::move(message)}; }

u16 Be16(const u8* p) { return static_cast<u16>(p[0] << 8 | p[1]); }
u32 Be32(const u8* p) {
    return static_cast<u32>(p[0]) << 24 | static_cast<u32>(p[1]) << 16 | static_cast<u32>(p[2]) << 8 | p[3];
}
u64 Be64(const u8* p) { return static_cast<u64>(Be32(p)) << 32 | Be32(p + 4); }

void ReadExact(const ReadAt& read, u64 offset, void* out, size_t size) {
    if (size != 0 && !read(offset, out, size)) Fail(kUnreadable);
}

// --- AES-128 CBC decryption, for ISOs ----------------------------------------------------------

class Aes128 {
public:
    explicit Aes128(const u8 key[16]) {
        BuildTables();
        u32 w[44];
        for (int i = 0; i < 4; ++i) w[i] = Be32(key + i * 4);
        u32 rcon = 0x01000000;
        for (int i = 4; i < 44; ++i) {
            u32 t = w[i - 1];
            if (i % 4 == 0) {
                t = (t << 8 | t >> 24);
                t = static_cast<u32>(tables().sbox[t >> 24]) << 24 | static_cast<u32>(tables().sbox[t >> 16 & 255]) << 16 |
                    static_cast<u32>(tables().sbox[t >> 8 & 255]) << 8 | tables().sbox[t & 255];
                t ^= rcon;
                rcon = (rcon << 1) ^ ((rcon & 0x80000000) ? 0x1B000000 : 0);
            }
            w[i] = w[i - 4] ^ t;
        }
        // The equivalent inverse cipher: round keys reversed, the middle ones through InvMixColumns.
        for (int round = 0; round <= 10; ++round) {
            for (int c = 0; c < 4; ++c) {
                u32 k = w[(10 - round) * 4 + c];
                if (round != 0 && round != 10) {
                    const Tables& t = tables();
                    k = t.td[t.sbox[k >> 24]] ^ Ror(t.td[t.sbox[k >> 16 & 255]], 8) ^
                        Ror(t.td[t.sbox[k >> 8 & 255]], 16) ^ Ror(t.td[t.sbox[k & 255]], 24);
                }
                keys_[round * 4 + c] = k;
            }
        }
    }

    // `size` is a multiple of 16; `iv` is 16 bytes. Works in place.
    void DecryptCbc(const u8* iv, const u8* in, u8* out, size_t size) const {
        u8 previous[16], next[16];
        std::memcpy(previous, iv, 16);
        for (size_t i = 0; i < size; i += 16) {
            std::memcpy(next, in + i, 16);
            DecryptBlock(next, out + i);
            for (int b = 0; b < 16; ++b) out[i + b] ^= previous[b];
            std::memcpy(previous, next, 16);
        }
    }

private:
    struct Tables {
        u8 sbox[256];
        u8 inverse[256];
        u32 td[256];
    };

    static u32 Ror(u32 v, int n) { return v >> n | v << (32 - n); }
    static Tables& tables() {
        static Tables t;
        return t;
    }
    static u8 Mul(u8 a, u8 b) {
        u8 r = 0;
        while (b != 0) {
            if (b & 1) r ^= a;
            a = static_cast<u8>((a << 1) ^ ((a & 0x80) ? 0x1B : 0));
            b >>= 1;
        }
        return r;
    }
    static void BuildTables() {
        static const bool built = [] {
            Tables& t = tables();
            u8 p = 1, q = 1;
            do {
                p = static_cast<u8>(p ^ (p << 1) ^ ((p & 0x80) ? 0x1B : 0));
                q ^= static_cast<u8>(q << 1);
                q ^= static_cast<u8>(q << 2);
                q ^= static_cast<u8>(q << 4);
                if (q & 0x80) q ^= 0x09;
                const auto rotl = [](u8 v, int n) { return static_cast<u8>(v << n | v >> (8 - n)); };
                const u8 s = static_cast<u8>(q ^ rotl(q, 1) ^ rotl(q, 2) ^ rotl(q, 3) ^ rotl(q, 4) ^ 0x63);
                t.sbox[p] = s;
            } while (p != 1);
            t.sbox[0] = 0x63;
            for (int i = 0; i < 256; ++i) t.inverse[t.sbox[i]] = static_cast<u8>(i);
            for (int i = 0; i < 256; ++i) {
                const u8 x = t.inverse[i];
                t.td[i] = static_cast<u32>(Mul(x, 0x0E)) << 24 | static_cast<u32>(Mul(x, 0x09)) << 16 |
                          static_cast<u32>(Mul(x, 0x0D)) << 8 | Mul(x, 0x0B);
            }
            return true;
        }();
        (void)built;
    }

    void DecryptBlock(const u8* in, u8* out) const {
        const Tables& t = tables();
        u32 s0 = Be32(in) ^ keys_[0], s1 = Be32(in + 4) ^ keys_[1], s2 = Be32(in + 8) ^ keys_[2], s3 = Be32(in + 12) ^ keys_[3];
        const auto mix = [&t](u32 a, u32 b, u32 c, u32 d) {
            return t.td[a >> 24] ^ Ror(t.td[b >> 16 & 255], 8) ^ Ror(t.td[c >> 8 & 255], 16) ^ Ror(t.td[d & 255], 24);
        };
        for (int round = 1; round < 10; ++round) {
            const u32* k = keys_ + round * 4;
            const u32 t0 = mix(s0, s3, s2, s1) ^ k[0];
            const u32 t1 = mix(s1, s0, s3, s2) ^ k[1];
            const u32 t2 = mix(s2, s1, s0, s3) ^ k[2];
            const u32 t3 = mix(s3, s2, s1, s0) ^ k[3];
            s0 = t0, s1 = t1, s2 = t2, s3 = t3;
        }
        const auto last = [&t](u32 a, u32 b, u32 c, u32 d, u32 k, u8* o) {
            const u32 v = (static_cast<u32>(t.inverse[a >> 24]) << 24 | static_cast<u32>(t.inverse[b >> 16 & 255]) << 16 |
                           static_cast<u32>(t.inverse[c >> 8 & 255]) << 8 | t.inverse[d & 255]) ^ k;
            o[0] = static_cast<u8>(v >> 24), o[1] = static_cast<u8>(v >> 16), o[2] = static_cast<u8>(v >> 8), o[3] = static_cast<u8>(v);
        };
        const u32* k = keys_ + 40;
        last(s0, s3, s2, s1, k[0], out);
        last(s1, s0, s3, s2, k[1], out + 4);
        last(s2, s1, s0, s3, k[2], out + 8);
        last(s3, s2, s1, s0, k[3], out + 12);
    }

    u32 keys_[44];
};

// --- the two kinds of image --------------------------------------------------------------------

class Image {
public:
    virtual ~Image() = default;
    // The disc as pressed, outside the partitions' data.
    virtual void ReadRaw(u64 offset, u8* out, size_t size) = 0;
    // Chooses the partition whose header is at `partition`; its data is then at ReadData's offset 0.
    virtual void SelectPartition(u64 partition, const u8* header) = 0;
    // The selected partition's data, decrypted and without the hash blocks.
    virtual void ReadData(u64 offset, u8* out, size_t size) = 0;
};

constexpr size_t kPartitionHeaderSize = 0x2C0;
u64 PartitionDataOffset(const u8* header) { return static_cast<u64>(Be32(header + 0x2B8)) << 2; }

class IsoImage final : public Image {
public:
    explicit IsoImage(const ReadAt& read) : read_(read) {}

    void ReadRaw(u64 offset, u8* out, size_t size) override { ReadExact(read_, offset, out, size); }

    void SelectPartition(u64 partition, const u8* header) override {
        // The ticket's title key, under the Wii's retail common key; the disc's ID check has already
        // ruled out everything that isn't this retail game.
        static constexpr u8 kCommonKey[16] = {0xEB, 0xE4, 0x2A, 0x22, 0x5E, 0x85, 0x93, 0xE4,
                                              0x48, 0xD9, 0xC5, 0x45, 0x73, 0x81, 0xAA, 0xF7};
        u8 iv[16] = {};
        std::memcpy(iv, header + 0x1DC, 8);
        u8 titleKey[16];
        Aes128(kCommonKey).DecryptCbc(iv, header + 0x1BF, titleKey, 16);
        aes_ = std::make_unique<Aes128>(titleKey);
        dataStart_ = partition + PartitionDataOffset(header);
        group_.resize(kBlocksPerGroup * kBlockTotal);
    }

    void ReadData(u64 offset, u8* out, size_t size) override {
        while (size != 0) {
            const u64 block = offset / kBlockData;
            const u64 groupIndex = block / kBlocksPerGroup;
            LoadGroup(groupIndex);
            const u64 inBlock = offset % kBlockData;
            const size_t take = static_cast<size_t>(std::min<u64>(size, kBlockData - inBlock));
            const u8* data = group_.data() + (block % kBlocksPerGroup) * kBlockTotal + kBlockHeader + inBlock;
            std::memcpy(out, data, take);
            offset += take;
            out += take;
            size -= take;
        }
    }

private:
    void LoadGroup(u64 index) {
        if (index == loaded_) return;
        loaded_ = ~u64{0};
        ReadExact(read_, dataStart_ + index * kBlocksPerGroup * kBlockTotal, group_.data(), group_.size());
        for (u64 b = 0; b < kBlocksPerGroup; ++b) {
            u8* block = group_.data() + b * kBlockTotal;
            // Each block's data is chained from an IV kept in its still-encrypted hash area.
            aes_->DecryptCbc(block + 0x3D0, block + kBlockHeader, block + kBlockHeader, kBlockData);
        }
        loaded_ = index;
    }

    const ReadAt& read_;
    std::unique_ptr<Aes128> aes_;
    u64 dataStart_ = 0;
    u64 loaded_ = ~u64{0};
    std::vector<u8> group_;
};

// The generator of the disc's padding, which RVZ stores as a seed instead of the data.
class LaggedFibonacci {
public:
    void SetSeed(const u8* seed) {
        position_ = 0;
        for (size_t i = 0; i < kSeedWords; ++i) buffer_[i] = Be32(seed + i * 4);
        for (size_t i = kSeedWords; i < kK; ++i) {
            buffer_[i] = (buffer_[i - 17] << 23) ^ (buffer_[i - 16] >> 9) ^ buffer_[i - 1];
        }
        for (u32& x : buffer_) {
            const u32 v = (x & 0xFF00FFFF) | ((x >> 2) & 0x00FF0000);
            x = (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
        }
        for (int i = 0; i < 4; ++i) Step();
    }
    void Skip(size_t count) {
        position_ += count;
        while (position_ >= kK * 4) {
            Step();
            position_ -= kK * 4;
        }
    }
    void Generate(u8* out, size_t count) {
        while (count != 0) {
            const size_t take = std::min(count, kK * 4 - position_);
            std::memcpy(out, reinterpret_cast<const u8*>(buffer_) + position_, take);
            position_ += take;
            out += take;
            count -= take;
            if (position_ == kK * 4) {
                Step();
                position_ = 0;
            }
        }
    }
    static constexpr size_t kSeedWords = 17;

private:
    static constexpr size_t kK = 521;
    static constexpr size_t kJ = 32;
    void Step() {
        for (size_t i = 0; i < kJ; ++i) buffer_[i] ^= buffer_[i + kK - kJ];
        for (size_t i = kJ; i < kK; ++i) buffer_[i] ^= buffer_[i - kJ];
    }
    u32 buffer_[kK];
    size_t position_ = 0;
};

class RvzImage final : public Image {
public:
    explicit RvzImage(const ReadAt& read) : read_(read), zstd_(ZSTD_createDCtx()) {
        u8 head[0x48];
        ReadExact(read_, 0, head, sizeof(head));
        const u32 discSize = Be32(head + 0x0C);
        if (discSize < 0xDC || discSize > 0x10000) Fail(kDamaged);
        u8 disc[0xDC];
        ReadExact(read_, sizeof(head), disc, sizeof(disc));
        if (Be32(disc) != 2) Fail("That disc image isn't a Wii game.");
        compression_ = Be32(disc + 0x04);
        if (compression_ != kCompressionNone && compression_ != kCompressionZstd) {
            Fail("That RVZ uses a compression this app can't read. In Dolphin, convert the game to RVZ with "
                 "Zstandard, or to ISO, then choose it again.");
        }
        chunkSize_ = Be32(disc + 0x0C);
        if (chunkSize_ < kBlockTotal || chunkSize_ % kBlockTotal != 0 || chunkSize_ > (u64{64} << 20)) Fail(kDamaged);
        std::memcpy(header_, disc + 0x10, sizeof(header_));

        const u32 partitionCount = Be32(disc + 0x90);
        const u32 partitionEntrySize = Be32(disc + 0x94);
        const u64 partitionsOffset = Be64(disc + 0x98);
        const u32 rawCount = Be32(disc + 0xB4);
        const u64 rawOffset = Be64(disc + 0xB8);
        const u32 rawSize = Be32(disc + 0xC0);
        const u32 groupCount = Be32(disc + 0xC4);
        const u64 groupsOffset = Be64(disc + 0xC8);
        const u32 groupsSize = Be32(disc + 0xD0);
        if (partitionCount > 64 || partitionEntrySize < 0x30 || partitionEntrySize > 0x1000 || rawCount > (1u << 20) ||
            groupCount > (1u << 26)) {
            Fail(kDamaged);
        }

        partitions_.resize(size_t{partitionCount} * partitionEntrySize);
        ReadExact(read_, partitionsOffset, partitions_.data(), partitions_.size());
        partitionEntrySize_ = partitionEntrySize;
        rawEntries_ = ReadTable(rawOffset, rawSize, size_t{rawCount} * kRawEntrySize);
        groups_ = ReadTable(groupsOffset, groupsSize, size_t{groupCount} * kGroupEntrySize);
    }
    ~RvzImage() override { ZSTD_freeDCtx(zstd_); }

    void ReadRaw(u64 offset, u8* out, size_t size) override {
        // The first 0x80 bytes live in the file's disc struct, not in the raw data.
        if (offset < sizeof(header_)) {
            const size_t take = static_cast<size_t>(std::min<u64>(size, sizeof(header_) - offset));
            std::memcpy(out, header_ + offset, take);
            offset += take;
            out += take;
            size -= take;
        }
        while (size != 0) {
            bool found = false;
            for (size_t i = 0; i * kRawEntrySize < rawEntries_.size() && !found; ++i) {
                const u8* e = rawEntries_.data() + i * kRawEntrySize;
                const u64 end = Be64(e) + Be64(e + 8);
                const u64 start = Be64(e) / kBlockTotal * kBlockTotal;
                if (offset < start || offset >= end) continue;
                found = true;
                const size_t took = ReadRegion(offset - start, out, std::min<u64>(size, end - offset), chunkSize_,
                                               Be32(e + 0x10), Be32(e + 0x14), 0);
                offset += took;
                out += took;
                size -= took;
            }
            if (!found) Fail(kDamaged);
        }
    }

    void SelectPartition(u64 partition, const u8* header) override {
        const u64 dataStart = partition + PartitionDataOffset(header);
        selected_ = nullptr;
        for (size_t i = 0; i * partitionEntrySize_ < partitions_.size(); ++i) {
            const u8* e = partitions_.data() + i * partitionEntrySize_;
            if (u64{Be32(e + 0x10)} * kBlockTotal == dataStart) selected_ = e;
        }
        if (selected_ == nullptr) Fail(kDamaged);
    }

    void ReadData(u64 offset, u8* out, size_t size) override {
        const u64 chunk = chunkSize_ * kBlockData / kBlockTotal;
        const u32 lists = static_cast<u32>(std::max<u64>(1, chunk / kGroupData));
        const u64 base = Be32(selected_ + 0x10);
        while (size != 0) {
            bool found = false;
            for (int d = 0; d < 2 && !found; ++d) {
                const u8* e = selected_ + 0x10 + d * 0x10;
                const u64 start = (Be32(e) - base) * kBlockData;
                const u64 end = start + u64{Be32(e + 4)} * kBlockData;
                if (Be32(e) < base || offset < start || offset >= end) continue;
                found = true;
                const size_t took = ReadRegion(offset - start, out, std::min<u64>(size, end - offset), chunk,
                                               Be32(e + 8), Be32(e + 12), lists);
                offset += took;
                out += took;
                size -= took;
            }
            if (!found) Fail(kDamaged);
        }
    }

private:
    static constexpr u32 kCompressionNone = 0;
    static constexpr u32 kCompressionZstd = 5;
    static constexpr size_t kRawEntrySize = 0x18;
    static constexpr size_t kGroupEntrySize = 12;
    // A block's 0x400 hash bytes are 52 SHA-1 slots at most, each 2 + 20 bytes when it differs.
    static constexpr size_t kMaxExceptionList = 2 + 22 * 52 * kBlocksPerGroup;

    size_t Inflate(const u8* in, size_t inSize, u8* out, size_t capacity) {
        ZSTD_DCtx_reset(zstd_, ZSTD_reset_session_only);
        ZSTD_inBuffer input{in, inSize, 0};
        ZSTD_outBuffer output{out, capacity, 0};
        while (true) {
            const size_t left = ZSTD_decompressStream(zstd_, &output, &input);
            if (ZSTD_isError(left)) Fail(kDamaged);
            if (left == 0) return output.pos;
            if (input.pos == input.size || output.pos == output.size) Fail(kDamaged);
        }
    }

    std::vector<u8> ReadTable(u64 offset, u32 storedSize, size_t size) {
        std::vector<u8> stored(storedSize);
        ReadExact(read_, offset, stored.data(), stored.size());
        if (compression_ == kCompressionNone) {
            if (stored.size() < size) Fail(kDamaged);
            stored.resize(size);
            return stored;
        }
        std::vector<u8> table(size + 1);
        if (Inflate(stored.data(), stored.size(), table.data(), table.size()) != size) Fail(kDamaged);
        table.resize(size);
        return table;
    }

    // Copies from `region` (its data split into `chunk`-sized groups) and returns how much.
    size_t ReadRegion(u64 offset, u8* out, u64 size, u64 chunk, u32 firstGroup, u32 groupCount, u32 lists) {
        const u64 index = offset / chunk;
        if (index >= groupCount) Fail(kDamaged);
        LoadGroup(u64{firstGroup} + index, chunk, index * chunk, lists);
        const u64 inChunk = offset % chunk;
        const size_t take = static_cast<size_t>(std::min(size, chunk - inChunk));
        std::memcpy(out, chunk_.data() + inChunk, take);
        return take;
    }

    void LoadGroup(u64 group, u64 chunk, u64 dataOffset, u32 lists) {
        if (group == loaded_) return;
        loaded_ = ~u64{0};
        if ((group + 1) * kGroupEntrySize > groups_.size()) Fail(kDamaged);
        const u8* e = groups_.data() + group * kGroupEntrySize;
        const u64 fileOffset = u64{Be32(e)} << 2;
        const u32 stored = Be32(e + 4);
        const bool compressed = (stored & 0x80000000) != 0 && compression_ == kCompressionZstd;
        const size_t storedSize = stored & 0x7FFFFFFF;
        const u32 packedSize = Be32(e + 8);
        chunk_.assign(chunk, 0);
        if (storedSize == 0) {  // all zeroes
            loaded_ = group;
            return;
        }
        const size_t maxPlain = lists * kMaxExceptionList + std::max<size_t>(chunk, packedSize) + 1;
        if (storedSize > maxPlain + (64 << 10)) Fail(kDamaged);
        stored_.resize(storedSize);
        ReadExact(read_, fileOffset, stored_.data(), storedSize);
        const u8* plain = stored_.data();
        size_t plainSize = storedSize;
        if (compressed) {
            inflated_.resize(maxPlain);
            plainSize = Inflate(stored_.data(), storedSize, inflated_.data(), inflated_.size());
            plain = inflated_.data();
        }
        // The hash exceptions come first: only the hash blocks need them, and those aren't kept.
        size_t pos = 0;
        for (u32 i = 0; i < lists; ++i) {
            if (pos + 2 > plainSize) Fail(kDamaged);
            pos += 2 + size_t{Be16(plain + pos)} * 22;
        }
        if (!compressed && lists != 0) pos = (pos + 3) & ~size_t{3};
        if (pos > plainSize) Fail(kDamaged);
        if (packedSize != 0) {
            if (packedSize > plainSize - pos) Fail(kDamaged);
            Unpack(plain + pos, packedSize, chunk, dataOffset);
        } else {
            std::memcpy(chunk_.data(), plain + pos, static_cast<size_t>(std::min<u64>(chunk, plainSize - pos)));
        }
        loaded_ = group;
    }

    // RVZ packing: runs of stored bytes and of generated padding, each with a size in front.
    void Unpack(const u8* in, size_t inSize, u64 chunk, u64 dataOffset) {
        size_t pos = 0;
        u64 written = 0;
        while (written < chunk && pos + 4 <= inSize) {
            const u32 word = Be32(in + pos);
            pos += 4;
            const size_t take = static_cast<size_t>(std::min<u64>(word & 0x7FFFFFFF, chunk - written));
            if (word & 0x80000000) {
                if (pos + LaggedFibonacci::kSeedWords * 4 > inSize) Fail(kDamaged);
                junk_.SetSeed(in + pos);
                pos += LaggedFibonacci::kSeedWords * 4;
                junk_.Skip(static_cast<size_t>((dataOffset + written) % kBlockTotal));
                junk_.Generate(chunk_.data() + written, take);
            } else {
                if (take > inSize - pos) Fail(kDamaged);
                std::memcpy(chunk_.data() + written, in + pos, take);
                pos += take;
            }
            written += take;
        }
    }

    const ReadAt& read_;
    ZSTD_DCtx* zstd_;
    u32 compression_ = 0;
    u64 chunkSize_ = 0;
    u8 header_[0x80] = {};
    std::vector<u8> partitions_;
    size_t partitionEntrySize_ = 0;
    std::vector<u8> rawEntries_;
    std::vector<u8> groups_;
    const u8* selected_ = nullptr;
    u64 loaded_ = ~u64{0};
    std::vector<u8> chunk_, stored_, inflated_;
    LaggedFibonacci junk_;
};

std::unique_ptr<Image> OpenImage(const ReadAt& read) {
    u8 start[0x20];
    ReadExact(read, 0, start, sizeof(start));
    if (std::memcmp(start, "RVZ\x01", 4) == 0) return std::make_unique<RvzImage>(read);
    if (std::memcmp(start, "WIA\x01", 4) == 0 || std::memcmp(start, "WBFS", 4) == 0) {
        Fail("This app reads ISO and RVZ files. In Dolphin, convert the game to ISO or RVZ, then choose it again.");
    }
    if (Be32(start + 0x18) == kWiiMagic) return std::make_unique<IsoImage>(read);
    if (Be32(start + 0x1C) == kGameCubeMagic) Fail("That disc image isn't a Wii game.");
    Fail("That file isn't a Wii disc image. Choose an ISO or RVZ of the game.");
}

// The offset of the first game partition's header.
u64 FindGamePartition(Image& image) {
    u8 table[0x20];
    image.ReadRaw(0x40000, table, sizeof(table));
    for (int group = 0; group < 4; ++group) {
        const u32 count = Be32(table + group * 8);
        const u64 entries = u64{Be32(table + group * 8 + 4)} << 2;
        if (count > 64) Fail(kDamaged);
        for (u32 i = 0; i < count; ++i) {
            u8 entry[8];
            image.ReadRaw(entries + i * u64{8}, entry, sizeof(entry));
            if (Be32(entry + 4) == 0) return u64{Be32(entry)} << 2;
        }
    }
    Fail(kDamaged);
}

// --- writing the tree --------------------------------------------------------------------------

struct OutputFile {
    std::string path;  // relative to the output folder
    u64 offset;
    u64 size;
};

// One level of a path from the disc. Only this game's plain names are expected.
bool SafeName(const std::string& name) {
    if (name.empty() || name == "." || name == "..") return false;
    for (const char c : name) {
        const auto b = static_cast<unsigned char>(c);
        if (b < 0x20 || b >= 0x7F || c == '/' || c == '\\' || c == ':') return false;
    }
    return true;
}

std::vector<OutputFile> ReadFileList(const std::vector<u8>& fst) {
    if (fst.size() < 12) Fail(kDamaged);
    const u32 count = Be32(fst.data() + 8);
    if (count == 0 || count > fst.size() / 12) Fail(kDamaged);
    const size_t names = size_t{count} * 12;
    std::vector<OutputFile> files;
    struct Folder {
        std::string path;
        u32 end;
    };
    std::vector<Folder> folders{{"files/", count}};
    for (u32 i = 1; i < count; ++i) {
        while (i >= folders.back().end) {
            if (folders.size() == 1) Fail(kDamaged);
            folders.pop_back();
        }
        const u8* e = fst.data() + size_t{i} * 12;
        const size_t nameOffset = names + (Be32(e) & 0xFFFFFF);
        if (nameOffset >= fst.size()) Fail(kDamaged);
        const auto* first = reinterpret_cast<const char*>(fst.data() + nameOffset);
        const size_t length = strnlen(first, fst.size() - nameOffset);
        const std::string name(first, length);
        if (!SafeName(name)) Fail(kDamaged);
        if (e[0] != 0) {
            const u32 end = Be32(e + 8);
            if (end <= i || end > folders.back().end) Fail(kDamaged);
            folders.push_back({folders.back().path + name + "/", end});
        } else {
            files.push_back({folders.back().path + name, u64{Be32(e + 4)} << 2, Be32(e + 8)});
        }
    }
    return files;
}

u64 DolSize(Image& image, u64 offset) {
    u8 header[0x100];
    image.ReadData(offset, header, sizeof(header));
    u64 size = sizeof(header);
    for (int i = 0; i < 18; ++i) {
        size = std::max<u64>(size, u64{Be32(header + i * 4)} + Be32(header + 0x90 + i * 4));
    }
    return size;
}

class Writer {
public:
    Writer(Image& image, const std::string& outDir, u64 total, const Progress& progress)
        : image_(image), root_(outDir), total_(total), progress_(progress), buffer_(1 << 20) {}

    // False when the player cancelled.
    bool Write(const OutputFile& file) {
        namespace fs = std::filesystem;
        const fs::path target = root_ / fs::path(file.path);
        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);
        if (ec) Fail("Couldn't create the game's folder: " + ec.message());
        std::unique_ptr<FILE, int (*)(FILE*)> out(std::fopen(target.string().c_str(), "wb"), &std::fclose);
        if (!out) Fail("Couldn't write " + file.path + ". Check that this phone has free space.");
        u64 offset = file.offset;
        u64 left = file.size;
        while (left != 0) {
            const size_t take = static_cast<size_t>(std::min<u64>(left, buffer_.size()));
            image_.ReadData(offset, buffer_.data(), take);
            if (std::fwrite(buffer_.data(), 1, take, out.get()) != take) {
                Fail("Couldn't write " + file.path + ". Check that this phone has free space.");
            }
            offset += take;
            left -= take;
            done_ += take;
            if (done_ - reported_ >= (u64{16} << 20) && !Report()) return false;
        }
        if (std::fflush(out.get()) != 0) Fail("Couldn't write " + file.path + ". Check that this phone has free space.");
        return true;
    }

    bool Report() {
        reported_ = done_;
        return !progress_ || progress_(done_, total_);
    }

private:
    Image& image_;
    std::filesystem::path root_;
    u64 total_;
    const Progress& progress_;
    std::vector<u8> buffer_;
    u64 done_ = 0;
    u64 reported_ = 0;
};

}  // namespace

bool ReadHeader(const ReadAt& read, uint8_t header[8], std::string& message) {
    try {
        OpenImage(read)->ReadRaw(0, header, 8);
        return true;
    } catch (const Failure& failure) {
        message = failure.message;
        return false;
    }
}

Result Extract(const ReadAt& read, const std::string& outDir, uint64_t freeBytes, const Progress& progress) {
    try {
        const std::unique_ptr<Image> image = OpenImage(read);
        const u64 partition = FindGamePartition(*image);
        u8 partitionHeader[kPartitionHeaderSize];
        image->ReadRaw(partition, partitionHeader, sizeof(partitionHeader));
        image->SelectPartition(partition, partitionHeader);

        u8 boot[0x440];
        image->ReadData(0, boot, sizeof(boot));
        const u64 dol = u64{Be32(boot + 0x420)} << 2;
        const u64 fstOffset = u64{Be32(boot + 0x424)} << 2;
        const u64 fstSize = u64{Be32(boot + 0x428)} << 2;
        if (fstSize == 0 || fstSize > (u64{64} << 20)) Fail(kDamaged);
        std::vector<u8> fst(static_cast<size_t>(fstSize));
        image->ReadData(fstOffset, fst.data(), fst.size());
        u8 apploader[0x20];
        image->ReadData(0x2440, apploader, sizeof(apploader));

        std::vector<OutputFile> files = {
            {"sys/boot.bin", 0, sizeof(boot)},
            {"sys/bi2.bin", 0x440, 0x2000},
            {"sys/apploader.img", 0x2440, u64{0x20} + Be32(apploader + 0x14) + Be32(apploader + 0x18)},
            {"sys/fst.bin", fstOffset, fstSize},
            {"sys/main.dol", dol, DolSize(*image, dol)},
        };
        std::vector<OutputFile> game = ReadFileList(fst);
        // In disc order, so groups are read once and the storage isn't seeking back and forth.
        std::sort(game.begin(), game.end(), [](const OutputFile& a, const OutputFile& b) { return a.offset < b.offset; });
        files.insert(files.end(), game.begin(), game.end());

        u64 total = 0;
        for (const OutputFile& file : files) total += file.size;
        if (total > freeBytes) {
            char text[160];
            std::snprintf(text, sizeof(text), "The game needs %.1f GB of free space, and this phone has %.1f GB.",
                          static_cast<double>(total) / (1u << 30), static_cast<double>(freeBytes) / (1u << 30));
            Fail(text);
        }

        Writer writer(*image, outDir, total, progress);
        for (const OutputFile& file : files) {
            if (!writer.Write(file)) return {Result::Status::Cancelled, ""};
        }
        writer.Report();
        return {};
    } catch (const Failure& failure) {
        return {Result::Status::Failed, failure.message};
    } catch (const std::exception& e) {
        return {Result::Status::Failed, e.what()};
    }
}

}  // namespace disc
