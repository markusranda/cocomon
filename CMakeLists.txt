cmake_minimum_required(VERSION 3.20)

cmake_policy(SET CMP0091 NEW)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(cocomon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(cocomon main.cpp)

target_include_directories(cocomon PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_directories(cocomon PRIVATE
    ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(cocomon PRIVATE
    raylib
    gdi32
    winmm
    user32
    shell32
)

# ----------------------------
# Tracy (Debug only)
# ----------------------------
set(TRACY_DIR "${CMAKE_SOURCE_DIR}/../tracy")
target_include_directories(cocomon SYSTEM PRIVATE
    $<$<CONFIG:Debug>:${TRACY_DIR}/public>
)
target_sources(cocomon PRIVATE
    $<$<CONFIG:Debug>:${TRACY_DIR}/public/TracyClient.cpp>
)
target_compile_definitions(cocomon PRIVATE
    $<$<CONFIG:Debug>:TRACY_ENABLE>
)
