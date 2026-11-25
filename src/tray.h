#pragma once
#include <windows.h>
namespace tapir::tray {
enum class Event { None, LeftClick };
enum class Command : WPARAM { Animations = 1, Settings = 2, Restart = 3, Quit = 4 };
inline constexpr UINT kNotifyMessage = WM_APP + 3;   // tray callback -> owner hwnd
inline constexpr UINT kCommandMessage = WM_APP + 4;  // menu command -> owner hwnd
bool create(HWND owner, HICON icon);                 // NIM_ADD, tooltip "tapir"
void destroy();                                       // NIM_DELETE, destroy menu
Event handleNotify(WPARAM wParam, LPARAM lParam);    // called from owner WndProc on kNotifyMessage
void setAnimationsChecked(bool checked);             // updates menu checkmark
}
