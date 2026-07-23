#include "antirecall/pattern.hpp"

#include <cctype>
#include <sstream>
#include <string>

namespace antirecall {
namespace {
std::optional<std::byte> parse_hex_byte(std::string_view token) {
    if (token.size() != 2 || !std::isxdigit(static_cast<unsigned char>(token[0])) ||
        !std::isxdigit(static_cast<unsigned char>(token[1]))) {
        return std::nullopt;
    }
    unsigned int value = 0;
    std::istringstream input{std::string(token)};
    input >> std::hex >> value;
    if (!input || value > 0xff) {
        return std::nullopt;
    }
    return static_cast<std::byte>(value);
}
} // namespace

std::optional<BytePattern> BytePattern::parse(std::string_view text) {
    std::istringstream input{std::string(text)};
    std::string token;
    std::vector<Token> tokens;
    while (input >> token) {
        if (token == "?" || token == "??") {
            tokens.push_back(Token{std::byte{0}, true});
            continue;
        }
        const auto value = parse_hex_byte(token);
        if (!value) {
            return std::nullopt;
        }
        tokens.push_back(Token{*value, false});
    }
    if (tokens.empty()) {
        return std::nullopt;
    }
    return BytePattern{std::move(tokens)};
}

std::size_t BytePattern::size() const noexcept { return tokens_.size(); }

std::vector<std::size_t> BytePattern::find_all(std::span<const std::byte> bytes) const {
    std::vector<std::size_t> matches;
    if (tokens_.size() > bytes.size()) {
        return matches;
    }
    for (std::size_t start = 0; start + tokens_.size() <= bytes.size(); ++start) {
        bool match = true;
        for (std::size_t index = 0; index < tokens_.size(); ++index) {
            if (!tokens_[index].wildcard && bytes[start + index] != tokens_[index].value) {
                match = false;
                break;
            }
        }
        if (match) {
            matches.push_back(start);
        }
    }
    return matches;
}
} // namespace antirecall
