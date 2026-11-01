# 0472–0474：原版迁移与桌面等待的实际端口诊断

2026-11-01。0474已完成有界实机验收：29组元数据/接收者完整配对，8组查到任务，
其中6组为receive-only同步等待上下文。原A/B/C/D、旧keychain和watchdog保持。
这使服务去向可复核，**没有解除迁移等待，也没有完成system-shell登记或桌面**。

## 原生实现及边界

0353只改固定任务线程观察器并增加一个头文件。对非runnable线程，在原调度锁下
复制保存寄存器；只解码trap47的x16和x1–x5，不读取x0指向的消息内容。
持有task/space引用，原ipc_port_translate_send成功返回借用且已锁定的端口；
在锁内调用ipc_port_get_receiver_task_locked并复制元数据，解锁后打印。
不改变端口、权限、消息、线程调度或原版服务行为。

0354按原XNU mach_msg.c的MACH64_RCV_SYNC_WAIT语义，补查packed mr_lp低字
指向的sync boosting端口；SEND优先。receive-only的原请求message ID仍标为未知，
不能把boosting上下文当作已配对的原RPC。寄存器采样和端口查询不原子；所有结果
只代表查询时的状态。元数据/owner分两条短行，最坏字段值下各小于224字节。

## 已查明的等待关系

| 采样位置 | 原生查询和符号化结果 |
| --- | --- |
| SpringBoard PID27 / tid310 | system-shell check-in栈，同步上下文当前接收者backboardd PID57 |
| backboardd PID57 / tid2192 | DataMigration同步调用，当前接收者datamigrator PID165 |
| datamigrator PID165 / tid1477 | 两次采样分别指向wrapper PID220、245 |
| locationd PID63 / tid1517 | BLSXPCBacklightProxy isAlwaysOnEnabled，同步上下文当前接收者SpringBoard PID27 |
| wrapper PID245 / tid2879 | _CLInternalPerformMigration；端口查询成功，但无直接任务接收者，PID=-1 |
| locationd PID63 / tid1481 | SEND message1203接收者launchd PID1，栈为CLMescalSigner/CLHarvesterService，不能当作BLS请求 |

更正此前的可能推断：com.apple.backlightd确实由backboardd承载，但不能据此识别
BLS调用；0474实际BLS等待指向SpringBoard。wrapper无直接任务接收者尚不能区分
in-transit、kernel/reply等状态。多个快照不证明同一事务依赖环。插件实例/PID变化
不证明前一插件成功或整轮迁移完成；第二次快照仍在CoreLocation迁移等待。

## 三层验证与保留的失败证据

- 主机：0353实际生产helper 204项/9个编译行为变异；0354为208项/11个变异。
  IPC锁、任务、调度与端口是明确模型，不能替代原生和实机证明。
- 原生：333 common+4 host、5,279源文件精确回放，构建前后相同；该TU无错误、
  27个未屏蔽警告；最终二进制调用原端口API已核对。相较0470只有观察器两文件
  不同，既有crypto/security代码未改；HID207、ABI和打包回读保持。
- 0472：USB在固定查询前失联，原日志封存，未通过诊断验收。末尾有按键保持记录，
  未据此断言唯一原因。0473复用同内核，加密回归通过，但缺同步上下文、长行截断，
  IPC明确partial/unaccepted。0474修复这些诊断缺口，并完成全部固定查询。
- 0474实机：旧A先UNWRAP，原负载/密文保持，锁定已有缓存/直接读写零传输拒绝；
  旧B/C/D、原securityd类6–11首次成功操作全UNWRAP；Root/nonce/bank/systembag、
  RTC/NVRAM/defaults保持。原watchdog聚合心跳至321.403秒，有限窗口，非长期稳定。

原生0474为18,947,872字节，SHA-256：

```text
d9b15aaa6696841b9a5928e43372dee1d19274febdf968b8f97f19508b0444a2
```

[0474验收](qualification/0474/0474-IPC-qualified.json)、
[解析结果](qualification/0474/0474-IPC-observations.json)、
[加密回归](qualification/0474/0474-A-qualified.json)、
[生产代码可重跑校验器](../scripts/tests/run-ipc-receiver.py)。
[0472不完整记录](qualification/0472/0472-incomplete-runtime.json)与
[0473部分验收](qualification/0473/0473-IPC-partial.json)按原结果保留。

后续检查CoreLocation等待端口的实际状态与资源/回复生命周期，再修真实缺失接口。
不跳迁移、不编造成功、不删库/重建key/降保护类。保留硬件密钥、加密Data和原版
MACF/ACL/Sandbox/watchdog。factory无PIN不等价SEP/口令；完整AKS、数据库、迁移、
桌面仍未完成。无新触摸、全盘扫描、Data fsck、物理120Hz或GPU/显示/输入扩展。
