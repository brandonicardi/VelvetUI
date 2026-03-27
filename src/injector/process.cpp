// ============================================================
// VelvetUI - Process utilities implementation
// ============================================================

#include "process.h"
#include "velvet_common.h"
#include <TlHelp32.h>

namespace Velvet {

    std::vector<DWORD> FindProcessByName(const wchar_t* processName)
    {
        std::vector<DWORD> pids;

        // Crear snapshot de todos los procesos
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            LogConsole(L"Error al crear snapshot de procesos");
            return pids;
        }

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(PROCESSENTRY32W);

        // Iterar todos los procesos
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (_wcsicmp(entry.szExeFile, processName) == 0) {
                    pids.push_back(entry.th32ProcessID);
                }
            } while (Process32NextW(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return pids;
    }

    bool IsRunningAsAdmin()
    {
        BOOL isAdmin = FALSE;
        PSID adminGroup = nullptr;

        SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
        if (AllocateAndInitializeSid(
                &authority, 2,
                SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                0, 0, 0, 0, 0, 0,
                &adminGroup))
        {
            CheckTokenMembership(nullptr, adminGroup, &isAdmin);
            FreeSid(adminGroup);
        }

        return isAdmin != FALSE;
    }

} // namespace Velvet
