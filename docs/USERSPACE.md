# 用户态启动与 shell

正常启动（`TEST=0`）使用引导盘和基础 rootfs，可按需接入一张应用盘。

| 产物 | 内容与运行位置 | 是否必需 |
| --- | --- | --- |
| `zonix.img` / `zonix-uefi.img` | 引导和内核，不含任何用户 ELF | 必需 |
| `rootfs.img` | `/sbin/init`、`/bin/sh`、基础命令和基础目录，挂载到 `/` | 必需 |
| `apps.img` | 独立额外应用盘，接入时挂载到 `/opt` | 可选 |
| `desktop.img` | Desktop、Terminal、Files 和字体，作为应用盘挂载到 `/opt` | 可选 |

内核查找包含 `/sbin/init` 的 FAT 文件系统作为基础 rootfs，不按“第一张可挂载盘”猜测根目录。随后加载 `/sbin/init` 为用户态 PID 1，由它启动 `/bin/sh`。init 等待并回收子进程，shell 退出或发生用户态异常后重新启动它。失去 init 属于致命错误，不能终止 PID 1。

接入的额外文件系统包含 `/bin` 时可作为应用盘挂到 `/opt`；未接入时保留基础 rootfs 的空 `/opt` 目录，不影响 init、shell 或 `/bin` 中的程序。应用盘不提供基础 init/shell。init、shell 及基础命令使用 Clang 交叉编译；应用可选择 zcc、Clang 或兼容的交叉编译器，全部共用现有 ABI 和运行时。默认构建包含引导、内核和基础 rootfs，可直接启动 CLI。

```sh
# x86：只接基础 rootfs，进入 CLI，不接应用盘
make ARCH=x86 TEST=0 -j8
make ARCH=x86 TEST=0 qemu       # UEFI；BIOS 使用 qemu-bios

# aarch64 / riscv64：同样不需要应用盘
make ARCH=aarch64 TEST=0 -j8
make ARCH=aarch64 TEST=0 qemu

make ARCH=riscv64 TEST=0 -j8
make ARCH=riscv64 TEST=0 qemu

# 额外应用是可选的，显式构建并接入
make ARCH=x86 TEST=0 user-image USER_CC=zcc
make ARCH=x86 TEST=0 qemu USER_DISK=bin/x86/apps.img

# 同一应用源码也可选 Clang，无需 zcc 编译器
make ARCH=x86 TEST=0 user-image USER_CC=clang
# 编译器示例单独、明确地经 zcc 构建
make ARCH=x86 TEST=0 zcc-examples
```

启动镜像的规则仅依赖引导和内核。默认构建另行调用 `rootfs`，可用 `make rootfs` 单独更新基础系统。`rootfs`/`user-image` 各自创建 64 MiB FAT32 镜像，随后分别更新 init/shell 或额外应用，保留已有其他文件；不要更新正在被 QEMU 使用的镜像。主机上的构建产物仍为 `user/INIT.ELF`、`user/SH.ELF`、`user/HELLO.ELF`，安装时映射到上述目录。

QEMU 默认只接引导盘和基础 rootfs，只有指定 `USER_DISK` 才接入应用盘。x86 的基础 rootfs 使用 AHCI/IDE；aarch64/riscv64 的引导盘通过 VirtIO 提供给固件，基础 rootfs 通过 SDHCI 提供给内核，不再需要正常启动的引导盘镜像副本。

`TEST=1` 保留既有内核调试 shell 和测试根目录；回归入口自行准备临时测试用户盘，通过 `/mnt` 加载测试 ELF。测试启动盘镜像副本 `sdcard.img` 仍供既有回归入口使用。普通 `user` 构建不构建 init/shell，它们由 `rootfs` 入口显式构建。

## 编译器、ABI 与运行时

| 对象 | 构建方式 |
| --- | --- |
| init、shell、ls/cat/cp/mkdir/mv/rm/sync/shutdown/reboot/ps/kill/vi/dmesg 等基础组件 | Clang 交叉编译，`USER_CC` 不改变基础组件的编译器 |
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
| `shutdown` / `reboot` | 同步存储后关机/重启，行为见下文“基础命令”；作为内置命令不从磁盘加载程序，根盘失效后仍可在串口上使用 |
| `sh` / `sh -c "命令"` | 启动交互子 shell / 执行一条命令后返回其状态 |
| `hello` | 接入应用盘后，从 `/opt/bin/hello` 前台运行示例 |
| `./hello` / `/opt/bin/hello` | 按相对/绝对路径执行并等待结束 |

不含斜杠的程序名依次在 `/bin`、`/opt/bin` 查找；带斜杠的名称直接按相对/绝对路径执行。系统程序在盘内无需 `.ELF` 扩展名，仍是符合共同 ABI 的静态 ELF。

参数支持空格/制表符分隔、单引号、双引号、反斜杠转义及空参数。输入限 255 字节，最多 16 个参数（包括 argv[0]）；未闭合引号、尾部反斜杠、参数超限或行超长都拒绝执行。未找到命令的 shell 状态为 127。

控制台入口将 CR、CRLF 统一为 LF，状态随控制台字节流保留，避免从 shell 交接给程序时遗留一个 LF。其他控制字节仍由读取程序解释；交互式 shell 使用下述用户态行编辑器；cat 与 shell 的非终端回退仍使用 `user/command.h` 的 CLI 行输入策略，回显写入 stderr，支持退格（8/127）。x86 PS/2 与 VirtIO 键盘共用字符转换，支持左右 Shift/Ctrl 独立按下和释放、Shift 大写及标点、Ctrl 字符；aarch64/riscv64 串口的控制字节走同一控制台入口。

init 通过 `sys_spawn_foreground` 启动控制台控制者 shell；shell 用同一接口启动前台程序。控制台归属在子进程发布前原子交接；若加载期间收到 Ctrl-C，则取消创建，不启动遗漏中断的子进程。普通 `sys_spawn` 不改变归属，后台任务不能读取控制台输入。shell 读命令时，Ctrl-C 清空输入行；运行前台程序时，Ctrl-C 丢弃待读输入并请求终止该程序，不能终止 init 或内核任务。退出仍经过现有安全边界、FD 关闭和 wait 回收；正常退出、异常或终止均恢复父进程的前台归属，shell 可以继续读取命令。Ctrl-D 在空行表示 EOF，shell 随后退出，由 init 重新启动；非空行先交付已有内容、不附加换行，下一次空行 Ctrl-D 才结束输入。

单命令沿用前台 PID；管道通过一个由 shell 持有的前台命令组统一处理输入和 Ctrl-C，详见下文。当前不提供 POSIX 信号、通用进程组或后台作业控制。

## Shell 行编辑

交互编辑在 Clang 用户态 shell 中完成，内核仅提供输入字节、终端尺寸与通用光标操作。stdin/stdout/stderr 均连接控制台时启用；重定向为文件或管道时回退到既有逐行输入，不向文件输出编辑控制序列。`sh -c` 直接执行命令，不进入行编辑。

| 按键 | 操作 |
| --- | --- |
| Left / Right | 在命令内移动一字节，不越过行首/行尾 |
| Home / End | 移到命令开头/末尾 |
| 普通字符、Tab | 在光标处插入；Tab 保留原字节，显示按 8 列制表位展开 |
| Backspace / Delete | 删除光标前/当前位置的一字节；边界处不操作 |
| Up / Down | 浏览较旧/较新的历史；回到最新位置时恢复未提交草稿和光标 |
| Enter | 提交整条命令，与当前光标位置无关 |
| Ctrl-C | 取消当前输入，不加入历史；状态 -128，重新显示提示符 |
| Ctrl-D | 非空行提交已有内容；空行退出 shell，由 init 重启 |

每个 shell 只在内存中保留最近 16 条非空白提交，连续相同命令只保留一条；取消和超长拒绝的输入不入历史。召回的是可编辑副本，改变它不会改写旧记录。上下移动到历史边界时停住；子 shell/重启后的 shell 拥有独立历史，不写历史文件。

输入仍限 255 个 ASCII/Tab 字节，超长行拒绝执行。长命令以单行横向视口显示，光标一起移动；过长当前目录仅在提示符中缩略，实际 cwd 保持不变。提示符从新行开始，保留程序未以 LF 结束的输出；每次重绘清除旧行尾，不留下较长历史记录的残影。串口支持常见 CSI/SS3 方向键、Home/End 与 Delete 编码，未知控制序列被消费，不混入命令。首版不提供补全、Unicode 字符编辑或持久历史。

## 重定向

内置命令和外部程序均支持 stdin/stdout 重定向：

| 语法 | 行为 |
| --- | --- |
| `< file` | 从文件读取 stdin |
| `> file` | 创建或清零文件，将 stdout 写入它 |
| `>> file` | 创建或打开文件，每次写入追加到 EOF |

```sh
echo hello > note.txt
echo world >> note.txt
cat < note.txt
cat < note.txt > copy.txt
ps > process.txt
cat > draft.txt
```

操作符可与普通单词相邻，例如 `cat<note.txt>copy.txt`。引号和转义内的 `<`/`>` 是字面字符，空参数保留。每段最多 16 个参数、16 个重定向；数字 FD 前缀（如 `2>`）、FD 合并和 here-doc 暂不支持。数字参数紧接操作符会被当作不支持的 FD 前缀，应使用 `exit 7 > out` 这样的空格分隔。

先完整检查语法，再按出现顺序应用重定向，随后运行命令。后续重定向取代先前同一 FD 的绑定；失败时恢复 shell 输入输出，但不回滚已经发生的文件创建/清零。仅含重定向的命令也可创建或清零文件。错误诊断保留在 stderr；提示符、下一条命令及 Ctrl-C 的完成提示使用恢复后的终端。

shell 保存原始 0/1 描述符，备份与临时打开的文件均标记 close-on-exec；外部程序只继承最终绑定，不继承这些临时句柄。正常结束、打开/加载失败和前台中断后均恢复并关闭临时 FD；恢复失败则退出当前 shell，由 init 使用自己的正常 stdio 重启。

## 最小管道

```sh
echo hello | cat
cat < source.bin | cat | cat > copy.bin
ls / | cat > listing
```

未引用/转义的 `|` 连接相邻命令的 stdout 与 stdin，最多 8 段；整行仍限 255 字节。先检查整条语法，拒绝空段、首尾 `|`、`||` 和超限段数，语法错误不创建目标文件。引号或转义中的 `|` 保留为参数字符。

所有段并发运行，shell 启动全部段后才等待，避免大于缓冲区的数据把生产者堵在启动消费者之前。每个匿名管道有 4 KiB 缓冲区；shell 及时关闭自己的管道端，子进程只继承选中的 0/1，不继承管道原始 FD 或 stdio 备份。所有写端关闭且缓冲读空后返回 EOF；读端提前关闭会让写入返回错误。各段的显式重定向覆盖默认管道绑定，例如 `echo hello > note | cat` 会将数据写入 note，而 cat 读到 EOF。stderr 不经管道传送。

管道内的内置命令使用基础 `/bin/sh -c` 在独立用户进程中执行；`cd` 和 `exit` 只改变该段，不改变交互 shell。正常管道状态取最后一段，未启用 pipefail；命令组成员内部再启动嵌套管道组暂不支持。管道准备、重定向或加载失败时，shell 关闭所有临时端、取消已启动的命令并回收直接子进程，报告非零状态；已经发生的文件写入不回滚。

Ctrl-C 取消整个前台命令组，包括已继承成员身份的后代，唤醒阻塞读写并走既有安全终止/关闭/回收路径；不终止 shell、init 或内核任务。shell 等到成员退出后再恢复提示符，状态为 -128。拥有者 shell 被 kill 时也取消该组，由 init 恢复 shell。普通命令的原始输入模式仍按全屏应用的终端租约处理 Ctrl-C；这不是 POSIX 作业控制或信号系统。

## 基础命令

以下命令是独立的用户态 ELF，固定用 Clang 构建，随 `make rootfs` 安装到 `/bin`；不需要额外应用盘。它们通过共同的 ABI 和运行时调用现有 VFS 与电源管理接口。内核调试 shell 的命令保留供调试和既有测试使用。

| 命令 | 当前行为 |
| --- | --- |
| `ls [path]` | 默认列出当前目录；文件操作数显示路径，目录名附加 `/` |
| `cat [file...]` | 有文件操作数时依次原样输出；无参数时读 stdin。普通文件输入保持二进制字节，流式处理，不限制为 64 KiB |
| `cp source destination` | 复制普通文件，支持覆盖和目标为目录；识别同文件并在截断前拒绝 |
| `mkdir directory...` | 创建目录；父目录必须存在，不提供 `-p` |
| `mv source name` | 在同一目录内给文件或目录改名，目标可以是新名字或同一目录下的路径；不能移到别的目录。只改大小写也可以，例如 `mv /VAR var` 把旧系统建成大写的 FAT 短名改回小写 |
| `rm [-d] path...` | 默认删除文件；`-d` 也可删除空目录，不提供递归删除 |
| `sync` | 同步已挂载文件系统及其设备缓存，失败返回非零状态 |
| `shutdown` | 排空 I/O、同步文件与设备，再请求平台关机；失败恢复存储访问并返回 shell。已经下线的设备无法同步，不阻止关机 |
| `reboot` | 与 `shutdown` 相同的同步，然后重启机器 |
| `ps` | 按 PID 升序显示 PID、PPID、状态、用户/内核类型和名称；包括尚未回收的 zombie |
| `kill PID...` | 对正十进制 PID 请求安全终止；不接受信号选项、进程组或广播语义 |
| `vi [file]` | 精简全屏编辑器，普通/插入/命令模式；ASCII/LF，最大 64 KiB |
| `logd` | 由 init 开机启动的日志进程，把内核日志写进 `/var/log/kernel.log`，见下文“日志文件” |
| `dmesg [text]` | 打印内核环形缓冲中还保留的日志（最近 512 条，含调试级别）后退出；带参数时只打印包含该文字的行，例如 `dmesg usb` |

shell 把 `shutdown` 和 `reboot` 作为内置命令执行，与 `/bin` 中的同名程序行为相同；这两个程序仍随 rootfs 安装，供其他调用者使用。

### 根盘故障时的恢复

SD 卡传输一个块出错时，驱动先复位控制器并重新识别卡；只有 CID、容量和寻址方式都与原来一致才重传该块。每个请求最多这样复位一次，复位失败、重传仍失败或同一请求再次出错时，卡下线，之后的读写都返回 device unavailable，不再访问硬件。卡下线后 shell 无法加载 `/bin` 或 `/opt/bin` 中的任何程序，但内置的 `reboot` / `shutdown` 仍可用：同步时跳过已经下线的设备（内核日志记一条警告，那里未写入的数据已经无法保存），其他设备照常同步，健康设备上的同步失败仍然阻止关机。若卡恰好在 `reboot` 自己的同步过程中下线，这次 `reboot` 报告 I/O error 并返回 shell，再执行一次 `reboot` 即可重启。

命令成功返回 0，操作失败返回 1，用法错误返回 2；诊断写入 stderr。`cat` 的控制台输入采用共用行策略：Enter 提交带换行的一行，Ctrl-D 交付当前未完成行或在空行结束；键盘回显写 stderr，重定向文件只收到数据。交互输入每行最多 4094 字节，超长行排空后报错，不写入截断行；普通文件 stdin 不受此行长度限制。`cp` 处理短读写，发生错误时报告失败，但可能留下部分目标文件；它不是原子复制，也不隐式同步落盘。使用 `sync` 或正常 `shutdown` 确认数据持久化。

`cp` 通过打开的 FD 获取设备/文件标识，确认目标不是源文件后才截断；因此大小写、相对路径等同文件别名不会误毁源文件。当前 FAT 后端提供这一标识，不能提供身份的后端会被 `cp` 拒绝。

当前为单用户权限模型：没有 UID/凭据，所有普通用户进程共享终止权限，可终止兄弟或已被 init 收养的用户进程；父子关系仅约束 wait/回收。init 与所有内核 MM 任务拒绝终止；PID 0、负 PID 和超出 int 范围的 PID 非法。shell 属于普通用户进程，主动终止后由 init 重新启动。Ctrl-C 针对当前前台命令或管道命令组。

`kill` 先验证全部 PID 的语法再执行，避免后面的非法操作数导致前面的进程已被终止。语法正确时逐项请求：不存在或已是 zombie 的目标报告 not found，受保护目标报告 permission denied；某项失败不跳过其他目标，最终返回 1。成功表示请求已受理，目标在安全边界完成清理，不保证调用返回时已经退出或被父进程回收。

`ps` 的 U/S/R/R+/Z 分别表示未初始化、睡眠、可运行、运行中和 zombie。每条元数据在关中断保护下复制，不暴露地址空间、内核栈或对象指针；多次读取不构成整张进程表的原子快照。

P1-2 的基础 CLI 功能已实现；本地验收与当前提交状态见 [TODO.md](TODO.md#p1-2完整-cli-基础系统)。

## 日志文件

init 在启动 shell 和开机服务之前启动基础系统的 `/bin/logd`。logd 从 `/dev/kmsg` 读取内核日志（含程序写入的记录），追加到 `/var/log/kernel.log`，每次开机先写一行 `---- boot, UTC time 秒数 ----`。新记录最迟 1 秒内同步到磁盘，warn 和 error 立即同步，所以即使系统卡死或断电，文件里也保留着出事前约 1 秒以内的记录。文件超过 1 MiB 时改名为 `kernel.old`（覆盖旧的）并重新开始。用 `cat /var/log/kernel.log` 查看；`/dev/kmsg` 的格式和级别见 [USER_ABI.md](USER_ABI.md#内核日志)。

程序可以用 `user/command.h` 的 `command_log(priority, text)` 写一条记录（3 error、4 warn、6 info、7 debug），shell 里也可以 `echo 文字 > /dev/kmsg`。init 记录开机服务的启动和退出，netd 记录取得的地址。程序崩溃时内核记录程序名、PID 和出错地址；内核 panic 时记录原因和调用栈，把日志中的地址交给宿主机的 `scripts/symbolize.py bin/<arch>/kernel 日志文件` 即可看到函数名和源码行。panic 那一刻的信息只在屏幕上，来不及写盘，可以拍照后用同一个脚本解析。

`scripts/test_log_qemu.py <arch>` 在 rootfs 的副本上启动系统，检查日志文件里有开机标记、带时间戳的内核记录和从 shell 写入的记录，然后不关机直接结束 QEMU，再次启动后检查上一次开机的记录仍在。

## 全屏编辑器 vi

`vi [file]` 从基础 rootfs `/bin/vi` 启动，不依赖应用盘或 Desktop。作为 Clang C++ 基础组件构建，使用共同 C ABI、运行时与 `external/zstl`；编辑器状态由用户态类拥有，不在内核实现编辑命令。

| 按键/命令 | 操作 |
| --- | --- |
| `i` / `a` | 在当前位置前 / 后进入插入模式 |
| `Esc` | 返回普通模式 |
| 方向键 / `hjkl` | 移动光标；方向键也可在插入模式使用 |
| Home、End / `0`、`$` | 移到行首/行尾；`0`、`$` 在普通模式使用 |
| Enter、退格、Delete | 插入模式换行、删除前一字节、删除当前字节；删除换行可合并行 |
| `x` / `dd` | 普通模式删除当前字符 / 当前行 |
| `:w [path]` | 保存，指定路径可另存；成功后清除未保存标记 |
| `:q` / `:wq [path]` | 退出 / 保存成功后退出；`:q` 拒绝丢弃未保存修改 |
| `:q!` | 丢弃修改并退出 |
| Ctrl-C | 取消插入/命令状态，保留编辑缓冲区 |

支持纵向及横向视口，Tab 按 8 列显示但保持原字节。底部显示模式、文件名、修改标记和消息；按受影响的行局部重绘。缓冲区固定 64 KiB，满时拒绝新增字符，不截断已有内容；仅接受 ASCII、Tab 和 LF，不接受 CRLF、Unicode 或二进制文件，不自动补末尾换行。加载失败时不进入全屏模式。

保存沿用当前 VFS 的原地写回语义：检查文件类型后清零并完整写入，关闭并同步成功才报告 Written。当前没有原子文件替换，写入/同步失败可能留下部分文件；编辑缓冲区和未保存标记仍保留，可重试或 `:w other.txt` 另存。保存失败不会让 `:wq` 退出。

全屏应用通过通用终端接口取得尺寸与前台原始输入租约。退出、故障或被 kill 时，内核重置输入模式/控制序列、清屏、显示光标，并丢弃应用遗留按键，使 shell 继续可用。其他进程和内核的控制台输出在租约期间暂存（1 KiB，超限会在释放时提示），退出后再显示，避免覆盖编辑画面；panic 会先强制恢复终端。

## 当前目录 ABI

每个进程持有独立的绝对当前目录（含结尾 NUL 最长 128 字节），spawn 继承目录值；子进程 `chdir` 不改变父进程。open、spawn、stat、readdir 和 chdir 共用路径解析，支持相对路径、重复斜杠、`.` 和 `..`，不能越过 `/`；斜杠前的分量必须是实际目录。

- 共享运行时 `sys_chdir(path)`：成功返回 0，失败返回负 Error，原目录保留。
- 共享运行时 `sys_getcwd(buffer, capacity)`：复制含 NUL 的路径，成功返回 0；缓冲区过小返回 `SYS_ERROR_FULL`，无效用户内存返回错误。

syscall 编号在 `include/abi/syscall.h` 定义并与 zcc 运行时同步，三架构入口只实现各自 ABI 的调用桩。

## 可选图形演示 gfxdemo

`gfxdemo` 是独立的 Clang C++ 用户应用，使用同一套 ABI、zstl 和运行时；不安装到基础 rootfs，不属于普通 `user` 目标，也不是启动镜像的依赖。显式构建并安装到应用盘：

```sh
# ARCH 同样支持 aarch64 / riscv64；先按前文准备引导盘和基础 rootfs
make ARCH=x86 TEST=0 USER_CC=clang bin/x86/user/GFXDEMO.ELF
bash scripts/install_userspace_image.sh apps bin/x86/apps.img bin/x86/user/GFXDEMO.ELF
make ARCH=x86 TEST=0 qemu USER_DISK=bin/x86/apps.img
# shell 内执行 gfxdemo，或 /opt/bin/gfxdemo
```

程序查询 `/dev/display` 并取得前台显示会话，绘制白边、RGB 色条和棋盘。移动鼠标显示方形指针，按住任一鼠标键变为紫色，按住 Shift 变为黄色。Space 重绘切换色条，q/Esc 正常退出，Ctrl-C 请求终止；退出、故障或 kill 后恢复文字控制台，可再次启动。不接应用盘仍正常进入 shell。

显示设备复用 UEFI 提供的 32 位 RGB/BGR 帧缓冲；用户态统一提交 `0x00RRGGBB` 像素，通过受控区域复制绘制，不映射物理内存。BIOS CGA 或没有可用帧缓冲时，设备返回 `NoDevice`，演示报告 device unavailable 后返回 shell。图形占用期间暂停文字绘制和光标，将控制台输出暂存 1 KiB，归还时显示并报告截断。

x86 QEMU 使用 PS/2 键盘与三键相对鼠标。aarch64 和 riscv64 的 QEMU 配置使用 VirtIO 键盘/鼠标，并支持垂直滚轮切换色条。演示使用前台键鼠事件，shell 和 `vi` 继续使用原有字符输入。接口和权限见 [USER_ABI.md](USER_ABI.md#显示控制-abi)。

### 控制台作业控制的回归检查

`scripts/test_console_qemu.py <架构>` 启动 `TEST=0` 的生产系统，用模拟键盘在真实的 init 和 shell 上操作，从串口读输出：Ctrl-C 必须结束前台命令和整条管道并回到提示符；`vi` 占用控制台的原始模式时，Ctrl-C 作为普通按键交给它处理。伪终端上的同类行为由内核测试中的用户态夹具 `user/p1life` 覆盖。

## 可选 Desktop

`desktop` 是从 shell 启动的可选用户态程序。三个架构的套件都包含 Desktop、图形终端、Files、文本编辑器、设置和界面字体，统一装到独立的 `desktop.img`。这些程序静态链接同一套 mini-cocos 与 Zonix GUI 平台。先准备下文指定的 mini-cocos checkout。aarch64 和 riscv64 把命令里的 `ARCH=x86` 换成相应架构。

```sh
make ARCH=x86 TEST=0 -j8
make ARCH=x86 TEST=0 desktop-image MINICOCOS_DIR=../mini-cocos
make ARCH=x86 TEST=0 qemu USER_DISK=bin/x86/desktop.img
# 在 shell 中执行 desktop
```

界面字体是仓库内的 Inter Regular（`fonts/Inter-Regular.otf`），连同许可证装到套件镜像的 `/fonts`；构建时完整更新套件镜像。用 `make qemu ARCH=x86` 启动时默认只接引导盘和基础 rootfs，无需 mini-cocos。Desktop 套件三个架构用同一份源码。架构 Makefile 给出 mini-cocos 的目标处理器 `GUI_PROCESSOR` 和 GUI 代码的架构选项 `GUI_ARCH_CFLAGS`；后者允许浮点指令，但保持共用运行时的调用约定：aarch64 去掉 `-mgeneral-regs-only`，riscv64 用 `rv64gc` 加整数寄存器传参的 `lp64`。运行时不碰浮点寄存器，所以可以和 GUI 代码链接在一起。x86 的数学函数用 x87 指令（`math_x86.cpp`）；其他架构共用 `math_series.cpp` 里的级数实现，只在 `math_<arch>.cpp` 提供取整和开方。GUI 程序链接时去掉调试信息和所有局部符号：riscv64 的目标文件为 PC 相对重定位留有大量无名局部符号，保留它们会让程序超过内核 1 MiB 的可执行文件上限。

屏幕顶部是菜单栏：左侧是系统名和最前面窗口所属的程序名，没有窗口时显示 Desktop；右侧是网络状态和日期时间（如 `10.0.2.15    Nov 20  14:05`），时间每分钟更新一次。网络状态来自 netd：有地址时显示地址，netd 在运行但还没取得地址时显示 No network，没有 netd 时不显示；Desktop 每 5 秒问一次 netd，所以之后取得的地址或之后启动的 netd 也会出现。时间来自 `sys_realtime()`，是 UTC；在设置里选了时区后按该偏移显示本地时间。平台没有时钟时改为显示开机以来的时长（`Up 时:分`）。窗口不能拖到菜单栏上面。屏幕底部居中是 Dock，依次是 Terminal、Files、文本编辑器、设置和退出。单击程序图标与按它的快捷键效果相同，有窗口的程序在图标下方有一个标记点，单击退出图标与 F12 一样离开 Desktop。Dock 在所有窗口之下，被窗口盖住时可以把窗口拖开；屏幕太小放不下时不显示。套件的桌面右上角有一列图标：Zonix（根目录）、Home（`/home`）和 Applications（`/opt`）。单击选中，图标有深色底、名称有蓝色底；单击桌面空白处或窗口取消选中；双击用 Files 打开该目录，Files 无法进入时在状态栏说明原因并停在原目录。图标在所有窗口之下，靠右打开的 Files 窗口排在图标列左边；屏幕放不下时不显示。按 F2 打开 Terminal，按 F4 打开 Files；程序已有窗口时按键聚焦它最前面的那个，按住 Shift 再按或再点则另开一个。Terminal、Files 和文本编辑器都可以开多个窗口，同一程序后开的窗口依次向右下错开。所有程序合计最多 8 个窗口，开满后再开会失败，Files 在状态栏提示先关闭一个窗口。单击窗口切换焦点，按住标题栏拖动，单击标题栏左侧的红色圆钮关闭，单击它右边的黄色圆钮或按 Alt-M 最小化；最小化的窗口不显示，在 Dock 上单击它的程序或按程序的快捷键会恢复它。Alt-Tab 把最前面的窗口放到最后，连续按可以轮流切换所有窗口，Alt-Shift-Tab 把最后面的窗口提到最前。Terminal、Files 和文本编辑器可以拖动窗口右下角调整大小：拖动时显示新大小的蓝色轮廓，松开后程序按新大小重新排版，各程序有最小尺寸，Terminal 按整字符取整，并把新的行列数告诉 shell（下一次提示符起生效）。F12 退出 Desktop。安装示例客户端后，Desktop 会在启动时打开两个示例窗口，按 1 或 2 可重开。示例窗口的内容区支持鼠标和 Space，聚焦时 q/Esc 退出、Ctrl-C 终止前台 Desktop。另装 mini-cocos 渲染示例后按 F3 打开它。无可用帧缓冲时，Desktop 报告设备不可用并返回 shell。

每个客户端有两条专用匿名管道。像素客户端先发送一次 Size 请求声明表面尺寸；用户调整窗口大小后，Desktop 通过输入管道发送 `INPUT_RESIZE` 事件，x、y 是建议的表面尺寸，支持缩放的客户端按自己的最小尺寸取舍后再发一次 Size 并重画整个表面，Desktop 收到后换成新尺寸的表面，不支持的客户端忽略它。两个示例客户端通过 stdout 发送 24×24 XRGB8888 像素帧，Desktop 按窗口尺寸放大并合成，通过 stdin 发送聚焦窗口的键盘事件、内容区鼠标按下/释放和画布坐标。桌面处理指针移动和标题栏操作。双方每次只交换一条输入及其对应帧，桌面可等待输入和任一客户端的帧或 EOF。

客户端异常退出只关闭它自己的窗口，剩余窗口仍可交互。Desktop 正常关闭时关闭管道并回收客户端，异常退出时内核恢复前台控制台并关闭父进程的管道端，客户端由此结束并交给 init 回收。

### 图形终端

在 Desktop 按 F2 打开或聚焦 Terminal 窗口。它在一个独立伪终端上启动基础 rootfs 的 `/bin/sh`。64×20 字符网格沿用现有 ASCII 字体、ANSI 光标和清屏语义，支持键入、光标移动、历史、滚屏、重定向和管道。Terminal 在自己的进程内把原有 PSF 字形转换为纹理。mini-cocos 批量绘制变化区域内的字形和光标，再通过与 Files 相同的管道发送局部像素矩形，Desktop 合成这些像素。通过 `desktop-image` 一起更新 Desktop 和 Terminal。

Terminal 聚焦时，键盘 Ctrl-C 交给这个伪终端，取消输入行或中断该终端的前台命令或管道，Desktop 与原来的物理 shell 保持运行。关闭窗口会话挂断，回收本会话中的 shell 和命令。F12 可以随时退出并回到物理 shell。串口 Ctrl-C 仍由物理控制台处理。

当前布局使用 8×16 字形，需要至少 516×352 的显示区域。每个 Terminal 窗口有自己的伪终端和 shell，使用原有 ANSI 子集。

### 文件浏览器

Files 随 Desktop 套件安装，按 F4 打开。文件夹和普通文件用不同图标显示。单击选中，状态栏显示名称和类型或大小。双击或按 Enter 打开所选项，也可以通过右键菜单的 Open 打开。右键内容区可刷新，子目录中还可以返回上一级。单击菜单外或按 Esc 收起菜单。

打开普通文件时，Desktop 在单独的 Text Editor 窗口中启动文本编辑器，把绝对路径作为一个参数传入，可以打开带空格的 ASCII 文件名。每个文件开一个编辑窗；再次打开已在编辑的文件会聚焦原来的窗口。

右键菜单可新建空文件和文件夹，选中文件或文件夹时还可在当前目录内重命名和删除。FAT 名称支持最多 31 个可打印 ASCII 字符和内部空格，名称末尾不能有空格或点。重名会报错。删除前会显示目标名称并要求确认，文件夹必须为空。SSH 和本地 shell 也使用相同的命名规则，命令中的带空格路径需要加引号。

左侧可直达根目录、Home 和应用目录。顶部按钮用于返回、前进和上一级，底部按钮用于翻页。方向键可移动选区，Backspace 返回上一级。q/Esc 或标题栏的关闭钮关闭窗口。Files 使用 mini-cocos 的场景、标签和控件分发键鼠事件，VFS 操作使用该进程自己的当前目录。

### 设置

设置随 Desktop 套件安装，从 Dock 打开，同时只开一个窗口。目前有两项：时区，用两侧的箭头按 30 分钟一档在 UTC−12:00 到 UTC+14:00 之间调整；界面文字大小，Small、Medium、Large 对应 14、16、18 像素。每次改动立即写入并同步到磁盘，状态栏显示 Saved 或失败原因。q/Esc 或标题栏的关闭钮关闭窗口。

时区在设置窗口关闭时对菜单栏的时钟生效。菜单栏、窗口标题和桌面图标的文字也在设置窗口关闭时换成新的大小；其他程序的文字对之后打开的窗口生效。

设置保存在 `/etc/settings`，每行一个 `键=值`，现有 `timezone=+08:00` 和 `text_size=16` 两个键；文件、键缺失或值不合法时用默认值（UTC、16 像素）。读写代码在 `user/gui/settings.h`，不依赖图形引擎，Desktop 和各程序启动时各读一次。

### 文本编辑器

文本编辑器随 Desktop 套件安装，从 Files 打开文件时启动，也可以从 Dock 打开一个未命名的新文档，窗口与 Terminal 同大。它直接输入，没有模式：可打印字符、Enter 和 Tab 在光标处插入，Backspace 和 Delete 删除，方向键、Home、End、PageUp、PageDown 移动，Ctrl-Home 和 Ctrl-End 到文首和文末。单击文字区把光标放到最近的字符边界。按住 Shift 再移动光标或单击，或者按住左键拖动，可以选中文字，Ctrl-A 全选；选中后输入、Backspace 或 Delete 会替换或删除选中部分。行不折行，光标移出可见区域时内容横向或纵向滚动，行数超过一屏时右侧显示位置指示条。

工具栏的 Save 按钮或 Ctrl-S 保存，写回后同步到磁盘；没有改动时按钮不可用。未命名文档保存时，以及 Ctrl-Shift-S 另存为时，会询问完整路径（如 `/home/notes.txt`）；已存在的文件和文件夹会被拒绝，避免误覆盖。状态栏左侧显示 Modified、Saved 或失败原因，右侧显示光标的行和列，有选区时显示选中的字节数。

Ctrl-C 复制、Ctrl-X 剪切、Ctrl-V 粘贴。剪贴板是文件 `/tmp/clipboard`，所有编辑器窗口共用，也可以在终端里查看或写入。Ctrl-Z 撤销，Ctrl-Y 或 Ctrl-Shift-Z 重做；连续输入的字符作为一次撤销，换行、删除、粘贴各算一次，最多保留 1000 次。撤销回到保存时的内容会去掉 Modified 标记。Ctrl-F 打开查找框，回车从光标往后找并选中，到末尾后从头接着找；Ctrl-G 查找下一个。Ctrl-Q 或标题栏的关闭钮关闭窗口；有未保存的改动时先询问是否丢弃，取消后继续编辑。询问出现后再点一次关闭钮会直接关闭，编辑器无响应时也能这样关掉。

支持的文件与命令行 `vi` 相同：ASCII、LF 换行和 Tab，最大 64 KiB，Tab 按 8 列对齐显示。超过大小或含其他字节的文件仍会打开窗口，状态栏说明原因，内容不显示也不能修改。没有替换、自动换行和多级缩进。另存为后 Files 再打开新路径时会另开一个窗口。

文本、光标、选区、撤销历史和查找在 `user/editor/document.h`，与界面代码分开；所有修改都经过一个替换操作，它同时记录撤销信息。`editor.cpp` 负责绘制、键鼠、剪贴板、对话框和保存提示。

### Zonix 用户态 GUI 平台

`user/gui` 把 mini-cocos 的软件绘制、字体 VFS 和进程内分配接到现有用户态 ABI。Desktop、Files 和 Terminal 在各自进程内使用引擎。Desktop 用离屏后端把各窗口的标题栏和关闭按钮绘制到内存。合成器缓存这块画布，在重绘、遮挡和拖动时把它与客户端内容拼到显示缓冲，并自行绘制渐变背景、窗口边框、圆角、阴影和箭头指针，未聚焦窗口的标题栏会变暗。关闭按钮的命中使用与绘制相同的尺寸和位置，窗口的焦点、拖动、关闭和回收继续由 Desktop 处理。

`user/gui/controls` 在 mini-cocos 上提供 Zonix 标准控件。应用通过 `ContextMenu::attach` 给场景提供按右键位置生成菜单项的回调，每项包含标签和执行回调。控件负责窗口内的布局、绘制和事件处理，包括按 Esc、点击菜单外和切换焦点时收起。Files 用这套接口保留自己的目录选择和导航逻辑。

Desktop 内部分成两部分：`desktop.cpp` 管理窗口、指针和输入分发，`compositor.cpp` 只负责合成画面。合成器把窗口后面不常变的内容（渐变背景、菜单栏、桌面图标和 Dock）保存为一整屏的桌面层，只在这些内容变化时重画相应区域；合成一块区域时先逐行复制桌面层，再从后往前叠上各窗口的阴影、边框、标题栏和客户区，客户区整行复制，最后画尺寸轮廓和指针。显示有光标层时（`DISPLAY_SET_CURSOR` 成功），指针交给光标层：合成时不画指针，移动指针只调用 `DISPLAY_MOVE_CURSOR`，即使合成落后指针也立即跟手；显示返回 NotSupported 时照旧自己画。Desktop 不立即重画，而是先记下变化的区域：相近的区域合并，暂时没有输入和客户端更新时一起合成，每次提交最多 65536 个像素；客户端一直在发更新时，约每 16 ms 也会提交一次。一阵指针移动因此只重画一次。`desktop --bench` 不读输入，按固定步骤打开 Terminal 和 Files、拖动窗口、单独移动指针各 120 步，每一步都提交画面，把各项的次数、总耗时和最慢一次写进内核日志后退出，用于在没有人操作的机器上（例如经串口）测量合成速度。Dock 的位置、项目和尺寸在 `dock.h`，三方共用：Desktop 处理点击，合成器画底板和运行标记，装饰层画图标。桌面图标的项目和位置在 `desktop_icons.h`，同样三方共用；装饰层把图标和名称画在画布底部清成透明的几行里，得到预乘透明度的像素，合成器按透明度把它们叠到渐变背景上。选中状态由装饰层保存，变化时重画画布。Files 启动时可以多带一个目录参数（`files --window /opt`），先进入该目录。菜单栏的文字由装饰层绘制，Desktop 在每轮事件循环前把最前面的程序、网络状态和时钟文字交给它，有变化才重绘；等待输入时带超时，到下一分钟或下一次查询网络状态时自动醒来。网络状态用 socket 库的 `net_info` 查询，它不等待 netd 启动，最多等 1 秒回答，Desktop 因此不会因网络卡住。每种程序的标题、可执行文件、启动参数、初始位置、快捷键和通信方式集中在 `client.h` 的 `APPS` 表里，新增一种程序只需加一行。窗口是与程序无关的槽位，槽位记录正在其中运行的程序。Files 的目录状态（路径、分页、访问历史）在 `user/files/directory.h`，与界面代码分开。

Desktop、Terminal 和控件共用 `user/gui/theme.h` 中的颜色：中性深灰底色配蓝色强调色，标题栏与工具栏同色，连成一条。窗口为 10 像素圆角加一像素细边，阴影向下偏移并在 28 像素内淡出，聚焦窗口的阴影更深。关闭钮是标题栏左侧的红色圆钮，标题居中。`controls/style` 提供抗锯齿的圆角矩形、边框和图标符号，同一进程内所有控件共用一套纹理；圆角的覆盖率算法在 `user/gui/shape.h`，合成器和控件共用；每个进程的标签用 Inter 按设置里的字号（默认 16 像素）原始大小绘制，并用 `place_label` 对齐到整数像素。

应用可以用 `Toolbar` 排列标准 `Button` 和单行文字。Files 为工具栏提供返回、前进、上一级的回调和启用状态，并更新当前路径。左侧快捷目录和底部翻页也使用标准 `Button`。按钮和菜单共用字体、边框和按下态，工具栏统一处理间距和对齐。

`MonoFont` 把控制台的 8×16 PSF 字体（Terminus Font，`fonts/console.psf`）做成一张纹理，Terminal 和文本编辑器用它绘制等宽文字。`Dialog` 提供输入框和确认框，确认按钮可以是 Delete 或 Discard。

标题栏的关闭钮默认由 Desktop 直接结束客户端。`APPS` 表中标记为先询问的程序改为收到关闭请求事件，由客户端自己退出；客户端通过 `make_window_view` 的第二个回调接收它。请求之后客户端没有再收到键鼠输入时，下一次点击直接结束它。

`IconGrid` 根据内容区大小排列文件和文件夹图标，并处理选中、双击和右键命中。Files 按控件容量读取一页目录数据，负责状态栏、目录导航和右键菜单项。

Files 用管道后端接收窗口坐标中的键鼠事件，mini-cocos 将事件分发给内容区控件。它的软件绘制结果按变化矩形经管道交给 Desktop。套件的 Terminal 用同一软件渲染器绘制固定网格，PTY 负责终端输入输出和前台命令。Desktop 持有显示会话，通过 `/dev/display` 读取带入队时间的键鼠事件来识别双击，并提交最终画面。使用这版 Desktop 时需同时更新内核。当前两个后端均按事件重绘，暂无动画时钟。

构建在对象目录展开固定版本源码，对副本应用 Zonix 的无 RTTI、字体、离屏视图和按钮绘制顺序兼容补丁。引擎和平台库使用项目的 `external/zstl` 静态链接到 GUI ELF。可选 GUI 软件及字体从应用盘安装，启动镜像和基础 rootfs 独立构建。套件的程序共用一份运行时分配器。堆从程序映像末尾开始，用 `sys_brk` 按需增长，每次至少 256 KiB；末尾空闲超过 1 MiB 时把多余部分还给内核。Desktop 的窗口画布在客户端报告尺寸时分配，窗口关闭时释放。

## 联网

联网由可选的网络服务 netd 提供。netd 独占内核的 `/dev/net0`，在用户态运行固定版本的 lwIP（IPv4、TCP、UDP、ICMP、DHCP 和 DNS），通过命名本地通道 `net` 向其他程序提供 socket 接口。内核里没有协议代码：三种架构的内核探测到现代 VirtIO PCI 网卡时都注册 `/dev/net0`，收到帧时由网卡中断唤醒 netd。下面以 x86 为例；aarch64 和 riscv64 把 `ARCH` 换掉，QEMU 参数相同。

```sh
git clone --branch STABLE-2_2_1_RELEASE https://github.com/lwip-tcpip/lwip.git ../lwip
make ARCH=x86 TEST=0
make ARCH=x86 TEST=0 USER_CC=clang network-image LWIP_DIR=../lwip
make ARCH=x86 TEST=0 qemu USER_DISK=bin/x86/network.img   QEMU_FLAGS='-netdev user,id=net0,ipv6=off,hostfwd=tcp:127.0.0.1:2323-:2323 -device virtio-net-pci,disable-legacy=on,netdev=net0'
```

`network.img` 带有 netd、`ping`、`nc`、`netinfo`、`tcpserve`、`tcpecho` 和 `/opt/etc/services`，init 开机时在后台启动 netd。netd 用 DHCP 取得地址、网关和 DNS 服务器，取得后把 `netd: address 10.0.2.15, DNS 10.0.2.3` 写进内核日志（`dmesg netd` 查看）和服务日志；`netd -a 192.168.1.20/24 -g 192.168.1.1 -d 192.168.1.1` 使用固定配置。netd 同时最多服务 16 个 socket，另外最多 16 条连接等待接受。

- `netinfo` 显示地址、掩码、网关和 DNS；`netinfo HOST` 解析一个主机名。
- `fetch [-o FILE|-] http://HOST[:PORT]/PATH` 用 HTTP/1.1 下载文件：默认存为路径的最后一段（没有时为 `index.html`），`-o -` 输出到终端。支持 Content-Length、chunked 编码和最多 5 次重定向，30 秒收不到数据时放弃；状态不是 200 时报告服务器的回答，不写文件。暂不支持 HTTPS。
- `ping [-c COUNT] HOST` 发送 ICMP Echo 请求，默认 4 次，显示往返时间。
- `nc HOST PORT` 建立 TCP 连接，把终端输入发给对方、把收到的内容显示出来，对方关闭后退出；终端按 Ctrl-D 结束发送方向，标准输入重定向自文件（`nc HOST PORT < FILE`）时先发完整个文件再结束发送方向。`nc -u HOST PORT` 把每行输入作为一个 UDP 数据报发送并显示回复。`nc -l PORT` 接受一条 TCP 连接，`nc -l -u PORT` 接收数据报并回复给最后的发送者。
- `tcpserve PORT /absolute/worker-path` 监听 TCP 端口，每接受一条连接启动一个子进程，把连接直接作为它的标准输入和输出，最多同时四个；第五条连接会被关闭。`tcpecho` 只用标准输入输出回显数据。在来宾运行 `tcpserve 2323 /opt/bin/tcpecho` 后，宿主机用 `nc 127.0.0.1 2323` 连接；按 Ctrl-C 停止 `tcpserve`。

QEMU 用户网络的 `restrict=on` 会挡住来宾主动发起的连接和 DNS，只需要从宿主机连进来时可以加上它。

`network-image` 默认生成独立的 `network.img`。只有一块应用盘挂在 `/opt`，要让 Desktop 和网络同时可用，先生成 `desktop.img`，再把网络程序装进去：`make ARCH=x86 TEST=0 USER_CC=clang network-image NETWORK_IMAGE=bin/x86/desktop.img LWIP_DIR=../lwip`。之后重新生成 `desktop.img` 会覆盖这些程序，需要再装一次。

### socket 接口

程序包含 `user/net/zonix_net.h` 并链接 `zonix_net.o`（`user/net/Makefile` 中的 `NET_LIBRARY`）。每个 socket 都是一个描述符：

- `net_tcp_connect(address, port)` 返回已连接的流；`net_tcp_listen(port)` 返回监听描述符，有连接等待时读就绪，`net_tcp_accept(listener, &address, &port)` 取出一条连接。
- TCP 流就是普通字节流，`read`、`write`、`sys_wait_io`、`fcntl(O_NONBLOCK)` 和 `close` 直接可用，`net_shutdown_write(fd)` 结束发送方向。
- `net_udp_open(port)`（0 表示任意端口）返回数据报描述符，`net_udp_send` / `net_udp_receive` 收发单个数据报，最大 1472 字节，收到的数据报装不下时截断。
- `net_resolve(name, &address)` 接受点分十进制或主机名，`net_ping`、`net_info` 对应上面的命令，`net_parse_ipv4` / `net_format_ipv4` 转换文本，`net_error_text` 给出错误说明。

地址是主机字节序的 32 位整数（10.0.2.15 为 0x0A00020F）。函数失败返回负的 `NET_ERROR_*`；netd 没有运行时，库最多等 2 秒（给开机时同时启动的服务留出时间）后返回 `NET_ERROR_NO_SERVICE`。库与 netd 之间的协议见 `user/net/netproto.h`：每个 socket 对应一条通道，建立 TCP 连接后通道里就是连接的字节流，netd 在两者之间转发。

`scripts/test_network_qemu.py <arch>` 启动带 `network.img` 的系统，从宿主机配合检查 `netinfo`、`ping`、DNS、TCP 客户端、UDP 双向收发、`fetch` 跟随重定向、解 chunked 编码和逐字节保存 256 KiB 文件（用 `nc` 送回宿主机比对）、`tcpserve` 同时处理两条连接和 netd 在客户端退出后继续服务，并打印回显速度。

## 系统随机源与用户态加密库

`sys_getrandom` 可供普通 Zonix 用户程序读取系统随机字节。在 x86 QEMU 中可用 `rng-builtin` 给来宾提供宿主随机源。

```sh
make ARCH=x86 TEST=0 qemu \
  QEMU_FLAGS='-object rng-builtin,id=entropy -device virtio-rng-pci,disable-legacy=on,rng=entropy'
```

可选的 `crypto` 目标构建 `bin/<arch>/user/libzonixcrypto.a`，供应用静态链接。它使用固定版本的 Mbed TLS 提供 SHA-256 和 ChaCha20-Poly1305，平台接口继续使用现有 Zonix 启动代码和 `libzccrt.a`。

```sh
git clone --branch mbedtls-3.6.8 https://github.com/Mbed-TLS/mbedtls.git ../mbedtls
make ARCH=x86 TEST=0 crypto MBEDTLS_DIR=../mbedtls
```

应用使用 `-Iuser/crypto` 引入 `zonix_crypto.h`。`zonix_crypto_random` 循环读取到指定长度，返回 0 表示成功。失败返回负错误并擦除已写入的随机字节。使用 Mbed TLS 原生密码接口时，再加入 `-Iobj/<arch>/user/crypto/source/include` 和 `-DMBEDTLS_CONFIG_FILE='"zonix_mbedtls_config.h"'`。链接时把 `libzonixcrypto.a` 放在 `libzccrt.a` 前。Mbed TLS 源码和许可证位于 `obj/<arch>/user/crypto/source`。

## 可选 SSH 登录

SSH 服务使用固定版本的 TinySSH。`tcpserve` 通过 netd 接受连接，把每条连接交给独立的 `sshd` 进程，认证通过后由它创建 PTY 并启动 `/bin/sh`，最多同时四个会话。提供单个 `zonix` 登录名、一个授权 Ed25519 公钥、交互式 shell 和窗口尺寸更新。Zonix 目前是单用户系统，授权公钥持有者获得同本地 shell 一样的系统权限。三种架构都可以使用；`sshd` 依赖系统随机源，QEMU 需要同时提供网卡和 `virtio-rng-pci`，VisionFive 2 用芯片自带的随机数发生器。

准备好 `../lwip`、`../mbedtls` 和下列 TinySSH checkout。使用已有的 Ed25519 登录公钥文件，并在仓库外生成 SSH 主机密钥。

```sh
git clone --branch 20261101 https://github.com/janmojzis/tinyssh.git ../tinyssh
make -C ../tinyssh tinysshd-makekey tinysshd-printkey
hostkeys="$HOME/.config/zonix/ssh-host"
mkdir -p "$(dirname "$hostkeys")"
../tinyssh/tinysshd-makekey "$hostkeys"
make ARCH=x86 TEST=0 ssh-image SSH_HOSTKEY_DIR="$hostkeys" \
  SSH_AUTHORIZED_KEY="$HOME/.ssh/id_ed25519.pub"
make ARCH=x86 TEST=0 qemu USER_DISK=bin/x86/ssh.img \
  QEMU_FLAGS='-object rng-builtin,id=entropy -device virtio-rng-pci,disable-legacy=on,rng=entropy -netdev user,id=net0,ipv6=off,restrict=on,hostfwd=tcp:127.0.0.1:2222-:22 -device virtio-net-pci,disable-legacy=on,netdev=net0'
```

`ssh.img` 包含 `network.img` 的全部程序和 `sshd`，它的 `/opt/etc/services` 让 init 开机时依次启动 netd 和 `/opt/bin/tcpserve 22 /opt/bin/sshd`，地址来自 DHCP。在宿主机预先登记主机公钥，再用 OpenSSH 登录。

```sh
known_hosts="$HOME/.config/zonix/known_hosts"
printf '[127.0.0.1]:2222 %s\n' "$(../tinyssh/tinysshd-printkey "$hostkeys")" > "$known_hosts"
ssh -t -o StrictHostKeyChecking=yes -o UserKnownHostsFile="$known_hosts" \
  -o IdentitiesOnly=yes -o ObscureKeystrokeTiming=no -i "$HOME/.ssh/id_ed25519" -p 2222 zonix@127.0.0.1
```

OpenSSH 9.5 起默认按 20 ms 的节拍发送按键来掩盖输入节奏，`ObscureKeystrokeTiming=no` 关掉它后按键回显约 5～10 ms。`ssh.img` 含来宾主机私钥，构建时将宿主文件权限设为 0600。退出远程 shell 或断开连接会回收该 PTY 和 `sshd`。要停止服务，用 `ps` 找到 `tcpserve` 的 PID 再 `kill`；init 不会自动重启它，需要时在 shell 中重新运行同一条命令。

### 开机服务

init 启动 shell 之前读取应用盘上的 `/opt/etc/services`（不存在时什么也不做）。每行是一条以绝对路径开头的命令，最多 8 个参数，以 `#` 开头的行是注释，最多启动 8 个服务。服务在后台运行，不占用前台：标准输入继承控制台，标准输出和标准错误追加到 `/var/log/services.log`（每次开机先写一行 `---- boot ----`，开机时超过 1 MiB 则改名为 `services.old`），所以服务的提示和 sshd 的登录记录不会插进 shell 的输入行；打不开这个文件时服务仍输出到控制台。退出时 init 在控制台打印 `init: service PID exited (STATUS), see /var/log/services.log`，不重新启动。基础 rootfs 不提供这个文件，没有应用盘时系统照常进入 shell。

`scripts/test_ssh_qemu.py <arch> --host-key <SSH_HOSTKEY_DIR>/ed25519.pk` 用 OpenSSH 从宿主机登录 `make ssh-image` 生成的系统，检查开机服务、DHCP 地址、两个并发会话、关闭其中一个后另一个继续工作和再次登录，并打印按键回显延迟和两个方向的传输速度。它需要 `TEST=0` 内核、`ssh-image` 和对应的登录私钥（默认 `~/.ssh/id_ed25519`）。

## mini-cocos 软件渲染示例

`minicoco` 是可选的 x86 用户态应用。它使用 mini-cocos 的 `zocos_software` 软件渲染库，绘制带旋转变换的棋盘贴图，并通过 `/dev/display` 提交画面。方向键或鼠标移动贴图，Space 切换配色，左键按下高亮、释放恢复，q/Esc 退出到 shell。

从 Zonix 仓库根目录运行以下命令。需要 CMake、Ninja 和现有 Clang/LLD 交叉工具链。示例已验证的 [mini-cocos 版本](https://github.com/leafvmaple/mini-cocos/tree/f58e97052de5a696ad7ed55b99700f265a5b331f) 如下：

```sh
git clone https://github.com/leafvmaple/mini-cocos.git ../mini-cocos
git -C ../mini-cocos checkout --detach f58e97052de5a696ad7ed55b99700f265a5b331f
make ARCH=x86 TEST=0 minicocos-demo MINICOCOS_DIR=../mini-cocos
bash scripts/install_userspace_image.sh apps bin/x86/apps.img bin/x86/user/MINICOCO.ELF
make ARCH=x86 TEST=0 rootfs
make ARCH=x86 TEST=0 qemu USER_DISK=bin/x86/apps.img
# 在 shell 中执行 minicoco
```

可通过 `MINICOCOS_DIR` 指向包含该版本的引擎 checkout。构建直接使用 Zonix 的 `external/zstl`，设置 `ZOCOS_FREESTANDING=ON`、`ZOCOS_USE_CRT=OFF`，关闭宿主、Lua 和引擎测试。示例调用软件渲染库并走现有用户态启动和 syscall 封装。320×240 的渲染缓冲由示例提供，贴图与绘制命令在初始化时使用有界内存，后续帧复用。

应用只安装到可选应用盘。x86 UEFI 使用 32 位帧缓冲运行示例，BIOS 文本模式报告显示设备不可用后返回 shell。

### 在 Desktop 窗口运行

将 `MINICOCO.ELF` 另行安装到已构建的 `desktop.img` 应用盘。在 shell 运行 `desktop`，用 F2 打开 Terminal，用 F3 打开 mini-cocos。mini-cocos 声明 320×240 内容尺寸，Desktop 以原像素呈现。单击内容区切换焦点，移动鼠标或按方向键移动贴图。拖标题栏移动窗口，单击 × 或在窗口聚焦时按 q/Esc 关闭 mini-cocos。Terminal 仍可输入命令，F12 退出 Desktop。

窗口客户端沿用两条匿名管道和已有 `Input` 记录。图像管道先发送一条尺寸记录，随后发送带坐标和尺寸的 XRGB8888 矩形。同一批更新可拆成多个像素包。Desktop 逐包写入窗口缓存，收到尾包后合并变化范围，记为待重画区域。独立更新可以直接发送尾包。拖动时复用窗口缓存。更新应用盘时请一起安装新版 Desktop 和像素客户端。尺寸须适配当前屏幕并在 512×384 上限内，当前会话使用启动时声明的尺寸。使用说明中的裸 `minicoco` 命令仍可独立运行。
