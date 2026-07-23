#include "antirecall/build_id.hpp"
#include <array>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "/opt/wechat/wechat";
    const auto id = antirecall::read_gnu_build_id(path);
    if (!id) {
        std::cerr << "unable to read GNU Build ID from " << path << '\n';
        return 1;
    }
    constexpr std::string_view supported = "be2d6b53c50f5cf754b00a8001e5ee88980fdeeb";
    constexpr std::streamoff parse_revoke_file_offset = 0x497a4e0;
    constexpr std::array<unsigned char, 32> expected{
        0x55,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x88,0x02,0x00,
        0x00,0x49,0x89,0xd6,0x49,0x89,0xf7,0x48,0x89,0xfb,0x0f,0x28,0x05,0x7f,0xe1,0x07,
    };
    std::cout << "Build ID: " << *id << '\n';
    if (*id != supported) {
        std::cerr << "unsupported Build ID\n";
        return 2;
    }
    std::ifstream input(path, std::ios::binary);
    std::array<unsigned char, expected.size()> actual{};
    input.seekg(parse_revoke_file_offset);
    input.read(reinterpret_cast<char*>(actual.data()), actual.size());
    if (!input || actual != expected) {
        std::cerr << "parseRevokeXML entry bytes mismatch\n";
        return 3;
    }
    std::cout << "parseRevokeXML: RVA 0x497b4e0, bytes verified\n";
    std::cout << "message layout: newmsgid +0x148, replacemsg +0x150\n";
    return 0;
}
