#pragma once
// ============================================================
// VelvetUI - DLL Injector
// Inyecta velvet.dll en un proceso target usando
// CreateRemoteThread + LoadLibraryW
// ============================================================

#include <Windows.h>
#include <string>

namespace Velvet {

    // Resultado de una operación de inyección
    struct InjectionResult {
        bool success;
        DWORD errorCode;      // GetLastError() si falló
        std::wstring message;  // Descripción legible
    };

    // Inyecta una DLL en un proceso por PID
    // dllPath debe ser la ruta ABSOLUTA a la DLL
    InjectionResult InjectDll(DWORD targetPid, const wchar_t* dllPath);

    // Desinyecta una DLL de un proceso por PID
    // Busca el módulo por nombre y llama FreeLibrary remotamente
    InjectionResult EjectDll(DWORD targetPid, const wchar_t* dllName);

} // namespace Velvet
