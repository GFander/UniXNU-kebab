# 0470–0471：真实 APFS A 类保护、锁定准入与重启恢复

2026-11-01。此前原版系统请求APFS class1时真实返回Unsupported；现已接通factory
无口令A类文件后端。原版APFS在锁定时拒绝已打开文件的缓存读、非缓存读和写入，
传输字节数为零；解锁恢复原内容，完整内核重启后解封同一旧密钥及原密文。
这不是完整AppleKeyStore、Apple SEP、锁屏口令或可用SpringBoard验收。

## 格式和生命周期

从既有硬件Root/已认证bank按独立Complete Protection用途派生A类密钥，绑定bankID、
容器/System和版本/策略，不重写DCB1或原随机C/D密钥。锁定、撤销及解锁广播失败时
擦除A类派生密钥；原版已授权设备解锁后才恢复相同材料。factory无PIN是限定策略，
不能冒充Apple UID/口令/SEP等价性。既有B KF03/HB01及C/D KF02/HT01不变。

持久KF04 inner88→AES-KW96认证UUID、revision、cryptoID、class1、FEK32/IV16。
内部HA01是header24+KW40，原APFS收到加密opaque64和wrapped标记，原生I/O短期
解封必须处于实际unlocked状态。A↔B/C/D重包封保留FEK/IV并绑定目标cryptoID。
没有明文回退、保护类降级、Root/bank重建或删库。

这遵循[苹果对Complete Protection锁定不可访问的语义](https://support.apple.com/en-ie/guide/security/secb010e978a/web)，
未声称实现其硬件细节。[上游AKS状态组件](https://github.com/UniXNU/AppleKeyStoreState)
作为状态/选择器边界参考，本机仍绑定20H392原版客户端ABI和真实硬件Root后端。

## 分层证据

| 层次 | 实际范围 |
| --- | --- |
| 生产核心主机 | 1,561项A/core检查，独立A/KF04/HA01向量，10个编译行为变异；原B/core 1,874项保持 |
| 原生块驱动主机 | 实际C核心与C++驱动/XTS/析构：A 41例+3变异，B 42例+3变异；独立4096B XTS和析构字节清除 |
| C/D兼容 | 原keychain 2,754项、file/metadata/target-id 1,953项通过；A未bootstrap从Unsupported改为NotReady，旧C/D向量不变 |
| 模型边界 | Root恢复、VFS广播、锁/RNG、bufattr是明确模型；APFS缓存拒绝没有用模型冒充成功 |
| 全量原生 | 331 common+4 host、5,278源文件精确回放且构建前后相同；7 TU、HID207、原MACF/PROTECT/AUDIT/ACL与ABI/打包回读通过 |
| 0470首次实机 | private A cryptoID259236真实65559B加密写读，锁定已有cached/nocache读和写均EPERM1、residual65559；closed重开和exclusive create均EPERM1，解锁解封原key并完整恢复 |
| 0471完整重启 | 同一259236首次操作UNWRAP，非NEW；两次完整负载SHA与两处原始密文条带不变；锁定/解锁语义再次通过 |
| 旧数据和服务 | 旧B245422/252640先unwrap，三份B负载/密文保持；原securityd6–11首次成功操作全unwrap；Root/nonce/MKB/systembag/C,D、RTC/NVRAM/defaults保持 |
| 原版监控 | watchdog聚合心跳分别至321.307/329.608秒，无panic或查询超时；有限窗口，非长期稳定性证明 |

固件只写已确认设备boot_b，两轮Sending/Writing均OKAY。新fixture只用固定私有路径；
原版系统也成功创建了A类文件密钥，但未据此宣称所有数据库/系统迁移完成。
核心和I/O临时FEK会擦除，IV/tweak仍在CPX；没有整机RAM/所有副本/物理取证清除证明。
没有新人工触摸、硬件120Hz、全盘System扫描、Data fsck或断电撕裂写验收。

原生0470/复用0471：18,947,880字节，SHA-256：

```text
c0a8b2f44a3eed4eb43f9658d8325d3377620266e4a7026c582855e7d63c8ee2
```

[首轮验收](qualification/0470/0470-A-qualified.json)、
[完整重启](qualification/0471/0471-A-qualified.json)、
[生产代码/源码/原生资料](qualification/0470/)及
[可重跑验证](../scripts/tests/run-class-a-backend.py)分别保存。

## 主线仍未完成

0470原版wrapper仍在CoreLocation迁移同步等待，locationd仍见BacklightServices
isAlwaysOnEnabled，backboardd仍见DataMigration同步请求。backlightd由backboardd
承载已核对，尚未证明这些等待的实际事务配对或唯一依赖环。
后续继续追实际IPC接收者、资源与生命周期，补实际缺失的原版密钥/数据库接口，完成
原版迁移及system-shell check-in。保留MACF/ACL/Sandbox、硬件Root及原版watchdog，
暂不扩展GPU/显示/输入；蓝牙延期至可用SpringBoard之后。
