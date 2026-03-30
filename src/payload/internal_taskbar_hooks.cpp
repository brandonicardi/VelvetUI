// ============================================================
// VelvetUI - Internal taskbar frame-size hooks
//
// Goal:
//   Make the shell measure the taskbar at the configured height,
//   instead of only enlarging the HWND while the internal XAML
//   tree keeps rendering at 48px.
//
// Strategy:
//   1. Resolve internal taskbar symbols from Taskbar.View.dll
//      using DbgHelp + public symbols.
//   2. Hook the frame-size methods that decide the taskbar height.
//   3. Hook relayout/update methods so the XAML frame can grow
//      beyond the default 48px limit.
// ============================================================

#include "internal_taskbar_hooks.h"
#include "windhawk_symbol_resolver.h"

#include <Windows.h>
#include <Unknwn.h>
#include <DbgHelp.h>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>

#include <MinHook.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Forward-declare log functions (defined in dllmain.cpp)
namespace Log {
    void Info(const wchar_t* msg);
    void Info(const wchar_t* msg, const wchar_t* detail);
    void Info(const wchar_t* msg, DWORD value);
    void Hresult(const wchar_t* msg, HRESULT hr);
}

namespace wux = winrt::Windows::UI::Xaml;

namespace Velvet::InternalTaskbarHooks {
    namespace {
        HANDLE g_process = GetCurrentProcess();
        HWND g_primaryTaskbar = nullptr;
        double g_taskbarHeight = 0.0;
        double g_originalTaskbarHeight = 0.0;
        bool g_initialized = false;
        bool g_symbolsInitialized = false;
        bool g_applyingSettings = false;
        bool g_pendingMeasureOverride = false;
        bool g_inSystemTrayControllerUpdateFrameSize = false;
        bool g_inTaskbarControllerUpdateFrameHeight = false;
        bool g_loggedTrayRefreshHook = false;
        bool g_loggedTrayHandleSettingException = false;
        bool g_loggedUpdateFrameHeightException = false;

        constexpr DWORD kSymTagFunction = 5;
        constexpr DWORD kSymTagData = 7;
        constexpr DWORD kSymTagPublicSymbol = 10;
        constexpr bool kEnableAggressiveTrayRefresh = false;
        constexpr wchar_t kInternalHooksBuildMarker[] = L"safe-mode";

        std::vector<DWORD64> g_taskbarModuleBases;
        std::unique_ptr<WindhawkSymbolResolver> g_windhawkSymbolResolver;
        double* g_constant48Value = nullptr;

        using TaskbarConfiguration_GetFrameSize_t = double(WINAPI*)(int enumTaskbarSize);
        using SystemTrayController_GetFrameSize_t = double(WINAPI*)(void* pThis, int enumTaskbarSize);
        using SystemTraySecondaryController_GetFrameSize_t = double(WINAPI*)(void* pThis, int enumTaskbarSize);
        using TaskbarFrame_put_Height_t = void(WINAPI*)(void* pThis, double value);
        using TaskbarFrame_put_MaxHeight_t = void(WINAPI*)(void* pThis, double value);
        using SystemTrayFrame_put_Height_t = void(WINAPI*)(void* pThis, double value);
        using TaskbarFrame_MeasureOverride_t = int(WINAPI*)(void* pThis, winrt::Windows::Foundation::Size size, winrt::Windows::Foundation::Size* resultSize);
        using TaskbarController_UpdateFrameHeight_t = void(WINAPI*)(void* pThis);
        using SystemTrayController_UpdateFrameSize_t = void(WINAPI*)(void* pThis);
        using SystemTraySecondaryController_UpdateFrameSize_t = void(WINAPI*)(void* pThis);
        using TaskbarController_OnGroupingModeChanged_t = void(WINAPI*)(void* pThis);
        using TrayUI_GetMinSize_t = void(WINAPI*)(void* pThis, HMONITOR monitor, SIZE* size);
        using TrayUI__StuckTrayChange_t = void(WINAPI*)(void* pThis);
        using TrayUI__HandleSettingChange_t = void(WINAPI*)(void* pThis, HWND hWnd, UINT msg, UINT_PTR wParam, LONG_PTR lParam);

        TaskbarConfiguration_GetFrameSize_t g_origTaskbarConfiguration_GetFrameSize = nullptr;
        SystemTrayController_GetFrameSize_t g_origSystemTrayController_GetFrameSize = nullptr;
        SystemTraySecondaryController_GetFrameSize_t g_origSystemTraySecondaryController_GetFrameSize = nullptr;
        TaskbarFrame_put_Height_t g_origTaskbarFrame_put_Height = nullptr;
        TaskbarFrame_put_MaxHeight_t g_origTaskbarFrame_put_MaxHeight = nullptr;
        SystemTrayFrame_put_Height_t g_origSystemTrayFrame_put_Height = nullptr;
        TaskbarFrame_MeasureOverride_t g_origTaskbarFrame_MeasureOverride = nullptr;
        TaskbarController_UpdateFrameHeight_t g_origTaskbarController_UpdateFrameHeight = nullptr;
        SystemTrayController_UpdateFrameSize_t g_origSystemTrayController_UpdateFrameSize = nullptr;
        SystemTraySecondaryController_UpdateFrameSize_t g_origSystemTraySecondaryController_UpdateFrameSize = nullptr;
        TaskbarController_OnGroupingModeChanged_t g_taskbarController_OnGroupingModeChanged_Address = nullptr;
        TrayUI_GetMinSize_t g_origTrayUI_GetMinSize = nullptr;
        TrayUI__StuckTrayChange_t g_origTrayUI__StuckTrayChange = nullptr;
        TrayUI__HandleSettingChange_t g_origTrayUI__HandleSettingChange = nullptr;

        LONG g_taskbarFrameOffset = 0;
        LONG g_systemTrayLastHeightOffset = 0;

        bool IsSupportedTaskbarSize(int enumTaskbarSize)
        {
            return enumTaskbarSize == 1 || enumTaskbarSize == 2;
        }

        bool ProtectAndMemcpy(DWORD protect, void* dst, const void* src, size_t size)
        {
            DWORD oldProtect = 0;
            if (!VirtualProtect(dst, size, protect, &oldProtect)) {
                return false;
            }

            memcpy(dst, src, size);

            DWORD dummy = 0;
            VirtualProtect(dst, size, oldProtect, &dummy);
            return true;
        }

        std::wstring BuildSymbolPath()
        {
            wchar_t localAppData[MAX_PATH]{};
            DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
            std::wstring cachePath;
            if (len > 0 && len < MAX_PATH) {
                cachePath = localAppData;
                cachePath += L"\\VelvetUI\\symbols";
                CreateDirectoryW((std::wstring(localAppData) + L"\\VelvetUI").c_str(), nullptr);
                CreateDirectoryW(cachePath.c_str(), nullptr);
            } else {
                cachePath = L".\\symbols";
                CreateDirectoryW(cachePath.c_str(), nullptr);
            }

            return L"srv*" + cachePath + L"*https://msdl.microsoft.com/download/symbols";
        }

        bool EnsureSymbolsInitialized()
        {
            if (g_symbolsInitialized) {
                return true;
            }

            SymSetOptions(
                SYMOPT_UNDNAME |
                SYMOPT_DEFERRED_LOADS |
                SYMOPT_FAIL_CRITICAL_ERRORS |
                SYMOPT_AUTO_PUBLICS);

            const auto symbolPath = BuildSymbolPath();
            if (!SymInitializeW(g_process, symbolPath.c_str(), FALSE)) {
                Log::Info(L"InternalHooks: SymInitializeW fallo", GetLastError());
                return false;
            }

            g_symbolsInitialized = true;
            Log::Info(L"InternalHooks: DbgHelp inicializado");
            return true;
        }

        void LoadModuleSymbolsIfPresent(const wchar_t* moduleName, bool allowLoadFromSystem32 = false)
        {
            HMODULE module = GetModuleHandleW(moduleName);
            if (!module && allowLoadFromSystem32) {
                module = LoadLibraryExW(moduleName, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            }
            if (!module) {
                return;
            }

            wchar_t modulePath[MAX_PATH]{};
            if (!GetModuleFileNameW(module, modulePath, MAX_PATH)) {
                return;
            }

            SymRefreshModuleList(g_process);

            DWORD64 base = SymLoadModuleExW(
                g_process,
                nullptr,
                modulePath,
                nullptr,
                reinterpret_cast<DWORD64>(module),
                0,
                nullptr,
                0);

            if (base == 0) {
                DWORD error = GetLastError();
                if (error == ERROR_SUCCESS) {
                    base = reinterpret_cast<DWORD64>(module);
                } else {
                    return;
                }
            }

            if (std::find(g_taskbarModuleBases.begin(), g_taskbarModuleBases.end(), base) !=
                g_taskbarModuleBases.end()) {
                return;
            }

            g_taskbarModuleBases.push_back(base);

            wchar_t buf[256];
            swprintf_s(buf, L"InternalHooks: simbolos cargados para %s", moduleName);
            Log::Info(buf);
        }

        bool LoadTaskbarShellSymbols()
        {
            g_taskbarModuleBases.clear();
            if (!g_windhawkSymbolResolver) {
                g_windhawkSymbolResolver = WindhawkSymbolResolver::TryCreate();
                if (g_windhawkSymbolResolver) {
                    Log::Info(L"InternalHooks: resolver DIA estilo Windhawk activo");
                } else {
                    Log::Info(L"InternalHooks: resolver DIA estilo Windhawk no disponible");
                }
            }

            LoadModuleSymbolsIfPresent(L"Taskbar.View.dll");
            LoadModuleSymbolsIfPresent(L"taskbar.view.dll");
            LoadModuleSymbolsIfPresent(L"ExplorerExtensions.dll");
            LoadModuleSymbolsIfPresent(L"explorerextensions.dll");
            LoadModuleSymbolsIfPresent(L"taskbar.dll", true);

            if (g_taskbarModuleBases.empty()) {
                Log::Info(L"InternalHooks: no se encontro Taskbar.View.dll, ExplorerExtensions.dll ni taskbar.dll");
                return false;
            }

            Log::Info(L"InternalHooks: simbolos shell cargados");
            return true;
        }

        struct SymbolMatch {
            DWORD64 address = 0;
            std::wstring matchedName;
            size_t patternIndex = static_cast<size_t>(-1);
            bool matchedUndecorated = false;
            bool usedWildcard = false;
            bool compilerGenerated = false;
            DWORD symTag = 0;
            ULONGLONG length = 0;
        };

        bool ContainsWildcard(const std::wstring& pattern)
        {
            return pattern.find(L'?') != std::wstring::npos ||
                   (!pattern.empty() &&
                    (pattern.front() == L'*' || pattern.back() == L'*'));
        }

        bool IsCompilerGeneratedSymbol(const std::wstring& symbolName)
        {
            return symbolName.find(L'`') != std::wstring::npos ||
                   symbolName.find(L"lambda") != std::wstring::npos ||
                   symbolName.find(L"implements_delegate") != std::wstring::npos;
        }

        bool IsBetterSymbolMatch(const SymbolMatch& candidate, const SymbolMatch& current)
        {
            if (candidate.patternIndex != current.patternIndex) {
                return candidate.patternIndex < current.patternIndex;
            }

            if (candidate.usedWildcard != current.usedWildcard) {
                return !candidate.usedWildcard;
            }

            if (candidate.compilerGenerated != current.compilerGenerated) {
                return !candidate.compilerGenerated;
            }

            auto tagPriority = [](DWORD symTag) {
                switch (symTag) {
                case kSymTagFunction:
                    return 0;
                case kSymTagPublicSymbol:
                    return 1;
                case kSymTagData:
                    return 2;
                default:
                    return 3;
                }
            };
            if (candidate.symTag != current.symTag) {
                return tagPriority(candidate.symTag) < tagPriority(current.symTag);
            }

            if (candidate.matchedUndecorated != current.matchedUndecorated) {
                return candidate.matchedUndecorated;
            }

            if (candidate.length != current.length) {
                return candidate.length > current.length;
            }

            if (candidate.matchedName.length() != current.matchedName.length()) {
                return candidate.matchedName.length() < current.matchedName.length();
            }

            return candidate.address < current.address;
        }

        struct SymbolSearchContext {
            std::vector<SymbolMatch> matches;
        };

        BOOL CALLBACK EnumSymbolCallback(PSYMBOL_INFOW info, ULONG, PVOID userContext)
        {
            auto* ctx = static_cast<SymbolSearchContext*>(userContext);
            if (!ctx || !info) {
                return FALSE;
            }

            SymbolMatch match{};
            match.address = info->Address;
            match.matchedName.assign(info->Name, info->NameLen);
            ctx->matches.push_back(std::move(match));
            return TRUE;
        }

        std::vector<SymbolMatch> ResolveSymbols(
            const std::vector<std::wstring>& patterns,
            const wchar_t* debugName)
        {
            if (g_windhawkSymbolResolver && !g_taskbarModuleBases.empty()) {
                std::vector<HMODULE> modules;
                modules.reserve(g_taskbarModuleBases.size());
                for (const auto moduleBase : g_taskbarModuleBases) {
                    modules.push_back(reinterpret_cast<HMODULE>(moduleBase));
                }

                const auto resolved = g_windhawkSymbolResolver->ResolveSymbols(modules, patterns);
                if (!resolved.empty()) {
                    std::vector<SymbolMatch> results;
                    results.reserve(resolved.size());
                    for (const auto& match : resolved) {
                        results.push_back(SymbolMatch{
                            match.address,
                            match.matchedName,
                            match.patternIndex,
                            match.matchedUndecorated,
                            match.usedWildcard,
                            match.compilerGenerated,
                            match.symTag,
                            match.length
                        });
                    }

                    const wchar_t* tagName = L"unknown";
                    switch (results.front().symTag) {
                    case kSymTagFunction:
                        tagName = L"function";
                        break;
                    case kSymTagPublicSymbol:
                        tagName = L"public";
                        break;
                    case kSymTagData:
                        tagName = L"data";
                        break;
                    }

                    wchar_t buf[1024];
                    swprintf_s(
                        buf,
                        L"InternalHooks: %s -> %s (matches=%zu, backend=DIA, %s/%s, %s, addr=0x%p)",
                        debugName,
                        results.front().matchedName.c_str(),
                        results.size(),
                        results.front().usedWildcard ? L"wildcard" : L"exact",
                        results.front().matchedUndecorated ? L"undecorated" : L"decorated",
                        tagName,
                        reinterpret_cast<void*>(results.front().address));
                    Log::Info(buf);
                    return results;
                }
            }

            std::vector<SymbolMatch> results;
            std::unordered_map<DWORD64, SymbolMatch> bestMatchesByAddress;

            for (const auto moduleBase : g_taskbarModuleBases) {
                for (size_t patternIndex = 0; patternIndex < patterns.size(); ++patternIndex) {
                    const auto& pattern = patterns[patternIndex];
                    SymbolSearchContext context{};

                    if (!SymEnumSymbolsW(
                            g_process,
                            moduleBase,
                            pattern.c_str(),
                            EnumSymbolCallback,
                            &context))
                    {
                        continue;
                    }

                    for (auto& match : context.matches) {
                        if (match.address == 0) {
                            continue;
                        }

                        match.patternIndex = patternIndex;
                        match.usedWildcard = ContainsWildcard(pattern);
                        match.matchedUndecorated = true;
                        match.compilerGenerated = IsCompilerGeneratedSymbol(match.matchedName);
                        match.symTag = kSymTagPublicSymbol;
                        match.length = 0;

                        auto it = bestMatchesByAddress.find(match.address);
                        if (it == bestMatchesByAddress.end() ||
                            IsBetterSymbolMatch(match, it->second)) {
                            bestMatchesByAddress[match.address] = std::move(match);
                        }
                    }
                }
            }

            results.reserve(bestMatchesByAddress.size());
            for (auto& [_, match] : bestMatchesByAddress) {
                results.push_back(std::move(match));
            }
            std::sort(results.begin(), results.end(), IsBetterSymbolMatch);

            if (results.empty()) {
                wchar_t buf[256];
                swprintf_s(buf, L"InternalHooks: no se encontro simbolo para %s", debugName);
                Log::Info(buf);
                return results;
            }

            wchar_t buf[1024];
            swprintf_s(
                buf,
                L"InternalHooks: %s -> %s (matches=%zu, %s, addr=0x%p)",
                debugName,
                results.front().matchedName.c_str(),
                results.size(),
                results.front().usedWildcard ? L"wildcard" : L"exact",
                reinterpret_cast<void*>(results.front().address));
            Log::Info(buf);
            return results;
        }

        std::optional<DWORD64> ResolveSymbol(
            const std::vector<std::wstring>& patterns,
            const wchar_t* debugName)
        {
            const auto matches = ResolveSymbols(patterns, debugName);
            if (matches.empty()) {
                return std::nullopt;
            }

            if (((wcscmp(debugName, L"TaskbarController::UpdateFrameHeight") == 0) ||
                 (wcscmp(debugName, L"SystemTrayController::UpdateFrameSize") == 0)) &&
                matches.size() > 1) {
                wchar_t buf[1024];
                swprintf_s(
                    buf,
                    L"InternalHooks: simbolo ambiguo para %s, se omite hook por seguridad",
                    debugName);
                Log::Info(buf);
                return std::nullopt;
            }

            if (matches.front().compilerGenerated && matches.front().usedWildcard) {
                wchar_t buf[1024];
                swprintf_s(
                    buf,
                    L"InternalHooks: descartando simbolo generado para %s -> %s",
                    debugName,
                    matches.front().matchedName.c_str());
                Log::Info(buf);
                return std::nullopt;
            }

            return matches.front().address;
        }

        LONG ResolveTaskbarFrameOffset()
        {
            if (g_taskbarFrameOffset != 0) {
                return g_taskbarFrameOffset;
            }

            const BYTE* p = reinterpret_cast<const BYTE*>(g_taskbarController_OnGroupingModeChanged_Address);
            if (!p) {
                return 0;
            }

            if (p[0] == 0x48 &&
                p[1] == 0x83 &&
                p[2] == 0xEC &&
                (p[4] == 0x48 || p[4] == 0x4C) &&
                p[5] == 0x8B &&
                (p[6] & 0xC0) == 0x80)
            {
                LONG offset = *reinterpret_cast<const LONG*>(p + 7);
                if (offset > 0 && offset < 0xFFFF) {
                    g_taskbarFrameOffset = offset;
                    Log::Info(L"InternalHooks: taskbarFrameOffset", static_cast<DWORD>(offset));
                    return offset;
                }
            }

            Log::Info(L"InternalHooks: taskbarFrameOffset no encontrado");
            return 0;
        }

        LONG ResolveSystemTrayLastHeightOffset()
        {
            if (g_systemTrayLastHeightOffset != 0) {
                return g_systemTrayLastHeightOffset;
            }

            const BYTE* start = reinterpret_cast<const BYTE*>(g_origSystemTrayController_UpdateFrameSize);
            if (!start) {
                return 0;
            }

            const BYTE* end = start + 0x200;
            for (const BYTE* p = start; p != end; ++p) {
                if (p[0] == 0x66 &&
                    p[1] == 0x0F &&
                    p[2] == 0x2E &&
                    p[3] == 0xB3 &&
                    p[8] == 0x7A &&
                    p[10] == 0x75)
                {
                    LONG offset = *reinterpret_cast<const LONG*>(p + 4);
                    if (offset > 0 && offset < 0xFFFF) {
                        g_systemTrayLastHeightOffset = offset;
                        Log::Info(L"InternalHooks: systemTrayLastHeightOffset", static_cast<DWORD>(offset));
                        return offset;
                    }
                }
            }

            Log::Info(L"InternalHooks: systemTrayLastHeightOffset no encontrado");
            return 0;
        }

        void RefreshTaskbarWindow()
        {
            if (!g_primaryTaskbar || !IsWindow(g_primaryTaskbar)) {
                return;
            }

            RECT rc{};
            if (!GetWindowRect(g_primaryTaskbar, &rc)) {
                return;
            }

            SetWindowPos(
                g_primaryTaskbar,
                nullptr,
                rc.left,
                rc.top,
                rc.right - rc.left,
                rc.bottom - rc.top,
                SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

            InvalidateRect(g_primaryTaskbar, nullptr, TRUE);
            UpdateWindow(g_primaryTaskbar);
        }

        void TriggerTaskbarSettingChange()
        {
            if (!g_primaryTaskbar || !IsWindow(g_primaryTaskbar)) {
                return;
            }

            SendMessageW(g_primaryTaskbar, WM_SETTINGCHANGE, SPI_SETLOGICALDPIOVERRIDE, 0);
        }

        int GetScaledTaskbarHeight()
        {
            UINT dpi = 96;
            if (g_primaryTaskbar && IsWindow(g_primaryTaskbar)) {
                dpi = GetDpiForWindow(g_primaryTaskbar);
                if (dpi == 0) {
                    dpi = 96;
                }
            }

            return MulDiv(static_cast<int>(std::lround(g_taskbarHeight)), static_cast<int>(dpi), 96);
        }

        bool ApplyFrameSizeConstantFallback()
        {
            if (!g_constant48Value || !(g_taskbarHeight > 0.0)) {
                return false;
            }

            if (g_originalTaskbarHeight == 0.0) {
                g_originalTaskbarHeight = *g_constant48Value;
            }

            const double newValue = g_taskbarHeight;
            if (!ProtectAndMemcpy(PAGE_READWRITE, g_constant48Value, &newValue, sizeof(newValue))) {
                Log::Info(L"InternalHooks: no se pudo parchear la constante 48px");
                return false;
            }

            wchar_t buf[256];
            swprintf_s(buf, L"InternalHooks: fallback constante 48px -> %.0f", g_taskbarHeight);
            Log::Info(buf);
            TriggerTaskbarSettingChange();
            return true;
        }

        void WaitForMeasureOverride()
        {
            if (!g_origTaskbarFrame_MeasureOverride) {
                return;
            }

            for (int i = 0; i < 100; ++i) {
                if (!g_pendingMeasureOverride) {
                    break;
                }

                Sleep(20);
            }
        }

        void ApplySettingsLikeWindhawk()
        {
            if (!g_primaryTaskbar || !IsWindow(g_primaryTaskbar) || !(g_taskbarHeight > 0.0)) {
                return;
            }

            const double targetHeight = std::max(2.0, g_taskbarHeight);
            g_applyingSettings = true;

            auto triggerHeight = [&](double heightValue) {
                g_taskbarHeight = std::max(2.0, heightValue);

                if (!g_origTaskbarConfiguration_GetFrameSize && g_constant48Value) {
                    const double patchedValue = g_taskbarHeight;
                    ProtectAndMemcpy(
                        PAGE_READWRITE,
                        g_constant48Value,
                        &patchedValue,
                        sizeof(patchedValue));
                }

                g_pendingMeasureOverride = true;
                TriggerTaskbarSettingChange();
                WaitForMeasureOverride();
            };

            triggerHeight(targetHeight - 1.0);
            triggerHeight(targetHeight);

            g_taskbarHeight = targetHeight;
            g_applyingSettings = false;
            RefreshTaskbarWindow();
        }

        template <typename T>
        bool CreateHookAtAddress(
            DWORD64 address,
            T detour,
            T* original,
            const wchar_t* debugName)
        {
            if (address == 0) {
                return false;
            }

            void* originalAddress = nullptr;
            const auto status = MH_CreateHook(
                reinterpret_cast<LPVOID>(address),
                reinterpret_cast<LPVOID>(detour),
                &originalAddress);

            if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
                Log::Info(debugName, static_cast<DWORD>(status));
                return false;
            }

            *original = reinterpret_cast<T>(originalAddress);
            return true;
        }

        double WINAPI Hooked_TaskbarConfiguration_GetFrameSize(int enumTaskbarSize)
        {
            if (!g_originalTaskbarHeight && g_origTaskbarConfiguration_GetFrameSize && IsSupportedTaskbarSize(enumTaskbarSize)) {
                g_originalTaskbarHeight = g_origTaskbarConfiguration_GetFrameSize(enumTaskbarSize);
            }

            if (g_taskbarHeight > 0 && IsSupportedTaskbarSize(enumTaskbarSize)) {
                return g_taskbarHeight;
            }

            return g_origTaskbarConfiguration_GetFrameSize(enumTaskbarSize);
        }

        double WINAPI Hooked_SystemTrayController_GetFrameSize(void* pThis, int enumTaskbarSize)
        {
            if (g_taskbarHeight > 0 && IsSupportedTaskbarSize(enumTaskbarSize)) {
                return g_taskbarHeight;
            }

            return g_origSystemTrayController_GetFrameSize(pThis, enumTaskbarSize);
        }

        double WINAPI Hooked_SystemTraySecondaryController_GetFrameSize(void* pThis, int enumTaskbarSize)
        {
            if (g_taskbarHeight > 0 && IsSupportedTaskbarSize(enumTaskbarSize)) {
                return g_taskbarHeight;
            }

            return g_origSystemTraySecondaryController_GetFrameSize(pThis, enumTaskbarSize);
        }

        void WINAPI Hooked_TaskbarFrame_put_Height(void* pThis, double value)
        {
            if (g_origTaskbarFrame_put_MaxHeight) {
                g_origTaskbarFrame_put_MaxHeight(
                    pThis,
                    std::numeric_limits<double>::infinity());
            }

            g_origTaskbarFrame_put_Height(pThis, value);
        }

        void WINAPI Hooked_SystemTrayFrame_put_Height(void* pThis, double value)
        {
            if (g_inSystemTrayControllerUpdateFrameSize) {
                value = std::numeric_limits<double>::quiet_NaN();
            }

            g_origSystemTrayFrame_put_Height(pThis, value);
        }

        int WINAPI Hooked_TaskbarFrame_MeasureOverride(
            void* pThis,
            winrt::Windows::Foundation::Size size,
            winrt::Windows::Foundation::Size* resultSize)
        {
            const int ret = g_origTaskbarFrame_MeasureOverride(pThis, size, resultSize);
            g_pendingMeasureOverride = false;
            return ret;
        }

        void WINAPI Hooked_SystemTrayController_UpdateFrameSize(void* pThis)
        {
            const LONG lastHeightOffset = ResolveSystemTrayLastHeightOffset();
            if (lastHeightOffset) {
                *reinterpret_cast<double*>(reinterpret_cast<BYTE*>(pThis) + lastHeightOffset) = 0.0;
            }

            g_inSystemTrayControllerUpdateFrameSize = true;
            g_origSystemTrayController_UpdateFrameSize(pThis);
            g_inSystemTrayControllerUpdateFrameSize = false;
        }

        void WINAPI Hooked_SystemTraySecondaryController_UpdateFrameSize(void* pThis)
        {
            g_inSystemTrayControllerUpdateFrameSize = true;
            g_origSystemTraySecondaryController_UpdateFrameSize(pThis);
            g_inSystemTrayControllerUpdateFrameSize = false;
        }

        void WINAPI Hooked_TrayUI_GetMinSize(void* pThis, HMONITOR monitor, SIZE* size)
        {
            g_origTrayUI_GetMinSize(pThis, monitor, size);

            if (!size || !(g_taskbarHeight > 0.0)) {
                return;
            }

            size->cy = std::max<LONG>(size->cy, GetScaledTaskbarHeight());
        }

        void WINAPI Hooked_TrayUI__HandleSettingChange(
            void* pThis,
            HWND hWnd,
            UINT msg,
            UINT_PTR wParam,
            LONG_PTR lParam)
        {
            __try {
                g_origTrayUI__HandleSettingChange(pThis, hWnd, msg, wParam, lParam);

                if (kEnableAggressiveTrayRefresh &&
                    g_applyingSettings &&
                    g_origTrayUI__StuckTrayChange) {
                    g_origTrayUI__StuckTrayChange(pThis);

                    if (!g_loggedTrayRefreshHook) {
                        Log::Info(L"InternalHooks: TrayUI::_StuckTrayChange forzado");
                        g_loggedTrayRefreshHook = true;
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                if (!g_loggedTrayHandleSettingException) {
                    Log::Info(L"InternalHooks: excepcion atrapada en TrayUI::_HandleSettingChange");
                    g_loggedTrayHandleSettingException = true;
                }
            }
        }

        bool TryGetTaskbarFrameQuerySource(
            void* pThis,
            LONG taskbarFrameOffset,
            IUnknown** querySource)
        {
            if (!querySource) {
                return false;
            }

            *querySource = nullptr;

            __try {
                if (taskbarFrameOffset <= 0) {
                    return false;
                }

                void* taskbarFrame =
                    *reinterpret_cast<void**>(reinterpret_cast<BYTE*>(pThis) + taskbarFrameOffset);
                if (!taskbarFrame) {
                    return false;
                }

                auto frameInterfaces = reinterpret_cast<IUnknown**>(taskbarFrame);
                if (!frameInterfaces[1]) {
                    return false;
                }

                *querySource = frameInterfaces[1];
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        bool TryQueryTaskbarFrameElement(IUnknown* querySource, void** elementAbi)
        {
            if (!querySource || !elementAbi) {
                return false;
            }

            *elementAbi = nullptr;

            __try {
                return SUCCEEDED(querySource->QueryInterface(
                    winrt::guid_of<wux::FrameworkElement>(),
                    elementAbi));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        bool TryInvokeTaskbarControllerUpdateFrameHeight(void* pThis)
        {
            __try {
                g_inTaskbarControllerUpdateFrameHeight = true;
                g_origTaskbarController_UpdateFrameHeight(pThis);
                g_inTaskbarControllerUpdateFrameHeight = false;
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                g_inTaskbarControllerUpdateFrameHeight = false;
                return false;
            }
        }

        void WINAPI Hooked_TaskbarController_UpdateFrameHeight(void* pThis)
        {
            wux::FrameworkElement taskbarFrameElement{ nullptr };

            const LONG taskbarFrameOffset = ResolveTaskbarFrameOffset();
            IUnknown* querySource = nullptr;
            if (TryGetTaskbarFrameQuerySource(pThis, taskbarFrameOffset, &querySource)) {
                TryQueryTaskbarFrameElement(querySource, winrt::put_abi(taskbarFrameElement));
            }

            if (taskbarFrameElement) {
                taskbarFrameElement.MaxHeight(std::numeric_limits<double>::infinity());
            }

            if (!TryInvokeTaskbarControllerUpdateFrameHeight(pThis)) {
                if (!g_loggedUpdateFrameHeightException) {
                    Log::Info(L"InternalHooks: excepcion atrapada en TaskbarController::UpdateFrameHeight");
                    g_loggedUpdateFrameHeightException = true;
                }
                return;
            }

            if (taskbarFrameElement) {
                taskbarFrameElement.MaxHeight(std::numeric_limits<double>::infinity());

                auto contentGrid = wux::Media::VisualTreeHelper::GetParent(taskbarFrameElement).try_as<wux::FrameworkElement>();
                if (contentGrid) {
                    const double frameHeight = taskbarFrameElement.Height();
                    const double contentGridHeight = contentGrid.Height();
                    if (contentGridHeight > 0 && contentGridHeight != frameHeight) {
                        contentGrid.Height(frameHeight);
                    }
                }
            }
        }

        bool EnableInternalHooks()
        {
            const auto enableStatus = MH_EnableHook(MH_ALL_HOOKS);
            if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
                Log::Info(L"InternalHooks: MH_EnableHook fallo", static_cast<DWORD>(enableStatus));
                return false;
            }

            return true;
        }
    } // namespace

    bool Initialize(const VelvetConfig& config, HWND primaryTaskbar)
    {
        if (g_initialized) {
            return true;
        }

        wchar_t markerBuf[256];
        swprintf_s(
            markerBuf,
            L"InternalHooks: build=%s trayRefresh=%s",
            kInternalHooksBuildMarker,
            kEnableAggressiveTrayRefresh ? L"on" : L"off");
        Log::Info(markerBuf);

        g_primaryTaskbar = primaryTaskbar;
        g_taskbarHeight = std::max(48.0, config.floating.taskbarHeight);

        if (!EnsureSymbolsInitialized()) {
            return false;
        }

        if (!LoadTaskbarShellSymbols()) {
            return false;
        }

        struct HookSpec {
            const wchar_t* debugName;
            std::vector<std::wstring> patterns;
            std::function<bool(DWORD64)> install;
        };

        const std::array<HookSpec, 9> hookSpecs = {
            HookSpec{
                L"TaskbarConfiguration::GetFrameSize",
                {
                    LR"(public: static double __cdecl winrt::Taskbar::implementation::TaskbarConfiguration::GetFrameSize(enum winrt::WindowsUdk::UI::Shell::TaskbarSize))",
                    L"*TaskbarConfiguration*GetFrameSize*"
                },
                [](DWORD64 address) {
                    return CreateHookAtAddress(
                        address,
                        &Hooked_TaskbarConfiguration_GetFrameSize,
                        &g_origTaskbarConfiguration_GetFrameSize,
                        L"InternalHooks: hook TaskbarConfiguration_GetFrameSize fallo");
                }},
            HookSpec{
                L"SystemTrayController::GetFrameSize",
                {
                    LR"(private: double __cdecl winrt::SystemTray::implementation::SystemTrayController::GetFrameSize(enum winrt::WindowsUdk::UI::Shell::TaskbarSize))",
                    L"*SystemTrayController*GetFrameSize*"
                },
                [](DWORD64 address) {
                    return CreateHookAtAddress(
                        address,
                        &Hooked_SystemTrayController_GetFrameSize,
                        &g_origSystemTrayController_GetFrameSize,
                        L"InternalHooks: hook SystemTrayController_GetFrameSize fallo");
                }},
            HookSpec{
                L"SystemTraySecondaryController::GetFrameSize",
                {
                    LR"(private: double __cdecl winrt::SystemTray::implementation::SystemTraySecondaryController::GetFrameSize(enum winrt::WindowsUdk::UI::Shell::TaskbarSize))",
                    L"*SystemTraySecondaryController*GetFrameSize*"
                },
                [](DWORD64 address) {
                    return CreateHookAtAddress(
                        address,
                        &Hooked_SystemTraySecondaryController_GetFrameSize,
                        &g_origSystemTraySecondaryController_GetFrameSize,
                        L"InternalHooks: hook SystemTraySecondaryController_GetFrameSize fallo");
                }},
            HookSpec{
                L"TaskbarController::UpdateFrameHeight",
                {
                    LR"(private: void __cdecl winrt::Taskbar::implementation::TaskbarController::UpdateFrameHeight(void))",
                    L"*TaskbarController*UpdateFrameHeight*"
                },
                [](DWORD64 address) {
                    return CreateHookAtAddress(
                        address,
                        &Hooked_TaskbarController_UpdateFrameHeight,
                        &g_origTaskbarController_UpdateFrameHeight,
                        L"InternalHooks: hook TaskbarController_UpdateFrameHeight fallo");
                }},
            HookSpec{
                L"SystemTrayController::UpdateFrameSize",
                {
                    LR"(private: void __cdecl winrt::SystemTray::implementation::SystemTrayController::UpdateFrameSize(void))",
                    L"*SystemTrayController*UpdateFrameSize*"
                },
                [](DWORD64 address) {
                    return CreateHookAtAddress(
                        address,
                        &Hooked_SystemTrayController_UpdateFrameSize,
                        &g_origSystemTrayController_UpdateFrameSize,
                        L"InternalHooks: hook SystemTrayController_UpdateFrameSize fallo");
                }},
            HookSpec{
                L"SystemTraySecondaryController::UpdateFrameSize",
                {
                    LR"(private: void __cdecl winrt::SystemTray::implementation::SystemTraySecondaryController::UpdateFrameSize(void))",
                    L"*SystemTraySecondaryController*UpdateFrameSize*"
                },
                [](DWORD64 address) {
                    return CreateHookAtAddress(
                        address,
                        &Hooked_SystemTraySecondaryController_UpdateFrameSize,
                        &g_origSystemTraySecondaryController_UpdateFrameSize,
                        L"InternalHooks: hook SystemTraySecondaryController_UpdateFrameSize fallo");
                }},
            HookSpec{
                L"TrayUI::GetMinSize",
                {
                    LR"(public: virtual void __cdecl TrayUI::GetMinSize(struct HMONITOR__ *,struct tagSIZE *))"
                },
                [](DWORD64 address) {
                    return CreateHookAtAddress(
                        address,
                        &Hooked_TrayUI_GetMinSize,
                        &g_origTrayUI_GetMinSize,
                        L"InternalHooks: hook TrayUI_GetMinSize fallo");
                }},
            HookSpec{
                L"TrayUI::_HandleSettingChange",
                {
                    LR"(public: void __cdecl TrayUI::_HandleSettingChange(struct HWND__ *,unsigned int,unsigned __int64,__int64))"
                },
                [](DWORD64 address) {
                    return CreateHookAtAddress(
                        address,
                        &Hooked_TrayUI__HandleSettingChange,
                        &g_origTrayUI__HandleSettingChange,
                        L"InternalHooks: hook TrayUI__HandleSettingChange fallo");
                }},
            HookSpec{
                L"TaskbarFrame::MeasureOverride",
                {
                    LR"(public: virtual int __cdecl winrt::impl::produce<struct winrt::Taskbar::implementation::TaskbarFrame,struct winrt::Windows::UI::Xaml::IFrameworkElementOverrides>::MeasureOverride(struct winrt::Windows::Foundation::Size,struct winrt::Windows::Foundation::Size *))"
                },
                [](DWORD64 address) {
                    return CreateHookAtAddress(
                        address,
                        &Hooked_TaskbarFrame_MeasureOverride,
                        &g_origTaskbarFrame_MeasureOverride,
                        L"InternalHooks: hook TaskbarFrame_MeasureOverride fallo");
                }},
        };

        bool installedAtLeastOne = false;
        for (const auto& spec : hookSpecs) {
            const auto address = ResolveSymbol(spec.patterns, spec.debugName);
            if (address && spec.install(*address)) {
                installedAtLeastOne = true;
            }
        }

        const auto taskbarFrameHeightAddress = ResolveSymbol(
            {
                LR"(public: __cdecl winrt::impl::consume_Windows_UI_Xaml_IFrameworkElement<struct winrt::Taskbar::implementation::TaskbarFrame>::Height(double)const )",
                LR"(public: void __cdecl winrt::impl::consume_Windows_UI_Xaml_IFrameworkElement<struct winrt::Taskbar::implementation::TaskbarFrame>::Height(double)const )"
            },
            L"TaskbarFrame::Height");
        if (taskbarFrameHeightAddress) {
            if (CreateHookAtAddress(
                    *taskbarFrameHeightAddress,
                    &Hooked_TaskbarFrame_put_Height,
                    &g_origTaskbarFrame_put_Height,
                    L"InternalHooks: hook TaskbarFrame::Height fallo")) {
                installedAtLeastOne = true;
            }
        }

        const auto systemTrayFrameHeightAddress = ResolveSymbol(
            {
                LR"(public: __cdecl winrt::impl::consume_Windows_UI_Xaml_IFrameworkElement<struct winrt::SystemTray::SystemTrayFrame>::Height(double)const )"
            },
            L"SystemTrayFrame::Height");
        if (systemTrayFrameHeightAddress) {
            if (CreateHookAtAddress(
                    *systemTrayFrameHeightAddress,
                    &Hooked_SystemTrayFrame_put_Height,
                    &g_origSystemTrayFrame_put_Height,
                    L"InternalHooks: hook SystemTrayFrame::Height fallo")) {
                installedAtLeastOne = true;
            }
        }

        const auto groupingAddress = ResolveSymbol(
            {
                LR"(private: void __cdecl winrt::Taskbar::implementation::TaskbarController::OnGroupingModeChanged(void))",
                L"*TaskbarController*OnGroupingModeChanged*"
            },
            L"TaskbarController::OnGroupingModeChanged");
        if (groupingAddress) {
            g_taskbarController_OnGroupingModeChanged_Address =
                reinterpret_cast<TaskbarController_OnGroupingModeChanged_t>(*groupingAddress);
        }

        const auto maxHeightAddress = ResolveSymbol(
            {
                LR"(public: __cdecl winrt::impl::consume_Windows_UI_Xaml_IFrameworkElement<struct winrt::Taskbar::implementation::TaskbarFrame>::MaxHeight(double)const )"
            },
            L"TaskbarFrame::MaxHeight");
        if (maxHeightAddress) {
            g_origTaskbarFrame_put_MaxHeight =
                reinterpret_cast<TaskbarFrame_put_MaxHeight_t>(*maxHeightAddress);
        }

        const auto constant48Address = ResolveSymbol(
            {LR"(__real@4048000000000000)"},
            L"48px_constant");
        if (constant48Address) {
            g_constant48Value = reinterpret_cast<double*>(*constant48Address);
        }

        const auto stuckTrayChangeAddress = ResolveSymbol(
            {
                LR"(public: void __cdecl TrayUI::_StuckTrayChange(void))"
            },
            L"TrayUI::_StuckTrayChange");
        if (stuckTrayChangeAddress) {
            g_origTrayUI__StuckTrayChange =
                reinterpret_cast<TrayUI__StuckTrayChange_t>(*stuckTrayChangeAddress);
        }

        bool usedConstantFallback = false;
        if (!g_origTaskbarConfiguration_GetFrameSize && g_constant48Value) {
            usedConstantFallback = ApplyFrameSizeConstantFallback();
        }

        if (!installedAtLeastOne && !usedConstantFallback) {
            Log::Info(L"InternalHooks: no se pudo instalar ningun hook interno");
            return false;
        }

        if (installedAtLeastOne && !EnableInternalHooks()) {
            return false;
        }

        if (installedAtLeastOne || usedConstantFallback) {
            ApplySettingsLikeWindhawk();
        } else {
            RefreshTaskbarWindow();
        }

        g_initialized = true;
        if (installedAtLeastOne) {
            Log::Info(L"InternalHooks: hooks de frame-size/relayout activos");
        } else {
            Log::Info(L"InternalHooks: fallback de constante de frame-size activo");
        }
        return true;
    }

    void RequestShellRefresh(HWND primaryTaskbar)
    {
        if (primaryTaskbar) {
            g_primaryTaskbar = primaryTaskbar;
        }

        if (!g_primaryTaskbar || !IsWindow(g_primaryTaskbar)) {
            return;
        }

        ApplySettingsLikeWindhawk();
    }

    void Shutdown()
    {
        if (g_constant48Value && g_originalTaskbarHeight > 0.0) {
            ProtectAndMemcpy(
                PAGE_READWRITE,
                g_constant48Value,
                &g_originalTaskbarHeight,
                sizeof(g_originalTaskbarHeight));
        }

        g_initialized = false;
        g_primaryTaskbar = nullptr;
        g_taskbarHeight = 0.0;
        g_originalTaskbarHeight = 0.0;
        g_applyingSettings = false;
        g_pendingMeasureOverride = false;
        g_inSystemTrayControllerUpdateFrameSize = false;
        g_inTaskbarControllerUpdateFrameHeight = false;
        g_loggedTrayRefreshHook = false;
        g_loggedTrayHandleSettingException = false;
        g_loggedUpdateFrameHeightException = false;
        g_taskbarModuleBases.clear();
        g_windhawkSymbolResolver.reset();
        g_constant48Value = nullptr;
        g_taskbarFrameOffset = 0;
        g_systemTrayLastHeightOffset = 0;
        g_taskbarController_OnGroupingModeChanged_Address = nullptr;
        g_origTaskbarConfiguration_GetFrameSize = nullptr;
        g_origSystemTrayController_GetFrameSize = nullptr;
        g_origSystemTraySecondaryController_GetFrameSize = nullptr;
        g_origTaskbarFrame_put_Height = nullptr;
        g_origTaskbarFrame_put_MaxHeight = nullptr;
        g_origSystemTrayFrame_put_Height = nullptr;
        g_origTaskbarFrame_MeasureOverride = nullptr;
        g_origTaskbarController_UpdateFrameHeight = nullptr;
        g_origSystemTrayController_UpdateFrameSize = nullptr;
        g_origSystemTraySecondaryController_UpdateFrameSize = nullptr;
        g_origTrayUI_GetMinSize = nullptr;
        g_origTrayUI__StuckTrayChange = nullptr;
        g_origTrayUI__HandleSettingChange = nullptr;

        if (g_symbolsInitialized) {
            SymCleanup(g_process);
            g_symbolsInitialized = false;
        }
    }

} // namespace Velvet::InternalTaskbarHooks
