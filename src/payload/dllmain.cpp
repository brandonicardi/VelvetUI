// ============================================================
// VelvetUI - payload DLL (velvet.dll)
// Fase 2: Visual Tree Access via XAML Diagnostics
//
// Flow:
//   1. DLL is injected into explorer.exe (by VelvetInjector)
//   2. DllMain spawns a worker thread
//   3. Worker thread loads Windows.UI.Xaml.dll and calls
//      InitializeXamlDiagnosticsEx with our CLSID
//   4. XAML framework calls DllGetClassObject -> creates VelvetTAP
//   5. XAML framework calls VelvetTAP::SetSite with IVisualTreeService3
//   6. VelvetTAP registers VisualTreeWatcher for callbacks
//   7. OnVisualTreeChange fires for every XAML element add/remove
// ============================================================

#include <Windows.h>
#include <string>
#include <combaseapi.h>

#include "velvet_tap.h"
#include "simple_factory.h"

// ============================================================
// Logging - OutputDebugString (visible in DebugView)
// ============================================================
namespace Log {
    void Info(const wchar_t* msg) {
        std::wstring formatted = L"[VelvetUI] ";
        formatted += msg;
        OutputDebugStringW(formatted.c_str());
    }

    void Info(const wchar_t* msg, DWORD value) {
        wchar_t buffer[512];
        swprintf_s(buffer, L"[VelvetUI] %s: %lu", msg, value);
        OutputDebugStringW(buffer);
    }

    void Info(const wchar_t* msg, const wchar_t* detail) {
        wchar_t buffer[1024];
        swprintf_s(buffer, L"[VelvetUI] %s: %s", msg, detail);
        OutputDebugStringW(buffer);
    }

    void Hresult(const wchar_t* msg, HRESULT hr) {
        wchar_t buffer[512];
        swprintf_s(buffer, L"[VelvetUI] %s: 0x%08X", msg, static_cast<unsigned int>(hr));
        OutputDebugStringW(buffer);
    }
}

// ============================================================
// COM exports: DllGetClassObject / DllCanUnloadNow
//
// Use STDAPI to match the declarations in combaseapi.h.
// The .def file ensures these are exported with correct names.
// ============================================================

_Use_decl_annotations_
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) try
{
    Log::Info(L"DllGetClassObject llamado");

    if (rclsid == CLSID_VelvetTAP) {
        *ppv = nullptr;
        return winrt::make<SimpleFactory<VelvetTAP>>().as(riid, ppv);
    }

    return CLASS_E_CLASSNOTAVAILABLE;
}
catch (...)
{
    return winrt::to_hresult();
}

_Use_decl_annotations_
STDAPI DllCanUnloadNow(void)
{
    return winrt::get_module_lock() ? S_FALSE : S_OK;
}

// ============================================================
// InitializeTAP - loads Windows.UI.Xaml.dll and calls
// InitializeXamlDiagnosticsEx to kick off the TAP registration.
// ============================================================

using PFN_InitializeXamlDiagnosticsEx = decltype(&InitializeXamlDiagnosticsEx);

static HRESULT GetCurrentModulePath(wchar_t* path, DWORD maxLen)
{
    HMODULE hModule = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetCurrentModulePath),
            &hModule))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    DWORD len = GetModuleFileNameW(hModule, path, maxLen);
    if (len == 0 || len >= maxLen) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

static HRESULT InitializeTAP()
{
    wchar_t dllPath[MAX_PATH];
    HRESULT hr = GetCurrentModulePath(dllPath, MAX_PATH);
    if (FAILED(hr)) {
        Log::Hresult(L"No se pudo obtener la ruta de velvet.dll", hr);
        return hr;
    }

    Log::Info(L"Ruta de velvet.dll", dllPath);

    HMODULE hXaml = LoadLibraryExW(
        L"Windows.UI.Xaml.dll",
        nullptr,
        LOAD_LIBRARY_SEARCH_SYSTEM32);

    if (!hXaml) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Hresult(L"No se pudo cargar Windows.UI.Xaml.dll", hr);
        return hr;
    }

    Log::Info(L"Windows.UI.Xaml.dll cargada OK");

    auto pfnInit = reinterpret_cast<PFN_InitializeXamlDiagnosticsEx>(
        GetProcAddress(hXaml, "InitializeXamlDiagnosticsEx"));

    if (!pfnInit) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Hresult(L"InitializeXamlDiagnosticsEx no encontrada", hr);
        return hr;
    }

    // Retry with incrementing endpoint names.
    // InitializeXamlDiagnosticsEx fails with ERROR_NOT_FOUND if the
    // endpoint name is already taken or if XAML isn't ready yet.
    // TranslucentTB 2025.1 uses this same approach.
    hr = E_FAIL;
    for (int i = 1; i <= 10 && FAILED(hr); ++i)
    {
        wchar_t endpoint[64];
        swprintf_s(endpoint, L"VisualDiagConnection%d", i);

        Log::Info(L"Intentando endpoint", endpoint);

        hr = pfnInit(
            endpoint,
            GetCurrentProcessId(),
            nullptr,
            dllPath,
            CLSID_VelvetTAP,
            nullptr);

        if (FAILED(hr)) {
            Log::Hresult(endpoint, hr);
            Sleep(500);
        }
    }

    if (FAILED(hr)) {
        Log::Hresult(L"InitializeXamlDiagnosticsEx fallo tras 10 intentos", hr);
        return hr;
    }

    Log::Info(L"InitializeXamlDiagnosticsEx OK - esperando callbacks");
    return S_OK;
}

// ============================================================
// Worker thread
// ============================================================
DWORD WINAPI VelvetWorker(LPVOID)
{
    Sleep(1000);

    Log::Info(L"=== VelvetUI Fase 2: Visual Tree Access ===");

    HRESULT hr = InitializeTAP();
    if (FAILED(hr)) {
        Log::Hresult(L"InitializeTAP fallo", hr);
        Log::Info(L"=== Fase 2 FALLIDA ===");
        return 1;
    }

    Log::Info(L"=== Fase 2 inicializada OK ===");
    Log::Info(L"Revisa DebugView para los eventos del Visual Tree");

    while (true) {
        Sleep(60000);
    }

    return 0;
}

// ============================================================
// DLL Entry Point
// ============================================================
HMODULE g_hModule = nullptr;
HANDLE  g_hWorkerThread = nullptr;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(hModule);
        g_hModule = hModule;

        Log::Info(L"=== VelvetUI payload cargada ===");
        Log::Info(L"PID", GetCurrentProcessId());

        g_hWorkerThread = CreateThread(
            nullptr, 0, VelvetWorker, nullptr, 0, nullptr);
        break;
    }
    case DLL_PROCESS_DETACH:
    {
        if (g_hWorkerThread) {
            WaitForSingleObject(g_hWorkerThread, 2000);
            CloseHandle(g_hWorkerThread);
        }
        Log::Info(L"=== VelvetUI payload descargada ===");
        break;
    }
    }

    return TRUE;
}
