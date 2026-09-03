#include "guardian/build_id.hpp"
#include "guardian/inline_hook.hpp"
#include "guardian/process.hpp"

#include <algorithm>
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
constexpr std::uintptr_t image_resource_dispatch_rva = 0x6aaaf70;
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
constexpr std::array<std::byte, 32> expected_image_resource_dispatch_entry{
    std::byte{0x55}, std::byte{0x41}, std::byte{0x57}, std::byte{0x41},
    std::byte{0x56}, std::byte{0x41}, std::byte{0x54}, std::byte{0x53},
    std::byte{0x48}, std::byte{0x81}, std::byte{0xec}, std::byte{0xb0},
    std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x48},
    std::byte{0x89}, std::byte{0xf3}, std::byte{0x0f}, std::byte{0xb6},
    std::byte{0x86}, std::byte{0x88}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0xa8}, std::byte{0x01}, std::byte{0x74},
    std::byte{0x11}, std::byte{0x48}, std::byte{0x8b}, std::byte{0x83},
};

// Linux 4.1.1.8 uses this three-argument parseRevokeXML body. Keeping a fourth
// opaque register argument in the prototype is ABI-safe and preserves RCX for
// builds where the compiler happens to pass extra context.
using ParseRevokeXml = bool (*)(void*, void*, void*, std::uintptr_t);
using DispatchImageResource = int (*)(void*, void*);
guardian::InlineHook revoke_hook;
guardian::InlineHook image_resource_hook;
ParseRevokeXml original_parse_revoke_xml = nullptr;
DispatchImageResource original_dispatch_image_resource = nullptr;
std::atomic<unsigned long> blocked_count{0};
std::atomic<unsigned long> traced_image_count{0};
std::atomic<unsigned long> queued_original_count{0};
bool probe_only = false;
bool trace_images = false;
bool auto_original_images = false;

void log_line(const char* level, const std::string& message) {
    std::fprintf(stderr, "[wechat-guardian] %s: %s\n", level, message.c_str());
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

bool replace_suffix(std::string& value, std::string_view from, std::string_view to) {
    if (!value.ends_with(from)) return false;
    value.replace(value.size() - from.size(), from.size(), to);
    return true;
}

// Writes a borrowed libc++ long-string view. Image dispatch copies these fields
// synchronously into its request; the stack-backed source remains alive through
// that call and is never handed to a destructor.
void set_borrowed_wechat_string(std::byte* destination, const std::string& value) {
    std::memset(destination, 0, 24);
    destination[0] = std::byte{1};
    const auto size = value.size();
    const auto* data = value.data();
    std::memcpy(destination + 8, &size, sizeof(size));
    std::memcpy(destination + 16, &data, sizeof(data));
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

// The layout is captured from a live 4.1.1.8 client. In auto-original mode,
// dispatching a type-2 mid-image resource produces one type-1 full-image request.
// Other resource types retain the observation-only path.
int hooked_dispatch_image_resource(void* service, void* resource) {
    if (!resource) return original_dispatch_image_resource(service, resource);

    const auto* bytes = static_cast<const std::byte*>(resource);
    const auto field = [bytes](std::ptrdiff_t offset) { return read_wechat_string(bytes + offset); };
    const auto u32 = [bytes](std::ptrdiff_t offset) {
        std::uint32_t value = 0;
        std::memcpy(&value, bytes + offset, sizeof(value));
        return value;
    };
    const auto type = u32(0xa0);
    const auto count = ++traced_image_count;
    if (count <= 20 || count % 100 == 0) {
        std::uint64_t payload_size = 0;
        std::memcpy(&payload_size, bytes + 0x148, sizeof(payload_size));
        log_line("image-trace", "resource=" + std::to_string(count) +
                                    " type=" + std::to_string(type) +
                                    " subtype=" + std::to_string(u32(0xa4)) +
                                    " state=" + std::to_string(u32(0xa8)) +
                                    " size=" + std::to_string(payload_size) +
                                    " id=" + field(0x40) + " key=" + field(0x58) +
                                    " aeskey=" + field(0x70) + " path=" + field(0x88));
    }

    const bool request_full_image = auto_original_images && type == 2;
    std::array<std::byte, 0x470> full_resource{};
    bool full_request_ready = false;
    std::string path;
    if (request_full_image) {
        // The dispatcher is allowed to recycle its input. Build the complete
        // follow-up request while the delivered resource is still live.
        auto id = field(0x40);
        path = field(0x88);
        if (replace_suffix(id, "_2", "_0") &&
            replace_suffix(path, "_mid_temp", "_hd_temp")) {
            // The client owns every non-string field. The copied request switches
            // only from its delivered mid-image to its full-image counterpart.
            std::memcpy(full_resource.data(), resource, full_resource.size());
            const std::uint32_t full_type = 1;
            std::memcpy(full_resource.data() + 0xa0, &full_type, sizeof(full_type));
            set_borrowed_wechat_string(full_resource.data() + 0x40, id);
            set_borrowed_wechat_string(full_resource.data() + 0x88, path);
            full_request_ready = true;
        } else {
            log_line("original-image", "skipped resource with unrecognized mid-image naming");
        }
    }

    const int result = original_dispatch_image_resource(service, resource);
    if (!full_request_ready) return result;

    const int full_result = original_dispatch_image_resource(service, full_resource.data());
    const auto queued = ++queued_original_count;
    log_line("original-image", "queued full resource=" + std::to_string(queued) +
                               " result=" + std::to_string(full_result) + " path=" + path);
    return result;
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
    probe_only = std::getenv("WECHAT_GUARDIAN_PROBE_ONLY") != nullptr;
    auto_original_images = std::getenv("WECHAT_GUARDIAN_AUTO_ORIGINAL_IMAGES") != nullptr;
    trace_images = std::getenv("WECHAT_GUARDIAN_TRACE_IMAGES") != nullptr || auto_original_images;
    const auto id = guardian::read_gnu_build_id("/proc/self/exe");
    if (!id) {
        log_line("disabled", "cannot read executable GNU Build ID");
        return;
    }
    if (*id != supported_build_id) {
        // LD_PRELOAD may be inherited by helper processes; silence those unless
        // explicit diagnostics were requested.
        if (std::getenv("WECHAT_GUARDIAN_VERBOSE")) {
            log_line("disabled", "unsupported executable Build ID " + *id);
        }
        return;
    }
    const auto bias = guardian::find_module_load_bias("/opt/wechat/wechat");
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

    if (trace_images) {
        auto* image_target = reinterpret_cast<void*>(*bias + image_resource_dispatch_rva);
        if (!image_resource_hook.install(image_target,
                                         reinterpret_cast<void*>(&hooked_dispatch_image_resource),
                                         expected_image_resource_dispatch_entry, 0, error)) {
            log_line("disabled", "image trace hook validation/install failed: " + error);
            revoke_hook.uninstall(error);
            return;
        }
        original_dispatch_image_resource =
            reinterpret_cast<DispatchImageResource>(image_resource_hook.trampoline());
    }
    log_line("enabled", std::string("WeChat 4.1.1.8 hook installed (") +
                            (auto_original_images ? "auto-original" :
                             (trace_images ? "image-trace" : (probe_only ? "probe-only" : "blocking"))) + ")");
}
} // namespace

__attribute__((constructor)) static void wechat_guardian_initialize() {
    initialize();
}

extern "C" __attribute__((visibility("default"))) unsigned long
wechat_guardian_blocked_count() {
    return blocked_count.load();
}

extern "C" __attribute__((visibility("default"))) unsigned long
wechat_guardian_traced_image_count() {
    return traced_image_count.load();
}

extern "C" __attribute__((visibility("default"))) unsigned long
wechat_guardian_queued_original_count() {
    return queued_original_count.load();
}
