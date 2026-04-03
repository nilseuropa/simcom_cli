#include "string_utils.hpp"

#include <cctype>

std::string trim(std::string_view input) {
    std::size_t start = 0;
    while (start < input.size() &&
           std::isspace(static_cast<unsigned char>(input[start])) != 0) {
        ++start;
    }

    std::size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
        --end;
    }

    return std::string(input.substr(start, end - start));
}

std::string to_upper(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (const char ch : input) {
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
    return out;
}

bool starts_with_ci(std::string_view input, std::string_view prefix) {
    if (prefix.size() > input.size()) {
        return false;
    }

    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(input[i])) !=
            std::toupper(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> split_csv(std::string_view input) {
    std::vector<std::string> parts;
    std::string current;
    bool in_quotes = false;

    for (std::size_t i = 0; i < input.size(); ++i) {
        const char ch = input[i];
        if (ch == '"') {
            in_quotes = !in_quotes;
            current.push_back(ch);
            continue;
        }

        if (ch == ',' && !in_quotes) {
            parts.push_back(trim(current));
            current.clear();
            continue;
        }

        current.push_back(ch);
    }

    parts.push_back(trim(current));
    return parts;
}

std::string unquote(std::string_view input) {
    const std::string value = trim(input);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::string join_strings(const std::vector<std::string>& values, std::string_view separator) {
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out.append(separator);
        }
        out.append(values[i]);
    }
    return out;
}

std::string normalize_command_name(std::string_view command) {
    std::string upper = to_upper(trim(command));

    while (!upper.empty() && (upper.back() == '\r' || upper.back() == '\n')) {
        upper.pop_back();
    }

    if (starts_with_ci(upper, "AT")) {
        upper.erase(0, 2);
    }

    if (upper.empty()) {
        return "AT";
    }

    std::size_t end = 0;
    while (end < upper.size()) {
        const char ch = upper[end];
        if (ch == '=' || ch == '?' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
            break;
        }
        ++end;
    }

    if (end == 0) {
        return upper;
    }

    return upper.substr(0, end);
}
