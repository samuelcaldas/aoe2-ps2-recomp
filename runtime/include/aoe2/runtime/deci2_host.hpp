#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>

namespace aoe2::runtime {

inline constexpr std::uint16_t EE_TTY_PROTOCOL = 0x0210;

enum class Deci2Event : std::uint32_t { Write = 3, WriteDone = 4, Error = 6 };

class Deci2Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Deci2Notification {
    int socket;
    std::uint64_t generation;
    Deci2Event event;
    std::uint32_t handler;
    std::uint32_t option;
};

class Deci2Host {
public:
    using Sink = std::function<void(std::string_view)>;
    explicit Deci2Host(Sink sink);
    ~Deci2Host();
    Deci2Host(const Deci2Host&) = delete;
    Deci2Host& operator=(const Deci2Host&) = delete;

    int open(std::uint16_t protocol, std::uint32_t handler, std::uint32_t option);
    void close(int socket);
    void request(int socket, std::uint32_t destination);
    std::optional<Deci2Notification> poll(int socket);
    std::size_t send(int socket, std::span<const std::uint8_t> bytes);
    void finish(const Deci2Notification& notification);
    void lock(int socket, bool locked);
    void check(int socket) const;
    void fail(int socket);
    void reset();

private:
    struct State;
    std::unique_ptr<State> state_;
    Sink sink_;
};

// Escape terminal commands without changing printable UTF-8 bytes.
std::string escape_tty_text(std::string_view text);

}
