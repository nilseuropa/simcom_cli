#pragma once

#include "serial_port.hpp"

#include <string>
#include <vector>

struct AtResponse {
    std::string command;
    std::vector<std::string> echoed_lines;
    std::vector<std::string> information_lines;
    std::string final_result;
    bool success{false};
    bool timed_out{false};
};

class AtClient {
public:
    explicit AtClient(SerialPort& port);

    [[nodiscard]] AtResponse send_command(const std::string& command, int timeout_ms);
    [[nodiscard]] std::vector<std::string> read_lines(int timeout_ms);

private:
    SerialPort& port_;
    std::string rx_buffer_;
};
