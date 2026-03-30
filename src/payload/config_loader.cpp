// ============================================================
// VelvetUI - ConfigLoader implementation
// ============================================================

#include "config_loader.h"

#include <Windows.h>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Forward-declare log functions (defined in dllmain.cpp)
namespace Log {
    void Info(const wchar_t* msg);
    void Info(const wchar_t* msg, const wchar_t* detail);
    void Hresult(const wchar_t* msg, HRESULT hr);
}

namespace Velvet {

    // --------------------------------------------------------
    // GetConfigPath - returns path to config.json next to the DLL
    // --------------------------------------------------------
    std::wstring ConfigLoader::GetConfigPath()
    {
        wchar_t dllPath[MAX_PATH]{};
        HMODULE hModule = nullptr;

        // Get handle to our own DLL
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetConfigPath),
            &hModule);

        GetModuleFileNameW(hModule, dllPath, MAX_PATH);

        // Replace "velvet.dll" with "config.json"
        std::wstring path(dllPath);
        auto lastSlash = path.find_last_of(L'\\');
        if (lastSlash != std::wstring::npos) {
            path = path.substr(0, lastSlash + 1);
        }
        path += L"config.json";
        return path;
    }

    // --------------------------------------------------------
    // ParseHexColor - "#RRGGBB" or "RRGGBB" -> uint32_t
    // --------------------------------------------------------
    uint32_t ConfigLoader::ParseHexColor(const std::string& hex)
    {
        std::string clean = hex;
        if (!clean.empty() && clean[0] == '#') {
            clean = clean.substr(1);
        }

        if (clean.length() != 6) {
            return 0x000000; // fallback to black
        }

        try {
            return static_cast<uint32_t>(std::stoul(clean, nullptr, 16));
        }
        catch (...) {
            return 0x000000;
        }
    }

    // --------------------------------------------------------
    // ToHexColor - uint32_t -> "#RRGGBB"
    // --------------------------------------------------------
    std::string ConfigLoader::ToHexColor(uint32_t rgb)
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "#%06X", rgb & 0xFFFFFF);
        return std::string(buf);
    }

    // --------------------------------------------------------
    // Load
    // --------------------------------------------------------
    VelvetConfig ConfigLoader::Load()
    {
        VelvetConfig config{}; // defaults from struct initializers

        std::wstring path = GetConfigPath();
        Log::Info(L"ConfigLoader: buscando config en", path.c_str());

        // Try to open the file
        std::ifstream file(path);
        if (!file.is_open()) {
            Log::Info(L"ConfigLoader: config.json no encontrado, creando con defaults");
            Save(config);
            return config;
        }

        // Parse JSON
        json j;
        try {
            file >> j;
        }
        catch (const json::parse_error& e) {
            // Convert error message for logging
            wchar_t buf[512];
            swprintf_s(buf, L"ConfigLoader: error de parseo JSON - usando defaults (offset %zu)",
                static_cast<size_t>(e.byte));
            Log::Info(buf);
            return config;
        }

        try {
            // --- Floating ---
            if (j.contains("floating") && j["floating"].is_object()) {
                auto& f = j["floating"];
                if (f.contains("enabled"))          config.floating.enabled          = f["enabled"].get<bool>();
                if (f.contains("marginHorizontal")) config.floating.marginHorizontal = f["marginHorizontal"].get<double>();
                if (f.contains("marginBottom"))     config.floating.marginBottom     = f["marginBottom"].get<double>();
                if (f.contains("cornerRadius"))     config.floating.cornerRadius     = f["cornerRadius"].get<double>();
                if (f.contains("padding"))          config.floating.padding          = f["padding"].get<double>();
                if (f.contains("taskbarHeight"))    config.floating.taskbarHeight    = f["taskbarHeight"].get<double>();
                if (f.contains("iconSize"))         config.floating.iconSize         = f["iconSize"].get<double>();
                if (f.contains("taskbarButtonWidth")) config.floating.taskbarButtonWidth = f["taskbarButtonWidth"].get<double>();
            }

            // --- Glass ---
            if (j.contains("glass") && j["glass"].is_object()) {
                auto& g = j["glass"];
                if (g.contains("enabled"))         config.glass.enabled         = g["enabled"].get<bool>();
                if (g.contains("blurAmount"))      config.glass.blurAmount      = g["blurAmount"].get<float>();
                if (g.contains("saturation"))      config.glass.saturation      = g["saturation"].get<float>();
                if (g.contains("tintColor"))       config.glass.tintColor       = ParseHexColor(g["tintColor"].get<std::string>());
                if (g.contains("tintOpacity"))     config.glass.tintOpacity     = g["tintOpacity"].get<float>();
                if (g.contains("noiseOpacity"))    config.glass.noiseOpacity    = g["noiseOpacity"].get<float>();
                if (g.contains("specularOpacity")) config.glass.specularOpacity = g["specularOpacity"].get<float>();
                if (g.contains("specularColor"))   config.glass.specularColor   = ParseHexColor(g["specularColor"].get<std::string>());
            }

            // --- Border ---
            if (j.contains("border") && j["border"].is_object()) {
                auto& b = j["border"];
                if (b.contains("enabled"))   config.border.enabled   = b["enabled"].get<bool>();
                if (b.contains("thickness")) config.border.thickness = b["thickness"].get<double>();
                if (b.contains("color"))     config.border.color     = b["color"].get<std::string>();
            }
        }
        catch (const json::exception&) {
            Log::Info(L"ConfigLoader: tipos invalidos en config.json - usando defaults/parcial");
        }

        Log::Info(L"ConfigLoader: config.json cargado OK");
        return config;
    }

    // --------------------------------------------------------
    // Save
    // --------------------------------------------------------
    bool ConfigLoader::Save(const VelvetConfig& config)
    {
        json j;

        j["floating"] = {
            {"enabled",          config.floating.enabled},
            {"marginHorizontal", config.floating.marginHorizontal},
            {"marginBottom",     config.floating.marginBottom},
            {"cornerRadius",     config.floating.cornerRadius},
            {"padding",          config.floating.padding},
            {"taskbarHeight",    config.floating.taskbarHeight},
            {"iconSize",         config.floating.iconSize},
            {"taskbarButtonWidth", config.floating.taskbarButtonWidth}
        };

        j["glass"] = {
            {"enabled",         config.glass.enabled},
            {"blurAmount",      config.glass.blurAmount},
            {"saturation",      config.glass.saturation},
            {"tintColor",       ToHexColor(config.glass.tintColor)},
            {"tintOpacity",     config.glass.tintOpacity},
            {"noiseOpacity",    config.glass.noiseOpacity},
            {"specularOpacity", config.glass.specularOpacity},
            {"specularColor",   ToHexColor(config.glass.specularColor)}
        };

        j["border"] = {
            {"enabled",   config.border.enabled},
            {"thickness", config.border.thickness},
            {"color",     config.border.color}
        };

        std::wstring path = GetConfigPath();
        std::ofstream file(path);
        if (!file.is_open()) {
            Log::Info(L"ConfigLoader: no se pudo crear config.json");
            return false;
        }

        file << j.dump(2);
        Log::Info(L"ConfigLoader: config.json guardado OK");
        return true;
    }

} // namespace Velvet
