#include "windhawk_symbol_resolver.h"

#include <Windows.h>
#include <DbgHelp.h>
#include <oleauto.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <dia/dia2.h>
#include <dia/diacreate.h>
#include <wrl/client.h>

namespace Velvet {
namespace {

using Microsoft::WRL::ComPtr;

std::filesystem::path g_engineArchPath;

bool ContainsWildcard(std::wstring_view pattern) {
    return pattern.find(L'?') != std::wstring_view::npos ||
           (!pattern.empty() &&
            (pattern.front() == L'*' || pattern.back() == L'*'));
}

bool IsCompilerGeneratedSymbol(std::wstring_view symbolName) {
    return symbolName.find(L'`') != std::wstring_view::npos ||
           symbolName.find(L"lambda") != std::wstring_view::npos ||
           symbolName.find(L"implements_delegate") != std::wstring_view::npos;
}

int GetSymbolTagPriority(DWORD symTag) {
    switch (symTag) {
    case SymTagFunction:
        return 0;
    case SymTagPublicSymbol:
        return 1;
    case SymTagData:
        return 2;
    default:
        return 3;
    }
}

bool IsBetterMatch(
    const WindhawkResolvedSymbol& candidate,
    const WindhawkResolvedSymbol& current) {
    if (candidate.patternIndex != current.patternIndex) {
        return candidate.patternIndex < current.patternIndex;
    }

    if (candidate.usedWildcard != current.usedWildcard) {
        return !candidate.usedWildcard;
    }

    if (candidate.compilerGenerated != current.compilerGenerated) {
        return !candidate.compilerGenerated;
    }

    if (candidate.symTag != current.symTag) {
        return GetSymbolTagPriority(candidate.symTag) <
               GetSymbolTagPriority(current.symTag);
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

std::wstring GetModulePath(HMODULE module) {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(module, buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }

    return std::wstring(buffer, len);
}

std::optional<std::wstring> FindWindhawkEngineRoot() {
    wchar_t programFiles[MAX_PATH] = {};
    const DWORD len = GetEnvironmentVariableW(L"ProgramFiles", programFiles, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return std::nullopt;
    }

    const std::filesystem::path engineRoot =
        std::filesystem::path(programFiles) / L"Windhawk" / L"Engine";
    if (!std::filesystem::exists(engineRoot)) {
        return std::nullopt;
    }

    std::filesystem::path bestMatch;
    for (const auto& entry : std::filesystem::directory_iterator(engineRoot)) {
        if (!entry.is_directory()) {
            continue;
        }

        const auto candidate = entry.path() / L"64" / L"msdia140_windhawk.dll";
        if (!std::filesystem::exists(candidate)) {
            continue;
        }

        if (bestMatch.empty() ||
            entry.path().filename().native() > bestMatch.filename().native()) {
            bestMatch = entry.path();
        }
    }

    if (bestMatch.empty()) {
        return std::nullopt;
    }

    return bestMatch.native();
}

std::wstring GetSymbolsCachePath() {
    wchar_t localAppData[MAX_PATH] = {};
    const DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);

    std::filesystem::path cachePath;
    if (len > 0 && len < MAX_PATH) {
        cachePath = std::filesystem::path(localAppData) / L"VelvetUI" / L"symbols";
    } else {
        cachePath = std::filesystem::current_path() / L"symbols";
    }

    std::error_code ec;
    std::filesystem::create_directories(cachePath, ec);
    return cachePath.native();
}

std::wstring BuildSearchPath(const std::wstring& symbolsCachePath) {
    return L"srv*" + symbolsCachePath + L"*https://msdl.microsoft.com/download/symbols";
}

void** FindImportPtr(HMODULE module, PCSTR importModuleName, PCSTR importName) {
    auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
    auto* ntHeader = reinterpret_cast<IMAGE_NT_HEADERS*>(
        reinterpret_cast<BYTE*>(dosHeader) + dosHeader->e_lfanew);

    if (ntHeader->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT ||
        ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress == 0) {
        return nullptr;
    }

    const auto imageBase = reinterpret_cast<ULONG_PTR>(module);
    auto* importDescriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
        imageBase +
        ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

    while (importDescriptor->OriginalFirstThunk) {
        const auto importedModuleName =
            reinterpret_cast<const char*>(imageBase + importDescriptor->Name);
        if (_stricmp(importedModuleName, importModuleName) == 0) {
            auto* originalThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
                imageBase + importDescriptor->OriginalFirstThunk);
            auto* firstThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
                imageBase + importDescriptor->FirstThunk);

            while (originalThunk->u1.Function) {
                if (!IMAGE_SNAP_BY_ORDINAL(originalThunk->u1.Function)) {
                    auto* importByName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                        imageBase + originalThunk->u1.AddressOfData);
                    if (strcmp(reinterpret_cast<const char*>(importByName->Name), importName) == 0) {
                        return reinterpret_cast<void**>(firstThunk);
                    }
                }

                ++originalThunk;
                ++firstThunk;
            }
        }

        ++importDescriptor;
    }

    return nullptr;
}

HMODULE WINAPI MsdiaLoadLibraryExWHook(LPCWSTR fileName, HANDLE file, DWORD flags) {
    if (wcscmp(fileName, L"SYMSRV.DLL") != 0) {
        return LoadLibraryExW(fileName, file, flags);
    }

    DWORD adjustedFlags = flags | LOAD_WITH_ALTERED_SEARCH_PATH;
    adjustedFlags &= ~LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR;
    adjustedFlags &= ~LOAD_LIBRARY_SEARCH_APPLICATION_DIR;
    adjustedFlags &= ~LOAD_LIBRARY_SEARCH_USER_DIRS;
    adjustedFlags &= ~LOAD_LIBRARY_SEARCH_SYSTEM32;
    adjustedFlags &= ~LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;

    const auto symsrvPath = g_engineArchPath / L"symsrv_windhawk.dll";
    HMODULE symsrvModule = LoadLibraryExW(symsrvPath.c_str(), file, adjustedFlags);
    if (!symsrvModule) {
        return nullptr;
    }

    auto* setOptions = reinterpret_cast<PSYMBOLSERVERSETOPTIONSPROC>(
        GetProcAddress(symsrvModule, "SymbolServerSetOptions"));
    if (setOptions) {
        setOptions(SSRVOPT_UNATTENDED, TRUE);
        setOptions(SSRVOPT_TRACE, FALSE);
    }

    return symsrvModule;
}

class DiaLoadCallback final : public IDiaLoadCallback2 {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (!object) {
            return E_POINTER;
        }

        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDiaLoadCallback)) {
            *object = static_cast<IDiaLoadCallback*>(this);
            return S_OK;
        }

        if (riid == __uuidof(IDiaLoadCallback2)) {
            *object = static_cast<IDiaLoadCallback2*>(this);
            return S_OK;
        }

        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE NotifyDebugDir(BOOL, DWORD, BYTE*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE NotifyOpenDBG(LPCOLESTR, HRESULT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE NotifyOpenPDB(LPCOLESTR, HRESULT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RestrictRegistryAccess() override { return E_FAIL; }
    HRESULT STDMETHODCALLTYPE RestrictSymbolServerAccess() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RestrictOriginalPathAccess() override { return E_FAIL; }
    HRESULT STDMETHODCALLTYPE RestrictReferencePathAccess() override { return E_FAIL; }
    HRESULT STDMETHODCALLTYPE RestrictDBGAccess() override { return E_FAIL; }
    HRESULT STDMETHODCALLTYPE RestrictSystemRootAccess() override { return E_FAIL; }
};

ComPtr<IDiaDataSource> LoadDiaDataSource(const std::wstring& engineRoot) {
    g_engineArchPath = std::filesystem::path(engineRoot) / L"64";
    const auto msdiaPath = g_engineArchPath / L"msdia140_windhawk.dll";

    HMODULE msdiaModule =
        LoadLibraryExW(msdiaPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!msdiaModule) {
        return nullptr;
    }

    void** loadLibraryExPtr =
        FindImportPtr(msdiaModule, "kernel32.dll", "LoadLibraryExW");
    if (loadLibraryExPtr) {
        DWORD oldProtect = 0;
        if (VirtualProtect(loadLibraryExPtr, sizeof(*loadLibraryExPtr), PAGE_READWRITE, &oldProtect)) {
            *loadLibraryExPtr = reinterpret_cast<void*>(&MsdiaLoadLibraryExWHook);
            DWORD ignored = 0;
            VirtualProtect(loadLibraryExPtr, sizeof(*loadLibraryExPtr), oldProtect, &ignored);
        }
    }

    ComPtr<IDiaDataSource> diaSource;
    if (FAILED(NoRegCoCreate(msdiaPath.c_str(), CLSID_DiaSource, IID_PPV_ARGS(&diaSource)))) {
        FreeLibrary(msdiaModule);
        return nullptr;
    }

    FreeLibrary(msdiaModule);
    return diaSource;
}

} // namespace

WindhawkSymbolResolver::WindhawkSymbolResolver(
    std::wstring engineRoot,
    std::wstring symbolsCachePath)
    : m_engineRoot(std::move(engineRoot)),
      m_symbolsCachePath(std::move(symbolsCachePath)) {}

std::unique_ptr<WindhawkSymbolResolver> WindhawkSymbolResolver::TryCreate() {
    const auto engineRoot = FindWindhawkEngineRoot();
    if (!engineRoot) {
        return nullptr;
    }

    return std::unique_ptr<WindhawkSymbolResolver>(
        new WindhawkSymbolResolver(*engineRoot, GetSymbolsCachePath()));
}

std::vector<WindhawkResolvedSymbol> WindhawkSymbolResolver::ResolveSymbols(
    const std::vector<HMODULE>& modules,
    const std::vector<std::wstring>& patterns) {
    std::vector<WindhawkResolvedSymbol> results;
    for (const auto module : modules) {
        auto moduleResults = ResolveSymbolsForModule(module, patterns);
        results.insert(results.end(), moduleResults.begin(), moduleResults.end());
    }

    std::sort(results.begin(), results.end(), IsBetterMatch);
    results.erase(
        std::unique(results.begin(), results.end(), [](const auto& a, const auto& b) {
            return a.address == b.address;
        }),
        results.end());
    return results;
}

std::vector<WindhawkResolvedSymbol> WindhawkSymbolResolver::ResolveSymbolsForModule(
    HMODULE module,
    const std::vector<std::wstring>& patterns) {
    std::vector<WindhawkResolvedSymbol> matches;
    auto* cacheEntry = GetOrLoadModuleCache(module);
    if (!cacheEntry) {
        return matches;
    }

    for (const auto& entry : cacheEntry->symbols) {
        auto bestMatch = MatchesAnyPattern(entry, patterns);
        if (bestMatch) {
            matches.push_back(*bestMatch);
        }
    }

    std::sort(matches.begin(), matches.end(), IsBetterMatch);
    matches.erase(
        std::unique(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
            return a.address == b.address;
        }),
        matches.end());

    return matches;
}

WindhawkSymbolResolver::ModuleCacheEntry* WindhawkSymbolResolver::GetOrLoadModuleCache(HMODULE module) {
    if (!module) {
        return nullptr;
    }

    const auto modulePath = GetModulePath(module);
    if (modulePath.empty()) {
        return nullptr;
    }

    auto it = std::find_if(
        m_moduleCaches.begin(),
        m_moduleCaches.end(),
        [&](const auto& entry) { return entry.modulePath == modulePath; });
    if (it != m_moduleCaches.end()) {
        return &*it;
    }

    ModuleCacheEntry cacheEntry{};
    cacheEntry.modulePath = modulePath;
    if (!EnumerateModuleSymbols(module, &cacheEntry)) {
        return nullptr;
    }

    m_moduleCaches.push_back(std::move(cacheEntry));
    return &m_moduleCaches.back();
}

bool WindhawkSymbolResolver::EnumerateModuleSymbols(HMODULE module, ModuleCacheEntry* cacheEntry) {
    if (!cacheEntry) {
        return false;
    }

    const auto modulePath = GetModulePath(module);
    if (modulePath.empty()) {
        return false;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        return false;
    }

    auto diaSource = LoadDiaDataSource(m_engineRoot);
    if (!diaSource) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return false;
    }

    DiaLoadCallback callbacks;
    const auto searchPath = BuildSearchPath(m_symbolsCachePath);
    hr = diaSource->loadDataForExe(modulePath.c_str(), searchPath.c_str(), &callbacks);
    if (FAILED(hr)) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return false;
    }

    ComPtr<IDiaSession> diaSession;
    hr = diaSource->openSession(&diaSession);
    if (FAILED(hr)) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return false;
    }

    ComPtr<IDiaSymbol> globalScope;
    hr = diaSession->get_globalScope(&globalScope);
    if (FAILED(hr)) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return false;
    }

    constexpr DWORD tags[] = {
        SymTagPublicSymbol,
        SymTagFunction,
        SymTagData
    };
    for (const auto tag : tags) {
        ComPtr<IDiaEnumSymbols> diaSymbols;
        hr = globalScope->findChildren(
            static_cast<enum SymTagEnum>(tag),
            nullptr,
            nsNone,
            &diaSymbols);
        if (FAILED(hr) || !diaSymbols) {
            continue;
        }

        while (true) {
            ComPtr<IDiaSymbol> diaSymbol;
            ULONG count = 0;
            hr = diaSymbols->Next(1, &diaSymbol, &count);
            if (FAILED(hr) || hr == S_FALSE || count == 0) {
                break;
            }

            DWORD rva = 0;
            hr = diaSymbol->get_relativeVirtualAddress(&rva);
            if (FAILED(hr) || hr == S_FALSE) {
                continue;
            }

            DWORD symTag = 0;
            hr = diaSymbol->get_symTag(&symTag);
            if (FAILED(hr) || hr == S_FALSE) {
                symTag = SymTagNull;
            }

            ULONGLONG length = 0;
            hr = diaSymbol->get_length(&length);
            if (FAILED(hr) || hr == S_FALSE) {
                length = 0;
            }

            BSTR decorated = nullptr;
            hr = diaSymbol->get_name(&decorated);
            if (FAILED(hr) || hr == S_FALSE) {
                decorated = nullptr;
            }

            BSTR undecorated = nullptr;
            hr = diaSymbol->get_undecoratedName(&undecorated);
            if (FAILED(hr) || hr == S_FALSE) {
                undecorated = nullptr;
            }

            if (!decorated && !undecorated) {
                if (decorated) {
                    SysFreeString(decorated);
                }
                if (undecorated) {
                    SysFreeString(undecorated);
                }
                continue;
            }

            SymbolEntry entry{};
            entry.address =
                reinterpret_cast<DWORD64>(module) + static_cast<DWORD64>(rva);
            entry.symTag = symTag;
            entry.length = length;
            if (decorated) {
                entry.decoratedName = decorated;
            }
            if (undecorated) {
                entry.undecoratedName = undecorated;
            }
            cacheEntry->symbols.push_back(std::move(entry));

            if (decorated) {
                SysFreeString(decorated);
            }
            if (undecorated) {
                SysFreeString(undecorated);
            }
        }
    }

    if (shouldUninitialize) {
        CoUninitialize();
    }

    return !cacheEntry->symbols.empty();
}

bool WindhawkSymbolResolver::MatchesPattern(const wchar_t* value, const wchar_t* pattern) {
    if (!value || !pattern) {
        return false;
    }

    if (!ContainsWildcard(pattern)) {
        return wcscmp(value, pattern) == 0;
    }

    while (*pattern) {
        if (*pattern == L'*') {
            ++pattern;
            if (!*pattern) {
                return true;
            }

            while (*value) {
                if (MatchesPattern(value, pattern)) {
                    return true;
                }
                ++value;
            }

            return MatchesPattern(value, pattern);
        }

        if (*pattern == L'?') {
            if (!*value) {
                return false;
            }

            ++value;
            ++pattern;
            continue;
        }

        if (*value != *pattern) {
            return false;
        }

        ++value;
        ++pattern;
    }

    return *value == L'\0';
}

std::optional<WindhawkResolvedSymbol> WindhawkSymbolResolver::MatchesAnyPattern(
    const SymbolEntry& entry,
    const std::vector<std::wstring>& patterns) {
    std::optional<WindhawkResolvedSymbol> bestMatch;

    for (size_t patternIndex = 0; patternIndex < patterns.size(); ++patternIndex) {
        const auto& pattern = patterns[patternIndex];
        const bool usedWildcard = ContainsWildcard(pattern);

        const auto consider = [&](const std::wstring& symbolName, bool matchedUndecorated) {
            if (symbolName.empty() || !MatchesPattern(symbolName.c_str(), pattern.c_str())) {
                return;
            }

            WindhawkResolvedSymbol candidate{};
            candidate.address = entry.address;
            candidate.matchedName = symbolName;
            candidate.patternIndex = patternIndex;
            candidate.matchedUndecorated = matchedUndecorated;
            candidate.usedWildcard = usedWildcard;
            candidate.compilerGenerated = IsCompilerGeneratedSymbol(symbolName);
            candidate.symTag = entry.symTag;
            candidate.length = entry.length;

            if (!bestMatch || IsBetterMatch(candidate, *bestMatch)) {
                bestMatch = std::move(candidate);
            }
        };

        consider(entry.undecoratedName, true);
        consider(entry.decoratedName, false);
    }

    return bestMatch;
}

} // namespace Velvet
