// ============================================================
// VelvetUI - payload DLL (velvet.dll)
// Fase 1: Detectar la barra de tareas desde dentro de explorer
// ============================================================

#include <Windows.h>
#include <string>
#include <vector>

// Log via OutputDebugString (visible en DebugView)
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
}

// ============================================================
// Fase 1: Buscar ventanas de la barra de tareas
// ============================================================

struct TaskbarInfo {
    HWND mainTaskbar = nullptr;       // Shell_TrayWnd
    HWND secondaryTaskbar = nullptr;  // Shell_SecondaryTrayWnd
    HWND rebarWindow = nullptr;       // ReBarWindow32
    HWND taskSwClass = nullptr;       // MSTaskSwWClass
};

TaskbarInfo FindTaskbarWindows()
{
    TaskbarInfo info;

    // Barra principal
    info.mainTaskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (info.mainTaskbar) {
        Log::Info(L"Shell_TrayWnd encontrada", (DWORD)(UINT_PTR)info.mainTaskbar);

        // Obtener dimensiones
        RECT rect;
        if (GetWindowRect(info.mainTaskbar, &rect)) {
            wchar_t buf[256];
            swprintf_s(buf, L"[VelvetUI]   Posicion: (%ld,%ld) - (%ld,%ld) | Tamano: %ldx%ld",
                rect.left, rect.top, rect.right, rect.bottom,
                rect.right - rect.left, rect.bottom - rect.top);
            OutputDebugStringW(buf);
        }
    } else {
        Log::Info(L"Shell_TrayWnd NO encontrada");
    }

    // Barra secundaria (multi-monitor)
    info.secondaryTaskbar = FindWindowW(L"Shell_SecondaryTrayWnd", nullptr);
    if (info.secondaryTaskbar) {
        Log::Info(L"Shell_SecondaryTrayWnd encontrada (multi-monitor)");
    }

    // Enumerar ventanas hijas de la barra principal
    if (info.mainTaskbar) {
        Log::Info(L"--- Ventanas hijas de Shell_TrayWnd ---");
        
        EnumChildWindows(info.mainTaskbar, [](HWND hwnd, LPARAM lParam) -> BOOL {
            wchar_t className[256] = { 0 };
            wchar_t windowText[256] = { 0 };
            GetClassNameW(hwnd, className, 256);
            GetWindowTextW(hwnd, windowText, 256);
            
            RECT rect;
            GetWindowRect(hwnd, &rect);
            
            wchar_t buf[1024];
            swprintf_s(buf, L"[VelvetUI]   HWND=%p | Class=%-30s | Text=%-20s | Rect=(%ld,%ld,%ld,%ld)",
                hwnd, className, windowText,
                rect.left, rect.top, rect.right, rect.bottom);
            OutputDebugStringW(buf);
            
            return TRUE; // continuar enumerando
        }, 0);
        
        Log::Info(L"--- Fin de ventanas hijas ---");
    }

    return info;
}

// ============================================================
// Fase 1: Intentar acceder a XAML Islands
// Buscamos la ventana de tipo "Windows.UI.Xaml" que contiene
// el visual tree XAML de la barra de tareas
// ============================================================

void FindXamlWindows()
{
    Log::Info(L"=== Buscando ventanas XAML ===");
    
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        // Solo nos interesan las ventanas de nuestro proceso
        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != GetCurrentProcessId()) return TRUE;
        
        wchar_t className[256] = { 0 };
        GetClassNameW(hwnd, className, 256);
        
        // Buscar ventanas XAML relevantes
        if (wcsstr(className, L"Xaml") || 
            wcsstr(className, L"XAML") ||
            wcsstr(className, L"Windows.UI") ||
            wcsstr(className, L"Taskbar") ||
            wcsstr(className, L"Shell_TrayWnd") ||
            wcsstr(className, L"Shell_Secondary")) 
        {
            wchar_t windowText[256] = { 0 };
            GetWindowTextW(hwnd, windowText, 256);
            
            RECT rect;
            GetWindowRect(hwnd, &rect);
            
            wchar_t buf[1024];
            swprintf_s(buf, L"[VelvetUI]   HWND=%p | Class=%-40s | Text=%-20s | Size=%ldx%ld",
                hwnd, className, windowText,
                rect.right - rect.left, rect.bottom - rect.top);
            OutputDebugStringW(buf);
        }
        
        return TRUE;
    }, 0);
    
    Log::Info(L"=== Fin busqueda XAML ===");
}

// ============================================================
// Worker thread: ejecutamos la lógica en un thread separado
// para no bloquear DllMain
// ============================================================

DWORD WINAPI VelvetWorker(LPVOID lpParam)
{
    // Esperar un poco para que explorer termine de inicializar
    Sleep(500);
    
    Log::Info(L"=== VelvetUI Fase 1: Reconocimiento ===");
    
    // Paso 1: Encontrar la barra de tareas
    TaskbarInfo taskbar = FindTaskbarWindows();
    
    // Paso 2: Buscar ventanas XAML
    FindXamlWindows();
    
    Log::Info(L"=== Fin Fase 1 ===");
    
    return 0;
}

// ============================================================
// DLL Entry Point
// ============================================================

HMODULE g_hModule = nullptr;
HANDLE g_hWorkerThread = nullptr;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(hModule);
        g_hModule = hModule;

        DWORD pid = GetCurrentProcessId();
        Log::Info(L"=== VelvetUI payload cargada ===");
        Log::Info(L"PID", pid);

        // Lanzar worker thread (no hacer trabajo pesado en DllMain)
        g_hWorkerThread = CreateThread(nullptr, 0, VelvetWorker, nullptr, 0, nullptr);
        
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
