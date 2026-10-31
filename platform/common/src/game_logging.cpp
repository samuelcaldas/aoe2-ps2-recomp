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
  if (logcat_pipe[1] >= 0)
    close(logcat_pipe[1]);
  logcat_pipe[1] = -1;
  if (logcat_thread.joinable())
    logcat_thread.join();
}

void read_logcat() {
  FILE *reader = fdopen(logcat_pipe[0], "r");
  if (!reader)
    return;
  char line[1024];
  while (fgets(line, sizeof(line), reader)) {
    const auto length = std::strlen(line);
    if (length && line[length - 1] == '\n')
      line[length - 1] = '\0';
    __android_log_write(ANDROID_LOG_INFO, "ps2x", line);
  }
  fclose(reader);
}

void redirect_logcat() {
  if (pipe(logcat_pipe) != 0)
    throw std::system_error(errno, std::generic_category(), "logcat pipe");
  setvbuf(stdout, nullptr, _IOLBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);
  if (dup2(logcat_pipe[1], STDOUT_FILENO) < 0 ||
      dup2(logcat_pipe[1], STDERR_FILENO) < 0)
    throw std::system_error(errno, std::generic_category(), "logcat redirect");
  logcat_thread = std::thread(read_logcat);
  if (std::atexit(stop_logcat) != 0)
    stop_logcat();
}
} // namespace
#elif defined(__SWITCH__)
#include <mutex>
#include <streambuf>
#include <string>
#include <switch.h>

namespace {
class SwitchSvcStreambuf : public std::streambuf {
protected:
  int overflow(int c) override {
    if (c != EOF) {
      char ch = static_cast<char>(c);
      xsputn(&ch, 1);
    }
    return c;
  }

  std::streamsize xsputn(const char *s, std::streamsize count) override {
    std::lock_guard<std::mutex> lock(mutex_);
    for (std::streamsize i = 0; i < count; ++i) {
      char c = s[i];
      if (c == '\n' || buffer_.size() >= 2048) {
        if (c == '\n')
          buffer_.push_back('\n');
        svcOutputDebugString(buffer_.data(), buffer_.size());
        buffer_.clear();
      } else {
        buffer_.push_back(c);
      }
    }
    return count;
  }

  int sync() override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!buffer_.empty() &&
        (buffer_.back() == '\n' || buffer_.size() >= 1024)) {
      svcOutputDebugString(buffer_.data(), buffer_.size());
      buffer_.clear();
    }
    return 0;
  }

private:
  std::mutex mutex_;
  std::string buffer_;
};

SwitchSvcStreambuf s_switch_svc_buf;
} // namespace
#endif

namespace aoe2::platform {
void initialize_game_logging() {
#if defined(__ANDROID__)
  redirect_logcat();
#elif defined(__SWITCH__)
  // Route streams through atomic line-buffered SwitchSvcStreambuf to prevent
  // unbuffered operator<< token spamming svcOutputDebugString hundreds of
  // thousands of times.
  consoleDebugInit(debugDevice_SVC);
  std::cout.rdbuf(&s_switch_svc_buf);
  std::cerr.rdbuf(&s_switch_svc_buf);
  std::clog.rdbuf(&s_switch_svc_buf);
  std::cerr.unsetf(std::ios_base::unitbuf);
  std::cout.unsetf(std::ios_base::unitbuf);
  std::clog.unsetf(std::ios_base::unitbuf);
  std::set_terminate([] {
    std::cerr << "[terminate] unhandled exception\n";
    try {
      if (const auto error = std::current_exception())
        std::rethrow_exception(error);
    } catch (const std::system_error &error) {
      std::cerr << "[terminate] system_error " << error.code() << ": "
                << error.what() << '\n';
    } catch (const std::exception &error) {
      std::cerr << "[terminate] exception: " << error.what() << '\n';
    } catch (...) {
      std::cerr << "[terminate] non-standard exception\n";
    }
    std::abort();
  });
#else
  std::set_terminate([] {
    std::cerr << "[terminate] unhandled exception\n";
    try {
      if (const auto error = std::current_exception())
        std::rethrow_exception(error);
    } catch (const std::system_error &error) {
      std::cerr << "[terminate] system_error " << error.code() << ": "
                << error.what() << '\n';
    } catch (const std::exception &error) {
      std::cerr << "[terminate] exception: " << error.what() << '\n';
    } catch (...) {
      std::cerr << "[terminate] non-standard exception\n";
    }
    std::abort();
  });
#endif
}
} // namespace aoe2::platform
