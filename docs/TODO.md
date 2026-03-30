# Zonix 操作系统开发 TODO List

> 本文档面向以学习为目的的操作系统开发者，提供循序渐进的开发路线图。

## 📋 项目概览

### 最近更新 (2026-03-24) — 文件系统分层收敛 + FAT 模块化

**近期主线完成了 fd 管理从调度层解耦、FAT 代码拆分与目录遍历去重，并补齐了 VFS 参数健壮性。**

#### 自 v0.8.0 以来的主要成就
- ✨ **Spinlock**：中断安全的原子自旋锁，架构抽象 `arch_spin_hint()`
- ✨ **WaitQueue**：结构化 sleep/wakeup 机制（类 Linux `wait_queue_head_t`）
- ✨ **Semaphore**：基于 Spinlock + WaitQueue 的计数信号量
- ✨ **Mutex**：带 ownership 追踪和断言的互斥锁
- ✨ **LockGuard\<T\>**：泛型 RAII 锁守卫模板
- ✨ **抢占式调度**：优先级感知 Round-Robin + 每 tick 时间片递减
- ✨ **架构抽象层**：`arch_*()` 包装中断控制、自旋提示等平台原语
- ✨ **驱动适配**：kbd、ide 驱动改用 WaitQueue 替代 ad-hoc 等待模式
- ✨ **Timer ISR 修正**：EOI 后再调度，避免 PIC 死锁

Zonix 是一个教学型操作系统，当前主线已实现：
- ✅ 双引导加载（BIOS + UEFI）
- ✅ x86_64 长模式（64 位内核）
- ✅ 物理内存管理器（First-Fit 页分配 + 引用计数）
- ✅ 虚拟内存管理（页表、MMIO 映射、缺页处理）
- ✅ 页面置换/交换系统（FIFO 算法 + 磁盘后端）
- ✅ 中断和异常处理框架（IDT 256 项 + 完整 TrapFrame）
- ✅ 进程调度（Round-Robin + fork/exit/wait + 上下文切换）
- ✅ 多输出控制台（CGA + 帧缓冲 + 串口）
- ✅ 交互式 Shell（20+ 命令 + 参数解析）
- ✅ PS/2 键盘驱动（中断驱动 + 扫描码映射）
- ✅ 完整的多磁盘 IDE/ATA 驱动（4 设备 + 中断 I/O）
- ✅ AHCI/SATA 驱动（MMIO + PCI 枚举）
- ✅ 块设备抽象层（多设备注册与管理）
- ✅ FAT32 只读文件系统（MBR 分区 + 双挂载点）
- ✅ kmalloc/kfree + C++ new/delete 运算符重载
- ✅ 同步原语（Spinlock、WaitQueue、Semaphore、Mutex、LockGuard\<T\>）
- ✅ 抢占式优先级调度（timeslice + priority-aware Round-Robin）
- ✅ CONFIG_* 模块化内核配置

---

## 🎯 阶段一：核心基础设施（优先级：高）

### 1.1 内存管理增强

#### 物理内存管理
- [ ] **实现 Buddy System 分配器**
  - 文件：创建 `kernel/mm/pmm_buddy.cpp`
  - 参考：当前的 `kernel/mm/pmm_firstfit.cpp` (First-Fit 算法)
  - 目标：减少内存碎片，提高分配效率
  - 学习点：二叉树、位运算、内存对齐

- [ ] **添加 Slab 分配器**
  - 文件：创建 `kernel/mm/slab.cpp`
  - 目标：优化小对象分配
  - 学习点：对象缓存、预分配策略

- [ ] **完善内存统计**
  - 修改：`kernel/mm/pmm.cpp`
  - 添加：总内存、已用内存、空闲内存统计
  - 添加：内存使用历史追踪

#### 虚拟内存管理
- [x] **实现完整的页表管理** ✅ (v0.8.0)
  - 完成：`kernel/mm/pmm.cpp` — 4 级页表（PML4→PDPT→PD→PT）、2MB 大页拆分
  - 完成：`kernel/mm/vmm.cpp` — 映射/解映射、MMIO 映射、权限管理

- [x] **实现内存区域（VMA）管理** ✅ (v0.8.0)
  - 完成：`kernel/mm/vmm.h` — `MemoryDesc`（类 Linux `mm_struct`）
  - 完成：mmap 链表、页目录绑定、swap 链表

- [x] **实现页面换入换出机制** ✅ (v0.8.0)
  - 完成：`kernel/mm/swap.cpp` — FIFO 算法 + 磁盘后端
  - 完成：`kernel/mm/swap_fifo.cpp`、`kernel/mm/swap_test.cpp`
  - 待扩展：Clock → LRU 算法

- [ ] **实现 Copy-on-Write**
  - 目标：优化 `fork()` 性能
  - 学习点：页面保护、缺页处理

- [ ] **添加内存映射文件支持**
  - 实现：`mmap()` 系统调用
  - 学习点：文件与内存的统一视图

### 1.2 进程管理

#### 进程基础
- [x] **设计进程控制块（PCB）** ✅ (v0.8.0)
  - 完成：`kernel/sched/sched.h` — `TaskStruct`（PID、状态、内存空间、调度信息）

- [x] **实现进程创建** ✅ (v0.8.0)
  - 完成：`kernel/sched/sched.cpp` — `TaskManager::fork()`
  - 完成：内存空间复制、内核栈设置

- [ ] **实现进程执行**
  - 实现：`exec()` 系统调用族
  - 学习点：ELF 加载、用户栈初始化
  - 参考：`arch/x86/boot/bios/bootload.c` 中的 ELF 加载

- [x] **实现进程等待与退出** ✅ (v0.8.0)
  - 完成：`TaskManager::exit()` — 状态设为 Zombie、唤醒父进程
  - 完成：`TaskManager::wait()` — 僵尸回收、孤儿重定向到 idle

- [x] **实现进程间关系管理** ✅ (v0.8.0)
  - 完成：父子进程链表（`parent`、`child_list`）
  - 完成：进程链表（`set_links()`、`remove_links()`）

#### 进程调度
- [x] **实现 Round-Robin 调度器** ✅ (v0.8.0)
  - 完成：`kernel/sched/sched.cpp` — `TaskManager::schedule()`
  - 完成：PIT 时间片驱动、就绪队列遍历

- [x] **实现优先级调度** ✅ (v0.9.3)
  - 完成：`sched_prio` 命名空间（MAX/DEFAULT/MIN/IDLE 优先级）
  - 完成：priority-aware Round-Robin + cursor 扫描保证公平
  - 完成：`calc_timeslice()` 按优先级计算时间片
  - 完成：fork 继承父进程优先级

- [x] **实现抢占式调度** ✅ (v0.9.3)
  - 完成：`TaskManager::tick()` 每 tick 递减 time_slice
  - 完成：time_slice 耗尽设 `need_resched`，EOI 后检查并调度
  - 修复：Timer ISR 内 context switch 导致 EOI 丢失的 bug

- [ ] **实现 CFS 调度器（进阶）**
  - 学习点：红黑树、虚拟运行时间
  - 目标：实现完全公平调度

- [ ] **添加调度统计信息**
  - 统计：CPU 使用率、上下文切换次数
  - 统计：进程等待时间、周转时间

### 1.3 中断与异常

- [x] **实现中断EOI信号处理** ✅ (2025-10-14)
  - 完成：`kernel/trap/trap.cpp` - 统一EOI发送
  - 完成：`arch/x86/kernel/drivers/pic.cpp` - 实现 pic_send_eoi()
  - 效果：修复键盘响应慢的问题
  - 学习点：8259A PIC工作原理、中断确认机制

- [x] **IDT 完整初始化** ✅ (v0.8.0)
  - 完成：256 项 IDT 初始化
  - 完成：x86_64 TrapFrame（所有 GPR + 硬件压栈上下文）
  - 完成：20 个 CPU 异常命名处理器
  - 完成：缺页异常处理（输出 CR2，委托 `vmm_pg_fault()`）

- [ ] **完善异常处理**
  - 添加：异常堆栈回溯（backtrace / stack unwinding）

- [ ] **实现中断嵌套**
  - 支持：可重入中断处理
  - 添加：中断优先级管理（当前使用 RAII `intr::Guard` 管理中断状态）

- [ ] **添加中断统计**
  - 统计：各类中断的触发次数
  - 提供：/proc/interrupts 接口

---

## 🎯 阶段二：设备与文件系统（优先级：中高）

### 2.1 设备驱动框架

#### 字符设备
- [x] **完善键盘驱动** ✅ (v0.8.0)
  - 完成：`kernel/drivers/kbd.cpp` — 中断驱动 + 扫描码映射
  - 完成：键盘缓冲区（128 字节环形缓冲）
  - 完成：阻塞读取 `getc_blocking()` + sleep/wakeup
  - 待完成：多种键盘布局（US、UK 等）
  - 待完成：组合键（Ctrl、Alt、Shift）

- [x] **实现串口驱动** ✅ (v0.8.0)
  - 完成：`kernel/drivers/serial.cpp` — COM1 115200 baud
  - 完成：调试输出、双输出（串口 + 控制台）

- [ ] **实现鼠标驱动**
  - 文件：`kernel/drivers/mouse.cpp`
  - 支持：PS/2 鼠标
  - 学习点：中断驱动的输入处理

#### 块设备
- [x] **多磁盘IDE/ATA驱动** ✅ (2025-10-14)
  - 完成：`kernel/drivers/ide.cpp` - 支持4个IDE设备，中断驱动 sleep/wakeup I/O
  - 完成：Primary/Secondary双通道支持
  - 完成：设备自动检测（hd0/hd1/hd2/hd3）
  - 完成：`kernel/block/blk.cpp` - 抽象块设备层多设备注册
  - 完成：带小数的磁盘容量显示（lsblk命令）

- [x] **AHCI/SATA 驱动** ✅ (v0.8.0)
  - 完成：`kernel/drivers/ahci.cpp` - MMIO SATA 控制器
  - 完成：PCI BAR 发现 + FIS 命令提交
  - 完成：中断处理，支持最多 4 端口

- [x] **PCI 总线枚举** ✅ (v0.8.0)
  - 完成：`kernel/drivers/pci.cpp` - 配置空间读写
  - 完成：BAR 读取、Bus Master 使能、按 class/subclass 设备枚举

- [ ] **完善硬盘驱动**
  - 当前：`kernel/drivers/ide.cpp`、`kernel/drivers/ahci.cpp`
  - 待添加：DMA 支持
  - 待添加：异步 I/O
  - 待添加：更完善的错误处理

- [ ] **实现磁盘缓存**
  - 文件：`kernel/fs/buffer.cpp`
  - 算法：LRU 缓存替换
  - 优化：预读、延迟写

#### 显示驱动
- [x] **完善控制台** ✅ (v0.8.0)
  - 完成：`kernel/drivers/cga.cpp` — CGA 文本模式
  - 完成：`kernel/drivers/fbcons.cpp` — 帧缓冲 PSF 字体渲染
  - 完成：`kernel/cons/cons.cpp` — 多输出后端（CGA + FB + 串口）
  - 待完成：ANSI 转义序列
  - 待完成：颜色支持
  - 待完成：滚动、清屏优化

- [ ] **实现 VGA 图形模式**
  - 文件：`kernel/drivers/vga.cpp`
  - 支持：基本图形绘制（点、线、矩形）
  - 支持：模式切换（文本 ↔ 图形）

- [ ] **实现简单的图形界面**
  - 实现：窗口管理器原型
  - 实现：简单的 GUI 控件

### 2.2 文件系统

#### VFS 层
- [x] **设计 VFS 接口** ✅ (v0.9.3+)
  - 文件：`kernel/fs/vfs.h`
  - 完成：统一挂载点管理、路径解析、`open/stat/readdir` 分发接口

- [x] **实现文件描述符管理** ✅ (v0.9.3+)
  - 完成：`kernel/fs/fd.{h,cpp}` 中的 `fd::Table`（分配、查询、关闭、全关闭）
  - 完成：`TaskStruct::files()` 文件上下文访问器与 syscall 对接
  - 待完成：共享 open-file description（`ForkPolicy::Share`）与标准 0/1/2 初始化

#### 具体文件系统
- [x] **FAT32 只读文件系统** ✅ (v0.8.0, 持续增强)
  - 完成：`kernel/fs/fat/fat_core.cpp` — MBR/GPT 探测 + 挂载状态初始化
  - 完成：`kernel/fs/fat/fat_dir.cpp` — 多级路径查找、根目录/子目录读取、目录遍历去重
  - 完成：`kernel/fs/fat/fat_vfs_adapter.cpp` — VFS 适配层

- [ ] **实现 MinixFS/简单 FS**
  - 文件：`kernel/fs/minixfs/`
  - 实现：超级块、inode、数据块管理
  - 实现：目录项管理

- [ ] **实现基本文件操作**
  - 实现：`open()`, `read()`, `write()`, `close()`
  - 实现：`lseek()`, `stat()`
  - 实现：`ioctl()`

- [ ] **实现目录操作**
  - 实现：`mkdir()`, `rmdir()`
  - 实现：`opendir()`, `readdir()`, `closedir()`
  - 实现：路径解析

- [ ] **实现文件权限**
  - 实现：chmod、chown
  - 实现：权限检查

#### 高级特性
- [ ] **实现 ramfs**
  - 目标：内存文件系统
  - 用途：/tmp、/proc

- [ ] **实现 procfs**
  - 提供：/proc/cpuinfo
  - 提供：/proc/meminfo
  - 提供：/proc/[pid]/ 目录

---

## 🎯 阶段三：系统调用与用户态（优先级：中）

### 3.1 系统调用接口

- [ ] **设计系统调用表**
  - 文件：`kernel/trap/syscall.cpp`
  - 定义：系统调用号
  - 实现：系统调用分发

- [ ] **实现进程相关系统调用**
  ```c
  fork(), exec(), exit(), wait()
  getpid(), getppid()
  sleep(), alarm()
  ```

- [ ] **实现文件相关系统调用**
  ```c
  open(), read(), write(), close()
  creat(), unlink(), link()
  mkdir(), rmdir(), chdir()
  stat(), fstat(), chmod()
  dup(), dup2(), pipe()
  ```

- [ ] **实现内存相关系统调用**
  ```c
  brk(), sbrk()
  mmap(), munmap()
  mprotect()
  ```

- [ ] **实现信号处理系统调用**
  ```c
  signal(), kill()
  sigaction(), sigprocmask()
  ```

- [ ] **添加系统调用追踪**
  - 实现：strace 功能
  - 记录：系统调用参数和返回值

### 3.2 用户态支持

- [ ] **实现用户态切换**
  - 实现：ring3 → ring0 切换
  - 实现：用户栈、内核栈切换

- [ ] **实现用户程序加载器**
  - 解析：ELF 文件格式
  - 参考：`arch/x86/boot/bios/bootload.c`
  - 初始化：用户栈、参数传递

- [ ] **创建简单用户程序**
  - 目录：`user/`
  - 示例：hello、ls、cat、sh

- [ ] **实现动态链接**
  - 实现：动态链接器
  - 支持：共享库加载

---

## 🎯 阶段四：Shell 与工具（优先级：中）

### 4.1 Shell 增强

- [x] **实现交互式 Shell** ✅ (v0.8.0)
  - 完成：`kernel/cons/shell.cpp` — 20+ 内置命令 + 参数解析

- [ ] **添加更多内置命令**
  - 完善：`kernel/cons/shell.cpp`
  - 添加：`ls`, `cat`, `echo`, `cd`, `pwd`
  - 添加：`ps`, `kill`, `top`
  - 添加：`mount`, `umount`, `df`

- [ ] **实现命令行编辑**
  - 支持：左右移动光标
  - 支持：删除、插入字符
  - 支持：Home、End 键

- [ ] **实现命令历史**
  - 支持：上下箭头浏览历史
  - 支持：Ctrl-R 搜索历史
  - 持久化：历史记录保存

- [ ] **实现自动补全**
  - 支持：命令补全
  - 支持：文件名补全
  - 支持：Tab 键触发

- [ ] **实现管道和重定向**
  - 实现：`|` 管道
  - 实现：`>`, `>>`, `<` 重定向
  - 实现：`&` 后台执行

- [ ] **实现脚本支持**
  - 支持：.sh 脚本执行
  - 支持：条件判断、循环
  - 支持：变量和函数

### 4.2 系统工具

- [ ] **实现基础工具集**
  ```
  cat, ls, cp, mv, rm, mkdir
  grep, find, sort, uniq
  echo, head, tail, wc
  ```

- [ ] **实现系统监控工具**
  ```
  ps, top, free, df
  uptime, dmesg
  ```

---

## 🎯 阶段五：同步与进程间通信（优先级：中）

### 5.1 同步原语

- [x] **实现 Spinlock** ✅ (v0.9.3)
  - 完成：`kernel/lib/spinlock.h` + `kernel/sync/spinlock.cpp`
  - 完成：中断保存/恢复 + 原子 test-and-set
  - 完成：`arch_spin_hint()` 架构抽象

- [x] **实现 WaitQueue** ✅ (v0.9.3)
  - 完成：`kernel/lib/waitqueue.h` + `kernel/sync/waitqueue.cpp`
  - 完成：FIFO sleep/wakeup_one/wakeup_all
  - 完成：kbd 和 ide 驱动已适配

- [x] **实现信号量** ✅ (v0.9.3)
  - 完成：`kernel/lib/semaphore.h` + `kernel/sync/semaphore.cpp`
  - 完成：down/up/try_down（基于 Spinlock + WaitQueue）

- [x] **实现互斥锁** ✅ (v0.9.3)
  - 完成：`kernel/lib/mutex.h` + `kernel/sync/mutex.cpp`
  - 完成：lock/unlock/try_lock + ownership 断言

- [x] **实现 LockGuard\<T\>** ✅ (v0.9.3)
  - 完成：`kernel/lib/lock_guard.h`
  - 完成：泛型 RAII 模板，所有锁类型统一使用

- [ ] **实现条件变量**
  - 文件：`kernel/sync/condvar.cpp`
  - 实现：wait(Mutex&)、signal、broadcast
  - 基础：可在现有 WaitQueue 上扩展

- [ ] **实现读写锁**
  - 基础：基于 Spinlock + WaitQueue
  - 优化：多读单写场景

### 5.2 进程间通信

- [ ] **实现管道**
  - 实现：匿名管道
  - 实现：命名管道（FIFO）

- [ ] **实现消息队列**
  - 实现：消息发送、接收
  - 实现：消息优先级

- [ ] **实现共享内存**
  - 实现：shmget、shmat、shmdt
  - 学习点：内存共享机制

- [ ] **实现信号**
  - 实现：基本信号处理
  - 实现：信号掩码

---

## 🎯 阶段六：网络与高级特性（优先级：低）

### 6.1 网络支持

- [ ] **实现网络驱动**
  - 驱动：E1000 网卡（QEMU 支持）
  - 学习点：DMA、中断处理

- [ ] **实现网络协议栈**
  - 实现：以太网帧处理
  - 实现：ARP 协议
  - 实现：IP 协议
  - 实现：ICMP（ping）
  - 实现：TCP/UDP（基础）

- [ ] **实现 Socket 接口**
  - 实现：socket()、bind()、listen()
  - 实现：connect()、accept()
  - 实现：send()、recv()

### 6.2 多核支持

- [ ] **实现 SMP 初始化**
  - 检测：CPU 数量
  - 启动：Application Processors (AP)

- [ ] **实现 per-CPU 数据**
  - 设计：每核独立数据结构
  - 优化：减少锁竞争

- [ ] **实现多核调度**
  - 实现：负载均衡
  - 实现：CPU 亲和性

- [x] **实现 Spinlock** ✅ (v0.9.3)
  - 完成：已在同步原语阶段实现
  - 完成：设计兼容 SMP（atomic + arch_spin_hint）

### 6.3 64 位支持

- [x] **迁移到 Long Mode** ✅ (v0.7.0)
  - 完成：x86_64 长模式内核，高半核映射 `0xFFFFFFFF80000000`
  - 完成：64 位 TrapFrame、Context（RAX-R15）
  - 完成：引导代码 32→64 模式切换

- [ ] **支持大内存**
  - 支持：超过 4GB 物理内存
  - 修改：内存管理器

### 6.4 安全性

- [ ] **实现用户管理**
  - 实现：用户、组概念
  - 实现：/etc/passwd、/etc/group

- [ ] **实现权限控制**
  - 实现：基于 UID/GID 的权限
  - 实现：文件权限检查

- [ ] **实现 ASLR**
  - 随机化：栈、堆、mmap 地址
  - 目标：提高安全性

---

## 🎯 阶段七：调试、测试与文档（优先级：高）

### 7.1 调试工具

- [ ] **实现内核调试器**
  - 功能：断点、单步
  - 功能：内存查看、寄存器查看
  - 参考：kdb、kgdb

- [ ] **增强 assert 机制**
  - 添加：更详细的错误信息
  - 添加：堆栈回溯

- [ ] **实现内存泄漏检测**
  - 跟踪：内存分配和释放
  - 报告：未释放的内存

- [ ] **实现性能分析工具**
  - 统计：函数调用次数和耗时
  - 实现：简单的 profiler

- [ ] **添加日志系统**
  - 分级：DEBUG、INFO、WARN、ERROR
  - 输出：串口、屏幕、文件

### 7.2 测试框架

- [ ] **建立单元测试框架**
  - 目录：`tests/unit/`
  - 框架：简单的测试宏

- [ ] **为核心模块编写测试**
  - 测试：内存管理器（参考 `kernel/mm/pmm_firstfit.cpp` 的 `check()`）
  - 测试：调度器
  - 测试：文件系统

- [ ] **实现集成测试**
  - 目录：`tests/integration/`
  - 测试：系统调用
  - 测试：多进程交互

- [ ] **添加压力测试**
  - 测试：内存分配压力
  - 测试：进程创建压力
  - 测试：文件 I/O 压力

- [ ] **创建自动化测试脚本**
  - 脚本：`tests/run_tests.sh`
  - 集成：CI/CD（GitHub Actions）

### 7.3 文档完善

- [ ] **更新 README**
  - 完善：`README.md`
  - 添加：项目简介、特性列表
  - 添加：详细构建说明
  - 添加：运行和调试指南

- [ ] **编写架构设计文档**
  - 文档：`docs/architecture.md`
  - 内容：系统架构图
  - 内容：各模块设计思路

- [ ] **编写开发者指南**
  - 文档：`docs/developer_guide.md`
  - 内容：代码规范
  - 内容：提交规范
  - 内容：调试技巧

- [ ] **创建学习路径文档**
  - 文档：`docs/learning_path.md`
  - 内容：推荐学习顺序
  - 内容：每个模块的学习要点
  - 内容：参考资源

- [ ] **为主要模块添加注释**
  - 要求：每个函数添加文档注释
  - 要求：关键算法添加说明
  - 工具：Doxygen 文档生成

### 7.4 代码质量

- [ ] **统一代码风格**
  - 工具：clang-format
  - 配置：`.clang-format` 文件
  - 应用：所有源文件

- [ ] **优化 Makefile**
  - 完善：`Makefile`
  - 添加：依赖自动生成
  - 添加：增量编译优化
  - 添加：更多构建目标

- [ ] **消除代码重复**
  - 工具：PMD、CPD
  - 重构：提取公共函数

- [ ] **完善错误处理**
  - 检查：所有返回值
  - 添加：错误码定义
  - 统一：错误处理风格

---

## 📚 学习建议

### 推荐学习路径

**第一阶段：打好基础（2-3 个月）**
1. 完善物理内存管理（Buddy System）
2. 实现基本的进程创建（fork）
3. 完善调度器（Round-Robin）
4. 实现基础系统调用

**第二阶段：建立生态（3-4 个月）**
1. 实现简单文件系统
2. 完善设备驱动（键盘、硬盘）
3. 扩展 Shell 功能
4. 创建用户程序

**第三阶段：深入优化（持续）**
1. 优化调度算法（CFS）
2. 实现虚拟内存高级特性（COW、swap）
3. 添加网络支持
4. 探索多核支持

### 学习方法

**对于每个模块：**
1. **理论学习**：阅读相关章节（推荐《Operating System Concepts》）
2. **代码阅读**：研究 Linux 0.11 或 xv6 的实现
3. **动手实践**：在 Zonix 中实现该功能
4. **测试验证**：编写测试用例
5. **文档记录**：写下实现思路和遇到的问题

**调试技巧：**
- 使用 GDB：`make debug ARCH=x86`
- 添加 `cprintf` 调试输出
- 查看反汇编：`obj/kernel.asm`

**推荐资源：**
- 书籍：《Operating System Concepts》（恐龙书）
- 书籍：《Understanding the Linux Kernel》
- 项目：xv6（MIT 教学 OS）
- 项目：Linux 0.11
- 网站：OSDev Wiki
- 课程：MIT 6.828、清华 uCore

---

## 🔧 开发工具配置

### 基础环境
```bash
# 当前已安装（参考 README.md）
sudo apt install make clang lld llvm nasm

# 推荐额外安装
sudo apt install gdb qemu-system-x86 
sudo apt install clang-format cppcheck
```

### 调试配置
- GDB 配置：`scripts/gdbinit`
- QEMU 调试：参考 `Makefile` 的 `debug` / `debug-bios` 目标

### 代码编辑器
推荐使用 VS Code 并安装插件：
- C/C++ (Microsoft)
- Makefile Tools
- x86 and x86_64 Assembly

---

## 📊 进度追踪

### 统计信息
- **总任务数**：约 150+ 个子任务
- **预计完成时间**：6-12 个月（取决于学习深度）
- **当前完成度**：约 60%（同步原语 + 多架构联调 + 自动化测试）
- **最近完成**：x86/aarch64 QEMU 测试链路与调度器/链表重构 (2026-03-23)

### 里程碑

- [x] **Milestone 0**：✅ 迁移到 64 位长模式 (v0.7.0)
- [x] **Milestone 1**：✅ 实现基本的文件系统操作 — FAT32 只读 (v0.7.1)
- [x] **Milestone 2**：✅ 支持多进程并发 — Round-Robin + fork/exit/wait (v0.3.0+)
- [x] **Milestone 3**：✅ 实现交互式 Shell — 20+ 命令 (v0.8.0)
- [x] **Milestone 4**：✅ 同步原语 + 抢占式调度 (v0.9.3)
- [x] **Milestone 4.5**：✅ 多架构 CI 联调（x86 BIOS/UEFI + aarch64 UEFI）
- [ ] **Milestone 5**：用户态进程 (Ring 3) + syscall 表 + ELF 加载
- [ ] **Milestone 6**：VFS + 文件描述符 + 管道
- [ ] **Milestone 6**：VFS/FD 增强（写路径、pipe、fcntl、stdio 语义）
- [ ] **Milestone 7**：支持网络通信

---

## 📝 附录

### 项目结构
```
zonix-plus/
├── arch/x86/
│   ├── boot/           # 引导加载程序（BIOS + UEFI）
│   ├── include/asm/    # 架构相关头文件
│   └── kernel/         # 架构相关内核代码（head.S, IDT, 上下文切换）
├── kernel/             # 内核代码
│   ├── cons/           # 控制台和 Shell
│   ├── debug/          # 调试工具
│   ├── drivers/        # 设备驱动
│   ├── block/          # 块设备抽象
│   ├── fs/             # 文件系统
│   ├── lib/            # 内核库函数
│   ├── mm/             # 内存管理
│   ├── sched/          # 调度器
│   ├── sync/           # 同步原语（spinlock, waitqueue, semaphore, mutex）
│   └── trap/           # 中断处理
├── include/            # 公共头文件（base/, kernel/, uefi/）
├── fonts/              # 控制台字体 (PSF)
├── scripts/            # 构建工具和链接脚本
├── docs/               # 文档
└── tests/              # 测试（待创建）
```

### 重要文件
- `Makefile`：构建系统
- `scripts/kernel.ld`：内核链接脚本
- `scripts/boot.ld`：引导程序链接脚本
- `kernel/mm/pmm.cpp`：物理内存管理
- `kernel/trap/trap.cpp`：中断处理
- `kernel/cons/shell.cpp`：Shell 实现

---

## 📄 许可证

本项目采用 MIT 许可证，详见 `LICENSE`。

