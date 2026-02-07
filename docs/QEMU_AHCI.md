# QEMU AHCI 配置说明

## 概述

Zonix OS 现在支持两种磁盘控制器模式：
- **IDE 模式**：传统的IDE/ATA控制器
- **AHCI 模式**：现代的AHCI (Advanced Host Controller Interface) 控制器

## QEMU 配置文件

QEMU 支持类似 Bochs 的配置文件方式（INI格式），项目提供了以下配置文件：

- **qemu.cfg** - IDE 模式配置
- **qemu-ahci.cfg** - AHCI 模式配置
- **qemu-debug.cfg** - 调试模式配置（IDE + GDB）

## 使用方法

### 标准 IDE 模式（默认）
```bash
make qemu                    # 使用 qemu.cfg
# 或直接运行
qemu-system-i386 -readconfig qemu.cfg -S -no-reboot
```

### AHCI 模式
```bash
make qemu-ahci              # 使用 qemu-ahci.cfg
# 或直接运行
qemu-system-i386 -readconfig qemu-ahci.cfg -S -no-reboot
```

### 调试模式
```bash
make debug-qemu             # 使用 qemu-debug.cfg + GDB
make debug-qemu-ahci        # AHCI模式调试
```

## 配置文件详解

### qemu.cfg (IDE模式)
```ini
[machine]
  type = "pc"              # 标准PC机器类型
  
[memory]
  size = "32M"             # 32MB内存

[boot-opts]
  order = "c"              # 从硬盘启动

# 主磁盘 (hda)
[drive]
  file = "bin/zonix.img"
  format = "raw"
  if = "ide"              # IDE接口
  index = "0"             # 主盘
  
# 从磁盘 (hdb)
[drive]
  file = "bin/fat32_test.img"
  format = "raw"
  if = "ide"
  index = "1"             # 从盘
```

### qemu-ahci.cfg (AHCI模式)
```ini
[machine]
  type = "q35"            # Q35芯片组，支持AHCI

# AHCI控制器
[device "ahci0"]
  driver = "ahci"

# SATA主磁盘
[drive "sata0"]
  file = "bin/zonix.img"
  format = "raw"
  if = "none"             # 不直接关联接口

[device]
  driver = "ide-hd"       # SATA硬盘
  bus = "ahci0.0"         # AHCI端口0
  drive = "sata0"

# IDE从磁盘 (hdb)
[drive]
  file = "bin/fat32_test.img"
  format = "raw"
  if = "ide"
  index = "1"
```

## 设备映射

### AHCI 模式
- `sda` (AHCI port 0) → zonix.img（主系统盘）
- `hdb` (IDE slave) → fat32_test.img（测试盘）

### IDE 模式（传统）
- `hda` (IDE master) → zonix.img（主系统盘）
- `hdb` (IDE slave) → fat32_test.img（测试盘）

## AHCI vs IDE 区别

| 特性 | IDE | AHCI |
|------|-----|------|
| 速度 | 较慢 | 更快 |
| 热插拔 | 不支持 | 支持 |
| NCQ | 不支持 | 支持 |
| 端口数 | 4个设备 | 最多32个端口 |
| 现代操作系统 | 兼容模式 | 原生支持 |

## 驱动代码位置

- IDE驱动：`kern/drivers/ide.h`, `kern/drivers/ide.cpp`
- AHCI驱动：`kern/drivers/ahci.h`, `kern/drivers/ahci.cpp`
- 块设备层：`kern/drivers/blk.h`, `kern/drivers/blk.cpp`

## 注意事项

1. **Bochs 支持**
   - Bochs 需要 2.6.0+ 版本才支持AHCI
   - 当前配置默认使用IDE模式
   - 要启用AHCI，需修改 `bochsrc.bxrc` 中的注释

2. **QEMU 支持**
   - QEMU 原生支持AHCI
   - 建议使用QEMU进行AHCI开发和测试

3. **驱动初始化**
   - 系统启动时会同时初始化IDE和AHCI驱动
   - 每个驱动只会检测并注册自己的设备
   - 块设备管理器统一管理所有磁盘

## 故障排查

### QEMU 启动失败
```bash
# 检查QEMU版本（建议 >= 2.0）
qemu-system-i386 --version

# 查看支持的设备
qemu-system-i386 -device help | grep ahci
```

### 设备未检测到
- 检查 `blk_init` 输出，确认设备是否被识别
- 查看 `AHCI: Found X device(s)` 或 `hd_init: found X device(s)` 信息

### 调试AHCI驱动
```bash
make debug-qemu-ahci
# 在GDB中设置断点
(gdb) b AhciManager::init
(gdb) b AhciDevice::read
```

## 开发建议

1. 使用 `make qemu-ahci` 测试AHCI驱动功能
2. 使用 `make qemu` 保持IDE驱动兼容性
3. 两种模式都应该能正常启动系统
