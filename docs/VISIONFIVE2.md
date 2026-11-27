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

GPT，三个 FAT32 分区，类型都是普通数据（basic data）：

| 分区 | 大小 | 卷标 | 内容 |
|---|---|---|---|
| 1 | 256 MB | ZBOOT | `ZONIX.EFI`、`KERNEL.ELF`、`dtbs/jh7110-starfive-visionfive-2-v1.3b.dtb` |
| 2 | 8 GB | ZONIX | 基础 rootfs（`bin/riscv64/rootfs.img` 里的全部文件） |
| 3 | 1 GB | ZAPPS | 可选应用盘（`bin/riscv64/desktop.img` 里的全部文件），挂载到 `/opt` |

内核把 GPT 普通数据分区注册为 `mmc0p1` 到 `mmc0p3`，根文件系统挂载程序在其中找到带
`/sbin/init` 的分区，其余带 `/bin` 的 FAT 分区作为应用盘挂到 `/opt`。没有第 3 分区时
照常进入命令行。

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

内核自己驱动 HDMI（固件不提供帧缓冲）：打开 PMU 的 VOUT 电源域和 SYSCRG/VOUTCRG 的显示时钟、解除复位，Innosilicon HDMI 发送器的 PHY 锁定后输出 1080p60，DC8200 从内存里的 XRGB8888 帧缓冲扫描输出；像素时钟取 PLL2（1188 MHz）÷ 8。显示控制器读内存不经过 CPU 缓存，CPU 写帧缓冲后经 L2 缓存控制器（`0x02010000` 的 Flush64）写回，写帧缓冲的代码都调用 `display::flush`。热插拔引脚没有接到发送器，内核不检测显示器是否连接，也不读 EDID，显示器须支持 1080p60。DC8200 的光标层显示 32×32 ARGB8888 指针图像，图像放在 4 GiB 以下的一页内存里，Desktop 用它显示鼠标指针（`DISPLAY_SET_CURSOR`）。

## Desktop

在 shell 里执行 `desktop` 启动，用蓝牙或 USB 键鼠操作，F12 退出。更新单个程序不必拔卡：在 U-Boot 里 `run zonix_net` 后 `tftpboot 0x50000000 <文件>`，再 `fatwrite mmc 1:3 0x50000000 bin/desktop ${filesize}`。`desktop --bench` 不读输入，自己打开 Terminal 和 Files、拖动窗口、移动指针各 120 步后退出，结果写进内核日志（`dmesg` 中的 `desktop bench:` 行，依次是项目、次数、总毫秒数、最慢一次的毫秒数），可以经串口完成整轮测量。1080p 下的一次结果：整屏重画 30 ms；打开 Terminal 440 ms、Files 590 ms 后画面稳定，期间最慢的一次重画 10 ms；拖动窗口每步约 8 ms，最慢 20 ms；指针由光标层显示，移动不重画画面。

## 以太网

内核把 U-Boot 用来 TFTP 的那个网口（GMAC1，`0x16040000`，Synopsys DesignWare Ethernet QoS 5.20）作为 `/dev/net0` 交给 netd，用法与 QEMU 的 VirtIO 网卡相同。U-Boot 已经按板子配好了 RGMII、PHY 的时钟延迟和由 PHY 提供的发送时钟，内核只确认 GMAC1 的总线时钟和复位打开，其余沿用固件的设置；没有经过 U-Boot 网络启动时这些设置可能缺失，驱动在软件复位处超时并报告。启动时只登记设备，第一次打开 `/dev/net0`（例如启动 netd）时才复位并配置控制器，所以驱动出问题不会影响开机。

驱动用一个 DMA 通道，32 个接收描述符、4 个发送描述符，每个缓冲 2 KiB。收到帧、发完帧和 DMA 出错时 GMAC1 的中断（PLIC 78 号）唤醒等待者，时钟节拍兜底并每秒查一次链路；统计计数器的中断屏蔽掉，中断线连续触发 1000 次却没有 DMA 事件时驱动关掉 DMA 中断、退回节拍轮询。改用中断后 ping 开发机约 1.3 ms（轮询时约 7 ms），SSH 上传约 22 KiB/s、下载约 300 KiB/s（轮询时 4 和 144 KiB/s）。这个控制器的 DMA 不经过 CPU 缓存，而一个 64 字节缓存行里有 4 个描述符：接收描述符整行一起交还控制器，发送一次只有一帧在途，避免 CPU 写回缓存行时覆盖控制器刚写的描述符。MAC 地址用 EEPROM 里这块板子的地址：U-Boot 把它写进交给 EFI 程序的设备树（`ethernet@16040000` 的 `local-mac-address`），Zonix 的 EFI 加载器把设备树复制到第一个 1 GB 内存里交给内核，驱动从中读出。U-Boot 启动 OS 前停掉网口时会清掉控制器里的地址寄存器，所以没有设备树时才退回 `MAC_ADDRESS0` 里的值，再没有就用 `02:00:5a:6f:6e:01`（`dmesg` 中有警告）。PHY 通过 MDIO 扫描找到，每秒读一次标准寄存器，按协商结果设置 10/100/1000 和双工。寄存器信息取自 NetBSD 的 BSD 许可 `dwc_eqos` 驱动，板级地址、时钟和复位编号取自 Linux 的双许可设备树。

网络程序（netd、ping、netinfo、fetch、nc、tcpserve、sshd）用 `make ARCH=riscv64 ssh-image ...`（参数与 x86 相同，主机密钥目录单独为板子生成）生成，放进 ZAPPS 的 `/bin`，主机密钥和登录公钥放进 `/ssh`，`/etc/services` 写上 netd 和 SSH 服务后开机自动启动。已验证 DHCP、ping 和 OpenSSH 公钥登录（用户名 `zonix`）。

## 随机数

`sshd` 需要系统随机源，板上没有 VirtIO 熵设备，内核改用 JH7110 自带的真随机数发生器（`0x1600C000`）：打开系统总线 STG 口的时钟和 STG 时钟复位控制器里它的两个时钟、解除复位，关掉自动重新播种，开机时播种一次。每次读取发一条生成命令，轮询事件寄存器等结果，从 8 个结果寄存器读出 256 位；事件寄存器报告 LFSR 锁死时先重新播种。中断线不打开。`dmesg` 中有 `random: hardware generator ready`。寄存器、时钟和复位编号取自 Linux 驱动 `jh7110-trng.c` 和设备树的事实。

## USB

4 个 USB-A 口来自 PCIe0 上的 VIA VL805。固件不初始化 PCIe0，内核打开 STG 时钟、解除复位、经 GPIO26 释放 PERST#，等待链路建立后给 VL805 分配 32 位内存窗口（`0x30000000` 起）里的 BAR，再由通用 xHCI 驱动接管。PCIe 设备的 DMA 与 CPU 缓存一致。VL805 的 USB 2.0 一侧是它内置的 4 口集线器（2109:3431），USB 2.0/1.1 设备都接在集线器后面，内核的集线器驱动会给它们分配地址（例如 ROG 无线接收器显示为 `usb 1.3`）。设备可以在运行中插拔（`dmesg usb` 查看枚举和移除记录）。集线器刚上电时各口会报告一次过流变化，只记调试日志；1.1 口开机时未供电，原因待查。不支持 USB 3 集线器。

USB 蓝牙适配器由 HCI 层接管：开机或插入后复位、读出地址和版本，并扫描约 10 秒，列出附近的经典蓝牙和 BLE 设备（`dmesg bluetooth`；要扫描的设备先进入配对模式，重插适配器即重扫），已验证 33FA:0012（蓝牙 6.0，未下载固件即响应命令；是否完全不需要固件，要到扫描能用时才能确认）。处于配对模式的 LE 键盘和鼠标会被自动连接、配对并使用，开机时或之后任何时候进入配对模式都可以，最多同时 4 个（已验证 B.O.W HB399 键盘和 B.O.W 鼠标）；设备休眠断开或板子重启后，按键或移动即自动重连。鼠标按其报告描述符解析（report 模式），没有可用描述符时用 boot 协议。配对信息保存在 `/var/lib/bluetooth/bonds`（LTK 为明文），删除该文件并重启即清除所有配对。暂不支持经典蓝牙键鼠。

## 已知情况

- TF 卡由 dw_mmc 轮询读写，不用 DMA 和中断。每个 512 字节块从发命令到取完数据都关着中断：传输中途若切换任务，卡会填满 FIFO，控制器以主机超时（中断状态里的 HTO 位）结束传输。块传输出错时驱动复位控制器、重新识别卡后重传一次，再失败才让整块盘下线（见 USERSPACE.md 的“根盘故障时的恢复”）。盘下线后串口上仍可用 shell 内置的 `reboot`，看门狗也会在 30 秒内复位整块板子（见下条）。
- 看门狗：内核启动后打开 JH7110 自带的看门狗（`0x13070000`），由一个最高优先级的内核线程每秒喂一次，同时每 5 秒读一个根盘扇区确认磁盘可用。内核卡死、中断长期关闭或根盘读不了时停止喂狗，约 30 秒后看门狗复位整颗芯片，U-Boot 照常用 TFTP 取新内核启动，板子不需要人去现场。打开前先关着复位输出测一次计数速度，计数不在 1–200 MHz 之间就不打开（`dmesg` 中有 `watchdog: armed` 或 `not arming`）。`shutdown` 关机前会先关掉看门狗，关机失败时系统在没有看门狗的状态下继续运行。寄存器（计数初值 0x000、当前值 0x004、控制 0x008 位 0 计数位 1 复位、清中断兼重装 0x00C、锁 0xC00 密钥 0x1ACCE551，第二次到期才复位）取自 Linux 驱动 `starfive-wdt.c` 的事实，没有找到 TRM 中的表。
- 只使用前 1 GB 内存：riscv64 直接映射只覆盖 DRAM 的第一个 1 GB，其余内存不交给分配器。
- 引导程序读 `tp` 得到 hart 号；当前固件从 hart 1 启动，PLIC S 模式上下文为 2。
- 串口是 DesignWare APB UART：内核不改固件设好的线路参数，并处理它的 busy-detect 中断。
- `shutdown` 和 `reboot` 由 OpenSBI 经 I2C5 通知 AXP15060 PMIC 完成。U-Boot 启动 OS 时关掉了 I2C5 的时钟并把控制器复位成高速、10 位地址模式，而 OpenSBI 只设目标地址和使能位，所以内核在 SBI 复位前打开时钟、解除复位，并把控制器设回 7 位标准速率主机模式。
- 尚无：RTC。
