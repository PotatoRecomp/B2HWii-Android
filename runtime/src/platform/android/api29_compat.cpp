// Android 10 (API 29) support for a library built against API 30.
//
// libmain is compiled for android-30, so bionic functions added in API 30 are linked as plain
// references and the dynamic loader refuses the library on Android 10. The two that remain are
// given here: on Android 11 and later each forwards to bionic's own function, unchanged; only
// Android 10 takes the fallback.
#if defined(__ANDROID__)

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

namespace {

void* LibcSymbol(const char* name) {
    static void* const libc = dlopen("libc.so", RTLD_NOW | RTLD_NOLOAD);
    return libc != nullptr ? dlsym(libc, name) : nullptr;
}

int64_t Nanos(const timespec& t) { return static_cast<int64_t>(t.tv_sec) * 1'000'000'000 + t.tv_nsec; }

timespec FromNanos(int64_t ns) {
    timespec t{};
    t.tv_sec = static_cast<time_t>(ns / 1'000'000'000);
    t.tv_nsec = static_cast<long>(ns % 1'000'000'000);
    return t;
}

}  // namespace

extern "C" {

// API 30. libc++'s condition_variable waits with it (steady_clock deadlines).
__attribute__((visibility("default"))) int pthread_cond_clockwait(pthread_cond_t* cond, pthread_mutex_t* mutex,
                                                                  clockid_t clock, const timespec* deadline) {
    using Fn = int (*)(pthread_cond_t*, pthread_mutex_t*, clockid_t, const timespec*);
    static const Fn real = reinterpret_cast<Fn>(LibcSymbol("pthread_cond_clockwait"));
    if (real != nullptr) {
        return real(cond, mutex, clock, deadline);
    }
    if (clock == CLOCK_REALTIME) {
        return pthread_cond_timedwait(cond, mutex, deadline);
    }
    if (clock != CLOCK_MONOTONIC) {
        return EINVAL;
    }
    using MonotonicFn = int (*)(pthread_cond_t*, pthread_mutex_t*, const timespec*);
    static const MonotonicFn monotonic =
        reinterpret_cast<MonotonicFn>(LibcSymbol("pthread_cond_timedwait_monotonic_np"));
    if (monotonic != nullptr) {
        return monotonic(cond, mutex, deadline);
    }
    // Last resort: the same distance from now on the realtime clock.
    timespec now{};
    timespec realNow{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    clock_gettime(CLOCK_REALTIME, &realNow);
    const timespec realDeadline = FromNanos(Nanos(realNow) + (Nanos(*deadline) - Nanos(now)));
    return pthread_cond_timedwait(cond, mutex, &realDeadline);
}

// API 30 in bionic, but the system call exists on every Android 10 kernel (3.17+).
__attribute__((visibility("default"))) int memfd_create(const char* name, unsigned int flags) {
    using Fn = int (*)(const char*, unsigned int);
    static const Fn real = reinterpret_cast<Fn>(LibcSymbol("memfd_create"));
    if (real != nullptr) {
        return real(name, flags);
    }
    return static_cast<int>(syscall(__NR_memfd_create, name, flags));
}

}  // extern "C"

#endif  // __ANDROID__
