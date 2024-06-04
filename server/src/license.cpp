#include "license.hpp"

#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "spdlog/spdlog.h"

namespace metalware::license {
namespace {
#if _WIN32
static std::string get_license_location() {
  char* appdata = nullptr;
  size_t len = 0;
  _dupenv_s(&appdata, &len, "APPDATA");
  std::string license_location = appdata;
  license_location += "\\metalware-license.txt";
  return license_location;
}
#elif __APPLE__
static std::string get_license_location() {
  return "/tmp/metalware-license.txt";
}
#else
static std::string get_license_location() {
  return ".metalware-license.txt";
}
#endif

std::string cached_license;

std::string generate_random_uuid() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> distrib(0, 15);

  std::stringstream ss;
  for (int i = 0; i < 32; ++i) {
    if (i == 8 || i == 12 || i == 16 || i == 20)
      ss << '-';
    ss << std::hex << distrib(gen);
  }
  return ss.str();
}
}  // namespace

std::variant<std::monostate, LicenseError> set_license_key(const std::string& license_key) {
  if (!is_valid(license_key)) {
    return LicenseError{.type = LicenseErrorType::InvalidKey, .message = "Invalid license key"};
  }

  std::ofstream license_file(get_license_location());

  if (!license_file) {
    return LicenseError{.type = LicenseErrorType::FailedToWrite,
        .message = fmt::format("Failed to write license key to {}", get_license_location())};
  }

  license_file << license_key;
  cached_license = license_key;
  license_file.close();

  return std::monostate{};
}

const std::string& get_cached_license() {
  return cached_license;
}

bool read_license_file() {
  std::ifstream license_file(get_license_location());
  if (!license_file) {
    return false;
  }

  license_file >> cached_license;
  return true;
}

#ifdef DEV
static int total = 0;
static int valid = 0;

std::string generate_valid_uuid() {
  std::string uuid;
  do {
    uuid = generate_random_uuid();
    total++;
  } while (!is_valid(uuid));
  valid++;
  return uuid;
}

void report_statistics() {
  spdlog::info("Total UUIDs generated: {}", total);
  spdlog::info("Valid UUIDs generated: {}", valid);
}

void gen() {
  // Do this on multiple threads
  std::vector<std::thread> threads;
  constexpr auto num_threads = 10;
  threads.reserve(num_threads);
  std::ofstream lout("metalware-licenses.txt");
  std::mutex mutex;

  lout << "Valid UUIDs:\n";
  for (int i = 0; i < num_threads; i++) {
    threads.emplace_back([&num_threads, &lout, &mutex]() {
      for (int i = 0; i < 1000 / num_threads; i++) {
        std::string valid_uuid = metalware::license::generate_valid_uuid();
        std::cout << "Generated valid UUID: " << valid_uuid << std::endl;
        {
          std::scoped_lock lock(mutex);
          lout << valid_uuid << std::endl;
          lout.flush();
        }
      }
    });
  }
  // Wait for all threads to finish
  for (auto& thread : threads) {
    thread.join();
  }

  metalware::license::report_statistics();
}
#endif
}  // namespace metalware::license
