#include "test/test_defs.h"
#include "drivers/device_tree.h"
#include "lib/string.h"

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

// Builds a flattened device tree in a caller's buffer:
//   / { soc { ethernet@16030000 { local-mac-address = FIRST; };
//             ethernet@16040000 { phy { local-mac-address = CHILD; }; }; }; };
// Only the second controller's child carries an address; the reader must not
// take the child's value for its parent.
class Writer {
public:
    explicit Writer(uint8_t* buffer) : buffer_(buffer) {}

    size_t build(const uint8_t (&first)[6], const uint8_t (&child)[6]) {
        offset_ = HEADER_BYTES;
        const size_t structure = offset_;
        begin("");
        begin("soc");
        begin("ethernet@16030000");
        prop(first, sizeof(first));
        end();
        begin("ethernet@16040000");
        begin("phy");
        prop(child, sizeof(child));
        end();
        end();
        end();
        end();
        word(9);
        const size_t structure_size = offset_ - structure;
        const size_t strings = offset_;
        memcpy(buffer_ + offset_, NAME, sizeof(NAME));
        offset_ += sizeof(NAME);
        const uint32_t header[] = {0xD00DFEED,
                                   static_cast<uint32_t>(offset_),
                                   static_cast<uint32_t>(structure),
                                   static_cast<uint32_t>(strings),
                                   static_cast<uint32_t>(HEADER_BYTES),
                                   17,
                                   16,
                                   0,
                                   static_cast<uint32_t>(sizeof(NAME)),
                                   static_cast<uint32_t>(structure_size)};
        for (size_t i = 0; i < sizeof(header) / sizeof(header[0]); ++i)
            put(i * 4, header[i]);
        return offset_;
    }

    // Offset of the first property's length word.
    size_t first_property_length() const { return first_property_; }

private:
    static constexpr size_t HEADER_BYTES = 40;
    static constexpr char NAME[] = "local-mac-address";

    void put(size_t at, uint32_t value) {
        buffer_[at] = static_cast<uint8_t>(value >> 24);
        buffer_[at + 1] = static_cast<uint8_t>(value >> 16);
        buffer_[at + 2] = static_cast<uint8_t>(value >> 8);
        buffer_[at + 3] = static_cast<uint8_t>(value);
    }
    void word(uint32_t value) {
        put(offset_, value);
        offset_ += 4;
    }
    void pad() {
        while (offset_ % 4 != 0)
            buffer_[offset_++] = 0;
    }
    void begin(const char* name) {
        word(1);
        const size_t length = strlen(name) + 1;
        memcpy(buffer_ + offset_, name, length);
        offset_ += length;
        pad();
    }
    void end() { word(2); }
    void prop(const uint8_t* value, size_t size) {
        word(3);
        if (first_property_ == 0)
            first_property_ = offset_;
        word(static_cast<uint32_t>(size));
        word(0);
        memcpy(buffer_ + offset_, value, size);
        offset_ += size;
        pad();
    }

    uint8_t* buffer_;
    size_t offset_{};
    size_t first_property_{};
};

constexpr uint8_t FIRST[6] = {0x6C, 0xCF, 0x39, 0x00, 0xAD, 0x4A};
constexpr uint8_t CHILD[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};

void test_lookup() {
    TEST_START("Device tree: a property is read from the named node only");
    static uint8_t blob[256]{};
    Writer writer(blob);
    const size_t size = writer.build(FIRST, CHILD);
    auto tree = device_tree::Tree::open(blob, size);
    TEST_ASSERT(tree.ok(), "A well-formed tree opens");
    if (tree.ok()) {
        auto first = tree.value().property("ethernet@16030000", "local-mac-address");
        TEST_ASSERT(first.ok() && first.value().size == sizeof(FIRST) &&
                        memcmp(first.value().data, FIRST, sizeof(FIRST)) == 0,
                    "The value of the node with that name and unit address is returned");
        auto second = tree.value().property("ethernet@16040000", "local-mac-address");
        TEST_ASSERT(!second.ok() && second.error() == Error::NotFound, "A child node's property is not the parent's");
        auto missing = tree.value().property("ethernet@16050000", "local-mac-address");
        TEST_ASSERT(!missing.ok() && missing.error() == Error::NotFound, "A node that is absent is not found");
    }
    TEST_END();
}

void test_malformed() {
    TEST_START("Device tree: a malformed tree is rejected, not read past its end");
    static uint8_t blob[256]{};
    Writer writer(blob);
    const size_t size = writer.build(FIRST, CHILD);
    TEST_ASSERT(!device_tree::Tree::open(blob, size - 1).ok(), "A tree larger than its buffer is refused");
    blob[0] ^= 0xFF;
    TEST_ASSERT(!device_tree::Tree::open(blob, size).ok(), "A wrong magic number is refused");
    blob[0] ^= 0xFF;
    // A property claiming more bytes than the structure block holds.
    const size_t at = writer.first_property_length();
    blob[at] = 0x7F;
    auto tree = device_tree::Tree::open(blob, size);
    auto value = tree.ok() ? tree.value().property("ethernet@16030000", "local-mac-address")
                           : Result<device_tree::Property>(Error::Invalid);
    TEST_ASSERT(!value.ok() && value.error() == Error::Invalid, "An oversized property is reported as invalid");
    TEST_END();
}

}  // namespace

namespace device_tree_test {

void test() {
    tests_passed = 0;
    tests_failed = 0;
    test_lookup();
    test_malformed();
    TEST_SUMMARY("Device tree");
}

}  // namespace device_tree_test
