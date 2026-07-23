#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace antirecall {
std::optional<std::string> read_gnu_build_id(std::string_view elf_path);
}
