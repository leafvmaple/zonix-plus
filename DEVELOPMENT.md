# Zonix OS — Development Notes

> 本文档记录项目当前的架构状态、已完成的优化、遗留问题和后续计划。
> 最后更新: 2026-03-04 (v0.8.0)

---

## 1. 项目结构

```
zonix-plus/
├── arch/x86/                   # 架构相关代码 (仿 Linux 布局)
│   ├── boot/
│   │   ├── Makefile            # 转发到 bios/ 和 uefi/
│   │   ├── bootlib.h           # boot 共享库 (memcpy/memset)
│   │   ├── bios/
│   │   │   ├── Makefile        # gcc -m32: MBR + VBR + Bootloader
│   │   │   ├── mbr.S           # Master Boot Record (512B, 16-bit)
│   │   │   ├── vbr.S           # Volume Boot Record (512B, 16-bit)
│   │   │   ├── entry.S         # 32→64-bit long mode trampoline
│   │   │   └── bootload.c      # ELF loader (32-bit protected mode)
│   │   └── uefi/
│   │       ├── Makefile        # mingw-gcc: PE32+ cross-compile
│   │       └── bootload.c      # UEFI bootloader (efi_main)
│   ├── include/asm/            # <asm/xxx.h> 头文件
│   │   ├── arch.h, cpu.h, cr.h, io.h, mmu.h, pg.h, ports.h
│   │   ├── seg.h, segments.h
│   │   └── drivers/            # 硬件寄存器定义 (i8042, i8254, i8259)
│   └── kernel/                 # 架构相关 kernel 代码
│       ├── e820.cpp/h          # E820 内存探测
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
├── fonts/console.psf           # 内嵌控制台字体 (objcopy → .rodata)
├── scripts/                    # 构建辅助
│   ├── kernel.ld, mbr.ld, boot.ld, bootload.ld, uefi.ld  # 链接脚本
│   ├── create_zonix_image.sh   # FAT32 磁盘镜像
│   ├── create_uefi_image.sh    # GPT+ESP UEFI 镜像
│   ├── create_fat32_image.sh   # FAT32 测试盘
│   └── gdbinit                 # GDB 调试脚本
├── docs/                       # 文档
├── Makefile                    # 顶层构建 (~310 行)
└── qemu*.cfg, bochsrc*.bxrc   # 模拟器配置
```

## 2. 构建系统

### 工具链

| 目标 | 编译器 | 位宽 | 语言 |
|------|--------|------|------|
| Kernel | `g++` | 64-bit | C++17 freestanding |
| arch/x86/kernel/ | `g++` | 64-bit | C++17 / ASM |
| BIOS boot (MBR/VBR/Bootloader) | `gcc -m32` | 32-bit | C / ASM |
| UEFI boot (BOOTX64.EFI) | `x86_64-w64-mingw32-gcc` | 64-bit | C (PE32+) |

### Makefile 层级

```
Makefile                         # 顶层: 变量、宏、kernel、磁盘镜像、运行目标
└── include arch/x86/boot/Makefile
    ├── include bios/Makefile    # MBR + VBR + Bootloader (gcc -m32)
    └── include uefi/Makefile    # BOOTX64.EFI (mingw cross-compile)
```

### Include 路径 (`-I`)

```makefile
-I include              # <base/...>, <kernel/...>, <uefi/...>
-I arch/x86/include     # <asm/...>, <asm/drivers/...>
-I arch/x86/kernel      # "idt.h", "e820.h"
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

- [ ] **`E820_RAM` 宏冲突**: `arch/x86/include/asm/seg.h:82` 和 `arch/x86/kernel/e820.h:8` 同时定义 `E820_RAM`，导致每次编译产生 `-Wmacro-redefined` 警告。E820 常量不应放在 `seg.h` 中。
  - **方案**: 将 `seg.h` 中的 E820 相关宏 (`E820_MEM_BASE`, `E820_MEM_DATA`, `E820_RAM`, `E820_RESERVED`, `E820_ACPI`, `E820_NVS`) 移到 `e820.h` 或独立的 `asm/e820.h` 中。

- [ ] **`seg.h` / `segments.h` 职责不清**: `seg.h` 混杂了 GDT 选择子、段类型宏、E820 常量、内存布局地址。`segments.h` 是 C++ 结构体层。应按职责拆分。

- [ ] **README.md 路径过时**: 仍引用 `./tools/` (应为 `./scripts/`)，`make qemu-debug` (应为 `make debug-qemu`)。

### 中优先级

- [ ] **编译警告 (6 个)**:
  - `shell.cpp:474,603` — `int` vs `size_t` 有符号比较
  - `shell.cpp:538` — 未使用的 `strncmp()` 函数
  - `stdio.cpp:45` — `uint64_t` vs `int` 有符号比较
  - `ide.cpp:36` — 未使用的 `hd_wait_data_on_base()` 函数

- [ ] **链接器 RWX 警告**: `entry.S` 和 `console.psf.o` 缺少 `.note.GNU-stack` section。
  - **方案**: 在 `entry.S` 末尾添加 `.section .note.GNU-stack,"",@progbits`；objcopy 命令添加 `--add-section .note.GNU-stack=/dev/null`。

- [ ] **Makefile 冗余**:
  - `HOSTCC`/`HOSTCFLAGS` 已定义但从未使用
  - bootloader 编译规则重复了 `BOOT_CFLAGS` 中已有的 flags

- [ ] **`.vscode/settings.json`**: 残留 `"fat16.h": "c"` 对旧文件名的关联

### 低优先级

- [ ] **docs/TODO.md 大量过时**: 仍引用旧路径 (`kern/`, `.c` 后缀)，很多已完成项未打勾
- [ ] **全局函数未 namespace 化**: `cprintf()`, `shell_*()`, `page2pa()`, `kfree()`, `swapfs_*()` 等仍是全局函数
- [ ] **删除垃圾文件**: 根目录的 `-c` 文件 (Makefile 副产物)、空目录 `scripts/bin/`
- [ ] **`.gitignore` 可扩展**: 可加入 `*.img`, `*.EFI`, `*.bin`, `compile_commands.json`, `*.swp` 等
- [ ] **内嵌 TODO 注释 (6 处)**:
  - `stdio.cpp:206` — cprintf 返回值
  - `ahci.cpp:34` — AHCI 硬编码设备大小
  - `fat.cpp:162` — FAT write 支持
  - `pmm.cpp:214` — slab allocator
  - `sched.cpp:128` — 进程完整复制

---

## 5. 架构约定

### 命名规范

| 类型 | 规范 | 示例 |
|------|------|------|
| 命名空间 | `lower_case` | `vmm`, `pmm`, `sched`, `blk` |
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
