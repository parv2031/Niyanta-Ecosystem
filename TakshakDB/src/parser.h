#pragma once
#include <string>
#include <vector>

// Represents a parsed database command
struct Command {
    std::string name;
    std::vector<std::string> args;
};

class Parser {
public:
    // Parses raw inline string (e.g. "SET name Arjun") into a Command struct
    static Command parse(const std::string& raw_input);
};
