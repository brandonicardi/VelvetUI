#pragma once
// ============================================================
// VelvetUI - Common definitions
// ============================================================

#include <Windows.h>
#include <string>
#include <cstdio>

namespace Velvet {

    constexpr const wchar_t* VERSION = L"0.1.0";
    constexpr const wchar_t* DLL_NAME = L"velvet.dll";
    constexpr const wchar_t* LOG_PREFIX = L"[VelvetUI]";

    // Nombre del proceso target
    constexpr const wchar_t* TARGET_PROCESS = L"explorer.exe";

    // Log helper para el inyector (escribe a consola)
    inline void LogConsole(const wchar_t* msg) {
        wprintf(L"%s %s\n", LOG_PREFIX, msg);
    }

    inline void LogConsole(const wchar_t* msg, const wchar_t* detail) {
        wprintf(L"%s %s: %s\n", LOG_PREFIX, msg, detail);
    }

    inline void LogConsole(const wchar_t* msg, DWORD value) {
        wprintf(L"%s %s: %lu\n", LOG_PREFIX, msg, value);
    }

    // Códigos de retorno
    enum class ExitCode : int {
        Success = 0,
        ProcessNotFound = 1,
        InjectionFailed = 2,
        DllNotFound = 3,
        InsufficientPrivileges = 4,
        InvalidArguments = 5,
    };

} // namespace Velvet
