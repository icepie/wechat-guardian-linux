#include "guardian/build_id.hpp"
#include "guardian/pattern.hpp"
#include "guardian/image_resource.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <cstring>

namespace {
int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void write_long_wechat_string(std::byte* destination, std::string& storage) {
    destination[0] = std::byte{1};
    const auto size = storage.size();
    auto* data = storage.data();
    std::memcpy(destination + 8, &size, sizeof(size));
    std::memcpy(destination + 16, &data, sizeof(data));
}

void test_image_resource_upgrade(std::uint32_t type, std::string id, std::string path,
                                 const std::string& expected_id,
                                 const std::string& expected_path) {
    std::array<std::byte, 0xb0> resource{};
    write_long_wechat_string(resource.data() + 0x40, id);
    write_long_wechat_string(resource.data() + 0x88, path);
    std::memcpy(resource.data() + 0xa0, &type, sizeof(type));

    std::string upgraded_path;
    expect(guardian::upgrade_image_resource(resource.data(), upgraded_path),
           "image resource upgrades");
    std::uint32_t upgraded_type = 0;
    std::memcpy(&upgraded_type, resource.data() + 0xa0, sizeof(upgraded_type));
    expect(id == expected_id, "image resource id becomes full image id");
    expect(path == expected_path, "image destination path becomes full image path: got " + path +
                                     ", expected " + expected_path);
    expect(upgraded_path == expected_path, "upgraded path is returned for diagnostics: got " +
                                              upgraded_path + ", expected " + expected_path);
}
}

int main() {
    using guardian::BytePattern;

    const auto pattern = BytePattern::parse("48 8B ?? ?? E8 ? ? ? ?");
    expect(pattern.has_value(), "valid wildcard pattern parses");
    expect(pattern && pattern->size() == 9, "pattern length is correct");

    const std::vector<std::byte> bytes{
        std::byte{0x90}, std::byte{0x48}, std::byte{0x8B}, std::byte{0x12},
        std::byte{0x34}, std::byte{0xE8}, std::byte{0x01}, std::byte{0x02},
        std::byte{0x03}, std::byte{0x04}, std::byte{0x90}
    };
    if (pattern) {
        const auto matches = pattern->find_all(bytes);
        expect(matches.size() == 1 && matches[0] == 1, "wildcard scanner finds exact match");
    }
    expect(!BytePattern::parse("48 ZZ").has_value(), "invalid hex pattern is rejected");

    const auto id = guardian::read_gnu_build_id("/opt/wechat/wechat");
    expect(id.has_value(), "reads GNU Build ID from current WeChat ELF");
    expect(id && *id == "be2d6b53c50f5cf754b00a8001e5ee88980fdeeb",
           "current WeChat Build ID matches supported build");

    test_image_resource_upgrade(
        3, "50511185562@chatroom_1788502049_4073_1",
        "/cache/ImageTemp/4073_1788502049_thumb_temp",
        "50511185562@chatroom_1788502049_4073_0",
        "/cache/ImageTemp/4073_1788502049_hd____temp");
    test_image_resource_upgrade(
        2, "50511185562@chatroom_1788502049_4073_2",
        "/cache/ImageTemp/4073_1788502049_mid_temp",
        "50511185562@chatroom_1788502049_4073_0",
        "/cache/ImageTemp/4073_1788502049_hd__temp");
    std::array<std::byte, 0xb0> unrelated_resource{};
    const std::uint32_t full_type = 1;
    std::memcpy(unrelated_resource.data() + 0xa0, &full_type, sizeof(full_type));
    std::string ignored_path;
    expect(!guardian::upgrade_image_resource(unrelated_resource.data(), ignored_path),
           "full image resources are not rewritten");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "all tests passed\n";
    return EXIT_SUCCESS;
}
