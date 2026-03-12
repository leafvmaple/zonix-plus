/**
 * Generic binary execution framework.
 *
 * Format detection strategy
 * -------------------------
 *   1. Read file into a kernel buffer.
 *   2. Inspect the first few bytes (magic numbers) to identify the format.
 *   3. Dispatch to the matching loader which maps segments into pgdir
 *      and returns an entry-point address.
 *
 * Adding a new format (e.g. flat binary, a.out):
 *   - Write a loader: uintptr_t load_xxx(data, size, pgdir);
 *   - Add a magic check + dispatch in load_binary() below.
 */

#include "exec.h"
#include "elf_loader.h"

#include "lib/stdio.h"
#include "lib/memory.h"
#include "lib/string.h"
#include "lib/math.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "sched/sched.h"
#include "drivers/intr.h"
#include "debug/assert.h"

#include <asm/mmu.h>
#include <asm/cpu.h>
#include <asm/segments.h>
#include <base/bpb.h>

extern pde_t* boot_pgdir;

namespace exec {

// ============================================================================
// Helpers — RAII buffer wrapper to avoid repeated free logic
// ============================================================================

struct KernelBuf {
    uint8_t* ptr = nullptr;

    KernelBuf() = default;
    ~KernelBuf() { kfree(ptr); }

    bool alloc(size_t bytes) {
        ptr = static_cast<uint8_t*>(kmalloc(bytes));
        return ptr != nullptr;
    }

    // Non-copyable
    KernelBuf(const KernelBuf&) = delete;
    KernelBuf& operator=(const KernelBuf&) = delete;
};

pde_t* create_user_pgdir() {
    auto* pgdir = static_cast<pde_t*>(kmalloc(PG_SIZE));
    if (!pgdir) {
        cprintf("exec: failed to allocate page for PML4\n");
        return nullptr;
    }

    memset(pgdir, 0, PG_SIZE);
    // Copy higher-half kernel mappings (PML4 entries 256-511)
    memcpy(&pgdir[USER_PML4_ENTRIES], &boot_pgdir[USER_PML4_ENTRIES], (ENTRY_NUM - USER_PML4_ENTRIES) * sizeof(pde_t));

    return pgdir;
}

uintptr_t setup_user_stack(pde_t* pgdir) {
    uintptr_t stack_bottom = USER_STACK_TOP - USER_STACK_SIZE;

    for (uintptr_t va = stack_bottom; va < USER_STACK_TOP; va += PG_SIZE) {
        Page* page = pmm::pgdir_alloc_page(pgdir, va, PTE_U | PTE_W);
        if (!page) {
            cprintf("exec: failed to allocate user stack page at 0x%lx\n", va);
            return 0;
        }

        // Zero the stack page
        memset(phys_to_virt(pmm::page2pa(page)), 0, PG_SIZE);
    }

    return USER_STACK_TOP;
}

/**
 * Attempt to load a binary of unknown format into @p pgdir.
 *
 * Currently supported formats:
 *   - ELF64 (detected by "\x7fELF" magic)
 *
 * @return Entry-point virtual address, or 0 on failure / unknown format
 */
static uintptr_t load_binary(const uint8_t* data, size_t size, pde_t* pgdir) {
    if (elf::is_elf(data, size)) {
        return elf::load(data, size, pgdir);
    }

    // --- future formats go here ---
    // if (is_flat_binary(data, size)) { ... }
    // if (is_aout(data, size))        { ... }

    cprintf("exec: unrecognised binary format (magic: %02x %02x %02x %02x)\n", size > 0 ? data[0] : 0,
            size > 1 ? data[1] : 0, size > 2 ? data[2] : 0, size > 3 ? data[3] : 0);
    return 0;
}

int exec(const char* path, FatInfo* fat) {
    if (!fat || !path) {
        cprintf("exec: invalid arguments\n");
        return -1;
    }

    fat_dir_entry_t dir_entry;
    if (fat->find_file(path, &dir_entry) != 0) {
        cprintf("exec: file not found: %s\n", path);
        return -1;
    }

    if (dir_entry.attr & FAT_ATTR_DIRECTORY) {
        cprintf("exec: cannot execute directory: %s\n", path);
        return -1;
    }

    uint32_t file_size = dir_entry.file_size;
    if (file_size == 0) {
        cprintf("exec: empty file: %s\n", path);
        return -1;
    }
    if (file_size > MAX_BINARY_SIZE) {
        cprintf("exec: file too large (%d bytes, max %d)\n", file_size, MAX_BINARY_SIZE);
        return -1;
    }

    KernelBuf buf;
    if (!buf.alloc(file_size)) {
        cprintf("exec: out of memory for binary buffer (%d bytes)\n", file_size);
        return -1;
    }

    int bytes_read = fat->read_file(&dir_entry, buf.ptr, 0, file_size);
    if (bytes_read < static_cast<int>(file_size)) {
        cprintf("exec: failed to read file (%d/%d bytes)\n", bytes_read, file_size);
        return -1;
    }

    pde_t* user_pgdir = create_user_pgdir();
    if (!user_pgdir) {
        cprintf("exec: failed to create user page directory\n");
        return -1;
    }

    uintptr_t entry = load_binary(buf.ptr, file_size, user_pgdir);
    if (entry == 0) {
        cprintf("exec: failed to load binary\n");
        pmm::free_user_pgdir(user_pgdir);
        return -1;
    }

    uintptr_t user_rsp = setup_user_stack(user_pgdir);
    if (user_rsp == 0) {
        cprintf("exec: failed to set up user stack\n");
        pmm::free_user_pgdir(user_pgdir);
        return -1;
    }

    TrapFrame tf{};
    tf.cs = USER_CS;
    tf.ss = USER_DS;
    tf.rflags = FL_IF;  // enable interrupts
    tf.rip = entry;     // binary entry point
    tf.rsp = user_rsp;  // user stack pointer

    MemoryDesc* mm = new MemoryDesc();
    mm->mmap_list.init();
    mm->pgdir = user_pgdir;
    mm->map_count = 0;

    int pid = sched::fork(0, 0, &tf);
    if (pid <= 0) {
        cprintf("exec: fork failed\n");
        delete mm;
        return -1;
    }

    TaskStruct* proc = sched::find_proc(pid);
    if (proc) {
        proc->memory = mm;
        strncpy(proc->name, path, sizeof(proc->name) - 1);
        proc->name[sizeof(proc->name) - 1] = '\0';
    }

    cprintf("exec: started user process '%s' (PID %d) entry=0x%lx rsp=0x%lx\n", path, pid, entry, user_rsp);
    return pid;
}

}  // namespace exec
