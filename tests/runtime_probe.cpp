// SPDX-License-Identifier: MIT
// Runs only as native Orbis code; its existence is not hardware evidence.
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <span>
#include <thread>
#include <sys/stat.h>
#include <orbis/libkernel.h>

static_assert(sizeof(void*) == 8);
static_assert(char(-1) < 0);
static_assert(std::endian::native == std::endian::little);
static_assert(std::is_same_v<decltype(std::abs(-1.5f)), float>);

int main() {
  mkdir("/data/project-prime", 0700);
  FILE* log = fopen("/data/project-prime/runtime.jsonl", "w");
  if (!log) { sceKernelDebugOutText(0, "Project Prime: cannot open runtime log\n"); return 1; }
  fprintf(log, "{\"schema\":1,\"target\":\"orbis\",\"probe\":\"runtime\"}\n"); fflush(log);
  std::atomic<unsigned> value{0};
  std::mutex mutex;
  std::condition_variable condition;
  bool ready = false;
  const auto begin = std::chrono::steady_clock::now();
  std::thread worker([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    { std::lock_guard<std::mutex> lock(mutex); value.store(42); ready = true; }
    condition.notify_one();
  });
  bool signalled;
  { std::unique_lock<std::mutex> lock(mutex);
    signalled = condition.wait_for(lock, std::chrono::seconds(2), [&] { return ready; }); }
  worker.join();
  const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - begin).count();
  std::array<unsigned, 4> words{1, 2, 3, 4};
  std::span<const unsigned> view(words);
  const bool ok = signalled && value.load() == 42 && view.size() == 4 && ns >= 10000000;
  fprintf(log, "{\"thread_cv_atomic\":%s,\"elapsed_ns\":%lld,\"passed\":%s}\n",
          signalled && value.load() == 42 ? "true" : "false", (long long)ns, ok ? "true" : "false");
  fflush(log); fclose(log);
  sceKernelDebugOutText(0, ok ? "Project Prime runtime probe PASS\n" : "Project Prime runtime probe FAIL\n");
  return ok ? 0 : 2;
}
