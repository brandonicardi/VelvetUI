#pragma once

#include <Windows.h>

#include <optional>
#include <memory>
#include <string>
#include <vector>

namespace Velvet {

struct WindhawkResolvedSymbol {
    DWORD64 address = 0;
    std::wstring matchedName;
    size_t patternIndex = static_cast<size_t>(-1);
    bool matchedUndecorated = false;
    bool usedWildcard = false;
    bool compilerGenerated = false;
    DWORD symTag = 0;
    ULONGLONG length = 0;
};

class WindhawkSymbolResolver {
public:
    static std::unique_ptr<WindhawkSymbolResolver> TryCreate();

    std::vector<WindhawkResolvedSymbol> ResolveSymbols(
        const std::vector<HMODULE>& modules,
        const std::vector<std::wstring>& patterns);

private:
    WindhawkSymbolResolver(std::wstring engineRoot, std::wstring symbolsCachePath);

    std::vector<WindhawkResolvedSymbol> ResolveSymbolsForModule(
        HMODULE module,
        const std::vector<std::wstring>& patterns);

    struct SymbolEntry {
        DWORD64 address = 0;
        std::wstring decoratedName;
        std::wstring undecoratedName;
        DWORD symTag = 0;
        ULONGLONG length = 0;
    };

    struct ModuleCacheEntry {
        std::wstring modulePath;
        std::vector<SymbolEntry> symbols;
    };

    ModuleCacheEntry* GetOrLoadModuleCache(HMODULE module);
    bool EnumerateModuleSymbols(HMODULE module, ModuleCacheEntry* cacheEntry);

    static bool MatchesPattern(const wchar_t* value, const wchar_t* pattern);
    static std::optional<WindhawkResolvedSymbol> MatchesAnyPattern(
        const SymbolEntry& entry,
        const std::vector<std::wstring>& patterns);

    std::wstring m_engineRoot;
    std::wstring m_symbolsCachePath;
    std::vector<ModuleCacheEntry> m_moduleCaches;
};

} // namespace Velvet
