#pragma once

#include <cstdint>

namespace tapir::anim {

inline constexpr int kShowMs = 120;
inline constexpr int kSelectionMs = 100;
inline constexpr int kCompactMs = 140;

constexpr float easeOutCubic(float t) noexcept {
    const float clamped = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float inv = 1.0f - clamped;
    return 1.0f - inv * inv * inv;
}

class Timeline {
public:
    void start(std::int64_t nowMs, int durationMs) noexcept {
        startMs_ = nowMs;
        durationMs_ = durationMs > 0 ? durationMs : 0;
        active_ = true;
    }

    void snap() noexcept { active_ = false; }

    void reset() noexcept {
        active_ = false;
        startMs_ = 0;
        durationMs_ = 0;
    }

    bool active() const noexcept { return active_; }

    bool finished(std::int64_t nowMs) const noexcept {
        return active_ && durationMs_ == 0 ? true
             : active_ && nowMs - startMs_ >= durationMs_;
    }

    float progress(std::int64_t nowMs) const noexcept {
        if (!active_) return 0.0f;
        if (durationMs_ == 0) return 1.0f;
        const std::int64_t elapsed = nowMs - startMs_;
        if (elapsed >= durationMs_) return 1.0f;
        if (elapsed <= 0) return 0.0f;
        return easeOutCubic(static_cast<float>(elapsed) / static_cast<float>(durationMs_));
    }

    bool update(std::int64_t nowMs) noexcept {
        if (!active_) return false;
        if (finished(nowMs)) {
            active_ = false;
            return false;
        }
        return true;
    }

private:
    std::int64_t startMs_ = 0;
    int durationMs_ = 0;
    bool active_ = false;
};

} // namespace tapir::anim
