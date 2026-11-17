# 用户态启动与 shell

正常启动（`TEST=0`）使用三个职责分明的产物：

| 产物 | 内容与运行位置 | 是否必需 |
| --- | --- | --- |
| `zonix.img` / `zonix-uefi.img` | 引导和内核，不含任何用户 ELF | 必需 |
| `rootfs.img` | `/sbin/init`、`/bin/sh` 和基础目录，挂载到 `/` | 必需 |
| `apps.img` | 额外应用 `/bin/hello`，接入时挂载到 `/opt` | 可选 |

内核查找包含 `/sbin/init` 的 FAT 文件系统作为基础 rootfs，不按“第一张可挂载盘”猜测根目录。随后加载 `/sbin/init` 为用户态 PID 1，由它启动 `/bin/sh`。init 等待并回收子进程，shell 退出或发生用户态异常后重新启动它。失去 init 属于致命错误，不能终止 PID 1。

接入的额外文件系统包含 `/bin` 时可作为应用盘挂到 `/opt`；未接入时保留基础 rootfs 的空 `/opt` 目录，不影响 init、shell 或 `/bin` 中的程序。应用盘不提供基础 init/shell。init、shell 使用 Clang 交叉编译；应用可选择 zcc、Clang 或兼容的交叉编译器，全部共用现有 ABI 和运行时。三者分别构建：

```sh
# x86：只接基础 rootfs，进入 CLI，不接应用盘
make ARCH=x86 TEST=0 -j8
make ARCH=x86 TEST=0 rootfs
make ARCH=x86 TEST=0 qemu       # UEFI；BIOS 使用 qemu-bios

# aarch64 / riscv64：同样不需要应用盘
make ARCH=aarch64 TEST=0 bin/aarch64/zonix-uefi.img -j8
make ARCH=aarch64 TEST=0 rootfs
make ARCH=aarch64 TEST=0 qemu

make ARCH=riscv64 TEST=0 bin/riscv64/zonix-uefi.img -j8
make ARCH=riscv64 TEST=0 rootfs
make ARCH=riscv64 TEST=0 qemu

# 额外应用是可选的，显式构建并接入
make ARCH=x86 TEST=0 user-image USER_CC=zcc
make ARCH=x86 TEST=0 qemu USER_DISK=bin/x86/apps.img

# 同一应用源码也可选 Clang，无需 zcc 编译器
make ARCH=x86 TEST=0 user-image USER_CC=clang
# 编译器示例单独、明确地经 zcc 构建
make ARCH=x86 TEST=0 zcc-examples
```

启动镜像的规则不依赖 `rootfs`、`user-image` 或任何用户 ELF。`rootfs`/`user-image` 各自创建 64 MiB FAT32 镜像，随后分别更新 init/shell 或额外应用，保留已有其他文件；不要更新正在被 QEMU 使用的镜像。主机上的构建产物仍为 `user/INIT.ELF`、`user/SH.ELF`、`user/HELLO.ELF`，安装时映射到上述目录。

QEMU 默认只接引导盘和基础 rootfs，只有指定 `USER_DISK` 才接入应用盘。x86 的基础 rootfs 使用 AHCI/IDE；aarch64/riscv64 的引导盘通过 VirtIO 提供给固件，基础 rootfs 通过 SDHCI 提供给内核，不再需要正常启动的引导盘镜像副本。

`TEST=1` 保留既有内核调试 shell 和测试根目录；回归入口自行准备临时测试用户盘，通过 `/mnt` 加载测试 ELF。测试启动盘镜像副本 `sdcard.img` 仍供既有回归入口使用。普通 `user` 构建不构建 init/shell，它们由 `rootfs` 入口显式构建。

## 编译器、ABI 与运行时

| 对象 | 构建方式 |
| --- | --- |
| init、shell 等基础组件 | Clang 交叉编译，`USER_CC` 不改变基础组件的编译器 |
| 普通应用 | `USER_CC=zcc`（保留默认路径）、`USER_CC=clang`，或显式指定兼容的交叉 C 编译器命令 |
| zcc 示例 | 显式 `zcc-examples` 目标，始终通过 zcc 前端 |

基础 rootfs 和 Clang 应用目标不依赖 zcc 编译器。当前共用的启动对象 `crt0.o`、系统调用封装、`libzccrt.a`、头文件和链接脚本仍维护在 `user/zcc/src/runtime`；该目录的运行时独立使用 Clang/汇编工具构建，不会构建 zcc 前端，也不引入第二套 CRT 或宿主 libc。

`USER_CC=clang` 使用项目配置的目标 Clang；其他原生编译器命令必须自身指向正确目标，例如交叉 GCC。构建规则传入共同的 freestanding 编译选项和架构 ABI 选项，然后显式链接共享运行时。不能使用宿主默认 ABI 或 Linux/macOS libc。当前本地验证覆盖 zcc 和 Clang，交叉 GCC 尚未实测。

切换应用编译器会更新选择记录并重建应用，避免沿用上一个编译器的 ELF；基础组件不受该选择影响。zcc 示例从普通 `user` 目标中分离，测试入口显式选择 zcc 构建既有夹具，并按需构建这些示例。选择 zcc 时失败会报告错误，不自动换用 Clang。

两条路径都生成符合 Zonix ABI 的静态 ELF；相同的 ELF 容器格式本身不足以保证兼容，还必须匹配目标指令集、调用约定、栈入口、系统调用和数据布局。详情见 [USER_ABI.md](USER_ABI.md)。

## 当前 shell

| 命令 | 行为 |
| --- | --- |
| `help` | 查看当前支持的命令 |
| `pwd` / `cd [目录]` | 查看/切换当前目录；不带参数返回 `/` |
| `echo [参数…]` | 输出拆分后的参数 |
| `status` | 查看上一条命令的退出状态，不覆盖该状态 |
| `exit [0..255]` | 退出当前 shell；默认返回上一条命令的状态 |
| `sh` | 从基础 rootfs 的 `/bin/sh` 启动子 shell |
| `hello` | 接入应用盘后，从 `/opt/bin/hello` 前台运行示例 |
| `./hello` / `/opt/bin/hello` | 按相对/绝对路径执行并等待结束 |

不含斜杠的程序名依次在 `/bin`、`/opt/bin` 查找；带斜杠的名称直接按相对/绝对路径执行。系统程序在盘内无需 `.ELF` 扩展名，仍是符合共同 ABI 的静态 ELF。

参数支持空格/制表符分隔、单引号、双引号、反斜杠转义及空参数。输入限 255 字节，最多 16 个参数（包括 argv[0]）；未闭合引号、尾部反斜杠、参数超限或行超长都拒绝执行。未找到命令的 shell 状态为 127。

控制台目前提供原始字节，shell 自行回显、处理退格（8/127）和 CR/LF。Ctrl-C 字节取消当前输入行；Ctrl-D 字节在空行退出。它们还不是前台进程组信号，也不提供作业控制。现有 x86 PS/2 驱动尚未解码 Shift/Ctrl，因此图形窗口中可使用小写、单引号和 `exit`；aarch64/riscv64 串口可直接输入这些控制字节。键盘修饰键、完整行编辑及终端控制仍属后续输入工作。

这是 P1-2 的 init/shell 增量。内核调试 shell 的文件工具、`sync`、`shutdown` 等命令尚未迁入用户态，不能将调试 shell 的命令表当作当前用户 shell 的功能表。基本工具、编辑器、重定向和管道仍在 TODO 中。

## 当前目录 ABI

每个进程持有独立的绝对当前目录（含结尾 NUL 最长 128 字节），spawn 继承目录值；子进程 `chdir` 不改变父进程。open、spawn、stat、readdir 和 chdir 共用路径解析，支持相对路径、重复斜杠、`.` 和 `..`，不能越过 `/`；斜杠前的分量必须是实际目录。

- 共享运行时 `sys_chdir(path)`：成功返回 0，失败返回负 Error，原目录保留。
- 共享运行时 `sys_getcwd(buffer, capacity)`：复制含 NUL 的路径，成功返回 0；缓冲区过小返回 `SYS_ERROR_FULL`，无效用户内存返回错误。

syscall 编号在 `include/abi/syscall.h` 定义并与 zcc 运行时同步，三架构入口只实现各自 ABI 的调用桩。
