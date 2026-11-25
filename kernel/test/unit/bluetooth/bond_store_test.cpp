#include "test/test_defs.h"
#include "drivers/bluetooth/bond_store.h"
#include "lib/memory.h"

static int tests_passed = 0;
static int tests_failed = 0;

namespace {

using bluetooth::bond_store::Record;
namespace bond_store = bluetooth::bond_store;

// A keyboard in boot protocol and a mouse in report protocol, bonded with
// two different adapters.
void make_records(Record* records) {
    Record& keyboard = records[0];
    keyboard = Record{};
    const uint8_t local[] = {0x1D, 0x60, 0x05, 0x0E, 0x7F, 0x04};
    const uint8_t keyboard_address[] = {0x6B, 0xD5, 0x26, 0xD3, 0xF5, 0xCB};
    memcpy(keyboard.local, local, sizeof(local));
    keyboard.appearance = 0x03C1;
    bluetooth::Bond& bond = keyboard.bond;
    bond.valid = true;
    memcpy(bond.address, keyboard_address, sizeof(keyboard_address));
    bond.address_type = 1;
    for (size_t i = 0; i < bluetooth::KEY_BYTES; ++i)
        bond.ltk[i] = static_cast<uint8_t>(0xA0 + i);
    bond.ediv = 0x1234;
    for (size_t i = 0; i < sizeof(bond.rand); ++i)
        bond.rand[i] = static_cast<uint8_t>(i + 1);
    bond.protocol_mode = 0x0017;
    bond.reports[0] = 0x001E;
    bond.configs[0] = 0x001F;
    bond.kinds[0] = bluetooth::ReportKind::Keyboard;
    bond.report_count = 1;

    Record& mouse = records[1];
    mouse = keyboard;
    mouse.local[0] = 0x99;  // Another adapter
    mouse.appearance = 0x03C2;
    mouse.bond.address[0] = 0x59;
    mouse.bond.protocol_mode = 0;
    mouse.bond.kinds[0] = bluetooth::ReportKind::Mouse;
    mouse.bond.mouse_layout = {
        true, 3, {true, false, 5, 0}, {true, true, 16, 8}, {true, true, 16, 24}, {true, true, 8, 40}};
}

bool same_field(const input::ReportField& a, const input::ReportField& b) {
    return a.present == b.present && a.is_signed == b.is_signed && a.size == b.size && a.bit == b.bit;
}

bool same(const Record& a, const Record& b) {
    const bluetooth::Bond& x = a.bond;
    const bluetooth::Bond& y = b.bond;
    return memcmp(a.local, b.local, sizeof(a.local)) == 0 && a.appearance == b.appearance && x.valid == y.valid &&
           memcmp(x.address, y.address, sizeof(x.address)) == 0 && x.address_type == y.address_type &&
           memcmp(x.ltk, y.ltk, sizeof(x.ltk)) == 0 && x.ediv == y.ediv &&
           memcmp(x.rand, y.rand, sizeof(x.rand)) == 0 && x.protocol_mode == y.protocol_mode &&
           x.report_count == y.report_count && memcmp(x.reports, y.reports, sizeof(x.reports)) == 0 &&
           memcmp(x.configs, y.configs, sizeof(x.configs)) == 0 && x.kinds[0] == y.kinds[0] &&
           x.mouse_layout.valid == y.mouse_layout.valid && x.mouse_layout.report_id == y.mouse_layout.report_id &&
           same_field(x.mouse_layout.buttons, y.mouse_layout.buttons) &&
           same_field(x.mouse_layout.x, y.mouse_layout.x) && same_field(x.mouse_layout.y, y.mouse_layout.y) &&
           same_field(x.mouse_layout.wheel, y.mouse_layout.wheel);
}

void test_round_trip() {
    TEST_START("Bluetooth bond file: round trip");
    Record records[2];
    make_records(records);
    uint8_t file[bond_store::FILE_CAPACITY];
    const size_t length = bond_store::encode(records, 2, file, sizeof(file));
    TEST_ASSERT(length == bond_store::HEADER_BYTES + 2 * bond_store::RECORD_BYTES + bond_store::CHECKSUM_BYTES,
                "Two records make a file of two records");
    Record decoded[bond_store::MAX_RECORDS];
    size_t count = 0;
    TEST_ASSERT(bond_store::decode(file, length, decoded, bond_store::MAX_RECORDS, count) && count == 2,
                "The file reads back as two records");
    TEST_ASSERT(same(decoded[0], records[0]) && same(decoded[1], records[1]),
                "Keys, report handles and the mouse layout survive, with the adapter each bond belongs to");
    TEST_END();
}

void test_damaged_files() {
    TEST_START("Bluetooth bond file: damaged files are refused");
    Record records[2];
    make_records(records);
    uint8_t file[bond_store::FILE_CAPACITY];
    const size_t length = bond_store::encode(records, 2, file, sizeof(file));
    Record decoded[bond_store::MAX_RECORDS];
    size_t count = 0;
    file[bond_store::HEADER_BYTES + 20] ^= 0x01;  // A bit of a key flips
    TEST_ASSERT(!bond_store::decode(file, length, decoded, bond_store::MAX_RECORDS, count) && count == 0,
                "A changed byte fails the checksum");
    file[bond_store::HEADER_BYTES + 20] ^= 0x01;
    TEST_ASSERT(!bond_store::decode(file, length - 1, decoded, bond_store::MAX_RECORDS, count),
                "A file cut short is refused");
    file[4] = 2;
    TEST_ASSERT(!bond_store::decode(file, length, decoded, bond_store::MAX_RECORDS, count),
                "Another version is refused");
    TEST_ASSERT(bond_store::encode(records, bond_store::MAX_RECORDS + 1, file, sizeof(file)) == 0,
                "More records than the file holds are not written");
    TEST_END();
}

}  // namespace

namespace bond_store_test {

void test() {
    tests_passed = 0;
    tests_failed = 0;
    test_round_trip();
    test_damaged_files();
    TEST_SUMMARY("Bluetooth bonds");
}

}  // namespace bond_store_test
