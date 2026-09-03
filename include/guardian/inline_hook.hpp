#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace guardian {

class InlineHook {
public:
    InlineHook() = default;
    InlineHook(const InlineHook&) = delete;
    InlineHook& operator=(const InlineHook&) = delete;
    ~InlineHook();

    bool install(void* target, void* replacement, std::span<const std::byte> expected,
                 std::size_t overwrite_size, std::string& error);
    bool uninstall(std::string& error);
    [[nodiscard]] void* trampoline() const noexcept { return trampoline_; }
    [[nodiscard]] bool installed() const noexcept { return target_ != nullptr; }

private:
    void* target_{nullptr};
    void* trampoline_{nullptr};
    std::size_t trampoline_size_{0};
    std::size_t overwrite_size_{0};
    std::array<std::byte, 32> original_{};
};

} // namespace guardian
