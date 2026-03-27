// ============================================================
// VelvetUI - VisualTreeWatcher
//
// Implements IVisualTreeServiceCallback2 to receive notifications
// when XAML visual tree elements are added/removed.
//
// Optimizations vs m417z gist:
//   - Early type filtering: only processes relevant XAML types
//   - Caches key element handles for later modification
//   - Single DesktopWindowXamlSource resolution at init
// ============================================================

#pragma once

#include "xaml_diagnostics.h"

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>

#include <unordered_map>
#include <string>
#include <mutex>

namespace wf  = winrt::Windows::Foundation;
namespace wux = winrt::Windows::UI::Xaml;

// Forward-declare log functions (defined in dllmain.cpp)
namespace Log {
    void Info(const wchar_t* msg);
    void Info(const wchar_t* msg, const wchar_t* detail);
    void Info(const wchar_t* msg, DWORD value);
    void Hresult(const wchar_t* msg, HRESULT hr);
}

// ============================================================
// Element cache entry
// ============================================================
struct CachedElement {
    InstanceHandle                handle = 0;
    std::wstring                  type;
    std::wstring                  name;
    wux::FrameworkElement         element{ nullptr };
};

// ============================================================
// VisualTreeWatcher
// ============================================================
struct VisualTreeWatcher : winrt::implements<VisualTreeWatcher,
                                             IVisualTreeServiceCallback2,
                                             winrt::non_agile>
{
    VisualTreeWatcher() = default;

    VisualTreeWatcher(const VisualTreeWatcher&)            = delete;
    VisualTreeWatcher& operator=(const VisualTreeWatcher&) = delete;
    VisualTreeWatcher(VisualTreeWatcher&&)                 = delete;
    VisualTreeWatcher& operator=(VisualTreeWatcher&&)      = delete;

    void SetXamlDiagnostics(winrt::com_ptr<IXamlDiagnostics> diagnostics);
    const CachedElement* GetCachedElement(const std::wstring& name) const;

    ~VisualTreeWatcher();

private:
    // IVisualTreeServiceCallback2
    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(
        ParentChildRelation relation,
        VisualElement element,
        VisualMutationType mutationType) override;

    HRESULT STDMETHODCALLTYPE OnElementStateChanged(
        InstanceHandle element,
        VisualElementState elementState,
        LPCWSTR context) noexcept override;

    template<typename T>
    T FromHandle(InstanceHandle handle)
    {
        wf::IInspectable obj;
        winrt::check_hresult(m_diagnostics->GetIInspectableFromHandle(
            handle,
            reinterpret_cast<::IInspectable**>(winrt::put_abi(obj))));
        return obj.as<T>();
    }

    bool IsRelevantType(const wchar_t* typeName) const;
    void ProcessElementAdd(const VisualElement& element);
    void ProcessElementRemove(const VisualElement& element);

    winrt::com_ptr<IXamlDiagnostics>                m_diagnostics{ nullptr };
    mutable std::mutex                              m_cacheMutex;
    std::unordered_map<std::wstring, CachedElement> m_elementCache;

    std::atomic<uint32_t> m_totalEvents{ 0 };
    std::atomic<uint32_t> m_filteredEvents{ 0 };
};
