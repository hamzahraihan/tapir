#pragma once

#include "config.h"

#include <cstdint>
#include <d2d1.h>
#include <dwrite.h>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <wrl/client.h>

namespace tapir::render {

inline constexpr float kSearchRowHeightDip = 56.0f;
inline constexpr float kSectionHeaderHeightDip = 20.0f;
inline constexpr float kResultRowHeightDip = 48.0f;
inline constexpr float kCommandBarHeightDip = 40.0f;

struct SearchRowView {
    std::wstring_view text;
    std::wstring_view placeholder;
    std::wstring_view ghost;
    std::size_t caret = 0;
    std::pair<std::size_t, std::size_t> selection{0, 0};
    bool focused = false;
    wchar_t glyph = 0;
    float widthDip = 680.0f;
    float heightDip = kSearchRowHeightDip;
    unsigned dpi = 96;
};

struct ResultRowView {
    std::wstring_view title;
    std::wstring_view subtitle;
    std::wstring_view typeLabel;
    const std::uint32_t* matched = nullptr;
    std::size_t matchedCount = 0;
    bool selected = false;
    bool hovered = false;
};

struct CommandBarView {
    std::wstring_view modeLabel;
    std::wstring_view primaryKey;
    std::wstring_view primaryLabel;
    std::wstring_view moreKey;
    std::wstring_view moreLabel;
    bool showMore = true;
};

struct PaletteView {
    SearchRowView search;
    const ResultRowView* rows = nullptr;
    std::size_t rowCount = 0;
    std::wstring_view sectionHeader;
    std::wstring_view emptyLabel;
    bool showEmptyState = false;
    bool listVisible = false;
    CommandBarView commands;
    unsigned dpi = 96;
    bool acrylic = false;
};

class Painter {
public:
    struct Impl;

    Painter(const Painter&) = delete;
    Painter& operator=(const Painter&) = delete;
    Painter(Painter&&) noexcept;
    Painter& operator=(Painter&&) noexcept;
    ~Painter();

    static std::optional<Painter> create(
        Microsoft::WRL::ComPtr<ID2D1RenderTarget> target, const Settings& settings);

    void draw(const PaletteView& view);
    void setTarget(Microsoft::WRL::ComPtr<ID2D1RenderTarget> target);

private:
    explicit Painter(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace tapir::render
