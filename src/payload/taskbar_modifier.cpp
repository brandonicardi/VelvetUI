// ============================================================
// VelvetUI - TaskbarModifier implementation
// ============================================================

#include "taskbar_modifier.h"

// Win32 macro GetCurrentTime conflicts with WinRT Animation headers.
// Must undef before including any WinRT XAML media headers.
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Composition.h>

#include <algorithm>
#include <limits>

// GUIDs for D2D1 effects used by Composition API.
// We define them manually to avoid pulling in Win2D NuGet
// or the full d2d1effects.h (which may not be in all SDK installs).
// These are stable GUIDs from the D2D1 spec.
static constexpr GUID CLSID_D2D1GaussianBlur = {
    0x1FEB6D69, 0x2FE6, 0x4AC9,
    {0x8C, 0x58, 0x1D, 0x7F, 0x93, 0xE7, 0xA6, 0xA5}
};

static constexpr GUID CLSID_D2D1Saturation = {
    0x5CB2D9CF, 0x327D, 0x459F,
    {0xA0, 0xCE, 0x40, 0xC0, 0xB2, 0x08, 0x6B, 0xF7}
};

static constexpr GUID CLSID_D2D1ColorSourceEffect = {
    0x61C23C20, 0xAE69, 0x4D8E,
    {0x94, 0xCF, 0x50, 0x07, 0x8D, 0xF6, 0x38, 0xF2}
};

static constexpr GUID CLSID_D2D1CompositeEffect = {
    0x48FC9F51, 0xF6AC, 0x48F1,
    {0x8B, 0x58, 0x3B, 0x28, 0xAC, 0x46, 0xF7, 0x6D}
};

// Forward-declare log functions (defined in dllmain.cpp)
namespace Log {
    void Info(const wchar_t* msg);
    void Info(const wchar_t* msg, const wchar_t* detail);
    void Info(const wchar_t* msg, DWORD value);
    void Hresult(const wchar_t* msg, HRESULT hr);
}

namespace Velvet {
    namespace {
        double ComputeDesiredTaskbarWidth(
            const wux::FrameworkElement& element,
            const VelvetConfig& config)
        {
            const double hostWidth = element.ActualWidth() > 1.0 ? element.ActualWidth() : 1920.0;
            return std::max(640.0, hostWidth - (config.floating.marginHorizontal * 2.0));
        }

        bool IsLargeUnnamedTaskbarHost(
            const std::wstring& typeName,
            const std::wstring& elementName,
            const wux::FrameworkElement& element)
        {
            if (!elementName.empty()) {
                return false;
            }

            if (element.ActualWidth() < 1000.0 || element.ActualHeight() > 52.0) {
                return false;
            }

            return
                typeName.find(L"DesktopWindowXamlSource") != std::wstring::npos ||
                typeName.find(L"ScrollContentPresenter") != std::wstring::npos ||
                typeName == L"Windows.UI.Xaml.Controls.Border" ||
                typeName == L"Windows.UI.Xaml.Controls.Grid";
        }
    }

    void TaskbarModifier::OnElementRemoved(const std::wstring& key)
    {
        m_appliedElements.erase(key);
    }

    TaskbarModifier::TaskbarModifier(const VelvetConfig& config)
        : m_config(config)
    {
        Log::Info(L"TaskbarModifier: creado");
        Log::Info(L"  Floating enabled", m_config.floating.enabled ? L"true" : L"false");
        Log::Info(L"  Glass enabled", m_config.glass.enabled ? L"true" : L"false");
    }

    // ============================================================
    // OnElementDiscovered - main dispatch
    //
    // Builds a "Type#Name" key and decides what to apply.
    // Only processes elements relevant to taskbar modification.
    // Skips elements with ActualWidth=0 (HoverFlyout duplicates).
    // ============================================================
    void TaskbarModifier::OnElementDiscovered(
        const std::wstring& typeName,
        const std::wstring& elementName,
        const wux::FrameworkElement& element)
    {
        // Build compound key: "Type#Name"
        std::wstring key = typeName + L"#" + elementName;
        const bool allowRepeatedApply =
            elementName.empty() ||
            elementName == L"TaskListButton" ||
            elementName == L"LaunchListButton" ||
            elementName == L"Icon";

        // Skip zero-width elements (flyout duplicates of BackgroundFill, etc.)
        if (element.ActualWidth() < 1.0) {
            return;
        }

        // Skip already-applied elements
        if (!allowRepeatedApply && m_appliedElements.count(key)) {
            return;
        }

        try {
            if (m_config.floating.enabled &&
                IsLargeUnnamedTaskbarHost(typeName, elementName, element)) {
                ApplyHostContainerMetrics(typeName, elementName, element);
            }

            // --- Layout layer: center the frame once Win32 geometry is narrowed ---
            if (m_config.floating.enabled &&
                (elementName == L"TaskbarFrame" ||
                 typeName.find(L"TaskbarFrame") != std::wstring::npos)) {
                ApplyTaskbarFrameLayout(element);
                if (!allowRepeatedApply) {
                    m_appliedElements.insert(key);
                }
            }
            else if (elementName == L"LaunchListButton" ||
                     typeName.find(L"Taskbar.TaskListButton") != std::wstring::npos) {
                ApplyTaskbarButtonMetrics(element);
                if (!allowRepeatedApply) {
                    m_appliedElements.insert(key);
                }
            }
            else if (elementName == L"Icon" &&
                     typeName.find(L"Windows.UI.Xaml.Controls.Image") != std::wstring::npos) {
                ApplyIconMetrics(element);
                if (!allowRepeatedApply) {
                    m_appliedElements.insert(key);
                }
            }

            // --- Visual styling for the main capsule container ---
            if (elementName == L"RootGrid" && m_config.floating.enabled) {
                ApplyRootGridStyle(element);

                if (m_config.glass.enabled) {
                    ApplyGlassEffect(element);
                }

                if (!allowRepeatedApply) {
                    m_appliedElements.insert(key);
                }
            }

            // --- Background capsule host ---
            if (elementName == L"BackgroundFill" &&
                typeName.find(L"Rectangle") != std::wstring::npos) {
                ClearBackgroundFill(element);
                if (!allowRepeatedApply) {
                    m_appliedElements.insert(key);
                }
            }
            else if (elementName == L"BackgroundStroke" &&
                     typeName.find(L"Rectangle") != std::wstring::npos) {
                ClearBackgroundStroke(element);
                if (!allowRepeatedApply) {
                    m_appliedElements.insert(key);
                }
            }
            else if (elementName == L"ScreenEdgeStroke") {
                HideScreenEdgeStroke(element);
                if (!allowRepeatedApply) {
                    m_appliedElements.insert(key);
                }
            }
            else if (elementName == L"SystemTrayFrameGrid") {
                ApplySystemTrayFrameLayout(element);
                if (!allowRepeatedApply) {
                    m_appliedElements.insert(key);
                }
            }
        }
        catch (const winrt::hresult_error& e) {
            wchar_t buf[512];
            swprintf_s(buf, L"TaskbarModifier: error aplicando a %s - 0x%08X",
                key.c_str(), static_cast<unsigned int>(e.code()));
            Log::Info(buf);
        }
    }

    void TaskbarModifier::ApplyHostContainerMetrics(
        const std::wstring& typeName,
        const std::wstring&,
        const wux::FrameworkElement& element)
    {
        const double targetHeight = std::max(48.0, m_config.floating.taskbarHeight);

        element.VerticalAlignment(wux::VerticalAlignment::Stretch);
        element.Height(targetHeight);
        element.MinHeight(targetHeight);
        element.MaxHeight(targetHeight);
        element.Margin(wux::ThicknessHelper::FromLengths(0.0, 0.0, 0.0, 0.0));

        wchar_t buf[256];
        swprintf_s(buf, L"TaskbarModifier: host %s -> height=%.0f",
            typeName.c_str(), targetHeight);
        Log::Info(buf);
    }

    // ============================================================
    // ApplyTaskbarFrameLayout
    //
    // This is the XAML companion to the Win32 geometry hooks:
    // center the frame and let it size to content instead of
    // insisting on a stretched dock-like layout.
    // ============================================================
    void TaskbarModifier::ApplyTaskbarFrameLayout(const wux::FrameworkElement& element)
    {
        const double targetWidth = ComputeDesiredTaskbarWidth(element, m_config);
        const double targetHeight = std::max(48.0, m_config.floating.taskbarHeight);

        element.HorizontalAlignment(wux::HorizontalAlignment::Center);
        element.VerticalAlignment(wux::VerticalAlignment::Stretch);
        element.Width(targetWidth);
        element.MinWidth(targetWidth);
        element.MaxWidth(targetWidth);
        element.Height(targetHeight);
        element.MinHeight(targetHeight);
        element.MaxHeight(targetHeight);
        element.Margin(wux::ThicknessHelper::FromLengths(0.0, 0.0, 0.0, 0.0));

        wchar_t buf[256];
        swprintf_s(buf, L"TaskbarModifier: TaskbarFrame -> centered width=%.0f height=%.0f",
            targetWidth, targetHeight);
        Log::Info(buf);
    }

    void TaskbarModifier::ApplyTaskbarButtonMetrics(const wux::FrameworkElement& element)
    {
        const double buttonWidth = std::max(44.0, m_config.floating.taskbarButtonWidth);
        const double buttonHeight = std::max(48.0, m_config.floating.taskbarHeight - 8.0);

        element.Width(buttonWidth);
        element.MinWidth(buttonWidth);
        element.MaxWidth(buttonWidth);
        element.Height(buttonHeight);
        element.MinHeight(buttonHeight);
        element.MaxHeight(buttonHeight);
    }

    void TaskbarModifier::ApplyIconMetrics(const wux::FrameworkElement& element)
    {
        auto image = element.try_as<wux::Controls::Image>();
        if (!image) {
            return;
        }

        const double iconSize = std::clamp(
            m_config.floating.iconSize,
            20.0,
            32.0);

        image.Width(iconSize);
        image.Height(iconSize);
        image.Stretch(wux::Media::Stretch::Uniform);
    }

    // ============================================================
    // ApplySystemTrayFrameLayout
    //
    // Lucent pulls the tray cluster inward so it sits inside the
    // same floating capsule instead of hugging the screen edge.
    // ============================================================
    void TaskbarModifier::ApplySystemTrayFrameLayout(const wux::FrameworkElement& element)
    {
        auto grid = element.try_as<wux::Controls::Grid>();
        if (!grid) {
            return;
        }

        const double trayCompensation = std::clamp(
            element.ActualWidth() * 0.20,
            28.0,
            56.0);
        const double trayShift = -std::clamp(
            m_config.floating.marginHorizontal + trayCompensation,
            180.0,
            320.0);

        auto transform = wux::Media::TranslateTransform();
        transform.X(trayShift);
        transform.Y(-2.0);

        grid.RenderTransform(transform);
        grid.HorizontalAlignment(wux::HorizontalAlignment::Right);
        grid.VerticalAlignment(wux::VerticalAlignment::Stretch);
        grid.Margin(wux::ThicknessHelper::FromLengths(0.0, 0.0, 0.0, 0.0));
        grid.Padding(wux::ThicknessHelper::FromUniformLength(0.0));
        grid.Background(wux::Media::SolidColorBrush(
            winrt::Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)));
        grid.CornerRadius(wux::CornerRadiusHelper::FromRadii(0.0, 0.0, 0.0, 0.0));

        wchar_t buf[256];
        swprintf_s(buf, L"TaskbarModifier: SystemTrayFrameGrid -> shiftX=%.0f trayW=%.0f",
            trayShift, element.ActualWidth());
        Log::Info(buf);
    }

    // ============================================================
    // ApplyRootGridStyle
    //
    // Rounds corners and adds padding + subtle border.
    // Uses try_as<Grid> because RootGrid is always a Grid,
    // but we handle the case defensively.
    // ============================================================
    void TaskbarModifier::ApplyRootGridStyle(const wux::FrameworkElement& element)
    {
        auto grid = element.try_as<wux::Controls::Grid>();
        if (!grid) {
            Log::Info(L"TaskbarModifier: RootGrid no es Grid, skip");
            return;
        }

        double cr = m_config.floating.cornerRadius;
        double pad = m_config.floating.padding;
        const double targetWidth = ComputeDesiredTaskbarWidth(element, m_config);
        const double targetHeight = std::max(48.0, m_config.floating.taskbarHeight);
        const double innerBottomInset = std::clamp(
            m_config.floating.marginBottom * 0.35,
            4.0,
            8.0);
        const double visualHeight = std::max(48.0, targetHeight - innerBottomInset);
        grid.HorizontalAlignment(wux::HorizontalAlignment::Center);
        grid.VerticalAlignment(wux::VerticalAlignment::Top);
        grid.Width(targetWidth);
        grid.MinWidth(targetWidth);
        grid.MaxWidth(targetWidth);
        grid.Height(visualHeight);
        grid.MinHeight(visualHeight);
        grid.MaxHeight(visualHeight);
        grid.CornerRadius(wux::CornerRadiusHelper::FromRadii(cr, cr, cr, cr));
        grid.Padding(wux::ThicknessHelper::FromLengths(pad, 0.0, pad, 0.0));
        grid.Margin(wux::ThicknessHelper::FromLengths(0.0, 0.0, 0.0, innerBottomInset));
        grid.Background(CreateTaskbarBackgroundBrush());

        // Border
        if (m_config.border.enabled) {
            grid.BorderThickness(wux::ThicknessHelper::FromUniformLength(
                m_config.border.thickness));

            if (m_config.border.color == "auto") {
                // explorer.exe doesn't have a UWP Application object,
                // so we can't query system theme resources directly.
                // Use a semi-transparent white that looks good on both
                // light and dark taskbar backgrounds.
                auto fallback = wux::Media::SolidColorBrush(
                    winrt::Windows::UI::ColorHelper::FromArgb(18, 255, 255, 255));
                grid.BorderBrush(fallback);
            }
            else {
                // Custom color from config
                uint32_t rgb = 0x808080;
                try {
                    std::string hex = m_config.border.color;
                    if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);
                    rgb = static_cast<uint32_t>(std::stoul(hex, nullptr, 16));
                }
                catch (...) {}

                auto brush = wux::Media::SolidColorBrush(
                    winrt::Windows::UI::ColorHelper::FromArgb(
                        255,
                        static_cast<uint8_t>((rgb >> 16) & 0xFF),
                        static_cast<uint8_t>((rgb >> 8) & 0xFF),
                        static_cast<uint8_t>(rgb & 0xFF)));
                grid.BorderBrush(brush);
            }
        }

        wchar_t buf[256];
        swprintf_s(buf, L"TaskbarModifier: RootGrid -> Width=%.0f CornerRadius=%.0f Padding=%.0f Border=%s",
            targetWidth, cr, pad, m_config.border.enabled ? L"on" : L"off");
        Log::Info(buf);
    }

    // ============================================================
    // CreateTaskbarBackgroundBrush
    //
    // The XAML layer stays very subtle; the real blur comes from
    // the Composition host backdrop attached to BackgroundFill.
    // ============================================================
    wux::Media::Brush TaskbarModifier::CreateTaskbarBackgroundBrush() const
    {
        auto overlay = wux::Media::SolidColorBrush(
            winrt::Windows::UI::ColorHelper::FromArgb(8, 255, 255, 255));
        return overlay;
    }

    // ============================================================
    // ClearBackgroundFill - make the solid background transparent
    // ============================================================
    void TaskbarModifier::ClearBackgroundFill(const wux::FrameworkElement& element)
    {
        auto rect = element.try_as<wux::Shapes::Rectangle>();
        if (!rect) return;

        rect.Visibility(wux::Visibility::Visible);
        rect.Fill(wux::Media::SolidColorBrush(
            winrt::Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0)));
        Log::Info(L"TaskbarModifier: BackgroundFill -> Transparent");
    }

    // ============================================================
    // ClearBackgroundStroke - make the top stroke transparent
    // ============================================================
    void TaskbarModifier::ClearBackgroundStroke(const wux::FrameworkElement& element)
    {
        auto rect = element.try_as<wux::Shapes::Rectangle>();
        if (!rect) return;

        auto transparent = wux::Media::SolidColorBrush(
            winrt::Windows::UI::ColorHelper::FromArgb(0, 0, 0, 0));
        rect.Fill(transparent);

        Log::Info(L"TaskbarModifier: BackgroundStroke -> Transparent");
    }

    // ============================================================
    // HideScreenEdgeStroke - collapse the edge line
    // ============================================================
    void TaskbarModifier::HideScreenEdgeStroke(const wux::FrameworkElement& element)
    {
        element.Visibility(wux::Visibility::Collapsed);
        Log::Info(L"TaskbarModifier: ScreenEdgeStroke -> Collapsed");
    }

    // ============================================================
    // Uint32ToColor - helper
    // ============================================================
    winrt::Windows::UI::Color TaskbarModifier::Uint32ToColor(uint32_t rgb, float opacity) const
    {
        return winrt::Windows::UI::ColorHelper::FromArgb(
            static_cast<uint8_t>(opacity * 255.0f),
            static_cast<uint8_t>((rgb >> 16) & 0xFF),
            static_cast<uint8_t>((rgb >> 8) & 0xFF),
            static_cast<uint8_t>(rgb & 0xFF));
    }

    // ============================================================
    // ApplyGlassEffect
    //
    // Creates a Composition effect chain:
    //   1. CompositionBackdropBrush (captures what's behind)
    //   2. GaussianBlur on the backdrop
    //   3. Saturation boost
    //   4. Tint overlay (semi-transparent color)
    //
    // The effect is applied as a SpriteVisual behind the
    // RootGrid content using ElementCompositionPreview.
    //
    // Note: This uses Windows.UI.Composition directly, NOT
    // AcrylicBrush, which has known multi-monitor bugs.
    // ============================================================
    void TaskbarModifier::ApplyGlassEffect(const wux::FrameworkElement& targetElement)
    {
        try {
            // Get the Visual for this element
            auto elementVisual = wux::Hosting::ElementCompositionPreview::GetElementVisual(targetElement);
            if (!elementVisual) {
            Log::Info(L"TaskbarModifier: no se pudo obtener Visual del host glass");
            return;
        }

            // Get or cache compositor
            if (!m_compositor) {
                m_compositor = elementVisual.Compositor();
            }

            // Host backdrop brush (system blur, similar to Acrylic
            // but managed by DWM, works on multi-monitor)
            auto hostBrush = m_compositor.CreateHostBackdropBrush();

            // Step 2: Add tint overlay
            auto tintColor = Uint32ToColor(m_config.glass.tintColor, m_config.glass.tintOpacity);
            auto tintBrush = m_compositor.CreateColorBrush(tintColor);

            // Insert tint above glass, below content
            // We need a ContainerVisual to stack them
            auto container = m_compositor.CreateContainerVisual();
            container.RelativeSizeAdjustment({ 1.0f, 1.0f });

            // Glass layer (bottom)
            auto glassLayer = m_compositor.CreateSpriteVisual();
            glassLayer.RelativeSizeAdjustment({ 1.0f, 1.0f });
            glassLayer.Brush(hostBrush);
            container.Children().InsertAtBottom(glassLayer);

            // Tint layer (middle)
            auto tintLayer = m_compositor.CreateSpriteVisual();
            tintLayer.RelativeSizeAdjustment({ 1.0f, 1.0f });
            tintLayer.Brush(tintBrush);
            container.Children().InsertAtTop(tintLayer);

            // Specular highlight (top edge gradient)
            if (m_config.glass.specularOpacity > 0.01f) {
                auto specVisual = m_compositor.CreateSpriteVisual();
                specVisual.RelativeSizeAdjustment({ 1.0f, 0.0f });
                specVisual.Size({ 0.0f, 1.0f }); // 1px tall, stretches width

                auto specColor = Uint32ToColor(
                    m_config.glass.specularColor,
                    m_config.glass.specularOpacity);
                auto specBrush = m_compositor.CreateColorBrush(specColor);
                specVisual.Brush(specBrush);
                container.Children().InsertAtTop(specVisual);
            }

            // Apply the container as background visual
            wux::Hosting::ElementCompositionPreview::SetElementChildVisual(targetElement, container);

            // Apply rounded clip to BOTH the container and the element visual.
            //
            // Critical: bind clip.Size to elementVisual.Size, NOT container.Size.
            // container uses RelativeSizeAdjustment so its .Size property stays {0,0}
            // (the compositor resolves the actual size at render time but doesn't
            // expose it back). elementVisual.Size IS explicitly set by XAML and
            // correctly tracks the rendered element dimensions.
            float cr = static_cast<float>(m_config.floating.cornerRadius);

            // Clip 1: on the container (clips the Composition glass layers)
            auto containerClip = m_compositor.CreateRoundedRectangleGeometry();
            containerClip.CornerRadius({ cr, cr });
            auto containerSizeAnim = m_compositor.CreateExpressionAnimation(L"visual.Size");
            containerSizeAnim.SetReferenceParameter(L"visual", elementVisual);
            containerClip.StartAnimation(L"Size", containerSizeAnim);
            container.Clip(m_compositor.CreateGeometricClip(containerClip));

            // Clip 2: on the element visual itself (clips everything — XAML + Composition)
            auto elementClip = m_compositor.CreateRoundedRectangleGeometry();
            elementClip.CornerRadius({ cr, cr });
            auto elementSizeAnim = m_compositor.CreateExpressionAnimation(L"visual.Size");
            elementSizeAnim.SetReferenceParameter(L"visual", elementVisual);
            elementClip.StartAnimation(L"Size", elementSizeAnim);
            elementVisual.Clip(m_compositor.CreateGeometricClip(elementClip));

            Log::Info(L"TaskbarModifier: Glass effect aplicado en RootGrid");
        }
        catch (const winrt::hresult_error& e) {
            wchar_t buf[256];
            swprintf_s(buf, L"TaskbarModifier: error aplicando glass - 0x%08X",
                static_cast<unsigned int>(e.code()));
            Log::Info(buf);
        }
    }

} // namespace Velvet
