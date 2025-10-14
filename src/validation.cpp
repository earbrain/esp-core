#include "earbrain/validation.hpp"

#include <algorithm>
#include <cctype>

namespace earbrain::validation {

bool is_valid_ssid(std::string_view ssid) {
  return !ssid.empty() && ssid.size() <= 32;
}

bool is_valid_passphrase(std::string_view passphrase) {
  const std::size_t len = passphrase.size();
  if (len == 0) {
    return true;
  }
  if (len >= 8 && len <= 63) {
    return true;
  }
  if (len == 64) {
    return std::all_of(passphrase.begin(), passphrase.end(), [](char ch) {
      return std::isxdigit(static_cast<unsigned char>(ch));
    });
  }
  return false;
}

} // namespace earbrain::validation
