# Owner authorization for commits and pushes

- This is the owner's core project. Never create a Git commit or push unless
  the owner explicitly tells you to perform that specific operation for the
  current changes. Obtain authorization before executing the operation.
- Commit authorization does not authorize pushing. "Continue", "implement",
  "finish" or "organize" do not authorize either operation. Never carry an
  authorization from earlier work over to a new change or task.
- Prepare edits and verification locally, then leave the changes uncommitted
  for the owner's review until explicitly instructed to commit or push.

# Product architecture: complete CLI base and optional Desktop

- Zonix must be a complete command-line operating system without Desktop
  installed. The base system must boot into a usable user-space init and shell,
  run programs, manage processes and files, and provide diagnostics and orderly
  shutdown without GUI binaries, libraries, assets or services.
- Desktop is optional user-space software. Display composition, window policy,
  widgets, themes and graphical applications belong in user space. The kernel
  provides generic display, input, IPC and resource-management interfaces;
  neither the kernel nor CLI tools may depend on Desktop or a GUI engine.
  A framebuffer-backed text console is compatible with this separation.
- Base and Desktop images must use the same kernel; their difference is the
  installed user-space software and startup configuration. Do not introduce a
  Desktop-specific kernel implementation or require a graphical service to boot
  or operate the CLI system.
- Desktop must be startable from the CLI. Normal exit or failure must release
  its resources and restore usable console display and input. A graphical
  application failure must not take down the kernel or unrelated applications.
- Changes to graphical support must preserve the three operating modes:
  Desktop absent, Desktop running, and CLI use after Desktop exits. Verify them
  as the Testing section describes for user-space changes.

# User program toolchain

- System boot images contain only boot assets and the kernel, never user ELF
  programs, including in TEST=1. User programs are built and packaged separately;
  test entry points prepare their own user disk instead of adding user-program
  dependencies or copying ELF files in system-image recipes.
- The base rootfs is a separate build/install artifact containing system user-space
  programs, including `/sbin/init` and `/bin/sh`. Normal CLI boot must work without
  an application disk. Optional user application images contain extra applications,
  mount at `/opt`, and must not supply the base system's init or shell. Neither
  base-rootfs nor application-image builds belong in boot-image prerequisites.
- Prefer Clang cross-compilation for system base components such as init, shell
  and CLI tools. Building the base system must not require the zcc compiler.
  Applications are not tied to one compiler: zcc, Clang or a compatible cross-GCC
  may produce a static ELF satisfying the same Zonix target and process ABI.
- Preserve an explicit zcc build path using `user/zcc`'s frontend. When selecting
  zcc or validating its examples, actually compile through zcc; do not silently
  substitute Clang. Compiler choice does not determine a program's installation
  role or whether it executes in user mode.
- Share the existing `user/zcc/src/runtime` startup, syscall wrappers, library and
  linker scripts across compilers. Its location does not require using the zcc
  frontend. Do not add a second runtime or link a host OS CRT/libc. Match the
  target architecture and calling convention; an ELF container alone is not
  sufficient for compatibility. Existing register fixtures retain their own entry.
- Keep the kernel and zcc runtime ABI definitions synchronized. A runtime that
  omits new kernel calls must not be accepted by weakening the ABI checker.
- zcc is a separate Git submodule. Include its source changes and update the
  parent gitlink when submitting toolchain changes; staged checks must inspect
  the pinned dependency rather than uncommitted headers from the worktree.

# Mandatory kernel rules

These rules apply to every change, including bug fixes and test code. Follow the
existing subsystem design rather than adding a second implementation style.

## Shared STL and resource ownership

- Use the project-level `external/zstl` submodule for standard structures and
  utilities, including `sys::unique_ptr`, `sys::move` and `sys::nothrow`. Do not
  implement another first-party standard container or smart pointer. Add missing
  required functionality to zstl instead; its tests live in the submodule.
- All consumers, including future mini-cocos integration, must select the same
  zstl include root and runtime ABI. zstl is always self-contained; do not define
  the obsolete `ZSTL_FREESTANDING` macro. Freestanding compilation and using a
  platform C runtime are separate choices. The kernel does not link an external
  CRT; `kernel/cxxrt.cpp` supplies allocating new/delete and byte operations.
  Placement definitions and allocation tag types come from zstl. Do not define
  `ZSTL_RUNTIME_DECLARATIONS_PROVIDED` unless a platform header has actually
  supplied all required declarations. Future freestanding mini-cocos builds must use
  `ZOCOS_FREESTANDING=ON`, `ZOCOS_USE_CRT=OFF` and this same zstl dependency.
- Follow `docs/ERROR_HANDLING.md` for owning, borrowing and transferring resources.
  Use move-only owners for exclusive resources; retain explicit checked operations
  for cleanup that can fail or requires a subsystem-specific ordering protocol.

## Architecture separation

- `kernel/`, `include/base/` and `include/kernel/` are architecture independent.
  Never add ISA or board selection there (`__riscv`, `__aarch64__`,
  `__x86_64__`, `ARCH_*`, `CONFIG_X86`, `BOARD_*`, or aliases of those macros).
- Declare a common interface and implement it separately in
  `arch/<arch>/kernel/` or `arch/<arch>/include/asm/`. The build selects the
  implementation through source directories and the `<asm/...>` include path.
- Keep register layouts, ISA instructions, page table encodings, syscall calling
  conventions and architecture-dependent constants in those implementations.
  Shared tests must use the same common interface. Do not copy an ISA branch
  into a shared helper, macro or common header to hide it.
- Do not put inline assembly in shared kernel code.
- Architecture entry points hand off to `kern_init`; it runs C++ global
  constructors once. Do not run `.init_array` again in architecture boot code.
- Each architecture's `head.S` supplies the same `__kernel_pg_dir` array ABI,
  declared in `<asm/pgtable.h>`. Shared VM initialization records it in Manager's
  kernel MM; architecture initialization must not implement Manager's accessors.
- Use the `__` prefix for boot data supplied by assembly/linker ABI, matching
  `__kernel_boot_info`. It identifies the symbol's origin, not a compile-time
  constant: boot code initializes the page table entries at runtime. The prefix
  alone never exempts an ordinary C++ global from the state ownership rules.

## State ownership

- Mutable subsystem state belongs to its owning class. Use private static class
  members for singleton state, with methods or narrowly scoped accessors/friends.
  Keep ordinary instance state on the instance and temporary state on the stack.
- Never add mutable file/namespace globals, including anonymous namespaces,
  `extern` declarations of C++ state, or mutable function-local `static` state.
  Do not turn existing static class members back into globals.
- A namespace, `static` linkage or a renamed variable does not provide ownership.
  A const pointer to mutable data is also not an immutable constant.
- Assembly-backed boot page tables have permanent storage. Kernel MM borrows
  them; only owned user page tables may be freed by `MemoryDesc` destruction.
- Immutable constants/tables, actual assembly/linker ABI symbols, and test-only
  fixtures are exceptions. MM fixtures must not borrow boot page tables or use
  the live kernel MM as a private swap queue; use a local `MemoryDesc` and owned
  page tables for MM tests.
- `scripts/kernel_rule_exceptions.json` records specific existing legacy state
  and ABI symbols. It is not permission to add new globals. Do not grow the
  legacy list to make a change pass; remove entries when their state is migrated.

## Page table predicates

- Define hardware masks once with semantic names in the architecture header.
  Do not embed numeric masks or bit shifts in `pte_*`/`pde_*` tests.
- A bit test produces `bool` with an explicit comparison: `(entry & MASK) != 0`
  for a set bit, `(entry & MASK) == 0` for a clear bit, or an explicit comparison
  with a named field encoding. Never use a masked integer as a boolean, including
  in `return`, `if`, `!`, `&&` or `||`.
- Compose access decisions and permission merges from semantic predicates such
  as `pte_present`, `pte_user` and `pte_writable`. Keep inverted hardware encodings
  and table-level restrictions inside the architecture's predicates.
- Predicates inspecting a permission bit do not imply a valid mapping. Check
  presence separately in access decisions; permission merges may inspect flags
  without a present bit. Do not change permission semantics as a style cleanup.

## Required verification

- Follow `docs/NAMING.md`, the authoritative project naming convention. Types
  and scoped enum values use PascalCase; functions/variables/namespaces use
  snake_case. Private instance and mutable static members use a trailing `_`,
  never `s_`/`m_`/`g_`. Mutable static members must be private.
- Local const/constexpr values remain snake_case. Class/namespace constants and
  macros use UPPER_CASE. Type template parameters use PascalCase; non-type
  parameters use UPPER_CASE. Pure getters use bare property names; shared arch
  interfaces name their purpose rather than a specific architecture register.
- Naming exceptions require an exact path, qualified name and external-contract
  explanation in `scripts/naming_exceptions.json`. They do not grant ownership
  exceptions. Preserve actual standard/firmware/linker/assembly interfaces.

- Run `make install-hooks` once per clone. The versioned `.githooks/pre-commit`
  checks an isolated snapshot of the Git index for all three architectures and
  does not run tests. Windows Git delegates to the default WSL distribution. Do
  not bypass this hook.
- Run `make check ARCH=x86`, `make check ARCH=aarch64` and
  `make check ARCH=riscv64` after changing kernel code or these rules.
  The checker uses Clang's AST for state ownership and scans unpreprocessed shared
  source for architecture selection, including inactive branches. It also checks
  architecture page headers for numeric masks and implicit boolean bit tests.
- Local pre-commit is the submission gate; `make check` is the explicit manual
  entry point. Normal builds and CI must not invoke this check automatically.
  A failed check must be fixed, never ignored or bypassed.
- Run affected production builds with `TEST=0`. For shared MM, scheduling, trap
  or executable changes, compile all three kernels. Report validation
  limitations plainly; successful compilation is not runtime validation.
- Choose how much to test from the Testing section below. It replaces the
  earlier blanket rule against running tests.

## Testing

Match the testing to what the change can break. Running everything for a
trivial edit and running nothing for a kernel change are both wrong.

### How much to run

- **Nothing beyond the build** when behaviour cannot change: documentation,
  comments, log or message wording, formatting, a rename the compiler fully
  checks, or deleting code that nothing builds or references. Build the
  affected targets and stop.
- **Look at it** for user-space appearance and layout (Desktop, Files,
  Terminal, controls): build the image, start it in QEMU and check the three
  modes above. These have no automated suite; do not write one for pixels.
- **The suite for that area** when the change stays inside one subsystem or one
  architecture: run the host harness or script test that covers it, or that
  architecture's QEMU suite. Examples: a storage driver change runs
  `scripts/tests/test_storage_drivers.py`; a checker script change runs its
  `scripts/tests/test_*.py`; an aarch64 driver change runs the aarch64 QEMU
  suite.
- **The full regression** when the change can affect code outside its own file
  set:
  - shared kernel code under `kernel/mm`, `kernel/sched`, `kernel/trap`,
    `kernel/syscall`, `kernel/fs`, `kernel/exec`, `kernel/block`, `kernel/sync`
    or `kernel/cons`, and shared headers under `include/`;
  - architecture entry, trap and context-switch code, page tables, boot code
    and linker scripts;
  - the user ABI (`include/abi`, syscall numbers, the zcc runtime) or the
    `external/zstl` and `user/zcc` pins;
  - the build system when it changes what is compiled or linked;
  - a refactor that touches more than one subsystem, even if it is meant to
    preserve behaviour;
  - anything about to be tagged as a release.

The full regression is all of:

```sh
python3 -m unittest discover -s scripts/tests
make ARCH=x86 TEST=1 && bash scripts/ci_qemu_test.sh bios && bash scripts/ci_qemu_test.sh uefi
make ARCH=aarch64 TEST=1 bin/aarch64/sdcard.img && bash scripts/ci_qemu_test_aarch64.sh
make ARCH=riscv64 TEST=1 bin/riscv64/sdcard.img && bash scripts/ci_qemu_test_riscv64.sh
python3 scripts/test_shutdown_qemu.py x86 --boot bios --unsupported
python3 scripts/test_shutdown_qemu.py x86 --boot uefi
python3 scripts/test_shutdown_qemu.py aarch64
python3 scripts/test_shutdown_qemu.py riscv64
make ARCH=x86 TEST=0 && python3 scripts/test_console_qemu.py x86
make ARCH=aarch64 TEST=0 && python3 scripts/test_console_qemu.py aarch64
make ARCH=riscv64 TEST=0 && python3 scripts/test_console_qemu.py riscv64
```

The console checks boot the production system with its real init and shell, so
they also leave the tree built with `TEST=0`. Report which of these ran and
their results.
A suite that already fails before the change is reported as such, with its
cause, and is repaired or removed; it is never left failing or skipped.

### When to add a test

- A bug fix gets one test that fails without the fix. Check that it does.
- New behaviour that user space or another subsystem relies on gets a test of
  that behaviour: a syscall, a file-system operation, an error or fault path.
- Extend the existing suite for the subsystem. Add a file only for a subject
  that has none, and register it in `kernel/test/test_main.cpp` or run it from
  `scripts/tests`.

### What not to test

This is an operating system. Reading and writing a piece of memory, a register
or a descriptor is often the test itself, because it proves a mapping, a
permission or a hardware state. Judge a test by what its failure would reveal,
not by how simple it looks. Before removing one, find out what it was written
to catch and whether anything else still catches that; if its original purpose
is gone but a real invariant remains, re-aim it instead of deleting it.

Code built for the kernel is not covered by tests that run on the host. zstl
compiled with the kernel's flags is exercised by the in-kernel suite, not by
zstl's own host tests.

With that in mind, do not test:

- Getters, setters and constants that touch no hardware, mapping or
  architecture-specific layout, and values the test has just assigned.
- The compiler or the language. Use `static_assert` for compile-time facts.
- Source text, file layout or editor configuration.
- The same behaviour in both a host harness and the in-kernel suite. Host
  harnesses are for injected failures the real kernel cannot produce;
  everything else belongs in the in-kernel suite.
- Details a harmless refactor would change: exact PIDs, tick counts, log text,
  or sizes copied from the implementation.
- One assertion per message. A test covers a behaviour, and a few assertions
  are usually enough.

### Keeping tests honest

- A host harness links real kernel sources against stubs. When kernel code
  gains a dependency, update the stubs in the same change and run that harness.
- Delete a test when the behaviour it covers is removed. Do not weaken an
  assertion to make it pass.
- Tests must not depend on a terminal, on timing, or on an earlier test having
  run.

## Working with the user

- Carry out authorized local fixes and verification without unnecessary approval
  questions. Do not open security scans, cloud review tools or pull requests
  unless the user requests them.
- Preserve unrelated work. Keep diffs focused; do not mass-format the repository.
