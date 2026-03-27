#pragma once
// ============================================================
// VelvetUI - Process utilities
// Funciones para buscar y manipular procesos de Windows
// ============================================================

#include <Windows.h>
#include <vector>
#include <string>

namespace Velvet {

    // Busca todos los PIDs de un proceso por nombre (ej: "explorer.exe")
    // Retorna vector vacío si no encuentra ninguno
    std::vector<DWORD> FindProcessByName(const wchar_t* processName);

    // Verifica si el proceso actual tiene privilegios de administrador
    bool IsRunningAsAdmin();

} // namespace Velvet
