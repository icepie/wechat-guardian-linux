#include "guardian/image_resource.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace guardian {
namespace {

std::string read_wechat_string(const void* address) {
    if (!address) return {};
    const auto* raw = static_cast<const unsigned char*>(address);
    if ((raw[0] & 1U) == 0) {
        const std::size_t size = raw[0] >> 1U;
        if (size > 22) return {};
        return {reinterpret_cast<const char*>(raw + 1), size};
    }
    std::size_t size = 0;
    const char* data = nullptr;
    std::memcpy(&size, raw + 8, sizeof(size));
    std::memcpy(&data, raw + 16, sizeof(data));
    if (!data || size > 4096) return {};
    return {data, size};
}

bool replace_suffix(std::string& value, std::string_view from, std::string_view to) {
    if (!value.ends_with(from)) return false;
    value.replace(value.size() - from.size(), from.size(), to);
    return true;
}

bool upgrade_path(std::string& path, std::uint32_t type) {
    const auto marker = type == 2 ? std::string_view{"_mid"} : std::string_view{"_thumb"};
    const auto position = path.rfind(marker);
    if (position == std::string::npos) return false;
    // Keep the foreign libc++ string allocation unchanged. WeChat accepts the
    // destination filename verbatim; the CDN resource type controls image size.
    path.replace(position, marker.size(), type == 2 ? "_hd_" : "_hd___");
    return true;
}

bool assign_wechat_string(void* address, std::string_view value) {
    const auto old_value = read_wechat_string(address);
    if (old_value.size() != value.size()) return false;
    auto* bytes = static_cast<std::byte*>(address);
    const auto short_string = (std::to_integer<unsigned char>(*bytes) & 1U) == 0;
    char* data = nullptr;
    if (short_string) {
        data = reinterpret_cast<char*>(bytes + 1);
    } else {
        std::memcpy(&data, bytes + 16, sizeof(data));
    }
    if (!data) return false;
    std::memcpy(data, value.data(), value.size());
    return true;
}

} // namespace

bool upgrade_image_resource(void* resource, std::string& upgraded_path) {
    if (!resource) return false;
    auto* bytes = static_cast<std::byte*>(resource);
    std::uint32_t type = 0;
    std::memcpy(&type, bytes + 0xa0, sizeof(type));
    if (type != 2 && type != 3) return false;

    auto id = read_wechat_string(bytes + 0x40);
    auto path = read_wechat_string(bytes + 0x88);
    const auto id_suffix = type == 2 ? std::string_view{"_2"} : std::string_view{"_1"};
    if (!replace_suffix(id, id_suffix, "_0") || !upgrade_path(path, type)) return false;
    if (!assign_wechat_string(bytes + 0x40, id) ||
        !assign_wechat_string(bytes + 0x88, path)) {
        return false;
    }

    constexpr std::uint32_t full_type = 1;
    std::memcpy(bytes + 0xa0, &full_type, sizeof(full_type));
    upgraded_path = std::move(path);
    return true;
}

} // namespace guardian
