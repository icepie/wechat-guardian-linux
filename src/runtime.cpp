#include "antirecall/build_id.hpp"
#include "antirecall/inline_hook.hpp"
#include "antirecall/process.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {
constexpr std::string_view supported_build_id = "be2d6b53c50f5cf754b00a8001e5ee88980fdeeb";
constexpr std::uintptr_t parse_revoke_xml_rva = 0x497b4e0;
constexpr std::ptrdiff_t new_msg_id_offset = 0x148;
constexpr std::ptrdiff_t replace_msg_offset = 0x150;
constexpr std::array<std::byte, 32> expected_entry{
    std::byte{0x55}, std::byte{0x41}, std::byte{0x57}, std::byte{0x41},
    std::byte{0x56}, std::byte{0x41}, std::byte{0x55}, std::byte{0x41},
    std::byte{0x54}, std::byte{0x53}, std::byte{0x48}, std::byte{0x81},
    std::byte{0xec}, std::byte{0x88}, std::byte{0x02}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x49}, std::byte{0x89}, std::byte{0xd6},
    std::byte{0x49}, std::byte{0x89}, std::byte{0xf7}, std::byte{0x48},
    std::byte{0x89}, std::byte{0xfb}, std::byte{0x0f}, std::byte{0x28},
    std::byte{0x05}, std::byte{0x7f}, std::byte{0xe1}, std::byte{0x07},
};

// Linux 4.1.1.8 uses this three-argument parseRevokeXML body. Keeping a fourth
// opaque register argument in the prototype is ABI-safe and preserves RCX for
// builds where the compiler happens to pass extra context.
using ParseRevokeXml = bool (*)(void*, void*, void*, std::uintptr_t);
antirecall::InlineHook revoke_hook;
ParseRevokeXml original_parse_revoke_xml = nullptr;
std::atomic<unsigned long> blocked_count{0};
bool probe_only = false;

void log_line(const char* level, const std::string& message) {
    std::fprintf(stderr, "[wechat-antirecall] %s: %s\n", level, message.c_str());
    std::fflush(stderr);
}

// WeChat's Linux binary uses libc++'s 24-byte string representation here:
// short strings store size*2 in byte 0 and data at +1; long strings store the
// data pointer at +16. We only need a bounded, best-effort read for detecting
// the local user's own recall. Unknown layouts deliberately return empty.
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

bool looks_like_self_recall(std::string_view tip) {
    constexpr std::string_view markers[]{
        "你撤回了一条消息", "你撤回了", "You recalled", "You deleted a message",
    };
    for (const auto marker : markers) {
        if (tip.find(marker) != std::string_view::npos) return true;
    }
    return false;
}

bool hooked_parse_revoke_xml(void* message, void* xml, void* flag, std::uintptr_t context) {
    const bool result = original_parse_revoke_xml(message, xml, flag, context);
    if (!result || !message) return result;

    const auto raw_xml = read_wechat_string(xml);
    if (raw_xml.find("<revokemsg>") == std::string::npos &&
        raw_xml.find("<revokemsg ") == std::string::npos) {
        return result;
    }

    auto* bytes = static_cast<std::byte*>(message);
    auto* new_msg_id = reinterpret_cast<std::uint64_t*>(bytes + new_msg_id_offset);
    const auto tip = read_wechat_string(bytes + replace_msg_offset);

    // Preserve native behavior for the local user's own recall when the rendered
    // tip or raw revoke XML makes that distinction available. For remote recalls,
    // retain the original message by clearing the target id after XML parsing.
    if (!looks_like_self_recall(tip) && !looks_like_self_recall(raw_xml) && *new_msg_id != 0) {
        if (!probe_only) {
            *new_msg_id = 0;
        }
        const auto count = ++blocked_count;
        if (count <= 20 || count % 100 == 0) {
            const auto action = probe_only ? "observed" : "blocked";
            log_line("info", std::string(action) + " a remote recall (count=" +
                                 std::to_string(count) + ")");
        }
    }
    return result;
}

void initialize() {
    probe_only = std::getenv("WECHAT_ANTI_RECALL_PROBE_ONLY") != nullptr;
    const auto id = antirecall::read_gnu_build_id("/proc/self/exe");
    if (!id) {
        log_line("disabled", "cannot read executable GNU Build ID");
        return;
    }
    if (*id != supported_build_id) {
        // LD_PRELOAD may be inherited by helper processes; silence those unless
        // explicit diagnostics were requested.
        if (std::getenv("WECHAT_ANTI_RECALL_VERBOSE")) {
            log_line("disabled", "unsupported executable Build ID " + *id);
        }
        return;
    }
    const auto bias = antirecall::find_module_load_bias("/opt/wechat/wechat");
    if (!bias) {
        log_line("disabled", "cannot locate /opt/wechat/wechat load bias");
        return;
    }
    auto* target = reinterpret_cast<void*>(*bias + parse_revoke_xml_rva);
    std::string error;
    if (!revoke_hook.install(target, reinterpret_cast<void*>(&hooked_parse_revoke_xml),
                             expected_entry, 0, error)) {
        log_line("disabled", "hook validation/install failed: " + error);
        return;
    }
    original_parse_revoke_xml = reinterpret_cast<ParseRevokeXml>(revoke_hook.trampoline());
    log_line("enabled", std::string("WeChat 4.1.1.8 hook installed (") +
                            (probe_only ? "probe-only" : "blocking") + ")");
}
} // namespace

__attribute__((constructor)) static void wechat_antirecall_initialize() {
    initialize();
}

extern "C" __attribute__((visibility("default"))) unsigned long
wechat_antirecall_blocked_count() {
    return blocked_count.load();
}
