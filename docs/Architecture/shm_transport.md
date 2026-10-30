---
layout: default
title: 4 架构说明 · 共享内存传输
---

# 4.2 共享内存传输

<div class="overview">
<div class="label">本页总览</div>
<p><code>nav2/shm_transport</code> 是 Windows 侧的本地总线。它的算法只有一份，
平台差异收在 <code>ShmChannel::Impl</code> 里。</p>
<ul>
  <li><a href="#ring">环形缓冲与游标</a></li>
  <li><a href="#platform">两套后端的对照</a></li>
  <li><a href="#traps">两条踩过的坑</a></li>
  <li><a href="#bench">性能基线</a></li>
</ul>
</div>

## 4.2.1 环形缓冲与游标

| 特性 | 说明 |
| --- | --- |
| 通道名 | `ros2shim` + 话题名（可选前缀 `NAV2_SHIM_NAMESPACE`） |
| 拓扑 | **单写者 + 多读者**，读者各自持有游标 |
| 落后太多 | 丢最旧的消息（`dropped` 计数） |
| 槽几何 | 由 `NAV2_SHM_*` 决定，发布 / 订阅双方必须一致 |

「单写者」是硬约定，不是性能取舍：守护进程镜像 `/scan` 时，本机就不能再有另一个进程发 `/scan`。

## 4.2.2 两套后端的对照

| 要同步的东西 | Windows | Linux / POSIX |
| --- | --- | --- |
| 通道本体 | `CreateFileMapping(Local\nav2shim_<名>_data)` | `shm_open("/nav2shim_<名>_data")` + `ftruncate` + `mmap` |
| 写者锁 | 命名互斥体 + 5 s 有界 `WaitForSingleObject` | 段内 `pthread_mutex_t`（`PTHREAD_PROCESS_SHARED` + `ROBUST`）+ `pthread_mutex_timedlock` |
| 唤醒读者 | 命名手工事件（`SetEvent` / `ResetEvent`） | 段内 `pthread_cond_t` **广播**（`sem_post` 只唤醒一个，多个读者不够用） |
| 段的生命周期 | 最后一个句柄关掉时内核销毁 | 段本身的 `flock` 共享锁：活着的每人一把，内核在进程死掉时收回，最后一把锁的人 `shm_unlink` |

POSIX 侧那两个同步对象就住在共享段里（`ShmSync`，紧跟 layout 头），所以 `kHeaderSize` 在两个平台上
不一样：Windows 是「layout 头对齐到 64」，POSIX 还要加上 `ShmSync`。
`header_size` 写进头部，`open` 用同一个值算槽位偏移。

## 4.2.3 两条踩过的坑

<div class="callout warn">
<span class="label">坑 1</span>
<p><strong><code>EOWNERDEAD</code> 必须当成「锁拿到了」。</strong></p>
<p><code>ROBUST</code> 互斥体在持有者死掉时把锁交出来，但只有调用
<code>pthread_mutex_consistent()</code> 的人才能再把它放掉。写者的
<code>wake_readers()</code> 原来把 <code>EOWNERDEAD</code> 当成「没锁上」直接返回，
<code>wait_mutex</code> 就被留成「owner 是自己的线程、状态不可恢复」—— 下一次
<code>publish</code> 等的是它自己，0% CPU，永远。</p>
<p>这不是推演：一个被杀掉的写者留下的段被下一轮 <code>create</code> 收编，第一轮就挂在吞吐用例上，
之后每一轮都挂在同一处（gdb 里线程和段内 <code>__owner</code> 都对得上）。</p>
</div>

<div class="callout warn">
<span class="label">坑 2</span>
<p><strong>段的生命周期不能靠段内的引用计数。</strong></p>
<p>计数只有活着的进程会减，被 <code>kill -9</code> 的进程没人减，名字就永远不回收，
下一轮 <code>create</code> 收编到的是尸体：环里是上一轮的消息，互斥体是死线程留下的状态。</p>
<p>现在用 <code>flock</code>：<code>create</code> 先要独占锁，<strong>拿得到 = 没人在用</strong>，
那就把段里的环、互斥体、条件变量全部重新初始化；<strong>拿不到 = 有活人在用</strong>，
原样收编（Windows 的命名对象本来就是这个语义，内核负责）。</p>
</div>

两条都有回归用例：**写者被杀掉的段必须起一个空环**、**写者还活着的段不准被重置**。

## 4.2.4 C ABI 的前缀

C ABI 加了 `shm_channel_` 前缀（`shm_channel_create` / `shm_channel_open` / …）：
原来的 `shm_open` 在 POSIX 上是 `<sys/mman.h>` 的符号，同名不只编不过，
`.so` 里导出它还会**抢占真正的 `shm_open`**。唯一的调用方是 `python/shm_transport.py`，一起改了。

## 4.2.5 两平台都能编能测

| 平台 | 结果 |
| --- | --- |
| Windows / MSYS2 UCRT64（MinGW） | 5 条全绿，连跑 3 次 |
| Ubuntu 24.04 / glibc 2.39 / g++ 13.3 | 7 条全绿，连跑 5 次（含一次在 6 核全量编译负载下） |
| Alpine 3.24 / musl（Ubuntu 上的 chroot） | 7 条全绿，连跑 5 次，吞吐 1.96 M msg/s |

```bash
# 一个入口，两边都能跑
python nav2/shm_transport/build.py
python nav2/shm_transport/build.py --build-dir nav2/shm_transport/build-dbg
python nav2/shm_transport/build.py --no-test --jobs 6
```

<div class="callout note">
<span class="label">提示</span>
<p>Alpine 里只有<strong>直接 <code>g++</code> 编译</strong>跑得通：CMake + ninja 在 chroot 里会
因为缺 <code>/proc</code> 报 <code>posix_spawn /usr/bin/c++</code> 失败。
别把这段读成「CMake 在 Alpine 上能跑」。</p>
<p>那个 Alpine 根的 <code>/dev/shm</code> 是<strong>普通 ext4 目录、不是 tmpfs</strong>，
所以「<code>MAP_SHARED</code> 落在 tmpfs 之外的文件系统上」顺带也验了。</p>
</div>

## 4.2.6 性能基线

共享内存**不是零拷贝**。一次发布有 4 次拷贝：

```
publish(msg) → serialize()      拷贝1 结构体→bytes
             → memcpy 进共享槽   拷贝2 bytes→slot
============== 进程边界（这里是共享，不是拷贝）==============
subscribe() → memcpy 出槽       拷贝3 slot→读者缓冲
            → deserialize()     拷贝4 bytes→结构体
```

准确说法是**零系统调用（zero-syscall），不是零拷贝**。省下的是内核 socket/UDP 栈。

实测（Release / 6 P-core，完整路径含 serialize/deserialize）：

| 载荷 | 线上大小 | 同进程 msg/s | 跨进程 msg/s | 延迟 p50 |
| --- | --- | --- | --- | --- |
| Twist | 48 B | 1.32 M | 1.40 M | 0.5 µs |
| Odometry | 709 B | 462 k | 483 k | 1.6 µs |
| LaserScan | 5813 B | 87 k | 88 k | 8.8 µs |

**跨进程 ≈ 同进程** —— 这是「共享内存本来就不为进程边界付钱」的最好证据。

<div class="callout note">
<span class="label">下一步的优化点</span>
<p>真正值得优化的是 <code>WriteArchive</code> <strong>逐字段 <code>push_back</code> 且不
<code>reserve</code></strong>，大消息会反复扩容 + 拷贝；加 size 预估 + <code>reserve()</code> 是下一步。</p>
</div>

<div class="pagenav">
<div class="prev"><span class="dir">上一页</span><a class="ttl" href="overview">4.1 总体架构</a></div>
<div class="next"><span class="dir">下一页</span><a class="ttl" href="action">4.3 Action 实现</a></div>
</div>
