cmake_minimum_required(VERSION 3.20)
project(VelvetUI
    VERSION 0.1.0
    DESCRIPTION "Windows 11 shell customization - Velvet ecosystem"
    LANGUAGES CXX
)

# ============================================================
# Configuración global
# ============================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output: todos los binarios van a build/bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Windows-specific
if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX)
endif()

# ============================================================
# Subproyectos
# ============================================================
add_subdirectory(src/payload)
add_subdirectory(src/injector)
