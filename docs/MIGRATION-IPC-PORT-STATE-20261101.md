# 0475：CoreLocation 等待端口的真实转交状态

2026-11-01。在[0474接收者观察](MIGRATION-IPC-RECEIVERS-20261101.md)基础上，
0355只扩展原生观察器。实机取得29组完整IPC记录、14条端口状态、3条转交路径。
此前无直接接收任务的端口在本轮全部为IN-TRANSIT，不是仅凭PID=-1推定。
**这是实际阻塞去向的证据，迁移和system-shell登记仍未完成。**

## 锁与引用生命周期

按当前原XNU ipc_port.h/ipc_port.c契约记录dead、task-space、kernel-space、
reply-space、transit、limbo，及队列深度/限额、service/specialreply等标志。
初始translate_send返回借用锁，没有新引用。转交目的端口由parent持引用；在
parent锁内取得下一跳独立引用，解锁parent并释放上一跳自有引用后，再锁下一跳。
每次只持一个端口锁，最多8跳，重复地址截断；输出全部在锁外。

不用原版可能长链/多锁的destination_chain_lock；不读取用户消息或修改消息、
权限、调度、初始化结果。采样期间端口可死亡/改派，所以路径非原子，既不是
单次RPC配对，也不是全系统依赖环或死锁证明。

## 实机观察与符号化

| 位置 | 实际观察 |
| --- | --- |
| SpringBoard27主线程 | check-in同步上下文指向backboardd57 |
| backboardd57 | DataMigration同步上下文指向datamigrator166 |
| datamigrator166 | 两次采样分别指向wrapper221/242，不能凭PID变化称迁移成功 |
| 第二wrapper242/tid2842 | 栈在_CLInternalPerformMigration；head为transit，队列1/6；目的端口由locationd63持有，队列8/16 |
| locationd63/tid1475 | 自身同步请求的转交目的为同一个locationd服务端口；两采样队列6/16、8/16，不能据计数识别具体消息 |
| locationd63/tid1494 | BLSXPCBacklightProxy isAlwaysOnEnabled同步上下文仍指向SpringBoard27 |
| locationd63/tid1471 | SEND1203指向launchd1，队列6/6；不能据此把它与BLS或迁移视为同一事务 |

迁移wrapper与locationd自请求的目的对象均为本次运行的`fffffe1b3b717600`。
这些地址仅用于同一次日志的对应，不用于跨启动硬编码。原始日志和匹配20H392
缓存的符号化资料分别保留，先前离线推断不覆盖真实接收者结果。

## 三层验收

- 主机：实际生产helper240检查、13个已编译行为变异；包含死亡/改派、所有状态、
  队列、逐跳引用释放、8跳/重复地址截断、224字节行预算。IPC/task/scheduler和
  并发生命周期为明确模型，不冒充物理并发证明。初次遗漏状态行前缀断言使一个
  变异存活，已补检查后全量重跑13个均拒绝；原失败未算通过。
- 原生：334 common+4 host、5,279源文件精确回放且构建前后相同；该TU0错误、
  28个未屏蔽警告。最终原生端口API调用与STATE输出存在，HID207/宏/ACL及打包
  回读保持。相较0474只改观察器头，全部crypto/security源码不变。
- 实机：boot_b Sending/Writing均OKAY；旧A先UNWRAP并恢复原负载/密文，锁定已有
  缓存/直接读写零传输拒绝；旧B/C/D及原securityd6–11旧记录恢复。Root/nonce、
  bank/systembag、RTC/NVRAM/defaults保持；原watchdog心跳至311.756秒，无panic
  或查询超时。正常FASTBOOT前实际VFS/UFS同步、PON04、IMEM MATCH。

0475原生18,997,232字节，SHA-256：

```text
1f11a8b8f289021a30c5d79a65248f75b4e88c763db7fa8c1e1294d6de852571
```

[实机收据](qualification/0475/0475-IPC-qualified.json)、
[解析记录](qualification/0475/0475-IPC-observations.json)、
[加密回归](qualification/0475/0475-A-qualified.json)、
[可重跑生产代码/源码/日志校验](../scripts/tests/run-ipc-port-state.py)。
原始封存日志字节不变，闭源二进制/完整反汇编/密钥/私有Data不发布。

## 下一步

查明locationd服务初始化和回复的实际生命周期，以及与桌面check-in的启动顺序。
不能把队列有消息当作消息内容/原始事务证明，也不能把插件实例变化当作完成。
保留现有硬件密钥与加密Data，不跳迁移、不编造成功、不删库/重建key/降类。
完整密钥接口、数据库/迁移/桌面目标仍在进行；暂不扩展GPU/显示/输入，蓝牙延期。
factory无PIN仍不等价Apple SEP/口令；无新触摸、物理120Hz、全盘扫描或Data fsck。
