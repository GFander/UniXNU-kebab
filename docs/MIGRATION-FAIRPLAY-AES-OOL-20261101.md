# 0481–0482：FairPlay 设备子密钥与迁移 OOL 生命周期

2026-11-01。原版 FairPlayIOKit 装载之后，下一处 2201 AES 错误已经取得真实成功调用；原迁移进程暴露的 IOSurface OOL panic 也已修复并完成有限窗口回归。**完整 FairPlay、迁移、system-shell 登记和可用 SpringBoard 仍未验收。**

## 原版语义与密钥边界

原版 FairPlayCryptoUtilities.cpp:233 在 AES 非零返回后包装 -22，再打印错误值；它不期待后端返回 -22。固定供体表给出 selector2201/base2000，与2212 nonce 使用不同的公开种子，派生出128-bit目的子密钥。0360 保留旧 deriveNonceKey 源体，使用原有真实硬件封装 Root 进行相同的一块零 IV CBC 派生，两个目的密钥分别保管；准备成功后一起发布，取消/复位均清除。没有重新生成 Root/bank、复用 nonce key 或为未知 selector 返回成功。

这是 HiOS 设备根和软件 CBC 后端，不冒充 Apple fused UID、GID、SEP、口令保护或硬件 AES 引擎。0482 原版路径实际完成 selector2201/encrypt/32bytes/result0，旧233断言未出现；这不等于所有加解密、所有 FairPlay 请求或初始化都已完成。

## 真实失败与修复

0481 的2201已经成功，但116.296秒 pid247 com.apple.migrat 在 IOSurfaceRootUserClient::externalMethod -> writeBytes -> getPhysicalSegment -> dmaCommandOperation panic。该轮明确未通过300秒回归，原始失败记录保留。

Apple 的[固定版本 IOMemoryDescriptor 接口契约](https://github.com/apple-oss-distributions/xnu/blob/xnu-8796.141.3/iokit/IOKit/IOMemoryDescriptor.h)要求 readBytes/writeBytes 的内存已经 prepare，并具有对应方向。0361 在四个既有传输点加入配对 prepare/complete：prepare失败不访问，成功prepare在短传输和成功路径均complete，真实失败透传，成功才公布输出长度。既有 IOGMD panic 检查保持，没有通过移除检查或伪造物理地址绕过。

## 分层验证

- AES：61项实际生产 C++ 主机检查、10个编译行为变异；底层 AES 使用 OpenSSL 适配，IOKit/cache 为模型。原 nonce KDF 字节身份受检查。
- OOL：3个实际生产函数、24项检查、8个编译行为变异；IOKit memory 为模型，全 create/valuecache 集成不能由主机模型代替。
- 原生：两次相应 TU 均0 error/12 warnings；0482完整构建，340common+4host/5280源码与postbuild一致。保护/MACF/ACL、旧B直接调用边、原组件文字段及三组基类ABI保持。
- 实机：0482固定诊断完整、watchdog 321.668 秒，旧A/B/C/D原始密文与负载、原版keychain六类冷恢复、Root/nonce/bank/systembag/RTC/NVRAM/defaults保持。该窗口未再出现0481的IOGMD panic；不声称四个OOL分支均独立实测或完整并发/内存擦除证明。

## 证据和后续

[0482封存实机收据](qualification/0482/0482-fairplay-aes-ool-qualified.json)、[日志](qualification/0482/0482-media-boot.log)、[旧加密回归](qualification/0482/0482-A-qualified.json)、[源码回放](qualification/0482/all-source-replay-0482.json)、[真实原生构建](qualification/0482/native-ios-identity-build-0482.json)。

[0481失败观察](qualification/0482/0481-fairplay-aes-observation.json)和[失败日志](qualification/0482/0481-media-boot.log)保持，不能改写成成功。主机自测和变异收据分别为[AES](qualification/0482/test-fairplay-aes-result-0481.json)、[OOL](qualification/0482/test-surface-ool-result-0482.json)。

原版 FairPlay 继续查询平台属性，迁移及 system-shell 仍需要实际请求/资源/生命周期诊断；32条资源预算不是全调用覆盖，也没有证明唯一等待因果。不随意补身份值、不跳迁移、不停原服务，不扩展GPU/显示/输入；蓝牙继续延期。
