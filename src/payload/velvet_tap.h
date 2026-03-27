// ============================================================
// VelvetUI - VelvetTAP (Test Automation Provider)
//
// Implements IObjectWithSite. When InitializeXamlDiagnosticsEx
// calls CoCreateInstance with our CLSID, the XAML framework
// creates a VelvetTAP instance and calls SetSite() with an
// IVisualTreeService3 pointer. We use that to register our
// VisualTreeWatcher for callbacks.
// ============================================================

#pragma once

#include "xaml_diagnostics.h"
#include "visual_tree_watcher.h"

#include <winrt/base.h>
#include <ocidl.h>

// {7A8B3C4D-1234-5678-9ABC-DEF012345678}
// Unique CLSID for VelvetUI's TAP. Must match the CLSID passed
// to InitializeXamlDiagnosticsEx.
static constexpr CLSID CLSID_VelvetTAP = {
    0x7a8b3c4d, 0x1234, 0x5678,
    { 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78 }
};

struct VelvetTAP : winrt::implements<VelvetTAP, IObjectWithSite, winrt::non_agile>
{
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* pUnkSite) override;
    HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** ppvSite) noexcept override;

private:
    template<typename T>
    static winrt::com_ptr<T> FromIUnknown(IUnknown* pSite)
    {
        winrt::com_ptr<IUnknown> site;
        site.copy_from(pSite);
        return site.as<T>();
    }

    winrt::com_ptr<IVisualTreeService3>  m_visualTreeService;
    winrt::com_ptr<VisualTreeWatcher>    m_watcher;
};
