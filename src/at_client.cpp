#include "at_client.hpp"

#include "string_utils.hpp"

#include <algorithm>
#include <chrono>

namespace {

bool is_final_result_line(const std::string& line) {
    const std::string upper = to_upper(trim(line));
    return upper == "OK" || upper == "ERROR" || upper == "CONNECT" ||
           upper == "NO CARRIER" || upper == "NO ANSWER" || upper == "BUSY" ||
           starts_with_ci(upper, "+CME ERROR:") || starts_with_ci(upper, "+CMS ERROR:");
}

bool is_success_result(const std::string& line) {
    return to_upper(trim(line)) == "OK" || to_upper(trim(line)) == "CONNECT";
}

bool is_echo_line(const std::string& line, const std::string& command) {
    return to_upper(trim(line)) == to_upper(trim(command));
}

} // namespace

AtClient::AtClient(SerialPort& port) : port_(port) {}

std::vector<std::string> AtClient::read_lines(const int timeout_ms) {
    std::vector<std::string> lines;
    rx_buffer_.append(port_.read_some(timeout_ms));

    while (true) {
        const std::size_t eol = rx_buffer_.find_first_of("\r\n");
        if (eol == std::string::npos) {
            break;
        }

        const std::string line = trim(rx_buffer_.substr(0, eol));
        if (!line.empty()) {
            lines.push_back(line);
        }

        const std::size_t next = rx_buffer_.find_first_not_of("\r\n", eol);
        if (next == std::string::npos) {
            rx_buffer_.clear();
            break;
        }

        rx_buffer_.erase(0, next);
    }

    return lines;
}

AtResponse AtClient::send_command(const std::string& command, const int timeout_ms) {
    AtResponse response;
    response.command = trim(command);

    rx_buffer_.clear();
    const auto drain_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
    while (std::chrono::steady_clock::now() < drain_deadline && !port_.read_some(20).empty()) {
    }

    std::string wire_command = response.command;
    if (wire_command.empty()) {
        wire_command = "AT";
    }
    if (wire_command.back() != '\r') {
        wire_command.push_back('\r');
    }

    port_.write_all(wire_command);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto now = std::chrono::steady_clock::now();
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        const int wait_ms = static_cast<int>(std::clamp<long long>(remaining, 1, 250));

        for (const auto& line : read_lines(wait_ms)) {
            if (is_echo_line(line, response.command)) {
                response.echoed_lines.push_back(line);
                continue;
            }

            if (is_final_result_line(line)) {
                response.final_result = line;
                response.success = is_success_result(line);
                return response;
            }

            response.information_lines.push_back(line);
        }
    }

    response.timed_out = true;
    response.final_result = "TIMEOUT";
    return response;
}
