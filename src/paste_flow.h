#pragma once

#include <cstdint>

namespace tapir::paste {

// Total budget for the focus polls, shared by the checks before the clipboard
// write and the checks before sending keys.
inline constexpr std::int64_t kFocusTimeoutMs = 300;

enum class Mode { Paste, CopyOnly };

// A request for the Win32 adapter. The adapter performs the action, fills an
// Observation with what it saw, and calls Flow::observe to get the next one.
enum class Action {
    HidePopup,
    ValidateTarget,
    RestoreDirect,
    RestoreAttached,
    PollFocus,
    WriteClipboard,
    SendCtrlV,
    DetachThreads,
    ClearMarker,
    LogFailure,
    Done,
};

struct Observation {
    bool validTarget = false;
    bool restoreSucceeded = false;
    bool foregroundOnTarget = false;
    bool targetThreadFocused = false;
    bool clipboardWritten = false;
    bool keysSent = false;
};

enum class Failure {
    None,
    InvalidTarget,
    RestoreFailed,
    TimeoutBeforeWrite,
    WriteFailed,
    TimeoutBeforeKeys,
    KeysFailed,
};

// Pure ordering and bailout logic for the paste flow. No Win32 calls, no clock,
// no waiting: time and every observation are supplied by the caller.
class Flow {
public:
    explicit Flow(Mode mode) noexcept;

    Action current() const noexcept { return current_; }
    bool done() const noexcept { return current_ == Action::Done; }
    Failure failure() const noexcept { return failure_; }
    bool attached() const noexcept { return attached_; }
    bool clipboardWritten() const noexcept { return clipboardWritten_; }

    // Milliseconds left in the focus budget at `nowMs`, never negative.
    std::int64_t remainingMs(std::int64_t nowMs) const noexcept;

    // Applies the result of `current()` and returns the next action.
    Action observe(const Observation& observation, std::int64_t nowMs) noexcept;

private:
    enum class Phase {
        Start,
        Validate,
        RestoreDirectly,
        RestoreWithAttach,
        BeforeWrite,
        Write,
        BeforeKeys,
        SendKeys,
        Cleanup,
    };

    void fail(Failure reason) noexcept;
    Action nextCleanup() noexcept;
    bool focusReady(const Observation& observation) const noexcept;
    void focusRestored(std::int64_t nowMs) noexcept;
    void startFocusWait(std::int64_t nowMs) noexcept;
    bool focusWaitExpired(std::int64_t nowMs) const noexcept;

    Mode mode_;
    Phase phase_ = Phase::Start;
    Action current_ = Action::HidePopup;
    Failure failure_ = Failure::None;
    bool attached_ = false;
    bool clipboardWritten_ = false;
    bool logged_ = false;
    bool focusWaitStarted_ = false;
    std::int64_t deadlineMs_ = 0;
};

const char* failureText(Failure failure) noexcept;

}  // namespace tapir::paste
