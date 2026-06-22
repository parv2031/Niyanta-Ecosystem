cmake_minimum_required(VERSION 3.10)
project(TakshakDB)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ensure thread support (we'll need this heavily in Phase 3/4)
find_package(Threads REQUIRED)

add_executable(takshak-server src/server.cpp src/parser.cpp src/lru_cache.cpp src/aof.cpp src/pubsub.cpp)
target_link_libraries(takshak-server Threads::Threads)
