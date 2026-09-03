#include "guardian/build_id.hpp"
#include "guardian/pattern.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
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

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "all tests passed\n";
    return EXIT_SUCCESS;
}
