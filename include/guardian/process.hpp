#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace guardian {
std::optional<std::uintptr_t> find_module_load_bias(std::string_view module_path);
}
