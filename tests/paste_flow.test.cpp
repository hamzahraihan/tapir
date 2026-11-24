#include <doctest.h>

#include "paste_flow.h"

namespace {

using tapir::paste::Action;
using tapir::paste::Failure;
using tapir::paste::Flow;
using tapir::paste::Mode;
using tapir::paste::Observation;

Observation Focused() {
    Observation observation;
    observation.foregroundOnTarget = true;
    observation.targetThreadFocused = true;
    return observation;
}

Observation ValidTarget() {
    Observation observation;
    observation.validTarget = true;
    return observation;
}

Observation Restored() {
    Observation observation;
    observation.restoreSucceeded = true;
    return observation;
}

Observation Written() {
    Observation observation;
    observation.clipboardWritten = true;
    return observation;
}

Observation Sent() {
    Observation observation;
    observation.keysSent = true;
    return observation;
}

Flow ReadyToWrite() {
    Flow flow(Mode::Paste);
    flow.observe({}, 0);
    flow.observe(ValidTarget(), 0);
    flow.observe(Restored(), 0);
    return flow;
}

}  // namespace

TEST_CASE("paste: a verified paste writes the clipboard before sending Ctrl+V") {
    Flow flow(Mode::Paste);
    CHECK(flow.current() == Action::HidePopup);

    CHECK(flow.observe({}, 1000) == Action::ValidateTarget);

    Observation validTarget;
    validTarget.validTarget = true;
    CHECK(flow.observe(validTarget, 1000) == Action::RestoreDirect);

    Observation restored;
    restored.restoreSucceeded = true;
    CHECK(flow.observe(restored, 1000) == Action::PollFocus);

    CHECK(flow.observe(Focused(), 1010) == Action::WriteClipboard);

    Observation written;
    written.clipboardWritten = true;
    CHECK(flow.observe(written, 1020) == Action::PollFocus);

    CHECK(flow.observe(Focused(), 1030) == Action::SendCtrlV);

    Observation sent;
    sent.keysSent = true;
    CHECK(flow.observe(sent, 1040) == Action::ClearMarker);
    CHECK(flow.observe({}, 1040) == Action::Done);

    CHECK(flow.failure() == Failure::None);
    CHECK_FALSE(flow.attached());
}

TEST_CASE("paste: a failed direct restore attaches threads and detaches on success") {
    Flow flow(Mode::Paste);
    CHECK(flow.observe({}, 0) == Action::ValidateTarget);
    CHECK(flow.observe(ValidTarget(), 0) == Action::RestoreDirect);
    CHECK(flow.observe({}, 0) == Action::RestoreAttached);
    CHECK(flow.attached());

    CHECK(flow.observe(Restored(), 0) == Action::PollFocus);
    CHECK(flow.observe(Focused(), 10) == Action::WriteClipboard);
    CHECK(flow.observe(Written(), 20) == Action::PollFocus);
    CHECK(flow.observe(Focused(), 30) == Action::SendCtrlV);
    CHECK(flow.observe(Sent(), 40) == Action::DetachThreads);
    CHECK_FALSE(flow.attached());
    CHECK(flow.observe({}, 40) == Action::ClearMarker);
    CHECK(flow.observe({}, 40) == Action::Done);
}

TEST_CASE("paste: an invalid target ends the flow before any write or keys") {
    Flow flow(Mode::Paste);
    CHECK(flow.observe({}, 0) == Action::ValidateTarget);
    CHECK(flow.observe({}, 0) == Action::LogFailure);
    CHECK(flow.failure() == Failure::InvalidTarget);
    CHECK_FALSE(flow.clipboardWritten());
    CHECK(flow.observe({}, 0) == Action::Done);
}

TEST_CASE("paste: focus polling stops at the 300 ms budget") {
    Flow flow(Mode::Paste);
    CHECK_EQ(flow.remainingMs(0), tapir::paste::kFocusTimeoutMs);
    CHECK(flow.observe({}, 0) == Action::ValidateTarget);
    CHECK(flow.observe(ValidTarget(), 0) == Action::RestoreDirect);
    CHECK(flow.observe(Restored(), 0) == Action::PollFocus);

    CHECK_EQ(flow.remainingMs(0), 300);
    CHECK_EQ(flow.remainingMs(200), 100);
    CHECK(flow.observe({}, 150) == Action::PollFocus);
    CHECK(flow.observe({}, 300) == Action::LogFailure);
    CHECK_EQ(flow.remainingMs(300), 0);
    CHECK(flow.failure() == Failure::TimeoutBeforeWrite);
    CHECK_FALSE(flow.clipboardWritten());
}

TEST_CASE("paste: both focus checks are required after the clipboard write") {
    Flow flow = ReadyToWrite();
    CHECK(flow.observe(Focused(), 10) == Action::WriteClipboard);

    Observation written = Written();
    CHECK(flow.observe(written, 20) == Action::PollFocus);

    Observation foregroundOnly;
    foregroundOnly.foregroundOnTarget = true;
    CHECK(flow.observe(foregroundOnly, 30) == Action::PollFocus);

    CHECK(flow.observe(Focused(), 40) == Action::SendCtrlV);
    CHECK(flow.observe(Sent(), 50) == Action::ClearMarker);
    CHECK(flow.observe({}, 50) == Action::Done);
}

TEST_CASE("paste: focus lost after the write leaves the item on the clipboard") {
    Flow flow = ReadyToWrite();
    CHECK(flow.observe(Focused(), 10) == Action::WriteClipboard);
    CHECK(flow.observe(Written(), 20) == Action::PollFocus);
    CHECK(flow.observe({}, 300) == Action::ClearMarker);
    CHECK(flow.failure() == Failure::TimeoutBeforeKeys);
    CHECK(flow.observe({}, 300) == Action::LogFailure);
    CHECK(flow.observe({}, 300) == Action::Done);
}

TEST_CASE("paste: a failed clipboard write sends no keys and needs no marker cleanup") {
    Flow flow = ReadyToWrite();
    CHECK(flow.observe(Focused(), 10) == Action::WriteClipboard);
    CHECK(flow.observe({}, 20) == Action::LogFailure);
    CHECK(flow.failure() == Failure::WriteFailed);
    CHECK(flow.observe({}, 20) == Action::Done);
}

TEST_CASE("paste: a failed key injection logs once and clears the marker") {
    Flow flow = ReadyToWrite();
    CHECK(flow.observe(Focused(), 10) == Action::WriteClipboard);
    CHECK(flow.observe(Written(), 20) == Action::PollFocus);
    CHECK(flow.observe(Focused(), 30) == Action::SendCtrlV);
    CHECK(flow.observe({}, 40) == Action::ClearMarker);
    CHECK(flow.failure() == Failure::KeysFailed);
    CHECK(flow.observe({}, 40) == Action::LogFailure);
    CHECK(flow.observe({}, 40) == Action::Done);
    CHECK(flow.observe({}, 40) == Action::Done);
}

TEST_CASE("paste: a failed attached restore detaches before logging") {
    Flow flow(Mode::Paste);
    flow.observe({}, 0);
    flow.observe(ValidTarget(), 0);
    flow.observe({}, 0);
    CHECK(flow.observe({}, 0) == Action::DetachThreads);
    CHECK_FALSE(flow.attached());
    CHECK(flow.observe({}, 0) == Action::LogFailure);
    CHECK(flow.failure() == Failure::RestoreFailed);
    CHECK(flow.observe({}, 0) == Action::Done);
}

TEST_CASE("paste: copy-only writes the clipboard and never sends keys") {
    Flow flow(Mode::CopyOnly);
    CHECK(flow.current() == Action::HidePopup);
    CHECK(flow.observe({}, 0) == Action::WriteClipboard);
    CHECK(flow.observe(Written(), 10) == Action::ClearMarker);
    CHECK(flow.observe({}, 10) == Action::Done);
    CHECK(flow.failure() == Failure::None);
}
