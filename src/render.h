#pragma once

#include "config.h"
#include "util/details_layout.h"

#include <cstddef>
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
inline constexpr float kMoreMenuRowHeightDip = 36.0f;
inline constexpr float kDetailsPaneWidthDip =
    static_cast<float>(details_layout::kWidthDip);

struct SearchRowView {
    std::wstring_view text;
    std::wstring_view placeholder;
    std::wstring_view ghost;
    std::wstring_view hint;
    std::wstring_view hintKey;
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
    ID2D1Bitmap* icon = nullptr;
};

struct MenuRowView {
    std::wstring_view label;
    bool selected = false;
};

struct MoreMenuView {
    const MenuRowView* rows = nullptr;
    std::size_t rowCount = 0;
};

struct CommandBarView {
    std::wstring_view modeLabel;
    std::wstring_view primaryKey;
    std::wstring_view primaryLabel;
    std::wstring_view moreKey;
    std::wstring_view moreLabel;
    bool showMore = true;
    wchar_t filterGlyph = 0;
};

struct DetailsView {
    bool show = false;
    std::wstring_view text;
    std::wstring_view label;
    std::wstring_view emptyLabel;
    float scrollDip = 0;
    float widthDip = kDetailsPaneWidthDip;
    float heightDip = 400.0f;
};

struct PaletteView {
    SearchRowView search;
    const ResultRowView* rows = nullptr;
    std::size_t rowCount = 0;
    std::wstring_view sectionHeader;
    std::wstring_view emptyLabel;
    bool showEmptyState = false;
    bool listVisible = false;
    MoreMenuView menu;
    CommandBarView commands;
    unsigned dpi = 96;
    bool acrylic = false;
    DetailsView details;
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
    void applySettings(const Settings& settings);
    void clearTextCache() noexcept;

private:
    explicit Painter(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace tapir::render
