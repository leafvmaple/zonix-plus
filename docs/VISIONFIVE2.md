# VisionFive 2 真机调试

StarFive VisionFive 2（JH7110，8 GB 版本，PCB v1.3B）上运行 Zonix 的做法。内核用
`BOARD=visionfive2` 构建，用户态与 QEMU 的 riscv64 完全相同。

## 硬件连接

- 串口：USB-TTL 模块选 3.3 V 电平，只接三根线：GND → 40 针排针 6 号，模块 RXD → 8 号
  （GPIO5，板子 TX），模块 TXD → 10 号（GPIO6，板子 RX），不接电源线。115200 8N1。
- 网线接第二个网口（`ethernet@16040000`），与开发机在同一局域网。
- 启动拨码 `RGPIO_0`/`RGPIO_1` 都拨到 L：从板载 QSPI Flash 启动 SPL、OpenSBI 和 U-Boot。
  这块板子的 BootROM 读不了 TF 卡（`dwmci_s: Response Timeout`），SD 启动模式不可用，
  但 Flash 里的 U-Boot 读写 TF 卡正常。

## TF 卡布局

GPT，两个 FAT32 分区，类型都是普通数据（basic data）：

| 分区 | 大小 | 卷标 | 内容 |
|---|---|---|---|
| 1 | 256 MB | ZBOOT | `ZONIX.EFI`、`KERNEL.ELF`、`dtbs/jh7110-starfive-visionfive-2-v1.3b.dtb` |
| 2 | 8 GB | ZONIX | 基础 rootfs（`bin/riscv64/rootfs.img` 里的全部文件） |

内核把 GPT 普通数据分区注册为 `mmc0p1`、`mmc0p2`，根文件系统挂载程序在其中找到带
`/sbin/init` 的分区。

## 网络加载内核

U-Boot 环境（保存在 Flash）每次上电先用 TFTP 从开发机取内核，写进第 1 分区再启动；
取不到时启动卡上已有的版本：

```
bootcmd=run zonix_net; run zonix_boot
zonix_net=setenv autoload no; dhcp && setenv serverip <开发机地址> && tftpboot 0x50000000 KERNEL.ELF && fatwrite mmc 1:1 0x50000000 KERNEL.ELF ${filesize} && tftpboot 0x48000000 ZONIX.EFI && fatwrite mmc 1:1 0x48000000 ZONIX.EFI ${filesize}
zonix_boot=fatload mmc 1:1 ${fdt_addr_r} dtbs/jh7110-starfive-visionfive-2-v1.3b.dtb && fatload mmc 1:1 0x48000000 ZONIX.EFI && bootefi 0x48000000 ${fdt_addr_r}
```

另设 `ethact=ethernet@16040000`、`netretry=no`、`tftptimeout=1000`、
`tftptimeoutcountmax=4`。恢复出厂环境：`env default -a; saveenv`。

TFTP 目录里放 `bin/riscv64/kernel`（改名 `KERNEL.ELF`）和 `bin/riscv64/BOOTRISCV64.EFI`
（改名 `ZONIX.EFI`）。Windows 开发机可用 `scripts/tftpd.ps1`（只读，服务所在目录，
需要放行 UDP 69 入站）。

## 显示

内核自己驱动 HDMI（固件不提供帧缓冲）：打开 PMU 的 VOUT 电源域和 SYSCRG/VOUTCRG 的显示时钟、解除复位，Innosilicon HDMI 发送器的 PHY 锁定后输出 1080p60，DC8200 从内存里的 XRGB8888 帧缓冲扫描输出；像素时钟取 PLL2（1188 MHz）÷ 8。显示控制器读内存不经过 CPU 缓存，CPU 写帧缓冲后经 L2 缓存控制器（`0x02010000` 的 Flush64）写回，写帧缓冲的代码都调用 `display::flush`。热插拔引脚没有接到发送器，内核不检测显示器是否连接，也不读 EDID，显示器须支持 1080p60。

## USB

4 个 USB-A 口来自 PCIe0 上的 VIA VL805。固件不初始化 PCIe0，内核打开 STG 时钟、解除复位、经 GPIO26 释放 PERST#，等待链路建立后给 VL805 分配 32 位内存窗口（`0x30000000` 起）里的 BAR，再由通用 xHCI 驱动接管。PCIe 设备的 DMA 与 CPU 缓存一致。VL805 的 USB 2.0 一侧是它内置的 4 口集线器（2109:3431），USB 2.0/1.1 设备都接在集线器后面，内核的集线器驱动会给它们分配地址（例如 ROG 无线接收器显示为 `usb 1.3`）。设备可以在运行中插拔（`dmesg usb` 查看枚举和移除记录）。集线器刚上电时各口会报告一次过流变化，只记调试日志；1.1 口开机时未供电，原因待查。不支持 USB 3 集线器。

USB 蓝牙适配器由 HCI 层接管：开机或插入后复位、读出地址和版本，并扫描约 10 秒，列出附近的经典蓝牙和 BLE 设备（`dmesg bluetooth`；要扫描的设备先进入配对模式，重插适配器即重扫），已验证 33FA:0012（蓝牙 6.0，未下载固件即响应命令；是否完全不需要固件，要到扫描能用时才能确认）。处于配对模式的 LE 键盘和鼠标会被自动连接、配对并使用，开机时或之后任何时候进入配对模式都可以，最多同时 4 个（已验证 B.O.W HB399 键盘和 B.O.W 鼠标）；设备休眠断开后按键或移动即自动重连（键盘已验证，鼠标的重连尚未验证）。鼠标按其报告描述符解析（report 模式），没有可用描述符时用 boot 协议。配对信息目前只在内存里，板子重启或重插适配器后需要让设备重新进入配对模式。暂不支持经典蓝牙键鼠。

## 已知情况

- 只使用前 1 GB 内存：riscv64 直接映射只覆盖 DRAM 的第一个 1 GB，其余内存不交给分配器。
- 引导程序读 `tp` 得到 hart 号；当前固件从 hart 1 启动，PLIC S 模式上下文为 2。
- 串口是 DesignWare APB UART：内核不改固件设好的线路参数，并处理它的 busy-detect 中断。
- `shutdown` 和 `reboot` 由 OpenSBI 经 I2C5 通知 AXP15060 PMIC 完成。U-Boot 启动 OS 时关掉了 I2C5 的时钟并把控制器复位成高速、10 位地址模式，而 OpenSBI 只设目标地址和使能位，所以内核在 SBI 复位前打开时钟、解除复位，并把控制器设回 7 位标准速率主机模式。
- 尚无：网卡驱动，RTC。
