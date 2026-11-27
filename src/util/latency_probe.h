#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>

namespace tapir {

// File-scope counters so index.cpp can note icon I/O without reaching AppState.
inline std::atomic<unsigned> g_probeIoCalls{0};
inline std::atomic<bool> g_probeEnabled{false};

inline void probeResetIo() noexcept {
  g_probeIoCalls.store(0, std::memory_order_relaxed);
}

inline void probeNoteIo() noexcept {
  if (g_probeEnabled.load(std::memory_order_relaxed)) {
    g_probeIoCalls.fetch_add(1, std::memory_order_relaxed);
  }
}

struct MatchMarks {
  long long scoreUs = 0;
  long long selectUs = 0;
};

struct KeystrokeSample {
  long long inputUs = 0;
  long long matchDoneUs = 0;
  long long sortDoneUs = 0;
  long long uiDoneUs = 0;
  unsigned queryLen = 0;
  unsigned publishedQueryLen = 0;
  unsigned ioCalls = 0;
  std::size_t entries = 0;
  std::size_t results = 0;
  bool valid = false;
};

struct LatencyProbe {
  std::array<KeystrokeSample, 256> samples{};
  bool enabled = false;
  bool active = false;
  bool rankStarted = false;
  bool rankDone = false;
  bool uiRecorded = false;
  std::size_t count = 0;
  std::chrono::steady_clock::time_point t0{};
  MatchMarks marks{};

  void reset() noexcept {
    samples = {};
    count = 0;
    active = false;
    rankStarted = false;
    rankDone = false;
    uiRecorded = false;
    marks = {};
    probeResetIo();
    g_probeEnabled.store(enabled, std::memory_order_relaxed);
  }

  void enable(bool on) noexcept {
    enabled = on;
    g_probeEnabled.store(on, std::memory_order_relaxed);
  }

  void begin(unsigned queryLen) noexcept {
    if (!enabled) return;
    marks = {};
    rankStarted = false;
    rankDone = false;
    uiRecorded = false;
    active = true;
    t0 = std::chrono::steady_clock::now();
    KeystrokeSample& sample = samples[count % samples.size()];
    sample = {};
    sample.queryLen = queryLen;
    sample.valid = true;
  }

  void markInput() noexcept {
    if (!enabled || !active) return;
    samples[count % samples.size()].inputUs = elapsedUs();
  }

  void beginRank() noexcept {
    if (!enabled || !active) return;
    rankStarted = true;
  }

  void finishRank(std::size_t entries, std::size_t results,
                  unsigned publishedQueryLen) noexcept {
    if (!enabled || !active || !rankStarted || rankDone) return;
    rankDone = true;
    KeystrokeSample& sample = samples[count % samples.size()];
    const long long sortDone = elapsedUs();
    sample.sortDoneUs = sortDone;
    sample.matchDoneUs = sortDone - marks.selectUs;
    sample.entries = entries;
    sample.results = results;
    sample.publishedQueryLen = publishedQueryLen;
  }

  void finishUi() noexcept {
    if (!enabled || !active || !rankDone || uiRecorded) return;
    uiRecorded = true;
    samples[count % samples.size()].uiDoneUs = elapsedUs();
  }

  void endKeystroke() noexcept {
    if (!enabled) return;
    if (active) {
      samples[count % samples.size()].ioCalls =
          g_probeIoCalls.load(std::memory_order_relaxed);
      ++count;
    }
    active = false;
  }

  const KeystrokeSample& current() const noexcept {
    if (active) return samples[count % samples.size()];
    if (count == 0) return samples[0];
    return samples[(count - 1) % samples.size()];
  }

 private:
  long long elapsedUs() const noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - t0)
        .count();
  }
};

}  // namespace tapir
