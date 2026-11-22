# Clipboard privacy formats — first-party research findings

**Scope and method.** Facts only. Sources are first-party: Microsoft Learn (Win32 and UWP/WinRT API reference) and the official Chromium source tree (first-party to Chromium/Google, explicitly labelled as such below). No blog posts, Q&A sites, or third-party writeups are used as authority.

## Summary

- Microsoft documents exactly three registered clipboard formats whose names are known to the system for opting copied data out of clipboard history and/or cloud sync: `ExcludeClipboardContentFromMonitorProcessing`, `CanIncludeInClipboardHistory`, and `CanUploadToCloudClipboard`. — https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats
- `ExcludeClipboardContentFromMonitorProcessing` is presence-only (any data); the other two carry a serialized `DWORD` where `0` means "do not include/sync" and `1` means "explicitly include/sync". — https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats
- A listener obtains numeric IDs with `RegisterClipboardFormat`, enumerates formats on an open clipboard with `EnumClipboardFormats`, resolves names with `GetClipboardFormatName`, and reads payloads with `GetClipboardData`. — https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerclipboardformatw , https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumclipboardformats , https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboardformatnamew , https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboarddata
- `WM_CLIPBOARDUPDATE` provides no source-process information: `wParam` and `lParam` are "not used and must be zero". — https://learn.microsoft.com/en-us/windows/win32/dataxchg/wm-clipboardupdate
- Source-process identification is a separate, best-effort lookup: `GetClipboardOwner` → `GetWindowThreadProcessId` → `QueryFullProcessImageName`. — https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboardowner , https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowthreadprocessid , https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-queryfullprocessimagenamea

## 1. Exact registered format names and where they are documented

**Fact:** Microsoft's "Clipboard Formats" page, section "Cloud Clipboard and Clipboard History Formats", names exactly three registered clipboard formats "whose names are known to the Windows system": `ExcludeClipboardContentFromMonitorProcessing`, `CanIncludeInClipboardHistory`, and `CanUploadToCloudClipboard`. — https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats

**Fact:** The same page frames them as an opt-out mechanism for the application placing data on the clipboard: "If you don't want the data your application places on the clipboard to be included in the clipboard history or synchronized with other devices, your application can control this behavior by placing data in certain registered clipboard formats whose names are known to the Windows system". — https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats

**Fact:** The page states that Cloud Clipboard "keeps a history of recent clipboard data items and can synchronize it between the user's devices". — https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats

**Fact:** These are *registered* clipboard formats, not standard/predefined ones. The same page says a registered format is one defined by an application, created with `RegisterClipboardFormat`, which "takes the name of the format and returns an unsigned integer value that represents the registered clipboard format"; `GetClipboardFormatName` retrieves the name from that value. — https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats

**Fact:** "If more than one application registers a clipboard format with exactly the same name, the clipboard format is registered only once. Both calls to the `RegisterClipboardFormat` function return the same value." — https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats

**Fact (first-party to Chromium, not Microsoft):** Chromium's official source registers the two DWORD formats by their exact names: `RegisterClipboardFormatChecked(L"CanIncludeInClipboardHistory")` and `RegisterClipboardFormatChecked(L"CanUploadToCloudClipboard")`. — https://chromium.googlesource.com/chromium/src/+/refs/heads/main/ui/base/clipboard/clipboard_format_type_win.cc#298 , https://chromium.googlesource.com/chromium/src/+/refs/heads/main/ui/base/clipboard/clipboard_format_type_win.cc#305

**Observation (first-party to Chromium):** The current Chromium `clipboard_win.cc` contains no occurrence of `ExcludeClipboardContentFromMonitorProcessing`; it sets only the two DWORD formats above. — https://chromium.googlesource.com/chromium/src/+/refs/heads/main/ui/base/clipboard/clipboard_win.cc

## 2. Value semantics: what means "disallow"

**Fact — `ExcludeClipboardContentFromMonitorProcessing`:** "Place any data on the clipboard in this format to prevent all clipboard formats being included in the clipboard history or synchronized to the user's other devices." The signal is the *presence* of the format with any data; no particular value is specified. — https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats

**Fact — `CanIncludeInClipboardHistory`:** "Place a serialized `DWORD` value of zero on the clipboard in this format to prevent all clipboard formats being included in the clipboard history, or place a value of one instead to explicitly request that the clipboard item be included in the clipboard history. This does not affect synchronization to the user's other devices." — https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats

**Fact — `CanUploadToCloudClipboard`:** "Place a serialized `DWORD` value of zero on the clipboard in this format to prevent all clipboard formats being synchronized to the user's other devices, or place a value of one instead to explicitly request that the clipboard item be synchronized to other devices. This does not affect the local device's clipboard history." — https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats

**Fact:** Registered clipboard formats "must be in the form of an `HGLOBAL` value" when placed on or retrieved from the clipboard. — https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerclipboardformatw

**Fact (first-party to Chromium):** Chromium writes `DWORD value = 0` into both `CanIncludeInClipboardHistory` and `CanUploadToCloudClipboard` via `WriteData`, whose implementation does `GlobalAlloc(GMEM_MOVEABLE, data.size())`, `GlobalLock`, `memcpy`, `GlobalUnlock`, then `SetClipboardData`. — https://chromium.googlesource.com/chromium/src/+/refs/heads/main/ui/base/clipboard/clipboard_win.cc#1208 , https://chromium.googlesource.com/chromium/src/+/refs/heads/main/ui/base/clipboard/clipboard_win.cc#1220 , https://chromium.googlesource.com/chromium/src/+/refs/heads/main/ui/base/clipboard/clipboard_win.cc#1228

**Fact (first-party to Chromium):** Chromium's `WriteConfidentialDataForPassword()` writes the same `DWORD 0` to both formats. — https://chromium.googlesource.com/chromium/src/+/refs/heads/main/ui/base/clipboard/clipboard_win.cc#1236

## 3. How a listener enumerates and reads these formats

**Fact:** `RegisterClipboardFormat(lpszFormat)` returns a `UINT` identifying the registered format, or zero on failure; format-name comparison is case-insensitive; registered formats are identified by values in the range `0xC000` through `0xFFFF`. — https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerclipboardformatw

**Fact:** `OpenClipboard` opens the clipboard for examination and prevents other applications from modifying it; it fails if another window has the clipboard open, and `CloseClipboard` must follow every successful call. — https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-openclipboard

**Fact:** `EnumClipboardFormats(0)` retrieves the first available format; subsequent calls pass the previous return value; the function fails if the clipboard is not open; a return of zero with `GetLastError() == ERROR_SUCCESS` means end of enumeration. — https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumclipboardformats

**Fact:** `GetClipboardFormatName(format, lpszFormatName, cchMaxCount)` retrieves the name of a registered format; it fails (returns zero) if the format is predefined; the string is truncated if longer than `cchMaxCount`. — https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboardformatnamew

**Fact:** `GetClipboardData(uFormat)` returns a `HANDLE` to the clipboard object; the clipboard controls the handle, the application should copy the data immediately, must not free the handle or leave it locked, and must not use it after `EmptyClipboard`, `CloseClipboard`, or `SetClipboardData` with the same format. The docs caution: "Clipboard data is not trusted. Parse the data carefully before using it in your application." — https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboarddata

**Fact:** A window that recognizes only one clipboard format can use `IsClipboardFormatAvailable` instead of enumerating; otherwise enumeration plus `GetClipboardData` is the documented paste path. — https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-operations

**Fact:** `AddClipboardFormatListener(hwnd)` places a window in the system-maintained listener list, and the window is then posted `WM_CLIPBOARDUPDATE` whenever clipboard contents change; `RemoveClipboardFormatListener` unregisters it. — https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-addclipboardformatlistener , https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-removeclipboardformatlistener

## 4. How applications set these formats in practice

### 4a. Microsoft first-party mechanisms

**Fact — Win32:** To place data on the clipboard, a window calls `OpenClipboard`, clears previous content with `EmptyClipboard` (which assigns clipboard ownership to the window that has the clipboard open), then calls `SetClipboardData` for each format with a global memory handle, then `CloseClipboard`. — https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-operations

**Fact — Win32:** `SetClipboardData(uFormat, hMem)` accepts a registered format in `uFormat`; if `hMem` identifies a memory object it "must have been allocated using the function with the `GMEM_MOVEABLE` flag"; a `NULL` `hMem` means delayed rendering. — https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setclipboarddata

**Fact — Win32:** A memory object placed on the clipboard should be allocated with `GlobalAlloc` using `GMEM_MOVEABLE`. — https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-operations

**Fact — UWP/WinRT:** `ClipboardContentOptions` exposes `IsAllowedInHistory` ("whether the new content being added to the clipboard is allowed in the clipboard history") and `IsRoamable` ("whether the new content being added to the clipboard can be synced to other devices"), plus `HistoryFormats` and `RoamingFormats`. — https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.datatransfer.clipboardcontentoptions?view=winrt-26100

**Fact — UWP/WinRT:** `Clipboard.SetContentWithOptions(DataPackage, ClipboardContentOptions)` sets clipboard content with those options; the `options` parameter may be null, in which case default options are used. — https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.datatransfer.clipboard.setcontentwithoptions?view=winrt-26100

**Fact — UWP/WinRT:** `Clipboard.SetContent` alone leaves content "eligible for clipboard history and can be synced to other devices"; to exclude it, use `SetContentWithOptions`. — https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.datatransfer.clipboard.setcontent?view=winrt-22621

### 4b. First-party to Chromium (not Microsoft)

**Fact:** Chromium's `WritePortableAndPlatformRepresentations` selects the privacy write based on `privacy_types`: `kNoDisplay` → `WriteConfidentialDataForPassword()`; otherwise `kNoLocalClipboardHistory` → `WriteClipboardHistory()` and `kNoCloudClipboard` → `WriteUploadCloudClipboard()`. — https://chromium.googlesource.com/chromium/src/+/refs/heads/main/ui/base/clipboard/clipboard_win.cc#1059

**Fact:** The in-source comment states: "On Windows, there is no special format to conceal passwords, but don't save it in the history or cloud clipboard for privacy reasons." — https://chromium.googlesource.com/chromium/src/+/refs/heads/main/ui/base/clipboard/clipboard_win.cc#1059

**Fact:** Chromium's `WriteToClipboard` calls `::SetClipboardData(cf_format, handle)` where `cf_format` comes from the registered `ClipboardFormatType`. — https://chromium.googlesource.com/chromium/src/+/refs/heads/main/ui/base/clipboard/clipboard_win.cc#1481

**Fact:** A Chromium commit titled "[Clipboard] Add privacy mode on Windows" describes the behavior as preventing clipboard data from being written into clipboard history and cloud clipboard "if the user is copying from an incognito window". — https://chromium.googlesource.com/chromium/src.git/+/5c87758eeb72ebad0c521a76486cb6d96b842478

**Explicit non-authority note:** Third-party writeups encountered during search (e.g., goframework.io, docs.rs) are not used as sources for any claim above.

## 5. What an ignore-list of process names can match via WM_CLIPBOARDUPDATE

**Fact:** `WM_CLIPBOARDUPDATE` is sent when clipboard contents change; `wParam` "is not used and must be zero" and `lParam` "is not used and must be zero". The message therefore carries no source-process identifier. — https://learn.microsoft.com/en-us/windows/win32/dataxchg/wm-clipboardupdate

**Fact:** `GetClipboardOwner()` returns the `HWND` of the current clipboard owner, or `NULL` if the clipboard is not owned; "The clipboard can still contain data even if the clipboard is not currently owned." — https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboardowner

**Fact:** "In general, the clipboard owner is the window that last placed data in clipboard. The `EmptyClipboard` function assigns clipboard ownership." — https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboardowner

**Fact:** "A window becomes the clipboard owner when it places data on the clipboard, specifically, when it calls the `EmptyClipboard` function. The window remains the clipboard owner until it is closed or another window empties the clipboard." — https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-operations

**Fact:** `GetWindowThreadProcessId(hWnd, lpdwProcessId)` retrieves the identifier of the thread that created the window and, optionally, the identifier of the process that created it; if `lpdwProcessId` is non-NULL, the process identifier is copied there. — https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowthreadprocessid

**Fact:** `QueryFullProcessImageName(hProcess, dwFlags, lpExeName, lpdwSize)` retrieves the full name of the executable image for a process; `hProcess` must be created with `PROCESS_QUERY_INFORMATION` or `PROCESS_QUERY_LIMITED_INFORMATION` access; `dwFlags = 0` yields the Win32 path format. — https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-queryfullprocessimagenamea

**Fact (first-party to Chromium):** Chromium's own clipboard monitor handles `WM_CLIPBOARDUPDATE` by calling `ClipboardMonitor::GetInstance()->NotifyClipboardDataChanged()` — i.e., it treats the message purely as a "something changed" notification, not as a source identifier. — https://chromium.googlesource.com/chromium/src/+/refs/heads/main/ui/base/clipboard/clipboard_win.cc#175

## Key facts

- Microsoft documents three system-known registered formats: `ExcludeClipboardContentFromMonitorProcessing` (presence-only, any data), `CanIncludeInClipboardHistory` (DWORD `0` = disallow local history, `1` = allow), and `CanUploadToCloudClipboard` (DWORD `0` = disallow cloud sync, `1` = allow). — https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats
- All three are registered formats: call `RegisterClipboardFormat` by exact name to get a numeric ID (range `0xC000`–`0xFFFF`), and the payload must be an `HGLOBAL`. — https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerclipboardformatw
- Reading them requires an open clipboard: `EnumClipboardFormats` to enumerate, `GetClipboardFormatName` to identify, `GetClipboardData` to read the DWORD; the clipboard owns the returned handle. — https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumclipboardformats , https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboarddata
- `WM_CLIPBOARDUPDATE` carries no source-process data (`wParam`/`lParam` unused and zero); process identification is a separate best-effort chain via `GetClipboardOwner` → `GetWindowThreadProcessId` → `QueryFullProcessImageName`, and the owner can be `NULL`. — https://learn.microsoft.com/en-us/windows/win32/dataxchg/wm-clipboardupdate , https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getclipboardowner , https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowthreadprocessid
- Chromium (first-party to Chromium, not Microsoft) writes `DWORD 0` to the two DWORD formats for password/incognito copies and does not set `ExcludeClipboardContentFromMonitorProcessing`. — https://chromium.googlesource.com/chromium/src/+/refs/heads/main/ui/base/clipboard/clipboard_win.cc#1236
