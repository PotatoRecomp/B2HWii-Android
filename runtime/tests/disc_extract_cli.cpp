// disc_extract_cli.cpp - desktop checks of runtime/disc against a real image.
//   disc_extract_cli extract IMAGE OUTDIR       unpack, as the launcher does
//   disc_extract_cli compare IMAGE_A IMAGE_B    every byte of both images' game partition data
#include "../disc/disc_extract.cpp"

#include <chrono>

namespace {

using disc::u64;
using disc::u8;

disc::ReadAt FileReader(FILE* file) {
    return [file](uint64_t offset, void* out, size_t size) {
        return _fseeki64(file, static_cast<int64_t>(offset), SEEK_SET) == 0 && std::fread(out, 1, size, file) == size;
    };
}

int Compare(const char* pathA, const char* pathB) {
    FILE* fileA = std::fopen(pathA, "rb");
    FILE* fileB = std::fopen(pathB, "rb");
    if (fileA == nullptr || fileB == nullptr) return 2;
    const disc::ReadAt readA = FileReader(fileA), readB = FileReader(fileB);
    try {
        auto a = disc::OpenImage(readA);
        auto b = disc::OpenImage(readB);
        const u64 partition = disc::FindGamePartition(*a);
        if (partition != disc::FindGamePartition(*b)) return 3;
        u8 header[disc::kPartitionHeaderSize];
        a->ReadRaw(partition, header, sizeof(header));
        a->SelectPartition(partition, header);
        b->SelectPartition(partition, header);
        const u64 dataBlocks = (u64{disc::Be32(header + 0x2BC)} << 2) / disc::kBlockTotal;
        const u64 size = dataBlocks * disc::kBlockData;
        std::vector<u8> bufferA(1 << 20), bufferB(1 << 20);
        u64 mismatches = 0;
        for (u64 offset = 0; offset < size; offset += bufferA.size()) {
            const size_t take = static_cast<size_t>(std::min<u64>(bufferA.size(), size - offset));
            a->ReadData(offset, bufferA.data(), take);
            b->ReadData(offset, bufferB.data(), take);
            if (std::memcmp(bufferA.data(), bufferB.data(), take) != 0 && mismatches++ < 10) {
                std::printf("differs in the MiB at %llx\n", static_cast<unsigned long long>(offset));
            }
        }
        std::printf("compared %llu bytes, %llu differing MiB\n", static_cast<unsigned long long>(size),
                    static_cast<unsigned long long>(mismatches));
        return mismatches == 0 ? 0 : 1;
    } catch (const disc::Failure& failure) {
        std::printf("failed: %s\n", failure.message.c_str());
        return 4;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "compare") == 0) return Compare(argv[2], argv[3]);
    if (argc != 4 || std::strcmp(argv[1], "extract") != 0) return 2;
    FILE* file = std::fopen(argv[2], "rb");
    if (file == nullptr) return 2;
    const disc::ReadAt read = FileReader(file);
    uint8_t header[8];
    std::string message;
    if (!disc::ReadHeader(read, header, message)) {
        std::printf("failed: %s\n", message.c_str());
        return 4;
    }
    std::printf("id %.6s\n", header);
    std::filesystem::create_directories(argv[3]);
    const auto start = std::chrono::steady_clock::now();
    const disc::Result result = disc::Extract(read, argv[3], ~uint64_t{0}, [](uint64_t, uint64_t) { return true; });
    const std::chrono::duration<double> took = std::chrono::steady_clock::now() - start;
    std::printf("status %d %s (%.1f s)\n", static_cast<int>(result.status), result.message.c_str(), took.count());
    return result.status == disc::Result::Status::Ok ? 0 : 1;
}
