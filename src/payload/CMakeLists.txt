project(VelvetPayload LANGUAGES CXX)

# ============================================================
# C++/WinRT requires C++17 (already set in root CMakeLists)
# We need to link against WindowsApp.lib for WinRT projections,
# and ole32/oleaut32 for COM.
# ============================================================

add_library(VelvetPayload SHARED
    dllmain.cpp
    visual_tree_watcher.cpp
    velvet_tap.cpp
)

target_include_directories(VelvetPayload PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link libraries required for COM + WinRT
target_link_libraries(VelvetPayload PRIVATE
    WindowsApp.lib      # WinRT projections (winrt::implements, etc.)
    ole32.lib            # CoInitializeEx, COM runtime
    oleaut32.lib         # BSTR, SAFEARRAY, etc.
    RuntimeObject.lib    # IInspectable
)

# DEF file to ensure DllGetClassObject and DllCanUnloadNow
# are exported with the correct names (no name mangling).
# This is critical: InitializeXamlDiagnosticsEx calls
# LoadLibrary on our DLL and then GetProcAddress for these.
set_target_properties(VelvetPayload PROPERTIES
    OUTPUT_NAME "velvet"
    PREFIX ""
    LINK_FLAGS "/DEF:\"${CMAKE_CURRENT_SOURCE_DIR}/velvet.def\""
)
