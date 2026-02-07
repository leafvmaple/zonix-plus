# QEMU 配置文件说明

## 快速开始

### IDE 模式（默认）
```bash
make qemu
# 或使用配置文件
qemu-system-i386 -readconfig qemu.cfg
```

### AHCI 模式
```bash
make qemu-ahci
# 或使用配置文件
qemu-system-i386 -readconfig qemu-ahci.cfg
```

### 调试模式
```bash
make debug-qemu
# GDB 调试：gdb -q -x tools/gdbinit
```

## 配置文件列表

| 文件 | 模式 | 用途 |
|------|------|------|
| qemu.cfg | IDE | 标准IDE模式 |
| qemu-ahci.cfg | AHCI | AHCI模式（现代） |
| qemu-debug.cfg | IDE | 调试模式 |

## 配置参数说明

### 机器类型
- `type = "pc"` - 标准PC机器（IDE）
- `type = "q35"` - Q35芯片组（支持AHCI）

### 磁盘配置

#### IDE模式
```ini
[drive "ide0"]
file = "bin/zonix.img"
if = "ide"
index = "0"      # hda
```

#### AHCI模式
```ini
[device "ahci0"]
driver = "ahci"

[drive "sata0"]
file = "bin/zonix.img"
if = "none"

[device "sata-disk0"]
driver = "ide-hd"
bus = "ahci0.0"
drive = "sata0"
```

## 修改配置

直接编辑相应的 `.cfg` 文件，例如添加磁盘：

```ini
[drive "sata1"]
file = "bin/disk2.img"
if = "none"

[device "sata-disk1"]
driver = "ide-hd"
bus = "ahci0.1"
drive = "sata1"
```

## 常见选项

| 选项 | 说明 |
|------|------|
| `-S` | 启动后暂停（用于调试） |
| `-no-reboot` | 关闭时不重启 |
| `-s` | 启用GDB调试（端口1234） |
| `-monitor stdio` | 监视器输出到标准输出 |

## 关于配置文件格式

QEMU配置文件使用INI格式，注意：
- **不要使用缩进**，所有键值对应该从行首开始
- 注释以 `#` 开头
- 字符串值需要用双引号括起来
- 部分标量值（如整数）不需要引号

正确示例：
```ini
[section]
key = "value"
number = 10
```

错误示例：
```ini
[section]
  key = "value"    # 不要有缩进！
  number = "10"    # 数字不需要引号
```
