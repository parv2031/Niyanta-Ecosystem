#include "aof.h"
#include <iostream>

AOF::AOF(const std::string& file) : filename(file) {
    // Open in append mode, create if it doesn't exist
    file_stream.open(filename, std::ios::app);
    if (!file_stream.is_open()) {
        std::cerr << "Failed to open AOF file: " << filename << "\n";
    }
}

AOF::~AOF() {
    if (file_stream.is_open()) {
        file_stream.close();
    }
}

void AOF::append(const Command& cmd) {
    if (!file_stream.is_open()) return;

    std::lock_guard<std::mutex> lock(aof_mutex);

    // Reconstruct the inline command string (e.g., "SET name Arjun")
    std::string log_entry = cmd.name;
    for (const auto& arg : cmd.args) {
        log_entry += " " + arg;
    }
    log_entry += "\n";

    file_stream << log_entry;
    
    // In a production database, calling flush() on every command is slow.
    // Real Redis allows configuring this (e.g., flush every second).
    // We flush immediately for safety.
    // file_stream.flush(); // Commented out: massively bottlenecks chess engine cache
}

void AOF::restore(LRUCache& cache) {
    std::ifstream in_file(filename);
    if (!in_file.is_open()) {
        std::cout << "No existing AOF file found. Starting fresh.\n";
        return;
    }

    std::cout << "Restoring database state from AOF...\n";
    std::string line;
    int commands_restored = 0;

    // Read every line in the log file and replay the commands
    while (std::getline(in_file, line)) {
        Command cmd = Parser::parse(line);
        
        if (cmd.name == "SET" && cmd.args.size() >= 2) {
            cache.put(cmd.args[0], cmd.args[1]);
        } else if (cmd.name == "DEL" && cmd.args.size() == 1) {
            cache.remove_key(cmd.args[0]);
        } else if (cmd.name == "EXPIRE" && cmd.args.size() == 2) {
            try {
                int seconds = std::stoi(cmd.args[1]);
                cache.expire(cmd.args[0], seconds);
            } catch (...) {}
        }
        commands_restored++;
    }

    std::cout << "Restored " << commands_restored << " commands from AOF.\n";
    in_file.close();
}
