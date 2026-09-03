#include "guardian/build_id.hpp"

#include <elf.h>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace guardian {
namespace {
template <typename T>
bool read_at(std::ifstream& input, std::streamoff offset, T& value) {
    input.seekg(offset);
    input.read(reinterpret_cast<char*>(&value), sizeof(value));
    return static_cast<bool>(input);
}

std::size_t align4(std::size_t value) { return (value + 3U) & ~std::size_t{3U}; }
} // namespace

std::optional<std::string> read_gnu_build_id(std::string_view elf_path) {
    std::ifstream input(std::string(elf_path), std::ios::binary);
    Elf64_Ehdr header{};
    if (!input.read(reinterpret_cast<char*>(&header), sizeof(header))) {
        return std::nullopt;
    }
    if (header.e_ident[EI_MAG0] != ELFMAG0 || header.e_ident[EI_MAG1] != ELFMAG1 ||
        header.e_ident[EI_MAG2] != ELFMAG2 || header.e_ident[EI_MAG3] != ELFMAG3 ||
        header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_ident[EI_DATA] != ELFDATA2LSB) {
        return std::nullopt;
    }

    for (Elf64_Half index = 0; index < header.e_phnum; ++index) {
        Elf64_Phdr phdr{};
        if (!read_at(input, static_cast<std::streamoff>(header.e_phoff) +
                              static_cast<std::streamoff>(index) * header.e_phentsize, phdr)) {
            return std::nullopt;
        }
        if (phdr.p_type != PT_NOTE || phdr.p_filesz == 0) {
            continue;
        }
        std::vector<unsigned char> notes(phdr.p_filesz);
        input.seekg(phdr.p_offset);
        input.read(reinterpret_cast<char*>(notes.data()), static_cast<std::streamsize>(notes.size()));
        if (!input) {
            return std::nullopt;
        }
        std::size_t cursor = 0;
        while (cursor + sizeof(Elf64_Nhdr) <= notes.size()) {
            Elf64_Nhdr note{};
            std::memcpy(&note, notes.data() + cursor, sizeof(note));
            cursor += sizeof(note);
            const auto name_size = align4(note.n_namesz);
            const auto desc_size = align4(note.n_descsz);
            if (cursor + name_size + desc_size > notes.size()) {
                break;
            }
            const char* name = reinterpret_cast<const char*>(notes.data() + cursor);
            const auto* desc = notes.data() + cursor + name_size;
            if (note.n_type == NT_GNU_BUILD_ID && note.n_namesz >= 3 &&
                std::string_view(name, 3) == "GNU") {
                std::ostringstream output;
                output << std::hex << std::setfill('0');
                for (Elf64_Word i = 0; i < note.n_descsz; ++i) {
                    output << std::setw(2) << static_cast<unsigned int>(desc[i]);
                }
                return output.str();
            }
            cursor += name_size + desc_size;
        }
    }
    return std::nullopt;
}
} // namespace guardian
