#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace guardian {

class BytePattern {
public:
    static std::optional<BytePattern> parse(std::string_view text);
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::vector<std::size_t> find_all(std::span<const std::byte> bytes) const;

private:
    struct Token {
        std::byte value{};
        bool wildcard{false};
    };
    explicit BytePattern(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}
    std::vector<Token> tokens_;
};

} // namespace guardian
