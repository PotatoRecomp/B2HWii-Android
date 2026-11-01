#include "gx_thread.hpp"

#include "fifo.hpp"
#include "../internal.hpp"

#include <aurora/aurora.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#endif

namespace aurora {
extern std::atomic<AuroraFrameWorkerWaitCallback> g_frameWorkerWaitCallback;
}

namespace aurora::gx::thread {
namespace {
Module Log("aurora::gx::thread");

using Clock = std::chrono::steady_clock;

struct Chunk {
  uint8_t* data = nullptr;
  uint32_t size = 0;
  uint32_t capacity = 0;
};

struct Item {
  Chunk chunk;
  std::function<void()> fn;
  uint64_t fence = 0;
};

struct State {
  std::mutex mutex;
  std::condition_variable cv;     // producer -> GX thread: work queued or stop
  std::condition_variable idleCv; // GX thread -> producer: queue ran empty
  std::deque<Item> queue;
  std::vector<Chunk> freeChunks;
  std::thread thread;
  std::thread::id threadId{};
  bool started = false;
  bool stop = false;
  bool busy = false;
  uint64_t nextFence = 0;
  uint64_t fenceDone = 0;
};

State g_state;
// A forwarded call running on the producer (BASARA_GX_THREAD=2) owns GX state like the GX thread.
thread_local bool t_runningForwarded = false;

std::atomic<uint64_t> g_chunks{0};
std::atomic<uint64_t> g_closures{0};
std::atomic<uint64_t> g_bytes{0};
std::atomic<uint64_t> g_syncs[static_cast<int>(SyncReason::Count)]{};
std::atomic<uint64_t> g_producerWaitNanos{0};
std::atomic<uint64_t> g_gxBusyNanos{0};

void run_item(Item& item) {
  if (item.fn) {
    item.fn();
    item.fn = nullptr;
  } else if (item.chunk.size != 0) {
    fifo::process_chunk(item.chunk.data, item.chunk.size);
  }
}

void thread_main() {
#if defined(__linux__)
  pthread_setname_np(pthread_self(), "GxThread");
#endif
  for (;;) {
    Item item;
    {
      std::unique_lock lock(g_state.mutex);
      if (g_state.queue.empty()) {
        g_state.busy = false;
        g_state.idleCv.notify_all();
        g_state.cv.wait(lock, [] { return g_state.stop || !g_state.queue.empty(); });
      }
      if (g_state.queue.empty() && g_state.stop) {
        g_state.busy = false;
        g_state.idleCv.notify_all();
        return;
      }
      item = std::move(g_state.queue.front());
      g_state.queue.pop_front();
      g_state.busy = true;
    }
    const auto started = Clock::now();
    try {
      run_item(item);
    } catch (const std::exception& e) {
      Log.error("GX thread item failed: {}", e.what());
    }
    g_gxBusyNanos.fetch_add(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count(),
        std::memory_order_relaxed);
    if (item.chunk.data != nullptr) {
      item.chunk.size = 0;
      std::lock_guard lock(g_state.mutex);
      g_state.freeChunks.push_back(item.chunk);
    }
    if (item.fence != 0) {
      {
        std::lock_guard lock(g_state.mutex);
        g_state.fenceDone = item.fence;
      }
      g_state.idleCv.notify_all();
    }
  }
}

void ensure_started_locked() {
  if (g_state.started) {
    return;
  }
  g_state.started = true;
  g_state.stop = false;
  g_state.busy = false;
  g_state.thread = std::thread(thread_main);
  g_state.threadId = g_state.thread.get_id();
  Log.info("GX thread enabled");
}

// Producer only. Swaps the FIFO write buffer for a free one and queues the full one.
void enqueue_pending_bytes_locked() {
  auto& size = fifo::detail::sBufferSize;
  if (size == 0) {
    return;
  }
  Chunk full{fifo::detail::sBufferData, size, fifo::detail::sBufferCapacity};
  Chunk next;
  if (!g_state.freeChunks.empty()) {
    next = g_state.freeChunks.back();
    g_state.freeChunks.pop_back();
  } else {
    next.capacity = full.capacity;
    next.data = static_cast<uint8_t*>(std::malloc(next.capacity));
  }
  fifo::detail::sBufferData = next.data;
  fifo::detail::sBufferCapacity = next.capacity;
  fifo::detail::sBufferSize = 0;
  g_bytes.fetch_add(full.size, std::memory_order_relaxed);
  g_chunks.fetch_add(1, std::memory_order_relaxed);
  g_state.queue.push_back(Item{full, nullptr});
}

} // namespace

bool enabled() noexcept {
  static const bool on = [] {
    const char* value = std::getenv("BASARA_GX_THREAD");
    return value != nullptr && (value[0] == '1' || value[0] == '2' || value[0] == '3');
  }();
  return on;
}

namespace {
// BASARA_GX_THREAD=3 (bisecting): everything still runs on the GX thread, but the producer waits
// for it after each hand-over, so nothing the game does next can overlap the decode.
bool lockstep() noexcept {
  static const bool on = [] {
    const char* value = std::getenv("BASARA_GX_THREAD");
    return value != nullptr && value[0] == '3';
  }();
  return on;
}
} // namespace

bool on_gx_thread() noexcept {
  // threadId is written once, before any item can run, under the queue mutex.
  return t_runningForwarded || (g_state.started && std::this_thread::get_id() == g_state.threadId);
}

void submit() {
  if (!offload() || fifo::detail::sBufferSize == 0) {
    return;
  }
  {
    std::lock_guard lock(g_state.mutex);
    ensure_started_locked();
    enqueue_pending_bytes_locked();
  }
  g_state.cv.notify_one();
  if (lockstep()) sync(SyncReason::Other);
}

namespace {
uint64_t post_impl(std::function<void()> fn, const char* name, bool fenced) {
  if (name != nullptr) {
    // First use of each forwarded call site (diagnostics).
    static std::mutex seenMutex;
    static std::vector<const char*> seen;
    std::lock_guard lock(seenMutex);
    if (std::find(seen.begin(), seen.end(), name) == seen.end() && seen.size() < 128) {
      seen.push_back(name);
      Log.info("first forwarded call: {}", name);
    }
  }
  // BASARA_GX_THREAD=2 (bisecting): run forwarded calls on the producer after a full sync.
  static const bool inlinePosts = [] {
    const char* value = std::getenv("BASARA_GX_THREAD");
    return value != nullptr && value[0] == '2';
  }();
  if (inlinePosts) {
    sync(SyncReason::Other);
    t_runningForwarded = true;
    fn();
    t_runningForwarded = false;
    return 0;
  }
  uint64_t ticket = 0;
  {
    std::lock_guard lock(g_state.mutex);
    ensure_started_locked();
    enqueue_pending_bytes_locked();
    if (fenced) ticket = ++g_state.nextFence;
    g_state.queue.push_back(Item{{}, std::move(fn), ticket});
    g_closures.fetch_add(1, std::memory_order_relaxed);
  }
  g_state.cv.notify_one();
  if (lockstep()) sync(SyncReason::Other);
  else sync_after(kSyncAfterPost);
  return ticket;
}

unsigned sync_after_mask() noexcept {
  static const unsigned mask = [] {
    const char* value = std::getenv("BASARA_GX_SYNC_AFTER");
    return value != nullptr ? static_cast<unsigned>(std::strtoul(value, nullptr, 0)) : 0u;
  }();
  return mask;
}
} // namespace

void sync_after(unsigned bit) {
  if ((sync_after_mask() & bit) != 0 && offload()) sync(SyncReason::Other);
}

void post(std::function<void()> fn, const char* name) { post_impl(std::move(fn), name, false); }

uint64_t post_fenced(std::function<void()> fn, const char* name) {
  if (!offload()) {
    fn();
    return 0;
  }
  return post_impl(std::move(fn), name, true);
}

bool fence_reached(uint64_t ticket) {
  if (ticket == 0 || !offload()) {
    return true;
  }
  std::lock_guard lock(g_state.mutex);
  return g_state.fenceDone >= ticket;
}

void wait_fence(uint64_t ticket, SyncReason reason) {
  if (ticket == 0 || !offload()) {
    return;
  }
  std::unique_lock lock(g_state.mutex);
  if (g_state.fenceDone >= ticket) {
    return;
  }
  g_syncs[static_cast<int>(reason)].fetch_add(1, std::memory_order_relaxed);
  g_state.cv.notify_one();
  const auto started = Clock::now();
  while (g_state.fenceDone < ticket) {
    if (g_state.idleCv.wait_for(lock, std::chrono::milliseconds(1), [ticket] { return g_state.fenceDone >= ticket; })) {
      break;
    }
    // Keep the guest's alarm/retrace pump alive while waiting.
    lock.unlock();
    if (const auto callback = g_frameWorkerWaitCallback.load(std::memory_order_acquire)) {
      callback();
    }
    lock.lock();
  }
  g_producerWaitNanos.fetch_add(
      std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count(),
      std::memory_order_relaxed);
}

void sync(SyncReason reason) {
  if (!offload()) {
    return;
  }
  g_syncs[static_cast<int>(reason)].fetch_add(1, std::memory_order_relaxed);
  std::unique_lock lock(g_state.mutex);
  if (!g_state.started) {
    // Nothing ever ran on the GX thread; whatever is pending is still the producer's to decode.
    return;
  }
  enqueue_pending_bytes_locked();
  if (g_state.queue.empty() && !g_state.busy) {
    return;
  }
  g_state.cv.notify_one();
  const auto started = Clock::now();
  while (!g_state.queue.empty() || g_state.busy) {
    if (g_state.idleCv.wait_for(lock, std::chrono::milliseconds(1),
                                [] { return g_state.queue.empty() && !g_state.busy; })) {
      break;
    }
    // Keep the guest's alarm/retrace pump alive, as a producer waiting on the frame worker does.
    lock.unlock();
    if (const auto callback = g_frameWorkerWaitCallback.load(std::memory_order_acquire)) {
      callback();
    }
    lock.lock();
  }
  g_producerWaitNanos.fetch_add(
      std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count(),
      std::memory_order_relaxed);
}

Stats take_stats() noexcept {
  Stats s;
  s.chunks = g_chunks.exchange(0, std::memory_order_relaxed);
  s.closures = g_closures.exchange(0, std::memory_order_relaxed);
  s.bytes = g_bytes.exchange(0, std::memory_order_relaxed);
  for (int i = 0; i < static_cast<int>(SyncReason::Count); ++i) {
    s.syncs[i] = g_syncs[i].exchange(0, std::memory_order_relaxed);
  }
  s.producerWaitNanos = g_producerWaitNanos.exchange(0, std::memory_order_relaxed);
  s.gxBusyNanos = g_gxBusyNanos.exchange(0, std::memory_order_relaxed);
  return s;
}

void shutdown() {
  {
    std::lock_guard lock(g_state.mutex);
    if (!g_state.started) {
      return;
    }
    g_state.stop = true;
  }
  g_state.cv.notify_all();
  if (g_state.thread.joinable()) {
    g_state.thread.join();
  }
  std::lock_guard lock(g_state.mutex);
  for (auto& chunk : g_state.freeChunks) {
    std::free(chunk.data);
  }
  g_state.freeChunks.clear();
  g_state.started = false;
}

} // namespace aurora::gx::thread

extern "C" void AuroraTakeGxThreadStats(uint64_t* chunks, uint64_t* closures, uint64_t* bytes, uint64_t* syncs,
                                        uint32_t syncCount, uint64_t* producerWaitNanos, uint64_t* gxBusyNanos) {
  const auto s = aurora::gx::thread::take_stats();
  *chunks = s.chunks;
  *closures = s.closures;
  *bytes = s.bytes;
  for (uint32_t i = 0; i < syncCount && i < static_cast<uint32_t>(aurora::gx::thread::SyncReason::Count); ++i) {
    syncs[i] = s.syncs[i];
  }
  *producerWaitNanos = s.producerWaitNanos;
  *gxBusyNanos = s.gxBusyNanos;
}

extern "C" bool AuroraGxThreadEnabled() { return aurora::gx::thread::enabled(); }

// Fence after everything submitted so far (0 when the GX thread is off: already reached).
extern "C" uint64_t AuroraGxThreadMark() {
  if (!aurora::gx::thread::offload()) return 0;
  return aurora::gx::thread::post_fenced([] {}, nullptr);
}

extern "C" void AuroraGxThreadWaitMark(uint64_t ticket) {
  aurora::gx::thread::wait_fence(ticket, aurora::gx::thread::SyncReason::EndFrame);
}

extern "C" bool AuroraGxThreadMarkReached(uint64_t ticket) {
  return aurora::gx::thread::fence_reached(ticket);
}

// Run fn(arg) on the GX thread after everything submitted so far, or now when it is off.
extern "C" void AuroraGxThreadPostCall(void (*fn)(uint32_t), uint32_t arg) {
  if (!aurora::gx::thread::offload()) {
    fn(arg);
    return;
  }
  aurora::gx::thread::post([fn, arg] { fn(arg); }, nullptr);
}
