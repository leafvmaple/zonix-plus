#include "drivers/device_tree.h"
#include "lib/string.h"
#include <kernel/address.h>

namespace device_tree {
namespace {

// Header fields, all big-endian 32-bit words.
constexpr uint32_t MAGIC = 0xD00DFEED;
constexpr size_t HEADER_MAGIC = 0;
constexpr size_t HEADER_TOTAL_SIZE = 4;
constexpr size_t HEADER_STRUCTURE_OFFSET = 8;
constexpr size_t HEADER_STRINGS_OFFSET = 12;
constexpr size_t HEADER_LAST_COMPATIBLE_VERSION = 24;
constexpr size_t HEADER_STRINGS_SIZE = 32;
constexpr size_t HEADER_STRUCTURE_SIZE = 36;
constexpr size_t HEADER_BYTES = 40;
constexpr uint32_t SUPPORTED_VERSION = 16;

// Structure block tokens.
constexpr uint32_t BEGIN_NODE = 1;
constexpr uint32_t END_NODE = 2;
constexpr uint32_t PROP = 3;
constexpr uint32_t NOP = 4;
constexpr uint32_t END = 9;
constexpr size_t TOKEN_BYTES = 4;

uint32_t load_be32(const uint8_t* bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) | bytes[3];
}

size_t align_token(size_t offset) {
    return (offset + TOKEN_BYTES - 1) & ~(TOKEN_BYTES - 1);
}

// The length of the NUL-terminated string at `offset`, or `size` when it
// runs past the end.
size_t string_length(const uint8_t* base, size_t offset, size_t size) {
    size_t length = 0;
    while (offset + length < size && base[offset + length] != 0)
        ++length;
    return offset + length < size ? length : size;
}

class Firmware {
public:
    static void set(uintptr_t pa) { pa_ = pa; }
    static uintptr_t pa() { return pa_; }

private:
    inline static uintptr_t pa_{};
};

}  // namespace

Result<Tree> Tree::open(const void* blob, size_t size) {
    ENSURE(blob && size >= HEADER_BYTES, Error::Invalid);
    const auto* bytes = static_cast<const uint8_t*>(blob);
    ENSURE(load_be32(bytes + HEADER_MAGIC) == MAGIC, Error::Invalid);
    ENSURE(load_be32(bytes + HEADER_LAST_COMPATIBLE_VERSION) <= SUPPORTED_VERSION, Error::NotSupported);
    const size_t total = load_be32(bytes + HEADER_TOTAL_SIZE);
    const size_t structure = load_be32(bytes + HEADER_STRUCTURE_OFFSET);
    const size_t structure_size = load_be32(bytes + HEADER_STRUCTURE_SIZE);
    const size_t strings = load_be32(bytes + HEADER_STRINGS_OFFSET);
    const size_t strings_size = load_be32(bytes + HEADER_STRINGS_SIZE);
    ENSURE(total >= HEADER_BYTES && total <= size, Error::Invalid);
    ENSURE(structure <= total && structure_size <= total - structure, Error::Invalid);
    ENSURE(strings <= total && strings_size <= total - strings, Error::Invalid);
    return Tree(bytes + structure, structure_size, bytes + strings, strings_size);
}

Result<Property> Tree::property(const char* node, const char* property) const {
    ENSURE(node && property);
    const size_t node_length = strlen(node);
    const size_t property_length = strlen(property);
    size_t offset = 0;
    // Depth of the node being read, and the depth of the requested node once
    // it has been entered; its properties come before its children.
    size_t depth = 0;
    size_t inside = 0;
    while (offset + TOKEN_BYTES <= structure_size_) {
        const uint32_t token = load_be32(structure_ + offset);
        offset += TOKEN_BYTES;
        switch (token) {
            case BEGIN_NODE: {
                const size_t length = string_length(structure_, offset, structure_size_);
                ENSURE(length != structure_size_, Error::Invalid);
                ++depth;
                if (inside == 0 && length == node_length && memcmp(structure_ + offset, node, length) == 0)
                    inside = depth;
                offset = align_token(offset + length + 1);
                break;
            }
            case END_NODE:
                ENSURE(depth > 0, Error::Invalid);
                if (inside == depth)
                    return Error::NotFound;
                --depth;
                break;
            case PROP: {
                ENSURE(offset + 2 * TOKEN_BYTES <= structure_size_, Error::Invalid);
                const size_t size = load_be32(structure_ + offset);
                const size_t name = load_be32(structure_ + offset + TOKEN_BYTES);
                offset += 2 * TOKEN_BYTES;
                ENSURE(size <= structure_size_ - offset, Error::Invalid);
                if (inside != 0 && inside == depth) {
                    const size_t length = string_length(strings_, name, strings_size_);
                    ENSURE(name < strings_size_ && length != strings_size_, Error::Invalid);
                    if (length == property_length && memcmp(strings_ + name, property, length) == 0)
                        return Property{structure_ + offset, size};
                }
                offset = align_token(offset + size);
                break;
            }
            case NOP: break;
            case END: return Error::NotFound;
            default: return Error::Invalid;
        }
    }
    return Error::Invalid;
}

void set_firmware_address(uintptr_t pa) {
    Firmware::set(pa);
}

Result<Tree> firmware() {
    const uintptr_t pa = Firmware::pa();
    const bool mapped = pa < KERNEL_DIRECT_MAP_END_PA && HEADER_BYTES <= KERNEL_DIRECT_MAP_END_PA - pa;
    ENSURE(pa != 0 && mapped, Error::NotFound);
    const auto* header = phys_to_virt<const uint8_t>(pa);
    const size_t size = load_be32(header + HEADER_TOTAL_SIZE);
    ENSURE(size <= KERNEL_DIRECT_MAP_END_PA - pa, Error::NotFound);
    return Tree::open(header, size);
}

}  // namespace device_tree
