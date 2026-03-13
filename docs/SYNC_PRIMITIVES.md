# Zonix 同步原语与调度器流程

> 本文档描述 Zonix 内核中所有同步原语和抢占式调度器的完整调用流程。

---

## 1. Spinlock — 自旋锁

**文件**: `kernel/lib/spinlock.h`

单核下退化为关中断；保留原子操作以便将来扩展 SMP。

```
spinlock.acquire()
│
├─ Step 1: 保存中断状态
│   flags = arch_irq_save()        ── 读 RFLAGS
│   arch_irq_disable()             ── CLI 关中断
│   saved_flags_ = flags           ── 存到成员变量
│
├─ Step 2: 原子抢锁
│   while (__atomic_test_and_set(&locked_, ACQUIRE))
│       ├─ 单核: 中断已关，不会失败，不自旋
│       └─ SMP:  pause 指令让出流水线，循环等待
│
└─ 返回: 持有锁，中断已关

spinlock.release()
│
├─ Step 1: 原子释放
│   __atomic_clear(&locked_, RELEASE)
│
├─ Step 2: 恢复中断状态
│   if (saved_flags_ & FL_IF)
│       arch_irq_enable()          ── STI 开中断
│
└─ 返回: 锁已释放，中断状态恢复

Spinlock::Guard (RAII)
│
├─ 构造: Guard(lock) → lock.acquire()
└─ 析构: ~Guard()   → lock.release()
```

---

## 2. WaitQueue — 等待队列

**文件**: `kernel/lib/waitqueue.h`, `kernel/sync/waitqueue.cpp`

替代原来 ad-hoc 的 `wait_state` / `wakeup()` 模式，提供结构化的阻塞/唤醒机制。

```
waitq.sleep()                          ← 进程上下文调用
│
├─ Step 1: 创建栈上 Entry
│   Entry entry { .task = current }
│
├─ Step 2: 加入队列 + 设为睡眠 (持锁)
│   {
│     Spinlock::Guard guard(lock_)     ── CLI + 抢锁
│     head_.add_before(entry.node)     ── 插入链表尾部 (FIFO)
│     entry.task->state = Sleeping     ── 标记进程阻塞
│   }                                  ── STI + 释放锁
│
├─ Step 3: 让出 CPU
│   TaskManager::schedule()            ── 切换到其他 Runnable 进程
│   ════════ 进程在此阻塞，直到被 wakeup ════════
│
├─ Step 4: 被唤醒后，清理 (持锁)
│   {
│     Spinlock::Guard guard(lock_)
│     entry.node.unlink()              ── 从队列中移除自己
│   }
│
└─ 返回: 进程已唤醒，继续执行

waitq.wakeup_one()                     ← 中断/进程上下文均可
│
├─ Spinlock::Guard guard(lock_)        ── CLI + 抢锁
├─ if head_.empty() → return           ── 无人等待，直接返回
│
├─ first = head_.get_next()            ── 取队首 (FIFO)
├─ entry = Entry::from_node(first)     ── 链表节点 → Entry 结构体
├─ first->unlink()                     ── 从队列移除
├─ entry->task->wakeup()               ── state = Runnable
│
└─ 返回: 已唤醒 1 个进程

waitq.wakeup_all()                     ← 中断/进程上下文均可
│
├─ Spinlock::Guard guard(lock_)
├─ while (!head_.empty())
│   ├─ node = head_.get_next()
│   ├─ entry = Entry::from_node(node)
│   ├─ node->unlink()
│   └─ entry->task->wakeup()          ── 逐个唤醒
│
└─ 返回: 所有等待进程已设为 Runnable
```

---

## 3. Semaphore — 信号量

**文件**: `kernel/lib/semaphore.h`

计数信号量，count 为 0 时阻塞。基于 Spinlock + WaitQueue。

```
sem.down()  (P 操作 / wait)
│
├─ Loop:
│   ├─ {
│   │     Spinlock::Guard guard(lock_)
│   │     if (count_ > 0)
│   │       ├─ count_--             ── 获取资源
│   │       └─ return               ── 成功，退出
│   │   }
│   │
│   └─ count_ == 0，资源不可用:
│       waitq_.sleep()              ── 阻塞在 WaitQueue 上
│       │
│       │  ┌─ Entry 加入 waitq_ 链表
│       │  ├─ state = Sleeping
│       │  ├─ schedule() ─── 切换走
│       │  │   ... 等待 up() 唤醒 ...
│       │  └─ 被唤醒，回到 Loop 顶部重试
│       │
│       └─ (回到 Loop 再次检查 count_)
│
└─ 返回: 已成功获取 1 个资源单位

sem.up()  (V 操作 / signal)
│
├─ Step 1: 增加计数 (持锁)
│   {
│     Spinlock::Guard guard(lock_)
│     count_++                      ── 释放 1 单位资源
│   }
│
├─ Step 2: 唤醒 1 个等待者
│   waitq_.wakeup_one()             ── FIFO 唤醒
│
└─ 返回

sem.try_down()  (非阻塞尝试)
│
├─ Spinlock::Guard guard(lock_)
├─ if (count_ > 0)
│   ├─ count_--
│   └─ return true
├─ else
│   └─ return false                 ── 不阻塞，直接失败
```

---

## 4. Mutex — 互斥锁

**文件**: `kernel/lib/mutex.h`, `kernel/sync/mutex.cpp`

带所有权追踪的互斥锁。竞争时阻塞（睡眠），而非自旋。

```
mutex.lock()
│
├─ Loop:
│   ├─ {
│   │     Spinlock::Guard guard(spin_)
│   │     if (!held_)
│   │       ├─ held_ = true
│   │       ├─ owner_ = current     ── 记录持有者
│   │       └─ return               ── 成功获取
│   │   }
│   │
│   └─ held_ == true，锁被占:
│       waitq_.sleep()              ── 阻塞等待 unlock()
│       └─ (被唤醒后回到 Loop 重试)
│
└─ 返回: 持有 Mutex

mutex.unlock()
│
├─ Step 1: 释放所有权 (持锁)
│   {
│     Spinlock::Guard guard(spin_)
│     held_ = false
│     owner_ = nullptr
│   }
│
├─ Step 2: 唤醒 1 个竞争者
│   waitq_.wakeup_one()
│
└─ 返回

mutex.try_lock()  (非阻塞尝试)
│
├─ Spinlock::Guard guard(spin_)
├─ if (!held_)
│   ├─ held_ = true
│   ├─ owner_ = current
│   └─ return true
├─ else
│   └─ return false

Mutex::Guard (RAII)
│
├─ 构造: Guard(mtx) → mtx.lock()
└─ 析构: ~Guard()   → mtx.unlock()
```

---

## 5. 抢占式调度 — Timer Tick + Schedule

**文件**: `kernel/trap/trap.cpp`, `kernel/sched/sched.cpp`

PIT 以 100Hz (10ms) 触发中断，每 tick 递减时间片，耗尽时抢占。

```
PIT 中断 (每 10ms, IRQ0)
│
└─ trap(tf)  [trapno == IRQ_OFFSET + IRQ_TIMER]
    │
    └─ irq_timer(tf)
        │
        ├─ pit::ticks++
        │
        ├─ sched::tick()
        │   └─ TaskManager::tick()
        │       ├─ if (!s_current) return
        │       ├─ if (s_current == idle) return
        │       ├─ s_current->time_slice--
        │       └─ if (time_slice <= 0)
        │           s_current->need_resched = 1  ── 标记需要调度
        │
        ├─ if (cur->need_resched)
        │   └─ sched::schedule()                 ── 触发调度 ↓
        │
        └─ fbcons::tick()
```

### schedule() — 优先级感知 Round-Robin

```
TaskManager::schedule()
│
├─ intr::Guard guard                 ── 关中断保护调度器
│
├─ Step 1: 初始化
│   next = s_idle_proc               ── 默认选 idle
│   best_prio = IDLE_PRIO + 1       ── 比 idle 还差
│
├─ Step 2: 从 RR 游标开始扫描进程链表
│   s_sched_cursor → 上次调度位置的下一个节点
│   │
│   └─ 遍历整个 s_proc_list (环绕):
│       for each proc:
│         if (proc->state == Runnable && proc != idle)
│           if (proc->priority < best_prio)  ── 数字小 = 优先级高
│             next = proc
│             best_prio = proc->priority
│
├─ Step 3: 推进 RR 游标
│   s_sched_cursor = next->list_node.get_next()
│
├─ Step 4: 补充时间片
│   if (next->time_slice <= 0)
│     next->time_slice = calc_timeslice(next->priority)
│     │
│     └─ calc_timeslice(prio):
│         slice = BASE(10) × (21 - prio) / 11
│         ├─ prio 0  (最高) → ~19 ticks (190ms)
│         ├─ prio 10 (默认) → 10 ticks (100ms)
│         └─ prio 20 (最低) → ~1 tick  (10ms)
│
├─ Step 5: 上下文切换
│   if (next != s_current)
│     ├─ s_current->state = Runnable     ── 当前进程入就绪队列
│     ├─ s_current->need_resched = 0
│     └─ next->run()
│         ├─ set_current(next)
│         ├─ next->state = Running
│         ├─ arch_load_cr3(next->cr3)    ── 切换页表
│         ├─ tss::set_rsp0(next->kstack) ── 更新 Ring0 栈
│         └─ switch_to(&prev->ctx, &next->ctx)
│             ├─ 保存 prev: rip,rsp,rbx,rbp,r12-r15
│             ├─ 恢复 next: r15-r12,rbp,rbx,rsp
│             └─ push rip; ret           ── 跳到 next 的代码
│   else
│     └─ need_resched = 0               ── 无需切换，清标记
│
└─ 返回 (或切换到 next 的上下文)
```

---

## 6. 使用示例 — 键盘阻塞读

**文件**: `kernel/drivers/kbd.cpp`

```
kbd::getc_blocking()                    ← Shell 进程调用
│
├─ Loop:
│   ├─ {
│   │     intr::Guard guard            ── 关中断检查缓冲区
│   │     if (kbd_read != kbd_write)   ── 有数据
│   │       ├─ c = kbd_buf[kbd_read]
│   │       ├─ kbd_read++ (环形)
│   │       └─ return c                ── 成功读取
│   │   }
│   │
│   └─ 缓冲区空:
│       kbd_waitq.sleep()              ── 阻塞在等待队列
│           ├─ Entry 入队
│           ├─ state = Sleeping
│           ├─ schedule() ─── 切换到 idle/其他进程
│           │
│           │  ───── 用户按键 ─────
│           │  IRQ1 → kbd::intr()
│           │    ├─ getc() 读硬件扫描码
│           │    ├─ 写入 kbd_buf
│           │    └─ kbd_waitq.wakeup_one()
│           │        └─ Shell进程 → Runnable
│           │
│           │  ───── 下次 timer tick ─────
│           │  schedule() 选中 Shell
│           │
│           └─ 被唤醒，回到 Loop 顶部
│
└─ return c
```

---

## 7. 使用示例 — IDE 磁盘读

**文件**: `kernel/drivers/ide.cpp`

```
IdeDevice::read(block, buf, count)
│
├─ for each sector i:
│   │
│   ├─ hd_wait_ready_on_base()        ── 忙等 BSY 清除
│   │
│   ├─ {
│   │     intr::Guard guard
│   │     request.reset()
│   │     request.buffer = buf + i*512
│   │     request.op = Read
│   │     发送 LBA + CMD_READ → 端口 I/O
│   │   }
│   │
│   ├─ while (!request.done)
│   │     request.waitq.sleep()        ── 阻塞等中断
│   │       │
│   │       │  ───── 磁盘完成传输 ─────
│   │       │  IRQ14 → IdeDevice::interrupt()
│   │       │    ├─ 读取 STATUS 寄存器
│   │       │    ├─ insw() 读 256 words 到 buffer
│   │       │    ├─ request.done = 1
│   │       │    └─ request.waitq.wakeup_one()
│   │       │
│   │       └─ 被唤醒，检查 done → 退出循环
│   │
│   ├─ if (request.err) → return -1
│   └─ request.reset()
│
└─ return 0
```

---

## 依赖关系

```
   ┌──────────────────────────────────────────────┐
   │              应用层 (Shell / User)            │
   └───────────┬──────────────────┬───────────────┘
               │                  │
         kbd::getc_blocking  IdeDevice::read
               │                  │
   ┌───────────▼──────────────────▼───────────────┐
   │              WaitQueue.sleep/wakeup           │
   │           Semaphore.down/up                   │
   │           Mutex.lock/unlock                   │
   └───────────┬──────────────────────────────────┘
               │
   ┌───────────▼──────────────────────────────────┐
   │              Spinlock (原子 + CLI/STI)         │
   └───────────┬──────────────────────────────────┘
               │
   ┌───────────▼──────────────────────────────────┐
   │     TaskManager::schedule() (RR + Priority)   │
   │         ← tick() 每 10ms 递减 time_slice      │
   │         ← need_resched 触发切换               │
   └───────────┬──────────────────────────────────┘
               │
   ┌───────────▼──────────────────────────────────┐
   │     switch_to() — 汇编上下文切换              │
   │     保存/恢复 rip,rsp,rbx,rbp,r12-r15        │
   └──────────────────────────────────────────────┘
```
