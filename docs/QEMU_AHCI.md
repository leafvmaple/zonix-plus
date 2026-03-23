# QEMU AHCI 运行说明（x86）

本文档聚焦 x86 下的 AHCI 磁盘路径。

## 当前默认行为

`make qemu ARCH=x86` 默认走 UEFI + AHCI：

- 系统盘：`bin/x86/zonix-uefi.img`，挂到 AHCI 端口 0
- 用户盘：`bin/x86/userdata.img`，默认挂到 AHCI 端口 1（可切换 IDE）

对应配置文件：`qemu-uefi.cfg`

## 常用命令

```bash
# 默认：UEFI + AHCI
make qemu ARCH=x86

# 用户盘改为 IDE（便于对比驱动行为）
make qemu ARCH=x86 DISK=ide

# BIOS 回退路径（主盘 IDE，仍可挂 AHCI 控制器）
make qemu-bios ARCH=x86

# 带 GDB 的调试
make debug ARCH=x86
```

## 设备映射

### UEFI + AHCI（默认）
- `ahci0.0`：系统盘（`zonix-uefi.img`）
- `ahci0.1`：用户盘（`userdata.img`，当 `DISK=ahci`）

### UEFI + IDE 用户盘（`DISK=ide`）
- `ahci0.0`：系统盘（AHCI）
- `hdb`：用户盘（IDE）

### BIOS 回退模式
- 主盘 `hda`：`zonix.img`
- 用户盘：由 `DISK=ahci|ide` 决定

## 相关代码位置

- AHCI 驱动：`arch/x86/kernel/drivers/ahci.h`, `arch/x86/kernel/drivers/ahci.cpp`
- IDE 驱动：`arch/x86/kernel/drivers/ide.h`, `arch/x86/kernel/drivers/ide.cpp`
- 块设备层：`kernel/block/blk.h`, `kernel/block/blk.cpp`

## 排错建议

### AHCI 设备未识别
1. 确认启动命令是 `make qemu ARCH=x86`。
2. 查看串口日志是否出现 AHCI 控制器探测输出。
3. 用 `make qemu ARCH=x86 DISK=ide` 对比是否只有 AHCI 路径异常。

### 调试 AHCI 初始化
```bash
make debug ARCH=x86
# GDB:
#   b AhciManager::init
#   b AhciManager::probe_callback
```
