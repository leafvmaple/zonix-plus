# 最小用户程序 ABI

系统基础组件和应用共用静态 ELF、spawn/wait、文件及当前目录接口。init、shell 优先使用 Clang 交叉编译；应用可以使用 zcc、Clang 或兼容的交叉 GCC。编译器选择不决定执行权限或安装归属。

三架构共用 `include/abi/` 契约与现有 `user/zcc/src/runtime/minilib.h`。程序入口、系统调用汇编、基础库和链接脚本仍在 `user/zcc/src/runtime/` 维护，供两条构建路径共用；不新增第二套运行时。基础组件只依赖这套运行时，不依赖 zcc 编译器。

## 目标与工具链

| 架构 | 当前共用的用户态编译选项 |
| --- | --- |
| x86_64 | `-m64 -mno-red-zone -march=x86-64 -msse2 -mno-avx` |
| aarch64 | `-mgeneral-regs-only`，当前用户态不使用 FP/SIMD |
| riscv64 | `-march=rv64imac -mabi=lp64 -mno-relax`，整数 ABI |

Clang 必须选择相应 `*-none-elf` 目标；其他编译器必须自身面向相同目标。共享 C 选项使用 freestanding、禁用宿主头文件/内建函数、栈保护及 PIC/PIE，链接时只显式使用共同的启动对象和运行时库。当前没有动态链接器或宿主 libc 兼容层。

## 程序入口

加载 64 位静态 ELF 的 `e_entry`，不支持动态链接器。用户栈为四页，初始 SP 按 16 字节对齐；从低地址起依次为：

```text
SP -> argc                64 位
      argv[0..argc-1]     64 位用户指针
      NULL                argv 结束
      NULL                空环境结束
      对齐空隙及参数字符串
```

`_start` 调用 `int main(int argc, char** argv)`，再将返回值交给 exit。`argv[0]` 由调用者提供；`argc=0, argv=NULL` 合法。最多 16 个参数、合计 4096 字节（包括各字符串的 NUL）；路径最多 128 字节（包括 NUL）。spawn 将路径和参数复制为内核所有的值，再创建地址空间和任务，不保留调用者指针。全部资源准备好后才发布 PID 和调度链接。

## 系统调用

| 调用 | 编号 | 契约 |
| --- | ---: | --- |
| exit | 1 | 退出，父进程取得完整的 `int` 退出码 |
| read / write | 3 / 4 | 返回实际字节数；无字节成功时错误返回 -1 |
| open / close | 5 / 6 | 沿用文件 ABI；`mode=0`，错误返回 -1 |
| wait | 7 | PID>0 等指定直接子进程，PID=0 等任意直接子进程；返回被回收的 PID，状态指针可为空 |
| chdir | 12 | 切换本进程当前目录；成功返回 0，失败保持原目录 |
| getpid | 20 | 返回当前 PID |
| kill | 37 | 仅允许终止自身或直接的用户态子进程；成功返回 0 |
| readdir | 89 | 相对/绝对目录路径、从零开始的索引、`DirectoryEntry*`；1 表示一项，0 表示结束 |
| stat | 106 | 相对/绝对路径、`FileStat*`；成功返回 0 |
| yield | 158 | 主动让出 CPU，返回 0 |
| getcwd | 183 | 复制含 NUL 的当前目录；成功返回 0，容量不足返回 Full |
| spawn | 400 | 相对/绝对 ELF 路径、argc、以 NULL 结束的 argv；返回新 PID |

新进程和元数据调用失败返回负的内核 `Error` 值，包括 NoMem=-2、Invalid=-3、NotFound=-4、Busy=-9、NotSupported=-11、Full=-12、Access=-14、Interrupted=-15。它们不是 Linux 系统调用的兼容实现。`include/abi/process.h` 定义用户侧结构、常用错误和参数限制；结构中类型、长度和属性字段均为 32 位无符号整数。zcc 运行时的系统调用定义必须与内核完整一致，进程结构和文件标志也必须同步；ABI 检查不允许单方面新增调用。

用户指针按读写方向验证并复制。wait 状态写回成功后才回收子进程；错误的状态指针不会消耗退出结果。用户异常退出码为 -1，kill 退出码为 -128。kill 请求在返回用户态前生效；等待子进程、控制台输入及 FD 锁的阻塞可被唤醒并正常展开。其他进行中的内核操作须先完成和释放资源，不能从其活动栈中强行销毁任务。

## FD 和资源

0/1/2 为 stdin/stdout/stderr，初始内核启动任务连接 `/dev/console`。spawn 保留父进程的 FD 编号；每个 FD 持有一个可移动的 `OpenFileRef` 引用（zstl `unique_ptr` 加领域释放器），共享同一打开文件描述及偏移。跨进程 I/O 持有描述的互斥锁；独立 open 的偏移彼此独立。close 只释放本进程引用，最后一个引用关闭 VFS 文件。

exit 先关闭本进程全部 FD，再发布 zombie 状态；wait 在进程离开 CPU 后释放内核栈、用户地址空间及任务。孤儿交给现有 init 回收。路径可使用相对/绝对形式，进程拥有独立当前目录，spawn 继承目录值；目录枚举不承诺在并发目录修改时保持快照。用户态 init/shell 已实现，重定向与管道仍待完成。

## 构建与定向验证

`make ARCH=<arch> rootfs` 用 Clang 构建 init/shell。应用使用 `make ARCH=<arch> user USER_CC=zcc` 或 `USER_CC=clang`；兼容的交叉编译器命令也可通过 `USER_CC` 指定。`make ARCH=<arch> zcc-examples` 明确经 zcc 构建编译器示例。启动镜像在任何模式下（包括 `TEST=1`）均不依赖、不打包用户 ELF；只包含引导文件与内核。

QEMU 测试入口明确选择 zcc 构建既有用户态夹具，并通过 `zcc-examples` 构建编译器示例，再准备临时用户数据盘。ELF 只进入该用户盘，内核测试从 `/mnt` 加载；AArch64/RISC-V 通过已有 SDHCI 驱动连接第二张卡。构建 `TEST=1` 系统镜像不会自动准备用户程序。原有 x86 SSE 寄存器测试仍使用测试自身入口与专用 Clang 指令生成配置。

```sh
make ARCH=x86 TEST=1 bin/x86/zonix.img bin/x86/zonix-uefi.img
python3 scripts/test_lifecycle_qemu.py x86 --boot bios
python3 scripts/test_lifecycle_qemu.py x86 --boot uefi
make ARCH=aarch64 TEST=1 bin/aarch64/sdcard.img
python3 scripts/test_lifecycle_qemu.py aarch64
make ARCH=riscv64 TEST=1 bin/riscv64/sdcard.img
python3 scripts/test_lifecycle_qemu.py riscv64
```

定向脚本复制磁盘并注入 `/P1.RUN`，只运行生命周期场景；正常完整 QEMU 回归也包含这些场景，无需额外增加 CI 启动次数。运行回归仍需遵守仓库的当前任务测试授权规则。

两条构建链：

- 基础组件 / Clang 应用：`C → 目标 Clang → 对象文件 → LLD + 共用 crt0/libzccrt → ELF`。
- zcc 应用 / 示例：`C → zcc 解析/类型与 LLVM IR → llc → 汇编器 → 链接器 + 同一套 crt0/libzccrt → ELF`。

zcc 沿用 LLVM 工具做预处理、目标指令生成和汇编，并沿用 Clang 编译底层运行时库；选择 zcc 的程序，其 C 解析和 IR 生成实际经过 zcc，不在失败时静默回退到 Clang。当前补齐了本阶段所需的 64 位 long、指针、数组、函数声明、简单结构体和类型转换，仍是 C 子集，不能宣称兼容完整 C。

zcc 是独立 Git 子模块。修改该子模块时，后续提交必须先记录子模块改动，再更新主仓库 gitlink；本地检查及 pre-commit 的隔离快照都使用对应运行时头文件，不能只提交父仓库而遗漏 zcc 源码。
