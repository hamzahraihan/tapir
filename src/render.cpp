#include "render.h"

#include <algorithm>
#include <cstdint>

namespace tapir::render {
namespace {

using Microsoft::WRL::ComPtr;

D2D1_COLOR_F toColor(const Rgb& c, float alpha = 1.0f) noexcept {
    return D2D1::ColorF(static_cast<float>(c.r) / 255.0f,
                        static_cast<float>(c.g) / 255.0f,
                        static_cast<float>(c.b) / 255.0f, alpha);
}

std::wstring pickFont(IDWriteFactory* factory, const wchar_t* primary,
                      const wchar_t* fallback) {
    ComPtr<IDWriteFontCollection> fonts;
    if (SUCCEEDED(factory->GetSystemFontCollection(&fonts, FALSE))) {
        UINT32 index = 0;
        BOOL found = FALSE;
        if (SUCCEEDED(fonts->FindFamilyName(primary, &index, &found)) && found) {
            return std::wstring(primary);
        }
    }
    return std::wstring(fallback);
}

void configureFormat(IDWriteTextFormat* format) noexcept {
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
}

ComPtr<IDWriteTextLayout> makeLayout(IDWriteFactory* factory,
                                     IDWriteTextFormat* format,
                                     std::wstring_view text, float maxWidth, float height) {
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(factory->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()),
                                         format, maxWidth, height, &layout))) {
        return {};
    }
    DWRITE_TRIMMING trimming{};
    trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
    layout->SetTrimming(&trimming, nullptr);
    layout->SetMaxWidth(maxWidth);
    return layout;
}

float measurePrefix(IDWriteFactory* factory, IDWriteTextFormat* format,
                    std::wstring_view text, float maxWidth, float height, std::size_t upto) {
    const std::size_t n = upto < text.size() ? upto : text.size();
    auto layout = makeLayout(factory, format, text.substr(0, n), maxWidth, height);
    if (!layout) return 0.0f;
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) return 0.0f;
    const float w = metrics.widthIncludingTrailingWhitespace;
    return w < maxWidth ? w : maxWidth;
}

} // namespace

struct Painter::Impl {
    ComPtr<ID2D1RenderTarget> target;
    Settings::Theme theme;
    ComPtr<IDWriteFactory> write;
    ComPtr<ID2D1SolidColorBrush> background;
    ComPtr<ID2D1SolidColorBrush> backgroundTint;
    ComPtr<ID2D1SolidColorBrush> foreground;
    ComPtr<ID2D1SolidColorBrush> muted;
    ComPtr<ID2D1SolidColorBrush> accent;
    ComPtr<ID2D1SolidColorBrush> selection;
    ComPtr<IDWriteTextFormat> textFormat;
    ComPtr<IDWriteTextFormat> placeholderFormat;
    ComPtr<IDWriteTextFormat> glyphFormat;
};

Painter::Painter(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Painter::Painter(Painter&&) noexcept = default;
Painter& Painter::operator=(Painter&&) noexcept = default;
Painter::~Painter() = default;

std::optional<Painter> Painter::create(ComPtr<ID2D1RenderTarget> target,
                                        const Settings& settings) {
    if (!target) return std::nullopt;

    auto impl = std::make_unique<Impl>();
    impl->target = std::move(target);

    IUnknown* rawWrite = nullptr;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   &rawWrite)) || rawWrite == nullptr) {
        return std::nullopt;
    }
    impl->write.Attach(static_cast<IDWriteFactory*>(rawWrite));

    impl->theme = settings.theme;
    const auto& theme = impl->theme;
    if (FAILED(impl->target->CreateSolidColorBrush(toColor(theme.background), &impl->background)) ||
        FAILED(impl->target->CreateSolidColorBrush(
            toColor(theme.background, theme.backdropOpacity), &impl->backgroundTint)) ||
        FAILED(impl->target->CreateSolidColorBrush(toColor(theme.foreground), &impl->foreground)) ||
        FAILED(impl->target->CreateSolidColorBrush(toColor(theme.muted), &impl->muted)) ||
        FAILED(impl->target->CreateSolidColorBrush(toColor(theme.accent), &impl->accent)) ||
        FAILED(impl->target->CreateSolidColorBrush(toColor(theme.selection, 0.30f), &impl->selection))) {
        return std::nullopt;
    }

    const std::wstring textFamily = pickFont(impl->write.Get(), theme.font.c_str(), L"Segoe UI");
    const std::wstring glyphFamily = pickFont(impl->write.Get(), L"Segoe Fluent Icons",
                                              L"Segoe MDL2 Assets");

    const auto makeFormat = [&](const std::wstring& family, float size,
                                ComPtr<IDWriteTextFormat>& out) {
        return SUCCEEDED(impl->write->CreateTextFormat(
            family.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, size, L"en-US", &out));
    };

    if (!makeFormat(textFamily, 19.0f, impl->textFormat) ||
        !makeFormat(textFamily, 19.0f, impl->placeholderFormat) ||
        !makeFormat(glyphFamily, 20.0f, impl->glyphFormat)) {
        return std::nullopt;
    }
    configureFormat(impl->textFormat.Get());
    configureFormat(impl->placeholderFormat.Get());
    configureFormat(impl->glyphFormat.Get());

    return Painter(std::move(impl));
}

void Painter::setTarget(ComPtr<ID2D1RenderTarget> target) {
    if (!impl_ || !target) return;
    impl_->target = std::move(target);
    // Brushes are render-target-dependent; recreate them from the new target.
    const auto& theme = impl_->theme;
    impl_->target->CreateSolidColorBrush(toColor(theme.background), &impl_->background);
    impl_->target->CreateSolidColorBrush(toColor(theme.background, theme.backdropOpacity),
                                         &impl_->backgroundTint);
    impl_->target->CreateSolidColorBrush(toColor(theme.foreground), &impl_->foreground);
    impl_->target->CreateSolidColorBrush(toColor(theme.muted), &impl_->muted);
    impl_->target->CreateSolidColorBrush(toColor(theme.accent), &impl_->accent);
    impl_->target->CreateSolidColorBrush(toColor(theme.selection, 0.30f), &impl_->selection);
}

void Painter::draw(const SearchRowView& view) {
    if (!impl_ || !impl_->target) return;
    auto& p = *impl_;
    const float scale = static_cast<float>(view.dpi) / 96.0f;
    const float width = view.widthDip * scale;
    const float height = view.heightDip * scale;
    const float glyphX = 16.0f * scale;
    const float glyphW = 20.0f * scale;
    const float textX = 52.0f * scale;
    const float textW = width - textX - 12.0f * scale;
    if (textW <= 0.0f || width <= 0.0f || height <= 0.0f) return;

    p.target->BeginDraw();
    if (view.acrylic) {
        p.target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    } else {
        p.target->Clear(toColor(p.theme.background));
    }

    if (view.acrylic) {
        const D2D1_RECT_F full = D2D1::RectF(0.0f, 0.0f, width, height);
        p.target->FillRectangle(full, p.backgroundTint.Get());
    }

    // Mode glyph (no image assets; code point supplied by the caller).
    if (view.glyph != 0) {
        const std::wstring_view glyph(&view.glyph, 1);
        auto layout = makeLayout(p.write.Get(), p.glyphFormat.Get(), glyph, glyphW, height);
        if (layout) {
            p.target->DrawTextLayout(D2D1::Point2F(glyphX, 0.0f), layout.Get(),
                                     p.muted.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
        }
    }

    // Placeholder when empty, otherwise the edited text.
    if (view.text.empty()) {
        auto layout = makeLayout(p.write.Get(), p.placeholderFormat.Get(), view.placeholder,
                                 textW, height);
        if (layout) {
            p.target->DrawTextLayout(D2D1::Point2F(textX, 0.0f), layout.Get(),
                                     p.muted.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
        }
    } else {
        auto layout = makeLayout(p.write.Get(), p.textFormat.Get(), view.text, textW, height);
        if (layout) {
            p.target->DrawTextLayout(D2D1::Point2F(textX, 0.0f), layout.Get(),
                                     p.foreground.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
        }

        const std::size_t n = view.text.size();
        std::size_t first = view.selection.first < n ? view.selection.first : n;
        std::size_t last = view.selection.second < n ? view.selection.second : n;
        if (first > last) std::swap(first, last);
        if (first != last) {
            const float x1 = textX + measurePrefix(p.write.Get(), p.textFormat.Get(),
                                                    view.text, textW, height, first);
            const float x2 = textX + measurePrefix(p.write.Get(), p.textFormat.Get(),
                                                    view.text, textW, height, last);
            const D2D1_RECT_F rect = D2D1::RectF(x1, 8.0f * scale, x2, height - 8.0f * scale);
            p.target->FillRectangle(rect, p.selection.Get());
        }
    }

    if (view.focused) {
        const float caretX = textX + measurePrefix(p.write.Get(), p.textFormat.Get(),
                                                    view.text, textW, height, view.caret);
        const D2D1_RECT_F caret = D2D1::RectF(caretX, 8.0f * scale,
                                              caretX + scale, height - 8.0f * scale);
        p.target->FillRectangle(caret, p.accent.Get());
    }

    p.target->EndDraw();
}

} // namespace tapir::render
