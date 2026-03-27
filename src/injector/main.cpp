// ============================================================
// VelvetUI - Main entry point
// 
// Uso:
//   VelvetUI.exe                  → Inyecta en explorer.exe
//   VelvetUI.exe --eject          → Eyecta de explorer.exe
//   VelvetUI.exe --target notepad.exe  → Inyecta en notepad (test)
//   VelvetUI.exe --help           → Ayuda
// ============================================================

#include "velvet_common.h"
#include "process.h"
#include "injector.h"
#include <filesystem>

using namespace Velvet;

void PrintBanner()
{
    wprintf(L"\n");
    wprintf(L"  VelvetUI v%s\n", VERSION);
    wprintf(L"  Windows 11 Shell Customization\n");
    wprintf(L"  ─────────────────────────────────\n\n");
}

void PrintHelp()
{
    wprintf(L"  Uso:\n");
    wprintf(L"    VelvetUI.exe                         Inyecta en explorer.exe\n");
    wprintf(L"    VelvetUI.exe --eject                 Eyecta de explorer.exe\n");
    wprintf(L"    VelvetUI.exe --target <proceso.exe>  Inyecta en otro proceso (test)\n");
    wprintf(L"    VelvetUI.exe --help                  Muestra esta ayuda\n\n");
}

int wmain(int argc, wchar_t* argv[])
{
    PrintBanner();

    // Parsear argumentos
    bool doEject = false;
    const wchar_t* targetProcess = TARGET_PROCESS; // default: explorer.exe

    for (int i = 1; i < argc; i++) {
        if (_wcsicmp(argv[i], L"--help") == 0 || _wcsicmp(argv[i], L"-h") == 0) {
            PrintHelp();
            return static_cast<int>(ExitCode::Success);
        }
        else if (_wcsicmp(argv[i], L"--eject") == 0) {
            doEject = true;
        }
        else if (_wcsicmp(argv[i], L"--target") == 0 && i + 1 < argc) {
            targetProcess = argv[++i];
        }
    }

    // Verificar privilegios de administrador
    if (!IsRunningAsAdmin()) {
        LogConsole(L"ADVERTENCIA: No se esta ejecutando como Administrador");
        LogConsole(L"La inyeccion en explorer.exe requiere privilegios elevados");
        LogConsole(L"Clic derecho -> Ejecutar como administrador\n");
        return static_cast<int>(ExitCode::InsufficientPrivileges);
    }

    // Buscar el proceso target
    LogConsole(L"Buscando proceso", targetProcess);
    auto pids = FindProcessByName(targetProcess);

    if (pids.empty()) {
        LogConsole(L"Proceso no encontrado", targetProcess);
        return static_cast<int>(ExitCode::ProcessNotFound);
    }

    LogConsole(L"Proceso encontrado, PID", pids[0]);

    // Modo eyección
    if (doEject) {
        LogConsole(L"Eyectando velvet.dll...");
        auto result = EjectDll(pids[0], DLL_NAME);
        LogConsole(result.message.c_str());
        return result.success
            ? static_cast<int>(ExitCode::Success)
            : static_cast<int>(ExitCode::InjectionFailed);
    }

    // Modo inyección: resolver ruta absoluta de la DLL
    // La DLL debe estar en la misma carpeta que el exe
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::filesystem::path dllPath =
        std::filesystem::path(exePath).parent_path() / DLL_NAME;

    if (!std::filesystem::exists(dllPath)) {
        LogConsole(L"DLL no encontrada", dllPath.wstring().c_str());
        LogConsole(L"velvet.dll debe estar en la misma carpeta que VelvetUI.exe");
        return static_cast<int>(ExitCode::DllNotFound);
    }

    LogConsole(L"DLL encontrada", dllPath.wstring().c_str());
    LogConsole(L"Inyectando...");

    auto result = InjectDll(pids[0], dllPath.wstring().c_str());

    if (result.success) {
        LogConsole(L"OK!", result.message.c_str());
        LogConsole(L"Abri DebugView para ver los logs de la DLL");
    } else {
        LogConsole(L"ERROR", result.message.c_str());
        if (result.errorCode != 0) {
            LogConsole(L"Codigo de error Windows", result.errorCode);
        }
    }

    wprintf(L"\n");
    return result.success
        ? static_cast<int>(ExitCode::Success)
        : static_cast<int>(ExitCode::InjectionFailed);
}
