# 0464–0465：原版 keychain C/D 包封、解封和重启恢复

2026-10-31。修复原版 securityd 被状态读取角色拒绝、后端缺少包封/解封的问题。
现在原版用户态能够对 keychain 类别 **7/8/10/11** 的 32 字节 AES 密钥执行真实
包封/解封，并在完整内核重启后恢复先前记录。**A/AKU、B 类文件、完整 keychain、
系统迁移和桌面登记仍未完成**，不是全部密钥服务或数据库问题的验收。

## 原版合约与权限

只读核对固定 20H392 securityd：SHA-256
`844d488d12708ba540b1ab3fd47455ee7f863270e2feda062cf8c787f9381693`。
原版转换函数 `0x1000edaf4` 的 CFEqual 导入、分支和立即返回值逐项检查，得到：

| keychain class | 原版保护级别 | 本次范围 |
| --- | --- | --- |
| 6 / AK | WhenUnlocked | 尚未实现相应锁生命周期，继续拒绝 |
| 7 / CK | AfterFirstUnlock | 真实包封/解封及重启恢复 |
| 8 / DK | AlwaysPrivate | 真实包封/解封及重启恢复 |
| 9 / AKU | WhenUnlockedThisDeviceOnly | 尚未实现相应锁生命周期，继续拒绝 |
| 10 / CKU | AfterFirstUnlockThisDeviceOnly | 真实包封/解封及重启恢复 |
| 11 / DKU | AlwaysThisDeviceOnlyPrivate | 真实包封/解封及重启恢复 |
| 12 / AKPU | WhenPasscodeSetThisDeviceOnly | 无口令 factory 策略不提供该能力 |

这些编号不是 APFS 文件保护类编号。原版 AppleKeyStore 在 wrap/unwrap 路径检查
客户端 `+0xf4` 的 bit3，初始化时该位来自
`com.apple.keystore.access-keychain-keys` entitlement。新客户端使用实际 AMFI
`copyClientEntitlement` 的严格 `kOSBooleanTrue`，并平衡返回对象引用。
该 capability 在创建连接时固定，仅允许 selector10/11；root 身份不自动授予，
也不提升为 bank/卷映射/通知管理权限。状态读取与原有管理角色分离继续保留。

见[原版类别证据](qualification/0464/original-keychain-class-mapping-0464.json)、
[权限窗口摘要](qualification/0464/original-provider-permission-summary-0464.json)和
[原版包装器 ABI](qualification/0463/original-aks-wrap-contract-summary-0463.json)。
未独立读取运行中 securityd 的可执行 UUID，也未新增 Apple 签名认证声明。
类别对应通过原版代码核对；[固定 Apple Security 源码](qualification/0464/official-security-source-pins.json)
用于核对 metadata key 的包封、数据库存储和读取验证流程。

## 真实硬件 Root 后端

新 `kebab_keychain_key_crypt` 与 APFS 文件密钥接口分开。只接受 bag0、已覆盖类别、
32 字节明文密钥/40 字节 RFC3394 包封；其他长度、bag、类别和 generation 请求真实拒绝。
KEK 由已恢复的硬件 Root 下独立用途的 HMAC 派生，并绑定已认证 bank ID、
container/System 身份、记录版本/策略和精确 keychain class。没有复用 C/D 类钥，
没有改动或替换既有 4 KiB bank，也没有新建软件 Root 或记录 plaintext 密钥。
原版用户态持久化其包封记录；本接口不自行改写 keychain 数据库。

RFC3394 ciphertext 是本后端的 opaque 格式，不冒充 Apple SEP blob；数据运算使用
已有原生密码学实现，不声称每次操作在 TEE 中完成或使用 UFS ICE。临时 KEK、派生
上下文和输出暂存清除；未记录密钥/明文。失败保留输出，描述符短写返回真实 I/O 错误；
不声称描述符发生部分短写时具有原子性。bank 撤销或认证失败后拒绝，重新认证旧记录后恢复。

## 验证结果

| 层次 | 证据与结果 |
| --- | --- |
| 核心主机测试 | 2,754 项断言；实际 bank 创建/认证/撤销与 AES/HMAC 代码，4 组独立 HMAC/RFC3394 向量；密文逐 bit 篡改、交叉 class/Root/bank/卷身份、AES 故障、上下文/别名/长度与输出保持；9 个编译成功的行为变异被拒绝 |
| 客户端主机测试 | 完整客户端 16,956 项检查，保留旧接口案例并更新两项真实权限语义；严格布尔 entitlement、原角色隔离、引用释放、封送、短写和后端错误；9 个行为变异被拒绝 |
| 模型边界 | 主机 Root 恢复、锁、执行上下文、RNG、无关 CPX teardown，以及客户端 IOKit/AMFI/crypto 是明确模型；它们不代替实机证明 |
| 原生构建 | C/C++ 真实编译单元和全量构建；328 common +4 host/5,268 文件完全回放；207 项 HID 虚表、prelink/存储/watchdog ABI、owner 策略及镜像回读通过 |
| 0464 实机 | 四类别原版 wrap/unwrap 成功，securityd 有 entitlement 且非管理角色；原硬件 Root/nonce、systembag/MKB、旧 C/D 冷读及密文、RTC/NVRAM/defaults 保留，原版 watchdog 至 321.332 秒 |
| 0465 完整重启 | 复用同一原生内核，四类别第一次成功操作均为 unwrap，先于该类别任何新 wrap；重新启动的 securityd 从持久记录恢复。相同回归及原版 watchdog 至 321.271 秒 |

正常 FASTBOOT 前实际 VFS/UFS 同步、PON reason 与 IMEM 回读通过；两次 boot_b
Sending/Writing 均 OKAY。这里没有物理断电、断电撕裂写、用户口令或多用户验收。
首次原生检查发现该编译单元使用 raw `OSPtr` 兼容 ABI，已修正引用释放、重跑客户端
检查并完成构建；最终 C/C++ 语法检查分别有 70/14 条非致命警告，未关闭警告掩盖。

0464/0465 复用内核 SHA-256：

```text
67cb6e899b8c1e145ce4b185249aca31181a5701fd0656b6b57f75b3a9a7bbc1
```

[0464 收据](qualification/0464/0464-qualified-runtime.json)、
[0465 重启收据](qualification/0465/0465-qualified-runtime.json)、
[完整构建](qualification/0464/native-ios-identity-build-0464.json)及
[公开可重跑工具](../scripts/tests/run-keychain-cd.py)分别保存。
公共工具校验实际生产代码、父版本反向还原、18 个行为变异和封存日志；不操作手机。

## 仍需修复

6/9 类请求仍 Unsupported；不能降成 C/D。原版 locationd 的 B 类 journal 仍
EPERM，紧邻 APFS class2 new-key Unsupported；SQLite 重试没有被隐藏。
下一步实现真实 AK/AKU 锁状态与 B 类公钥/包封、CPX/open/close/lock 生命周期，
再以原版数据库写入、迁移与 system-shell check-in 的实际进展验收。

旧 C/D bank/密钥/密文、MACF/ACL/Sandbox 和原版 watchdog 保留。没有格式化、
删库或额外全盘 verify/fsck；最近 Data 只读 fsck 0416、完整 System/SystemOS 检查 0341。
触摸仍以 0459 人工双指验收为准；可用 SpringBoard、GPU 呈现和物理 120 Hz 未完成，蓝牙延期。
