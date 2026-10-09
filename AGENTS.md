# Owner authorization for commits and pushes

- This is the owner's core project. Never create a Git commit or push unless
  the owner explicitly tells you to perform that specific operation for the
  current changes. Obtain authorization before executing the operation.
- Commit authorization does not authorize pushing. "Continue", "implement",
  "finish" or "organize" do not authorize either operation. Never carry an
  authorization from earlier work over to a new change or task.
- Prepare edits and verification locally, then leave the changes uncommitted
  for the owner's review until explicitly instructed to commit or push.

# Mandatory kernel rules

These rules apply to every change, including bug fixes and test code. Follow the
existing subsystem design rather than adding a second implementation style.

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
  runs the harness regression tests before allowing a local commit. Windows Git
  delegates to the default WSL distribution. Do not bypass this hook.
- Run `make check ARCH=x86`, `make check ARCH=aarch64` and
  `make check ARCH=riscv64` after changing kernel code or these rules.
  The checker uses Clang's AST for state ownership and scans unpreprocessed shared
  source for architecture selection, including inactive branches. It also checks
  architecture page headers for numeric masks and implicit boolean bit tests.
- Local pre-commit is the submission gate; `make check` is the explicit manual
  entry point. Normal builds and CI must not invoke this check automatically.
  A failed check must be fixed, never ignored or bypassed.
- Run the affected builds/tests. For shared MM, scheduling, trap or executable
  changes, compile all three kernels with `TEST=1`; exercise x86 BIOS and UEFI
  when the environment supports them. Report any validation limitation plainly.
- Add positive and negative harness cases when changing what the checker accepts.

## Working with the user

- Carry out authorized local fixes and verification without unnecessary approval
  questions. Do not open security scans, cloud review tools or pull requests
  unless the user requests them.
- Preserve unrelated work. Keep diffs focused; do not mass-format the repository.
