// Host test for runtime/disc: extracts an image with the same code the Android launcher uses.
#include "../../runtime/disc/disc_extract.h"
#include <cstdio>
int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: disc-test IMAGE OUTDIR\n"); return 2; }
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::perror("open"); return 1; }
    disc::ReadAt read = [f](uint64_t off, void* out, size_t size) {
        if (_fseeki64(f, (long long)off, SEEK_SET) != 0) return false;
        return std::fread(out, 1, size, f) == size;
    };
    uint64_t last = 0;
    auto r = disc::Extract(read, argv[2], ~0ull, [&](uint64_t d, uint64_t t) {
        if (d - last > (256ull << 20) || d == t) { std::printf("%llu / %llu MiB\n", d >> 20, t >> 20); last = d; }
        return true;
    });
    std::printf("status %d %s\n", (int)r.status, r.message.c_str());
    return r.status == disc::Result::Status::Ok ? 0 : 1;
}
