#include "render.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

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

void configureWrappedFormat(IDWriteTextFormat* format) noexcept {
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
}

ComPtr<IDWriteTextLayout> makeLayout(IDWriteFactory* factory,
                                     IDWriteTextFormat* format,
                                     std::wstring_view text, float maxWidth, float height,
                                     bool trim = true) {
    ComPtr<IDWriteTextLayout> layout;
    if (text.empty() || maxWidth <= 0.0f || height <= 0.0f) return {};
    if (FAILED(factory->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()),
                                         format, maxWidth, height, &layout))) {
        return {};
    }
    if (trim) {
        DWRITE_TRIMMING trimming{};
        trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
        layout->SetTrimming(&trimming, nullptr);
    }
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
    ComPtr<ID2D1SolidColorBrush> hover;
    ComPtr<ID2D1SolidColorBrush> surface;
    ComPtr<ID2D1SolidColorBrush> separator;
    ComPtr<ID2D1SolidColorBrush> detailsBackground;
    ComPtr<IDWriteTextFormat> textFormat;
    ComPtr<IDWriteTextFormat> placeholderFormat;
    ComPtr<IDWriteTextFormat> glyphFormat;
    ComPtr<IDWriteTextFormat> titleFormat;
    ComPtr<IDWriteTextFormat> titleStrongFormat;
    ComPtr<IDWriteTextFormat> subtitleFormat;
    ComPtr<IDWriteTextFormat> labelFormat;
    ComPtr<IDWriteTextFormat> keyFormat;
    ComPtr<IDWriteTextFormat> detailsFormat;
};

namespace {

using Impl = Painter::Impl;

bool recreateBrushes(Impl& p) {
    const auto& t = p.theme;
    return SUCCEEDED(p.target->CreateSolidColorBrush(toColor(t.background), &p.background)) &&
           SUCCEEDED(p.target->CreateSolidColorBrush(
               toColor(t.background, t.backdropOpacity), &p.backgroundTint)) &&
           SUCCEEDED(p.target->CreateSolidColorBrush(toColor(t.foreground), &p.foreground)) &&
           SUCCEEDED(p.target->CreateSolidColorBrush(toColor(t.muted), &p.muted)) &&
           SUCCEEDED(p.target->CreateSolidColorBrush(toColor(t.accent), &p.accent)) &&
           SUCCEEDED(p.target->CreateSolidColorBrush(toColor(t.selection, 0.30f), &p.selection)) &&
           SUCCEEDED(p.target->CreateSolidColorBrush(toColor(t.selection, 0.14f), &p.hover)) &&
           SUCCEEDED(p.target->CreateSolidColorBrush(toColor(t.surface), &p.surface)) &&
           SUCCEEDED(p.target->CreateSolidColorBrush(toColor(t.surface, 0.55f),
                                                     &p.detailsBackground)) &&
           SUCCEEDED(p.target->CreateSolidColorBrush(toColor(t.muted, 0.35f), &p.separator));
}

bool createFormats(Impl& p) {
    const std::wstring textFamily =
        pickFont(p.write.Get(), p.theme.font.c_str(), L"Segoe UI");
    const std::wstring glyphFamily = pickFont(p.write.Get(), L"Segoe Fluent Icons",
                                              L"Segoe MDL2 Assets");

    const auto makeFormat = [&](const std::wstring& family, float size,
                                DWRITE_FONT_WEIGHT weight,
                                ComPtr<IDWriteTextFormat>& out) {
        return SUCCEEDED(p.write->CreateTextFormat(
            family.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, size, L"en-US", &out));
    };

    if (!makeFormat(textFamily, 19.0f, DWRITE_FONT_WEIGHT_NORMAL, p.textFormat) ||
        !makeFormat(textFamily, 19.0f, DWRITE_FONT_WEIGHT_NORMAL, p.placeholderFormat) ||
        !makeFormat(glyphFamily, 20.0f, DWRITE_FONT_WEIGHT_NORMAL, p.glyphFormat) ||
        !makeFormat(textFamily, 14.0f, DWRITE_FONT_WEIGHT_NORMAL, p.titleFormat) ||
        !makeFormat(textFamily, 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, p.titleStrongFormat) ||
        !makeFormat(textFamily, 12.0f, DWRITE_FONT_WEIGHT_NORMAL, p.subtitleFormat) ||
        !makeFormat(textFamily, 12.0f, DWRITE_FONT_WEIGHT_NORMAL, p.labelFormat) ||
        !makeFormat(textFamily, 12.0f, DWRITE_FONT_WEIGHT_NORMAL, p.keyFormat) ||
        !makeFormat(textFamily, 13.0f, DWRITE_FONT_WEIGHT_NORMAL, p.detailsFormat)) {
        return false;
    }
    configureFormat(p.textFormat.Get());
    configureFormat(p.placeholderFormat.Get());
    configureFormat(p.glyphFormat.Get());
    configureFormat(p.titleFormat.Get());
    configureFormat(p.titleStrongFormat.Get());
    configureFormat(p.subtitleFormat.Get());
    configureFormat(p.labelFormat.Get());
    configureFormat(p.keyFormat.Get());
    configureWrappedFormat(p.detailsFormat.Get());
    return true;
}

float drawSegment(Impl& p, IDWriteTextFormat* format, ID2D1Brush* brush,
                  std::wstring_view text, float x, float y, float maxW, float h,
                  bool trim = true) {
    auto layout = makeLayout(p.write.Get(), format, text, maxW, h, trim);
    if (!layout) return 0.0f;
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) return 0.0f;
    const float w = std::min(metrics.widthIncludingTrailingWhitespace, maxW);
    p.target->DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), brush,
                             D2D1_DRAW_TEXT_OPTIONS_NONE);
    return w;
}

float textWidth(Impl& p, IDWriteTextFormat* format, std::wstring_view text,
                float maxW, float h) {
    auto layout = makeLayout(p.write.Get(), format, text, maxW, h);
    if (!layout) return 0.0f;
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) return 0.0f;
    return std::min(metrics.widthIncludingTrailingWhitespace, maxW);
}

void fillRounded(Impl& p, const D2D1_RECT_F& rect, float radius, ID2D1Brush* brush) {
    p.target->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush);
}

void drawSearchRow(Impl& p, const SearchRowView& view, float scale) {
    const float width = view.widthDip * scale;
    const float height = view.heightDip * scale;
    const float glyphX = 16.0f * scale;
    const float glyphW = 20.0f * scale;
    const float textX = 52.0f * scale;
    const float textW = width - textX - 12.0f * scale;
    if (textW <= 0.0f || width <= 0.0f || height <= 0.0f) return;

    if (view.glyph != 0) {
        drawSegment(p, p.glyphFormat.Get(), p.muted.Get(),
                    std::wstring_view(&view.glyph, 1), glyphX, 0.0f, glyphW, height);
    }

    if (view.text.empty()) {
        drawSegment(p, p.placeholderFormat.Get(), p.muted.Get(), view.placeholder,
                    textX, 0.0f, textW, height);
    } else {
        drawSegment(p, p.textFormat.Get(), p.foreground.Get(), view.text,
                    textX, 0.0f, textW, height);
        const std::size_t n = view.text.size();
        std::size_t first = view.selection.first < n ? view.selection.first : n;
        std::size_t last = view.selection.second < n ? view.selection.second : n;
        if (first > last) std::swap(first, last);
        if (first != last) {
            const float x1 = textX + measurePrefix(p.write.Get(), p.textFormat.Get(),
                                                   view.text, textW, height, first);
            const float x2 = textX + measurePrefix(p.write.Get(), p.textFormat.Get(),
                                                   view.text, textW, height, last);
            p.target->FillRectangle(D2D1::RectF(x1, 8.0f * scale, x2, height - 8.0f * scale),
                                    p.selection.Get());
        }
    }

    if (!view.text.empty() && !view.ghost.empty() &&
        view.selection.first == view.selection.second && view.caret == view.text.size()) {
        const float caretX = textX + measurePrefix(p.write.Get(), p.textFormat.Get(),
                                                   view.text, textW, height, view.caret);
        drawSegment(p, p.textFormat.Get(), p.muted.Get(), view.ghost, caretX, 0.0f,
                    std::max(0.0f, textX + textW - caretX), height);
    }

    if (view.focused) {
        const float caretX = textX + measurePrefix(p.write.Get(), p.textFormat.Get(),
                                                   view.text, textW, height, view.caret);
        p.target->FillRectangle(D2D1::RectF(caretX, 8.0f * scale,
                                            caretX + scale, height - 8.0f * scale),
                                p.accent.Get());
    }
}

void drawHighlightedTitle(Impl& p, const ResultRowView& row, float x, float y,
                          float maxW, float h) {
    float used = 0.0f;
    std::size_t pos = 0;
    const auto drawPart = [&](IDWriteTextFormat* format, ID2D1Brush* brush,
                              std::wstring_view part) {
        used += drawSegment(p, format, brush, part, x + used, y,
                            std::max(0.0f, maxW - used), h);
    };
    for (std::size_t i = 0; i < row.matchedCount && used < maxW; ++i) {
        const std::size_t match = row.matched[i];
        if (match < pos || match >= row.title.size()) continue;
        if (match > pos) {
            drawPart(p.titleFormat.Get(), p.foreground.Get(),
                     row.title.substr(pos, match - pos));
        }
        drawPart(p.titleStrongFormat.Get(), p.accent.Get(), row.title.substr(match, 1));
        pos = match + 1;
    }
    if (pos < row.title.size() && used < maxW) {
        drawPart(p.titleFormat.Get(), p.foreground.Get(), row.title.substr(pos));
    }
}

void drawResultRow(Impl& p, const ResultRowView& row, float y, float width,
                   float height, float scale) {
    const D2D1_RECT_F area = D2D1::RectF(8.0f * scale, y + 2.0f * scale,
                                         width - 8.0f * scale, y + height - 2.0f * scale);
    if (row.selected) {
        fillRounded(p, area, 6.0f * scale, p.selection.Get());
        p.target->FillRectangle(D2D1::RectF(12.0f * scale, y + 8.0f * scale,
                                            15.0f * scale, y + height - 8.0f * scale),
                                p.accent.Get());
    } else if (row.hovered) {
        fillRounded(p, area, 6.0f * scale, p.hover.Get());
    }

    const D2D1_RECT_F tile = D2D1::RectF(24.0f * scale, y + 8.0f * scale,
                                         56.0f * scale, y + 40.0f * scale);
    fillRounded(p, tile, 6.0f * scale, p.surface.Get());
    if (row.icon != nullptr) {
        p.target->DrawBitmap(row.icon, tile, 1.0f,
                             D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else if (!row.title.empty()) {
        drawSegment(p, p.titleFormat.Get(), p.muted.Get(), row.title.substr(0, 1),
                    tile.left + 9.0f * scale, y + 8.0f * scale, 20.0f * scale,
                    24.0f * scale);
    }

    const float textX = 68.0f * scale;
    const float textW = std::max(1.0f, width - textX - 120.0f * scale);
    drawHighlightedTitle(p, row, textX, y + 1.0f * scale, textW, 24.0f * scale);
    drawSegment(p, p.subtitleFormat.Get(), p.muted.Get(), row.subtitle,
                textX, y + 24.0f * scale, textW, 20.0f * scale);
    if (!row.typeLabel.empty()) {
        const float labelW = textWidth(p, p.labelFormat.Get(), row.typeLabel,
                                       80.0f * scale, height);
        drawSegment(p, p.labelFormat.Get(), p.muted.Get(), row.typeLabel,
                    width - 16.0f * scale - labelW, y + 8.0f * scale,
                    labelW + scale, 24.0f * scale);
    }
}

float keyCap(Impl& p, std::wstring_view label, float x, float y, float scale) {
    const float capHeight = 22.0f * scale;
    const float labelW = textWidth(p, p.keyFormat.Get(), label, 160.0f * scale, capHeight);
    const float capW = labelW + 12.0f * scale;
    fillRounded(p, D2D1::RectF(x, y, x + capW, y + capHeight), 4.0f * scale, p.surface.Get());
    drawSegment(p, p.keyFormat.Get(), p.foreground.Get(), label,
                x + 6.0f * scale, y, labelW + scale, capHeight);
    return capW;
}

void drawMoreMenu(Impl& p, const MoreMenuView& menu, float width, float scale) {
    const float rowHeight = kMoreMenuRowHeightDip * scale;
    const float menuWidth = std::min(260.0f * scale, width - 32.0f * scale);
    if (menuWidth <= 0.0f) return;
    if (menu.rowCount == 0 && menu.rows == nullptr) return;

    const bool empty = menu.rowCount == 0;
    const std::size_t count = empty ? 1 : menu.rowCount;
    const float x = 16.0f * scale;
    const float y = kSearchRowHeightDip * scale + 8.0f * scale;
    const float menuHeight = static_cast<float>(count) * rowHeight + 12.0f * scale;
    const D2D1_RECT_F card = D2D1::RectF(x, y, x + menuWidth, y + menuHeight);
    fillRounded(p, card, 8.0f * scale, p.surface.Get());
    p.target->DrawRoundedRectangle(D2D1::RoundedRect(card, 8.0f * scale, 8.0f * scale),
                                   p.muted.Get(), scale);

    for (std::size_t i = 0; i < count; ++i) {
        const float rowY = y + 6.0f * scale + static_cast<float>(i) * rowHeight;
        const D2D1_RECT_F row = D2D1::RectF(x + 4.0f * scale, rowY,
                                            x + menuWidth - 4.0f * scale,
                                            rowY + rowHeight - 2.0f * scale);
        const bool selected = !empty && menu.rows[i].selected;
        if (selected) {
            fillRounded(p, row, 5.0f * scale, p.selection.Get());
            p.target->FillRectangle(
                D2D1::RectF(x + 8.0f * scale, rowY + 6.0f * scale,
                            x + 11.0f * scale, rowY + rowHeight - 8.0f * scale),
                p.accent.Get());
        }
        const std::wstring_view label = empty ? L"No actions" : menu.rows[i].label;
        drawSegment(p, empty ? p.subtitleFormat.Get() : p.titleFormat.Get(),
                    p.foreground.Get(), label, x + 20.0f * scale, rowY,
                    menuWidth - 32.0f * scale, rowHeight - 2.0f * scale);
    }
}

void drawDetailsPane(Impl& p, const DetailsView& view, float x, float y, float width,
                     float height, float scale) {
    if (width <= 0.0f || height <= 0.0f) return;

    p.target->FillRectangle(D2D1::RectF(x, y, x + width, y + height),
                            p.detailsBackground.Get());
    p.target->FillRectangle(D2D1::RectF(x, y, x + scale, y + height), p.separator.Get());

    const float pad = 16.0f * scale;
    const float contentX = x + pad;
    const float contentW = width - 2.0f * pad;
    const float contentBottom = y + height - pad;
    if (contentW <= 0.0f || contentBottom <= y + pad) return;

    p.target->PushAxisAlignedClip(D2D1::RectF(contentX, y + pad,
                                              contentX + contentW, contentBottom),
                                  D2D1_ANTIALIAS_MODE_ALIASED);

    const float labelH = 20.0f * scale;
    drawSegment(p, p.subtitleFormat.Get(), p.muted.Get(), view.label,
                contentX, y + pad, contentW, labelH);

    const float textY = y + pad + labelH + 8.0f * scale;
    const float textH = contentBottom - textY;
    if (textH > 0.0f) {
        bool hasText = false;
        for (const wchar_t ch : view.text) {
            if (ch != L' ' && ch != L'\t' && ch != L'\r' && ch != L'\n') {
                hasText = true;
                break;
            }
        }
        if (hasText) {
            auto layout = makeLayout(p.write.Get(), p.detailsFormat.Get(), view.text,
                                     contentW, 1.0e6f, false);
            if (layout) {
                DWRITE_TEXT_METRICS metrics{};
                float scroll = view.scrollDip * scale;
                if (SUCCEEDED(layout->GetMetrics(&metrics))) {
                    scroll = std::clamp(scroll, 0.0f,
                                        std::max(0.0f, metrics.height - textH));
                }
                p.target->DrawTextLayout(D2D1::Point2F(contentX, textY - scroll),
                                         layout.Get(), p.foreground.Get());
            }
        } else {
            drawSegment(p, p.detailsFormat.Get(), p.muted.Get(), view.emptyLabel,
                        contentX, textY, contentW, textH, false);
        }
    }

    p.target->PopAxisAlignedClip();
}

void drawCommandBar(Impl& p, const CommandBarView& bar, float y, float width,
                    float height, float scale) {
    p.target->FillRectangle(D2D1::RectF(0.0f, y, width, y + scale), p.surface.Get());
    const float capY = y + (height - 22.0f * scale) * 0.5f;
    drawSegment(p, p.labelFormat.Get(), p.muted.Get(), bar.modeLabel,
                16.0f * scale, y, 160.0f * scale, height);

    float x = width - 16.0f * scale;
    if (bar.showMore) {
        const float labelW = textWidth(p, p.labelFormat.Get(), bar.moreLabel,
                                       80.0f * scale, height);
        x -= labelW;
        drawSegment(p, p.labelFormat.Get(), p.muted.Get(), bar.moreLabel, x, y,
                    labelW + scale, height);
        x -= keyCap(p, bar.moreKey, x, capY, scale);
        x -= 12.0f * scale;
    }
    const float labelW = textWidth(p, p.labelFormat.Get(), bar.primaryLabel,
                                   80.0f * scale, height);
    x -= labelW;
    drawSegment(p, p.labelFormat.Get(), p.muted.Get(), bar.primaryLabel, x, y,
                labelW + scale, height);
    x -= keyCap(p, bar.primaryKey, x, capY, scale);
}

} // namespace

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
    if (!recreateBrushes(*impl) || !createFormats(*impl)) return std::nullopt;

    return Painter(std::move(impl));
}

void Painter::setTarget(ComPtr<ID2D1RenderTarget> target) {
    if (!impl_ || !target) return;
    impl_->target = std::move(target);
    recreateBrushes(*impl_);
}

void Painter::applySettings(const Settings& settings) {
    if (!impl_) return;
    const bool fontChanged = impl_->theme.font != settings.theme.font;
    impl_->theme = settings.theme;
    if (!recreateBrushes(*impl_)) return;
    if (fontChanged) createFormats(*impl_);
}

void Painter::draw(const PaletteView& view) {
    if (!impl_ || !impl_->target) return;
    Impl& p = *impl_;
    const float scale = static_cast<float>(view.dpi) / 96.0f;
    const float listWidth = view.search.widthDip * scale;
    const bool showDetails = view.details.show && view.details.widthDip > 0.0f &&
                             view.details.heightDip > 0.0f;
    const float paneWidth = showDetails ? view.details.widthDip * scale : 0.0f;
    const float width = listWidth + paneWidth;
    const float searchHeight = view.search.heightDip * scale;
    const float rowHeight = kResultRowHeightDip * scale;
    const float headerHeight = kSectionHeaderHeightDip * scale;
    const float barHeight = kCommandBarHeightDip * scale;
    const bool listVisible = view.listVisible;
    const float shownRows = view.rowCount > 0 ? static_cast<float>(view.rowCount)
                                              : (view.showEmptyState ? 1.0f : 0.0f);
    const float listHeight = listVisible ? headerHeight + shownRows * rowHeight + barHeight : 0.0f;
    const float contentHeight = searchHeight + listHeight;
    const float height = showDetails ? view.details.heightDip * scale : contentHeight;
    if (width <= 0.0f || height <= 0.0f) return;

    p.target->BeginDraw();
    const D2D1_SIZE_F targetSize = p.target->GetSize();
    if (view.acrylic) {
        p.target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        p.target->FillRectangle(
            D2D1::RectF(0.0f, 0.0f, targetSize.width, targetSize.height),
            p.backgroundTint.Get());
    } else {
        p.target->Clear(toColor(p.theme.background));
    }

    drawSearchRow(p, view.search, scale);
    if (listVisible) {
        float y = searchHeight;
        if (!view.sectionHeader.empty() && view.rowCount > 0) {
            drawSegment(p, p.subtitleFormat.Get(), p.muted.Get(), view.sectionHeader,
                        52.0f * scale, y, listWidth - 64.0f * scale, headerHeight);
            y += headerHeight;
        }
        if (view.showEmptyState) {
            drawSegment(p, p.subtitleFormat.Get(), p.muted.Get(), view.emptyLabel,
                        0.0f, y + rowHeight * 0.5f - 10.0f * scale, listWidth, 20.0f * scale);
            y += rowHeight;
        }
        for (std::size_t i = 0; i < view.rowCount; ++i) {
            drawResultRow(p, view.rows[i], y, listWidth, rowHeight, scale);
            y += rowHeight;
        }
        drawCommandBar(p, view.commands, y, listWidth, barHeight, scale);
    }
    if (showDetails) {
        drawDetailsPane(p, view.details, listWidth, 0.0f, paneWidth, height, scale);
    }
    drawMoreMenu(p, view.menu, listWidth, scale);
    p.target->EndDraw();
}

} // namespace tapir::render
