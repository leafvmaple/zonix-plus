#include "exec/exec.h"
#include "fs/vfs.h"
#include "mm/vmm.h"
#include "sched/sched.h"
#include "drivers/intr.h"
#include "lib/stdarg.h"
#include "lib/string.h"
#include <asm/mmu.h>
#include <base/elf.h>

extern "C" int vprintf(const char*, va_list);
extern "C" void* aligned_alloc(size_t, size_t);
extern "C" void free(void*);
extern "C" [[noreturn]] void exit(int);

namespace {

// Host substitutes exercise the real exec/ELF implementation without kernel hooks.
class ExecFixture {
public:
    ExecFixture() { current_ = this; }
    ~ExecFixture() { current_ = nullptr; }
    static ExecFixture& current() { return *current_; }
    void set_file(vfs::File& file) { file_ = &file; }
    vfs::File* file() { return file_; }
    void fail_allocation(int allocation) { fail_allocation_ = allocation; }
    void fail_fork() { fork_error_ = Error::Busy; }
    pde_t* kernel_root() { return kernel_root_; }
    void* allocate(size_t size) {
        if (++allocations_ == fail_allocation_) {
            return nullptr;
        }
        void* ptr = aligned_alloc(PG_SIZE, (size + PG_SIZE - 1) / PG_SIZE * PG_SIZE);
        assert(ptr);
        ++live_;
        return ptr;
    }
    void deallocate(void* ptr) {
        if (ptr) {
            --live_;
            free(ptr);
        }
    }
    Page* map(uintptr_t va, uint32_t perm) {
        void* bytes = allocate(PG_SIZE);
        if (!bytes) {
            return nullptr;
        }
        assert(mapped_ < 6);
        const int index = mapped_++;
        data_[index] = bytes;
        addresses_[index] = va;
        entries_[index] = make_pte_page(virt_to_phys(bytes), perm);
        return &pages_[index];
    }
    pte_t* entry(uintptr_t va) {
        va -= va % PG_SIZE;
        for (int i = 0; i < mapped_; ++i) {
            if (addresses_[i] == va) {
                return &entries_[i];
            }
        }
        return nullptr;
    }
    uintptr_t physical(Page* page) { return virt_to_phys(data_[page - pages_]); }
    void free_root(pde_t* root) {
        for (int i = 0; i < mapped_; ++i) {
            deallocate(data_[i]);
        }
        mapped_ = 0;
        ++freed_roots_;
        deallocate(root);
    }
    void enter_guard() { ++guards_; }
    void leave_guard() { --guards_; }
    bool guarded() const { return guards_ != 0; }
    void setup_frame(uintptr_t entry, uintptr_t stack) {
        assert(entry == 0x400000 && stack == USER_STACK_TOP);
        frame_ready_ = true;
    }
    Result<int> fork() {
        assert(guards_ == 1 && frame_ready_);
        ++forks_;
        if (fork_error_ != Error::None) {
            return fork_error_;
        }
        child_.memory = &vmm::Manager::kernel_mm();
        return 17;
    }
    Task* child(int pid) {
        assert(guards_ == 1 && pid == 17);
        return &child_;
    }
    void close_file() { ++closed_; }
    void verify_failure(Error expected, Result<int>& result, int closed = 1) {
        assert(!result.ok() && result.error() == expected);
        assert(live_ == 0 && mapped_ == 0 && guards_ == 0 && closed_ == closed);
        assert(!child_.memory);
    }
    void verify_success(Result<int>& result) {
        assert(result.ok() && result.value() == 17);
        assert(forks_ == 1 && guards_ == 0 && closed_ == 1 && freed_roots_ == 0);
        assert(child_.memory && child_.memory != &vmm::Manager::kernel_mm());
        assert(live_ == 8 && mapped_ == 6);
        const auto* bytes = static_cast<const uint8_t*>(data_[0]);
        assert(bytes[0] == 0x5a && bytes[7] == 0x5a && bytes[8] == 0);
        delete child_.memory;
        child_.memory = nullptr;
        assert(live_ == 0 && mapped_ == 0 && freed_roots_ == 1);
    }
    void verify_fork_failure() { assert(forks_ == 1 && freed_roots_ == 1); }

private:
    inline static ExecFixture* current_{};
    alignas(PG_SIZE) pde_t kernel_root_[PAGE_TABLE_ENTRIES]{};
    Page pages_[6]{};
    void* data_[6]{};
    uintptr_t addresses_[6]{};
    pte_t entries_[6]{};
    Task child_{};
    vfs::File* file_{};
    Error fork_error_{Error::None};
    int allocations_{};
    int fail_allocation_{};
    int live_{};
    int mapped_{};
    int freed_roots_{};
    int guards_{};
    int forks_{};
    int closed_{};
    bool frame_ready_{};
};

class ExecutableFile : public vfs::File {
public:
    void invalidate() { invalid_ = true; }
    Error stat(vfs::Stat* st) override {
        st->set(vfs::NodeType::File, 512, 0);
        return Error::None;
    }
    Result<int> read(void* buf, size_t size, size_t) override {
        assert(size == 512);
        memset(buf, 0, size);
        auto* eh = static_cast<ElfHeader*>(buf);
        eh->e_magic = invalid_ ? 0 : ELF_MAGIC;
        eh->e_elf[0] = 2;
        eh->e_type = 2;
        eh->e_machine = EM_CURRENT;
        eh->e_version = 1;
        eh->e_entry = 0x400000;
        eh->e_phoff = sizeof(ElfHeader);
        eh->e_phentsize = sizeof(ProgramHeader);
        eh->e_phnum = 1;
        eh->e_ehsize = sizeof(ElfHeader);
        auto* ph = reinterpret_cast<ProgramHeader*>(static_cast<uint8_t*>(buf) + eh->e_phoff);
        ph->p_type = ELF_PT_LOAD;
        ph->p_flags = ELF_PF_R | ELF_PF_X;
        ph->p_offset = 256;
        ph->p_va = eh->e_entry;
        ph->p_filesz = 8;
        ph->p_memsz = PG_SIZE + 16;
        memset(static_cast<uint8_t*>(buf) + ph->p_offset, 0x5a, ph->p_filesz);
        return static_cast<int>(size);
    }
    Result<int> write(const void*, size_t, size_t) override { return Error::NotSupported; }

private:
    bool invalid_{};
};

}  // namespace

int cprintf(const char* format, ...) {
    assert(!ExecFixture::current().guarded());
    va_list args;
    va_start(args, format);
    int written = vprintf(format, args);
    va_end(args);
    return written;
}

void panic_at(const char*, int, const char*, ...) {
    exit(77);
}
void* kmalloc(size_t size) {
    return ExecFixture::current().allocate(size);
}
void kfree(void* ptr) {
    ExecFixture::current().deallocate(ptr);
}
void* operator new(size_t size, const sys::nothrow_t&) noexcept {
    return kmalloc(size);
}
void operator delete(void* ptr) noexcept {
    kfree(ptr);
}
void operator delete(void* ptr, size_t) noexcept {
    kfree(ptr);
}

namespace pmm {
Page* alloc_and_map_page(pde_t*, uintptr_t va, uint32_t perm) {
    return ExecFixture::current().map(va, perm);
}
pte_t* get_pte(pde_t*, uintptr_t va, bool) {
    return ExecFixture::current().entry(va);
}
uintptr_t page_to_phys(Page* page) {
    return ExecFixture::current().physical(page);
}
void free_user_pgdir(pde_t* root) {
    ExecFixture::current().free_root(root);
}
}  // namespace pmm

namespace vmm {
int init() {
    Manager::kernel_mm_.pgdir = ExecFixture::current().kernel_root();
    return 0;
}
}  // namespace vmm

namespace intr {
Guard::Guard() {
    ExecFixture::current().enter_guard();
}
Guard::~Guard() {
    ExecFixture::current().leave_guard();
}
}  // namespace intr

namespace sched {
Result<int> fork(uint32_t, uintptr_t, TrapFrame*) {
    return ExecFixture::current().fork();
}
Task* find_process(int pid) {
    return ExecFixture::current().child(pid);
}
}  // namespace sched

void Task::set_name(const char*) {}
void arch_setup_user_tf(TrapFrame*, uintptr_t entry, uintptr_t stack) {
    ExecFixture::current().setup_frame(entry, stack);
}

namespace vfs {
File::~File() = default;
Error open(const char* path, File** out) {
    // The file object lives in main; closing records ownership without deleting the fixture.
    if (strcmp(path, "file") != 0) {
        return Error::NotFound;
    }
    assert(*out == nullptr);
    *out = ExecFixture::current().file();
    return Error::None;
}
Result<int> read(File* file, void* buf, size_t size, size_t offset) {
    return file->read(buf, size, offset);
}
void close(File*) {
    ExecFixture::current().close_file();
}
}  // namespace vfs

extern "C" int main(int argc, char** argv) {
    assert(argc == 2);
    ExecFixture fixture;
    ExecutableFile file;
    fixture.set_file(file);
    assert(vmm::init() == 0);
    if (strcmp(argv[1], "fork") == 0) {
        fixture.fail_fork();
    } else if (strcmp(argv[1], "bad-elf") == 0) {
        file.invalidate();
    } else if (strcmp(argv[1], "missing") == 0) {
        // No allocation failure: VFS refuses the requested path.
    } else if (strcmp(argv[1], "success") != 0) {
        fixture.fail_allocation(argv[1][0] - '0');
    }
    auto result = exec::exec(strcmp(argv[1], "missing") == 0 ? "missing" : "file");
    if (strcmp(argv[1], "success") == 0) {
        fixture.verify_success(result);
    } else if (strcmp(argv[1], "fork") == 0) {
        fixture.verify_failure(Error::Busy, result);
        fixture.verify_fork_failure();
    } else if (strcmp(argv[1], "bad-elf") == 0) {
        fixture.verify_failure(Error::Invalid, result);
    } else if (strcmp(argv[1], "missing") == 0) {
        fixture.verify_failure(Error::NotFound, result, 0);
    } else {
        fixture.verify_failure(Error::NoMem, result);
    }
    cprintf("[OK] exec ownership and errors: %s\n", argv[1]);
    return 0;
}
