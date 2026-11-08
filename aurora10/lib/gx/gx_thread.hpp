#pragma once

// Optional GX thread (BASARA_GX_THREAD=1). The producer (game) thread keeps only the __gx shadow
// registers and the FIFO write buffer; everything that decodes or consumes GX state (g_gxState, gfx
// staging, texture caches) runs on the GX thread, in submission order:
//   - FIFO bytes are handed over in chunks (drain() submits instead of decoding),
//   - void API calls that touch decoded state are posted as closures (post()),
//   - calls that return decoded state to the caller wait for the GX thread to go idle (sync()) and
//     then run on the producer; while it is idle the producer owns every GX structure.
// Off by default; with it off every function here is a no-op and offload() is false.

#include <cstdint>
#include <functional>

namespace aurora::gx::thread {

// Why the producer waited for the GX thread (logged as counts).
enum class SyncReason : uint8_t {
  DrawDone,
  BeginFrame,
  EndFrame,
  Query,
  Copy,
  Settings,
  Other,
  Count,
};

bool enabled() noexcept;
bool on_gx_thread() noexcept;
// True on the producer while the GX thread is enabled: callers forward instead of running inline.
inline bool offload() noexcept { return enabled() && !on_gx_thread(); }

// Hand the producer's pending FIFO bytes to the GX thread without waiting.
void submit();
// Submit pending FIFO bytes, then run fn on the GX thread after them.
void post(std::function<void()> fn, const char* name = nullptr);
// Submit pending FIFO bytes and wait until the GX thread has run everything queued.
void sync(SyncReason reason);
// Like post(), and returns a ticket that wait_fence() can wait for without draining what is queued
// after it.
uint64_t post_fenced(std::function<void()> fn, const char* name = nullptr);
// Wait until the GX thread has finished the fenced item `ticket` (0 returns at once).
void wait_fence(uint64_t ticket, SyncReason reason);
// True once the GX thread has finished the fenced item `ticket` (0 counts as finished).
bool fence_reached(uint64_t ticket);

// Bisecting (BASARA_GX_SYNC_AFTER=mask): the producer syncs right after the call sites in `bit`
// (1 GXCallDisplayList, 2 GXEnd, 4 GXLoadTexObj, 8 every posted closure). No-op when not offloading.
enum : unsigned { kSyncAfterDisplayList = 1, kSyncAfterEnd = 2, kSyncAfterLoadTexObj = 4, kSyncAfterPost = 8 };
void sync_after(unsigned bit);

struct Stats {
  uint64_t chunks = 0;
  uint64_t closures = 0;
  uint64_t bytes = 0;
  uint64_t syncs[static_cast<int>(SyncReason::Count)] = {};
  uint64_t producerWaitNanos = 0;
  uint64_t gxBusyNanos = 0;
};
// Totals since the last call (reset on read).
Stats take_stats() noexcept;

void shutdown();

} // namespace aurora::gx::thread

#define AURORA_GX_FORWARD(...)                                                                                         \
  do {                                                                                                                 \
    if (aurora::gx::thread::offload()) {                                                                               \
      aurora::gx::thread::post([=] { __VA_ARGS__; }, #__VA_ARGS__);                                                                  \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (0)

#define AURORA_GX_SYNC(reason)                                                                                         \
  do {                                                                                                                 \
    if (aurora::gx::thread::offload()) aurora::gx::thread::sync(aurora::gx::thread::SyncReason::reason);              \
  } while (0)
