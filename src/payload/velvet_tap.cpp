// ============================================================
// VelvetUI - VelvetTAP implementation
// Fase 3: Forwards TaskbarModifier to VisualTreeWatcher
// ============================================================

#include "velvet_tap.h"

// Global TaskbarModifier created in VelvetWorker (dllmain.cpp).
// Set before InitializeTAP, read-only after that.
extern std::shared_ptr<Velvet::TaskbarModifier> g_modifier;

// ============================================================
// SetTaskbarModifier (kept for potential future use, but
// SetSite now reads the global directly)
// ============================================================
void VelvetTAP::SetTaskbarModifier(std::shared_ptr<Velvet::TaskbarModifier> modifier)
{
    m_modifier = std::move(modifier);
}

// ============================================================
// SetSite
//
// Called by the XAML diagnostics framework:
//   - With a valid pUnkSite when connecting (we get IVisualTreeService3)
//   - With nullptr when disconnecting
//
// Unlike the m417z gist which uses a global g_disabled flag
// and never truly unregisters, we do a clean UnadviseVisualTreeChange.
// ============================================================
HRESULT VelvetTAP::SetSite(IUnknown* pUnkSite) try
{
    // If we had a previous connection, clean up properly
    if (m_visualTreeService && m_watcher) {
        Log::Info(L"VelvetTAP: Desregistrando watcher anterior");
        HRESULT hr = m_visualTreeService->UnadviseVisualTreeChange(m_watcher.get());
        if (FAILED(hr)) {
            Log::Hresult(L"VelvetTAP: UnadviseVisualTreeChange fallo", hr);
        }
        m_watcher->SetXamlDiagnostics(nullptr);
    }

    m_visualTreeService = nullptr;

    if (!pUnkSite) {
        Log::Info(L"VelvetTAP: SetSite(nullptr) - desconectado");
        return S_OK;
    }

    // Get IVisualTreeService3 from the site
    m_visualTreeService = FromIUnknown<IVisualTreeService3>(pUnkSite);

    if (m_visualTreeService) {
        Log::Info(L"VelvetTAP: IVisualTreeService3 obtenido");

        // Create the watcher if it doesn't exist yet
        if (!m_watcher) {
            m_watcher = winrt::make_self<VisualTreeWatcher>();
            Log::Info(L"VelvetTAP: VisualTreeWatcher creado");
        }

        // Pass the TaskbarModifier to the watcher (read from global)
        auto& modifier = m_modifier ? m_modifier : g_modifier;
        if (modifier) {
            m_watcher->SetTaskbarModifier(modifier);
        }

        // Wire up diagnostics and register for callbacks
        auto diagnostics = m_visualTreeService.as<IXamlDiagnostics>();
        m_watcher->SetXamlDiagnostics(diagnostics);

        HRESULT hr = m_visualTreeService->AdviseVisualTreeChange(m_watcher.get());
        if (FAILED(hr)) {
            Log::Hresult(L"VelvetTAP: AdviseVisualTreeChange fallo", hr);
            return hr;
        }

        Log::Info(L"VelvetTAP: AdviseVisualTreeChange registrado OK");
    }

    return S_OK;
}
catch (...)
{
    HRESULT hr = winrt::to_hresult();
    Log::Hresult(L"VelvetTAP: SetSite exception", hr);
    return hr;
}

// ============================================================
// GetSite
// ============================================================
HRESULT VelvetTAP::GetSite(REFIID riid, void** ppvSite) noexcept
{
    return m_visualTreeService.as(riid, ppvSite);
}
