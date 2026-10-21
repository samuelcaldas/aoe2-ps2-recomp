#pragma once

#include <aoe2/runtime/deci2_host.hpp>

class PS2Runtime;
struct R5900Context;

namespace aoe2::runtime {

class Deci2RuntimeSession {
public:
    explicit Deci2RuntimeSession(PS2Runtime& runtime, Deci2Host::Sink sink = {});
    ~Deci2RuntimeSession();
    Deci2RuntimeSession(const Deci2RuntimeSession&) = delete;
    Deci2RuntimeSession& operator=(const Deci2RuntimeSession&) = delete;
    void reset();
    void verify_output() const;
    void close();

private:
    PS2Runtime& runtime_;
    std::shared_ptr<Deci2Host> host_;
};

std::shared_ptr<Deci2Host> find_deci2_host(PS2Runtime& runtime);
void invoke_deci2_handler(PS2Runtime& runtime, R5900Context& context,
                         std::shared_ptr<Deci2Host> host, Deci2Notification notification);
void write_game_tty(std::string_view text);

}
