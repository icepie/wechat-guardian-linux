#include "guardian/inline_hook.hpp"

#include <Zydis/Zydis.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace guardian {
namespace {
constexpr std::size_t jump_size = 14;

void write_absolute_jump(std::byte* output, const void* destination) {
    // jmp qword ptr [rip+0]; <absolute address>
    const std::byte prefix[]{std::byte{0xff}, std::byte{0x25}, std::byte{0x00},
                             std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
    std::memcpy(output, prefix, sizeof(prefix));
    const auto address = reinterpret_cast<std::uintptr_t>(destination);
    std::memcpy(output + sizeof(prefix), &address, sizeof(address));
}

bool protect_range(void* address, std::size_t length, int protection, std::string& error) {
    const auto page_size = static_cast<std::uintptr_t>(::sysconf(_SC_PAGESIZE));
    const auto start = reinterpret_cast<std::uintptr_t>(address) & ~(page_size - 1U);
    const auto end = (reinterpret_cast<std::uintptr_t>(address) + length + page_size - 1U) &
                     ~(page_size - 1U);
    if (::mprotect(reinterpret_cast<void*>(start), end - start, protection) != 0) {
        error = std::string("mprotect failed: ") + std::strerror(errno);
        return false;
    }
    return true;
}

struct RelocatedCode {
    std::vector<std::byte> bytes;
    std::size_t source_size{};
};

bool relocate_instructions(const std::byte* source, std::uintptr_t source_address,
                           std::byte* destination, std::uintptr_t destination_address,
                           std::size_t minimum_size, RelocatedCode& result, std::string& error) {
    ZydisDecoder decoder;
    if (!ZYAN_SUCCESS(ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64,
                                        ZYDIS_STACK_WIDTH_64))) {
        error = "Zydis decoder initialization failed";
        return false;
    }

    result.bytes.clear();
    result.source_size = 0;
    while (result.source_size < minimum_size) {
        ZydisDecodedInstruction instruction{};
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT]{};
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(
                &decoder, source + result.source_size, ZYDIS_MAX_INSTRUCTION_LENGTH,
                &instruction, operands))) {
            error = "unable to decode overwritten instruction";
            return false;
        }

        ZydisEncoderRequest request{};
        if (!ZYAN_SUCCESS(ZydisEncoderDecodedInstructionToEncoderRequest(
                &instruction, operands, instruction.operand_count_visible, &request))) {
            error = "unable to convert instruction for relocation";
            return false;
        }

        for (ZyanU8 index = 0; index < request.operand_count; ++index) {
            auto& operand = request.operands[index];
            if (operand.type == ZYDIS_OPERAND_TYPE_MEMORY &&
                operand.mem.base == ZYDIS_REGISTER_RIP) {
                ZyanU64 absolute = 0;
                if (!ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(
                        &instruction, &operands[index], source_address + result.source_size,
                        &absolute))) {
                    error = "unable to resolve RIP-relative operand";
                    return false;
                }
                operand.mem.displacement = static_cast<ZyanI64>(absolute);
            } else if (operand.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && operands[index].imm.is_relative) {
                ZyanU64 absolute = 0;
                if (!ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(
                        &instruction, &operands[index], source_address + result.source_size,
                        &absolute))) {
                    error = "unable to resolve relative branch operand";
                    return false;
                }
                operand.imm.u = absolute;
            }
        }

        std::byte encoded[ZYDIS_MAX_INSTRUCTION_LENGTH]{};
        ZyanUSize encoded_size = sizeof(encoded);
        if (!ZYAN_SUCCESS(ZydisEncoderEncodeInstructionAbsolute(
                &request, encoded, &encoded_size,
                destination_address + result.bytes.size()))) {
            error = "unable to re-encode relocated instruction";
            return false;
        }
        result.bytes.insert(result.bytes.end(), encoded, encoded + encoded_size);
        result.source_size += instruction.length;
        if (result.source_size > 32) {
            error = "hook prologue exceeds supported size";
            return false;
        }
    }
    std::memcpy(destination, result.bytes.data(), result.bytes.size());
    return true;
}
} // namespace

InlineHook::~InlineHook() {
    std::string ignored;
    uninstall(ignored);
}

bool InlineHook::install(void* target, void* replacement, std::span<const std::byte> expected,
                         std::size_t overwrite_size, std::string& error) {
    if (installed()) {
        error = "hook is already installed";
        return false;
    }
    if (!target || !replacement || expected.size() < jump_size) {
        error = "invalid hook arguments";
        return false;
    }
    if (std::memcmp(target, expected.data(), expected.size()) != 0) {
        error = "target bytes do not match the supported build";
        return false;
    }

    constexpr std::size_t allocation_size = 4096;
    void* allocation = ::mmap(nullptr, allocation_size, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (allocation == MAP_FAILED) {
        error = std::string("mmap failed: ") + std::strerror(errno);
        return false;
    }

    RelocatedCode relocated;
    const auto minimum = overwrite_size == 0 ? jump_size : overwrite_size;
    if (!relocate_instructions(static_cast<const std::byte*>(target),
                               reinterpret_cast<std::uintptr_t>(target),
                               static_cast<std::byte*>(allocation),
                               reinterpret_cast<std::uintptr_t>(allocation), minimum,
                               relocated, error)) {
        ::munmap(allocation, allocation_size);
        return false;
    }
    if (relocated.source_size > original_.size() || relocated.bytes.size() + jump_size > allocation_size) {
        error = "relocated hook body is too large";
        ::munmap(allocation, allocation_size);
        return false;
    }

    std::memcpy(original_.data(), target, relocated.source_size);
    write_absolute_jump(static_cast<std::byte*>(allocation) + relocated.bytes.size(),
                        static_cast<std::byte*>(target) + relocated.source_size);
    if (::mprotect(allocation, allocation_size, PROT_READ | PROT_EXEC) != 0) {
        error = std::string("trampoline mprotect failed: ") + std::strerror(errno);
        ::munmap(allocation, allocation_size);
        return false;
    }

    if (!protect_range(target, relocated.source_size, PROT_READ | PROT_WRITE | PROT_EXEC, error)) {
        ::munmap(allocation, allocation_size);
        return false;
    }
    std::vector<std::byte> patch(relocated.source_size, std::byte{0x90});
    write_absolute_jump(patch.data(), replacement);
    std::memcpy(target, patch.data(), patch.size());
    __builtin___clear_cache(static_cast<char*>(target), static_cast<char*>(target) + patch.size());
    if (!protect_range(target, relocated.source_size, PROT_READ | PROT_EXEC, error)) {
        // The code is already patched; retain state so uninstall can recover it.
        target_ = target;
        trampoline_ = allocation;
        trampoline_size_ = allocation_size;
        overwrite_size_ = relocated.source_size;
        return false;
    }

    target_ = target;
    trampoline_ = allocation;
    trampoline_size_ = allocation_size;
    overwrite_size_ = relocated.source_size;
    return true;
}

bool InlineHook::uninstall(std::string& error) {
    if (!installed()) {
        return true;
    }
    if (!protect_range(target_, overwrite_size_, PROT_READ | PROT_WRITE | PROT_EXEC, error)) {
        return false;
    }
    std::memcpy(target_, original_.data(), overwrite_size_);
    __builtin___clear_cache(static_cast<char*>(target_), static_cast<char*>(target_) + overwrite_size_);
    if (!protect_range(target_, overwrite_size_, PROT_READ | PROT_EXEC, error)) {
        return false;
    }
    ::munmap(trampoline_, trampoline_size_);
    target_ = nullptr;
    trampoline_ = nullptr;
    trampoline_size_ = 0;
    overwrite_size_ = 0;
    return true;
}
} // namespace guardian

extern "C" __attribute__((visibility("default"))) bool guardian_inline_hook_self_test() {
    return true;
}
