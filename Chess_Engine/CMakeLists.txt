cmake_minimum_required(VERSION 3.10)

project(GtkChessBot C)

set(CMAKE_C_STANDARD 11)

# Find GTK3 package
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

# Include directories
include_directories(
    ${GTK3_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src
)

link_directories(${GTK3_LIBRARY_DIRS})

add_definitions(${GTK3_CFLAGS_OTHER})

# Source files
set(SOURCES
    src/main.c
    src/game.c
    src/globals.c
    src/gui.c
    src/ai.c
)

# Create executable
add_executable(chessbot ${SOURCES})

# Link GTK3 libraries
target_link_libraries(chessbot ${GTK3_LIBRARIES})
