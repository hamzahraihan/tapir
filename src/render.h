#pragma once

#include "config.h"

#include <d2d1.h>
#include <dwrite.h>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <wrl/client.h>

namespace tapir::render {

struct SearchRowView {
    std::wstring_view text;
    std::wstring_view placeholder;
    std::size_t caret = 0;
    std::pair<std::size_t, std::size_t> selection{0, 0};
    bool focused = false;
    wchar_t glyph = 0;
    float widthDip = 680.0f;
    float heightDip = 56.0f;
    unsigned dpi = 96;
    bool acrylic = false;
};

class Painter {
public:
    Painter(const Painter&) = delete;
    Painter& operator=(const Painter&) = delete;
    Painter(Painter&&) noexcept;
    Painter& operator=(Painter&&) noexcept;
    ~Painter();

    static std::optional<Painter> create(
        Microsoft::WRL::ComPtr<ID2D1RenderTarget> target, const Settings& settings);

    void draw(const SearchRowView& view);
    void setTarget(Microsoft::WRL::ComPtr<ID2D1RenderTarget> target);

private:
    struct Impl;
    explicit Painter(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace tapir::render
