// Instrumented (MKW_PGO=generate) builds only: the game process is killed rather than exited on
// Android, so the profile runtime's atexit dump never runs. A background thread writes the
// counters every BASARA_PGO_DUMP seconds (30 by default) to <log or data dir>/libmain.profraw;
// the counters are cumulative, so the last write holds the whole session.
#if defined(MKW_PGO_GENERATE)

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

extern "C" int __llvm_profile_write_file(void);
extern "C" void __llvm_profile_set_filename(const char*);

namespace {

const char* DumpDirectory() {
    for (const char* name : {"BASARA_LOG_DIR", "WIICOMPILED_DATA_DIR"}) {
        const char* v = std::getenv(name);
        if (v && *v) return v;
    }
    return nullptr;
}

struct PgoDumper {
    PgoDumper() {
        std::thread([] {
            // The Java side sets the directories just before the library loads; give it a moment.
            const char* dir = nullptr;
            for (int i = 0; i < 60 && !(dir = DumpDirectory()); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            static std::string path = std::string(dir ? dir : "/data/local/tmp") + "/libmain.profraw";
            __llvm_profile_set_filename(path.c_str());
            int seconds = 30;
            if (const char* v = std::getenv("BASARA_PGO_DUMP")) seconds = std::max(5, std::atoi(v));
            for (;;) {
                std::this_thread::sleep_for(std::chrono::seconds(seconds));
                __llvm_profile_write_file();
            }
        }).detach();
    }
} g_pgoDumper;

}  // namespace

#endif
