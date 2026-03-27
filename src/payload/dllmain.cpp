// ============================================================
// VelvetUI - payload DLL (velvet.dll)
// Se inyecta en explorer.exe para modificar la barra de tareas
// 
// Fase 0: Solo verifica que la inyección funciona
// ============================================================

#include <Windows.h>
#include <string>

// Helper para loggear via OutputDebugString (visible en DebugView)
namespace Log {
    void Info(const wchar_t* msg) {
        std::wstring formatted = L"[VelvetUI] ";
        formatted += msg;
        formatted += L"\n";
        OutputDebugStringW(formatted.c_str());
    }

    void Info(const wchar_t* msg, DWORD value) {
        wchar_t buffer[512];
        swprintf_s(buffer, L"[VelvetUI] %s: %lu", msg, value);
        OutputDebugStringW(buffer);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        // No necesitamos notificaciones de threads
        DisableThreadLibraryCalls(hModule);

        // Obtener info del proceso donde estamos inyectados
        DWORD pid = GetCurrentProcessId();
        wchar_t processName[MAX_PATH] = { 0 };
        GetModuleFileNameW(nullptr, processName, MAX_PATH);

        Log::Info(L"=== VelvetUI payload cargada ===");
        Log::Info(L"PID del proceso", pid);
        Log::Info(processName);
        Log::Info(L"Fase 0: Inyeccion exitosa!");

        // TODO Fase 1: Inicializar hooks
        // TODO Fase 2: Conectar al visual tree XAML
        // TODO Fase 3: Aplicar estilos

        break;
    }
    case DLL_PROCESS_DETACH:
    {
        Log::Info(L"=== VelvetUI payload descargada ===");
        Log::Info(L"Cleanup completo");

        // TODO: Revertir todos los cambios XAML
        // TODO: Desregistrar hooks
        break;
    }
    }

    return TRUE;
}
