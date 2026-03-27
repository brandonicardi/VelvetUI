project(VelvetInjector LANGUAGES CXX)

add_executable(VelvetInjector
    main.cpp
    process.cpp
    injector.cpp
)

target_include_directories(VelvetInjector PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(VelvetInjector PROPERTIES
    OUTPUT_NAME "VelvetUI"
)