#include "serial_port.hpp"

#include <cerrno>
#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

namespace {

speed_t to_termios_speed(const int baud_rate) {
    switch (baud_rate) {
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
    case 230400:
        return B230400;
#ifdef B460800
    case 460800:
        return B460800;
#endif
#ifdef B921600
    case 921600:
        return B921600;
#endif
    default:
        throw std::runtime_error("unsupported baud rate: " + std::to_string(baud_rate));
    }
}

[[noreturn]] void throw_system_error(const std::string& prefix) {
    throw std::runtime_error(prefix + ": " + std::strerror(errno));
}

} // namespace

SerialPort::SerialPort(SerialPort&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

SerialPort& SerialPort::operator=(SerialPort&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

SerialPort::~SerialPort() {
    close();
}

void SerialPort::open(const std::string& path, const int baud_rate) {
    close();

    fd_ = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (fd_ < 0) {
        throw_system_error("failed to open serial port " + path);
    }

    termios tty{};
    if (::tcgetattr(fd_, &tty) != 0) {
        throw_system_error("tcgetattr failed for " + path);
    }

    ::cfmakeraw(&tty);
    tty.c_cflag |= static_cast<tcflag_t>(CLOCAL | CREAD);
    tty.c_cflag &= static_cast<tcflag_t>(~CSIZE);
    tty.c_cflag |= CS8;
    tty.c_cflag &= static_cast<tcflag_t>(~(PARENB | PARODD | CSTOPB | CRTSCTS));
    tty.c_iflag = IGNPAR;
    tty.c_oflag = 0;
    tty.c_lflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    const speed_t speed = to_termios_speed(baud_rate);
    if (::cfsetispeed(&tty, speed) != 0 || ::cfsetospeed(&tty, speed) != 0) {
        throw_system_error("failed to configure baud rate for " + path);
    }

    if (::tcsetattr(fd_, TCSANOW, &tty) != 0) {
        throw_system_error("tcsetattr failed for " + path);
    }

    ::tcflush(fd_, TCIOFLUSH);
}

void SerialPort::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool SerialPort::is_open() const noexcept {
    return fd_ >= 0;
}

void SerialPort::write_all(std::string_view data) const {
    std::size_t written = 0;
    while (written < data.size()) {
        const ssize_t rc = ::write(fd_, data.data() + written, data.size() - written);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw_system_error("serial write failed");
        }
        written += static_cast<std::size_t>(rc);
    }

    if (::tcdrain(fd_) != 0) {
        throw_system_error("tcdrain failed");
    }
}

std::string SerialPort::read_some(const int timeout_ms) const {
    pollfd poll_fd{};
    poll_fd.fd = fd_;
    poll_fd.events = POLLIN;

    const int rc = ::poll(&poll_fd, 1, timeout_ms);
    if (rc < 0) {
        if (errno == EINTR) {
            return {};
        }
        throw_system_error("poll failed");
    }

    if (rc == 0) {
        return {};
    }

    if ((poll_fd.revents & POLLIN) == 0) {
        return {};
    }

    char buffer[4096];
    const ssize_t bytes_read = ::read(fd_, buffer, sizeof(buffer));
    if (bytes_read < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            return {};
        }
        throw_system_error("serial read failed");
    }

    return std::string(buffer, static_cast<std::size_t>(bytes_read));
}
