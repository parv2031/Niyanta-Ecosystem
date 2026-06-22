#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include "parser.h"
#include "lru_cache.h"

// Phase 6: Append-Only File Persistence
class AOF {
private:
    std::string filename;
    std::ofstream file_stream;
    std::mutex aof_mutex; // Protects file writes from multiple threads

public:
    AOF(const std::string& file);
    ~AOF();

    // Appends a write command to the log
    void append(const Command& cmd);

    // Reads the log and restores the cache state on startup
    void restore(LRUCache& cache);
};
