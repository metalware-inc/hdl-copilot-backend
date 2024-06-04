#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/spdlog.h"

#if defined(_WIN32)
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#else
#include <unistd.h>  // for getpid()

#include <csignal>
#define CLOSE_SOCKET close
#endif

#include "languageclient.hpp"
#include "shared.hpp"

// #define IGNORE_ALL_DIAGNOSTIC_FILTERS
using namespace metalware;

#if defined(__linux__) || defined(__APPLE__)
// DEBUG
void signal_handler(int signo) {
  if (signo == SIGSEGV) {
    spdlog::error("Received SIGSEGV");
  } else if (signo == SIGABRT) {
    spdlog::error("Received SIGABRT");
  } else if (signo == SIGTERM) {
    spdlog::error("Received SIGTERM");
  } else if (signo == SIGPIPE) {
    spdlog::error("Received SIGPIPE");
  } else {
    spdlog::error("Received signal: {}", signo);
  }
  exit(1);
}
#endif

// HELPERS
int main(int argc, char *argv[]) {
  auto pid = getpid();
#if defined(_WIN32)
  // Make dir if it does not exist
  if (!std::filesystem::exists("C:\\Logs")) {
    std::filesystem::create_directory("C:\\Logs");
  }
  const std::string log_file_name = "C:\\Logs\\hdl-server-" + std::to_string(pid) + ".log";
#else
  const std::string log_file_name = "/tmp/hdl-server-" + std::to_string(pid) + ".log";
#endif
  auto file_logger = spdlog::basic_logger_mt("file_logger", log_file_name);
  spdlog::set_default_logger(file_logger);

  // add signals
#if defined(__linux__) || defined(__APPLE__)
  signal(SIGSEGV, signal_handler);
  signal(SIGABRT, signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGPIPE, signal_handler);
#endif

  spdlog::flush_on(spdlog::level::info);
  spdlog::info("Starting hdl-server version: {}", VERSION);

  std::shared_ptr<LanguageClient> client = std::make_shared<LanguageClient>();
  client->setup();
  client->handle_communication();
  return 0;
}
