#pragma once

#include <string_view>

namespace earbrain::validation {

bool is_valid_ssid(std::string_view ssid);
bool is_valid_passphrase(std::string_view passphrase);

} // namespace earbrain::validation
