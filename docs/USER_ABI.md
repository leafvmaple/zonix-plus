# 最小用户程序 ABI

系统基础组件和应用共用静态 ELF、spawn/wait、文件及当前目录接口。init、shell 优先使用 Clang 交叉编译；应用可以使用 zcc、Clang 或兼容的交叉 GCC。编译器选择不决定执行权限或安装归属。

三架构共用 `include/abi/` 契约与现有 `user/zcc/src/runtime/minilib.h`。程序入口、系统调用汇编、基础库和链接脚本仍在 `user/zcc/src/runtime/` 维护，供两条构建路径共用；不新增第二套运行时。基础组件只依赖这套运行时，不依赖 zcc 编译器。

## 目标与工具链

| 架构 | 当前共用的用户态编译选项 |
| --- | --- |
| x86_64 | `-m64 -mno-red-zone -march=x86-64 -msse2 -mno-avx` |
| aarch64 | `-mgeneral-regs-only`，共用运行时和基础程序不使用 FP/SIMD，Desktop 套件的 GUI 程序去掉此选项使用浮点；内核在切换任务时保存 q0–q31、FPCR 和 FPSR，新任务从全零状态开始 |
| riscv64 | `-march=rv64imac -mabi=lp64 -mno-relax`，整数 ABI；Desktop 套件的 GUI 程序用 `-march=rv64gc -mabi=lp64`，使用浮点指令但仍按整数寄存器传参；内核让每个任务都开着 `sstatus.FS`，切换任务时保存 f0–f31 和 fcsr，新任务从全零状态开始 |

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
| unlink | 10 | 删除文件；打开的文件仍由 FAT 的 Busy 规则保护 |
| chdir | 12 | 切换本进程当前目录；成功返回 0，失败保持原目录 |
| getpid | 20 | 返回当前 PID |
| sync | 36 | 同步已挂载文件系统及其设备缓存；成功返回 0，失败返回负 Error |
| kill | 37 | 单用户权限下可终止普通用户进程，init/内核任务受保护；成功返回 0 |
| rename | 38 | `sys_rename(path, new_name)` 在原目录内重命名，成功返回 0 |
| mkdir / rmdir | 39 / 40 | 创建目录 / 删除空目录；相对和绝对路径，成功返回 0 |
| ioctl | 54 | 通用 FD 控制；支持终端尺寸、前台原始输入/命令组与受控显示会话 |
| fcntl | 55 | `F_DUPFD_CLOEXEC` 复制并标记 FD，`F_SETFL` 修改管道端点的非阻塞模式 |
| dup2 | 63 | 将旧 FD 复制到指定目标，返回目标 FD；无效参数返回负 Error |
| readdir | 89 | 相对/绝对目录路径、从零开始的索引、`DirectoryEntry*`；1 表示一项，0 表示结束 |
| ftruncate | 93 | 只支持长度 0；FD 必须可写，不改变当前偏移 |
| stat | 106 | 相对/绝对路径、`FileStat*`；成功返回 0 |
| fstat | 108 | FD、`DescriptorStat*`；返回元数据和可比较的设备/文件身份 |
| yield | 158 | 主动让出 CPU，返回 0 |
| getcwd | 183 | 复制含 NUL 的当前目录；成功返回 0，容量不足返回 Full |
| pipe2 | 331 | 创建匿名字节流的读/写 FD，flags 为 0 或 O_CLOEXEC / O_NONBLOCK 的组合；返回 0 或负 Error |
| spawn | 400 | 相对/绝对 ELF 路径、argc、以 NULL 结束的 argv；返回新 PID |
| shutdown | 401 | 复用有序关机；成功不返回，失败返回负 Error 并恢复存储访问 |
| spawn_foreground | 402 | 与 spawn 相同参数，发布子进程前交接控制台；返回新 PID |
| process_next | 403 | long 型 PID 游标、`ProcessInfo*`；1 表示一项，0 表示结束，负值表示错误 |
| wait_readable | 404 | 等待指定 FD 位图中至少一项就绪，返回就绪子集或负 Error |
| openpty | 405 | 创建伪终端 master/slave FD，复制调用者指定的终端尺寸 |
| uptime_ms | 406 | 返回本次启动以来的单调时间，单位毫秒 |
| realtime | 410 | 返回 1970-01-01 00:00:00 UTC 以来的秒数，平台没有时钟时返回 NoDevice |
| brk | 411 | `sys_brk(long end)` 把程序断点移到 `end` 并返回新断点；传 0 只返回当前断点。见下文“程序断点” |
| wait_readable_for | 407 | 按 FD 位图等待读就绪，超时返回 0 |
| getrandom | 408 | 读取系统随机字节，每次最多 256 字节，返回实际字节数或负 Error |
| wait_io | 409 | 同时等待 FD 的读就绪和写就绪，超时返回 0 |
| channel_listen | 412 | `sys_channel_listen(name, flags)` 占用一个通道名，返回监听 FD。见下文“命名本地通道” |
| channel_connect | 413 | `sys_channel_connect(name, flags)` 连接通道名，返回双向流 FD |
| channel_accept | 414 | `sys_channel_accept(fd, flags)` 从监听 FD 取出一条已连接的流 |
| reboot | 415 | 与 shutdown 相同的有序同步，然后重启机器；成功不返回，失败返回负 Error 并恢复存储访问 |
| set_priority | 416 | `sys_set_priority(pid, priority)` 设置调用者（pid 为 0）或其子进程的调度优先级，范围 `PROCESS_PRIORITY_HIGHEST`(5) 到 `PROCESS_PRIORITY_LOWEST`(20)，数值越小越先运行，默认 10；越界返回 Invalid，目标不是自己或子进程返回 NotFound。被唤醒的任务优先级高于正在运行的任务时立即抢占 |

进程、元数据、文件变更、同步和关机调用失败返回负的内核 `Error` 值，包括 NoMem=-2、Invalid=-3、NotFound=-4、Busy=-9、NoDevice=-10、NotSupported=-11、Full=-12、Access=-14、Interrupted=-15。它们不是 Linux 系统调用的兼容实现。`include/abi/process.h` 定义用户侧结构、常用错误和参数限制；结构中类型、长度和属性字段均为 32 位无符号整数。zcc 运行时的系统调用定义必须与内核完整一致，进程结构和文件标志也必须同步；ABI 检查不允许单方面新增调用。原有 `FileStat`（12 字节）、`DirectoryEntry`（44 字节）和旧 syscall 语义保持不变。

`DescriptorStat` 为 32 字节：type/size/attrs/reserved 各 32 位，随后 device/inode 各 64 位；reserved 固定为零。device/inode 非零时，可在句柄存活期间比较文件身份；零表示后端不提供身份。它们不是跨重启持久 ID。FAT 使用目录项位置作为文件标识，VFS 使用已注册块设备编号作为设备标识，不暴露内核地址。

`ftruncate(fd, 0)` 持有同一打开文件描述的互斥锁，并通过现有文件系统同步边界截断文件；非零长度返回 NotSupported。只读 FD 返回 Access。mkdir/unlink/rmdir 使用与 open/stat 一致的路径复制、当前目录和规范化规则。

rename 接受相对或绝对源路径，第二个参数是原目录内的单个新名称。FAT 支持 8.3 名称，保留目录项位置、数据和属性。目标存在时返回 Exists，挂载点和只读条目受 Access 保护，带长文件名的 FAT 条目返回 NotSupported。

FAT 可按已有 VFAT 长名枚举、查找和修改文件内容。`DirectoryEntry.name` 仍为 32 字节，长名支持最多 31 字节的 ASCII。超出范围或长名记录无效时使用 8.3 别名。新建条目使用 8.3 名称，删除通过短名处理。

用户指针按读写方向验证并复制。wait 状态写回成功后才回收子进程；错误的状态指针不会消耗退出结果。用户异常退出码为 -1，kill 退出码为 -128。kill 请求在返回用户态前生效；等待子进程、控制台输入及 FD 锁的阻塞可被唤醒并正常展开。其他进行中的内核操作须先完成和释放资源，不能从其活动栈中强行销毁任务。

## 程序断点

exec 把程序断点放在最高的 LOAD 段按页向上取整的末尾，堆起初为空。`sys_brk(end)` 移动断点：增长时立即映射清零的可读写、不可执行页，缩小时释放断点所在页之上的页，断点所在页之下的内容保留。`end` 低于初始断点或超过用户栈下方一页的保护页时返回 Invalid；内存不足时返回 NoMem，已映射的新页全部撤回，断点不变。断点不必按页对齐，返回值就是传入的 `end`。没有堆的地址空间（内核线程）调用时返回 Invalid。目前没有按进程的内存上限，堆最多用到物理内存耗尽为止。

## 进程查看与终止权限

`sys_process_next(long after_pid, ProcessInfo*)` 从游标 -1 开始，包括 PID 0；每次返回当前仍存在且大于游标的最小 PID，调用者以下一条返回的 pid 继续。游标限于 [-1, INT_MAX]，其他值返回 Invalid；用户输出区必须完整可写。返回 0 时不修改输出，复制失败不推进任何内核游标，可用相同 PID 重试。单条元数据在中断保护下复制，复制到用户态时不保留 Task 指针；PID 当前单调分配、退出不会导致列表下标位移漏项。枚举包含 zombie，但不保证多次调用期间的全表快照。

`ProcessInfo` 固定为 48 字节：pid/ppid 为 32 位有符号整数，state/kind 为 32 位无符号整数，name 为 32 字节含 NUL 的名称。无父进程时 ppid=-1。状态固定为 Uninit=0、Sleeping=1、Runnable=2、Running=3、Zombie=4；类型为 User=1、Kernel=2。内核枚举值与这些 ABI 常量通过静态断言核对，不导出内部布局或地址。

当前没有 UID、凭据或用户隔离，普通用户进程具有同等的查看和终止权限；不能用临时父子关系代替权限身份。kill 可请求终止其他普通用户进程，包括兄弟和已被 init 收养的进程；wait 仍只回收直接子进程。PID 必须为正 int，PID 0/负数/越界返回 Invalid，不提供 POSIX 进程组或广播语义。不存在或已是 zombie 的目标返回 NotFound。合法正 PID 指向 init 或使用内核 MM 的任务时返回 Access，复用 Ctrl-C 同一终止保护。shell 是普通用户进程，终止后由 init 重启。成功返回 0 仅代表请求受理，仍由既有安全边界、FD 清理、zombie 发布和父进程 wait 完成退出与回收；以后引入用户/凭据时须在此权限边界增加检查。

## 终端控制 ABI

`include/abi/terminal.h` 与共用运行时副本同步。`sys_ioctl(fd, request, argument)` 的 argument 是 64 位 long：

| request | argument / 结果 |
| --- | --- |
| `TERMINAL_GET_SIZE` (0x5601) | 用户可写 `TerminalSize*`；复制 rows/cols 两个 uint32，固定 8 字节 |
| `TERMINAL_SET_RAW` (0x5602) | 立即值 0/1；1 将 Ctrl-C 交给前台程序，0 释放租约并恢复终端 |
| `TERMINAL_JOB` (0x5603) | 立即值 BEGIN=1 / CANCEL=2 / END=0；由前台用户任务管理一组前台命令 |

请求作用于真正的 console File；普通文件返回 NotSupported，无效 FD/指针/模式值返回 Invalid。VFS control 接口只接收内核局部参数，不持有用户地址。尺寸来自 framebuffer 字体网格；x86 文本模式为 80×25，纯串口 aarch64/riscv64 无 framebuffer 时使用 80×24。首版没有运行时尺寸变更通知。

只有当前前台用户任务可设置模式；init、内核任务及后台任务返回 Access。租约由 PID 持有，不随 FD 复制而复制，前台交接、拥有者退出或 panic 会释放。释放时丢弃应用残留输入、重置控制序列、清屏并显示光标；其他控制台输出在占用期间暂存至 1 KiB 缓冲，释放后显示，超限有截断提示。不是 POSIX termios 的完整实现。

输出使用通用 ANSI 子集：`CSI row;col H`/`f`（从 1 开始，缺省 1）、`CSI nG`（当前行第 n 列，缺省 1）、`CSI 2J`（清屏）、`CSI K`/`2K`（清到行尾/清整行）、`CSI ?25h`/`?25l`（显示/隐藏光标）。同一流发送到串口并由共享解析器交给 framebuffer 或架构文本后端，坐标在后端按实际网格限界。PS/2/VirtIO 方向键统一发出 `ESC [ A/B/C/D`，Home/End 发出 `ESC [ H/F`，Delete 发出 `ESC [ 3 ~`；串口可直接发送相同协议。

基础组件可使用 Clang C/C++，C++ 组件使用相同 zstl include root、禁用宿主 C++ CRT/异常/RTTI，共用 C 链接运行时声明。zcc 的显式应用路径继续处理 C；选择 zcc 构建 C++ 应用会报错，不静默切换编译器。

## 控制台前台归属

`spawn_foreground` 与普通 spawn 共用加载、参数复制、FD 继承和资源所有权实现；仅新增发布阶段的原子控制台交接。无控制者时只有 init 可创建控制者（正常启动为 shell）；有控制者时只有当前前台任务可把控制台交给新子进程。其他调用者返回 Access，不发布子进程，也不改变控制台归属。若创建者已收到终止请求，或控制者的输入队列中已有 Ctrl-C，则取消本次前台创建、返回 Interrupted，不发布子进程；消费对应中断输入，shell 记录 -128。普通 spawn 不改变前台 PID。前台任务退出时在发布 zombie 状态前恢复父进程归属；若父进程已退出、子进程被 init 收养，则恢复控制者。控制者退出后清空归属，允许 init 重新启动它。控制台只保存 PID，不保留可悬空的 Task 指针。

前台控制者读取输入时，Ctrl-C 作为字节 3 交给用户态取消输入行；其他前台任务运行时，控制台消费 Ctrl-C 并丢弃待读输入，通过既有安全终止机制请求退出，退出码为 `PROCESS_STATUS_KILLED`（-128）。init 和所有内核 MM 任务共用同一终止保护；IRQ 不关闭 FD、不释放地址空间、不直接销毁任务。前台之外的控制台读取失败（旧 read ABI 返回 -1）；输出仍可共享。Shift/Ctrl 转换由 PS/2 和 VirtIO 键盘共用，串口直接提供相同字节。

单命令保留上述单前台 PID 行为；最小管道使用下述命令组协议。它们不是 POSIX 信号、通用进程组或后台作业控制。EOF、回显和行编辑仍由读取程序解释，Ctrl-D 不直接终止任意用户程序。

## 前台命令组

对真实 console FD 调用 `TERMINAL_JOB_BEGIN`，仅当前前台普通用户任务可成功；已有命令组或原始输入租约时 Busy，init/内核/后台任务 Access。开始前检查已有 Ctrl-C，返回 Interrupted 时不建立命令组。组由拥有者 PID 标识，始终保留拥有者的前台归属以便并发发布多个命令；拥有者在组期间不能读取控制台，组成员可以读取，原始输入租约进一步限制到租约持有者。

拥有者的 `spawn_foreground` 在子进程发布时原子加入该组，不转移单独的前台 PID；组成员产生的用户子进程继承成员身份（包括普通 spawn 和被 init 收养的后代）。Ctrl-C 标记该组中断并请求终止全部成员；已取消的组拒绝继续发布成员。请求沿用 init/内核保护和安全退出边界，不在 IRQ 中销毁任务。后台任务不能借此加入其他人的组。组成员不能再 BEGIN 一个嵌套组；首版由一个拥有者启动并管理整条管道。

只有拥有者可 CANCEL 或 END。CANCEL 请求终止全部成员并封闭后续创建；END 可中断地等待所有成员结束，释放组，并在发生 Ctrl-C 时返回 Interrupted；主动 CANCEL 的错误清理不伪装成键盘中断。shell 仍逐个 wait 回收直接子进程，孤儿由 init 回收。拥有者退出时自动取消该组，终端不会依赖用户清理代码才能恢复。成员身份保存在任务中，只存 PID，不借用其他 Task 的指针。

## 命名本地通道

通道让互不相关的进程按名字建立双向字节流，常量在 `include/abi/channel.h`。名字为 1～31 个可打印 ASCII 字符（不含空格），全系统共享，最多同时 16 个。

- `sys_channel_listen(name, flags)`（412）占用名字并返回监听 FD；名字已被占用返回 Exists，名字无效返回 Invalid，名字表满返回 Full。监听 FD 关闭时释放名字，还没被接受的流随之关闭，连接方读到 EOF。
- `sys_channel_connect(name, flags)`（413）建立一条流，把一端放进监听者的队列并立即返回另一端；没有监听者返回 NotFound，队列已有 `CHANNEL_BACKLOG`（8）条流时返回 Full。连接成功不代表对方已接受，在接受之前写入的字节会留在流里。
- `sys_channel_accept(fd, flags)`（414）取出最早的一条流；队列为空时阻塞，监听 FD 为非阻塞时返回 Busy。fd 不是监听 FD 返回 Invalid。

三个调用的 flags 都接受 0 或 O_CLOEXEC / O_NONBLOCK 的组合，作用于返回的 FD；监听 FD 的非阻塞模式也可用 `F_SETFL` 修改。流的两个方向各有一个 4 KiB 缓冲，读写、非阻塞、就绪和关闭语义与下文的匿名管道相同（含非阻塞写入至多 4096 字节时整块写入或失败），`fstat` 类型同为 FILE_TYPE_PIPE。`ioctl(fd, CHANNEL_SHUTDOWN_WRITE, 0)`（0x5901）结束本端的写方向：对方读完缓冲后读到 EOF，本端之后的写入返回 -1，读方向不受影响。描述符关闭时两个方向都结束。

## 匿名管道 ABI

`sys_pipe2(int ends[2], int flags)`（331）创建读端 ends[0]、写端 ends[1]；flags 接受 0 或 O_CLOEXEC / O_NONBLOCK 的组合。先验证完整可写输出范围和两个空 FD 槽，再分配端点/描述，失败回收全部资源；无效 flags/地址返回 Invalid，FD 不足返回 Full，分配失败返回 NoMem。每个进程仍最多 16 个 FD。`fstat` 类型为 FILE_TYPE_PIPE=4，size/device/inode 为 0；不表示磁盘文件。

两个端点通过 zstl move-only owner 和领域释放器引用同一 4 KiB 环形缓冲；dup/spawn 共享打开描述，最后一个该方向的描述关闭后才关闭端点。管道不维护文件偏移，不参与存储冻结。读到已有数据即可短读返回，不为填满用户缓冲等待下一批；空缓冲在仍有写端时睡眠，否则返回 0。写满缓冲时睡眠，读端关闭唤醒写者并返回 I/O 错误，不产生 SIGPIPE。两端关闭均唤醒等待者；终止请求可打断等待。read/write 沿用旧 ABI：失败且未传输时返回 -1，已传输时可返回正的短计数；调用者负责循环。

`O_NONBLOCK` 由 `pipe2` 设置在两个端点上。`sys_fcntl(fd, F_SETFL, flags)` 接受 `0` 或 `O_NONBLOCK`，修改一个管道端点后续读写的模式，dup 和 spawn 共享该模式。其他文件类型返回 NotSupported。空管道仍有写端时，非阻塞 read 立即返回 -1。非阻塞写入不等待空间，至多 4096 字节的单次写入要么全部进入环形缓冲，要么返回 -1；更大的调用沿用 I/O 循环的分段与短写语义。最后写端关闭且数据读完后，读返回 0。

每个进程最多 24 个描述符。`sys_wait_readable(long mask)` 接收当前进程非零的 24 位 FD 位图，阻塞至至少一项读就绪，返回所有当前就绪项的位图。管道读端、通道流、伪终端 master 有数据或 EOF 时均就绪，通道监听 FD 有待接受的流时就绪，取得事件模式的显示描述符有待取事件时就绪，随后用 `DISPLAY_READ_EVENT` 读取。控制台和伪终端 slave 有输入时也就绪；它们沿用读取的规则，不允许读取的任务（例如后台任务）等待时返回 Access。注意控制台的 read 会等满请求的字节数，就绪后按 1 字节读取可以避免阻塞。空位、超出范围的位返回 Invalid，其他文件类型返回 NotSupported，无访问权限返回 Access，终止请求可中断等待。就绪是快照，不为共享 FD 预留数据；多个进程读同一端点时仍需处理竞争。

`sys_wait_readable_for(long mask, long timeout_ms)` 采用相同的 FD 位图，超时返回 0。超时取 0～2147483647 毫秒，传 0 检查当前状态后立即返回。`sys_uptime_ms()` 返回启动后的单调毫秒数，精度取决于内核定时器。`sys_realtime()` 返回当前时间，为 UTC 的 Unix 秒数，不含时区。内核在第一次调用时读取一次平台的后备时钟（x86 的 CMOS 时钟、QEMU virt 上 aarch64 的 PL031 和 riscv64 的 Goldfish RTC），之后按开机时长推算，因此不会跟随之后对硬件时钟的修改；没有时钟的平台返回 NoDevice。x86 的 CMOS 时钟只有两位年份，按 20xx 解释，并假定它保存的是 UTC。

`sys_wait_io(long read_mask, long write_mask, long timeout_ms)` 可在同一次调用中等待两个 24 位位图。返回值第 0～23 位为读就绪，第 24～47 位为写就绪。任一位图可为空，合并后至少需要一个 FD。管道写端和 PTY master 在可以写入至少一个字节或对端关闭时报告写就绪。就绪只反映当前状态，调用者仍需处理短写、竞争和关闭。

## 内核日志

内核日志保存在固定大小的环形缓冲里（512 条，每条至多 256 字节），每条带开机以来的毫秒数和级别。`/dev/kmsg` 让程序读取和追加日志：

- 每次 open 得到独立的读取位置，从缓冲中最早的一条开始。read 每次返回若干完整的行，格式与控制台相同：`[  秒.毫秒] [级别] 内容`；缓冲须至少 288 字节，否则返回 Invalid。读到最新时阻塞，非阻塞模式下返回 -1；有新记录时读就绪，可以和其他 FD 一起等待。读取速度跟不上、记录已被覆盖时，先返回一行 `kmsg: N records lost` 再从现存最早的一条继续。
- 每次 write 追加一条记录，内容前自动加上 `程序名[PID]: `，末尾换行可有可无。开头写 `<N>` 选择级别：0～3 为 error，4 为 warn，5～6 为 info，7 为 debug，默认 info。程序的记录只有 warn 及以上会同时显示在控制台上。

`/dev` 下的设备接受 `O_TRUNC` 并忽略它（与 POSIX 相同），所以 shell 的 `echo 文字 > /dev/kmsg` 可以使用；`O_APPEND` 仍返回 NotSupported。

内核 panic 时，panic 的位置、原因和调用栈（`panic called from 0x…` 及 `#N 0x…` 返回地址）也写进日志；内核态异常导致的 panic 之前还会记录出错位置的调用栈（`faulting context: pc 0x…`）。普通程序因页错误或异常被终止时记录一条 warn：`程序名[PID]: page fault at 0x… , pc 0x…; killed`。调用栈沿帧指针回溯，内核为此用 `-fno-omit-frame-pointer` 编译；在宿主机用 `scripts/symbolize.py bin/<arch>/kernel 日志文件` 可把地址换成函数名和源码行，需要用同一次构建的内核 ELF。

## 原始以太网设备

x86、aarch64 和 riscv64 探测到受支持的现代 VirtIO PCI 网卡后都提供 `/dev/net0`，一次只允许一个打开文件描述使用设备。正常情况下由网络服务 netd 打开它，其他程序通过 netd 的 socket 接口联网（见 [USERSPACE.md](USERSPACE.md#联网)）。`NET_GET_MAC`（0x5801）向调用者的可写缓冲复制六字节 MAC。

`read` 每次取一帧，缓冲须至少 1514 字节。接收队列有完成项时 FD 读就绪，可通过上述两种等待接口与其他 FD 一起等待。收到帧时网卡中断立即唤醒等待者；平台没有可用的中断线时退回定时器轮询。`write` 每次提交一个 14 至 1514 字节的以太网帧，字节数含以太网头，FCS 由链路处理；发送队列最多同时容纳 16 帧，队列满时等待完成，单次等候至多约一秒。read/write 失败沿用已有的 `-1` 返回约定。

## 最小伪终端 ABI

`sys_openpty(int ends[2], TerminalSize* size)` 返回 0 或负 Error。size 指向调用者提供的行列数，行列各为 1～4096。ends[0] 是 master，ends[1] 是 slave，新 FD 都标记 CLOEXEC。两端支持双向字节流，slave 提供现有终端尺寸、原始输入和命令组调用；master 可等待输出或挂断。master 输入写入不阻塞，一个独立的字节 3 即使在输入队列满时也会处理。输出采用有界缓冲，slave 写入可阻塞，master 读取就绪数据或 EOF。

创建者让第一个 shell 继承 slave stdin 后用普通 spawn 启动它，建立该伪终端的会话控制者。后代继承会话归属，即使重定向 stdio。会话独立维护前台进程和命令组。master 的单字节 Ctrl-C 按该会话的前台状态处理；仅当会话无前台命令或正在用原始模式时把字节交给读取者。最后关闭 master 或会话控制者退出会挂断，唤醒端点并终止该会话中的进程。所有关联引用释放后才销毁对象。

`TERMINAL_SET_SIZE`（0x5604）通过 master 接收 `TerminalSize*`，行列各为 1～4096。slave 随后的 `TERMINAL_GET_SIZE` 读取新尺寸。

## 系统随机源

`sys_getrandom(void* output, long size, long flags)` 从系统随机源读取字节。`flags` 当前取 0，`size` 不能为负。长度为 0 时返回 0。正长度请求最多写入 256 字节，返回实际字节数，调用者可循环读满。设备缺失返回 NoDevice，设备等候超时返回 Timeout，任务终止可中断等待。

当前后端是现代 VirtIO PCI 熵设备。内核接收设备填充的 DMA 缓冲，检查完成长度，再复制到用户缓冲。设备完成请求时由中断唤醒读者，没有可用中断线时退回定时器轮询。QEMU 来宾的随机质量依赖宿主提供的熵源。

## FD 和资源

0/1/2 为 stdin/stdout/stderr，初始内核启动任务连接 `/dev/console`。spawn 保留未标记 close-on-exec 的父进程 FD 编号；每个 FD 持有一个可移动的 `OpenFileRef` 引用（zstl `unique_ptr` 加领域释放器），共享同一打开文件描述及偏移。跨进程 I/O 持有描述的互斥锁；独立 open 的偏移彼此独立。close 只释放本进程引用，最后一个引用关闭 VFS 文件。

`O_CLOEXEC`（02000000）属于 FD 槽而非共享打开描述，`fd::Table::open` 消费它后再调用 VFS。`sys_fcntl(fd, F_DUPFD_CLOEXEC, minimum)` 返回不小于 minimum 的最小空槽，复制句柄并标记 close-on-exec；FD 或 minimum 不在 [0,24) 时 Invalid，无空槽时 Full。`sys_dup2(old, new)` 先验证源/目标，失败不改变目标；old==new 时保留原标记，否则替换目标并清除其 close-on-exec 标记。两者共享打开描述和偏移，均不重新打开文件。

`ForkPolicy::Share` 保留所有 FD 及标记，`ForkPolicy::Spawn` 仅保留未标记 FD，`Reset` 清空。两个用户态 spawn 接口共用 Spawn 策略，过滤仅影响新进程；父进程的 FD 保持有效。dup2 先取得新引用再释放旧目标，最后关闭在引用计数中断保护之外执行。新 syscall 和 flags 已同步到共用运行时。

控制台 CR/CRLF 在公共入口统一为 LF，跨前台交接保持同一转换状态。其余行编辑和 EOF 仍由 CLI 读取策略解释，不是内核完整 TTY 行规程。共用 `printf` 处理短写、返回实际字符数，并在写入失败时返回 -1，调用者可据此报告重定向输出失败。

exit 先关闭本进程全部 FD，再发布 zombie 状态；wait 在进程离开 CPU 后释放内核栈、用户地址空间及任务。孤儿交给现有 init 回收。路径可使用相对/绝对形式，进程拥有独立当前目录，spawn 继承目录值；目录枚举不承诺在并发目录修改时保持快照。用户态 init/shell、stdin/stdout 重定向与匿名管道已实现。

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

## 显示控制 ABI

`include/abi/display.h` 与运行时副本同步，通过现有 `sys_ioctl` 控制 `/dev/display`：

| request | argument / 结果 |
| --- | --- |
| `DISPLAY_GET_INFO` (0x5701) | 用户可写 `DisplayInfo*`；width/height/stride/format 各 uint32，共 16 字节 |
| `DISPLAY_ACQUIRE` (0x5702) | 立即值 0 保留字符输入，或传 `DISPLAY_ACQUIRE_EVENTS` (1) 选择键鼠事件。可另加 `DISPLAY_ACQUIRE_KEYBOARD_CTRL_C` (2) 将键盘 Ctrl-C 交给 GUI。取得前台独占显示会话，FD 必须可写 |
| `DISPLAY_RELEASE` (0x5703) | 立即值 0，归还当前 File 与 PID 持有的会话 |
| `DISPLAY_PRESENT` (0x5704) | 用户 `DisplayPresent*`，提交一个矩形 |
| `DISPLAY_READ_EVENT` (0x5705) | 用户可写 `DisplayEvent*`，阻塞读取一条键鼠事件，成功返回 0 |
| `DISPLAY_READ_TIMED_EVENT` (0x5706) | 用户可写 `DisplayTimedEvent*`，阻塞读取一条带入队时间的键鼠事件，成功返回 0 |

`DisplayPresent` 固定 32 字节：x/y/width/height/stride/reserved 各 uint32，随后 pixels 为 64 位用户地址（long，与现有 syscall 地址参数一致）。reserved 必须为 0，宽高非零且整个矩形位于当前分辨率内，源 stride 不小于 width×4，单位为字节。像素为 `DISPLAY_FORMAT_XRGB8888`，每字 0x00RRGGBB；Info.stride 是用户紧密缓冲区每行字节数 width×4，不是硬件扫描行 pitch。内核适配固件 RGB/BGR 排列，不暴露设备地址或映射接口。

syscall 边界先复制描述、检查会话和完整源地址跨度，再逐行复制到内核所有的紧密快照，成功后才提交设备；非法指针、矩形、stride、保留位均拒绝，不绘制部分输入。分配失败返回 NoMem；复制和绘制检查终止请求，返回 Interrupted 时可能已绘制部分合法矩形。接口不保证垂直同步或扫描输出原子性，不提供模式/尺寸切换。

只允许当前前台普通用户任务取得会话；init、内核任务和后台任务返回 Access。原始终端租约、前台命令组与显示会话互斥，冲突返回 Busy。无受支持的帧缓冲返回 NoDevice=-10；普通文件不支持该协议。查询不要求会话，提交和归还同时要求当前 PID 与取得会话的打开 File 匹配，继承 FD 不转移权限。dup 共享同一 File，最后关闭释放会话；另一次 open 不能代替拥有者释放。拥有者退出或交接前台即归还，即使子进程还保留旧 File，旧句柄也不能释放之后取得的新会话。

占用期间所有控制台文字输出进入既有 1 KiB 缓冲，超限标记截断；帧缓冲文字绘制、滚屏及定时光标暂停。正常释放、最后关闭、前台交接、拥有者退出或 panic 恢复控制台，丢弃应用残留输入并显示暂存输出。Ctrl-C 沿用安全终止流程，不由 IRQ 关闭 FD。

### 前台键鼠事件

`DisplayEvent` 固定 16 字节，由 type、code、value、modifiers 四个 32 位字段组成，value 为有符号整数。事件只交给以 `DISPLAY_ACQUIRE_EVENTS` 取得会话的 File 与 PID。缺少对应会话返回 Access，输出地址无效返回 Invalid，终止请求可中断阻塞读取。

`DisplayTimedEvent` 固定 24 字节，在 `DisplayEvent` 后附加一个 64 位 `long` 字段 `timestamp_ms`。时间以开机后的非负单调毫秒计，10 毫秒精度，在事件入队时记录。两种读取请求使用同一事件队列。

| type | code 与 value |
| --- | --- |
| `DISPLAY_EVENT_KEY` | 常用键使用 Linux evdev 键码，0 释放、1 按下、2 重复（后端上报时）。PS/2 重复扫描码以按下交付 |
| `DISPLAY_EVENT_BUTTON` | 左、右、中键各有独立 code，0 释放、1 按下 |
| `DISPLAY_EVENT_REL` | X、Y 或垂直滚轮，value 为有符号增量。X 向右、Y 向下、滚轮向上为正 |
| `DISPLAY_EVENT_RESET` | code 和 value 为 0。会话开始或队列溢出时通知应用清除已记录的按键和按钮状态 |

modifiers 是处理完该事件后的 Shift、Ctrl、Alt、Meta 位掩码。左右同一修饰键独立维护，任一按住则聚合位有效。RESET 也携带此刻的修饰键快照。队列最多保留 63 条事件，溢出时丢弃未读事件和触发溢出的事件，放入一个 RESET。应用自行累加相对位移及限制指针边界。

事件模式将硬件键盘与鼠标输入交给会话，终端字符读取返回 Access。默认 Ctrl-C 由控制台处理。显示会话选用 `DISPLAY_ACQUIRE_KEYBOARD_CTRL_C` 后由 GUI 按焦点分派键盘 Ctrl-C。串口输入的 Ctrl-C 仍可终止物理前台程序（适用于有串口输入的架构）。其他串口字符在事件模式期间丢弃。释放会话清空事件和遗留字符，恢复现有 shell 字符流。字符模式始终沿用原有键盘映射和控制序列。

x86 PS/2 提供相对移动及三键，滚轮由 aarch64/riscv64 的 VirtIO 鼠标提供。当前 ABI 面向一个前台座席，支持相对鼠标与常用键，不提供触摸、绝对坐标、设备热插拔或多键盘独立状态。
