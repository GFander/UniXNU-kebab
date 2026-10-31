# 0466–0467：真实 B 类存储、原版数据库进展与 keychain 重启恢复

2026-11-01。原版 locationd 的 B 类数据库 journal 曾因 class2 密钥后端缺失
返回 EPERM；0466 首次完成真实 B 类文件加密 I/O，原版数据库和 journal 的
fsync、WAL 同步屏障成功。0467 完整内核重启后，原版 APFS 解封旧 B 文件，
原版 securityd 也恢复类别6–11的旧 keychain 记录。
**关闭后内存中的明文文件密钥清除尚未证明，迁移和桌面登记仍未完成。**

## 后端、权限与格式

- B 私钥/公钥从现有已认证硬件 Root/bank 下的独立用途派生，绑定 bank ID、
  container/System、版本和 factory 无口令策略。锁定清除缓存私钥，公钥仍可创建文件；
  重新认证 bank 不自动解锁。现有4 KiB bank、随机 C/D 类钥和旧密文不变。
- 每文件 KF03 为 ephemeral Curve25519 公钥32 + AES-KW96；内部88字节严格绑定
  UUID、revision、crypto_id、class2、FEK32及IV16。使用已验收 X25519、
  SHA256 ConcatKDF；算法参照 [Apple Data Protection classes](https://support.apple.com/en-ie/guide/security/secb010e978a/web)。
  这是本后端格式，不冒充 Apple SEP blob、用户口令支持或硬件 AES/ICE 运算。
- 原版 bag0 selector12/13 的空秘密解锁/锁定接通，要求 AMFI 严格布尔
  `com.apple.keystore.device`；与 keychain 和管理权限分离，root 不自动获得。
  空 NSData 允许非空地址但长度必须0，从不读取任意用户秘密指针。
  selector7 根据实际状态返回4/5；keychain AK/AKU6/9遵守同一锁状态，12仍拒绝。
- 使用原生 `cp_key_store_action_for_volume` 广播。原始回调忽略各文件系统 ioctl
  结果，广播返回0不等于每个文件系统确认；以下原版文件行为才是实测依据。

## 三层证据

| 层次 | 实际结果 |
| --- | --- |
| 主机实际生产代码 | B/bank/AES/EC后端1,286项、完整客户端17,130项、关闭策略54项；22个实际编译行为变异被拒绝 |
| C/D兼容 | 原 keychain 2,754项独立向量/故障、file/metadata/target-id 1,953项通过；历史未bootstrap的B拒绝原因由Unsupported变为NotReady，C/D向量保持 |
| 模型边界 | Root恢复、VFS/IOKit/AMFI、锁/RNG/原生CPX析构是各测试明确的主机模型，不能代替实机 |
| 实际构建 | 329common+4host/5,274源文件精确回放，7单元原生检查和全量构建，构建前后源文件相同，HID207、存储/watchdog/AKS/prelink/owner与镜像回读通过 |
| 最终二进制 | 实查vn_close到关闭钩子、CPX free/flush到观察器的直接调用；MACF/PROTECT/AUDIT保留；初次缺原生头文件的编译错误已修复，不关闭警告掩盖 |
| 0466固定私有文件 | 真实65559B加密写读，锁定时已打开文件继续读写；锁定时公钥创建新B文件；关闭后两文件原生reopen均EPERM；解锁后完整原负载恢复 |
| 0466原版数据库 | locationd的B数据库与journal实际fsync成功，B WAL实际fcntl85屏障成功；此前B journal write EPERM未再观察到。成功write不逐次记录，不把未记录的返回值伪称成功字节数 |
| 0467完整重启 | 旧B文件crypto_id245422/245423首次操作均unwrap，早于任何新key；无重建，完整负载与密文条带保持。原版locationd打开既有B db/WAL/SHM并报告class2；没有声称该轮重新产生journal写入 |
| 原版keychain | 0466原版AK6 wrap/unwrap与AKU9 wrap成功；0467类别6/7/8/9/10/11首次成功操作全为unwrap，证明读取了旧持久记录 |
| 回归窗口 | 两次旧硬件Root/nonce、MKB/systembag、C/D冷读和原密文、RTC/NVRAM/defaults保持，原版watchdog心跳分别至321.548/321.640秒，无panic或查询超时 |

两次只刷确认设备的boot_b，Sending/Writing均OKAY；正常FASTBOOT前VFS/UFS同步、
PON04和IMEM回读通过。未格式化、删除数据库/journal、重建旧密钥或降保护类别。
没有新增人工触摸、物理120Hz、断电撕裂写、全量System扫描或Data fsck验收。

原生0466/复用启动0467：18,980,056字节，SHA-256：

```text
8742415ff15a01bd14a15cbc3c239799a032086023dc030013fa7c7383d08593
```

[0466实机证据](qualification/0466/0466-class-b-functional.json)、
[0467完整重启](qualification/0467/0467-class-b-functional.json)、
[源码/主机/原生资料](qualification/0466/)和
[可重跑校验器](../scripts/tests/run-class-b-backend.py)分开保存。

## 尚未完成与下一步

当前候选给APFS的CPX仍是raw32 FEK。`vnode_recycle`已请求，但封存日志没有
CPX销毁事件，同一次启动中解锁后的重开也未触发B unwrap；仅凭关闭时拒绝打开，
不能证明APFS缓存已清除明文密钥。原APFS回收调用图只是有界直接调用的过近似，
间接调用和分支可行性未完整证明。

下一步将CPX表示硬化为内部加密句柄，避免把明文FEK长期交给APFS缓存；必须再次
验证原版锁定准入、已打开I/O、公开密钥创建、关闭拒绝、解锁、冷恢复及C/D兼容。
此设计候选尚未构建/刷机，不算本里程碑的已完成功能。

原版migrationpluginwrapper仍在CLInternalPerformMigration同步等待；locationd已越过
此前SQLite初始化重试位置，但采样仍包含BacklightServices的isAlwaysOnEnabled等待。
不能把它推导为已完成迁移或已证明唯一IPC阻塞对象。APFS class1请求仍Unsupported，
其他真实密钥接口/数据库依赖仍需逐项处理。保持原MACF/ACL/Sandbox和watchdog；
暂不扩展GPU/显示/输入，蓝牙仍延期至可用SpringBoard以后。
