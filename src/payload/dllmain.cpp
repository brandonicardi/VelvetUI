// ============================================================
// VelvetUI - payload DLL (velvet.dll)
// Fase 3: Floating taskbar + Liquid Glass
//
// Strategy:
//   1. DLL is injected into explorer.exe
//   2. Worker thread loads config.json and creates TaskbarModifier
//   3. MinHook intercepts taskbar geometry APIs (SetWindowPos/GetWindowRect)
//   4. InitializeXamlDiagnosticsEx boots the TAP/XAML pipeline
//   5. VisualTreeWatcher + TaskbarModifier style the XAML tree
// ============================================================

#include <Windows.h>
#include <combaseapi.h>

#include <algorithm>
#include <memory>
#include <string>

#include <MinHook.h>

#include "velvet_tap.h"
#include "simple_factory.h"
#include "config_loader.h"
#include "taskbar_modifier.h"
#include "internal_taskbar_hooks.h"

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
// Global state
// ============================================================
HMODULE g_hModule = nullptr;
HANDLE  g_hWorkerThread = nullptr;

// Shared TaskbarModifier - created in the worker thread and
// passed to VisualTreeWatcher through VelvetTAP.
std::shared_ptr<Velvet::TaskbarModifier> g_modifier;

static Velvet::VelvetConfig g_runtimeConfig{};
static bool g_configLoaded = false;

static HWND g_hTaskbar = nullptr;

using SetWindowPos_t = decltype(&SetWindowPos);
using GetWindowRect_t = decltype(&GetWindowRect);

static SetWindowPos_t g_origSetWindowPos = nullptr;
static GetWindowRect_t g_origGetWindowRect = nullptr;
static bool g_hooksInitialized = false;
static bool g_loggedSetWindowPosHook = false;
static bool g_loggedGetWindowRectHook = false;

// ============================================================
// Helpers
// ============================================================
static bool IsWindowOwnedByCurrentProcess(HWND hWnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);
    return pid == GetCurrentProcessId();
}

static std::wstring GetWindowClassName(HWND hWnd)
{
    wchar_t className[128]{};
    if (GetClassNameW(hWnd, className, static_cast<int>(_countof(className))) <= 0) {
        return {};
    }

    return className;
}

static bool IsTrackedTaskbarWindow(HWND hWnd)
{
    if (!hWnd || !IsWindow(hWnd) || !IsWindowOwnedByCurrentProcess(hWnd)) {
        return false;
    }

    const auto className = GetWindowClassName(hWnd);
    return className == L"Shell_TrayWnd" || className == L"Shell_SecondaryTrayWnd";
}

static HWND FindPrimaryTaskbarWindow()
{
    HWND hwnd = nullptr;

    while ((hwnd = FindWindowExW(nullptr, hwnd, L"Shell_TrayWnd", nullptr)) != nullptr) {
        if (IsWindowOwnedByCurrentProcess(hwnd)) {
            return hwnd;
        }
    }

    return nullptr;
}

static BOOL GetWindowRectRaw(HWND hWnd, LPRECT rect)
{
    if (g_origGetWindowRect) {
        return g_origGetWindowRect(hWnd, rect);
    }

    return ::GetWindowRect(hWnd, rect);
}

static bool ComputeFloatingRect(HWND hWnd, int requestedHeight, RECT* outRect)
{
    if (!outRect) {
        return false;
    }

    HMONITOR hMon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi)) {
        return false;
    }

    RECT currentRect{};
    if (!GetWindowRectRaw(hWnd, &currentRect)) {
        currentRect = mi.rcMonitor;
    }

    const int monitorWidth = mi.rcMonitor.right - mi.rcMonitor.left;
    const int monitorHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;
    const int minWidth = 320;

    int height = requestedHeight > 0 ? requestedHeight : (currentRect.bottom - currentRect.top);
    const int configuredHeight = static_cast<int>(g_runtimeConfig.floating.taskbarHeight);
    if (configuredHeight > 0) {
        height = configuredHeight;
    }
    height = std::max(1, height);

    int insetX = static_cast<int>(g_runtimeConfig.floating.marginHorizontal);
    int insetBottom = static_cast<int>(g_runtimeConfig.floating.marginBottom);

    const int maxInsetX = std::max(0, (monitorWidth - minWidth) / 2);
    insetX = std::clamp(insetX, 0, maxInsetX);
    insetBottom = std::clamp(insetBottom, 0, std::max(0, monitorHeight - height));

    int width = monitorWidth - (insetX * 2);
    width = std::clamp(width, minWidth, monitorWidth);

    outRect->left = mi.rcMonitor.left + ((monitorWidth - width) / 2);
    outRect->right = outRect->left + width;
    outRect->top = mi.rcMonitor.bottom - insetBottom - height;
    outRect->bottom = outRect->top + height;

    if (outRect->top < mi.rcMonitor.top) {
        outRect->top = mi.rcMonitor.top;
        outRect->bottom = outRect->top + height;
    }

    return true;
}

static void LogFloatingRect(const wchar_t* prefix, const RECT& rect)
{
    wchar_t buffer[256];
    swprintf_s(
        buffer,
        L"%s left=%ld top=%ld right=%ld bottom=%ld width=%ld height=%ld",
        prefix,
        rect.left,
        rect.top,
        rect.right,
        rect.bottom,
        rect.right - rect.left,
        rect.bottom - rect.top);
    Log::Info(buffer);
}

static void ForceInitialFloatingReposition(HWND hWnd)
{
    if (!hWnd || !g_runtimeConfig.floating.enabled) {
        return;
    }

    RECT currentRect{};
    if (!GetWindowRectRaw(hWnd, &currentRect)) {
        Log::Info(L"Floating: no se pudo leer la rect actual para el reposicionamiento inicial");
        return;
    }

    RECT targetRect{};
    if (!ComputeFloatingRect(hWnd, currentRect.bottom - currentRect.top, &targetRect)) {
        Log::Info(L"Floating: no se pudo calcular la rect flotante inicial");
        return;
    }

    LogFloatingRect(L"Floating: rect actual", currentRect);
    LogFloatingRect(L"Floating: rect objetivo", targetRect);

    SetWindowPos(
        hWnd,
        nullptr,
        targetRect.left,
        targetRect.top,
        targetRect.right - targetRect.left,
        targetRect.bottom - targetRect.top,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static void TriggerTaskbarRelayout(HWND hWnd)
{
    Velvet::InternalTaskbarHooks::RequestShellRefresh(hWnd);
}

// ============================================================
// API hooks
// ============================================================
static BOOL WINAPI Hooked_SetWindowPos(
    HWND hWnd,
    HWND hWndInsertAfter,
    int X,
    int Y,
    int cx,
    int cy,
    UINT uFlags)
{
    if (g_configLoaded && g_runtimeConfig.floating.enabled && IsTrackedTaskbarWindow(hWnd)) {
        RECT targetRect{};
        const int requestedHeight = cy > 0 ? cy : 0;
        if (ComputeFloatingRect(hWnd, requestedHeight, &targetRect)) {
            if (!g_loggedSetWindowPosHook) {
                LogFloatingRect(L"Floating hook SetWindowPos", targetRect);
                g_loggedSetWindowPosHook = true;
            }
            X = targetRect.left;
            Y = targetRect.top;
            cx = targetRect.right - targetRect.left;
            cy = targetRect.bottom - targetRect.top;
            uFlags &= ~(SWP_NOMOVE | SWP_NOSIZE);
        }
    }

    return g_origSetWindowPos(
        hWnd,
        hWndInsertAfter,
        X,
        Y,
        cx,
        cy,
        uFlags);
}

static BOOL WINAPI Hooked_GetWindowRect(HWND hWnd, LPRECT lpRect)
{
    const BOOL ok = g_origGetWindowRect(hWnd, lpRect);

    if (!ok || !lpRect) {
        return ok;
    }

    if (g_configLoaded && g_runtimeConfig.floating.enabled && IsTrackedTaskbarWindow(hWnd)) {
        RECT targetRect{};
        const int currentHeight = lpRect->bottom - lpRect->top;
        if (ComputeFloatingRect(hWnd, currentHeight, &targetRect)) {
            if (!g_loggedGetWindowRectHook) {
                LogFloatingRect(L"Floating hook GetWindowRect", targetRect);
                g_loggedGetWindowRectHook = true;
            }
            *lpRect = targetRect;
        }
    }

    return ok;
}

static bool InitializeFloatingHooks()
{
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        Log::Info(L"MinHook: MH_Initialize fallo", static_cast<DWORD>(status));
        return false;
    }

    void* originalSetWindowPos = nullptr;
    status = MH_CreateHookApi(
        L"user32",
        "SetWindowPos",
        reinterpret_cast<LPVOID>(&Hooked_SetWindowPos),
        &originalSetWindowPos);
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
        Log::Info(L"MinHook: hook SetWindowPos fallo", static_cast<DWORD>(status));
        return false;
    }
    g_origSetWindowPos = reinterpret_cast<SetWindowPos_t>(originalSetWindowPos);
    if (!g_origSetWindowPos) {
        g_origSetWindowPos = &SetWindowPos;
    }

    void* originalGetWindowRect = nullptr;
    status = MH_CreateHookApi(
        L"user32",
        "GetWindowRect",
        reinterpret_cast<LPVOID>(&Hooked_GetWindowRect),
        &originalGetWindowRect);
    if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
        Log::Info(L"MinHook: hook GetWindowRect fallo", static_cast<DWORD>(status));
        return false;
    }
    g_origGetWindowRect = reinterpret_cast<GetWindowRect_t>(originalGetWindowRect);
    if (!g_origGetWindowRect) {
        g_origGetWindowRect = &GetWindowRect;
    }

    status = MH_EnableHook(MH_ALL_HOOKS);
    if (status != MH_OK && status != MH_ERROR_ENABLED) {
        Log::Info(L"MinHook: MH_EnableHook fallo", static_cast<DWORD>(status));
        return false;
    }

    g_hooksInitialized = true;
    g_loggedSetWindowPosHook = false;
    g_loggedGetWindowRectHook = false;
    Log::Info(L"Floating: hooks SetWindowPos/GetWindowRect activos");
    return true;
}

static void ShutdownFloatingHooks()
{
    if (!g_hooksInitialized) {
        return;
    }

    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();

    g_hooksInitialized = false;
    g_loggedSetWindowPosHook = false;
    g_loggedGetWindowRectHook = false;
    g_origSetWindowPos = nullptr;
    g_origGetWindowRect = nullptr;
}

// ============================================================
// COM exports: DllGetClassObject / DllCanUnloadNow
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

    Log::Info(L"=== VelvetUI Fase 3: Floating Taskbar + Liquid Glass ===");

    // Step 1: Load configuration
    g_runtimeConfig = Velvet::ConfigLoader::Load();
    g_configLoaded = true;

    // Step 2: Create TaskbarModifier (shared with VelvetTAP)
    g_modifier = std::make_shared<Velvet::TaskbarModifier>(g_runtimeConfig);

    // Step 3: Resolve the actual taskbar window owned by explorer.exe
    g_hTaskbar = FindPrimaryTaskbarWindow();
    if (g_hTaskbar) {
        Log::Info(L"Floating: Shell_TrayWnd resuelta para explorer.exe");
    } else {
        Log::Info(L"Floating: no se encontro Shell_TrayWnd para el proceso actual");
    }

    // Step 4: Install geometry hooks before the XAML pipeline starts
    if (g_runtimeConfig.floating.enabled) {
        InitializeFloatingHooks();
        Velvet::InternalTaskbarHooks::Initialize(g_runtimeConfig, g_hTaskbar);
        if (g_hTaskbar) {
            ForceInitialFloatingReposition(g_hTaskbar);
            TriggerTaskbarRelayout(g_hTaskbar);
        }
    }

    // Step 5: Initialize TAP (triggers XAML diagnostics pipeline)
    HRESULT hr = InitializeTAP();
    if (FAILED(hr)) {
        Log::Hresult(L"InitializeTAP fallo", hr);
        Log::Info(L"=== Fase 3 FALLIDA ===");
        return 1;
    }

    Log::Info(L"=== Fase 3 inicializada OK ===");
    return 0;
}

// ============================================================
// DLL Entry Point
// ============================================================
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
        Velvet::InternalTaskbarHooks::Shutdown();
        ShutdownFloatingHooks();
        g_modifier.reset();
        g_hTaskbar = nullptr;
        g_configLoaded = false;

        if (g_hWorkerThread) {
            CloseHandle(g_hWorkerThread);
            g_hWorkerThread = nullptr;
        }

        Log::Info(L"=== VelvetUI payload descargada ===");
        break;
    }
    }

    return TRUE;
}
