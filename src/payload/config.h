// ============================================================
// VelvetUI - Configuration types
// Fase 3: Floating Taskbar + Liquid Glass
//
// Structs that hold all tunable parameters.
// Loaded from config.json by ConfigLoader.
// ============================================================

#pragma once

#include <string>
#include <cstdint>

namespace Velvet {

    struct FloatingConfig {
        bool     enabled          = true;
        double   marginHorizontal = 250.0;
        double   marginBottom     = 22.0;
        double   cornerRadius     = 16.0;
        double   padding          = 10.0;
        double   taskbarHeight    = 60.0;
        double   iconSize         = 28.0;
        double   taskbarButtonWidth = 52.0;
    };

    struct GlassConfig {
        bool     enabled          = true;
        float    blurAmount       = 12.0f;
        float    saturation       = 1.8f;
        uint32_t tintColor        = 0x000000;   // RGB, sin alpha
        float    tintOpacity      = 0.08f;
        float    noiseOpacity     = 0.02f;
        float    specularOpacity  = 0.18f;
        uint32_t specularColor    = 0xFFFFFF;   // RGB, sin alpha
    };

    struct BorderConfig {
        bool        enabled       = true;
        double      thickness     = 0.8;
        std::string color         = "auto";     // "auto" = SurfaceStrokeColor del theme
    };

    struct VelvetConfig {
        FloatingConfig floating;
        GlassConfig    glass;
        BorderConfig   border;
    };

} // namespace Velvet
