// ============================================================
// VelvetUI - ConfigLoader
// Reads config.json from the same directory as velvet.dll
//
// Design:
//   - If config.json doesn't exist, creates it with defaults
//   - If a field is missing, uses the default from VelvetConfig
//   - If parsing fails entirely, logs error and uses all defaults
//   - Thread-safe: Load() returns a value copy
// ============================================================

#pragma once

#include "config.h"
#include <string>

namespace Velvet {

    class ConfigLoader {
    public:
        // Loads config from <dllDirectory>/config.json
        // Returns default config on any error.
        static VelvetConfig Load();

        // Saves the given config to <dllDirectory>/config.json
        // Returns true on success.
        static bool Save(const VelvetConfig& config);

    private:
        static std::wstring GetConfigPath();
        static uint32_t ParseHexColor(const std::string& hex);
        static std::string ToHexColor(uint32_t rgb);
    };

} // namespace Velvet
