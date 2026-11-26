#pragma once

#include <base/types.h>
#include "lib/result.h"

// Read-only access to a flattened device tree (devicetree specification
// v0.4, chapter 5), such as the one firmware hands the UEFI loader. Only
// what drivers need to look up board data is supported.
namespace device_tree {

struct Property {
    const uint8_t* data;
    size_t size;
};

class Tree {
public:
    // Checks the header and that its blocks lie within `size` bytes.
    static Result<Tree> open(const void* blob, size_t size);

    // The value of `property` in the first node named `node`, including any
    // unit address ("ethernet@16040000"). NotFound when either is missing,
    // Invalid when the structure block is malformed.
    Result<Property> property(const char* node, const char* property) const;

private:
    Tree(const uint8_t* structure, size_t structure_size, const uint8_t* strings, size_t strings_size)
        : structure_(structure), structure_size_(structure_size), strings_(strings), strings_size_(strings_size) {}

    const uint8_t* structure_;
    size_t structure_size_;
    const uint8_t* strings_;
    size_t strings_size_;
};

// Records where the loader left firmware's tree (0 for none); kern_init
// calls it once with the boot information.
void set_firmware_address(uintptr_t pa);
// The tree the UEFI loader received from firmware. NotFound when there was
// none or it lies outside the kernel's direct map.
Result<Tree> firmware();

}  // namespace device_tree
