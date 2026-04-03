#pragma once

#include "at_client.hpp"

#include <string>
#include <vector>

struct ParsedResponse {
    bool handled{false};
    std::vector<std::string> summary_lines;
};

ParsedResponse parse_response(const AtResponse& response);
std::vector<std::string> supported_parser_commands();
