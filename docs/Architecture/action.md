---
layout: default
title: 4 架构说明 · Action 实现
---

# 4.3 Action 实现

<div class="overview">
<div class="label">本页总览</div>
<p>上游一个 action 是在两个 topic 和两个 service 之上搭出来的。这里没有 DDS，
所以把同一件事摊成 <strong>6 条共享内存通道</strong>。本页说明这 6 条通道的协议，
以及踩过之后才明白的几条约定。</p>
<ul>
  <li><a href="#channels">六条通道</a></li>
  <li><a href="#rules">必须守住的约定</a></li>
  <li><a href="#trace">可观测性</a></li>
</ul>
</div>

<a id="channels"></a>

## 4.3.1 六条通道

| 通道 | 写者 | 读者 | 载荷 |
| --- | --- | --- | --- |
| `<action>/_goal` | 客户端 | 服务端 | 8 字节小端 goal id + CDR→兼容层的目标体 |
| `<action>/_goal_ack` | 服务端 | 所有客户端 | id + 1 字节（0 = REJECT，1 = ACCEPT） |
| `<action>/_cancel` | 客户端 | 服务端 | id（无体） |
| `<action>/_cancel_ack` | 服务端 | 所有客户端 | id + 1 字节（0 = REJECT，1 = ACCEPT） |
| `<action>/_feedback` | 服务端 | 所有客户端 | id + 反馈体 |
| `<action>/_result` | 服务端 | 所有客户端 | id + 1 字节 `ResultCode`（0 UNKNOWN / 1 SUCCEEDED / 2 ABORTED / 3 CANCELED）+ 结果体 |

<a id="rules"></a>

## 4.3.2 必须守住的约定

<div class="callout warn">
<span class="label">约定 1：id 代替 UUID</span>
<p>上游用 16 字节 UUID 认领 goal，这里用一个进程内自增计数器发出的 <code>uint64_t</code>，
<code>make_uuid(id)</code> 再把前 8 字节摊成 UUID 给 <code>rclcpp_action</code> 的 API 看。</p>
<p>计数器是<strong>进程级 static</strong>，不是每个客户端一个：同一个 action 上两个客户端各自从 1 数起的话，
服务端就分不清两条消息是谁的。跨进程不冲突，因为计数只在「发给同一个服务端」的客户端之间共享。</p>
</div>

<div class="callout warn">
<span class="label">约定 2：每个客户端 / 服务端各自一个读游标</span>
<p><code>Node::get_private_channel</code> 每次都 <code>open()</code> 出一个新对象，
而不是像 <code>get_channel</code> 那样把同一个对象递给所有人。</p>
<p>否则 <code>bt_navigator</code> 里「自己发给自己的客户端」
（<code>NavigateToPoseNavigator::self_client_</code>，接收 <code>/goal_pose</code> 时才发目标）
会把另一个客户端等的 ack 先读走。</p>
</div>

<div class="callout warn">
<span class="label">约定 3：同一条话题在一个节点里也只有一个读游标</span>
<p><code>Node::get_channel</code> 把同一个对象递给所有要这个名字的人。这对订阅是对的 ——
一个节点、一条话题、一个回调 —— 但<strong>在一个节点里对同一条话题建两个订阅就会互相抢读</strong>：
谁先 poll 谁把消息吃掉，另一个永远收不到。</p>
<p>上游的代码里有这种形状（<code>enable_stamped_cmd_vel</code> 打开时，同一话题上会同时挂
<code>Twist</code> 与 <code>TwistStamped</code> 两个订阅），所以
<code>config/nav2_shim_overrides.yaml</code> 把它钉成 <code>false</code>，
<code>nav2_waypoints.cpp</code> 也只听纯 <code>Twist</code> 的 <code>/cmd_vel</code>。</p>
</div>

<div class="callout warn">
<span class="label">约定 4：构造时 seek_to_latest()</span>
<p>共享内存段是内核对象，写它的进程退出之后里面的旧消息还在。客户端在构造时把
<code>_goal_ack</code> / <code>_cancel_ack</code> / <code>_feedback</code> / <code>_result</code>
四条的游标推到队头，服务端把 <code>_goal</code> / <code>_cancel</code> 推到队头 ——
不然新起的进程会去回答上一轮留下的 goal（而那时它还没 active，于是回一个 REJECT）。</p>
<p>唯一例外是 <code>transient_local</code> 的 latched 语义，它用 <code>seek_to_latched()</code>：
游标停在队头<strong>前一格</strong>，下一次读正好拿到最后一条，也就是「订阅晚于发布仍然要收到」的那种。</p>
</div>

<div class="callout warn">
<span class="label">约定 5：被 ack 的 result 推迟一拍再交付</span>
<p>客户端的一次 <code>spin_until_future_complete</code> 可能把 ack 和 result 一起收走 ——
本机 server 就在同一个进程里时几乎必然如此（<code>waypoint_follower</code> → <code>bt_navigator</code>
正是这样）。</p>
<p>立刻交付的话，<code>bt_action_node</code> 那种「结果早于 goal response 到达」的调用方会把它
当成上一个目标的结果丢掉，这条 goal 就再也没有下文（树停在
<code>ComputePathToPose=RUNNING</code> / <code>FollowPath=IDLE</code>）。
<code>Client::poll()</code> 因此把这种 result 放进 <code>deferred_results_</code>，
下一次 <code>poll()</code> 再交付，<code>bt_action_node</code> 也改成收下早到的结果。</p>
</div>

<div class="callout warn">
<span class="label">约定 6：结果必须「发出去」</span>
<p><code>Server::publish_result()</code> 写完通道就把 handle 从表里摘掉；
取消也要回一个结果（上游同样如此），不然调用方要等满整个超时。</p>
</div>

<a id="trace"></a>

## 4.3.3 可观测性

<div class="callout note">
<span class="label">排查手段</span>
<p>设 <code>ROS2_SHIM_ACTION_TRACE=1</code> 之后，六条通道上的每一次收发都往 stderr 打一行
<code>[act CLIENT|SERVER pid=... t=...] &lt;action&gt; &lt;事件&gt;</code>。</p>
<p>冷启动排查就是靠它：把每个节点的 <code>.log</code> 按时间排一遍，能直接看出「谁发了、谁没收到」。</p>
</div>

每次发消息还会检查 `ShmChannel::publish()` 的返回值，超过槽大小的载荷会打一行
`[act !! ] publish failed: N bytes > slot_size M`（这条**不受开关控制**，丢消息永远是 bug）。

行为树那边有对应的开关：

```powershell
$env:NAV2_SHIM_BT_TRACE = '1'      # bt_navigator 定期打印行为树里每个节点的状态
```

## 4.3.4 跨机的那一半

跨机的 action 转发在守护进程里：
`ListActions` / `GetActionInfo` 把 Ubuntu agent 的 action 表并进图注册表，
`SendActionGoal` / `ActionFeedback`(流) / `GetActionResult` / `CancelActionGoal` 四个 RPC 做转发，
codec 表（`bridge/windows/src/action_registry.cpp`）负责 CDR ↔ 兼容层的双向转码。

表里现在是 **20 个** action：仿真自己的 `Drive`，加上 `nav2_msgs` 里**每一个 `.action` 文件**
（19 个），并且每个类型都带一份逐字段打印器（`ros2 action send_goal` 打的就是它）——
别的 action 要跨机，仍得先给它加一份 codec。

<div class="callout warn">
<span class="label">限制</span>
<p><strong>action 的环形缓冲只有 8 个槽。</strong><code>_feedback</code> 在跟随时是 ~40 Hz，
也就是约 190 ms 的缓冲：任何一次超过 190 ms 的调度延迟都会让读者跳过（<code>dropped</code> 计数）
中间那些反馈。</p>
<p>反馈丢几条无害，但这个「慢读者静默丢消息」的性质是这套传输的固有性质，值得记住。
客户端自己<strong>没有超时</strong>（和上游一致），所以真丢了一条 ack/result，
表现就是调用方一直等 —— 排查手段就是上面那条 <code>ROS2_SHIM_ACTION_TRACE</code>。</p>
</div>

<div class="pagenav">
<div class="prev"><span class="dir">上一页</span><a class="ttl" href="shm_transport">4.2 共享内存传输</a></div>
<div class="next"><span class="dir">下一页</span><a class="ttl" href="../Capability/overview">5.1 已实现能力</a></div>
</div>
