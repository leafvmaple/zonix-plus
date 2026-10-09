# Project naming convention

This is the authoritative naming convention for first-party Zonix sources.
External contracts are recorded by exact path and qualified name in
`scripts/naming_exceptions.json`. Do not exempt entire files or add exceptions
for ordinary project code. Imported `user/zcc` has its own conventions.

## Identifier forms

| Entity | Form | Example |
| --- | --- | --- |
| Namespace | `snake_case` | `vmm`, `sched`, `list_detail` |
| Class, struct, union, enum, concept | `PascalCase` | `BlockDevice`, `TaskState` |
| Scoped enum value | `PascalCase` | `TaskState::Runnable` |
| Project type alias | `PascalCase` | `PageAddressMap`, `ReadCallback` |
| Type/template template parameter | `PascalCase` | `Element`, `T` |
| Non-type template parameter | `UPPER_CASE` | `CAPACITY`, `N`, `REVERSE` |
| Function, method (including private/static) | `snake_case` | `alloc_pages`, `select_victim` |
| Parameter, local variable, structured binding | `snake_case` | `page_count`, `task` |
| Local const/constexpr variable | `snake_case` | `header_size` |
| Public data field | `snake_case` | `exit_code` |
| Private/protected instance field | `snake_case_` | `state_` |
| Private mutable static field | `snake_case_` | `current_` |
| Class/namespace constant or immutable table | `UPPER_CASE` | `MAX_DEVICES` |
| Macro | `UPPER_CASE` | `PTE_VALID`, `ZONIX_ENSURE_SELECT` |

The same access-based member rule applies to classes and structs. Private
instance state remains `snake_case_` even if its type is const. Top-level const
does not mean that the pointed-to object is immutable. Persistent mutable state
belongs to a class as required by AGENTS.md; a naming exception is never an
ownership exception. Do not use `s_`, `m_`, `g_` or type/Hungarian prefixes for
new member names. Existing ownership exceptions do not authorize new globals.

Use enum class for new internal enumerations. Prefer using declarations to
typedefs; do not create a second alias solely to give a type another spelling.
Standard types (`size_t`, `uint32_t`, etc.), architecture types (`pte_t`,
`pde_t`), and standard container aliases (`iterator`, `value_type`, etc.) retain
their canonical names. Nonstandard traversal aliases use PascalCase.

## Interface semantics

- Pure property access uses a bare name: `state()`, `name()`, `current()`.
  It must not secretly allocate or initialize resources. Setters use `set_`.
- Boolean queries use `is_`, `has_`, `can_`, `needs_`, or a clear conventional
  predicate such as `empty`, `pte_present` or `pte_writable`.
- `find_` returns an optional/nullable match; absence is ordinary. Fallible
  operations use the existing Error/Result types. A bool that means "handled"
  must not be mistaken for successful execution.
- `try_` means an attempt such as nonblocking acquisition, not arbitrary failure.
  `ensure_` may initialize/allocate; failure must still be represented by its type.
- `release_`/`take_` express ownership transfer; document source and destination.
  Borrowed accessors must not imply transfer.
- Use matching pairs: alloc/free, map/unmap, lock/unlock, register/unregister,
  enqueue/dequeue. Directional copies use copy_from_user/copy_to_user.
- Existing standard operations read/write/fork/wait keep their conventional names.
  Output-argument retrieval and creation helpers such as get_filename/get_pte
  may retain get_; they are not pure zero-argument property accessors.

## Vocabulary and units

Use English names. Large scopes and public interfaces need descriptive names;
short local loops may use i/j, and small scopes may use buf/tmp. Canonical kernel
abbreviations include pid, fd, irq, pte, pde, pgdir, pa, va, kva, mmio and dma.
Treat acronyms as words in project types: GptHeader, PciDevice, AhciManager,
ElfHeader, Io. Hardware masks retain uppercase acronyms.

Ambiguous quantities must carry units/address space: byte_count, page_count,
timeout_ms, deadline_ticks, page_table_root_pa, user_stack_va. An unqualified
size/offset/address is acceptable only when the containing object fixes its
meaning. Priority constants distinguish HIGHEST/LOWEST from numeric MAX/MIN.

Distinguish a device's capacity (`block_count`) from the byte width of a block
(`BLOCK_SIZE_BYTES`), a FAT sector index (`fat_start_sector_`) from a partition
LBA (`partition_start_lba_`), and a PCI slot (`device_number`) from a PCI identity
(`device_id`). A memory-copy interface names the user address and kernel buffer
explicitly (`user_src_va`, `kernel_dst`) when both address spaces are involved.
These names carry units; they are not storage/type prefixes.

Architecture adapters expose operations such as `arch_invalidate_tlb_page` and
`arch_port_read16_buffer`; buffer counts are numbers of the specified-width
elements. Instruction/register helpers such as `invlpg`/`rcr3` stay within their
architecture. `map_physical_range` maps an existing byte range;
`alloc_and_map_page` allocates and maps one page. Neither is a pure accessor.

Use Task for the scheduler entity, process for process relationships, and Thread
only when a separate thread concept exists. Manager owns resources/lifecycle;
Desc, Info, Entry and Context must describe their actual roles. Do not encode
class/struct syntax in type names. Subsystem types should live in their logical
namespace; generic names such as vmm::Manager are clear when qualified.

## Files, other languages and ABI

Source files/directories use snake_case and remain case-sensitive. Keep .h/.cpp
and preprocessed .S extensions. Tests use test_<behavior>; Python follows the
same class/function/constant casing, with Python protocol names preserved.
Shell locals use snake_case; exported environment settings and arrays of build
options may use UPPER_CASE. Make configuration variables use UPPER_CASE;
existing make helper/target names and automatic variables retain their roles.
Build outputs required by firmware/FAT naming retain their external spelling.

Hardware layouts/register names remain in the architecture implementation.
Shared interfaces name their function (page table root/kernel stack), not CR3,
TTBR, SATP, RSP0 or ESP. Renaming disk fields must preserve layout and offsets.

Keep externally specified firmware, runtime, syscall and standard protocol
names. Boot data supplied by assembly/linker uses the existing __ prefix,
including __kernel_pg_dir and __kernel_boot_info; this denotes origin, not
compile-time immutability. Ordinary C++ identifiers must not contain __ or start
with _. Named assembly-local labels follow the assembler's .L convention;
numeric forward/backward labels remain valid. Exported entry points and __ boot
symbols retain their contracts. ABI symbol
names, section names and linker references must be migrated together if changed.

## Enforcement and migration

Clang AST checks declaration casing, member access/static roles, aliases,
template parameters and constants. Source checks also cover macro definitions
in inactive branches. Otherwise unused headers and dormant sources are also
checked; sources selected by another architecture retain that build context.
Boot firmware and first-party user C programs use their actual build flags.
Source checks reject register/instruction names in arch adapter interfaces and
named assembly-local labels lacking .L. Python/shell/Make vocabulary and units
remain part of the manual audit; caller-supplied environment names stay stable.
Zero-argument get_ methods must use bare property names. Semantic vocabulary/units
require human judgment; the
checker does not infer ownership or units from arbitrary English words.
The clang-tidy configuration mirrors the convention for editor/manual linting;
the mandatory harness does not require clang-tidy to be installed.

The existing local pre-commit checks the exact isolated index for all three
architectures and runs positive/negative harness tests. make check is the manual
entry point. Normal builds and CI must not automatically invoke naming checks.
Migrate declarations, definitions, uses, assembly and tests together. Keep
behavior changes separate from naming changes, and never mass-format a rename.
