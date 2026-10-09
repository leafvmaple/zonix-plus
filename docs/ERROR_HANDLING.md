# Error and Result contract

`kernel/lib/result.h` provides the common internal error vocabulary and result
container. It works with the kernel's freestanding C++20 configuration without
exceptions, RTTI, a standard library dependency or container-side allocation.

## Choosing a return type

- Use `Error` when an operation returns only a status. `Error::None` means success.
- Use `Result<T>` when success returns a value and failure returns an `Error`.
  Zero, null and other sentinel-looking values are still successful values;
  each operation must document whether they have a special domain meaning.
- `Result<void>` remains supported as a status wrapper and works with `TRY`.
  Prefer `Error` for new status-only kernel interfaces.
- Keep externally specified syscall, firmware and assembly return conventions.
  Translate internal errors at the boundary; do not change an ABI as part of an
  internal return-type migration.

The existing `Error` numeric values and `error_str()` strings are unchanged.
Subsystem migrations keep those codes and migrate a complete caller/callee
chain together. The first migrated chain is ELF loading and process creation.

## ELF and exec

`elf::load()` and `exec::setup_user_stack()` return `Result<uintptr_t>`;
`exec::create_user_pgdir()` returns `Result<pde_t*>`. A failed operation returns
an error rather than a null pointer or zero address. The caller owns the root
and any pages mapped before a failed load or stack setup; attach the root to
an owning `MemoryDesc` so teardown reclaims partial mappings as well.

`exec::exec()` preserves errors from opening, stat, reading, ELF validation,
allocation and process creation. Malformed binaries, directories and invalid
file sizes return `Invalid`; allocation failure returns `NoMem`. A successful
read whose byte count differs from the requested size returns `Io`.

The private `UserImage` in `exec.cpp` owns a prepared `MemoryDesc`. It cannot be
copied; moving it transfers ownership. Its destructor frees the address space
on failure. After a successful fork, exec transfers the memory to the child
while interrupts remain disabled. File handles and the input buffer also use
local destructors for every return path.

## Resource ownership

The reusable owners use the project's GNU++20 freestanding configuration:

| Type | Owns | Borrow | Transfer |
| --- | --- | --- | --- |
| `sys::unique_ptr<T, Deleter>` from zstl | One object with its cleanup policy | `get()`, `*`, `->` | Move, or `release()` into another owner |
| `KernelBuffer` | `kmalloc` bytes and their requested length | `data()`, `size()`; const access gives const bytes | Move the complete buffer |
| `vfs::FileHandle` | One open VFS file, closed through `vfs::close` | `get()`, `*`, `->` | Move into `fd::Table::alloc` |
| `Task` | Its dynamic kernel stack, file table and adopted user MM | `kernel_stack()`, `files()`, `memory()` | Publish after preparation; reap through `wait()` |

These types are noncopyable. Moves do not allocate and leave the source empty;
move assignment first cleans up the destination's old resource. Destruction and
reset clean up an owned resource once. The stateless `sys::unique_ptr` deleters used
by the kernel take no extra pointer space. zstl also supplies array ownership and
stateful deleters; this migration uses single-object ownership and stateless policies.
`KernelBuffer` owns raw bytes, not constructed C++ elements. Allocating zero bytes
succeeds with null data and zero length; allocation failure returns `NoMem`.

Construct an owner from a raw pointer only when the caller already holds ownership. Raw
pointers/references passed to ordinary operations borrow by default. Borrowing
does not extend lifetime; discard borrowed pointers before the owner is reset,
destroyed or its resource is handed off. `sys::unique_ptr` follows standard borrowing
semantics, including access during a temporary's full expression; do not retain those
pointers after the expression. `KernelBuffer::data()` rejects temporary borrowing.
`release()` is `[[nodiscard]]`: immediately attach its returned pointer
to the receiving owner. Prefer moving the owner when the receiving API supports
it. `vfs::open(path)`, `FileSystem::open(relpath)` and `CharDevFactory` return
`Result<FileHandle>`: success transfers a new, nonempty file owner, while error
results carry no file. Backend/device factories retain local owners for partial
resources, so errors and `TRY` clean them before returning. The legacy
two-argument VFS open API is an adapter over the owning API; it returns an owned
raw pointer on success and clears the output on failure.

VFS path resolution returns an owning result with a scoped mount reference and a
borrowed relative path. The reference owns one use count, not the permanently
stored mount slot: its zstl custom deleter decrements that count. Path operations,
directory visitor callbacks and filesystem printing hold this reference through
completion. A successful open transfers it to the returned file; the base file
destructor releases it after the derived destructor finishes. Unmount returns
`Busy` while any operation or file holds a reference. These counts follow the
kernel's current single-CPU interrupt-guard synchronization model.

The operation's mount reference remains alive throughout backend execution,
including destruction of partial file owners on failure. VFS preserves the
original filesystem/device error. A backend or device factory claiming success
without a file returns `Io`.
Unknown filesystem types or mount points return `NotFound`; factory allocation
failure returns `NoMem`; an already mounted slot returns `Exists`. A transition
reserves the slot across mount initialization or unmount/destruction and returns
`Busy` to competing mount/unmount operations. Initialization failure destroys the
unpublished filesystem before releasing the reservation. Filesystem callbacks
run outside the VFS interrupt guard; publishing, detaching and count updates use
short guarded sections. This protects lifetime, not concurrent filesystem data
updates or references retained by callers beyond an operation.

devfs owns its registry in private static state. Registration rejects empty names,
duplicates and exhaustion with `Invalid`, `Exists` and `Full`. Names and factory
function pointers are borrowed for the kernel's lifetime; registration does not
copy name storage. Readers take a bounded registry snapshot under the interrupt
guard, then invoke factories and directory visitors outside it. Devices registered
by a visitor become visible to the next traversal, not partway through the current
one. Factory errors retain their original code, including failures other than
allocation exhaustion.

Architecture entry code delegates C++ constructor execution to the common
`kern_init` path. Each registrar runs once; registration must not rely on accepting
duplicates to hide repeated startup constructors. The device duplicate check
also makes repeated registration fail explicitly instead of consuming capacity.

`Task` and `MemoryDesc` cannot be copied or moved: their intrusive list links must
keep stable addresses, and duplicating a page-table owner would double-release it.
Move `sys::unique_ptr<MemoryDesc>` instead of the descriptor itself. `Task::memory()`
is a borrowed view; `adopt_memory` receives an owner once, on an unpublished or
non-running child. Exec performs that handoff under its interrupt guard before the
child can run. Current tasks cannot adopt or release their live address space.
Kernel tasks explicitly borrow the permanent kernel MM. Idle borrows the assembly
boot stack; ordinary tasks own a `KernelBuffer` stack. Repeated stack setup returns
`Busy` without replacing the existing stack.

Task creation retains a local `sys::unique_ptr<Task>` through file-table, standard
file and stack preparation. Errors preserve their original code and let this owner
reclaim the partial task. PID assignment, linking, wakeup and ownership publication
occur under one interrupt guard. After publication, the process table owns the task;
exit publishes zombie state and reparents children, and wait detaches the zombie
before destroying it. Destruction checks that the task is off CPU and detached,
then releases files, dynamic stack and owned user MM in that order. Destructors do
not remove scheduler links or free borrowed resources.

`fd::Table::alloc(FileHandle)` consumes its argument on both success and failure.
Success moves the file into the descriptor entry; failure closes it. Entry lookup
borrows from the table. Closing, resetting or destroying the table invalidates
those borrows. Tables cannot be copied or moved; `ForkPolicy::Share` remains
`NotSupported` until shared open-file ownership is actually implemented.

Use fallible factories plus `TRY` so a successful result contains a ready resource,
and early returns clean up existing owners. Keep special resource protocols in
their subsystem: `UserImage` uses a `sys::unique_ptr<MemoryDesc>` and hands it to the child
under the interrupt guard; mapped pages belong to the address-space teardown,
and the boot page tables remain borrowed permanent storage.

Destructors are for non-failing release operations. Operations such as filesystem
unmount, disk write rollback or restoring mappings can fail and require an explicit
error policy. Keep those checks and their ordering visible; do not hide them in a
generic destructor. Use `constexpr` for real constant properties, `static_assert`
for type/layout contracts, and `[[nodiscard]]` for ownership-producing operations
and transfers. Add views only when pointer/length or iteration duplication needs
an explicit borrowed interface; a view must not imply ownership.

zstl is the project-level submodule at `external/zstl`. Kernel builds and host
contract tests use `ZSTL_FREESTANDING` with its include directory and no host C/C++
headers. `sys/new.hpp` supplies shared placement-new definitions and `sys::nothrow`;
`kernel/cxxrt.cpp` supplies allocating new/delete implementations. Recoverable
allocations use `new (sys::nothrow)` and check null. Ordinary new is fail-fast on
exhaustion, so compiler-generated constructors never run on a null allocation.
Future mini-cocos integration must consume this same zstl include root and build
mode, rather than introduce a second STL copy or a separate allocation tag type.
The pre-commit snapshot exports zstl at the staged gitlink revision, so local
uncommitted library edits cannot hide an incompatible pinned dependency.

## States and ownership

| State | `ok()` | `is_consumed()` | Permitted access |
| --- | --- | --- | --- |
| Value | true | false | `value()`, `value_or()`, `error()` returns None, release value |
| Error | false | false | `error()`, `value_or()`, release error |
| Consumed | false | true | State queries, destruction, assignment |

`Result<T>` constructs `T` only on success. Failure therefore does not require
a default constructor or create a resource. Constructing `Result<T>` from
`Error::None` is an assertion failure: a successful value must actually exist.
`Result<void>` has no value to construct, so both its default constructor and
`Error::None` represent success.

`value()` borrows a reference. It requires a successful lvalue result; access
on a temporary is deleted to prevent dangling references. The reference stays
valid only while that same value remains alive in the result.

`release_value()` returns the value by move construction, destroys the stored
moved-from value and consumes the result. `release_error()` requires an error,
returns it and consumes the result. Invalid access, including a second release
or accessing a consumed result, invokes the kernel assertion handler. These
assertions diagnose programming errors, not recoverable operation failures.

A result is copyable only if `T` is copy-constructible. Copying preserves the
source. Trivially copyable values (integers, pointers, enums and plain structs)
use default copying and moving: moving preserves the source. Nontrivial values
use explicit ownership transfer, and moving consumes the source. `Result<void>`
also uses default copying/moving. Explicit release consumes every result type.

Trivial payloads keep trivial copy/move construction and destruction whenever
those operations are available. Assignment is defaulted when the payload has
the corresponding trivial assignment operation; otherwise it reconstructs the
value, so `T` need not be assignable. Nontrivial payloads retain lifetime cleanup
and consuming moves. Self-copy and self-move preserve the state. Destruction
destroys a nontrivially destructible live value exactly once.
Values must be unqualified object types that can be constructed from an rvalue;
use pointers for borrowed objects and `Error` directly rather than `Result<Error>`.

`value_or(fallback)` on an lvalue or const result returns a copy and leaves the
result unchanged; this overload requires copy construction. On a mutable rvalue it returns the stored value or fallback
by move and consumes the result. The fallback argument is evaluated eagerly
in both cases.

## Propagation

`TRY(expr)` accepts `Error`, `Result<void>` and `Result<T>`. It evaluates `expr`
once, returns the original error from the enclosing function on failure, and
extracts the value on success. Status-only success has a void result.
`TRY_LOG` follows the same rules and logs on failure; its caller must provide
the logging declaration. Both macros use the existing GNU statement-expression
extension supported by the kernel toolchain.

```cpp
Result<Buffer> load_buffer();
Error write_buffer(const Buffer& buffer);

Error copy_buffer() {
    auto buffer = TRY(load_buffer());
    TRY(write_buffer(buffer));
    return Error::None;
}
```

Propagation owns a temporary result. Passing a copyable lvalue copies it;
explicitly moving a nontrivial lvalue transfers ownership and consumes it. Moving
a trivial value preserves its source, even when its type forbids copying. A move-only
lvalue must be moved explicitly. Do not reuse a consumed result until assigning
a new value or error to it. Ensure any acquired resources use destructors so
early propagation releases them.

## Checking returned errors

Both `Error` and `Result` are `[[nodiscard]]` types. Check, propagate or explicitly
discard a returned error. An explicit `(void)` discard needs a reason, such as
best-effort cleanup after the primary failure; it must not hide a required
operation or leave a resource owned by nobody. Production kernel compilation
treats ignored returned errors as errors with `-Werror=unused-result`.

`[[nodiscard]]` checks discarded return expressions. It does not prove that an
error saved in a local variable is subsequently handled. Keep the check next
to the call, or pass/return the result to code that owns the decision. The
consumed-state assertions detect invalid reuse, not unhandled errors; ordinary
error destruction does not panic.

## Logging rules

- Use `TRY` for routine propagation. Pure forwarding layers do not print.
- Use `ENSURE` for a recoverable precondition failure; choose an explicit code
  when `Invalid` is inaccurate. `assert` diagnoses an internal invariant failure.
- Expected outcomes (missing optional files, retryable busy states, probing an
  unsupported device) return an error without a fault log. Log only if the caller
  decides the outcome fails the requested operation.
- Use `TRY_LOG`/`ENSURE_LOG` where the message adds context: path, device, sector,
  virtual address or operation. Do not repeat the same generic failure at every
  layer. A root-cause message and each useful semantic boundary may contribute
  distinct information; the handling boundary prints the final operation summary.
- Keep the original error while adding context. Translate it only at a documented
  external ABI boundary; context must not replace it with a generic `Fail`.
- New fault messages should identify the subsystem, operation, useful context and
  error code/name. Existing logging macros currently print caller-supplied text;
  automatic source locations and propagation records are a future extension.
- Recovery/retry loops log the terminal failure once, rather than each attempt.
  Repeated faults need bounded/rate-limited reporting before adding per-attempt logs.
- In IRQ, allocator and lock-sensitive paths, return/record through a suitable
  nonblocking facility or defer output to a safe boundary. Do not add allocation
  or blocking console output merely to describe an error.
- Complete propagation tracing is an optional debug facility. A future trace must
  record source file, line, function, expression, code and operation/task identity,
  with bounded storage owned by its task/operation. Define reset, recovery, nested
  operation and interrupt boundaries before implementing it. Normal propagation
  remains allocation-free and does not acquire an implicit global trace buffer.

The exec/ELF validation and allocation chain returns errors silently. The shell
reports a failed execution with the resolved path and original code/name; syscall
callers receive the error directly. In particular, fork failure does not print
while holding the interrupt guard. Successful loader diagnostics are unchanged.

FAT directory/file helpers propagate device codes unchanged, and expected
`NotEmpty`/`Full` refusals do not print. The filesystem shell commands report the
operation, resolved path and original code/name. Rollback failures are secondary
and may print their own distinct context; they never overwrite the primary error.

Initialize an unpublished directory extension before linking it. Reclaim it only
after detachment is confirmed. A failed device write can have partially taken
effect: if publication or tail restoration is uncertain, retain any possibly
referenced clusters and report the uncertainty. This favors an allocated orphan
over a dangling reference to a freed cluster. It does not provide transactional
FAT updates or power-loss recovery; those require a separate filesystem design.

## Verification

`kernel/test/unit/lib/result_test.cpp` exercises state transitions, copying,
moving, resource lifetimes, fallback extraction and propagation in the kernel.
`kernel/test/host/result_contract_test.cpp` supplies the host test entry point
and assertion handler. `scripts/tests/test_result.py` runs that suite, verifies
invalid accesses terminate, and checks compilation rejects borrowing a
temporary or ignoring a result. The normal harness test discovery includes it.
Test entry points and assertion substitutes remain under `kernel/test`.

The ELF/exec kernel tests cover mappings, BSS/stack zeroing, error propagation,
file closure and physical page recovery. `scripts/tests/test_exec.py` compiles
the actual exec and ELF loader with host substitutes under `kernel/test/host`.
It disables copy elision and checks each allocation failure, fork failure and
successful transfer of the address space to the child. These substitutes do
not alter production interfaces or add test hooks to kernel code.

`scripts/tests/test_vfs.py` compiles the real VFS with host filesystem fixtures
under `kernel/test/host`. Reentrant unmount attempts exercise operation, visitor
and file-destructor lifetimes; competing mount calls exercise slot reservations
during initialization and teardown. Allocation/backend failures verify cleanup,
retryability and original error propagation, with copy elision disabled. The same
suite links the real devfs and checks device-factory failures, empty owners,
duplicate/full registries and registration during a directory visitor callback.

The exec host tests also reject console output inside the interrupt guard and
check missing/invalid binaries produce no fault logs in propagation helpers.
`kernel/test/unit/fs/fat_validation_test.cpp` injects distinct read/write errors,
partial writes and secondary rollback failures. It verifies primary codes,
reclamation of unpublished clusters and retention of possibly published ones.
