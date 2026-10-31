# 0468–0469：B 类文件密钥缓存硬化与重启恢复

2026-11-01。在[0466–0467真实B存储与数据库进展](CLASS-B-STORAGE-20261101.md)
基础上，避免继续把明文32字节文件密钥交给原版APFS长期缓存。
0468改为内部加密HB01句柄，0469完整内核重启恢复新旧B文件及六类旧keychain记录。
既有硬件Root、bank/systembag、KF03持久格式及C/D文件编码不变，没有重建旧密钥。

## 真实行为及边界

HB01由24字节头和40字节AES-KW密文构成。独立HMAC用途绑定已有硬件Root、bank、
容器/System、版本/策略与头部。原版APFS收到opaque64和wrapped标记；原生块I/O
桥接按需解封至短期对象，析构擦除key/unit/active，AES上下文同样清除。
明文FEK也会在密钥核心的必要临时变量中出现并清除，不能说它只出现在I/O对象中。
APFS仍缓存IV/tweak元数据；没有对整机RAM、所有副本或物理取证擦除作证明。

B类的已打开文件在锁定时必须继续工作，因此HB01并非锁定后完全不可解封的令牌；
真实原版APFS负责关闭后的描述符准入，持久KF03解封需要B私钥的解锁状态。
Root/bank撤销仍拒绝I/O，不提供明文回退。这不是Apple SEP、硬件AES或口令保护。

| 层次 | 证据 |
| --- | --- |
| 实际生产代码主机测试 | B/core 1,874项及12个编译行为变异；真实C核心+C++块驱动/XTS/析构42例及3个变异；独立4096字节XTS向量 |
| 兼容与模型 | 原C/D keychain 2,754项、file/metadata/target-id 1,953项；Root恢复、VFS/锁/RNG、bufattr为明确模型，未假冒原APFS准入成功 |
| 全量原生构建 | 330 common +4 host、5,275源文件相同，7单元检查、全量构建前后源相同，HID207及相关ABI/镜像回读通过 |
| 0468首次实机 | 旧B245422经新句柄恢复；锁定已有I/O成功、关闭后reopen=EPERM；锁定新建252640，真实写读/fsync/close成功，关闭拒绝、解锁恢复 |
| 0469完整重启 | 旧245422与新252640先UNWRAP再使用，无新建替代；三份65559字节负载及密文条带与前轮一致；类别6–11旧keychain首次成功操作全UNWRAP |
| 原版数据库与回归 | 两轮locationd旧B数据库/WAL/SHM class2查询成功；没有声称重新产生journal写入。旧Root/nonce/MKB/systembag/C,D密文、RTC/NVRAM/defaults保持，原版watchdog至321.483/324.294秒 |

两轮均只写确认设备boot_b，Sending/Writing均OKAY；日志已关闭封存。
观察到CPX free64，但未把其地址独立匹配到特定B文件，不据此宣称该文件回收完成。
关闭时明文FEK不再交给APFS是源码/编译测试与真实opaque路径共同证明的表示改变，
并非全RAM擦除证明。原客户端未变，保留0466的客户端17,130项/7变异证据。

原生0468/复用0469：18,980,208字节，SHA-256：

```text
1a22ba8dcf5bd950b9a27c71d07d8165cc3990311845cd1e51840670d6a5c5f1
```

[首轮实机](qualification/0468/0468-B-CPX-qualified.json)、
[完整重启](qualification/0469/0469-B-CPX-qualified.json)、
[源码/原生/主机输入](qualification/0468/)及
[可重跑测试](../scripts/tests/run-class-b-cpx.py)分层保存。

## 仍未完成

APFS class1仍真实返回Unsupported。migrationpluginwrapper仍在CoreLocation迁移调用
同步等待，locationd采样包含BacklightServices isAlwaysOnEnabled，backboardd采样包含
DataMigration同步请求。原版plist确认com.apple.backlightd由backboardd承载；尚未证明
这些等待构成同一事务的依赖环或唯一原因。系统迁移及system-shell check-in仍未完成。
继续补实际密钥/数据库依赖并追踪真实IPC；不跳迁移、不抹库、不降低保护、不停止原版
watchdog。GPU/显示/输入暂不扩展，蓝牙延期。
