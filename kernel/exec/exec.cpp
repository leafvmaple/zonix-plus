#include "mm/page_table.h"
#include "exec.h"
#include "elf_loader.h"
#include "fs/vfs.h"

#include "lib/stdio.h"
#include "lib/memory.h"
#include "lib/string.h"
#include "lib/math.h"
#include "lib/kernel_buffer.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "sched/sched.h"
#include "drivers/intr.h"
#include "debug/assert.h"

#include <asm/page.h>
#include <asm/mmu.h>

static_assert(USER_STACK_TOP < USER_SPACE_TOP && USER_STACK_TOP % PG_SIZE == 0);
static_assert(USER_STACK_TOP > USER_STACK_SIZE + PG_SIZE);

namespace exec {

Result<pde_t*> create_user_pgdir() {
    auto* pgdir = static_cast<pde_t*>(kmalloc(PG_SIZE));
    ENSURE(pgdir, Error::NoMem);

    memset(pgdir, 0, PG_SIZE);
    // Copy higher-half kernel mappings (top-level entries USER_TOP_ENTRIES..PAGE_TABLE_ENTRIES-1)
    memcpy(&pgdir[USER_TOP_ENTRIES], &vmm::Manager::kernel_pgdir()[USER_TOP_ENTRIES],
           (PAGE_TABLE_ENTRIES - USER_TOP_ENTRIES) * sizeof(pde_t));

    return pgdir;
}

Result<uintptr_t> setup_user_stack(MemoryDesc& memory, const Arguments& args) {
    auto* pgdir = memory.pgdir;
    ENSURE(pgdir);
    uintptr_t user_stack_bottom_va = USER_STACK_TOP - USER_STACK_SIZE;

    for (uintptr_t va = user_stack_bottom_va; va < USER_STACK_TOP; va += PG_SIZE) {
        Page* page = pmm::alloc_and_map_page(pgdir, va, VM_USER_RW);
        ENSURE(page, Error::NoMem);

        memset(phys_to_virt(pmm::page_to_phys(page)), 0, PG_SIZE);
    }

    const uintptr_t strings = USER_STACK_TOP - args.byte_count();
    const uintptr_t stack = round_down(strings - (args.size() + 3) * sizeof(uintptr_t), 16);
    if (args.byte_count())
        TRY(vmm::copy_to_user(&memory, strings, args.data(), args.byte_count()));
    const uintptr_t argc = args.size();
    TRY(vmm::copy_to_user(&memory, stack, &argc, sizeof(argc)));
    for (size_t i = 0; i < args.size(); ++i) {
        const uintptr_t pointer = strings + args.offset(i);
        TRY(vmm::copy_to_user(&memory, stack + (i + 1) * sizeof(pointer), &pointer, sizeof(pointer)));
    }
    const uintptr_t nulls[2]{};  // argv[argc], followed by an empty environment.
    TRY(vmm::copy_to_user(&memory, stack + (args.size() + 1) * sizeof(uintptr_t), nulls, sizeof(nulls)));
    return stack;
}

Error Arguments::add(const char* text) {
    ENSURE(text, Error::Invalid);
    ENSURE(offsets_.size() < MAX_ARGS, Error::Full);
    size_t length{};
    while (length < bytes_.size() - used_ && text[length] != '\0')
        ++length;
    ENSURE(length < bytes_.size() - used_, Error::Full);
    assert(offsets_.try_push_back(used_));
    memcpy(bytes_.data() + used_, text, length + 1);
    used_ += length + 1;
    return Error::None;
}

// Own the prepared address space until the child takes it under the interrupt guard.
class UserImage {
public:
    static Result<UserImage> load(const uint8_t* data, size_t size, const Arguments& args) {
        UserImage image;
        image.memory_ = sys::unique_ptr<MemoryDesc>(new (sys::nothrow) MemoryDesc());
        ENSURE(image.memory_, Error::NoMem);
        image.memory_->pgdir = TRY(create_user_pgdir());
        image.entry_va_ = TRY(elf::load(data, size, image.memory_->pgdir));
        image.stack_va_ = TRY(setup_user_stack(*image.memory_, args));
        return image;
    }

    UserImage(const UserImage&) = delete;
    UserImage& operator=(const UserImage&) = delete;
    UserImage(UserImage&&) noexcept = default;
    UserImage& operator=(UserImage&&) noexcept = default;

    uintptr_t entry_va() const { return entry_va_; }
    uintptr_t stack_va() const { return stack_va_; }
    [[nodiscard]] sys::unique_ptr<MemoryDesc> take_memory() { return sys::move(memory_); }

private:
    UserImage() = default;
    sys::unique_ptr<MemoryDesc> memory_;
    uintptr_t entry_va_{};
    uintptr_t stack_va_{};
};

Result<int> spawn(const char* path, const Arguments& args) {
    ENSURE(path);

    auto file = TRY(vfs::open(path));

    vfs::Stat st{};
    TRY(file->stat(&st));

    ENSURE(st.type != vfs::NodeType::Directory);

    uint32_t file_size = st.size;
    ENSURE(file_size > 0 && file_size <= MAX_BINARY_SIZE);

    auto buf = TRY(KernelBuffer::alloc(file_size));

    int bytes_read = TRY(vfs::read(file.get(), buf.data(), buf.size(), 0));
    ENSURE(bytes_read == static_cast<int>(file_size), Error::Io);

    file.reset();
    auto image = TRY(UserImage::load(buf.data(), buf.size(), args));
    const uintptr_t entry_va = image.entry_va();
    const uintptr_t user_stack_va = image.stack_va();

    return TaskManager::spawn_user(image.take_memory(), entry_va, user_stack_va, path);
}

Result<int> exec(const char* path) {
    ENSURE(path, Error::Invalid);
    auto args = sys::unique_ptr<Arguments>(new (sys::nothrow) Arguments());
    ENSURE(args, Error::NoMem);
    TRY(args->add(path));
    return spawn(path, *args);
}

}  // namespace exec
