project(VelvetPayload LANGUAGES C CXX)

# ============================================================
# C++/WinRT requires C++17 (already set in root CMakeLists)
# We need to link against WindowsApp.lib for WinRT projections,
# and ole32/oleaut32 for COM.
# ============================================================

add_library(VelvetPayload SHARED
    dllmain.cpp
    internal_taskbar_hooks.cpp
    windhawk_symbol_resolver.cpp
    visual_tree_watcher.cpp
    velvet_tap.cpp
    taskbar_modifier.cpp
    config_loader.cpp
    ${CMAKE_SOURCE_DIR}/third_party/minhook/src/buffer.c
    ${CMAKE_SOURCE_DIR}/third_party/minhook/src/hook.c
    ${CMAKE_SOURCE_DIR}/third_party/minhook/src/trampoline.c
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_sources(VelvetPayload PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/minhook/src/hde/hde64.c
    )
else()
    target_sources(VelvetPayload PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/minhook/src/hde/hde32.c
    )
endif()

target_include_directories(VelvetPayload PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party                  # nlohmann/json.hpp
    ${CMAKE_SOURCE_DIR}/third_party/minhook/include
    ${CMAKE_SOURCE_DIR}/third_party/minhook/src
    ${CMAKE_SOURCE_DIR}/third_party/windhawk-symbol-helper/libraries
)

# Link libraries required for COM + WinRT + Composition
target_link_libraries(VelvetPayload PRIVATE
    WindowsApp.lib      # WinRT projections (winrt::implements, etc.)
    ole32.lib            # CoInitializeEx, COM runtime
    oleaut32.lib         # BSTR, SAFEARRAY, etc.
    RuntimeObject.lib    # IInspectable
    Dbghelp.lib
    ${CMAKE_SOURCE_DIR}/third_party/windhawk-symbol-helper/libraries/dia/lib/amd64/diaguids.lib
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

# Copy config.json to output directory alongside velvet.dll
add_custom_command(TARGET VelvetPayload POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config.json"
        "$<TARGET_FILE_DIR:VelvetPayload>/config.json"
    COMMENT "Copying config.json to output directory"
)
