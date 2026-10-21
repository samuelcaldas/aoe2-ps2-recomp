#include <aoe2/runtime/deci2_host.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aoe2::runtime;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Action>
void rejects(Action action) {
    try { action(); }
    catch (const Deci2Error&) { return; }
    throw std::runtime_error("invalid operation was accepted");
}

std::vector<std::uint8_t> packet(std::string text) {
    std::vector<std::uint8_t> bytes(12, 0);
    const auto length = text.size() + bytes.size();
    bytes[0] = length & 255;
    bytes[1] = length >> 8;
    bytes[4] = 0x10;
    bytes[5] = 0x02;
    bytes[6] = 'E';
    bytes[7] = 'H';
    bytes.insert(bytes.end(), text.begin(), text.end());
    return bytes;
}

void completes_only_after_capture() {
    std::string output;
    Deci2Host host([&](std::string_view text) { output += text; });
    const auto socket = host.open(EE_TTY_PROTOCOL, 123, 456);
    host.request(socket, 'H');
    auto writing = host.poll(socket);
    require(writing && writing->event == Deci2Event::Write, "missing write event");
    require(writing->handler == 123 && writing->option == 456, "wrong callback ABI");
    const auto bytes = packet("hello\n");
    require(host.send(socket, bytes) == bytes.size(), "wrong accepted length");
    require(output == "hello\n", "transport header leaked into text");
    host.finish(*writing);
    auto done = host.poll(socket);
    require(done && done->event == Deci2Event::WriteDone, "missing completion");
    host.finish(*done);
    require(!host.poll(socket), "completion repeated");
    require(output == "hello\n", "output repeated");
}

void assembles_split_header_and_large_payload() {
    std::string output;
    Deci2Host host([&](std::string_view text) { output += text; });
    const auto socket = host.open(EE_TTY_PROTOCOL, 1, 2);
    host.request(socket, 'H');
    auto first = *host.poll(socket);
    const auto bytes = packet(std::string(900, 'x'));
    host.send(socket, std::span(bytes).first(3));
    host.finish(first);
    require(output.empty(), "partial packet emitted");
    auto second = *host.poll(socket);
    host.send(socket, std::span(bytes).subspan(3));
    host.finish(second);
    require(output == std::string(900, 'x'), "long payload truncated");
}

void rejects_invalid_lifecycle() {
    Deci2Host host([](std::string_view) {});
    rejects([&] { host.open(1, 1, 2); });
    const auto first = host.open(EE_TTY_PROTOCOL, 1, 2);
    const auto second = host.open(EE_TTY_PROTOCOL, 1, 2);
    require(first != second, "socket reused");
    rejects([&] { host.request(first, 'X'); });
    host.request(first, 'H');
    rejects([&] { host.request(first, 'H'); });
    const auto stale = *host.poll(first);
    host.close(first);
    host.finish(stale);
    rejects([&] { host.poll(first); });
    host.reset();
    rejects([&] { host.poll(second); });
}

void reports_malformed_packet_and_no_progress() {
    Deci2Host host([](std::string_view) {});
    const auto socket = host.open(EE_TTY_PROTOCOL, 1, 2);
    host.request(socket, 'H');
    auto writing = *host.poll(socket);
    auto bytes = packet("oops");
    bytes[0] = 2;
    rejects([&] { host.send(socket, bytes); });
    host.finish(writing);
    require(host.poll(socket)->event == Deci2Event::Error, "missing error event");
    const auto other = host.open(EE_TTY_PROTOCOL, 1, 2);
    host.request(other, 'H');
    host.finish(*host.poll(other));
    require(host.poll(other)->event == Deci2Event::Error, "zero progress accepted");
}

void rejects_sink_failure_without_completion() {
    Deci2Host host([](std::string_view) { throw std::ios_base::failure("sink failed"); });
    const auto socket = host.open(EE_TTY_PROTOCOL, 1, 2);
    host.request(socket, 'H');
    auto writing = *host.poll(socket);
    rejects([&] { host.send(socket, packet("lost")); });
    host.finish(writing);
    require(host.poll(socket)->event == Deci2Event::Error, "failed sink completed");
    rejects([&] { host.verify_output(); });
    host.reset();
    host.verify_output();
}

void does_not_complete_inside_write_handler() {
    Deci2Host host([](std::string_view) {});
    const auto socket = host.open(EE_TTY_PROTOCOL, 1, 2);
    host.request(socket, 'H');
    auto writing = *host.poll(socket);
    host.send(socket, packet("text"));
    require(!host.poll(socket), "completion reentered write handler");
    host.finish(writing);
    require(host.poll(socket)->event == Deci2Event::WriteDone, "completion lost");
}

void preserves_bytes_and_escapes_controls() {
    const std::string text("a\0\x1b\r\n\t\xc3\xa9", 8);
    require(escape_tty_text(text) == "a\\x00\\x1b\\x0d\n\t\xc3\xa9", "unsafe text escaping");
    std::string output;
    Deci2Host host([&](std::string_view value) { output.append(value); });
    const auto socket = host.open(EE_TTY_PROTOCOL, 1, 2);
    host.request(socket, 'H');
    host.poll(socket);
    host.send(socket, packet(text));
    require(output == text, "binary payload not preserved");
}

void locks_and_reset_cancel_pending_events() {
    Deci2Host host([](std::string_view) {});
    const auto socket = host.open(EE_TTY_PROTOCOL, 1, 2);
    host.request(socket, 'H');
    host.lock(socket, true);
    require(!host.poll(socket), "locked channel dispatched");
    host.lock(socket, false);
    const auto stale = *host.poll(socket);
    host.reset();
    const auto fresh = host.open(EE_TTY_PROTOCOL, 3, 4);
    host.request(fresh, 'H');
    host.finish(stale);
    require(host.poll(fresh)->handler == 3, "stale completion affected new channel");
}

int main() {
    try {
        completes_only_after_capture();
        assembles_split_header_and_large_payload();
        rejects_invalid_lifecycle();
        reports_malformed_packet_and_no_progress();
        rejects_sink_failure_without_completion();
        does_not_complete_inside_write_handler();
        preserves_bytes_and_escapes_controls();
        locks_and_reset_cancel_pending_events();
        std::cout << "DECI2 host tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
