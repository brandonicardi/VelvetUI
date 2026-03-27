project(VelvetPayload LANGUAGES CXX)

add_library(VelvetPayload SHARED
    dllmain.cpp
)

target_include_directories(VelvetPayload PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(VelvetPayload PROPERTIES
    OUTPUT_NAME "velvet"
    PREFIX ""
)