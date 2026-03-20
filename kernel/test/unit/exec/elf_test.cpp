#include "test/test_defs.h"
#include "lib/memory.h"

#include <base/elf.h>

// Only validate/is_elf — no actual loading (would need pgdir setup)
namespace elf {
bool is_elf(const uint8_t* data, size_t size);
int validate(const elfhdr* eh, size_t file_size);
}  // namespace elf

static int tests_passed = 0;
static int tests_failed = 0;

// ============================================================================
// Helper: build a minimal valid ELF64 header
// ============================================================================

static void make_valid_elf64(elfhdr* eh) {
    memset(eh, 0, sizeof(*eh));
    eh->e_magic = ELF_MAGIC;
    eh->e_elf[0] = 2;      // 64-bit
    eh->e_type = 2;        // Executable
    eh->e_machine = 0x3E;  // x86_64
    eh->e_version = 1;
    eh->e_entry = 0x400000;
    eh->e_phoff = sizeof(elfhdr);
    eh->e_phentsize = sizeof(proghdr);
    eh->e_phnum = 1;
    eh->e_ehsize = sizeof(elfhdr);
}

// ============================================================================
// is_elf: valid ELF
// ============================================================================

static void test_is_elf_valid() {
    TEST_START("elf::is_elf — valid ELF");

    alignas(8) uint8_t buf[512];
    memset(buf, 0, sizeof(buf));

    auto* eh = reinterpret_cast<elfhdr*>(buf);
    make_valid_elf64(eh);

    TEST_ASSERT(elf::is_elf(buf, sizeof(buf)), "Valid ELF64 header recognized");

    TEST_END();
}

// ============================================================================
// is_elf: nullptr / too small
// ============================================================================

static void test_is_elf_null_and_small() {
    TEST_START("elf::is_elf — null / small buffer");

    TEST_ASSERT(!elf::is_elf(nullptr, 0), "nullptr returns false");
    TEST_ASSERT(!elf::is_elf(nullptr, 1024), "nullptr with size returns false");

    uint8_t tiny[4] = {0x7f, 'E', 'L', 'F'};
    TEST_ASSERT(!elf::is_elf(tiny, sizeof(tiny)), "Buffer smaller than elfhdr returns false");

    TEST_END();
}

// ============================================================================
// is_elf: bad magic
// ============================================================================

static void test_is_elf_bad_magic() {
    TEST_START("elf::is_elf — bad magic");

    alignas(8) uint8_t buf[512];
    memset(buf, 0, sizeof(buf));

    auto* eh = reinterpret_cast<elfhdr*>(buf);
    make_valid_elf64(eh);
    eh->e_magic = 0xDEADBEEF;

    TEST_ASSERT(!elf::is_elf(buf, sizeof(buf)), "Bad magic returns false");

    TEST_END();
}

// ============================================================================
// validate: valid header
// ============================================================================

static void test_validate_valid() {
    TEST_START("elf::validate — valid header");

    alignas(8) uint8_t buf[512];
    memset(buf, 0, sizeof(buf));

    auto* eh = reinterpret_cast<elfhdr*>(buf);
    make_valid_elf64(eh);

    int rc = elf::validate(eh, sizeof(buf));
    TEST_ASSERT(rc == 0, "Valid ELF64 header passes validation");

    TEST_END();
}

// ============================================================================
// validate: wrong class (32-bit)
// ============================================================================

static void test_validate_wrong_class() {
    TEST_START("elf::validate — wrong class (32-bit)");

    alignas(8) uint8_t buf[512];
    memset(buf, 0, sizeof(buf));

    auto* eh = reinterpret_cast<elfhdr*>(buf);
    make_valid_elf64(eh);
    eh->e_elf[0] = 1;  // 32-bit

    int rc = elf::validate(eh, sizeof(buf));
    TEST_ASSERT(rc != 0, "32-bit ELF rejected");

    TEST_END();
}

// ============================================================================
// validate: not executable
// ============================================================================

static void test_validate_not_exec() {
    TEST_START("elf::validate — not executable");

    alignas(8) uint8_t buf[512];
    memset(buf, 0, sizeof(buf));

    auto* eh = reinterpret_cast<elfhdr*>(buf);
    make_valid_elf64(eh);
    eh->e_type = 1;  // Relocatable, not executable

    int rc = elf::validate(eh, sizeof(buf));
    TEST_ASSERT(rc != 0, "Relocatable ELF rejected");

    TEST_END();
}

// ============================================================================
// validate: wrong machine
// ============================================================================

static void test_validate_wrong_machine() {
    TEST_START("elf::validate — wrong machine");

    alignas(8) uint8_t buf[512];
    memset(buf, 0, sizeof(buf));

    auto* eh = reinterpret_cast<elfhdr*>(buf);
    make_valid_elf64(eh);
    eh->e_machine = 0x28;  // ARM instead of x86_64

    int rc = elf::validate(eh, sizeof(buf));
    TEST_ASSERT(rc != 0, "ARM machine type rejected");

    TEST_END();
}

// ============================================================================
// validate: no program headers
// ============================================================================

static void test_validate_no_phdr() {
    TEST_START("elf::validate — no program headers");

    alignas(8) uint8_t buf[512];
    memset(buf, 0, sizeof(buf));

    auto* eh = reinterpret_cast<elfhdr*>(buf);
    make_valid_elf64(eh);
    eh->e_phoff = 0;
    eh->e_phnum = 0;

    int rc = elf::validate(eh, sizeof(buf));
    TEST_ASSERT(rc != 0, "ELF with no program headers rejected");

    TEST_END();
}

// ============================================================================
// validate: program headers exceed file size
// ============================================================================

static void test_validate_phdr_overflow() {
    TEST_START("elf::validate — phdr table exceeds file");

    alignas(8) uint8_t buf[128];
    memset(buf, 0, sizeof(buf));

    auto* eh = reinterpret_cast<elfhdr*>(buf);
    make_valid_elf64(eh);
    eh->e_phnum = 100;  // Would need 100*56 = 5600 bytes, but file is 128

    int rc = elf::validate(eh, sizeof(buf));
    TEST_ASSERT(rc != 0, "phdr table overflow rejected");

    TEST_END();
}

// ============================================================================
// validate: file too small for header
// ============================================================================

static void test_validate_too_small() {
    TEST_START("elf::validate — file too small");

    uint8_t buf[8] = {};
    auto* eh = reinterpret_cast<elfhdr*>(buf);

    int rc = elf::validate(eh, sizeof(buf));
    TEST_ASSERT(rc != 0, "Tiny file rejected");

    TEST_END();
}

// ============================================================================
// Test Runner
// ============================================================================

namespace elf_test {

void test() {
    tests_passed = 0;
    tests_failed = 0;

    test_is_elf_valid();
    test_is_elf_null_and_small();
    test_is_elf_bad_magic();
    test_validate_valid();
    test_validate_wrong_class();
    test_validate_not_exec();
    test_validate_wrong_machine();
    test_validate_no_phdr();
    test_validate_phdr_overflow();
    test_validate_too_small();

    TEST_SUMMARY("ELF Loader");
}

}  // namespace elf_test
