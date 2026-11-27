#pragma once

#include <windows.graphics.effects.h>
#include <windows.graphics.effects.interop.h>
#include <d2d1effects.h>
#include <Windows.UI.Composition.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Composition.h>
#include <wrl/client.h>
#include <wrl/module.h>

namespace tapir {

class GaussianBlurEffect
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::WinRtClassicComMix>,
          ABI::Windows::Graphics::Effects::IGraphicsEffect,
          ABI::Windows::Graphics::Effects::IGraphicsEffectD2D1Interop,
          ABI::Windows::Graphics::Effects::IGraphicsEffectSource> {
public:
    HRESULT RuntimeClassInitialize(float radiusDip) noexcept {
        m_radius = radiusDip;
        m_source =
            winrt::Windows::UI::Composition::CompositionEffectSourceParameter(L"backdrop");
        return S_OK;
    }
    HRESULT get_Name(HSTRING* name) override {
        if (name == nullptr) return E_INVALIDARG;
        *name = nullptr;
        return S_OK;
    }
    HRESULT put_Name(HSTRING) override { return E_NOTIMPL; }
    HRESULT GetEffectId(GUID* id) override {
        if (id == nullptr) return E_INVALIDARG;
        *id = CLSID_D2D1GaussianBlur;
        return S_OK;
    }
    HRESULT GetNamedPropertyMapping(
        LPCWSTR name, UINT* index,
        ABI::Windows::Graphics::Effects::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) override {
        if (name == nullptr || index == nullptr || mapping == nullptr) return E_INVALIDARG;
        if (wcscmp(name, L"BlurAmount") == 0) {
            *index = 0;
            *mapping = ABI::Windows::Graphics::Effects::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
            return S_OK;
        }
        if (wcscmp(name, L"Optimization") == 0) {
            *index = 1;
            *mapping = ABI::Windows::Graphics::Effects::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
            return S_OK;
        }
        if (wcscmp(name, L"BorderMode") == 0) {
            *index = 2;
            *mapping = ABI::Windows::Graphics::Effects::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
            return S_OK;
        }
        return E_INVALIDARG;
    }
    HRESULT GetPropertyCount(UINT* count) override {
        if (count == nullptr) return E_INVALIDARG;
        *count = 3;
        return S_OK;
    }
    HRESULT GetProperty(UINT index,
                        ABI::Windows::Foundation::IPropertyValue** value) override {
        if (value == nullptr) return E_INVALIDARG;
        *value = nullptr;
        if (index == 0) {
            auto property = winrt::Windows::Foundation::PropertyValue::CreateSingle(m_radius);
            *value = static_cast<ABI::Windows::Foundation::IPropertyValue*>(
                winrt::detach_abi(property));
            return S_OK;
        }
        if (index == 1) {
            auto property = winrt::Windows::Foundation::PropertyValue::CreateUInt32(
                static_cast<std::uint32_t>(D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED));
            *value = static_cast<ABI::Windows::Foundation::IPropertyValue*>(
                winrt::detach_abi(property));
            return S_OK;
        }
        if (index == 2) {
            auto property = winrt::Windows::Foundation::PropertyValue::CreateUInt32(
                static_cast<std::uint32_t>(D2D1_BORDER_MODE_SOFT));
            *value = static_cast<ABI::Windows::Foundation::IPropertyValue*>(
                winrt::detach_abi(property));
            return S_OK;
        }
        return E_INVALIDARG;
    }
    HRESULT GetSource(UINT index,
                      ABI::Windows::Graphics::Effects::IGraphicsEffectSource** source) override {
        if (source == nullptr) return E_INVALIDARG;
        *source = nullptr;
        if (index != 0) return E_INVALIDARG;
        auto raw = static_cast<ABI::Windows::UI::Composition::ICompositionEffectSourceParameter*>(
            winrt::get_abi(m_source));
        return raw->QueryInterface(
            __uuidof(ABI::Windows::Graphics::Effects::IGraphicsEffectSource),
            reinterpret_cast<void**>(source));
    }
    HRESULT GetSourceCount(UINT* count) override {
        if (count == nullptr) return E_INVALIDARG;
        *count = 1;
        return S_OK;
    }

private:
    winrt::Windows::UI::Composition::CompositionEffectSourceParameter m_source{nullptr};
    float m_radius = 0.0f;
};

inline Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Effects::IGraphicsEffectSource>
createGaussianBlurEffect(float radiusDip) {
    Microsoft::WRL::ComPtr<GaussianBlurEffect> effect;
    winrt::check_hresult(
        Microsoft::WRL::MakeAndInitialize<GaussianBlurEffect>(&effect, radiusDip));
    Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Effects::IGraphicsEffectSource> source;
    winrt::check_hresult(effect.As(&source));
    return source;
}

} // namespace tapir
