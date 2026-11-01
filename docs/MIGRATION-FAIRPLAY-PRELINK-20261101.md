# 0480 原版 FairPlay 组件装载与初始化边界

2026-11-01。0480 复用完整构建的 0479 native/source，只新增受控的原版 FairPlayIOKit 和 IOTextEncryptionFamily 闭包；没有修改原版组件的可执行字节。

实机首次原版 daemon 查询对应类、取得原迭代器及对象，并以 type 0 成功打开原用户客户端。随后调用 AES selector 2201、128 bit、encrypt、32 bytes，当前 NativeAES 返回不支持，原组件初始化失败并退出。原包装器在非零结果后设置 -22，再把这个值打印；它不是期待 -22 的非法参数自测。不能把更少日志、成功 open 或改返回码当成功初始化。

本次封存日志 SHA-256 为 `07b26e86091f38e1634ec61d2cda1b355a089864cd1dd5c0a48aa93ade4d53f6`。原 A/B/C/D 负载与旧密文、原版 keychain 六类冷恢复、硬件 Root/nonce/bank/systembag、RTC/NVRAM/defaults 与原版 watchdog 314.209 秒回归通过。迁移、system-shell check-in 和可用 SpringBoard 未完成。

## 装载器范围

新增显式 opt-in profile，保留默认严格拒绝路径及原 0479 image/report 的逐字节身份。34 处符号加数通过明确原符号别名和原 addend 重绑；120 处原 word 连同前序 BR/B 固定哈希，逐条消费，其他未知编码仍拒绝。已知静态分支、记录指针及符号入口受检查；完整间接控制流、寄存器数据流和整个闭源组件 ABI 没有被证明。

实际原/native IORegistryEntry、IOService、IOUserClient 大小及 76/169/187 个虚表项目核对通过，包括原无符号 D1 tombstone 与 native 有界纯 B 尾跳的对应。此结构证据不代替功能测试。20 组主机测试及 12 个 Python 行为变异只用合成字节和映射；真实供体 emission 与实机证据分别记录。

## 证据

- [封存实机收据](qualification/0480/0480-fairplay-prelink-qualified.json)、[原始日志](qualification/0480/0480-media-boot.log)。
- [真实输入 emission 和旧路径身份](qualification/0480/real-input-validation-v2.json)、[基类 ABI](qualification/0480/fairplay-base-abi-qualified-0480.json)。
- [主机测试](qualification/0480/profile-test-result-0480.json)、[复用原生构建声明](qualification/0480/0480-native-alias.json)、[原生构建源证据](qualification/0479/native-ios-identity-build-0479.json)。
- [加密回归](qualification/0480/0480-A-qualified.json)、[systembag](qualification/0480/0480-systembag-qualified.json)、[defaults/watchdog](qualification/0480/0480-defaults-qualified.json)。

下一步按固定供体确认 selector 2201 的独立派生语义，沿用既有硬件封装 Root 并保持 nonce/旧 Data 不变，再做原生及实机验证。不得映射为 nonce key、假装 Apple UID/SEP 或伪造成功。当前未扩展 GPU/显示/输入，蓝牙继续延期。
