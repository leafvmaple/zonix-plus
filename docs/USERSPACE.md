# 用户态启动与 shell

正常启动（`TEST=0`）的顺序是：内核挂载系统盘到 `/`、独立用户盘到 `/user`，加载 `/user/INIT.ELF` 为 PID 1；init 启动 `/user/SH.ELF`，等待并回收子进程，shell 退出或发生用户态异常后重新启动它。失去 init 属于致命启动错误，不能终止 PID 1。

系统镜像始终只包含引导和内核；构建它不编译或打包用户程序。init、shell 和示例 HELLO 都经 zcc 前端及其运行时构建，安装用户盘是独立操作：

```sh
make ARCH=x86 TEST=0 -j8
make ARCH=x86 TEST=0 user-image
make ARCH=x86 TEST=0 qemu       # UEFI
# 或 make ARCH=x86 TEST=0 qemu-bios

make ARCH=aarch64 TEST=0 bin/aarch64/sdcard.img -j8
make ARCH=aarch64 TEST=0 user-image
make ARCH=aarch64 TEST=0 qemu

make ARCH=riscv64 TEST=0 bin/riscv64/sdcard.img -j8
make ARCH=riscv64 TEST=0 user-image
make ARCH=riscv64 TEST=0 qemu
```

`user-image` 在用户盘不存在时创建 64 MiB FAT32 镜像，之后仅更新 INIT、SH 和 HELLO，保留其他文件。不能同时更新 QEMU 正在使用的用户盘。QEMU 启动目标只要求用户盘已经存在，不隐式构建应用。x86 使用第二块 AHCI/IDE 盘，aarch64/riscv64 使用第二个 SDHCI 控制器上的 SD 卡。`TEST=1` 保留原内核调试 shell 和测试启动方式；回归入口自行准备临时用户盘，不依赖正常用户盘。

## 当前 shell

| 命令 | 行为 |
| --- | --- |
| `help` | 查看当前支持的命令 |
| `pwd` / `cd [目录]` | 查看/切换当前目录；不带参数返回 `/user` |
| `echo [参数…]` | 输出拆分后的参数 |
| `status` | 查看上一条命令的退出状态，不覆盖该状态 |
| `exit [0..255]` | 退出当前 shell；默认返回上一条命令的状态 |
| `hello` | 在 `/user` 查找 `HELLO.ELF` 并前台运行 |
| `./HELLO.ELF` / `/user/HELLO.ELF` | 按相对/绝对路径执行并等待结束 |

参数支持空格/制表符分隔、单引号、双引号、反斜杠转义及空参数。输入限 255 字节，最多 16 个参数（包括 argv[0]）；未闭合引号、尾部反斜杠、参数超限或行超长都拒绝执行。未找到命令的 shell 状态为 127。

控制台目前提供原始字节，shell 自行回显、处理退格（8/127）和 CR/LF。Ctrl-C 字节取消当前输入行；Ctrl-D 字节在空行退出。它们还不是前台进程组信号，也不提供作业控制。现有 x86 PS/2 驱动尚未解码 Shift/Ctrl，因此图形窗口中可使用小写、单引号和 `exit`；aarch64/riscv64 串口可直接输入这些控制字节。键盘修饰键、完整行编辑及终端控制仍属后续输入工作。

这是 P1-2 的 init/shell 增量。内核调试 shell 的文件工具、`sync`、`shutdown` 等命令尚未迁入用户态，不能将调试 shell 的命令表当作当前用户 shell 的功能表。基本工具、编辑器、重定向和管道仍在 TODO 中。

## 当前目录 ABI

每个进程持有独立的绝对当前目录（含结尾 NUL 最长 128 字节），spawn 继承目录值；子进程 `chdir` 不改变父进程。open、spawn、stat、readdir 和 chdir 共用路径解析，支持相对路径、重复斜杠、`.` 和 `..`，不能越过 `/`；斜杠前的分量必须是实际目录。

- zcc `sys_chdir(path)`：成功返回 0，失败返回负 Error，原目录保留。
- zcc `sys_getcwd(buffer, capacity)`：复制含 NUL 的路径，成功返回 0；缓冲区过小返回 `SYS_ERROR_FULL`，无效用户内存返回错误。

syscall 编号在 `include/abi/syscall.h` 定义并与 zcc 运行时同步，三架构入口只实现各自 ABI 的调用桩。
