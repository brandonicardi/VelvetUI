// ============================================================
// VelvetUI - VisualTreeWatcher implementation
// ============================================================

#include "visual_tree_watcher.h"

#include <algorithm>
#include <array>

// ============================================================
// Types we care about for taskbar modification.
// Everything else gets filtered out early to avoid
// expensive COM cross-apartment calls (FromHandle).
//
// This list covers the main taskbar element types.
// Expand as needed when targeting new elements.
// ============================================================
static constexpr std::array<const wchar_t*, 12> kRelevantTypeSubstrings = {
    L"Grid",
    L"Border",
    L"StackPanel",
    L"Canvas",
    L"ContentPresenter",
    L"Button",
    L"TextBlock",
    L"Image",
    L"Rectangle",
    L"Taskbar",
    L"SystemTray",
    L"DesktopWindowXamlSource",
};

// ============================================================
// SetXamlDiagnostics
// ============================================================
void VisualTreeWatcher::SetXamlDiagnostics(winrt::com_ptr<IXamlDiagnostics> diagnostics)
{
    m_diagnostics = std::move(diagnostics);
}

// ============================================================
// Destructor
// ============================================================
VisualTreeWatcher::~VisualTreeWatcher()
{
    Log::Info(L"VisualTreeWatcher destruido");
    Log::Info(L"  Total eventos recibidos", m_totalEvents.load());
    Log::Info(L"  Eventos procesados (post-filtro)", m_filteredEvents.load());
}

// ============================================================
// IsRelevantType - early filter
//
// Checks if the XAML type name contains any of our target
// substrings. This avoids calling FromHandle (which is an
// expensive cross-apartment COM call) for types we don't need.
// ============================================================
bool VisualTreeWatcher::IsRelevantType(const wchar_t* typeName) const
{
    if (!typeName) return false;

    for (const auto* substr : kRelevantTypeSubstrings) {
        if (wcsstr(typeName, substr) != nullptr) {
            return true;
        }
    }
    return false;
}

// ============================================================
// OnVisualTreeChange - main callback
// ============================================================
HRESULT VisualTreeWatcher::OnVisualTreeChange(
    ParentChildRelation relation,
    VisualElement element,
    VisualMutationType mutationType) try
{
    m_totalEvents++;

    // Early filter: skip types we don't care about
    if (!IsRelevantType(element.Type)) {
        return S_OK;
    }

    m_filteredEvents++;

    switch (mutationType) {
    case VisualMutationType::Add:
        ProcessElementAdd(element);
        break;

    case VisualMutationType::Remove:
        ProcessElementRemove(element);
        break;
    }

    return S_OK;
}
catch (...)
{
    HRESULT hr = winrt::to_hresult();
    Log::Hresult(L"OnVisualTreeChange error", hr);
    return hr;
}

// ============================================================
// ProcessElementAdd
//
// When an element is added, try to resolve it as a
// FrameworkElement and cache it if it has a name.
// ============================================================
void VisualTreeWatcher::ProcessElementAdd(const VisualElement& element)
{
    wchar_t buf[512];
    swprintf_s(buf, L"+ [Add] Type=%s  Handle=%llu  Children=%u",
        element.Type ? element.Type : L"(null)",
        element.Handle,
        element.NumChildren);
    Log::Info(buf);

    try {
        // First try: direct cast to FrameworkElement
        wux::FrameworkElement fe{ nullptr };

        try {
            const auto inspectable = FromHandle<wf::IInspectable>(element.Handle);
            fe = inspectable.try_as<wux::FrameworkElement>();
        }
        catch (...) {
            // Not a FrameworkElement directly - might be a DesktopWindowXamlSource
        }

        // Fallback: DesktopWindowXamlSource (only for the root XAML node)
        if (!fe) {
            try {
                auto dxs = FromHandle<winrt::Windows::UI::Xaml::Hosting::DesktopWindowXamlSource>(element.Handle);
                fe = dxs.Content().try_as<wux::FrameworkElement>();
            }
            catch (...) {
                // Not a DesktopWindowXamlSource either - skip
                return;
            }
        }

        if (!fe) return;

        auto name = fe.Name();
        swprintf_s(buf, L"    Name=\"%s\"  ActualW=%.0f  ActualH=%.0f",
            name.empty() ? L"(unnamed)" : name.c_str(),
            fe.ActualWidth(),
            fe.ActualHeight());
        Log::Info(buf);

        // Cache elements that have a name (these are the ones we'll
        // want to target in Phase 3 for style modification)
        if (!name.empty()) {
            std::lock_guard lock(m_cacheMutex);
            CachedElement cached;
            cached.handle  = element.Handle;
            cached.type    = element.Type ? element.Type : L"";
            cached.name    = std::wstring(name);
            cached.element = fe;

            m_elementCache[cached.name] = std::move(cached);

            swprintf_s(buf, L"    >> Cached: \"%s\" (total cached: %zu)",
                name.c_str(), m_elementCache.size());
            Log::Info(buf);
        }
    }
    catch (const winrt::hresult_error& e) {
        Log::Hresult(L"    ProcessElementAdd error", e.code());
    }
}

// ============================================================
// ProcessElementRemove
// ============================================================
void VisualTreeWatcher::ProcessElementRemove(const VisualElement& element)
{
    wchar_t buf[512];
    swprintf_s(buf, L"- [Remove] Type=%s  Handle=%llu",
        element.Type ? element.Type : L"(null)",
        element.Handle);
    Log::Info(buf);

    // Remove from cache if present
    std::lock_guard lock(m_cacheMutex);
    for (auto it = m_elementCache.begin(); it != m_elementCache.end(); ++it) {
        if (it->second.handle == element.Handle) {
            Log::Info(L"    >> Removed from cache", it->first.c_str());
            m_elementCache.erase(it);
            break;
        }
    }
}

// ============================================================
// GetCachedElement - thread-safe cache lookup
// ============================================================
const CachedElement* VisualTreeWatcher::GetCachedElement(const std::wstring& name) const
{
    std::lock_guard lock(m_cacheMutex);
    auto it = m_elementCache.find(name);
    return (it != m_elementCache.end()) ? &it->second : nullptr;
}

// ============================================================
// OnElementStateChanged - not used yet, stub
// ============================================================
HRESULT VisualTreeWatcher::OnElementStateChanged(
    InstanceHandle, VisualElementState, LPCWSTR) noexcept
{
    return S_OK;
}
