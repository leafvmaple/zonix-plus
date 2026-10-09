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

static_assert(USER_STACK_TOP < USER_SPACE_TOP && USER_STACK_TOP % PG_SIZE == 0);
static_assert(USER_STACK_TOP > USER_STACK_SIZE + PG_SIZE);

namespace exec {

class KernelBuf {
public:
    KernelBuf() = default;
    ~KernelBuf() { kfree(ptr_); }

    Error alloc(size_t bytes) {
        assert(!ptr_);
        ptr_ = static_cast<uint8_t*>(kmalloc(bytes));
        return ptr_ ? Error::None : Error::NoMem;
    }
    uint8_t* data() const { return ptr_; }

    // Non-copyable
    KernelBuf(const KernelBuf&) = delete;
    KernelBuf& operator=(const KernelBuf&) = delete;

private:
    uint8_t* ptr_{};
};

class OpenFile {
public:
    OpenFile() = default;

    ~OpenFile() {
        if (handle_ != nullptr) {
            vfs::close(handle_);
        }
    }

    Error open(const char* path) {
        assert(!handle_);
        return vfs::open(path, &handle_);
    }
    vfs::File* handle() const { return handle_; }

    OpenFile(const OpenFile&) = delete;
    OpenFile& operator=(const OpenFile&) = delete;

private:
    vfs::File* handle_{};
};

Result<pde_t*> create_user_pgdir() {
    auto* pgdir = static_cast<pde_t*>(kmalloc(PG_SIZE));
    ENSURE(pgdir, Error::NoMem);

    memset(pgdir, 0, PG_SIZE);
    // Copy higher-half kernel mappings (top-level entries USER_TOP_ENTRIES..PAGE_TABLE_ENTRIES-1)
    memcpy(&pgdir[USER_TOP_ENTRIES], &vmm::Manager::kernel_pgdir()[USER_TOP_ENTRIES],
           (PAGE_TABLE_ENTRIES - USER_TOP_ENTRIES) * sizeof(pde_t));

    return pgdir;
}

Result<uintptr_t> setup_user_stack(pde_t* pgdir) {
    ENSURE(pgdir);
    uintptr_t user_stack_bottom_va = USER_STACK_TOP - USER_STACK_SIZE;

    for (uintptr_t va = user_stack_bottom_va; va < USER_STACK_TOP; va += PG_SIZE) {
        Page* page = pmm::alloc_and_map_page(pgdir, va, VM_USER_RW);
        ENSURE(page, Error::NoMem);

        memset(phys_to_virt(pmm::page_to_phys(page)), 0, PG_SIZE);
    }

    return USER_STACK_TOP;
}

// Own the prepared address space until the child takes it under the interrupt guard.
class UserImage {
public:
    static Result<UserImage> load(const uint8_t* data, size_t size) {
        UserImage image;
        image.memory_ = new (std::nothrow) MemoryDesc();
        ENSURE(image.memory_, Error::NoMem);
        image.memory_->pgdir = TRY(create_user_pgdir());
        image.entry_va_ = TRY(elf::load(data, size, image.memory_->pgdir));
        image.stack_va_ = TRY(setup_user_stack(image.memory_->pgdir));
        return image;
    }

    UserImage(const UserImage&) = delete;
    UserImage& operator=(const UserImage&) = delete;
    UserImage(UserImage&& other) : memory_(other.memory_), entry_va_(other.entry_va_), stack_va_(other.stack_va_) {
        other.memory_ = nullptr;
    }
    ~UserImage() { delete memory_; }

    uintptr_t entry_va() const { return entry_va_; }
    uintptr_t stack_va() const { return stack_va_; }
    MemoryDesc* release_memory() {
        MemoryDesc* memory = memory_;
        memory_ = nullptr;
        return memory;
    }

private:
    UserImage() = default;
    MemoryDesc* memory_{};
    uintptr_t entry_va_{};
    uintptr_t stack_va_{};
};

Result<int> exec(const char* path) {
    ENSURE(path);

    OpenFile file;
    TRY(file.open(path));
    assert(file.handle());

    vfs::Stat st{};
    TRY(file.handle()->stat(&st));

    ENSURE(st.type != vfs::NodeType::Directory);

    uint32_t file_size = st.size;
    ENSURE(file_size > 0 && file_size <= MAX_BINARY_SIZE);

    KernelBuf buf;
    TRY(buf.alloc(file_size));

    int bytes_read = TRY(vfs::read(file.handle(), buf.data(), file_size, 0));
    ENSURE(bytes_read == static_cast<int>(file_size), Error::Io);

    auto image = TRY(UserImage::load(buf.data(), file_size));
    const uintptr_t entry_va = image.entry_va();
    const uintptr_t user_stack_va = image.stack_va();

    TrapFrame tf{};
    arch_setup_user_tf(&tf, entry_va, user_stack_va);

    int pid{};
    {
        intr::Guard guard;
        pid = TRY(sched::fork(0, user_stack_va, &tf));

        Task* proc = sched::find_process(pid);
        assert(proc);
        proc->memory = image.release_memory();
        proc->set_name(path);
    }

    cprintf("exec: started user process '%s' (PID %d) entry=0x%lx rsp=0x%lx\n", path, pid, entry_va, user_stack_va);
    return pid;
}

}  // namespace exec
