// Runner behavior adapted from the pinned PS2Recomp runner (GPL-3.0).
#include "game_entry.hpp"
#include <cstdlib>
#include <exception>
#include <iostream>
#include <system_error>

#if defined(__ANDROID__)
#include <android/log.h>
#include <cstdio>
#include <cstring>
#include <thread>
#include <unistd.h>

namespace {
int logcat_pipe[2] = {-1, -1};
std::thread logcat_thread;

void stop_logcat() {
    std::fflush(stdout);
    std::fflush(stderr);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    if (logcat_pipe[1] >= 0) close(logcat_pipe[1]);
    logcat_pipe[1] = -1;
    if (logcat_thread.joinable()) logcat_thread.join();
}

void read_logcat() {
    FILE* reader = fdopen(logcat_pipe[0], "r");
    if (!reader) return;
    char line[1024];
    while (fgets(line, sizeof(line), reader)) {
        const auto length = std::strlen(line);
        if (length && line[length - 1] == '\n') line[length - 1] = '\0';
        __android_log_write(ANDROID_LOG_INFO, "ps2x", line);
    }
    fclose(reader);
}

void redirect_logcat() {
    if (pipe(logcat_pipe) != 0) throw std::system_error(errno, std::generic_category(), "logcat pipe");
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    if (dup2(logcat_pipe[1], STDOUT_FILENO) < 0 || dup2(logcat_pipe[1], STDERR_FILENO) < 0)
        throw std::system_error(errno, std::generic_category(), "logcat redirect");
    logcat_thread = std::thread(read_logcat);
    if (std::atexit(stop_logcat) != 0) stop_logcat();
}
}
#elif defined(__SWITCH__)
#include <switch.h>
#endif

namespace aoe2::platform {
void initialize_game_logging() {
#if defined(__ANDROID__)
    redirect_logcat();
#elif defined(__SWITCH__)
    // Route stderr / debug logs to svcOutputDebugString for capture by Ryujinx / nxlink
    consoleDebugInit(debugDevice_SVC);
#endif
    std::set_terminate([] {
        std::cerr << "[terminate] unhandled exception\n";
        try {
            if (const auto error = std::current_exception()) std::rethrow_exception(error);
        } catch (const std::system_error& error) {
            std::cerr << "[terminate] system_error " << error.code() << ": " << error.what() << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[terminate] exception: " << error.what() << '\n';
        } catch (...) {
            std::cerr << "[terminate] non-standard exception\n";
        }
        std::abort();
    });
}
}
