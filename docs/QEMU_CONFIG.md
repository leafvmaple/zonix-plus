# QEMU 配置文件说明

本文档描述当前仓库实际使用的 QEMU 配置文件与对应 Makefile 目标。

## 快速开始

### x86（UEFI + AHCI，默认）
```bash
make qemu ARCH=x86
```

### x86（BIOS 回退路径）
```bash
make qemu-bios ARCH=x86
```

### aarch64（UEFI + virt）
```bash
make qemu ARCH=aarch64
```

### riscv64（UEFI + virt）
```bash
make qemu ARCH=riscv64
```

### 调试（x86）
```bash
make debug ARCH=x86
# 另一个终端可用 gdb -q -x scripts/gdbinit
```

## 配置文件列表

| 文件 | 架构 | 模式 | 说明 |
|------|------|------|------|
| `qemu-uefi.cfg` | x86 | UEFI + AHCI | 默认运行配置，系统盘走 AHCI 端口 0 |
| `qemu-bios.cfg` | x86 | BIOS + IDE | 回退路径，便于兼容性验证 |
| `qemu-uefi-aarch64.cfg` | aarch64 | UEFI + virt | 使用 virtio-blk 辅助固件启动 + SDHCI 供内核识别 |
| `qemu-uefi-riscv64.cfg` | riscv64 | UEFI + virt | 使用 virtio-blk 辅助固件启动 + SDHCI 供内核识别 |

## x86 运行参数约定

x86 运行由 `arch/x86/Makefile` 组装命令：

- `make qemu ARCH=x86` 会读取 `qemu-uefi.cfg`
- `make qemu-bios ARCH=x86` 会读取 `qemu-bios.cfg`
- `DISK=ahci|ide` 控制 user-data 磁盘的控制器类型（默认 `ahci`）

示例：
```bash
make qemu ARCH=x86 DISK=ide
```

## aarch64 运行参数约定

aarch64 运行由 `arch/aarch64/Makefile` 组装命令：

- 使用 `qemu-system-aarch64 -M virt`
- 读取 `qemu-uefi-aarch64.cfg`
- 固件路径默认 `AAVMF=/usr/share/qemu-efi-aarch64/QEMU_EFI.fd`

## riscv64 运行参数约定

riscv64 运行由 `arch/riscv64/Makefile` 组装命令：

- 使用 `qemu-system-riscv64 -M virt`
- 读取 `qemu-uefi-riscv64.cfg`
- 固件路径默认：
  - `AAVMF=/usr/share/qemu-efi-riscv64/RISCV_VIRT_CODE.fd`
  - `AAVMF_VARS=/usr/share/qemu-efi-riscv64/RISCV_VIRT_VARS.fd`
- 会自动复制 `bin/riscv64/zonix-uefi.img -> bin/riscv64/sdcard.img`
  供内核通过 SDHCI 驱动识别并在启动阶段自动挂载为系统盘（`/`）

## 配置文件格式要点

QEMU 配置文件是 INI 风格：

- 不要缩进 key/value
- 字符串一般用双引号
- 注释使用 `#`
- `-readconfig` 可叠加命令行参数

## 常见问题

### 1) OVMF 或 AAVMF 路径不对
- x86 可通过 `OVMF=/path/to/OVMF.fd` 覆盖
- aarch64 可通过 `AAVMF=/path/to/QEMU_EFI.fd` 覆盖

### 2) 没看到 user-data 磁盘
- x86 下确认是否传入了 `DISK=...`
- aarch64 下确认 `bin/aarch64/sdcard.img` 已构建
- riscv64 下确认 `bin/riscv64/sdcard.img` 已构建

### 3) 调试端口
`make debug ARCH=x86` 使用 QEMU `-S -s`，GDB 默认连接 `:1234`。
