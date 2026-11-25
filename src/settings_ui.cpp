#include "settings_ui.h"

#include "constants.h"
#include "hotkey.h"

#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tapir::settings {
namespace {

using Microsoft::WRL::ComPtr;

enum class Kind { Bool, Int, Float, Text, Hotkey, Color, Cycle, List };

struct Field {
    const char* key;
    const wchar_t* label;
    int tab;
    Kind kind;
};

constexpr Field kFields[] = {
    {"general.version", L"Config version", 0, Kind::Int},
    {"general.hotkey", L"Hotkey", 0, Kind::Hotkey},
    {"general.animations", L"Animations", 0, Kind::Bool},
    {"general.start_with_windows", L"Start with Windows", 0, Kind::Bool},
    {"theme.backdrop", L"Backdrop", 1, Kind::Cycle},
    {"theme.backdrop_opacity", L"Backdrop opacity", 1, Kind::Float},
    {"theme.font", L"Font", 1, Kind::Text},
    {"theme.background", L"Background", 1, Kind::Color},
    {"theme.foreground", L"Foreground", 1, Kind::Color},
    {"theme.accent", L"Accent", 1, Kind::Color},
    {"theme.muted", L"Muted", 1, Kind::Color},
    {"theme.surface", L"Surface", 1, Kind::Color},
    {"theme.selection", L"Selection", 1, Kind::Color},
    {"window.monitor", L"Monitor", 2, Kind::Cycle},
    {"window.position", L"Position", 2, Kind::Cycle},
    {"window.width", L"Width (DIP)", 2, Kind::Int},
    {"window.compact", L"Compact mode", 2, Kind::Bool},
    {"window.max_visible_rows", L"Max visible rows", 2, Kind::Int},
    {"window.details_pane", L"Details pane", 2, Kind::Bool},
    {"history.max_entries", L"Max entries", 3, Kind::Int},
    {"history.max_entry_bytes", L"Max entry bytes", 3, Kind::Int},
    {"history.ignore_processes", L"Ignored processes", 3, Kind::List},
    {"index.sources", L"Index sources", 4, Kind::List},
    {"privacy.encrypt_history", L"Encrypt history", 4, Kind::Bool},
};
constexpr int kFieldCount = static_cast<int>(std::size(kFields));

constexpr const wchar_t* kTabs[] = {L"General", L"Appearance", L"Window", L"History",
                                    L"Advanced"};
constexpr float kTabWidths[] = {88.0f, 104.0f, 76.0f, 80.0f, 88.0f};
constexpr int kTabCount = static_cast<int>(std::size(kTabs));

constexpr int kFocusTabs = -1;
constexpr int kFocusOk = -2;
constexpr int kFocusCancel = -3;
constexpr float kRowHeight = 34.0f;
constexpr float kRowGap = 4.0f;

struct State {
    HWND hwnd = nullptr;
    DoneCallback done = nullptr;
    Settings draft;
    std::wstring values[kFieldCount];
    std::wstring edit;
    std::size_t caret = 0;
    int tab = 0;
    int focus = kFocusTabs;
    int hover = -999;
    bool recording = false;
    bool trackingMouse = false;
    bool closing = false;
    unsigned dpi = 96;
    std::wstring error;

    ComPtr<ID2D1Factory> factory;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<IDWriteFactory> write;
    ComPtr<IDWriteTextFormat> labelFormat;
    ComPtr<IDWriteTextFormat> valueFormat;
    ComPtr<IDWriteTextFormat> smallFormat;
    ComPtr<ID2D1SolidColorBrush> backgroundBrush;
    ComPtr<ID2D1SolidColorBrush> surfaceBrush;
    ComPtr<ID2D1SolidColorBrush> textBrush;
    ComPtr<ID2D1SolidColorBrush> mutedBrush;
    ComPtr<ID2D1SolidColorBrush> accentBrush;
    ComPtr<ID2D1SolidColorBrush> focusBorderBrush;
    ComPtr<ID2D1SolidColorBrush> mutedBorderBrush;
};

std::optional<State> g_state;

const wchar_t* class_name() noexcept { return L"tapir-settings-window"; }

std::string toUtf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0,
                                           nullptr, nullptr);
    std::string out(static_cast<std::size_t>(length > 0 ? length : 0), '\0');
    if (length > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                            out.data(), length, nullptr, nullptr);
    }
    return out;
}

std::wstring trim(std::wstring_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == L' ' || text[begin] == L'\t')) ++begin;
    while (end > begin && (text[end - 1] == L' ' || text[end - 1] == L'\t')) --end;
    return std::wstring(text.substr(begin, end - begin));
}

std::vector<std::wstring> splitList(std::wstring_view text) {
    std::vector<std::wstring> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(L',', start);
        const std::wstring item = trim(text.substr(
            start, comma == std::wstring_view::npos ? std::wstring_view::npos : comma - start));
        if (!item.empty()) out.push_back(item);
        if (comma == std::wstring_view::npos) break;
        start = comma + 1;
    }
    return out;
}

std::wstring joinList(const std::vector<std::wstring>& items) {
    std::wstring out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) out += L", ";
        out += items[i];
    }
    return out;
}

std::wstring hexColor(Rgb color) {
    wchar_t buffer[8]{};
    std::swprintf(buffer, std::size(buffer), L"#%02x%02x%02x", color.r, color.g, color.b);
    return buffer;
}

bool parseHexColor(std::wstring_view text, Rgb& out) {
    if (text.size() != 7 || text.front() != L'#') return false;
    unsigned value = 0;
    for (std::size_t i = 1; i < 7; ++i) {
        const wchar_t c = text[i];
        unsigned digit = 0;
        if (c >= L'0' && c <= L'9') digit = static_cast<unsigned>(c - L'0');
        else if (c >= L'a' && c <= L'f') digit = static_cast<unsigned>(c - L'a') + 10;
        else if (c >= L'A' && c <= L'F') digit = static_cast<unsigned>(c - L'A') + 10;
        else return false;
        value = value * 16 + digit;
    }
    out = Rgb{static_cast<std::uint8_t>((value >> 16) & 0xff),
              static_cast<std::uint8_t>((value >> 8) & 0xff),
              static_cast<std::uint8_t>(value & 0xff)};
    return true;
}

D2D1_COLOR_F toColor(Rgb rgb, float alpha = 1.0f) {
    return D2D1::ColorF(rgb.r / 255.0f, rgb.g / 255.0f, rgb.b / 255.0f, alpha);
}

std::string quoted(std::wstring_view text) {
    std::wstring out = L"\"";
    for (const wchar_t c : text) {
        if (c == L'\\' || c == L'"') out += L'\\';
        out += c;
    }
    out += L'"';
    return toUtf8(out);
}

std::string arrayLiteral(const std::vector<std::wstring>& items) {
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) out += ", ";
        out += quoted(items[i]);
    }
    out += "]";
    return out;
}

void cycleOptions(int field, std::vector<std::wstring>& out) {
    switch (field) {
    case 4:
        out = {L"cursor", L"active", L"primary"};
        for (int i = 1; i <= 8; ++i) out.push_back(std::to_wstring(i));
        break;
    case 14:
        out = {L"upper-center", L"center"};
        break;
    case 22:
        out = {L"appsfolder", L"startmenu"};
        break;
    default:
        out = {L"acrylic", L"mica", L"none"};
        break;
    }
}

void initializeValues(State& state) {
    const Settings& s = state.draft;
    state.values[0] = std::to_wstring(s.general.version);
    state.values[1] = s.general.hotkey;
    state.values[2] = s.general.animations ? L"true" : L"false";
    state.values[3] = s.general.startWithWindows ? L"true" : L"false";
    state.values[4] = s.theme.backdrop == Backdrop::Acrylic
                          ? L"acrylic"
                          : (s.theme.backdrop == Backdrop::Mica ? L"mica" : L"none");
    state.values[5] = std::to_wstring(s.theme.backdropOpacity);
    state.values[6] = s.theme.font;
    state.values[7] = hexColor(s.theme.background);
    state.values[8] = hexColor(s.theme.foreground);
    state.values[9] = hexColor(s.theme.accent);
    state.values[10] = hexColor(s.theme.muted);
    state.values[11] = hexColor(s.theme.surface);
    state.values[12] = hexColor(s.theme.selection);
    state.values[13] = s.window.monitor.kind == Monitor::Cursor
                           ? L"cursor"
                           : (s.window.monitor.kind == Monitor::Active
                                  ? L"active"
                                  : (s.window.monitor.kind == Monitor::Primary
                                         ? L"primary"
                                         : std::to_wstring(s.window.monitor.index)));
    state.values[14] = s.window.position == WindowPosition::Center ? L"center" : L"upper-center";
    state.values[15] = std::to_wstring(s.window.widthDip);
    state.values[16] = s.window.compact ? L"true" : L"false";
    state.values[17] = std::to_wstring(s.window.maxVisibleRows);
    state.values[18] = s.window.detailsPane ? L"true" : L"false";
    state.values[19] = std::to_wstring(s.history.maxEntries);
    state.values[20] = std::to_wstring(s.history.maxEntryBytes);
    state.values[21] = joinList(s.history.ignoreProcesses);
    state.values[22] = joinList(s.index.sources);
    state.values[23] = s.privacy.encryptHistory ? L"true" : L"false";
}

bool ensureBrushes(State& state) {
    if (state.backgroundBrush != nullptr) return true;
    const Settings::Theme& theme = state.draft.theme;
    const auto make = [&](D2D1_COLOR_F color, ComPtr<ID2D1SolidColorBrush>& brush) {
        return SUCCEEDED(state.target->CreateSolidColorBrush(color, brush.GetAddressOf()));
    };
    return make(toColor(theme.background), state.backgroundBrush) &&
           make(toColor(theme.surface, 0.72f), state.surfaceBrush) &&
           make(toColor(theme.foreground), state.textBrush) &&
           make(toColor(theme.muted), state.mutedBrush) &&
           make(toColor(theme.accent), state.accentBrush) &&
           make(toColor(theme.accent), state.focusBorderBrush) &&
           make(toColor(theme.muted, 0.45f), state.mutedBorderBrush);
}

bool recreateTarget(State& state) {
    if (state.hwnd == nullptr || state.factory == nullptr) return false;
    RECT rect{};
    GetClientRect(state.hwnd, &rect);
    const D2D1_SIZE_U size{static_cast<UINT32>(std::max(1L, rect.right - rect.left)),
                           static_cast<UINT32>(std::max(1L, rect.bottom - rect.top))};
    if (FAILED(state.factory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(state.hwnd, size),
            state.target.ReleaseAndGetAddressOf()))) {
        return false;
    }
    state.target->SetDpi(static_cast<float>(state.dpi), static_cast<float>(state.dpi));
    state.backgroundBrush.Reset();
    state.surfaceBrush.Reset();
    state.textBrush.Reset();
    state.mutedBrush.Reset();
    state.accentBrush.Reset();
    state.focusBorderBrush.Reset();
    state.mutedBorderBrush.Reset();
    return ensureBrushes(state);
}

bool createResources(State& state) {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 state.factory.GetAddressOf()))) {
        return false;
    }
    RECT rect{};
    GetClientRect(state.hwnd, &rect);
    const D2D1_SIZE_U size{static_cast<UINT32>(std::max(1L, rect.right - rect.left)),
                           static_cast<UINT32>(std::max(1L, rect.bottom - rect.top))};
    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
    const D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps =
        D2D1::HwndRenderTargetProperties(state.hwnd, size);
    if (FAILED(state.factory->CreateHwndRenderTarget(props, hwndProps,
                                                     state.target.GetAddressOf()))) {
        return false;
    }
    state.target->SetDpi(static_cast<float>(state.dpi), static_cast<float>(state.dpi));
    if (!ensureBrushes(state)) return false;
    ComPtr<IUnknown> raw;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(raw.GetAddressOf())))) {
        return false;
    }
    if (FAILED(raw.As(&state.write))) return false;

    const std::wstring& family = state.draft.theme.font;
    const wchar_t* chosen = family.empty() ? L"Segoe UI" : family.c_str();
    const auto make = [&](float size, DWRITE_FONT_WEIGHT weight,
                          ComPtr<IDWriteTextFormat>& format) {
        return SUCCEEDED(state.write->CreateTextFormat(
            chosen, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            size, L"en-US", format.GetAddressOf()));
    };
    return make(12.0f, DWRITE_FONT_WEIGHT_NORMAL, state.labelFormat) &&
           make(12.0f, DWRITE_FONT_WEIGHT_NORMAL, state.valueFormat) &&
           make(11.0f, DWRITE_FONT_WEIGHT_NORMAL, state.smallFormat);
}

struct Layout {
    float width = 0.0f;
    float height = 0.0f;
    float tabX = 16.0f;
    float tabY = 10.0f;
    float tabHeight = 32.0f;
    float contentX = 16.0f;
    float contentY = 54.0f;
    float contentWidth = 0.0f;
    float contentHeight = 0.0f;
    float labelX = 28.0f;
    float controlX = 0.0f;
    float controlWidth = 0.0f;
    float footerY = 0.0f;
};

Layout layout(State& state) {
    Layout result;
    if (state.target != nullptr) {
        const D2D1_SIZE_F size = state.target->GetSize();
        result.width = size.width;
        result.height = size.height;
    }
    result.contentWidth = std::max(10.0f, result.width - 32.0f);
    result.contentHeight = std::max(10.0f, result.height - 112.0f);
    result.controlX = result.contentX + 206.0f;
    result.controlWidth = std::max(60.0f, result.width - result.controlX - 28.0f);
    result.footerY = result.height - 52.0f;
    return result;
}

float tabLeft(const Layout& layout, int index) {
    float x = layout.tabX;
    for (int i = 0; i < index; ++i) x += kTabWidths[i] + 4.0f;
    return x;
}

int rowCount(int tab) {
    int count = 0;
    for (const Field& field : kFields) {
        if (field.tab == tab) ++count;
    }
    return count;
}

D2D1_RECT_F rowRect(const Layout& layout, int row) {
    const float y = layout.contentY + 12.0f + row * (kRowHeight + kRowGap);
    return D2D1::RectF(layout.controlX, y, layout.controlX + layout.controlWidth,
                       y + kRowHeight);
}

int fieldAtPoint(State& state, float x, float y, const Layout& layout) {
    int row = 0;
    for (int i = 0; i < kFieldCount; ++i) {
        if (kFields[i].tab != state.tab) continue;
        const D2D1_RECT_F rect = rowRect(layout, row++);
        if (x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom) return i;
    }
    return -1;
}

int tabAtPoint(float x, float y, const Layout& layout) {
    if (y < layout.tabY || y > layout.tabY + layout.tabHeight) return -1;
    for (int i = 0; i < kTabCount; ++i) {
        const float left = tabLeft(layout, i);
        if (x >= left && x <= left + kTabWidths[i]) return i;
    }
    return -1;
}

void drawText(ID2D1RenderTarget* target, IDWriteTextFormat* format, const std::wstring& text,
              const D2D1_RECT_F& rect, ID2D1Brush* brush) {
    if (text.empty()) return;
    target->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format, rect, brush,
                     D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void draw(State& state) {
    if (state.target == nullptr && !recreateTarget(state)) return;
    const Layout lay = layout(state);
    const Rgb& background = state.draft.theme.background;

    if (!ensureBrushes(state)) return;
    ComPtr<ID2D1SolidColorBrush>& backgroundBrush = state.backgroundBrush;
    ComPtr<ID2D1SolidColorBrush>& surfaceBrush = state.surfaceBrush;
    ComPtr<ID2D1SolidColorBrush>& textBrush = state.textBrush;
    ComPtr<ID2D1SolidColorBrush>& mutedBrush = state.mutedBrush;
    ComPtr<ID2D1SolidColorBrush>& accentBrush = state.accentBrush;

    state.target->BeginDraw();
    state.target->Clear(toColor(background));
    state.target->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(lay.contentX, lay.contentY - 4.0f,
                                      lay.contentX + lay.contentWidth,
                                      lay.footerY - 8.0f),
                          6.0f, 6.0f),
        surfaceBrush.Get());

    for (int i = 0; i < kTabCount; ++i) {
        const float left = tabLeft(lay, i);
        const D2D1_RECT_F rect = D2D1::RectF(left, lay.tabY, left + kTabWidths[i],
                                             lay.tabY + lay.tabHeight);
        if (i == state.tab || state.hover == kFocusTabs - 100 - i) {
            state.target->FillRoundedRectangle(D2D1::RoundedRect(rect, 5.0f, 5.0f),
                                          surfaceBrush.Get());
        }
        if (i == state.tab) {
            state.target->FillRectangle(
                D2D1::RectF(rect.left + 8.0f, rect.bottom - 3.0f, rect.right - 8.0f,
                            rect.bottom),
                accentBrush.Get());
        }
        drawText(state.target.Get(), state.valueFormat.Get(), kTabs[i], rect,
                 (i == state.tab) ? textBrush.Get() : mutedBrush.Get());
    }

    int row = 0;
    for (int i = 0; i < kFieldCount; ++i) {
        if (kFields[i].tab != state.tab) continue;
        const D2D1_RECT_F rect = rowRect(lay, row++);
        const D2D1_RECT_F labelRect = D2D1::RectF(lay.labelX, rect.top,
                                                  lay.controlX - 10.0f, rect.bottom);
        drawText(state.target.Get(), state.labelFormat.Get(), kFields[i].label, labelRect,
                 mutedBrush.Get());
        const bool focused = state.focus == i;
        ID2D1SolidColorBrush* borderBrush =
            focused ? state.focusBorderBrush.Get() : state.mutedBorderBrush.Get();
        const D2D1_RECT_F frame = D2D1::RectF(rect.left, rect.top + 3.0f, rect.right,
                                              rect.bottom - 3.0f);

        switch (kFields[i].kind) {
        case Kind::Bool: {
            const D2D1_RECT_F box = D2D1::RectF(rect.left + 4.0f, frame.top + 4.0f,
                                                rect.left + 24.0f, frame.bottom - 4.0f);
            const bool on = state.values[i] == L"true";
            if (on) state.target->FillRoundedRectangle(D2D1::RoundedRect(box, 4.0f, 4.0f),
                                                  accentBrush.Get());
            else state.target->DrawRoundedRectangle(D2D1::RoundedRect(box, 4.0f, 4.0f),
                                               borderBrush, 1.0f);
            drawText(state.target.Get(), state.valueFormat.Get(), on ? L"Enabled" : L"Disabled",
                     D2D1::RectF(box.right + 10.0f, frame.top, rect.right, frame.bottom),
                     textBrush.Get());
            break;
        }
        case Kind::Cycle: {
            state.target->FillRoundedRectangle(D2D1::RoundedRect(frame, 5.0f, 5.0f),
                                          surfaceBrush.Get());
            state.target->DrawRoundedRectangle(D2D1::RoundedRect(frame, 5.0f, 5.0f),
                                          borderBrush, 1.0f);
            D2D1_RECT_F textRect = frame;
            if (i == 13) textRect.right -= 28.0f;
            drawText(state.target.Get(), state.valueFormat.Get(), state.values[i], textRect,
                     textBrush.Get());
            if (i == 13) {
                drawText(state.target.Get(), state.smallFormat.Get(), L"< >",
                         D2D1::RectF(frame.right - 32.0f, frame.top, frame.right, frame.bottom),
                         mutedBrush.Get());
            }
            break;
        }
        case Kind::Hotkey: {
            const float buttonWidth = 76.0f;
            const D2D1_RECT_F field = D2D1::RectF(frame.left, frame.top,
                                                  frame.right - buttonWidth - 8.0f, frame.bottom);
            state.target->FillRoundedRectangle(D2D1::RoundedRect(field, 5.0f, 5.0f),
                                          surfaceBrush.Get());
            state.target->DrawRoundedRectangle(D2D1::RoundedRect(field, 5.0f, 5.0f),
                                          borderBrush, 1.0f);
            const std::wstring shown = state.recording ? L"Press keys..." : state.values[i];
            drawText(state.target.Get(), state.valueFormat.Get(), shown, field, textBrush.Get());
            const D2D1_RECT_F button = D2D1::RectF(frame.right - buttonWidth, frame.top,
                                                   frame.right, frame.bottom);
            state.target->FillRoundedRectangle(D2D1::RoundedRect(button, 5.0f, 5.0f),
                                          surfaceBrush.Get());
            drawText(state.target.Get(), state.smallFormat.Get(),
                     state.recording ? L"Recording" : L"Record", button, accentBrush.Get());
            break;
        }
        default: {
            state.target->FillRoundedRectangle(D2D1::RoundedRect(frame, 5.0f, 5.0f),
                                          surfaceBrush.Get());
            state.target->DrawRoundedRectangle(D2D1::RoundedRect(frame, 5.0f, 5.0f),
                                          borderBrush, 1.0f);
            const std::wstring& shown = (focused && state.focus == i) ? state.edit : state.values[i];
            drawText(state.target.Get(), state.valueFormat.Get(), shown,
                     D2D1::RectF(frame.left + 8.0f, frame.top, frame.right - 8.0f, frame.bottom),
                     textBrush.Get());
            if (focused && state.focus == i && !state.recording) {
                const float caretX = frame.left + 8.0f +
                    static_cast<float>(state.caret) * 6.5f;
                if (caretX < frame.right - 6.0f) {
                    state.target->FillRectangle(
                        D2D1::RectF(caretX, frame.top + 4.0f, caretX + 1.0f, frame.bottom - 4.0f),
                        accentBrush.Get());
                }
            }
            break;
        }
        }
    }

    drawText(state.target.Get(), state.smallFormat.Get(), state.error,
             D2D1::RectF(lay.contentX, lay.footerY + 4.0f, lay.width - 196.0f, lay.height - 6.0f),
             accentBrush.Get());
    const D2D1_RECT_F cancel = D2D1::RectF(lay.width - 176.0f, lay.footerY,
                                           lay.width - 96.0f, lay.footerY + 32.0f);
    const D2D1_RECT_F ok = D2D1::RectF(lay.width - 88.0f, lay.footerY,
                                       lay.width - 16.0f, lay.footerY + 32.0f);
    state.target->FillRoundedRectangle(D2D1::RoundedRect(cancel, 5.0f, 5.0f), surfaceBrush.Get());
    state.target->FillRoundedRectangle(D2D1::RoundedRect(ok, 5.0f, 5.0f), accentBrush.Get());
    drawText(state.target.Get(), state.valueFormat.Get(), L"Cancel", cancel, textBrush.Get());
    drawText(state.target.Get(), state.valueFormat.Get(), L"OK", ok, backgroundBrush.Get());

    const HRESULT result = state.target->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) {
        state.target.Reset();
        InvalidateRect(state.hwnd, nullptr, FALSE);
    }
}

void destroyWindow() {
    if (!g_state.has_value()) return;
    State& state = *g_state;
    state.closing = true;
    const HWND hwnd = state.hwnd;
    state.hwnd = nullptr;
    if (hwnd != nullptr) DestroyWindow(hwnd);
    g_state.reset();
}

int validate(State& state) {
    for (int i = 0; i < kFieldCount; ++i) {
        const std::wstring text = trim(state.values[i]);
        switch (kFields[i].kind) {
        case Kind::Bool:
        case Kind::Cycle:
            break;
        case Kind::Int: {
            std::size_t used = 0;
            int value = 0;
            try {
                value = std::stoi(text, &used);
            } catch (...) {
                state.error = L"Enter a whole number.";
                return i;
            }
            if (used != text.size()) {
                state.error = L"Enter a whole number.";
                return i;
            }
            if (i == 0) {
                if (value != 1) {
                    state.error = L"Config version must be 1.";
                    return i;
                }
            } else if (value <= 0) {
                state.error = L"Enter a number greater than zero.";
                return i;
            }
            break;
        }
        case Kind::Float: {
            std::size_t used = 0;
            float value = 0.0f;
            try {
                value = std::stof(text, &used);
            } catch (...) {
                state.error = L"Enter a number from 0.0 to 1.0.";
                return i;
            }
            if (used != text.size() || value < 0.0f || value > 1.0f) {
                state.error = L"Enter a number from 0.0 to 1.0.";
                return i;
            }
            break;
        }
        case Kind::Color: {
            Rgb parsed{};
            if (!parseHexColor(text, parsed)) {
                state.error = L"Use a color like #1e1e2e.";
                return i;
            }
            break;
        }
        case Kind::Hotkey:
            if (!hotkey::parse(text).has_value()) {
                state.error = L"Use Ctrl, Alt, Shift, or Win with one key.";
                return i;
            }
            break;
        case Kind::Text:
            if (text.empty()) {
                state.error = L"This field cannot be empty.";
                return i;
            }
            break;
        case Kind::List: {
            const auto items = splitList(text);
            if (i == 22) {
                for (const std::wstring& item : items) {
                    if (item != L"appsfolder" && item != L"startmenu") {
                        state.error = L"Use appsfolder and startmenu.";
                        return i;
                    }
                }
            }
            break;
        }
        }
    }
    state.error.clear();
    return -1;
}

void commitEdit(State& state) {
    if (state.focus >= 0 && state.focus < kFieldCount &&
        kFields[state.focus].kind != Kind::Bool && kFields[state.focus].kind != Kind::Cycle) {
        state.values[state.focus] = state.edit;
    }
}

void setFocus(State& state, int focus) {
    if (state.focus == focus) return;
    commitEdit(state);
    state.focus = focus;
    state.edit.clear();
    state.caret = 0;
    if (focus >= 0 && focus < kFieldCount &&
        kFields[focus].kind != Kind::Bool && kFields[focus].kind != Kind::Cycle) {
        state.edit = state.values[focus];
        state.caret = state.edit.size();
    }
    InvalidateRect(state.hwnd, nullptr, FALSE);
}

bool isEditable(const State& state, int focus) {
    return focus >= 0 && focus < kFieldCount && kFields[focus].tab == state.tab &&
           kFields[focus].kind != Kind::Bool && kFields[focus].kind != Kind::Cycle;
}

void cycleField(State& state, int field, int step) {
    std::vector<std::wstring> options;
    cycleOptions(field, options);
    if (options.empty()) return;
    const auto found = std::find(options.begin(), options.end(), state.values[field]);
    std::size_t index = 0;
    if (found != options.end()) {
        index = static_cast<std::size_t>(found - options.begin());
    }
    index = (index + static_cast<std::size_t>(step) + options.size()) % options.size();
    state.values[field] = options[index];
    InvalidateRect(state.hwnd, nullptr, FALSE);
}

void accept(State& state) {
    commitEdit(state);
    const int invalid = validate(state);
    if (invalid >= 0) {
        state.tab = kFields[invalid].tab;
        setFocus(state, invalid);
        InvalidateRect(state.hwnd, nullptr, FALSE);
        return;
    }
    Result result;
    result.ok = true;
    for (int i = 0; i < kFieldCount; ++i) {
        config_writer::Value value;
        value.key = kFields[i].key;
        const std::wstring text = trim(state.values[i]);
        switch (kFields[i].kind) {
        case Kind::Bool:
            value.text = toUtf8(text);
            break;
        case Kind::Int:
        case Kind::Float:
            value.text = toUtf8(text);
            break;
        case Kind::Hotkey:
        case Kind::Text:
        case Kind::Color:
            value.text = quoted(text);
            break;
        case Kind::Cycle:
            if (i == 13 && !text.empty() && text.front() >= L'0' && text.front() <= L'9') {
                value.text = toUtf8(text);
            } else {
                value.text = quoted(text);
            }
            break;
        case Kind::List:
            value.text = arrayLiteral(splitList(text));
            break;
        }
        result.values.push_back(std::move(value));
    }
    const DoneCallback done = state.done;
    HWND hwnd = state.hwnd;
    if (done != nullptr) done(result);
    if (g_state.has_value() && g_state->hwnd == hwnd && hwnd != nullptr) destroyWindow();
}

void cancel(State& state) {
    const DoneCallback done = state.done;
    Result result;
    result.ok = false;
    if (done != nullptr) done(result);
    destroyWindow();
}

void recordKey(State& state, WPARAM key) {
    if (key == VK_ESCAPE) {
        state.recording = false;
        InvalidateRect(state.hwnd, nullptr, FALSE);
        return;
    }
    if (key == VK_CONTROL || key == VK_MENU || key == VK_SHIFT || key == VK_LWIN ||
        key == VK_RWIN) {
        return;
    }
    unsigned modifiers = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= hotkey::Ctrl;
    if (GetKeyState(VK_MENU) & 0x8000) modifiers |= hotkey::Alt;
    if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= hotkey::Shift;
    if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) {
        modifiers |= hotkey::Win;
    }
    std::wstring name;
    if (key >= 'A' && key <= 'Z') name = static_cast<wchar_t>(key);
    else if (key >= '0' && key <= '9') name = static_cast<wchar_t>(key);
    else if (key >= VK_F1 && key <= VK_F12) {
        name = L"F" + std::to_wstring(key - VK_F1 + 1);
    } else if (key == VK_SPACE) {
        name = L"Space";
    } else {
        state.error = L"That key cannot be used as a hotkey.";
        return;
    }
    std::wstring text;
    if (modifiers & hotkey::Ctrl) text += L"Ctrl+";
    if (modifiers & hotkey::Alt) text += L"Alt+";
    if (modifiers & hotkey::Shift) text += L"Shift+";
    if (modifiers & hotkey::Win) text += L"Win+";
    text += name;
    if (modifiers == 0 || !hotkey::parse(text).has_value()) {
        state.error = L"Use at least one modifier key.";
        return;
    }
    state.values[1] = text;
    state.recording = false;
    state.error.clear();
    InvalidateRect(state.hwnd, nullptr, FALSE);
}

void onKeyDown(State& state, WPARAM key) {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (state.recording) {
        recordKey(state, key);
        return;
    }
    switch (key) {
    case VK_ESCAPE:
        cancel(state);
        return;
    case VK_RETURN:
        accept(state);
        return;
    case VK_TAB: {
        const bool back = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        int next = state.focus - 1;
        if (back) {
            if (state.focus == kFocusTabs) next = kFocusCancel;
            else if (state.focus == kFocusCancel) next = kFocusOk;
            else if (state.focus == kFocusOk) {
                const int last = rowCount(state.tab) - 1;
                next = last >= 0 ? last : kFocusTabs;
            } else if (state.focus <= 0) next = kFocusTabs;
            else {
                int index = state.focus - 1;
                while (index >= 0 && kFields[index].tab != state.tab) --index;
                next = index >= 0 ? index : kFocusTabs;
            }
        } else {
            if (state.focus == kFocusTabs) {
                next = -1;
                for (int i = 0; i < kFieldCount; ++i) {
                    if (kFields[i].tab == state.tab) { next = i; break; }
                }
                if (next < 0) next = kFocusOk;
            } else if (state.focus >= 0) {
                int index = state.focus + 1;
                while (index < kFieldCount && kFields[index].tab != state.tab) ++index;
                next = index < kFieldCount ? index : kFocusOk;
            } else if (state.focus == kFocusOk) next = kFocusCancel;
            else next = kFocusTabs;
        }
        setFocus(state, next);
        return;
    }
    case VK_LEFT:
        if (state.focus == kFocusTabs) {
            state.tab = (state.tab + kTabCount - 1) % kTabCount;
            InvalidateRect(state.hwnd, nullptr, FALSE);
        } else if (state.focus >= 0 && kFields[state.focus].kind == Kind::Cycle) {
            cycleField(state, state.focus, -1);
        }
        return;
    case VK_RIGHT:
        if (state.focus == kFocusTabs) {
            state.tab = (state.tab + 1) % kTabCount;
            InvalidateRect(state.hwnd, nullptr, FALSE);
        } else if (state.focus >= 0 && kFields[state.focus].kind == Kind::Cycle) {
            cycleField(state, state.focus, 1);
        }
        return;
    case VK_SPACE:
        if (state.focus >= 0 && kFields[state.focus].kind == Kind::Bool) {
            state.values[state.focus] =
                state.values[state.focus] == L"true" ? L"false" : L"true";
            InvalidateRect(state.hwnd, nullptr, FALSE);
        }
        return;
    case VK_BACK:
        if (isEditable(state, state.focus) && !state.edit.empty() && state.caret > 0) {
            state.edit.erase(state.caret - 1, 1);
            --state.caret;
            InvalidateRect(state.hwnd, nullptr, FALSE);
        }
        return;
    case VK_DELETE:
        if (isEditable(state, state.focus) && state.caret < state.edit.size()) {
            state.edit.erase(state.caret, 1);
            InvalidateRect(state.hwnd, nullptr, FALSE);
        }
        return;
    case VK_HOME:
        if (isEditable(state, state.focus)) {
            state.caret = 0;
            InvalidateRect(state.hwnd, nullptr, FALSE);
        }
        return;
    case VK_END:
        if (isEditable(state, state.focus)) {
            state.caret = state.edit.size();
            InvalidateRect(state.hwnd, nullptr, FALSE);
        }
        return;
    default:
        break;
    }
    if (ctrl && key >= 'A' && key <= 'Z') return;
    if (isEditable(state, state.focus) && key >= VK_LEFT && key <= VK_DOWN) {
        if (key == VK_LEFT && state.caret > 0) --state.caret;
        if (key == VK_RIGHT && state.caret < state.edit.size()) ++state.caret;
        InvalidateRect(state.hwnd, nullptr, FALSE);
    }
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        State* state = reinterpret_cast<State*>(create->lpCreateParams);
        if (state != nullptr) {
            state->hwnd = hwnd;
            state->dpi = GetDpiForWindow(hwnd);
            if (!createResources(*state)) {
                destroyWindow();
                return -1;
            }
            RECT rect{};
            GetClientRect(hwnd, &rect);
            state->target->Resize(D2D1::SizeU(
                static_cast<UINT32>(std::max(1L, rect.right - rect.left)),
                static_cast<UINT32>(std::max(1L, rect.bottom - rect.top))));
        }
        return 0;
    }
    State* state = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (state == nullptr) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        draw(*state);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (state->target != nullptr) {
            state->target->Resize(D2D1::SizeU(
                static_cast<UINT32>(std::max<int>(1, LOWORD(lParam))),
                static_cast<UINT32>(std::max<int>(1, HIWORD(lParam)))));
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED:
        state->dpi = static_cast<unsigned>(LOWORD(wParam));
        if (state->target != nullptr) {
            state->target->SetDpi(static_cast<float>(state->dpi),
                                  static_cast<float>(state->dpi));
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        onKeyDown(*state, wParam);
        return 0;
    case WM_CHAR: {
        if (state->recording) return 0;
        if (isEditable(*state, state->focus) && wParam >= 32 && wParam != 127) {
            if (state->caret <= state->edit.size()) {
                state->edit.insert(state->caret, 1, static_cast<wchar_t>(wParam));
                ++state->caret;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const Layout lay = layout(*state);
        const float x = static_cast<float>(GET_X_LPARAM(lParam)) * 96.0f /
                        static_cast<float>(state->dpi);
        const float y = static_cast<float>(GET_Y_LPARAM(lParam)) * 96.0f /
                        static_cast<float>(state->dpi);
        if (y >= lay.footerY && y <= lay.footerY + 32.0f) {
            if (x >= lay.width - 88.0f && x <= lay.width - 16.0f) {
                accept(*state);
                return 0;
            }
            if (x >= lay.width - 176.0f && x <= lay.width - 96.0f) {
                cancel(*state);
                return 0;
            }
        }
        const int tab = tabAtPoint(x, y, lay);
        if (tab >= 0) {
            state->tab = tab;
            setFocus(*state, kFocusTabs);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        const int field = fieldAtPoint(*state, x, y, lay);
        if (field >= 0) {
            if (state->focus == field && kFields[field].kind == Kind::Hotkey) {
                const float buttonLeft = rowRect(lay, 0).right - 76.0f;
                if (x >= buttonLeft) {
                    state->recording = !state->recording;
                    state->error.clear();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            setFocus(*state, field);
            if (kFields[field].kind == Kind::Bool) {
                state->values[field] = state->values[field] == L"true" ? L"false" : L"true";
            } else if (kFields[field].kind == Kind::Cycle) {
                cycleField(*state, field, 1);
            } else if (kFields[field].kind == Kind::Hotkey) {
                state->recording = true;
                state->error.clear();
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        commitEdit(*state);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!state->trackingMouse) {
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hwnd;
            TrackMouseEvent(&track);
            state->trackingMouse = true;
        }
        const Layout lay = layout(*state);
        const float x = static_cast<float>(GET_X_LPARAM(lParam)) * 96.0f /
                        static_cast<float>(state->dpi);
        const float y = static_cast<float>(GET_Y_LPARAM(lParam)) * 96.0f /
                        static_cast<float>(state->dpi);
        int hover = -999;
        const int tab = tabAtPoint(x, y, lay);
        if (tab >= 0) hover = kFocusTabs - 100 - tab;
        else {
            const int field = fieldAtPoint(*state, x, y, lay);
            if (field >= 0) hover = field;
        }
        if (hover != state->hover) {
            state->hover = hover;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        state->trackingMouse = false;
        state->hover = -999;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_CLOSE:
        cancel(*state);
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool registerWindow() {
    static bool registered = false;
    if (registered) return true;
    WNDCLASSW wc{};
    wc.lpfnWndProc = &wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.lpszClassName = class_name();
    if (RegisterClassW(&wc) == 0) return false;
    registered = true;
    return true;
}

unsigned dpiForCursorMonitor() {
    POINT point{};
    if (!GetCursorPos(&point)) return GetDpiForSystem();
    const HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    UINT dpiX = 0;
    UINT dpiY = 0;
    if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX != 0) {
        return static_cast<unsigned>(dpiX);
    }
    return GetDpiForSystem();
}

} // namespace

bool open(HWND parent, const Settings& current, DoneCallback done) {
    if (g_state.has_value()) return false;
    if (!registerWindow()) return false;

    g_state.emplace();
    State& state = *g_state;
    state.done = done;
    state.draft = current;
    initializeValues(state);
    state.dpi = dpiForCursorMonitor();

    const float scale = static_cast<float>(state.dpi) / 96.0f;
    const int width = static_cast<int>(640.0f * scale);
    const int height = static_cast<int>(520.0f * scale);
    POINT point{};
    GetCursorPos(&point);
    const HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (GetMonitorInfoW(monitor, &info)) {
        x = info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2;
        y = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2;
    }

    const std::wstring title = std::wstring(kAppName) + L" Settings";
    state.hwnd = CreateWindowExW(0, class_name(), title.c_str(), WS_OVERLAPPEDWINDOW,
                                 x, y, width, height, parent, nullptr,
                                 GetModuleHandleW(nullptr), &state);
    if (state.hwnd == nullptr) {
        g_state.reset();
        return false;
    }
    ShowWindow(state.hwnd, SW_SHOW);
    UpdateWindow(state.hwnd);
    return true;
}

void close() {
    if (!g_state.has_value()) return;
    destroyWindow();
}

} // namespace tapir::settings
