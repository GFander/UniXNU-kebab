# 用户态崩溃排查：两项根因已验收，其余仍开放

最新状态：0382 正常启动固件已恢复并留在用户态。Cryptex 队列和独立动态库可见性两项修复通过实机回归；尚未解决全部 daemon 崩溃。

当前任务：读取实机运行日志，按共同根因修复 crashed/Failed；保留真实硬件密钥、加密 Data、原版 watchdog。暂不开发 GPU、显示和输入。不能把重复启动或消除日志视为完整 iOS 启动。

## 已定位和验证

- **0376：Cryptex 并发读队列修复。** 旧代码超过 32 个在途请求即返回 Busy；Apple IOStorageFamily 对该返回值直接完成失败，不会自动重试。正式补丁 0286 改为对有效请求分配真实描述符排队，保留内存分配/离线/后端错误、原 FIFO、引用和回调生命周期。实机跨过旧上限，峰值 38；完成 8192 次请求，失败 0。
- 对比 0375 早期日志的 disk3 Busy 13 次、存储启动失败 13 次、page-in 错误 7 次、缺失 hash record 4 次，0376 在约 20 分钟窗口四项均为 0。这是存储读取修复，不是其余服务均已修好。
- 264 个实际队列/worker/stop 主机用例通过 ASAN/UBSAN（IOKit/后端为显式替身）；两个 KDK 编译单元、完整构建通过。265 common + 4 host 补丁、5239 源文件逐一回读一致。
- 0376 原生内核 SHA256 `bce374874722d12f392d949aded751b12392c7ccf5938cab2e8607b3b29a16ff`，18849944 字节。运行日志 `boot123-20261029/0376-media-boot.log` SHA256 `632e18deb67dd924e3257d22b2d672995e664094566c1b9612c5468f2e97f274`；无内核 panic，但仍有真实用户态崩溃。

## 剩余根因清单

| 症状 | 实际证据 | 状态 |
| --- | --- | --- |
| tccd 反复退出 | SIGABRT，退出原因 25/0：`com.apple.tccd: profile not found`；原版 Sandbox 未列入 prelink 根 | 0377/0379 加载原版 Sandbox 后不再见该原因，但 watchdog 被原版服务类策略拒绝，两版均否决；正常版仍待接入 |
| navd/powerd/ReportCrash | objc fatal：无法 dlopen `/usr/lib/libobjc-trampolines.dylib` | 0378/0380/0382 已暴露真实只读原版库，原 dyld 和根身份不变；对应 dlopen 错误未再出现 |
| configd | SIGSEGV，地址 0x54；libdispatch async 路径，由 CFPreferences 同步回调进入 | 待进一步诊断真实空对象来源 |
| mediaserverd | SIGSEGV，地址 0；CoreFoundation retain / Fig 通知初始化 | 待进一步诊断 |
| locationd | SIGSEGV，地址 0x68；CoreFoundation RunLoop 相关路径 | 待进一步诊断 |
| lockdownd | 自身代码 main+0x4c1b4 的 BRK | 待分析原版可执行文件 |
| SpringBoard | 原版缓存地址 0x19b968ca8；`FBSDisplayMonitor _initWithBookendObserver:transformer:` +0x488 | 显示服务依赖，记录并暂缓开发 |
| backboardd | 初始化路径 SIGABRT/异常终止 | 尚未证明具体异常内容，不能直接认定已解决 |
| AppleKeyStore 17/102/103 | 原版客户端请求了尚未实现的选择器 | 保留真实不支持返回；不能用假成功替代硬件语义 |

`Too many corpses being created` 是许多进程失败后触发的崩溃快照资源限制，是次生现象。提高限制、禁止服务启动或吞掉错误都不能修复这些根因。

## 0377 初次候选记录（已被后续实测否决）

0377 已刷入 boot_b，Sending/Writing 均 OKAY；与 0376 使用同一个原生内核，仅追加原版 Sandbox/AppleMatch prelink 依赖，原版指令未修改。当前处于实机验证，不能提前认定策略加载成功。仍为正常 tool12 配置，不是 fsck 维护固件。

接下来先验证原版 Sandbox 注册及策略，再修复原版 Cryptex 中独立动态库的加载。维护后必须恢复正常启动固件；最终重复严格只读 Data fsck 和硬件密钥/加密文件/watchdog 回归。未开始 GPU、显示、输入；没有提交或推送 Git。

官方依据：[Apple IOStorageFamily](https://github.com/apple-oss-distributions/IOStorageFamily/blob/main/IOBlockStorageDriver.cpp)、[Apple objc trampoline](https://github.com/apple-oss-distributions/objc4/blob/objc4-876/runtime/objc-block-trampolines.mm)、[Apple dyld 1042](https://github.com/apple-oss-distributions/dyld/blob/dyld-1042.1/dyld/DyldProcessConfig.cpp)。

## 最终实机结果：两项根因修复验收，其余问题仍开放

- 正常版 **0378、0380** 分别完成默认资源生命周期、原版 MKB_INIT、硬件 nonce/Data Root、旧 C/D 明文及四条密文、原版 watchdog 心跳回归。观察均越过此前 defaults panic 时间；没有内核 panic。仍存在真实 daemon 崩溃，不能称完整 iOS 稳定启动。
- 正式补丁 **0287**：固定原版 SystemOS `/usr/lib` 使用原生 bindfs + MNT_UNION 只读暴露，逐一校验 objc trampoline、ffi trampoline、stdc++ 的真实 SHA/大小/只读来源；挂载后检查 backing vnode、原 System 根、原 dyld 原文件回退。无系统镜像写入；内核挂载参数只在 kernel context 与不可由用户指定的 MNTK_KERNEL_MOUNT 同时成立时按内核指针处理，普通用户 entitlement/copyinstr 分支不变。
- 46 项实际生产路径和挂载函数 ASAN/UBSAN 测试通过，VFS/IOKit 为明确替身；三 KDK 编译单元及完整构建通过。最终发布原生内核 **0378**，18850272 字节，SHA `44d08558b1dc461ab5c2f090f99b1359b629166cd69c42b06ea126058c2c3ee1`；266 common +4 host、5239 源文件一致。
- **0377、0379 均否决**：0379 的 0288 仅增加有界真实错误日志，不改变 MACF 返回值。原版 Sandbox 的 service-open 返回 EPERM：`watchdogd / KebabWatchdog / type1 / policy1`，随后原版 watchdogd IOServiceOpen 错误分支 BRK、关键进程退出引发 panic。原版 Sandbox 的 watchdogd 配置包含 `IOWatchdogUserClient` 和 `AppleARMWatchdogTimer`；我们的实际软件后端类为 `KebabWatchdog : IOWatchdog`。需要正确移植平台授权契约；未套用参考仓库按进程名跳过 MACF/filter 的办法，未假冒 Apple 硬件类、未关闭 watchdog。
- **0381** 原版签名 `fsck_apfs -n -l`：退出 0、Data OK、crypto-io enabled；严格 warning gate 通过，仅原有 APFS 版本/overallocation 两条，未修复或新增白名单。
- **0382** 已恢复为正常 tool12/ufsrw1/rootrw0 固件，保留两项修复，手机留在正常用户态，**未留在 fsck 或已否决的 Sandbox 固件**。Sending/Writing 均 OKAY；再见原版 MKB_INIT、C/D 密文、只读库来源校验及原版 watchdog 心跳。每天仍使用 owner_fast1，不做全镜像重复校验。

完整证据：`boot123-20261029/RUNTIME-CRASH-FIXES-ACCEPTED-0382.json`。该收据明确 `all_requested_errors_fixed=false`。0288 诊断源码保留在两台开发机和正式补丁队列（267+4），但交还的内核使用验收过的 0287 末态（266+4）。下一原生版本 0383，下一正式补丁 0289。

## Failed 字样的分类，不能统一屏蔽

0378 原始日志的 26 条去重失败行保存于 `0378-failure-lines.json`。

| 日志 | 含义/处理边界 |
| --- | --- |
| AES KAT unsupported/bounds，negative=0 | 错误输入拒绝自测通过，不能当作真实工作负载故障 |
| root hash / default snapshot / volume not sealed | 当前 owner 启动链缺少 Apple 原版封印/快照鉴权；RO 根启动通过不等于 Apple SSV 已接通 |
| no static trust cache at 默认文件路径 | 旧文件加载路径找不到；EFI 包仍含原版 trust 模块，不能仅据包存在推断这条 native 路径已生效；需要继续审计实际 AMFI/平台判定链 |
| checkOblitNVRAMkey gF 不存在 | 原版工具查询可选抹除状态键；日志自身提示键可能不存在，不应凭空伪造抹除状态 |
| multiuser/persona/nextsession/notify 注册失败 | 用户会话及通知依赖尚未完整接通，需要依实际 API/生命周期继续排查 |
| virtual random 加载失败 | 原版服务的私有加密/随机接口仍有缺口，不能用假成功代替 |
| APFS AKS new_key class2、class1 unavailable | 当前只验收 factory 无口令 C/D；真实 A/B 密钥策略、锁定与用户接口尚未实现，不可降级成 C/D 冒充通过 |
| Jetsam high watermark EINVAL | 原版 launchd 提交的内存策略接口不匹配，需追踪实际参数；与已修复的 Cryptex 读队列是不同问题 |

下一优先项是原版 Sandbox 与真实平台服务授权，之后按实际依赖继续 AKS/通知/进程故障。SpringBoard 的 FBSDisplayMonitor 断言已定位，仍遵守用户要求不开始 GPU、显示、输入开发。未提交/推送 Git。
