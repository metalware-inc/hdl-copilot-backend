#pragma once

#include <algorithm>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>
#include <cmath>
#include <variant>

namespace metalware::license {
  bool read_license_file();
  struct LicenseError;
  std::variant<std::monostate, LicenseError> set_license_key(const std::string& license_key);

  const std::string& get_cached_license();

  enum struct LicenseErrorType {
    InvalidKey,
    FailedToWrite
  };

  struct LicenseError {
    LicenseErrorType type;
    std::string message;
  };

  static constexpr int mgk = 0x59E;
  inline bool is_valid(const std::string& uuid_str) {
    // Extract numbers from the UUID and store them in a vector
    std::vector<int> numbers;
    for (char c : uuid_str) {
      if (std::isdigit(c)) {
        numbers.push_back(c - '0');
      }
    }

    // Perform some custom operations on the numbers
    int sum = std::accumulate(numbers.begin(), numbers.end(), 0);
    int product = std::accumulate(numbers.begin(), numbers.end(), 1, std::multiplies<int>());
    int xor_result = 0;
    for (int num : numbers) {
      xor_result ^= num;
    }

    // Apply non-linear transformations
    int transformed_sum = static_cast<int>(std::pow(sum, 3)) % 16384;
    int transformed_product = static_cast<int>(std::log(product + 1)) % 16384;
    int transformed_xor = (xor_result << 2) % 16384;

    // Combine the transformed results and perform a final check
    int final_value = transformed_sum ^ transformed_product ^ transformed_xor;
    return final_value == mgk;
  }

  inline bool is_valid() {
    if (is_valid(get_cached_license())) {
      return true;
    }
    return false;
  }
}  // namespace metalware::license
