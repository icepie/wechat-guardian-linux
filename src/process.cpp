#include "guardian/process.hpp"

#include <charconv>
#include <fstream>
#include <sstream>
#include <string>

namespace guardian {
std::optional<std::uintptr_t> find_module_load_bias(std::string_view module_path) {
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        if (line.find(module_path) == std::string::npos) {
            continue;
        }
        std::istringstream input(line);
        std::string range, permissions, offset_text, device, inode;
        input >> range >> permissions >> offset_text >> device >> inode;
        if (!input || permissions.size() < 3 || permissions[0] != 'r') {
            continue;
        }
        const auto dash = range.find('-');
        if (dash == std::string::npos) {
            continue;
        }
        std::uintptr_t start = 0;
        std::uintptr_t offset = 0;
        if (std::from_chars(range.data(), range.data() + dash, start, 16).ec != std::errc{} ||
            std::from_chars(offset_text.data(), offset_text.data() + offset_text.size(), offset, 16).ec != std::errc{}) {
            continue;
        }
        if (start >= offset) {
            return start - offset;
        }
    }
    return std::nullopt;
}
} // namespace guardian
