# Zonix 文件系统架构与操作流程

> 本文档描述 Zonix 内核中文件系统的完整架构分层、数据结构和操作流程。

---

## 1. 整体架构 — 分层总览

```
   ┌──────────────────────────────────────────────────────┐
   │          应用层 (Shell / User Process)                │
   │   cat, ls, exec, mount, umount                       │
   └────────────┬─────────────────────────────────────────┘
                │  vfs::open / read / write / close
   ┌────────────▼─────────────────────────────────────────┐
   │          文件描述符层 (fd::Table)                      │
   │   每进程 16 个 FD 槽位，追踪 File* + offset           │
   └────────────┬─────────────────────────────────────────┘
                │  vfs::File* (多态)
   ┌────────────▼─────────────────────────────────────────┐
   │          VFS 虚拟文件系统层                            │
   │   路径解析 → MountSlot → FileSystem                   │
   │   挂载点:  /dev  |  /mnt  |  /                       │
   └──────┬──────────────┬──────────────┬─────────────────┘
          │              │              │
   ┌──────▼─────┐ ┌──────▼─────┐ ┌─────▼──────┐
   │   DevFS    │ │  FatFS     │ │  FatFS     │
   │  (字符设备) │ │ (/mnt)     │ │ (/)        │
   └──────┬─────┘ └──────┬─────┘ └─────┬──────┘
          │              │              │
   ┌──────▼─────┐   ┌───▼──────────────▼─────────────────┐
   │ ConsoleFile│   │        FatInfo (FAT 核心实现)        │
   │ (字符 I/O) │   │  分区检测 → FAT表 → 簇链 → 目录树   │
   └────────────┘   └────────────────┬────────────────────┘
                                     │  BlockDevice::read/write
                    ┌────────────────▼────────────────────┐
                    │        块设备层 (BlockManager)        │
                    │  IDE / VirtIO / SDHCI 磁盘驱动       │
                    └─────────────────────────────────────┘
```

---

## 2. VFS 虚拟文件系统层

**文件**: `kernel/fs/vfs.h`, `kernel/fs/vfs.cpp`, `kernel/fs/vfs_fs.h`

### 2.1 核心数据结构

```
vfs::File (抽象基类)                ── 所有可打开文件的统一接口
├─ virtual read(buf, size, offset)  ── 读取数据
├─ virtual write(buf, size, offset) ── 写入数据
└─ virtual stat(Stat*)              ── 获取文件元信息

vfs::Stat                           ── 文件元信息
├─ NodeType type                    ── File / Directory / CharDevice
├─ uint32_t size                    ── 文件大小 (字节)
└─ uint32_t attrs                   ── 属性 (FAT 属性等)

vfs::DirEntry                       ── 目录项信息
├─ char name[32]                    ── 文件名
├─ NodeType type                    ── 节点类型
├─ uint32_t size                    ── 文件大小
└─ uint32_t attrs                   ── 属性

vfs::FileSystem (抽象接口)          ── 每种文件系统必须实现
├─ mount(BlockDevice*)              ── 挂载
├─ unmount()                        ── 卸载
├─ open(relpath, File**)            ── 打开文件 (相对路径)
├─ stat(relpath, Stat*)             ── 获取元信息
├─ readdir(relpath, callback, arg)  ── 列目录
└─ print()                          ── 打印 FS 信息
```

### 2.2 挂载表 — 路径路由

```
s_mounts[3] — 固定三槽挂载表
┌───────┬────────────┬──────────────┐
│ Index │ Mount Point│ 用途         │
├───────┼────────────┼──────────────┤
│   0   │ /dev       │ 设备文件系统  │
│   1   │ /mnt       │ 手动挂载点   │
│   2   │ /          │ 根文件系统   │
└───────┴────────────┴──────────────┘

每个 MountSlot 包含:
├─ mount_point     ── 挂载路径前缀
├─ dev             ── BlockDevice* (DevFS 为 nullptr)
├─ dev_name        ── 设备名 (如 "hda")
├─ fs              ── FileSystem* 实例
└─ fs_type         ── 文件系统类型名 (如 "fat", "devfs")
```

### 2.3 文件系统注册机制

```
s_fs_registry[8] — 文件系统工厂注册表
s_char_dev_registry[8] — 字符设备注册表

注册时机: 静态构造器 (内核启动前)
│
├─ FatFsRegistrar   → register_fs("fat", FatFileSystem::create)
├─ DevFsRegistrar   → register_fs("devfs", DevFileSystem::create)
└─ ConsoleRegistrar → register_char_dev("console", ConsoleFile::create)
```

### 2.4 路径解析流程

```
resolve_path("/mnt/docs/README.txt")
│
├─ Step 1: 前缀匹配
│   "/" 开头 → 跳过
│   "dev/" ?  → 不匹配
│   "mnt/" ?  → 匹配! → 选中 s_mounts[1]
│
├─ Step 2: 截取相对路径
│   "/mnt/docs/README.txt" → "docs/README.txt"
│
└─ 返回: MountSlot* (指向 /mnt)，relpath = "docs/README.txt"

resolve_path("/dev/console")
│
├─ "dev/" → 匹配 s_mounts[0]
└─ relpath = "console"

resolve_path("/boot/kernel")
│
├─ "dev/" → 不匹配
├─ "mnt/" → 不匹配
└─ 回退到 "/" → s_mounts[2]，relpath = "boot/kernel"
```

---

## 3. 文件描述符层

**文件**: `kernel/fs/fd.h`, `kernel/fs/fd.cpp`

每个进程持有一个 `fd::Table`，最多 16 个打开文件。

```
fd::Table
├─ entries_[16]         ── 文件描述符槽位数组
│   ├─ [0] → { file: ConsoleFile*, offset: 0, used: true }   (stdin)
│   ├─ [1] → { file: ConsoleFile*, offset: 0, used: true }   (stdout)
│   ├─ [2] → { file: ConsoleFile*, offset: 0, used: true }   (stderr)
│   ├─ [3] → { file: FatFile*,     offset: 512, used: true }
│   └─ [4..15] → { file: nullptr, offset: 0, used: false }
│
├─ alloc(File*)         ── 分配一个空闲 FD，返回 0~15
├─ get(fd)              ── 获取 Entry*，检查越界和已分配
├─ close(fd)            ── 关闭 FD，调用 vfs::close(file)
├─ close_all()          ── 进程退出时关闭所有 FD
└─ fork_from(parent, policy)
    ├─ Reset  → 新建空表 (默认)
    └─ Share  → 复制父进程 FD (未实现，需引用计数)
```

### 3.1 FD 分配流程

```
fd::Table::alloc(file)
│
├─ 遍历 entries_[0..15]
│   找到第一个 used == false 的槽位
│
├─ 填充:
│   entry.file = file
│   entry.offset = 0
│   entry.used = true
│
└─ 返回: FD 编号 (0~15)，满则返回 -1
```

---

## 4. 文件操作完整流程

### 4.1 open — 打开文件

```
vfs::open("/mnt/hello.txt", &file)
│
├─ Step 1: 路径解析
│   resolve_path("/mnt/hello.txt")
│   → slot = s_mounts[1] (/mnt)
│   → relpath = "hello.txt"
│
├─ Step 2: 检查挂载
│   if (!slot->fs) return -ENOENT   ── /mnt 未挂载
│
├─ Step 3: 委托给具体文件系统
│   slot->fs->open("hello.txt", &file)
│   │
│   └─ FatFileSystem::open("hello.txt", &file)
│       │
│       ├─ fat_.find_file("hello.txt", &dir_entry)
│       │   ├─ 解析路径为组件: ["hello.txt"]
│       │   ├─ 从根簇开始遍历目录
│       │   ├─ 逐扇区读取目录项
│       │   └─ 大小写不敏感匹配 → 找到 FatDirEntry
│       │
│       ├─ new FatFile(fat_, dir_entry)
│       │   └─ 包装为 vfs::File 子类
│       │
│       └─ *file = fatFile
│
└─ 返回: 0 成功，*file 指向 FatFile 实例
```

### 4.2 read — 读取文件

```
vfs::read(file, buf, 4096, offset=0)
│
├─ 调用 file->read(buf, 4096, 0)   ── 虚函数分派
│
└─ FatFile::read(buf, 4096, 0)
    │
    └─ fat_.read_file(&entry_, buf, 0, 4096)
        │
        └─ do_file_io(&entry, buf, offset=0, size=4096, "read", writeback=false)
            │
            ├─ Step 1: 定位起始簇
            │   cluster = entry->cluster_lo | (cluster_hi << 16)
            │   skip = offset / bytes_per_cluster_
            │   for (i = 0; i < skip; i++)
            │       cluster = read_entry(cluster)    ── 跟随 FAT 链
            │
            ├─ Step 2: 逐簇读取
            │   while (已读 < size && cluster 有效)
            │   │
            │   │  ├─ sector = cluster_to_sector(cluster)
            │   │  │   └─ data_start_ + (cluster - 2) * sectors_per_cluster_
            │   │  │
            │   │  ├─ dev_->read(sector, tmp_buf, sectors_per_cluster_)
            │   │  │   └─ 从块设备读取一整簇到临时缓冲区
            │   │  │
            │   │  ├─ 计算簇内偏移和拷贝长度
            │   │  │   in_cluster_offset = offset % bytes_per_cluster_
            │   │  │   copy_len = min(剩余, bytes_per_cluster_ - in_cluster_offset)
            │   │  │
            │   │  ├─ memcpy(buf + 已读, tmp_buf + in_cluster_offset, copy_len)
            │   │  │
            │   │  └─ cluster = read_entry(cluster)  ── 下一簇
            │   │
            │   └─ 循环直到读完 size 或链结束
            │
            └─ 返回: 实际读取字节数

    FAT 链跟踪:
    read_entry(cluster)
    │
    ├─ fat_offset = cluster * 4           (FAT32)
    ├─ fat_sector = fat_start_ + fat_offset / 512
    │
    ├─ if (buffer_sector_ != fat_sector)
    │   dev_->read(fat_sector, buffer_, 1) ── 缓存 FAT 扇区
    │   buffer_sector_ = fat_sector
    │
    ├─ entry_offset = fat_offset % 512
    ├─ value = *(uint32_t*)(buffer_ + entry_offset) & 0x0FFFFFFF
    │
    └─ return value
        ├─ >= 0x0FFFFFF8 → EOC (簇链结束)
        ├─ == 0x0FFFFFF7 → 坏簇
        └─ 其他 → 下一簇号
```

### 4.3 write — 写入文件

```
vfs::write(file, buf, size, offset)
│
├─ 调用 file->write(buf, size, offset)
│
└─ FatFile::write(buf, size, offset)
    │
    └─ fat_.write_file(&entry_, buf, offset, size)
        │
        └─ do_file_io(&entry, buf, offset, size, "write", writeback=true)
            │
            ├─ 和 read 相同的簇链遍历逻辑
            ├─ memcpy 方向相反: tmp_buf ← buf
            ├─ dev_->write(sector, tmp_buf, ...) ── 回写到设备
            │
            └─ 注: write_entry() 尚未实现 (不能分配新簇)
```

### 4.4 close — 关闭文件

```
vfs::close(file)
│
└─ delete file              ── 调用虚析构函数
    └─ ~FatFile()           ── 释放 FatFile 资源
```

### 4.5 stat — 获取文件信息

```
vfs::stat("/mnt/hello.txt", &st)
│
├─ resolve_path → slot, relpath
│
└─ slot->fs->stat(relpath, &st)
    │
    └─ FatFileSystem::stat("hello.txt", &st)
        │
        ├─ fat_.find_file("hello.txt", &entry)
        │
        └─ st->set(File, entry.file_size, entry.attr)
            ├─ type = File / Directory (根据 attr)
            ├─ size = 文件大小
            └─ attrs = FAT 属性 (Archive, ReadOnly, Hidden...)
```

### 4.6 readdir — 列目录

```
vfs::readdir("/mnt", callback, arg)
│
├─ resolve_path → slot (/mnt), relpath = ""
│
└─ slot->fs->readdir("", callback, arg)
    │
    └─ FatFileSystem::readdir("")
        │
        └─ fat_.read_dir("", bridge_callback, ...)
            │
            ├─ 定位目录起始簇 (根目录 = root_cluster_)
            │
            ├─ 遍历目录簇链:
            │   for each sector in cluster:
            │     for each 32字节 FatDirEntry:
            │       │
            │       ├─ 跳过空项 (name[0] == 0x00)
            │       ├─ 跳过已删除 (name[0] == 0xE5)
            │       ├─ 跳过 LFN 项 (attr == 0x0F)
            │       ├─ 跳过卷标 (attr & VOLUME_ID)
            │       │
            │       └─ 有效项: 转换为 vfs::DirEntry
            │           ├─ 解析 8.3 短文件名
            │           ├─ 设置 type, size, attrs
            │           └─ callback(dirEntry, arg)
            │
            └─ cluster = read_entry(cluster) → 下一簇
```

---

## 5. 设备文件系统 (DevFS)

**文件**: `kernel/fs/devfs.cpp`

虚拟文件系统，无需块设备，管理字符设备节点。

```
DevFS 架构:
│
├─ s_char_dev_registry[8]  ── 字符设备注册表
│   ├─ [0] { name: "console", factory: ConsoleFile::create }
│   └─ [1..7] 空
│
├─ open("console")
│   ├─ 遍历注册表，找到 name == "console"
│   ├─ 调用 factory() → new ConsoleFile()
│   └─ 返回 vfs::File*
│
├─ stat("console")
│   └─ 返回 { type: CharDevice }
│
└─ readdir("")
    └─ 遍历注册表，列出所有已注册设备名
```

### 5.1 ConsoleFile — 控制台字符设备

**文件**: `kernel/cons/cons_vfs.cpp`

```
ConsoleFile : public vfs::File
│
├─ read(buf, size, offset)
│   ├─ 忽略 offset (流式设备)
│   ├─ for i in 0..size:
│   │     buf[i] = cons::getc()     ── 阻塞读取键盘输入
│   └─ return size
│
├─ write(buf, size, offset)
│   ├─ 忽略 offset
│   ├─ for i in 0..size:
│   │     cons::putc(buf[i])        ── 输出到帧缓冲控制台
│   └─ return size
│
└─ stat(st)
    └─ st->set(CharDevice, 0, 0)
```

---

## 6. FAT 文件系统核心

**文件**: `kernel/fs/fat.h`, `kernel/fs/fat/fat_*.cpp`

### 6.1 FAT 磁盘布局

```
┌──────────┬───────────────┬───────────────┬──────────────────────┐
│ 引导扇区  │   保留扇区     │   FAT 表      │     数据区             │
│ Sector 0 │ 1..reserved-1 │ fat_start_    │     data_start_       │
└──────────┴───────────────┴───────────────┴──────────────────────┘
                            │               │
                            │  FAT1  FAT2   │ 簇2  簇3  簇4  ...
                            │ (主表) (副本)  │
                            └───────────────┘

FAT 表项 (FAT32, 每项 4 字节):
┌─────────┬────────────────────────┐
│ 簇号     │ 值 (指向下一簇)          │
├─────────┼────────────────────────┤
│ 2       │ 3          (→ 簇3)     │
│ 3       │ 0x0FFFFFFF (EOC, 结束)  │
│ 4       │ 5          (→ 簇5)     │
│ 5       │ 6          (→ 簇6)     │
│ 6       │ 0x0FFFFFFF (EOC)       │
│ 7       │ 0x00000000 (空闲)      │
└─────────┴────────────────────────┘
```

### 6.2 挂载流程

```
FatInfo::mount(dev)
│
├─ Step 1: 分区检测
│   ├─ 读取 Sector 0 (MBR 或 GPT 保护性 MBR)
│   │
│   ├─ 检查 GPT:
│   │   if (mbr.partitions[0].type == 0xEE)  ── GPT 保护性 MBR
│   │     ├─ 读取 Sector 1 (GPT Header)
│   │     ├─ 验证签名 "EFI PART"
│   │     ├─ 读取分区表项
│   │     └─ 找到 ESP 分区 → partition_start_
│   │
│   └─ 检查 MBR:
│       遍历 4 个分区项，找到类型匹配的
│       → partition_start_
│
├─ Step 2: 读取引导扇区
│   dev->read(partition_start_, boot_sector, 1)
│
├─ Step 3: 验证 + 初始化
│   do_init_state(dev, partition_start_, boot_sector)
│   │
│   ├─ 提取 BPB 参数:
│   │   bytes_per_sector_ = 512
│   │   sectors_per_cluster_ = bs.sectors_per_cluster
│   │   reserved_sectors_ = bs.reserved_sectors
│   │   num_fats_ = bs.num_fats
│   │
│   ├─ 计算关键偏移:
│   │   fat_start_ = partition_start_ + reserved_sectors_
│   │   root_start_ = fat_start_ + num_fats_ * fat_size_  (FAT16)
│   │   data_start_ = ...
│   │
│   ├─ 判断 FAT 类型:
│   │   cluster_count_ < 4085  → FAT12
│   │   cluster_count_ < 65525 → FAT16
│   │   else                   → FAT32
│   │
│   └─ FAT32: root_cluster_ = bs.root_cluster (通常 = 2)
│
└─ 返回: 0 成功
```

### 6.3 目录树查找

```
fat_.find_file("docs/README.txt", &result)
│
├─ 解析路径: ["docs", "README.txt"]
│
├─ 从 root_cluster_ 开始
│   │
│   ├─ 搜索 "docs":
│   │   find_entry(root_cluster_, "docs", &entry)
│   │   │
│   │   ├─ 读取根目录所有簇
│   │   ├─ 遍历每个 32 字节目录项
│   │   ├─ 转换 8.3 文件名 → 比较 (不区分大小写)
│   │   └─ 找到! entry.attr & DIRECTORY → 可以继续
│   │
│   ├─ docs_cluster = entry.cluster_lo | (cluster_hi << 16)
│   │
│   └─ 搜索 "README.txt":
│       find_entry(docs_cluster, "README.txt", &result)
│       │
│       ├─ 读取 docs 目录所有簇
│       ├─ 遍历目录项
│       └─ 找到! result 包含文件元信息
│
└─ 返回: 0 成功, result = FatDirEntry
    ├─ file_size  = 1234
    ├─ cluster_lo = 5
    ├─ attr       = ARCHIVE
    └─ ...
```

---

## 7. 块设备层

**文件**: `kernel/block/blk.h`, `kernel/block/blk.cpp`

### 7.1 块设备接口

```
BlockDevice (抽象基类)
├─ type         ── Disk / Swap
├─ size         ── 总扇区数
├─ name[8]      ── 设备名 (如 "hda")
│
├─ read(block_number, buf, block_count)   ── 读扇区
├─ write(block_number, buf, block_count)  ── 写扇区
└─ print_info()                           ── 打印设备信息

扇区大小: BlockDevice::SIZE = 512 字节
```

### 7.2 设备管理器

```
BlockManager (静态注册表)
├─ s_devices[4]           ── 最多 4 个块设备
│
├─ register_device(dev)   ── 注册新设备
├─ get_device("hda")      ── 按名称查找
├─ get_device(0)          ── 按索引查找
├─ get_device(Disk)       ── 按类型查找第一个
└─ print()                ── 打印 lsblk 风格设备列表
```

---

## 8. 内核启动 — 文件系统初始化

```
kernel init()
│
├─ ...
│
├─ blk::init()
│   ├─ BlockManager::init()
│   ├─ probe_backends()              ── 架构相关: 探测磁盘
│   │   ├─ x86:   IDE/AHCI 控制器
│   │   ├─ arm64: VirtIO 块设备
│   │   └─ rv64:  SDHCI / VirtIO
│   └─ 打印发现的设备数
│
├─ vfs::init()
│   └─ mount("/dev", nullptr, "devfs")
│       ├─ 查找工厂: s_fs_registry["devfs"] → DevFileSystem::create
│       ├─ fs = new DevFileSystem()
│       ├─ fs->mount(nullptr)        ── DevFS 不需要设备
│       └─ s_mounts[0].fs = fs
│
├─ rootfs::init()
│   ├─ 遍历所有注册的块设备:
│   │   for i in 0..device_count:
│   │     dev = get_device(i)
│   │     if dev.type != Disk → 跳过
│   │     尝试 mount("/", dev, "fat")
│   │     if 成功 → break
│   │
│   └─ 挂载成功: 根文件系统就绪
│
└─ ... (启动 Shell 进程)
```

---

## 9. Shell 文件操作命令

**文件**: `kernel/cons/shell.cpp`

### 9.1 mount / umount

```
cmd_mount("hda")
│
├─ dev = BlockManager::get_device("hda")
├─ if (!dev) → "device not found"
│
├─ vfs::mount("/mnt", dev, "fat")
│   ├─ 查找工厂 → FatFileSystem::create
│   ├─ fs = new FatFileSystem()
│   ├─ fs->mount(dev)              ── FatInfo 挂载
│   └─ s_mounts[1] = { "/mnt", dev, "hda", fs, "fat" }
│
└─ "mounted hda at /mnt"

cmd_umount()
│
├─ vfs::umount("/mnt")
│   ├─ slot->fs->unmount()         ── FatInfo 卸载
│   ├─ delete slot->fs
│   └─ 清空 slot
│
└─ "unmounted /mnt"
```

### 9.2 ls

```
cmd_ls("/mnt")
│
├─ vfs::readdir("/mnt", print_entry_cb, nullptr)
│   │
│   └─ 对每个目录项回调:
│       print_entry_cb(entry, arg)
│       ├─ 打印属性: [D]目录 [A]存档 [R]只读 [H]隐藏 [S]系统
│       ├─ 打印大小: entry.size 字节
│       └─ 打印名称: entry.name
│
└─ 输出示例:
   [D]         0  .
   [D]         0  ..
   [A]      1234  hello.txt
   [DA]        0  docs
```

### 9.3 cat

```
cmd_cat("/mnt/hello.txt")
│
├─ vfs::open("/mnt/hello.txt", &file)
│
├─ vfs::stat("/mnt/hello.txt", &st)
│   └─ size = 1234
│
├─ 分块读取 (每次 4KB):
│   while (offset < size)
│     bytes = min(4096, size - offset)
│     vfs::read(file, buf, bytes, offset)
│     ├─ 输出到控制台: cprintf("%s", buf)
│     └─ offset += bytes
│
├─ vfs::close(file)
│
└─ 打印内容完毕
```

### 9.4 exec

```
cmd_exec("/bin/hello")
│
├─ exec::exec("/bin/hello")
│   │
│   ├─ Step 1: 读取 ELF 文件
│   │   vfs::open("/bin/hello", &file)
│   │   vfs::stat → size
│   │   buf = kmalloc(size)
│   │   vfs::read(file, buf, size, 0)
│   │   vfs::close(file)
│   │
│   ├─ Step 2: 加载到用户空间
│   │   pgdir = create_user_pgdir()
│   │   │   ├─ 分配页目录页
│   │   │   ├─ 拷贝内核映射
│   │   │   └─ 返回独立地址空间
│   │   │
│   │   entry_point = elf::load(buf, size, pgdir)
│   │   │   ├─ 验证 ELF 头
│   │   │   ├─ 遍历 LOAD 段
│   │   │   │   ├─ 分配用户页 (VA < KERNEL_BASE)
│   │   │   │   ├─ 拷贝代码/数据
│   │   │   │   └─ BSS 段清零
│   │   │   └─ 返回入口地址
│   │   │
│   │   stack_top = setup_user_stack(pgdir)
│   │       ├─ 分配栈页 (USER_STACK_TOP 向下)
│   │       └─ 返回栈顶地址
│   │
│   ├─ Step 3: 创建进程
│   │   sched::fork(trapframe, mem_desc)
│   │   └─ 返回 PID
│   │
│   └─ return PID
│
├─ sched::wait(pid)        ── 等待子进程退出
│
└─ 回到 Shell
```

---

## 10. 用户态文件操作 (系统调用)

**文件**: `kernel/lib/unistd.h`

```
用户程序                      系统调用号       内核处理
─────────────────────────────────────────────────────
open("/dev/console")    ──→   NR_OPEN  (5)  ──→  vfs::open → fd::alloc
read(fd, buf, n)        ──→   NR_READ  (3)  ──→  fd::get(fd) → file->read
write(fd, buf, n)       ──→   NR_WRITE (4)  ──→  fd::get(fd) → file->write
close(fd)               ──→   NR_CLOSE (6)  ──→  fd::close(fd) → vfs::close
exit(code)              ──→   NR_EXIT  (1)  ──→  fd::close_all → sched::exit
```

---

## 依赖关系

```
   ┌─────────────────────────────────────────────────────────────┐
   │              应用层 (Shell 命令 / User 系统调用)              │
   │    ls · cat · exec · mount · open · read · write · close    │
   └────────────┬───────────────────────────┬────────────────────┘
                │                           │
   ┌────────────▼───────────┐  ┌────────────▼───────────────────┐
   │ fd::Table (文件描述符)   │  │ exec::exec (ELF 加载执行)      │
   │ 16 FD/进程, offset 追踪 │  │ VFS读文件 → ELF解析 → fork    │
   └────────────┬───────────┘  └────────────┬───────────────────┘
                │                           │
   ┌────────────▼───────────────────────────▼────────────────────┐
   │                    VFS 虚拟文件系统层                         │
   │   路径解析 · 挂载管理 · File 多态接口 · 文件系统注册           │
   └──────┬──────────────────┬──────────────────┬────────────────┘
          │                  │                  │
   ┌──────▼──────┐   ┌──────▼──────┐    ┌──────▼──────┐
   │   DevFS     │   │   FatFS     │    │   FatFS     │
   │  /dev       │   │  /mnt       │    │  /          │
   │ (虚拟)      │   │ (手动挂载)   │    │ (自动挂载)   │
   └──────┬──────┘   └──────┬──────┘    └──────┬──────┘
          │                 │                  │
   ┌──────▼──────┐   ┌─────▼──────────────────▼─────────────────┐
   │ ConsoleFile │   │              FatInfo                      │
   │ cons::getc  │   │  分区检测 · BPB解析 · FAT表缓存           │
   │ cons::putc  │   │  簇链遍历 · 目录树查找 · 文件读写          │
   └─────────────┘   └──────────────────┬────────────────────────┘
                                        │
                     ┌──────────────────▼────────────────────────┐
                     │           BlockDevice (块设备接口)          │
                     │    read(sector, buf, count)               │
                     │    write(sector, buf, count)              │
                     │    512 字节/扇区                           │
                     └──────────────────┬────────────────────────┘
                                        │
                     ┌──────────────────▼────────────────────────┐
                     │      硬件驱动 (IDE / VirtIO / SDHCI)       │
                     └───────────────────────────────────────────┘
```
