// Host ISA guard. Every other product target builds with -march=x86-64-v3 (or, in the "compat"
// build, x86-64-v2), so a CPU without that level would otherwise die on an illegal-instruction fault
// with no explanation. This TU alone skips that flag (own CMake object library, excluded from unity
// build/PCH) and runs from a priority-101 C initializer, ahead of every C++ dynamic initializer
// and thus the first AVX2 code that could execute. Keep it free of anything that could pull in
// vectorized code: no iostreams, no std::string, no runtime-wide headers.
//
// NSMBW: two x86 builds ship side by side. NSMBW.exe is x86-64-v3 (Haswell / Excavator and newer);
// NSMBW-compat.exe is x86-64-v2 (SSE4.2 and POPCNT: Nehalem / Bulldozer and newer) with fused
// multiply-add done in software (isa/ppc_soft_fma.h), so its results are bit-identical to the v3
// build and to phones. The v3 build, on a CPU that only has v2, starts the compat build beside it
// instead of reporting an error (MKW_X86_LEVEL_V2 marks the compat build's copy of this file).
//
// x86-64-v3 is an x86-specific optional-feature baseline (AVX2/BMI2/FMA and friends are not
// guaranteed present on every x86_64 chip); nothing here applies on AArch64, where ASIMD/NEON is
// mandatory in the base architecture and PublicProducts.cmake never applies an -march=x86-64-v3
// equivalent flag to begin with. That branch below is a no-op stub, not a port of this check.

#if defined(__x86_64__)

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <cpuid.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifndef MKW_X86_LEVEL_V2
#define MKW_X86_LEVEL_V2 0
#endif

namespace {

void HostCpuId(unsigned leaf, unsigned subleaf, unsigned regs[4]) {
    unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;
    __cpuid_count(leaf, subleaf, eax, ebx, ecx, edx);
    regs[0] = eax;
    regs[1] = ebx;
    regs[2] = ecx;
    regs[3] = edx;
}

unsigned HostCpuIdMaxLeaf(unsigned base) {
    unsigned regs[4] = {0, 0, 0, 0};
    HostCpuId(base, 0, regs);
    return regs[0];
}

// XGETBV through inline assembly rather than the _xgetbv intrinsic: the GNU
// driver gates that intrinsic behind -mxsave, which this file is specifically
// compiled without. Only reachable once CPUID has reported OSXSAVE.
uint64_t ReadXcr0() {
    unsigned eax = 0, edx = 0;
    __asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
    return (static_cast<uint64_t>(edx) << 32) | eax;
}

struct CpuFeature {
    const char* name;
    unsigned leaf;
    unsigned subleaf;
    unsigned reg;  // index into the eax/ebx/ecx/edx array filled by HostCpuId
    unsigned bit;
    bool isOsXsave;
    bool v3;  // part of x86-64-v3 only (the rest is x86-64-v2, which v3 includes)
};

// Everything x86-64-v3 implies, which includes all of x86-64-v2. Spelled out so
// the error message can name the exact instruction sets the machine lacks
// rather than only "AVX2", which is merely the best known member of the set.
constexpr CpuFeature kRequiredFeatures[] = {
    {"SSE3", 1, 0, 2, 0, false, false},
    {"SSSE3", 1, 0, 2, 9, false, false},
    {"FMA", 1, 0, 2, 12, false, true},
    {"CMPXCHG16B", 1, 0, 2, 13, false, false},
    {"SSE4.1", 1, 0, 2, 19, false, false},
    {"SSE4.2", 1, 0, 2, 20, false, false},
    {"MOVBE", 1, 0, 2, 22, false, true},
    {"POPCNT", 1, 0, 2, 23, false, false},
    {"OSXSAVE", 1, 0, 2, 27, true, true},
    {"AVX", 1, 0, 2, 28, false, true},
    {"F16C", 1, 0, 2, 29, false, true},
    {"BMI1", 7, 0, 1, 3, false, true},
    {"AVX2", 7, 0, 1, 5, false, true},
    {"BMI2", 7, 0, 1, 8, false, true},
    {"LAHF-SAHF", 0x80000001u, 0, 2, 0, false, false},
    {"LZCNT", 0x80000001u, 0, 2, 5, false, true},
};

// Fixed-capacity text accumulation: no allocation, no exceptions, nothing that
// could route through code this file is trying to stay ahead of.
struct TextBuffer {
    char data[1024] = {};
    size_t used = 0;

    void Append(const char* text) {
        if (text == nullptr) {
            return;
        }
        while (*text != '\0' && used + 1 < sizeof(data)) {
            data[used++] = *text++;
        }
        data[used] = '\0';
    }
};

// True when the host can run a build of that level (v3 or else v2). Otherwise `missing` holds the
// absent feature names, comma separated.
bool CollectMissingBaselineFeatures(TextBuffer& missing, bool v3) {
    const unsigned maxBasic = HostCpuIdMaxLeaf(0);
    const unsigned maxExtended = HostCpuIdMaxLeaf(0x80000000u);

    bool ok = true;
    bool haveOsXsave = false;

    for (const CpuFeature& feature : kRequiredFeatures) {
        if (feature.v3 && !v3) {
            continue;
        }
        const bool leafAvailable = (feature.leaf & 0x80000000u) != 0
                                       ? feature.leaf <= maxExtended
                                       : feature.leaf <= maxBasic;

        bool present = false;
        if (leafAvailable) {
            unsigned regs[4] = {0, 0, 0, 0};
            HostCpuId(feature.leaf, feature.subleaf, regs);
            present = (regs[feature.reg] & (1u << feature.bit)) != 0;
        }

        if (present) {
            haveOsXsave = haveOsXsave || feature.isOsXsave;
            continue;
        }

        if (!ok) {
            missing.Append(", ");
        }
        missing.Append(feature.name);
        ok = false;
    }

    // CPUID reporting AVX is not sufficient: the OS also has to have enabled
    // XMM and YMM state saving or every VEX-encoded instruction faults. This is
    // the same guard a compiler's own runtime feature dispatch applies.
    if (v3 && ok && haveOsXsave) {
        constexpr uint64_t kXmmAndYmmState = 0x6u;
        if ((ReadXcr0() & kXmmAndYmmState) != kXmmAndYmmState) {
            missing.Append("operating system support for AVX register state (XCR0 YMM bits)");
            ok = false;
        }
    }

    return ok;
}

// stdio is NOT usable from a .CRT$XIC initializer - the UCRT has not stood it
// up yet, and fprintf(stderr, ...) faults there. Verified on this toolchain:
// WriteFile on the raw standard-error handle and MessageBoxA both work, printf
// does not. Anything added to this reporting path has to respect that.
//
// The POSIX path runs from an __attribute__((constructor)) instead, ahead of libc's own startup
// guarantees; ::write() on the raw fd is the same kind of allocation-free, libc-init-independent
// primitive as WriteFile is on Windows, so the same restriction is honored here.
void WriteStdErrEarly(const char* text) {
#if defined(_WIN32)
    const HANDLE handle = ::GetStdHandle(STD_ERROR_HANDLE);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return;
    }
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    DWORD written = 0;
    ::WriteFile(handle, text, static_cast<DWORD>(length), &written, nullptr);
#else
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    (void)::write(STDERR_FILENO, text, length);
#endif
}

[[noreturn]] void ReportUnsupportedCpu(const char* missing, bool v3, bool compatWouldRun) {
    TextBuffer message;
    if (v3) {
        message.Append(
            "This build needs a processor that supports AVX2 and the rest of the "
            "x86-64-v3 instruction set.\n\nMissing on this machine: ");
        message.Append(missing);
        message.Append(
            "\n\nx86-64-v3 covers Intel Core processors from Haswell (4th "
            "generation, 2013) onward and AMD processors from Excavator (2015) onward.");
        if (compatWouldRun) {
            message.Append(
                "\n\nThis processor can run the compat build, the build for older processors that comes "
                "in the same download, in its \"compat\" folder. Keep that folder beside this program.");
        }
    } else {
        message.Append(
            "This build needs a processor that supports SSE4.2, POPCNT and the rest of the "
            "x86-64-v2 instruction set.\n\nMissing on this machine: ");
        message.Append(missing);
        message.Append(
            "\n\nx86-64-v2 covers Intel Core processors from Nehalem (1st generation, 2008) "
            "onward and AMD processors from Bulldozer (2011) onward.");
    }

    // The tag matches RT_TAG_RUNTIME in runtime_log.h. It is spelled out here
    // because this translation unit must not include runtime-wide headers (see
    // the file comment): runtime_log.h pulls in <iostream> and memory.h, and
    // this code runs before any C++ dynamic initializer.
    WriteStdErrEarly("[runtime] ");
    WriteStdErrEarly(message.data);
    WriteStdErrEarly("\n");

#if defined(_WIN32)
    ::MessageBoxA(nullptr, message.data, "WiiCompiled - Unsupported Processor",
                  MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TASKMODAL);
    // Leave through the OS rather than exit(): the C++ dynamic initializers
    // have not run yet, so there is no constructed program state to unwind and
    // the teardown path itself lives in AVX2 translation units.
    ::ExitProcess(1u);
#else
    // Same reasoning as the Windows path above: no C++ dynamic initializer has run yet, so
    // _exit() (skips atexit/global destructors, unlike exit()) is the correct way out.
    ::_exit(1);
#endif
}

#if !MKW_X86_LEVEL_V2
// The v3 build on a v2-only CPU: run the compat build in the "compat" folder beside this executable
// ("compat\<name>-compat" with the same extension) with the same command line, and leave with its
// exit code. Returns only when there is no such file or it could not be started.
#if defined(_WIN32)
void RunCompatBuild() {
    wchar_t exe[MAX_PATH] = {};
    const DWORD length = ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return;
    }
    // "...\NSMBW.exe" -> "...\compat\NSMBW-compat.exe"
    DWORD slash = length;
    while (slash > 0 && exe[slash - 1] != L'\\' && exe[slash - 1] != L'/') {
        --slash;
    }
    DWORD dot = length;
    while (dot > slash && exe[dot - 1] != L'.') {
        --dot;
    }
    const DWORD stemEnd = dot > slash ? dot - 1 : length;
    wchar_t path[MAX_PATH + 32] = {};
    DWORD at = 0;
    const auto append = [&](const wchar_t* from, DWORD count) {
        for (DWORD i = 0; i < count && at < MAX_PATH + 31; ++i) {
            path[at++] = from[i];
        }
    };
    append(exe, slash);
    append(L"compat\\", 7);
    append(exe + slash, stemEnd - slash);
    append(L"-compat", 7);
    append(exe + stemEnd, length - stemEnd);
    path[at] = L'\0';
    if (::GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        return;
    }
    // The compat build's DLLs (SDL3, Dawn, libc++...) are this folder's: put it first on the PATH the
    // child inherits. (Its own folder, searched first, holds only the executable.)
    static wchar_t searchPath[32768];
    DWORD pathAt = 0;
    for (DWORD i = 0; i + 1 < slash && pathAt < 32000; ++i) {
        searchPath[pathAt++] = exe[i];  // the folder without its trailing backslash
    }
    searchPath[pathAt++] = L';';
    const DWORD old = ::GetEnvironmentVariableW(L"PATH", searchPath + pathAt, 32767 - pathAt);
    if (old >= 32767 - pathAt) {
        searchPath[pathAt] = L'\0';  // too long to keep: the folder alone still finds the DLLs
    }
    ::SetEnvironmentVariableW(L"PATH", searchPath);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!::CreateProcessW(path, ::GetCommandLineW(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup,
                          &process)) {
        return;
    }
    ::CloseHandle(process.hThread);
    ::WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    ::GetExitCodeProcess(process.hProcess, &code);
    ::ExitProcess(code);
}
#else
char** g_argv = nullptr;

void RunCompatBuild() {
    char exe[4096] = {};
    const ssize_t length = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (length <= 0 || g_argv == nullptr) {
        return;
    }
    // ".../NSMBW" -> ".../compat/NSMBW-compat"
    ssize_t slash = length;
    while (slash > 0 && exe[slash - 1] != '/') {
        --slash;
    }
    char path[4096 + 32] = {};
    size_t at = 0;
    const auto append = [&](const char* from, size_t count) {
        for (size_t i = 0; i < count && at < sizeof(path) - 1; ++i) {
            path[at++] = from[i];
        }
    };
    append(exe, static_cast<size_t>(slash));
    append("compat/", 7);
    append(exe + slash, static_cast<size_t>(length - slash));
    append("-compat", 7);
    path[at] = '\0';
    if (::access(path, X_OK) != 0) {
        return;
    }
    ::execv(path, g_argv);  // replaces this process; returns only on failure
}
#endif
#endif  // !MKW_X86_LEVEL_V2

}  // namespace

extern "C" int MkwHostCpuBaselineInit() {
    TextBuffer missing;
#if MKW_X86_LEVEL_V2
    if (!CollectMissingBaselineFeatures(missing, false)) {
        ReportUnsupportedCpu(missing.data, false, false);
    }
#else
    bool v3 = CollectMissingBaselineFeatures(missing, true);
#if defined(_WIN32)
    // BASARA_TEST_COMPAT_HANDOFF: behave as on a CPU without x86-64-v3 (tests the hand-over below).
    if (::GetEnvironmentVariableA("BASARA_TEST_COMPAT_HANDOFF", nullptr, 0) != 0) {
        v3 = false;
        missing.Append("(pretended: BASARA_TEST_COMPAT_HANDOFF)");
    }
#endif
    if (!v3) {
        TextBuffer v2Missing;
        const bool v2 = CollectMissingBaselineFeatures(v2Missing, false);
        if (v2) {
            RunCompatBuild();
        }
        ReportUnsupportedCpu(missing.data, true, v2);
    }
#endif
    return 0;
}

// Priorities 0-100 are reserved for the implementation; 101 is the earliest a
// user constructor can request, which puts this ahead of every default-priority
// constructor in the image. glibc passes a constructor the program's arguments,
// which the hand-over to the compat build needs.
#if defined(_WIN32)
__attribute__((constructor(101))) static void MkwHostCpuBaselineCtor() {
    MkwHostCpuBaselineInit();
}
#else
__attribute__((constructor(101))) static void MkwHostCpuBaselineCtor(int, char** argv, char**) {
#if !MKW_X86_LEVEL_V2
    g_argv = argv;
#else
    (void)argv;
#endif
    MkwHostCpuBaselineInit();
}
#endif

#else  // !defined(__x86_64__)

extern "C" int MkwHostCpuBaselineInit() {
    return 0;
}

#endif  // defined(__x86_64__)
