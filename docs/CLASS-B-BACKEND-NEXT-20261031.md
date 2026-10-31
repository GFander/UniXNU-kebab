# 下一步：真实 class B 密钥与原版 APFS / AKS 生命周期

入口证据见 [0461–0462 实机数据库 I/O](LOCATION-DATABASE-IO-20261031.md)及
[0463 密码学基础与原版 AKS 调用](CLASS-B-CRYPTO-20261031.md)。
本页是未完成工程要求，不是功能验收。现有硬件 Root、持久 C/D bank、systembag、
所有旧加密 Data 与原始密文证明必须保留。

## 已确认与尚未确认

已确认 locationd 的 class2 journal 带偏移写入返回 EPERM，紧邻原版 APFS class2
new-key Unsupported；C 类数据库实际同步成功。当前本机文件回调、DCB1持久bank和
CPX句柄仅支持C/D，不能满足原版classB请求。原版SQLite重试位置仍见于实际线程栈。

0463 已确认原版 selector10/11 是 wrap/unwrap，13 是 lock bag，核对了包装器字段；
实测 securityd 的 bag0、类值7/9/10/11请求被当前非管理角色拒绝。X25519/ConcatKDF
基础及原生 KAT 通过，但未形成文件或 keychain 服务。尚未确认所有 B 类公钥/密钥包请求、
原版 APFS 的 wrapped 格式/大小、完整 open/close/lock 接口及 keychain 类别映射和授权链。
这些必须继续按 20H392 供体核对，不能把不同接口的类别编号混用。
不能将相邻日志当作已经取得同一IPC transaction或所有内核调用的完整因果栈。

## 实现要求

[Apple 的 Data Protection classes](https://support.apple.com/en-ca/guide/security/secb010e978a/web)
将 classB 定义为 Protected Unless Open：每文件临时公钥参与 Curve25519/ECDH，
配合 SHA-256 Concatenation KDF 保护文件密钥；关闭后清除相应文件密钥，再打开需要
类私钥。这说明仅把 class2 加进 C/D 对称分支不能实现相同语义。

1. 固定供体ABI：确认文件密钥/public-key与AKS用户态接口的字段、长度、权限、原错误和
   生命周期，包装器 ABI 已知的 selector10/11 仍需核对完整提供者与权限流程；未知请求继续拒绝。
   原版 securityd 的 keystore entitlement 已静态取得，但不得按进程名直接授予管理权，
   也不能仅放开角色却没有真实硬件支撑的 wrap/unwrap 服务。
2. 硬件支撑的B类密钥：使用既有硬件Root的独立用途隔离与可认证持久身份；不能复用
   C/D类钥、把普通软件常量当设备密钥、覆盖旧bank或静默重建认证失败的记录。
   新增持久能力必须带明确版本和旧记录兼容策略，旧C/D字节/密文保持。
3. 真正的B类文件格式与密码学：按实际ABI实现临时公钥、共享密钥/KDF、认证包封及
   错误拒绝；使用被核对的上游实现和独立已知答案，不凭“能解回自己生成的数据”验收。
4. CPX与打开文件生命周期：区分已打开文件的I/O句柄和重新打开所需的类私钥；落实
   close、lock、unlock、复位、失败撤销与清理。不能用一个永远可解的stateless C/D token
   冒充B类open/close保护，也不能伪造锁状态或绕过原版MKB事件。
5. 原版用户态互通：按实际调用接通所需AKS公钥/状态/密钥服务，保留root/entitlement、
   客户端角色和精确卷身份检查；不只给内核私有测试开旁路。

## 完成条件

- 独立密码学向量、篡改/错误身份/重放边界、权限拒绝和资源/锁生命周期测试通过；
  源码逐文件回放、实际原生构建与供体ABI通过。
- 原版APFS在真实Data上创建classB文件、写入、同步、关闭/重新打开和复位后恢复；
  明文正确、磁盘保留密文；锁/解锁和已打开文件行为与声明范围一致，错误不吞掉。
- 原locationd journal真实写入成功，数据库重试工作实际向前，原版DataMigration和
  system-shell登记用对应进度证明，不以“crashed少了”或PID存在替代。
- 原硬件Root/nonce、旧C/D与原始密文、原MKB/systembag、MACF/ACL/Sandbox、
  defaults/RTC/NVRAM与原版watchdog监控完整回归。
- 若仅完成factory无口令子范围，必须明确不具备完整A类、口令、SEP或keychain能力；
  不把本工程小目标声明为完整SpringBoard目标完成。

蓝牙仍延期；本问题修复前不通过跳过locationd迁移或伪造B类可用性推进桌面。
