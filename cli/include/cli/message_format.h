#pragma once

#include <string>
#include <vector>

namespace mm {

struct FormatResult {
    bool ok = false;
    std::string text;
    std::string error;
};

FormatResult format_message(const std::string& type_name, const std::string& bytes);
bool is_supported_message_type(const std::string& type_name);
std::vector<std::string> supported_message_types();

}  // namespace mm
