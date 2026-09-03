#include "guardian/inline_hook.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
using Function = int (*)(int);
guardian::InlineHook hook;
Function original = nullptr;

__attribute__((noinline)) int target(int value) {
    asm volatile("" ::: "memory");
    return value + 7;
}

__attribute__((noinline)) int replacement(int value) {
    return original(value) * 2;
}
}

int main() {
    std::array<std::byte, 16> expected{};
    __builtin_memcpy(expected.data(), reinterpret_cast<const void*>(&target), expected.size());
    std::string error;
    if (!hook.install(reinterpret_cast<void*>(&target), reinterpret_cast<void*>(&replacement),
                      expected, 0, error)) {
        std::cerr << "install failed: " << error << '\n';
        return EXIT_FAILURE;
    }
    original = reinterpret_cast<Function>(hook.trampoline());
    if (target(5) != 24) {
        std::cerr << "hook result mismatch\n";
        return EXIT_FAILURE;
    }
    if (!hook.uninstall(error)) {
        std::cerr << "uninstall failed: " << error << '\n';
        return EXIT_FAILURE;
    }
    if (target(5) != 12) {
        std::cerr << "unhook result mismatch\n";
        return EXIT_FAILURE;
    }
    std::cout << "inline hook test passed\n";
}
