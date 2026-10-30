---
layout: default
title: 2 系统配置 · 网络与端口配置
---

# 2.2 网络与端口配置

<div class="overview">
<div class="label">本页总览</div>
<p>本节介绍三个网络角色（仿真 agent / 真 ROS2 agent / 中继）分别监听哪个端口、
守护进程怎么找到它们，以及跨网段和防火墙怎么办。</p>
<ul>
  <li><a href="#roles">端口与角色</a></li>
  <li><a href="#discovery">自动发现的工作方式</a></li>
  <li><a href="#cross">跨网段与防火墙</a></li>
  <li><a href="#yaml">配置文件</a></li>
</ul>
</div>

<a id="roles"></a>

## 2.2.1 端口与角色

| 角色 | 在哪台机器 | 默认端口 | 环境变量 / 配置 |
| --- | --- | --- | --- |
| 仿真 agent | Ubuntu | 50051 | `ROS2_LINK_SIM_PORT` / `run_sim --port` |
| 真 ROS2 agent（`RclcppBackend`） | Ubuntu | 50111 | `ROS2_LINK_REAL_PORT` |
| 中继（合并两张图，对外） | Ubuntu | 50060 | `ROS2_LINK_PORT` |
| 桥守护进程 | Windows | 客户端，不监听 | — |

数据路径：Windows 守护进程只连**中继**（50060）；中继再连两个上游 agent（50051 / 50111）。

<div class="callout note">
<span class="label">为什么只连一个</span>
<p><code>bridge_daemon.cpp</code> 的 <code>connectToAgent()</code> 是<strong>单 peer 模型</strong>，
只连最近的那一个 agent。所以不能让 Windows 分别连两个 agent，必须有一个合并入口 ——
这就是中继存在的原因。</p>
</div>

<a id="discovery"></a>

## 2.2.2 自动发现的工作方式

守护进程没给 <code>ROS2_BRIDGE_AGENT</code> 时，做一次局域网快速扫描：

1. 枚举所有 up、非回环的网卡，推导出 IPv4 子网（CIDR）；
2. 把每个子网展开成主机地址（去掉网络 / 广播地址）；
3. 并发 TCP 探活配置的端口（默认 50051）；
4. 每个命中用 gRPC `Ping` 确认，并计时 RTT；
5. 去重、按延迟升序排，连最近的那个。

实测 504 个地址的扫描 + 握手在 1 s 量级完成。

<div class="callout warn">
<span class="label">重要</span>
<p><strong>本机永远不是自己的目标。</strong>排除集从<strong>所有</strong>网卡（up 和 down）构建，
整段 <code>127.0.0.0/8</code> 都被丢掉（不只是 <code>127.0.0.1</code>）。
所以守护进程和 agent 在同一台机器上时必须显式给地址。</p>
</div>

### 扫描参数

| 参数 | 默认 | 含义 |
| --- | --- | --- |
| `ports` | `[50051]` | 每个候选主机扫哪些 TCP 端口 |
| `connect_timeout_ms` | `150` | 每个 (host, port) 的 TCP 探活超时 |
| `concurrency` | `256` | 并发度 |
| `verify_with_ping` | `true` | 每个命中用 gRPC Ping 确认 |
| `overall_timeout_ms` | `3000` | 总预算 |
| `include_self` / `include_loopback` | `false` / `false` | 要不要连本机 / 回环 |
| `max_results` | `32` | 最多列多少个 |

<a id="cross"></a>

## 2.2.3 跨网段与防火墙

<div class="callout note">
<span class="label">注意</span>
<p>Ubuntu 要在扫描可达的同一网段内。跨网段要显式给地址
（<code>--agent host:port</code>），本项目<strong>没有中继 / 网关</strong>能做跨网段发现。</p>
</div>

两条要确认的：

```bash
# Ubuntu：确认 agent 绑在 0.0.0.0 而不是 127.0.0.1
ss -lnt | grep -E '50051|50111|50060'

# Ubuntu：防火墙放行（如果有）
sudo ufw allow 50060/tcp
```

Windows 侧客户端不需要放行入站 —— 它只是往外连。

<a id="yaml"></a>

## 2.2.4 配置文件

端口、超时、发现范围都在一个 YAML 里（带注释）：

```yaml
# gRPC/ros2_gRPC_sdk/config/ros2_gRPC_sdk.yaml
server:
  address: "0.0.0.0:50051"   # agent 绑定地址（0.0.0.0 = 局域网可达）
  num_threads: 0             # 0 = gRPC 默认 worker 数

client:
  connect_timeout_ms: 3000
  rpc_timeout_ms: 5000
  use_tls: false             # 开关在，但从没验过加密连接
  tls_root_cert_pem: ""
  tls_target_name_override: ""

discovery:
  ports: [50051]
  connect_timeout_ms: 150
  concurrency: 256
  max_hosts_per_subnet: 1024
  verify_with_ping: true
  ping_timeout_ms: 500
  overall_timeout_ms: 3000
  include_self: false
  include_loopback: false
  subnets: []                # 空 = 扫所有本地子网
  max_results: 32
```

配置目录的解析顺序（C++ 和 Python 一致）：

1. `ROS2_GRPC_SDK_CONFIG_DIR` 环境变量（一个目录）
2. `./config` 如果存在（仓库里就是这个）
3. 每用户目录：`%APPDATA%\ros2_gRPC_sdk`（Windows）或
   `$XDG_CONFIG_HOME/ros2_gRPC_sdk` / `~/.config/ros2_gRPC_sdk`

`ROS2_GRPC_SDK_CONFIG` 可以指向具体文件。文件缺失<strong>不是错误</strong> —— 会用内置默认值。

`max_results` 虽然能列 32 个，但守护进程只连最近的那一个（单 peer 模型），多机器人场景没做。

<div class="callout warn">
<span class="label">关于安全</span>
<p>客户端有 <code>use_tls</code> / <code>tls_root_cert_pem</code> /
<code>tls_target_name_override</code> 三个开关，但默认明文、<strong>从没验过</strong>；
没有 mTLS / 鉴权 / 压缩。局域网内默认假设可信。</p>
</div>

<div class="pagenav">
<div class="prev"><span class="dir">上一页</span><a class="ttl" href="environment">2.1 环境变量配置</a></div>
<div class="next"><span class="dir">下一页</span><a class="ttl" href="shm">2.3 共享内存几何配置</a></div>
</div>
