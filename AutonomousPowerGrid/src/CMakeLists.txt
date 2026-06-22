cmake_minimum_required(VERSION 3.16)
project(PowerGridRL CXX)

set(CMAKE_CXX_STANDARD 17)

# ── Sources ─────────────────────────────────────────
file(GLOB CPP_SOURCES "cpp/*.cpp")
add_executable(powergrid_rl ${CPP_SOURCES})

# ── Includes ────────────────────────────────────────
target_include_directories(powergrid_rl PRIVATE
    .
    cpp
)

# ── Link libraries ──────────────────────────────────
target_link_libraries(powergrid_rl PRIVATE
    raylib
    GL
    m
    pthread
    dl
    rt
    X11
    zmq
)
add_custom_command(
    TARGET powergrid_rl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/cpp/demand_data.csv
    $<TARGET_FILE_DIR:powergrid_rl>/demand_data.csv
)