// ============================================================
// VelvetUI - TaskbarModifier
// Fase 3: Applies floating layout + Liquid Glass effect
//
// Two stages:
//   1. XAML properties (margin, corner radius, fill, visibility)
//      → Applied directly on FrameworkElement via WinRT casts
//   2. Composition effects (blur, saturation, tint, specular)
//      → Applied via Windows.UI.Composition on the Visual layer
//
// Thread safety:
//   All methods are called from the XAML UI thread
//   (OnVisualTreeChange callback), so no locking needed here.
//   Config is loaded once at construction and is read-only.
// ============================================================

#pragma once

#include "config.h"

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Composition.h>

#include <string>
#include <unordered_set>

namespace wux  = winrt::Windows::UI::Xaml;
namespace wucc = winrt::Windows::UI::Composition;

namespace Velvet {

    class TaskbarModifier {
    public:
        explicit TaskbarModifier(const VelvetConfig& config);

        // Called from VisualTreeWatcher::ProcessElementAdd
        // for each named element. Decides what to apply based
        // on the element's type+name key.
        void OnElementDiscovered(
            const std::wstring& typeName,
            const std::wstring& elementName,
            const wux::FrameworkElement& element);

        // Called from VisualTreeWatcher::ProcessElementRemove.
        // Clears the applied-state for this element so it can be
        // re-styled if XAML recreates it (e.g. after a relayout).
        void OnElementRemoved(const std::wstring& key);

    private:
        // --- XAML property modifications ---
        void ApplyTaskbarFrameLayout(const wux::FrameworkElement& taskbarFrame);
        void ApplyHostContainerMetrics(
            const std::wstring& typeName,
            const std::wstring& elementName,
            const wux::FrameworkElement& host);
        void ApplyTaskbarButtonMetrics(const wux::FrameworkElement& button);
        void ApplyIconMetrics(const wux::FrameworkElement& icon);
        void ApplySystemTrayFrameLayout(const wux::FrameworkElement& grid);
        void ApplyRootGridStyle(const wux::FrameworkElement& rootGrid);
        void ClearBackgroundFill(const wux::FrameworkElement& rect);
        void ClearBackgroundStroke(const wux::FrameworkElement& rect);
        void HideScreenEdgeStroke(const wux::FrameworkElement& rect);
        wux::Media::Brush CreateTaskbarBackgroundBrush() const;

        // --- Composition effects (Liquid Glass) ---
        void ApplyGlassEffect(const wux::FrameworkElement& targetElement);
        winrt::Windows::UI::Color Uint32ToColor(uint32_t rgb, float opacity) const;

        VelvetConfig m_config;

        // Track which elements we've already modified so we don't
        // re-apply on duplicate callbacks (explorer can re-fire).
        // Key = "Type#Name" string.
        std::unordered_set<std::wstring> m_appliedElements;

        // We need the compositor reference for glass effects.
        // Set once when we first get a valid Visual.
        wucc::Compositor m_compositor{ nullptr };
    };

} // namespace Velvet
