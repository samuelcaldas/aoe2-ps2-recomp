// Headless runtime-initialization probe for the recompiled game.
//
// Performs the upstream runner's boot sequence up to, but not including,
// PS2Runtime::run(): memory and core subsystems are initialized without a
// window or audio device, the owned ELF is loaded into guest memory, and the
// entry point is checked against the generated function table. No recompiled
// guest function is ever called. Output reports only counts and yes/no checks,
// never guest addresses.
#include "ps2_runtime.h"
#include <aoe2/runtime/deci2_runtime_session.hpp>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

extern const uint32_t g_ps2RecompiledFunctionTableSlotCount;

namespace {

constexpr int kExitOk = 0;
constexpr int kExitInitFailed = 1;
constexpr int kExitUsage = 2;

struct ProbeOptions {
    std::string elf_path;
    std::optional<std::string> function_map_path;
};

class ProbeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

ProbeOptions ParseOptions(int argc, char* argv[]) {
    ProbeOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + flag);
        }
        const std::string value = argv[++index];
        if (flag == "--elf") {
            options.elf_path = value;
        } else if (flag == "--function-map") {
            options.function_map_path = value;
        } else {
            throw std::invalid_argument("unknown option " + flag);
        }
    }
    if (options.elf_path.empty()) {
        throw std::invalid_argument("--elf is required");
    }
    return options;
}

void Require(bool condition, const std::string& step) {
    if (!condition) {
        throw ProbeError(step + " failed");
    }
    std::cout << "[probe] " << step << ": ok" << std::endl;
}

// Mirrors PS2Runtime::initialize() without InitWindow()/InitAudioDevice(),
// as the upstream runtime tests do.
void InitializeHeadless(PS2Runtime& runtime, const std::string& elf_path) {
    Require(runtime.memory().initialize(), "memory initialize");
    Require(runtime.syncCoreSubsystems(), "core subsystems bind");
    Require(runtime.loadELF(elf_path), "ELF load");
}

std::vector<uint32_t> ReadFunctionMapStarts(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw ProbeError("cannot open function map " + path);
    }
    std::string line;
    std::getline(file, line);
    std::vector<uint32_t> starts;
    while (std::getline(file, line)) {
        std::istringstream row(line);
        std::string name;
        std::string start;
        if (std::getline(row, name, ',') && std::getline(row, start, ',')) {
            starts.push_back(static_cast<uint32_t>(std::stoul(start, nullptr, 16)));
        }
    }
    if (starts.empty()) {
        throw ProbeError("function map has no entries: " + path);
    }
    return starts;
}

void ReportMapCoverage(const PS2Runtime& runtime, const std::string& path) {
    const std::vector<uint32_t> starts = ReadFunctionMapStarts(path);
    std::size_t registered = 0;
    for (uint32_t start : starts) {
        registered += runtime.hasFunction(start) ? 1 : 0;
    }
    std::cout << "[probe] function map entries registered: " << registered << "/" << starts.size() << std::endl;
}

int RunProbe(const ProbeOptions& options) {
    std::cout << "[probe] generated table slots: " << g_ps2RecompiledFunctionTableSlotCount << std::endl;
    PS2Runtime runtime;
    aoe2::runtime::Deci2RuntimeSession tty(runtime);
    tty.reset();
    InitializeHeadless(runtime, options.elf_path);
    Require(runtime.hasFunction(runtime.cpu().pc), "entry point has a recompiled function");
    if (options.function_map_path) {
        ReportMapCoverage(runtime, *options.function_map_path);
    }
    std::cout << "[probe] runtime initialized: entry point resolved; guest code not executed" << std::endl;
    return kExitOk;
}

} // namespace

int main(int argc, char* argv[]) {
    int exit_code = kExitInitFailed;
    try {
        exit_code = RunProbe(ParseOptions(argc, argv));
    } catch (const std::invalid_argument& error) {
        std::cerr << "[probe] usage error: " << error.what() << "\n"
                  << "usage: desktop_runtime_probe --elf <AOK.ELF> [--function-map <function_map.csv>]" << std::endl;
        exit_code = kExitUsage;
    } catch (const std::exception& error) {
        std::cerr << "[probe] error: " << error.what() << std::endl;
    }
    std::cout.flush();
    std::cerr.flush();
    // Like the upstream runner, skip static/runtime destructors that assume run() happened.
    std::_Exit(exit_code);
}
