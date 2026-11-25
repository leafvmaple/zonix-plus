#include "drivers/bluetooth/bond_store.h"
#include "lib/memory.h"

namespace bluetooth::bond_store {
namespace {

constexpr uint8_t MAGIC[] = {'Z', 'B', 'T', 'B'};
constexpr uint8_t VERSION = 1;
constexpr uint32_t FNV_OFFSET = 2166136261U;
constexpr uint32_t FNV_PRIME = 16777619U;
constexpr size_t FIELD_BYTES = 5;  // Present, signed, size, bit

uint32_t checksum(const uint8_t* data, size_t length) {
    uint32_t hash = FNV_OFFSET;
    for (size_t i = 0; i < length; ++i)
        hash = (hash ^ data[i]) * FNV_PRIME;
    return hash;
}

// Sequential little-endian writing and reading of a record.
class Writer {
public:
    explicit Writer(uint8_t* at) : at_(at) {}
    void bytes(const uint8_t* data, size_t length) {
        memcpy(at_, data, length);
        at_ += length;
    }
    void u8(uint8_t value) { *at_++ = value; }
    void u16(uint16_t value) {
        u8(static_cast<uint8_t>(value));
        u8(static_cast<uint8_t>(value >> 8));
    }
    void field(const input::ReportField& field) {
        u8(field.present ? 1 : 0);
        u8(field.is_signed ? 1 : 0);
        u8(field.size);
        u16(field.bit);
    }

private:
    uint8_t* at_;
};

class Reader {
public:
    explicit Reader(const uint8_t* at) : at_(at) {}
    void bytes(uint8_t* data, size_t length) {
        memcpy(data, at_, length);
        at_ += length;
    }
    uint8_t u8() { return *at_++; }
    uint16_t u16() {
        const uint8_t low = u8();
        return static_cast<uint16_t>(low | (u8() << 8));
    }
    input::ReportField field() {
        input::ReportField field{};
        field.present = u8() != 0;
        field.is_signed = u8() != 0;
        field.size = u8();
        field.bit = u16();
        return field;
    }

private:
    const uint8_t* at_;
};

void write_record(const Record& record, uint8_t* out) {
    const Bond& bond = record.bond;
    Writer writer(out);
    writer.bytes(record.local, ADDRESS_BYTES);
    writer.bytes(bond.address, ADDRESS_BYTES);
    writer.u8(bond.address_type);
    writer.u16(record.appearance);
    writer.bytes(bond.ltk, KEY_BYTES);
    writer.u16(bond.ediv);
    writer.bytes(bond.rand, sizeof(bond.rand));
    writer.u16(bond.protocol_mode);
    writer.u8(static_cast<uint8_t>(bond.report_count));
    for (size_t i = 0; i < MAX_REPORTS; ++i)
        writer.u16(bond.reports[i]);
    for (size_t i = 0; i < MAX_REPORTS; ++i)
        writer.u16(bond.configs[i]);
    for (size_t i = 0; i < MAX_REPORTS; ++i)
        writer.u8(static_cast<uint8_t>(bond.kinds[i]));
    const input::MouseLayout& mouse = bond.mouse_layout;
    writer.u8(mouse.valid ? 1 : 0);
    writer.u8(mouse.report_id);
    writer.field(mouse.buttons);
    writer.field(mouse.x);
    writer.field(mouse.y);
    writer.field(mouse.wheel);
}

bool read_record(const uint8_t* in, Record& record) {
    Bond& bond = record.bond;
    Reader reader(in);
    reader.bytes(record.local, ADDRESS_BYTES);
    reader.bytes(bond.address, ADDRESS_BYTES);
    bond.address_type = reader.u8();
    record.appearance = reader.u16();
    reader.bytes(bond.ltk, KEY_BYTES);
    bond.ediv = reader.u16();
    reader.bytes(bond.rand, sizeof(bond.rand));
    bond.protocol_mode = reader.u16();
    bond.report_count = reader.u8();
    for (size_t i = 0; i < MAX_REPORTS; ++i)
        bond.reports[i] = reader.u16();
    for (size_t i = 0; i < MAX_REPORTS; ++i)
        bond.configs[i] = reader.u16();
    bool kinds_valid = true;
    for (size_t i = 0; i < MAX_REPORTS; ++i) {
        const uint8_t kind = reader.u8();
        kinds_valid = kinds_valid && kind <= static_cast<uint8_t>(ReportKind::Mouse);
        bond.kinds[i] = static_cast<ReportKind>(kind);
    }
    input::MouseLayout& mouse = bond.mouse_layout;
    mouse.valid = reader.u8() != 0;
    mouse.report_id = reader.u8();
    mouse.buttons = reader.field();
    mouse.x = reader.field();
    mouse.y = reader.field();
    mouse.wheel = reader.field();
    bond.valid = true;
    return kinds_valid && bond.report_count != 0 && bond.report_count <= MAX_REPORTS;
}

static_assert(RECORD_BYTES ==
              2 * ADDRESS_BYTES + 1 + 2 + KEY_BYTES + 2 + 8 + 2 + 1 + MAX_REPORTS * 5 + 2 + 4 * FIELD_BYTES);

}  // namespace

size_t encode(const Record* records, size_t count, uint8_t* out, size_t capacity) {
    const size_t length = HEADER_BYTES + count * RECORD_BYTES + CHECKSUM_BYTES;
    if (count > MAX_RECORDS || length > capacity)
        return 0;
    memcpy(out, MAGIC, sizeof(MAGIC));
    out[4] = VERSION;
    out[5] = static_cast<uint8_t>(count);
    out[6] = 0;
    out[7] = 0;
    for (size_t i = 0; i < count; ++i)
        write_record(records[i], out + HEADER_BYTES + i * RECORD_BYTES);
    const uint32_t sum = checksum(out, length - CHECKSUM_BYTES);
    for (size_t i = 0; i < CHECKSUM_BYTES; ++i)
        out[length - CHECKSUM_BYTES + i] = static_cast<uint8_t>(sum >> (8 * i));
    return length;
}

bool decode(const uint8_t* data, size_t length, Record* records, size_t capacity, size_t& count) {
    count = 0;
    if (length < HEADER_BYTES + CHECKSUM_BYTES || memcmp(data, MAGIC, sizeof(MAGIC)) != 0 || data[4] != VERSION)
        return false;
    const size_t stored = data[5];
    const size_t expected = HEADER_BYTES + stored * RECORD_BYTES + CHECKSUM_BYTES;
    if (stored > MAX_RECORDS || stored > capacity || length != expected)
        return false;
    uint32_t sum = 0;
    for (size_t i = 0; i < CHECKSUM_BYTES; ++i)
        sum |= static_cast<uint32_t>(data[expected - CHECKSUM_BYTES + i]) << (8 * i);
    if (sum != checksum(data, expected - CHECKSUM_BYTES))
        return false;
    for (size_t i = 0; i < stored; ++i) {
        records[i] = Record{};
        if (!read_record(data + HEADER_BYTES + i * RECORD_BYTES, records[i]))
            return false;
    }
    count = stored;
    return true;
}

}  // namespace bluetooth::bond_store
