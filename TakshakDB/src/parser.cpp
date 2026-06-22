#include "parser.h"
#include <sstream>
#include <algorithm>
#include <cctype>

Command Parser::parse(const std::string& raw_input) {
    Command cmd;
    if (raw_input.empty()) return cmd;

    std::istringstream iss(raw_input);
    std::string token;

    // Read the first token as the command name (e.g. "SET")
    if (iss >> token) {
        // Convert command to uppercase for case-insensitivity
        std::transform(token.begin(), token.end(), token.begin(), ::toupper);
        cmd.name = token;
    }

    // Read the rest of the tokens as arguments
    while (iss >> token) {
        cmd.args.push_back(token);
    }

    return cmd;
}
