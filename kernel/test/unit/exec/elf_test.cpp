#include "test/test_defs.h"
#include "lib/memory.h"
#include "lib/result.h"

#include <base/elf.h>

// Only validate/is_elf — no actual loading (would need pgdir setup)
namespace elf {
bool is_elf(const uint8_t* data, size_t size);
Error validate(const ElfHeader* eh, size_t file_size);
}  // namespace elf

static int tests_passed = 0;
static int tests_failed = 0;

// ============================================================================
// Helper: build a minimal valid ELF64 header
// ============================================================================

static void make_valid_elf64(ElfHeader* eh) {
    memset(eh, 0, sizeof(*eh));
    eh->e_magic = ELF_MAGIC;
    eh->e_elf[0] = 2;            // 64-bit
    eh->e_type = 2;              // Executable
    eh->e_machine = EM_CURRENT;  // Current architecture
    eh->e_version = 1;
    eh->e_entry = 0x400000;
    eh->e_phoff = sizeof(ElfHeader);
    eh->e_phentsize = sizeof(ProgramHeader);
    eh->e_phnum = 1;
    eh->e_ehsize = sizeof(ElfHeader);
    auto* ph = reinterpret_cast<ProgramHeader*>(reinterpret_cast<uint8_t*>(eh) + eh->e_phoff);
    memset(ph, 0, sizeof(*ph));
    ph->p_type = ELF_PT_LOAD;
    ph->p_flags = ELF_PF_R | ELF_PF_X;
    ph->p_va = eh->e_entry;
    ph->p_memsz = 4096;
}

// ============================================================================
// is_elf: valid ELF
// ============================================================================

static void test_is_elf_valid() {
    TEST_START("elf::is_elf — valid ELF");

    alignas(8) uint8_t buf[512];
    memset(buf, 0, sizeof(buf));

    auto* eh = reinterpret_cast<ElfHeader*>(buf);
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
    TEST_ASSERT(!elf::is_elf(tiny, sizeof(tiny)), "Buffer smaller than ElfHeader returns false");

    TEST_END();
}

// ============================================================================
// is_elf: bad magic
// ============================================================================

static void test_is_elf_bad_magic() {
    TEST_START("elf::is_elf — bad magic");

    alignas(8) uint8_t buf[512];
    memset(buf, 0, sizeof(buf));

    auto* eh = reinterpret_cast<ElfHeader*>(buf);
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

    auto* eh = reinterpret_cast<ElfHeader*>(buf);
    make_valid_elf64(eh);

    Error rc = elf::validate(eh, sizeof(buf));
    TEST_ASSERT(rc == Error::None, "Valid ELF64 header passes validation");

    TEST_END();
}

// ============================================================================
// validate: wrong class (32-bit)
// ============================================================================

static void test_validate_wrong_class() {
    TEST_START("elf::validate — wrong class (32-bit)");

    alignas(8) uint8_t buf[512];
    memset(buf, 0, sizeof(buf));

    auto* eh = reinterpret_cast<ElfHeader*>(buf);
    make_valid_elf64(eh);
    eh->e_elf[0] = 1;  // 32-bit

    Error rc = elf::validate(eh, sizeof(buf));
    TEST_ASSERT(rc != Error::None, "32-bit ELF rejected");

    TEST_END();
}

// ============================================================================
// validate: not executable
// ============================================================================

static void test_validate_not_exec() {
    TEST_START("elf::validate — not executable");

    alignas(8) uint8_t buf[512];
    memset(buf, 0, sizeof(buf));

    auto* eh = reinterpret_cast<ElfHeader*>(buf);
    make_valid_elf64(eh);
    eh->e_type = 1;  // Relocatable, not executable

    Error rc = elf::validate(eh, sizeof(buf));
    TEST_ASSERT(rc != Error::None, "Relocatable ELF rejected");

    TEST_END();
}

// ============================================================================
// validate: wrong machine
// ============================================================================

static void test_validate_wrong_machine() {
    TEST_START("elf::validate — wrong machine");

    alignas(8) uint8_t buf[512];
    memset(buf, 0, sizeof(buf));

    auto* eh = reinterpret_cast<ElfHeader*>(buf);
    make_valid_elf64(eh);
    eh->e_machine = 0xFFFF;  // Invalid machine type

    Error rc = elf::validate(eh, sizeof(buf));
    TEST_ASSERT(rc != Error::None, "Wrong machine type rejected");

    TEST_END();
}

// ============================================================================
// validate: no program headers
// ============================================================================

static void test_validate_no_phdr() {
    TEST_START("elf::validate — no program headers");

    alignas(8) uint8_t buf[512];
    memset(buf, 0, sizeof(buf));

    auto* eh = reinterpret_cast<ElfHeader*>(buf);
    make_valid_elf64(eh);
    eh->e_phoff = 0;
    eh->e_phnum = 0;

    Error rc = elf::validate(eh, sizeof(buf));
    TEST_ASSERT(rc != Error::None, "ELF with no program headers rejected");

    TEST_END();
}

// ============================================================================
// validate: program headers exceed file size
// ============================================================================

static void test_validate_phdr_overflow() {
    TEST_START("elf::validate — phdr table exceeds file");

    alignas(8) uint8_t buf[128];
    memset(buf, 0, sizeof(buf));

    auto* eh = reinterpret_cast<ElfHeader*>(buf);
    make_valid_elf64(eh);
    eh->e_phnum = 100;  // Would need 100*56 = 5600 bytes, but file is 128

    Error rc = elf::validate(eh, sizeof(buf));
    TEST_ASSERT(rc != Error::None, "phdr table overflow rejected");

    TEST_END();
}

// ============================================================================
// validate: file too small for header
// ============================================================================

static void test_validate_too_small() {
    TEST_START("elf::validate — file too small");

    uint8_t buf[8] = {};
    auto* eh = reinterpret_cast<ElfHeader*>(buf);

    Error rc = elf::validate(eh, sizeof(buf));
    TEST_ASSERT(rc != Error::None, "Tiny file rejected");

    TEST_END();
}

// ============================================================================
// Malformed input must be rejected before allocating or copying pages.
// ============================================================================

static void test_validate_malformed_segments() {
    TEST_START("elf::validate — malformed segments and header sizes");
    alignas(8) uint8_t buf[512]{};
    auto* eh = reinterpret_cast<ElfHeader*>(buf);
    make_valid_elf64(eh);
    auto* ph = reinterpret_cast<ProgramHeader*>(buf + sizeof(ElfHeader));

    ph->p_filesz = ph->p_memsz + 1;
    TEST_ASSERT(elf::validate(eh, sizeof(buf)) == Error::Invalid, "filesz larger than memsz rejected");
    make_valid_elf64(eh);
    ph->p_offset = static_cast<uint64_t>(-16);
    ph->p_filesz = 32;
    TEST_ASSERT(elf::validate(eh, sizeof(buf)) == Error::Invalid, "Wrapped segment file range rejected");
    make_valid_elf64(eh);
    ph->p_va = static_cast<uint64_t>(-4096);
    ph->p_memsz = 8192;
    TEST_ASSERT(elf::validate(eh, sizeof(buf)) == Error::Invalid, "Wrapped virtual range rejected");
    make_valid_elf64(eh);
    eh->e_phoff = static_cast<uint64_t>(-16);
    TEST_ASSERT(elf::validate(eh, sizeof(buf)) == Error::Invalid, "Wrapped program header range rejected");
    make_valid_elf64(eh);
    eh->e_phentsize = 0;
    TEST_ASSERT(elf::validate(eh, sizeof(buf)) == Error::Invalid, "Incorrect program header size rejected");
    make_valid_elf64(eh);
    eh->e_entry = ph->p_va + ph->p_memsz;
    TEST_ASSERT(elf::validate(eh, sizeof(buf)) == Error::Invalid, "Entry outside executable segment rejected");
    TEST_END();
}

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
    test_validate_malformed_segments();

    TEST_SUMMARY("ELF Loader");
}

}  // namespace elf_test
