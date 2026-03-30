// ============================================================
// VelvetUI - Internal taskbar frame-size hooks
// ============================================================

#pragma once

#include "config.h"

#include <Windows.h>

namespace Velvet::InternalTaskbarHooks {

    bool Initialize(const VelvetConfig& config, HWND primaryTaskbar);
    void RequestShellRefresh(HWND primaryTaskbar);
    void Shutdown();

} // namespace Velvet::InternalTaskbarHooks
