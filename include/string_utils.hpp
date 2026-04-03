#pragma once

#include <string>
#include <string_view>
#include <vector>

std::string trim(std::string_view input);
std::string to_upper(std::string_view input);
bool starts_with_ci(std::string_view input, std::string_view prefix);
std::vector<std::string> split_csv(std::string_view input);
std::string unquote(std::string_view input);
std::string join_strings(const std::vector<std::string>& values, std::string_view separator);
std::string normalize_command_name(std::string_view command);
