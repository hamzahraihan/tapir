# Popup keyboard focus after a hotkey — first-party Win32 facts

**Research question (tapir issue #3):** "How can the popup reliably take keyboard focus after the hotkey?"

**Scope.** This document records documented facts only, sourced exclusively from first-party Microsoft documentation (Microsoft Learn / the Win32 SDK reference). It contains no design recommendations. Every factual claim carries an inline source link. Where the documentation is silent or internally inconsistent, that is stated explicitly rather than inferred.

---

## 1. `SetForegroundWindow` and the foreground lock

### 1.1 What `SetForegroundWindow` does

`SetForegroundWindow` "Brings the thread that created the specified window into the foreground and activates the window. Keyboard input is directed to the window, and various visual cues are changed for the user. The system assigns a slightly higher priority to the thread that created the foreground window than it does to other threads." ([SetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow))

Return value: nonzero if the window was brought to the foreground, zero if it was not. ([SetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow))

### 1.2 The documented "allow" list

The system restricts which processes can set the foreground window. A process can call `SetForegroundWindow` successfully only if **all** of the following are true ([SetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow); [AllowSetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-allowsetforegroundwindow)):

- The calling process belongs to a desktop application, not a UWP app or a Windows Store app designed for Windows 8 or 8.1.
- The foreground process has not disabled calls to `SetForegroundWindow` by a previous call to `LockSetForegroundWindow`.
- No menus are active.

And **at least one** of the following is true ([SetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow); [AllowSetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-allowsetforegroundwindow)):

- The foreground lock time-out has expired (see `SPI_GETFOREGROUNDLOCKTIMEOUT` in `SystemParametersInfo`).
- The calling process is the foreground process.
- The calling process was started by the foreground process.
- There is currently no foreground window, and thus no foreground process.
- The calling process received the last input event.
- Either the foreground process or the calling process is being debugged.

Two explicit caveats are documented:

- "It is possible for a process to be denied the right to set the foreground window even if it meets these conditions." ([SetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow))
- "An application cannot force a window to the foreground while the user is working with another window. Instead, Windows flashes the taskbar button of the window to notify the user." ([SetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow))

**Documentation discrepancy (fact about the docs):** the "Window Features" overview places "The foreground lock time-out has expired" inside the *all-of* group rather than the *at-least-one-of* group; both pages otherwise list the same conditions. ([Window Features, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features))

### 1.3 `LockSetForegroundWindow`

`LockSetForegroundWindow(uLockCode)` lets the foreground process disable calls to `SetForegroundWindow`. `LSFW_LOCK` (1) disables calls; `LSFW_UNLOCK` (2) enables them. ([LockSetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-locksetforegroundwindow))

"The system automatically enables calls to `SetForegroundWindow` if the user presses the ALT key or takes some action that causes the system itself to change the foreground window (for example, clicking a background window)." The function "is provided so applications can prevent other applications from making a foreground change that can interrupt its interaction with the user." ([LockSetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-locksetforegroundwindow))

### 1.4 `AllowSetForegroundWindow`

`AllowSetForegroundWindow(dwProcessId)` "Enables the specified process to set the foreground window using the `SetForegroundWindow` function. The calling process must already be able to set the foreground window." `ASFW_ANY` enables all processes. The function fails if the calling process cannot set the foreground window. ([AllowSetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-allowsetforegroundwindow))

The grant is transient: "The process specified by the *dwProcessId* parameter loses the ability to set the foreground window the next time that either the user generates input, unless the input is directed at that process, or the next time a process calls `AllowSetForegroundWindow`, unless the same process is specified as in the previous call to `AllowSetForegroundWindow`." ([AllowSetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-allowsetforegroundwindow))

The "Window Features" overview also documents `BroadcastSystemMessage` with the `BSF_ALLOWSFW` flag as a second way a process that can set the foreground window can enable another process to do so. ([Window Features, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features))

### 1.5 `SPI_GETFOREGROUNDLOCKTIMEOUT` / `SPI_SETFOREGROUNDLOCKTIMEOUT`

- `SPI_GETFOREGROUNDLOCKTIMEOUT` (0x2000): "Retrieves the amount of time following user input, in milliseconds, during which the system will not allow applications to force themselves into the foreground. The *pvParam* parameter must point to a `DWORD` variable that receives the time."
- `SPI_SETFOREGROUNDLOCKTIMEOUT` (0x2001): "Sets the amount of time following user input, in milliseconds, during which the system does not allow applications to force themselves into the foreground. Set *pvParam* to the new time-out value. The calling thread must be able to change the foreground window, otherwise the call fails."
- `SPI_GETFOREGROUNDFLASHCOUNT` (0x2004): "Retrieves the number of times `SetForegroundWindow` will flash the taskbar button when rejecting a foreground switch request."

([SystemParametersInfo, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-systemparametersinfoa))

---

## 2. `GetForegroundWindow` semantics

`GetForegroundWindow` "Retrieves a handle to the foreground window (the window with which the user is currently working). The system assigns a slightly higher priority to the thread that creates the foreground window than it does to other threads." ([GetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getforegroundwindow))

**NULL condition:** "The foreground window can be **NULL** in certain circumstances, such as when a window is losing activation." ([GetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getforegroundwindow))

**Thread/process relationship:** "The thread that created the window with which the user is currently working is called the foreground thread, and the window is called the *foreground window*. All other threads are background threads, and the windows created by background threads are called *background windows*." The user sets the foreground window by clicking a window or using ALT+TAB or ALT+ESC; to check whether an application window is foreground, compare the handle returned by `GetForegroundWindow` to that of the application window. ([Window Features, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features))

`GetWindowThreadProcessId(hWnd, lpdwProcessId)` "Retrieves the identifier of the thread that created the specified window and, optionally, the identifier of the process that created the window." The return value is the thread identifier; the process identifier is written through `lpdwProcessId` if it is non-NULL; the return value is zero if the window handle is invalid. ([GetWindowThreadProcessId, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowthreadprocessid))

Consequently, the documented way to identify the foreground thread is to pass the `GetForegroundWindow` handle to `GetWindowThreadProcessId`; `GetGUIThreadInfo` explicitly states "To retrieve this value [the thread identifier], use the `GetWindowThreadProcessId` function." ([GetGUIThreadInfo, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getguithreadinfo))

---

## 3. `AttachThreadInput` semantics

### 3.1 What attaching does

`AttachThreadInput(idAttach, idAttachTo, fAttach)` "Attaches or detaches the input processing mechanism of one thread to that of another thread." Neither thread may be a system thread; a thread cannot attach to itself (`idAttachTo` cannot equal `idAttach`); `fAttach` TRUE attaches and FALSE detaches. ([AttachThreadInput, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-attachthreadinput))

"By using the `AttachThreadInput` function, a thread can share its input states (such as keyboard states and the current focus window) with another thread. Keyboard and mouse events received by both threads are processed in the order they were received until the threads are detached by calling `AttachThreadInput` a second time and specifying **FALSE** for the *fAttach* parameter." ([AttachThreadInput, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-attachthreadinput))

"By sharing input state, the threads share their concept of the active window. By doing this, one thread can always activate another thread's window. This function is also useful for sharing focus state, mouse capture state, keyboard state, and window Z-order state among windows created by different threads whose input state is shared." ([Creating Windows in Threads, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/procthread/creating-windows-in-threads))

### 3.2 Documented failure conditions and caveats

- "The `AttachThreadInput` function fails if either of the specified threads does not have a message queue. The system creates a thread's message queue when the thread makes its first call to one of the USER or GDI functions." ([AttachThreadInput, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-attachthreadinput))
- "The `AttachThreadInput` function also fails if a journal record hook is installed. Journal record hooks attach all input queues together." ([AttachThreadInput, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-attachthreadinput))
- "Note that key state, which can be ascertained by calls to the `GetKeyState` or `GetKeyboardState` function, is reset after a call to `AttachThreadInput`." ([AttachThreadInput, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-attachthreadinput))
- "You cannot attach a thread to a thread in another desktop." ([AttachThreadInput, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-attachthreadinput))
- Windows XP / Server 2003: no extended error information; `GetLastError` should not be called. This changed as of Windows Vista. ([AttachThreadInput, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-attachthreadinput))

### 3.3 Why attaching enables cross-thread focus calls

`SetFocus` "Sets the keyboard focus to the specified window. The window must be attached to the calling thread's message queue." Its return value is NULL "if the *hWnd* parameter is invalid or the window is not attached to the calling thread's message queue"; extended error `ERROR_INVALID_PARAMETER` (0x57) means the window is disabled. The remarks state: "By using the `AttachThreadInput` function, a thread can attach its input processing to another thread. This allows a thread to call `SetFocus` to set the keyboard focus to a window attached to another thread's message queue." ([SetFocus, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setfocus))

`SetActiveWindow` likewise "Activates a window. The window must be attached to the calling thread's message queue." ([SetActiveWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setactivewindow))

`BringWindowToTop` "Brings the specified window to the top of the Z order. If the window is a top-level window, it is activated. If the window is a child window, the top-level parent window associated with the child window is activated." Calling it "is similar to calling the `SetWindowPos` function to change a window's position in the Z order." ([BringWindowToTop, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-bringwindowtotop))

### 3.4 Deadlock / shared-queue caveats

- "A thread that calls the `SendMessage` function to send a message to another thread cannot continue executing until the window procedure that receives the message returns. ... If the receiving thread is attached to the same queue as the sender, it can cause an application deadlock to occur. (Note that journal hooks attach threads to the same queue.)" ([About Messages and Message Queues, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/winmsg/about-messages-and-message-queues))
- A thread that creates a window "must provide a message loop to process the messages in its message queue. In addition, you must use `MsgWaitForMultipleObjects` or `MsgWaitForMultipleObjectsEx` in that thread, rather than the other wait functions, so that it can process messages. Otherwise, the system can become deadlocked when the thread is sent a message while it is waiting." ([Creating Windows in Threads, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/procthread/creating-windows-in-threads))

### 3.5 Integrity-level restriction on attaching

A first-party Windows security-policy reference lists, among the abilities of a process started with UIAccess rights: "Set the foreground window" and "Use `AttachThreadInput` to attach a thread to a higher integrity input queue." It also states that an application started without UIAccess rights "can interact only with applications at the same or lower privilege level." ([User Account Control: Only elevate UIAccess applications that are installed in secure locations, Microsoft Learn](https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-10/security/threat-protection/security-policy-settings/user-account-control-only-elevate-uiaccess-applications-that-are-installed-in-secure-locations))

---

## 4. `RegisterHotKey` behavior

`RegisterHotKey(hWnd, id, fsModifiers, vk)` "Defines a system-wide hot key." `hWnd` is "a handle to the window that will receive `WM_HOTKEY` messages generated by the hot key. If this parameter is **NULL**, `WM_HOTKEY` messages are posted to the message queue of the calling thread and must be processed in the message loop." If `hWnd` is NULL, "the hot key is associated with the current thread rather than with a particular window." The function "fails if you try to associate a hot key with a window created by another thread." ([RegisterHotKey, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey))

Delivery: "When a key is pressed, the system looks for a match against all hot keys. Upon finding a match, the system posts the `WM_HOTKEY` message to the message queue of the window with which the hot key is associated. If the hot key is not associated with a window, then the `WM_HOTKEY` message is posted to the thread associated with the hot key." ([RegisterHotKey, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey))

`WM_HOTKEY` "is placed at the top of the message queue associated with the thread that registered the hot key," bypassing any existing messages in the queue. ([WM_HOTKEY, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-hotkey); [Keyboard Input Overview, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-keyboard-input))

**What is NOT guaranteed by these docs.** The `RegisterHotKey`, `WM_HOTKEY`, and "Hot-Key Support" documentation specifies only *message delivery to a thread's message queue*. It does not state that pressing a `RegisterHotKey` hotkey makes the registering window the foreground window, activates it, or gives it keyboard focus. This is thrown into relief by the explicitly documented contrast with a different mechanism: `WM_SETHOTKEY` "Sent to a window to associate a hot key with the window. When the user presses the hot key, the system activates the window," and if passed to `DefWindowProc`, "the system will bring the window's last active popup (if it exists) or the window itself ... to the foreground." `WM_HOTKEY` "is unrelated to the `WM_GETHOTKEY` and `WM_SETHOTKEY` hot keys." ([WM_SETHOTKEY, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-sethotkey); [WM_HOTKEY, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-hotkey))

Additional documented registration facts: registration "typically ... fails if the keystrokes specified for the hot key have already been registered for another hot key," though some OS-default hotkeys may be overridden "when one of the app's windows is in the foreground"; F12 is reserved for the debugger at all times. ([RegisterHotKey, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey))

---

## 5. `WM_ACTIVATE` semantics

`WM_ACTIVATE` is "Sent to both the window being activated and the window being deactivated. If the windows use the same input queue, the message is sent synchronously, first to the window procedure of the top-level window being deactivated, then to the window procedure of the top-level window being activated. If the windows use different input queues, the message is sent asynchronously, so the window is activated immediately." ([WM_ACTIVATE, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-activate))

`wParam` low-order word ([WM_ACTIVATE, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-activate)):

| Value | Meaning |
|---|---|
| `WA_ACTIVE` (1) | "Activated by some method other than a mouse click (for example, by a call to the `SetActiveWindow` function or by use of the keyboard interface to select the window)." |
| `WA_CLICKACTIVE` (2) | "Activated by a mouse click." |
| `WA_INACTIVE` (0) | "Deactivated." |

The high-order word of `wParam` specifies the minimized state (nonzero = minimized). `lParam` is "a handle to the window being activated or deactivated, depending on the value of the *wParam* parameter": if `WA_INACTIVE`, it is the handle to the window being activated; if `WA_ACTIVE` or `WA_CLICKACTIVE`, it is the handle to the window being deactivated; this handle can be NULL. ([WM_ACTIVATE, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-activate))

"If the window is being activated and is not minimized, the `DefWindowProc` function sets the keyboard focus to the window. If the window is activated by a mouse click, it also receives a `WM_MOUSEACTIVATE` message." ([WM_ACTIVATE, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-activate))

**Deactivation is signaled** by `WM_ACTIVATE` with `WA_INACTIVE`. Separately, "When the activation changes from a top-level window of one application to the top-level window of another, the system sends a `WM_ACTIVATEAPP` message to both applications"; when activation changes to a different top-level window in the same application, both windows receive `WM_ACTIVATE`. ([Window Features, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features))

### 5.1 Active window vs foreground window vs keyboard focus

- **Active window:** "the top-level window of the application with which the user is currently working." Only a top-level window can be active; only one top-level window in the system is active at a time; the system places it at the top of the Z-order. `SetActiveWindow` "activates a window, but not if the application is in the background. The window will be brought into the foreground (top of Z-Order) if its application is in the foreground when the system activates the window." ([Window Features, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features); [SetActiveWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setactivewindow))
- **Foreground window:** the window with which the user is currently working; identified by `GetForegroundWindow`. ([GetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getforegroundwindow))
- **Keyboard focus:** "a temporary property of a window. The system shares the keyboard among all windows on the display by shifting the keyboard focus, at the user's direction, from one window to another. The window that has the keyboard focus receives (from the message queue of the thread that created it) all keyboard messages until the focus changes to a different window." "The system posts keyboard messages to the message queue of the foreground thread that created the window with the keyboard focus." "The window with the keyboard focus is either the active window, or a child window of the active window." ([Keyboard Input Overview, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-keyboard-input))

`GetActiveWindow` "Retrieves the window handle to the active window attached to the calling thread's message queue. Otherwise, the return value is **NULL**." For the foreground window use `GetForegroundWindow`; for the active window in another thread's queue use `GetGUIThreadInfo`. ([GetActiveWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getactivewindow))

---

## 6. Documented mechanisms that give a background-launched popup keyboard focus

Each entry below states only what the first-party documentation says the function does.

- **`SetForegroundWindow`** — "Brings the thread that created the specified window into the foreground and activates the window. Keyboard input is directed to the window." Subject to the restrictions in section 1. ([SetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow))
- **`AttachThreadInput` + `SetFocus` / `SetActiveWindow` / `BringWindowToTop`** — attaching shares the focus window and active-window state across threads; `SetFocus` and `SetActiveWindow` require the target window to be attached to the calling thread's message queue, and `AttachThreadInput` is the documented way to make that true for another thread's window. `BringWindowToTop` activates a top-level window and brings it to the top of the Z order. ([AttachThreadInput, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-attachthreadinput); [SetFocus, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setfocus); [SetActiveWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setactivewindow); [BringWindowToTop, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-bringwindowtotop); [Creating Windows in Threads, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/procthread/creating-windows-in-threads))
- **`AllowSetForegroundWindow`** — enables a specified process to set the foreground window; the caller must already be able to do so, and the grant is lost on the next user input not directed at that process (or the next non-matching `AllowSetForegroundWindow` call). ([AllowSetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-allowsetforegroundwindow))
- **`SetWindowPos(HWND_TOPMOST)`** — `HWND_TOPMOST` "Places the window above all non-topmost windows. The window maintains its topmost position even when it is deactivated." `SWP_NOACTIVATE` means "Does not activate the window"; without it, "the window is activated and moved to the top of either the topmost or non-topmost group." The docs state: "If an application is not in the foreground, and should be in the foreground, it must call the `SetForegroundWindow` function" and "To use `SetWindowPos` to bring a window to the top, the process that owns the window must have `SetForegroundWindow` permission." ([SetWindowPos, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos))
- **`ShowWindow`** — `SW_SHOW`, `SW_SHOWNORMAL`/`SW_NORMAL`, and `SW_RESTORE` "Activate[] and display[]" the window; `SW_SHOWNOACTIVATE`, `SW_SHOWNA`, and `SW_SHOWMINNOACTIVE` display it without activating it; `SW_HIDE` "Hides the window and activates another window." ([ShowWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showwindow))
- **`SwitchToThisWindow`** — documented as "Switches focus to the specified window and brings it to the foreground," but carries the explicit notice: "**This function is not intended for general use. It may be altered or unavailable in subsequent versions of Windows.**" `fUnknown` TRUE "indicates that the window is being switched to using the Alt/Ctl+Tab key sequence"; it "should be **FALSE** otherwise." "This function is typically called to maintain window z-ordering." ([SwitchToThisWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-switchtothiswindow))
- **`SendInput`** (relevant to the paste flow, not to acquiring focus) — "Synthesizes keystrokes, mouse motions, and button clicks." It "is subject to UIPI. Applications are permitted to inject input only into applications that are at an equal or lesser integrity level." The return value is the number of events inserted; zero means "the input was already blocked by another thread," and "neither `GetLastError` nor the return value will indicate the failure was caused by UIPI blocking." ([SendInput, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput))

---

## 7. How to verify that keystrokes actually reach the popup

- **`GetFocus`** — "Retrieves the handle to the window that has the keyboard focus, if the window is attached to the calling thread's message queue." If the calling thread's queue has no focus window, the return is NULL; "If `GetFocus` returns **NULL**, another thread's queue may be attached to a window that has the keyboard focus." *Proves:* which window has focus **within the calling thread's queue**. *Does not prove:* which window is foreground, or which window has focus in another thread's queue. ([GetFocus, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getfocus))
- **`GetForegroundWindow`** — returns the handle of the window with which the user is currently working; can be NULL while a window is losing activation. *Proves:* the identity of the foreground window. *Does not prove:* which window holds keyboard focus (the focused window may be a child of the active window). ([GetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getforegroundwindow); [Keyboard Input Overview, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-keyboard-input))
- **`GetGUIThreadInfo`** — "Retrieves information about the active window or a specified GUI thread." `idThread` is obtained from `GetWindowThreadProcessId`; if `idThread` is NULL, "the function returns information for the foreground thread." The `GUITHREADINFO` structure includes `hwndActive` ("A handle to the active window within the thread") and `hwndFocus` ("A handle to the window that has the keyboard focus"). The function "succeeds even if the active window is not owned by the calling process" and fails if the thread does not exist or has no input queue. Caveat: "The function may not return valid window handles in the `GUITHREADINFO` structure when called to retrieve information for the foreground thread, such as when a window is losing activation." *Proves:* out-of-context active/focus window for a specified or foreground thread. *Does not prove:* anything about a thread that has no input queue, and the handle values can be transiently invalid during activation changes. ([GetGUIThreadInfo, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getguithreadinfo); [GUITHREADINFO, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-guithreadinfo))
- **Observing `WM_KEYDOWN` / `WM_CHAR` in the popup's window procedure** — "Pressing a key causes a `WM_KEYDOWN` or `WM_SYSKEYDOWN` message to be placed in the thread message queue attached to the window that has the keyboard focus," and "The window that has the keyboard focus receives ... all keyboard messages until the focus changes to a different window." `WM_CHAR` is generated by `TranslateMessage` from `WM_KEYDOWN`. *Proves:* keyboard messages are being dispatched to that window procedure, i.e. that window has keyboard focus at that moment. *Does not prove:* that focus will persist, or anything about the foreground window independent of the focus relationship. ([Keyboard Input Overview, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-keyboard-input); [About Messages and Message Queues, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/winmsg/about-messages-and-message-queues))
- **`WM_SETFOCUS` / `WM_KILLFOCUS`** — "When the keyboard focus changes from one window to another, the system sends a `WM_KILLFOCUS` message to the window that has lost the focus, and then sends a `WM_SETFOCUS` message to the window that has gained the focus." ([Keyboard Input Overview, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-keyboard-input))

---

## 8. Conditions that make the foreground/focus workaround succeed or fail

All conditions below are stated in first-party documentation; no claim is made about which combination applies to any particular application.

**Foreground lock and last input**

- `SPI_GETFOREGROUNDLOCKTIMEOUT` is "the amount of time following user input, in milliseconds, during which the system will not allow applications to force themselves into the foreground." ([SystemParametersInfo, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-systemparametersinfoa))
- `SetForegroundWindow` is permitted when "the foreground lock time-out has expired" or "the calling process received the last input event." ([SetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow))
- "The system automatically enables calls to `SetForegroundWindow` if the user presses the ALT key or takes some action that causes the system itself to change the foreground window (for example, clicking a background window)." ([LockSetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-locksetforegroundwindow))

**Calling process / foreground process relationship**

- Permitted when the calling process is the foreground process, was started by the foreground process, or there is currently no foreground window; also when either process is being debugged. ([SetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow))
- Denied when the foreground process has called `LockSetForegroundWindow(LSFW_LOCK)`, or when menus are active. ([SetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow); [LockSetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-locksetforegroundwindow))
- A `SetForegroundWindow` grant from `AllowSetForegroundWindow` is lost on the next user input not directed at that process, or on the next non-matching `AllowSetForegroundWindow` call. ([AllowSetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-allowsetforegroundwindow))
- "It is possible for a process to be denied the right to set the foreground window even if it meets these conditions." ([SetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow))

**UAC / integrity level**

- "UIPI is a security feature that prevents messages from being received from a lower-integrity-level sender." ([ChangeWindowMessageFilterEx, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-changewindowmessagefilterex))
- "UIPI implements restrictions in the windows subsystem that prevents lower-privilege applications from sending window messages or installing hooks in higher-privilege processes. Higher-privilege applications are permitted to send window messages to lower-privilege processes." "The UI privilege level is at the process level and applies to all windows created by the process. ... After the USER subsystem sets the UI privilege level during process initialization, it does not change." "UIPI does not interfere with or change the behavior of window messaging between applications at the same privilege (or integrity) level." ([Windows Integrity Mechanism Design, Microsoft Learn](https://learn.microsoft.com/en-us/previous-versions/dotnet/articles/bb625963(v=msdn.10)))
- A process started with UIAccess rights can "Set the foreground window" and "Use `AttachThreadInput` to attach a thread to a higher integrity input queue"; a process started without UIAccess rights "can interact only with applications at the same or lower privilege level." ([UAC: Only elevate UIAccess applications installed in secure locations, Microsoft Learn](https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-10/security/threat-protection/security-policy-settings/user-account-control-only-elevate-uiaccess-applications-that-are-installed-in-secure-locations))
- `SendInput` is subject to UIPI: input may be injected only into applications at an equal or lesser integrity level. `SendMessage` and `PostMessage` are likewise restricted to threads in processes of lesser or equal integrity level. ([SendInput, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput); [SendMessage, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendmessage); [PostMessage, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-postmessagew))

**Session / desktop isolation**

- "You cannot attach a thread to a thread in another desktop." ([AttachThreadInput, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-attachthreadinput))
- "As part of the Vista re-architecture, all services were moved off the interactive desktop into Session 0. hwnd and window manager operations are only effective inside a session and cross-session attempts to manipulate the hwnd will fail." ([SetWindowPos, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos))

**Message queue / hook / window state**

- `AttachThreadInput` fails if either thread has no message queue (a queue is created on the thread's first USER or GDI call) or if a journal record hook is installed. ([AttachThreadInput, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-attachthreadinput))
- `SetFocus` returns NULL if the window is invalid or not attached to the calling thread's message queue; `ERROR_INVALID_PARAMETER` (0x57) means the window is disabled. ([SetFocus, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setfocus))
- `SetActiveWindow` "activates a window, but not if the application is in the background." ([SetActiveWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setactivewindow))
- A thread sharing an input queue with a `SendMessage` sender "can cause an application deadlock to occur"; a window-owning thread must use `MsgWaitForMultipleObjects`/`MsgWaitForMultipleObjectsEx` rather than other wait functions or "the system can become deadlocked when the thread is sent a message while it is waiting." ([About Messages and Message Queues, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/winmsg/about-messages-and-message-queues); [Creating Windows in Threads, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/procthread/creating-windows-in-threads))

---

## Open questions (facts not established from first-party docs)

1. **Default numeric value of the foreground lock timeout.** The `SystemParametersInfo` reference defines `SPI_GETFOREGROUNDLOCKTIMEOUT`/`SPI_SETFOREGROUNDLOCKTIMEOUT` and their units (milliseconds) but does not state a default value. ([SystemParametersInfo, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-systemparametersinfoa))
2. **Grouping of the foreground-lock-timeout condition.** `SetForegroundWindow` lists it under "at least one of the following," while `Window Features` lists it under "all of the following." Both list the condition; the docs disagree on its logical grouping. ([SetForegroundWindow, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow); [Window Features, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features))
3. **Hotkey → foreground/focus transition.** No first-party API reference for `RegisterHotKey` or `WM_HOTKEY` states that pressing a registered hotkey changes the foreground window or grants keyboard focus to the registering window; they document only queue delivery. The contrasting `WM_SETHOTKEY` mechanism is explicitly documented to activate and foreground its window. ([RegisterHotKey, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey); [WM_HOTKEY, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-hotkey); [WM_SETHOTKEY, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-sethotkey))
4. **Deadlock wording in the `AttachThreadInput` reference itself.** The `AttachThreadInput` API page documents queue sharing, ordering, and the journal-hook interaction but does not use the word "deadlock"; the deadlock caveat appears in the general messages-and-message-queues and creating-windows-in-threads topics. ([AttachThreadInput, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-attachthreadinput); [About Messages and Message Queues, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/winmsg/about-messages-and-message-queues))

---

## Sources

1. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow
2. https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features
3. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-locksetforegroundwindow
4. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-allowsetforegroundwindow
5. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-systemparametersinfoa
6. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getforegroundwindow
7. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowthreadprocessid
8. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-attachthreadinput
9. https://learn.microsoft.com/en-us/windows/win32/procthread/creating-windows-in-threads
10. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setfocus
11. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setactivewindow
12. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-bringwindowtotop
13. https://learn.microsoft.com/en-us/windows/win32/winmsg/about-messages-and-message-queues
14. https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-10/security/threat-protection/security-policy-settings/user-account-control-only-elevate-uiaccess-applications-that-are-installed-in-secure-locations
15. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey
16. https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-hotkey
17. https://learn.microsoft.com/en-us/windows/win32/inputdev/about-keyboard-input
18. https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-sethotkey
19. https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-activate
20. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getactivewindow
21. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos
22. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showwindow
23. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-switchtothiswindow
24. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput
25. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getfocus
26. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getguithreadinfo
27. https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-guithreadinfo
28. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-changewindowmessagefilterex
29. https://learn.microsoft.com/en-us/previous-versions/dotnet/articles/bb625963(v=msdn.10)
30. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendmessage
31. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-postmessagew
