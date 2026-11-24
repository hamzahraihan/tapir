#include "paste_flow.h"

namespace tapir::paste {

Flow::Flow(Mode mode) noexcept : mode_(mode) {}

std::int64_t Flow::remainingMs(std::int64_t nowMs) const noexcept {
    if (!focusWaitStarted_) return kFocusTimeoutMs;
    const std::int64_t left = deadlineMs_ - nowMs;
    return left > 0 ? left : 0;
}

bool Flow::focusReady(const Observation& observation) const noexcept {
    return observation.foregroundOnTarget && observation.targetThreadFocused;
}

void Flow::focusRestored(std::int64_t nowMs) noexcept {
    phase_ = Phase::BeforeWrite;
    startFocusWait(nowMs);
    current_ = Action::PollFocus;
}

void Flow::startFocusWait(std::int64_t nowMs) noexcept {
    if (focusWaitStarted_) return;
    focusWaitStarted_ = true;
    deadlineMs_ = nowMs + kFocusTimeoutMs;
}

bool Flow::focusWaitExpired(std::int64_t nowMs) const noexcept {
    return focusWaitStarted_ && nowMs >= deadlineMs_;
}

Action Flow::nextCleanup() noexcept {
    if (attached_) {
        attached_ = false;
        return Action::DetachThreads;
    }
    if (clipboardWritten_) {
        clipboardWritten_ = false;
        return Action::ClearMarker;
    }
    if (failure_ != Failure::None && !logged_) {
        logged_ = true;
        return Action::LogFailure;
    }
    return Action::Done;
}

void Flow::fail(Failure reason) noexcept {
    failure_ = reason;
    phase_ = Phase::Cleanup;
    current_ = nextCleanup();
}

Action Flow::observe(const Observation& observation, std::int64_t nowMs) noexcept {
    switch (phase_) {
    case Phase::Start:
        if (mode_ == Mode::CopyOnly) {
            phase_ = Phase::Write;
            current_ = Action::WriteClipboard;
        } else {
            phase_ = Phase::Validate;
            current_ = Action::ValidateTarget;
        }
        break;

    case Phase::Validate:
        if (!observation.validTarget) {
            fail(Failure::InvalidTarget);
        } else {
            phase_ = Phase::RestoreDirectly;
            current_ = Action::RestoreDirect;
        }
        break;

    case Phase::RestoreDirectly:
        if (observation.restoreSucceeded) {
            focusRestored(nowMs);
        } else {
            attached_ = true;
            phase_ = Phase::RestoreWithAttach;
            current_ = Action::RestoreAttached;
        }
        break;

    case Phase::RestoreWithAttach:
        if (observation.restoreSucceeded) {
            focusRestored(nowMs);
        } else {
            fail(Failure::RestoreFailed);
        }
        break;

    case Phase::BeforeWrite:
        if (focusReady(observation)) {
            phase_ = Phase::Write;
            current_ = Action::WriteClipboard;
        } else if (focusWaitExpired(nowMs)) {
            fail(Failure::TimeoutBeforeWrite);
        } else {
            current_ = Action::PollFocus;
        }
        break;

    case Phase::Write:
        if (!observation.clipboardWritten) {
            fail(Failure::WriteFailed);
        } else {
            clipboardWritten_ = true;
            if (mode_ == Mode::CopyOnly) {
                phase_ = Phase::Cleanup;
                current_ = nextCleanup();
            } else {
                phase_ = Phase::BeforeKeys;
                current_ = Action::PollFocus;
            }
        }
        break;

    case Phase::BeforeKeys:
        if (focusReady(observation)) {
            phase_ = Phase::SendKeys;
            current_ = Action::SendCtrlV;
        } else if (focusWaitExpired(nowMs)) {
            fail(Failure::TimeoutBeforeKeys);
        } else {
            current_ = Action::PollFocus;
        }
        break;

    case Phase::SendKeys:
        if (!observation.keysSent) {
            fail(Failure::KeysFailed);
        } else {
            phase_ = Phase::Cleanup;
            current_ = nextCleanup();
        }
        break;

    case Phase::Cleanup:
        current_ = nextCleanup();
        break;
    }
    return current_;
}

const char* failureText(Failure failure) noexcept {
    switch (failure) {
    case Failure::InvalidTarget:
        return "invalid target window";
    case Failure::RestoreFailed:
        return "foreground restore failed";
    case Failure::TimeoutBeforeWrite:
        return "timed out waiting for focus before the clipboard write";
    case Failure::WriteFailed:
        return "clipboard write failed";
    case Failure::TimeoutBeforeKeys:
        return "timed out waiting for focus before sending keys";
    case Failure::KeysFailed:
        return "sending Ctrl+V failed";
    case Failure::None:
        break;
    }
    return "unknown paste failure";
}

}  // namespace tapir::paste
