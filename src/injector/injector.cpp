// ============================================================
// VelvetUI - DLL Injector implementation
// Técnica: CreateRemoteThread + LoadLibraryW
// 
// Esta es la técnica clásica de inyección de DLL:
// 1. Abrir el proceso target con permisos suficientes
// 2. Reservar memoria en el proceso remoto
// 3. Escribir la ruta de la DLL en esa memoria
// 4. Crear un thread remoto que ejecute LoadLibraryW
// 5. Esperar a que termine y limpiar
// ============================================================

#include "injector.h"
#include "velvet_common.h"
#include <TlHelp32.h>

namespace Velvet {

    InjectionResult InjectDll(DWORD targetPid, const wchar_t* dllPath)
    {
        InjectionResult result{ false, 0, L"" };

        // 1. Abrir el proceso target
        HANDLE hProcess = OpenProcess(
            PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
            PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
            FALSE,
            targetPid
        );

        if (!hProcess) {
            result.errorCode = GetLastError();
            result.message = L"No se pudo abrir el proceso. Ejecutar como Administrador.";
            return result;
        }

        // 2. Calcular tamaño necesario para la ruta de la DLL (en bytes, con null terminator)
        size_t dllPathSize = (wcslen(dllPath) + 1) * sizeof(wchar_t);

        // 3. Reservar memoria en el proceso remoto
        LPVOID remoteMem = VirtualAllocEx(
            hProcess,
            nullptr,
            dllPathSize,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE
        );

        if (!remoteMem) {
            result.errorCode = GetLastError();
            result.message = L"No se pudo reservar memoria en el proceso remoto";
            CloseHandle(hProcess);
            return result;
        }

        // 4. Escribir la ruta de la DLL en la memoria remota
        if (!WriteProcessMemory(hProcess, remoteMem, dllPath, dllPathSize, nullptr)) {
            result.errorCode = GetLastError();
            result.message = L"No se pudo escribir en la memoria del proceso remoto";
            VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
            CloseHandle(hProcess);
            return result;
        }

        // 5. Obtener la dirección de LoadLibraryW en kernel32.dll
        //    (es la misma en todos los procesos gracias a ASLR compartido)
        HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
        if (!hKernel32) {
            result.errorCode = GetLastError();
            result.message = L"No se encontro kernel32.dll";
            VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
            CloseHandle(hProcess);
            return result;
        }

        FARPROC loadLibAddr = GetProcAddress(hKernel32, "LoadLibraryW");
        if (!loadLibAddr) {
            result.errorCode = GetLastError();
            result.message = L"No se encontro LoadLibraryW en kernel32.dll";
            VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
            CloseHandle(hProcess);
            return result;
        }

        // 6. Crear thread remoto que ejecute LoadLibraryW(rutaDLL)
        HANDLE hThread = CreateRemoteThread(
            hProcess,
            nullptr,
            0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibAddr),
            remoteMem,
            0,
            nullptr
        );

        if (!hThread) {
            result.errorCode = GetLastError();
            result.message = L"No se pudo crear el thread remoto";
            VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
            CloseHandle(hProcess);
            return result;
        }

        // 7. Esperar a que el thread termine (LoadLibraryW retorne)
        WaitForSingleObject(hThread, 5000);

        // 8. Verificar que LoadLibraryW retornó un handle válido (!=0)
        DWORD exitCode = 0;
        GetExitCodeThread(hThread, &exitCode);

        // 9. Limpiar
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hThread);
        CloseHandle(hProcess);

        if (exitCode == 0) {
            result.errorCode = 0;
            result.message = L"LoadLibraryW retorno NULL - la DLL no se pudo cargar en el proceso";
            return result;
        }

        result.success = true;
        result.message = L"Inyeccion exitosa";
        return result;
    }

    InjectionResult EjectDll(DWORD targetPid, const wchar_t* dllName)
    {
        InjectionResult result{ false, 0, L"" };

        // Buscar el módulo en el proceso target
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, targetPid);
        if (snapshot == INVALID_HANDLE_VALUE) {
            result.errorCode = GetLastError();
            result.message = L"No se pudo crear snapshot de modulos";
            return result;
        }

        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(MODULEENTRY32W);
        HMODULE remoteModule = nullptr;

        if (Module32FirstW(snapshot, &entry)) {
            do {
                if (_wcsicmp(entry.szModule, dllName) == 0) {
                    remoteModule = entry.hModule;
                    break;
                }
            } while (Module32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);

        if (!remoteModule) {
            result.message = L"DLL no encontrada en el proceso";
            return result;
        }

        // Abrir el proceso y llamar FreeLibrary remotamente
        HANDLE hProcess = OpenProcess(
            PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION,
            FALSE,
            targetPid
        );

        if (!hProcess) {
            result.errorCode = GetLastError();
            result.message = L"No se pudo abrir el proceso para eyectar";
            return result;
        }

        FARPROC freeLibAddr = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "FreeLibrary");

        HANDLE hThread = CreateRemoteThread(
            hProcess,
            nullptr,
            0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(freeLibAddr),
            remoteModule,
            0,
            nullptr
        );

        if (hThread) {
            WaitForSingleObject(hThread, 5000);
            CloseHandle(hThread);
            result.success = true;
            result.message = L"DLL eyectada exitosamente";
        } else {
            result.errorCode = GetLastError();
            result.message = L"No se pudo crear thread para eyectar";
        }

        CloseHandle(hProcess);
        return result;
    }

} // namespace Velvet
