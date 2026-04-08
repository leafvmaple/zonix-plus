#include "exec.h"
#include "elf_loader.h"
#include "fs/vfs.h"

#include "lib/stdio.h"
#include "lib/memory.h"
#include "lib/string.h"
#include "lib/math.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "sched/sched.h"
#include "drivers/intr.h"
#include "debug/assert.h"

#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/arch.h>

extern pde_t* boot_pgdir;

namespace exec {

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

struct OpenFile {
    vfs::File* handle = nullptr;

    OpenFile() = default;

    ~OpenFile() {
        if (handle != nullptr) {
            vfs::close(handle);
        }
    }

    OpenFile(const OpenFile&) = delete;
    OpenFile& operator=(const OpenFile&) = delete;
};

pde_t* create_user_pgdir() {
    auto* pgdir = static_cast<pde_t*>(kmalloc(PG_SIZE));
    if (!pgdir) {
        cprintf("exec: failed to allocate page for PML4\n");
        return nullptr;
    }

    memset(pgdir, 0, PG_SIZE);
    // Copy higher-half kernel mappings (top-level entries USER_TOP_ENTRIES..PAGE_TABLE_ENTRIES-1)
    memcpy(&pgdir[USER_TOP_ENTRIES], &boot_pgdir[USER_TOP_ENTRIES],
           (PAGE_TABLE_ENTRIES - USER_TOP_ENTRIES) * sizeof(pde_t));

    return pgdir;
}

uintptr_t setup_user_stack(pde_t* pgdir) {
    uintptr_t stack_bottom = USER_STACK_TOP - USER_STACK_SIZE;

    for (uintptr_t va = stack_bottom; va < USER_STACK_TOP; va += PG_SIZE) {
        Page* page = pmm::pgdir_alloc_page(pgdir, va, VM_USER_RW);
        if (!page) {
            cprintf("exec: failed to allocate user stack page at 0x%lx\n", va);
            return 0;
        }

        memset(phys_to_virt(pmm::page_to_phys(page)), 0, PG_SIZE);
    }

    return USER_STACK_TOP;
}

static uintptr_t load_binary(const uint8_t* data, size_t size, pde_t* pgdir) {
    if (elf::is_elf(data, size)) {
        return elf::load(data, size, pgdir);
    }

    cprintf("exec: unrecognised binary format (magic: %02x %02x %02x %02x)\n", size > 0 ? data[0] : 0,
            size > 1 ? data[1] : 0, size > 2 ? data[2] : 0, size > 3 ? data[3] : 0);
    return 0;
}

Result<int> exec(const char* path) {
    ENSURE(path);

    OpenFile file;
    if (vfs::open(path, &file.handle) != Error::None || file.handle == nullptr) {
        cprintf("exec: file not found: %s\n", path);
        return Error::NotFound;
    }

    vfs::Stat st{};
    TRY_LOG(file.handle->stat(&st), "exec: failed to stat file: %s", path);

    ENSURE_LOG(st.type != vfs::NodeType::Directory, Error::Invalid, "exec: cannot execute directory: %s", path);

    uint32_t file_size = st.size;
    ENSURE_LOG(file_size > 0, Error::Invalid, "exec: empty file: %s", path);
    ENSURE_LOG(file_size <= MAX_BINARY_SIZE, Error::Invalid, "exec: file too large (%d bytes, max %d): %s", file_size,
               MAX_BINARY_SIZE, path);

    KernelBuf buf;
    ENSURE_LOG(buf.alloc(file_size), Error::NoMem, "exec: failed to allocate kernel buffer for file: %s", path);

    auto rd = vfs::read(file.handle, buf.ptr, file_size, 0);
    if (!rd.ok() || rd.value() < static_cast<int>(file_size)) {
        cprintf("exec: failed to read file (%d/%d bytes)\n", rd.ok() ? rd.value() : -1, file_size);
        return Error::IO;
    }

    pde_t* user_pgdir = create_user_pgdir();
    if (!user_pgdir) {
        cprintf("exec: failed to create user page directory\n");
        return Error::NoMem;
    }

    uintptr_t entry = load_binary(buf.ptr, file_size, user_pgdir);
    if (entry == 0) {
        cprintf("exec: failed to load binary\n");
        pmm::free_user_pgdir(user_pgdir);
        return Error::Fail;
    }

    uintptr_t user_rsp = setup_user_stack(user_pgdir);
    if (user_rsp == 0) {
        cprintf("exec: failed to set up user stack\n");
        pmm::free_user_pgdir(user_pgdir);
        return Error::NoMem;
    }

    TrapFrame tf{};
    arch_setup_user_tf(&tf, entry, user_rsp);

    MemoryDesc* mm = new MemoryDesc();
    mm->pgdir = user_pgdir;
    mm->map_count = 0;

    int pid{};
    {
        intr::Guard guard;
        auto pid_r = sched::fork(0, user_rsp, &tf);
        if (!pid_r.ok()) {
            cprintf("exec: fork failed\n");
            delete mm;
            return pid_r.error();
        }
        pid = pid_r.value();

        TaskStruct* proc = sched::find_proc(pid);
        if (proc) {
            proc->memory = mm;
            proc->set_name(path);
        }
    }

    cprintf("exec: started user process '%s' (PID %d) entry=0x%lx rsp=0x%lx\n", path, pid, entry, user_rsp);
    return pid;
}

}  // namespace exec
