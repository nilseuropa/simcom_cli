#pragma once

#include <string>
#include <string_view>

class SerialPort {
public:
    SerialPort() = default;
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;
    SerialPort(SerialPort&& other) noexcept;
    SerialPort& operator=(SerialPort&& other) noexcept;
    ~SerialPort();

    void open(const std::string& path, int baud_rate);
    void close();
    [[nodiscard]] bool is_open() const noexcept;
    void write_all(std::string_view data) const;
    [[nodiscard]] std::string read_some(int timeout_ms) const;

private:
    int fd_{-1};
};
