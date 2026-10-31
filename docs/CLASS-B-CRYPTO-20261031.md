# 0463：B 类密码学基础与原版 AKS 调用取证

2026-10-31。当前正常实机内核 0463；本轮完成 X25519/ConcatKDF 基础、
原生已知答案自检和原版调用元数据取证，**尚未完成 B 类文件服务或 keychain**。
[上一轮实际 journal 失败](LOCATION-DATABASE-IO-20261031.md)仍然存在。

## 源码与构建

新增补丁 `0348-kebab-qualify-classB-crypto-and-original-wrap-metadata.patch`。
327 个共同补丁 + 4 个主机补丁，5,266 个源码文件与 Mac 编译输入逐项一致。
相关编译单元、完整 DEVELOPMENT ARM64 构建、原版组件 ABI、207 项 HID 虚表、
prelink 和镜像回读通过。原生内核 18,961,928 字节，SHA-256：

```text
469ab3317049befa87da2a3fa84aa322d58379a997cbee72a9e2c7c79bc06545
```

代码源自 [BearSSL 0.6](https://www.bearssl.org/bearssl-0.6.tar.gz) 的 m31 Curve25519
与 constant-time copy；归档和成员哈希见 [upstream.json](qualification/0463/upstream.json)。
保留 MIT 许可，额外清除临时标量/域数组，限定符号名称。原版许可中的 8 行尾空格保持
字节不变。内核不包含主机 wipe 观察钩子。实际 ARM64 组件编译有 6 条非致命警告，
来自既有头文件、上游字节转换和 C99 风格；未通过关闭警告掩盖。

ConcatKDF 使用 SHA-256，输入为 `BE32(1)||Z||ephemeral_public||class_public`，
各字段固定 32 字节。此处是本后端的密码学基础格式，不声称等于 Apple SEP 包格式。
低阶点/零共享密钥拒绝，失败保持调用者输出，临时数据清除，支持已覆盖的重叠输入。
参考 [RFC 7748](https://www.rfc-editor.org/rfc/rfc7748.html) 与
[NIST SP 800-56A Rev.2](https://nvlpubs.nist.gov/nistpubs/SpecialPublications/NIST.SP.800-56Ar2.pdf)。
未做本设备密码学时序、侧信道、物理攻击或认证资质验收。

## 三层证据

| 层次 | 实际结果 | 边界 |
| --- | --- | --- |
| 主机密码学 | 514 组独立 X25519、514 组独立 KDF，RFC 1,000 次迭代；高位、重叠、低阶点、失败输出和清除检查通过；9 个编译成功的行为变异均被拒绝 | 固定公开测试标量；OpenSSL/独立向量不代替原版 AKS 互通 |
| 完整生产集成 | 完整客户端原有 16,572 项选择器/权限/封送测试保留；元数据上限 16；完整运行时自检的成功、失败、无提供者和并发状态通过 | IOKit/锁/根后端是明确模型，未修改授权角色 |
| 实机 | 一次原生 X25519/ConcatKDF KAT PASS，使用实际内核哈希提供者；原硬件 Root/nonce、systembag/MKB、旧 C/D 冷读/密文、RTC/NVRAM/defaults 与原版 watchdog 回归通过，监控至 321.169 秒 | 有限窗口；无新的人工触摸、完整内容扫描或 Data fsck；上次触摸验收仍为 0459 |

[源码回放](qualification/0463/public-all-source-replay-0463.json)、
[完整构建](qualification/0463/native-ios-identity-build-0463.json)、
[主机密码学](qualification/0463/test-x25519-component-result-0463.json)、
[生产集成](qualification/0463/test-class-b-integration-0463.json)、
[实机收据](qualification/0463/0463-qualified-runtime.json)分别保存。

## 原版 AKS 的新事实

固定 20H392 共享缓存和 kernelcache，只读核对原版包装器及其 IOConnectCallMethod 跳板。
[精简 ABI 收据](qualification/0463/original-aks-wrap-contract-summary-0463.json)
保留身份、入口与代码窗口哈希，不发布原版二进制或整段反汇编。

| selector | 原版包装器合约 |
| --- | --- |
| 10 | `aks_wrap_key`：2 个输入标量为有符号 bag handle、key class；原始密钥输入、调用者 wrapped 输出容量、1 个返回 class 标量 |
| 11 | `aks_unwrap_key`：相同 2 个输入标量；wrapped 输入、原始密钥输出，无标量输出 |
| 13 | `aks_lock_bag`：1 个有符号 bag handle 输入 |

因此 selector10 不是 B 类公钥查询。完整内核提供者流程、类别映射和权限链仍须核对。
真实 0463 捕获 16 次 selector10：PID102、bag0、类值 7/9/10/11，
`administrative=0`，全部返回 `0xe00002c1`（NotPrivileged）。执行序列将 PID102
对应到原版 securityd，但未独立读取该运行进程的可执行 UUID。
不能把这些 keychain 类值直接当作 APFS 的文件保护类 2/3/4。

原版 securityd 文件具有 `com.apple.keystore.access-keychain-keys=true`；
原版 AppleKeyStore 存在该 entitlement 的检查路径。见
[原文件身份摘要](qualification/0463/original-securityd-identity-summary-0463.json)和
[原版引用摘要](qualification/0463/original-keystore-entitlement-references-0463.json)。
本次静态提取未独立验证 Apple 代码签名，也未证明完整授权流程。当前本机客户端
将非管理用户限制为状态读取，合法 keychain 调用因此仍被拒绝。本轮没有放开权限。

## 下一步与不变项

按[真正 B 类与 AKS 生命周期要求](CLASS-B-BACKEND-NEXT-20261031.md)继续核对类别、
原版封送和授权，开发硬件 Root 支撑、用途隔离的真实包封/解封及 B 类文件生命周期。
不能仅按进程名称放行、改错误码、将 B 降成 C/D 或删除原版数据库。

当前 C/D bank、旧密钥/密文、MACF/ACL/Sandbox 和原版 watchdog 保留。
原版 locationd journal 仍 EPERM；迁移、system-shell check-in、A650/Metal、真实呈现、
物理 120 Hz 与可用 SpringBoard 未完成；蓝牙继续延期。
