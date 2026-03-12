# Zonix OS — Development Notes

> 本文档记录项目当前的架构状态、已完成的优化、遗留问题和后续计划。
> 最后更新: 2026-03-12 (v0.9.2)

---

## 1. 项目结构

```
zonix-plus/
├── arch/x86/                   # 架构相关代码 (仿 Linux 布局)
│   ├── boot/
│   │   ├── Makefile            # 转发到 bios/ 和 uefi/
│   │   ├── bootlib.h           # boot 共享库 (memcpy/memset)
│   │   ├── bios/
│   │   │   ├── Makefile        # clang -m32: MBR + VBR + Bootloader
│   │   │   ├── mbr.S           # Master Boot Record (512B, 16-bit)
│   │   │   ├── vbr.S           # Volume Boot Record (512B, 16-bit)
│   │   │   ├── entry.S         # 32→64-bit long mode trampoline
│   │   │   └── bootload.c      # ELF loader (32-bit protected mode)
│   │   └── uefi/
│   │       ├── Makefile        # mingw32-gcc: PE32+ cross-compile (UEFI)
│   │       └── bootload.c      # UEFI bootloader (efi_main)
│   ├── include/asm/            # <asm/xxx.h> 头文件
│   │   ├── arch.h, cpu.h, cr.h, io.h, memlayout.h, mmu.h, pg.h, ports.h
│   │   ├── seg.h, segments.h
│   │   └── drivers/            # 硬件寄存器定义 (i8042, i8254, i8259)
│   └── kernel/                 # 架构相关 kernel 代码
│       ├── head.S              # 64-bit 入口 + 页表初始化
│       ├── idt.cpp/h           # IDT 初始化 (idt::init)
│       ├── switch.S            # 上下文切换
│       ├── trapentry.S         # 中断入口 stub
│       └── vectors.S           # 256 个中断向量
├── kernel/                     # 架构无关 kernel (C++17, 64-bit)
│   ├── init.cpp                # kern_init() 入口
│   ├── lib/                    # 内核基础库#   ├── block/                  # 块设备抽象层
│   ├── cons/                   # 控制台 + shell
│   ├── debug/                  # panic / assert
│   ├── drivers/                # 设备驱动 (IDE, AHCI, PCI, PIC, PIT, ...)
│   ├── fs/                     # FAT32 文件系统
│   ├── mm/                     # 物理/虚拟内存管理 + swap
│   ├── sched/                  # 调度器 (Round-Robin)
│   └── trap/                   # 中断/异常分发
├── include/                    # 架构无关公共头文件
│   ├── base/                   # 基础类型 (types.h, elf.h, bpb.h, mbr.h)
│   ├── kernel/                 # 内核配置 (config.h, bootinfo.h, version.h, ...)
│   └── uefi/                   # UEFI 类型定义
├── fonts/console.psf           # 内嵌控制台字体 (llvm-objcopy → .rodata)
├── scripts/                    # 构建辅助
│   ├── kernel.ld, mbr.ld, boot.ld, bootload.ld, uefi.ld  # 链接脚本
│   ├── create_zonix_image.sh   # FAT32 磁盘镜像
│   ├── create_uefi_image.sh    # GPT+ESP UEFI 镜像
│   ├── create_userdata_image.sh  # 用户数据盘
│   └── gdbinit                 # GDB 调试脚本
├── docs/                       # 文档
├── Makefile                    # 顶层构建 (~310 行)
└── qemu*.cfg, bochsrc*.bxrc   # 模拟器配置
```

## 2. 构建系统

### 工具链

| 目标 | 编译器 | 位宽 | 语言 |
|------|--------|------|------|
| Kernel | `clang++` | 64-bit | C++17 freestanding |
| arch/x86/kernel/ | `clang++` | 64-bit | C++17 / ASM |
| BIOS boot (MBR/VBR/Bootloader) | `clang -m32` | 32-bit | C / ASM |
| UEFI boot (BOOTX64.EFI) | `x86_64-w64-mingw32-gcc` | 64-bit | C (PE32+) |
| Linker | `ld.lld` | — | LLVM linker |
| Utilities | `llvm-objdump`, `llvm-objcopy` | — | LLVM binutils |

### Makefile 层级

```
Makefile                         # 顶层: 变量、宏、kernel、磁盘镜像、运行目标
└── include arch/x86/boot/Makefile
    ├── include bios/Makefile    # MBR + VBR + Bootloader (clang -m32)
    └── include uefi/Makefile    # BOOTX64.EFI (mingw cross-compile)
```

### Include 路径 (`-I`)

```makefile
-I include              # <base/...>, <kernel/...>, <uefi/...>
-I arch/x86/include     # <asm/...>, <asm/drivers/...>
-I arch/x86/kernel      # "idt.h"
-I kernel               # "lib/...", "drivers/...", "mm/...", "sched/...", ...
```

### Include 风格约定

| 风格 | 用途 | 示例 |
|------|------|------|
| `<namespace/file.h>` | 公共 API 头文件 | `<base/types.h>`, `<asm/io.h>`, `<kernel/config.h>` |
| `"subsys/file.h"` | 跨模块 kernel 内部 | `"drivers/pic.h"`, `"mm/vmm.h"`, `"sched/sched.h"` |
| `"lib/file.h"` | kernel 基础库 | `"lib/stdio.h"`, `"lib/string.h"`, `"lib/memory.h"` |
| `"file.h"` | 同目录 include | `"blk.h"`, `"swap.h"`, `"pci.h"` |

### 特性

- **自动依赖跟踪**: `-MMD -MP` 生成 `.d` 文件，增量编译只重编改动的文件
- **安静模式**: 默认只输出 `LINK`/`MBR`/`VBR` 等摘要，`make V=1` 显示完整命令
- **辅助目标**: `make disasm`, `make format`, `make lint`, `make help`

---

## 3. 已完成的优化 (本轮)

### 3.1 目录重组 → Linux 风格 `arch/` 布局

| 之前 | 之后 |
|------|------|
| `kernel/arch/x86/` | `arch/x86/kernel/` |
| `include/arch/x86/asm/` | `arch/x86/include/asm/` |
| `boot/` (顶层) | `arch/x86/boot/` |
| `tools/` | `scripts/` |
| `kernel/include/` | `kernel/lib/` (上轮已完成) |

### 3.2 构建系统现代化

- Makefile 从 371 行精简重组为 310 行
- boot 构建独立为 `arch/x86/boot/{bios,uefi}/Makefile`
- 添加 `-MMD -MP` 自动依赖
- 反汇编从每次构建抽离为 `make disasm`
- 磁盘镜像构建提取到 `scripts/create_zonix_image.sh`

### 3.3 头文件组织修复

- 消除全部 56 处 `"../"` 相对路径 include (kernel 代码中已归零)
- 统一 include guard 为 `#pragma once` (修复了 `sysinfo.h` 的传统 guard)
- 统一 `include/base/elf.h` 的 include 风格为 `<base/types.h>`
- 移除多余的 `-I kernel/lib`，全部改为 `"lib/xxx.h"` 路径

### 3.4 命名规范统一

- 所有公共子系统 API 统一为 `namespace::func()` 风格
- 修复拼写错误: `kdb` → `kbd`, `interupt` → `interrupt`
- 统一命名: `pages_free()` → `free_pages()` (与 `alloc_pages` 对称)

---

## 4. 待修复问题

### 高优先级

- [x] **`E820_RAM` 宏冲突** *(已修复)*: 删除 `arch/x86/kernel/e820.h`（内容已内联到 `pmm.cpp` 并改用 `BOOT_MEM_AVAILABLE`），`seg.h` 中的 `E820_RAM` 不再冲突。

- [x] **`seg.h` / `segments.h` 职责不清** *(已修复)*: `seg.h` 精简为仅 GDT 选择子和段类型宏；内存布局（`KERNEL_BASE` 等）和 E820 常量拆分到新建的 `asm/memlayout.h`；`segments.h` 保持 C++ 结构体层不变。

- [x] **README.md 路径过时** *(已修复)*: `./tools/` → `./scripts/`，`make qemu-debug` → `make debug-qemu`。

### 中优先级

- [x] **编译警告 (6 个)** *(已修复)*:
  - `shell.cpp` — `cmd_pos`/`i` 改为 `size_t`，删除未使用的 `strncmp()`
  - `stdio.cpp` — 添加 `static_cast<uint64_t>(base)` 消除符号比较
  - `ide.cpp` — 删除未使用的 `hd_wait_data_on_base()`

- [x] **链接器警告** *(已修复)*:
  - `entry.S` 末尾添加 `.section .note.GNU-stack,"",@progbits`
  - `console.psf.o` objcopy 添加 `--add-section .note.GNU-stack=/dev/null --set-section-flags .note.GNU-stack=contents,readonly`
  - `LDFLAGS` / `BOOT_LDFLAGS` 添加 `--no-warn-rwx-segments` 消除 RWX LOAD segment 警告

- [x] **Makefile 冗余** *(已修复)*:
  - 删除未使用的 `HOSTCC`/`HOSTCFLAGS`
  - bootloader 编译规则去除与 `BOOT_CFLAGS` 重复的 flags

- [x] **`.vscode/settings.json`** *(已修复)*: 移除 `"fat16.h"` / `"e820.h"` 旧关联，添加 `"memlayout.h"`

### 低优先级

- [x] **docs/TODO.md 大量过时** *(已修复)*: 路径已更正 (`kern/` → `kernel/`、`.c` → `.cpp`)，已完成项已勾选，项目结构树已更新。

- [x] **全局函数 namespace 化** *(已修复)*:
  - `shell_init/handle_char/prompt/main()` → `shell::init/handle_char/prompt/main()` (`shell.h` 新增 `namespace shell`)
  - `page2pa/page2kva/pa2page/kva2page()` → `pmm::` namespace
  - `alloc_pages/free_pages/alloc_page/free_page()` → `pmm::` namespace
  - `get_pte/page_insert/pgdir_alloc_page/tlb_invl()` → `pmm::` namespace
  - `swapfs_init/read/write()` + `find_vaddr_for_page()` → `swap::` namespace
  - `cprintf()` / `kmalloc()` / `kfree()` 保持全局 — 属于 `lib/` 層的通用库函数，全局更合理

- [x] **删除垃圾文件** *(已修复)*: 删除根目录 `-c` 文件和空目录 `scripts/bin/`。

- [x] **`.gitignore` 扩展** *(已修复)*: 增加 `*.o`, `*.d`, `*.bin`, `*.img`, `*.EFI`, `compile_commands.json`, `*.swp`, `.vscode/`, `-c` 等。

- [x] **内嵌 TODO 注释 (6 处)** *(已审计)*: 位置已验证，内容为未来功能规划，保留不动：
  - `stdio.cpp:206` — cprintf 返回值
  - `ahci.cpp:34` — AHCI 硬编码设备大小
  - `fat.cpp:162` — FAT write 支持
  - `fat.h:58` — FatInfo 成员应改为 private
  - `pmm.cpp:229` — slab allocator
  - `sched.cpp:128` — 进程完整复制

---

## 5. 架构约定

### 命名规范

| 类型 | 规范 | 示例 |
|------|------|------|
| 命名空间 | `lower_case` | `vmm`, `pmm`, `sched`, `blk`, `cons`, `swap`, `shell` |
| 类/结构体 | `CamelCase` | `BlockDevice`, `TaskManager`, `Page` |
| 函数/方法 | `lower_case` | `init()`, `pg_fault()`, `alloc_pages()` |
| 私有成员 | `lower_case_` | `parent_`, `count_` |
| 常量 | `UPPER_CASE` | `MAX_DEVICES`, `SECTOR_SIZE` |
| 宏 | `UPPER_CASE` | `KERNEL_BASE`, `PAGE_SIZE` |

### 公共 API 模式

```cpp
namespace subsys {
    void init();                    // 子系统初始化
    // ... 其他公共接口
}
```

所有子系统在 `kern_init()` 中按依赖顺序初始化:

```cpp
cons::init() → pic::init() → pit::init() → idt::init() →
pmm::init() → vmm::init() → fbcons::late_init() → blk::init() →
swap::init() → sched::init() → intr::enable()
```

### Include guard

全部使用 `#pragma once` (无例外)。

---

## 6. 构建与运行

```bash
make                    # 构建全部 (kernel + boot + disk image + UEFI)
make bin/kernel         # 只编译内核
make mbr                # 只编译 MBR
make qemu               # QEMU 运行 (BIOS)
make qemu-uefi          # QEMU 运行 (UEFI)
make debug-qemu         # QEMU + GDB 调试
make disasm             # 生成反汇编
make format             # clang-format 格式化
make clean              # 清理构建产物
make V=1                # 显示完整编译命令
make help               # 显示所有目标
```
