# 硬件 nonce、APFS 主线与 SMP / 内存历史验收

本页在 2026-10-31 整理发布此前已完成的 0243、0247 和 0274 记录，不是本次重新刷机或新完成这些功能。当前功能基线为 [0460](STATUS.md)。本页对应原始收据保留当时版本、失败及局限，不改写为“当前状态”。

## 0243：硬件封装 Root 与 Image4 nonce 2212

Qualcomm QSEE/Keymaster 时间回调、硬件封装 keyblob 的既有 Preboot 记录、同根复位恢复/HMAC 见证、2212 专用 KDF、正反 CBC 与负例拒绝均已实测。原版 Image4 ignition 请求返回 0 / 132 字节，PID1 继续进入 preboot。当时接下来缺 fstab 与 dyld/libSystem；这些是历史阻塞，不是当前结论。

[原始验收收据](qualification/platform-history/B2-ACCEPTANCE-0243.json)包含固定原生/载荷哈希、14 个原版组件文字段哈希、默认关闭回归及明确的非 Apple UID/SEP、未做物理断电测试边界。它不包含实际 keyblob、Root、派生密钥或私有 HMAC 值。当前原硬件 Root、nonce 和 bank 的保留另有 [0460 实机回归](qualification/0460/0460-systembag-qualified.json)。

## 0247：当前构建转向 APFS-only

0247 完整构建、真实 APFS System/Preboot、nonce 与 Image4、驱动和 panic FASTBOOT 通过；原生内核不再定义 HFS 符号，比旧产物减少 514,296 字节。HFS 源码及历史实验保留，不代表当前镜像仍带 HFS 驱动；旧 HFS+ 根卷记录仅用于历史追溯。

见[原始验收收据](qualification/platform-history/ACCEPTANCE-0247.json)。当时 Cryptex 和完整 iOS 未完成，后续版本已经继续接入；旧收据里的“下一问题”必须按日期阅读。

## 0274：8 核与约 7.998 GiB 连续窗口

原始 [DRV 输出](qualification/platform-history/0274-DRV-output.txt)实际记录 8 个核的 MPIDR、signal、非零 timer/SGI、valid，以及 `boot_mem=max_mem=8587837440`、`physBase=0x200004000`。数值与[当时检查点](qualification/platform-history/checkpoint-0274.json)一致；新增整理清单对两份文件逐字节哈希校验。

此前 0270 请求整 8 GiB 连续空间在 UEFI 报 `NO WINDOW`，不是内核 panic；正确请求 8 GiB−2 MiB 留出加载对齐余量。0271–0274 实际运行，随后在正常 iOS 用户态基线持续使用 8 核。原始 DRV 末尾通用 registry 的 CPU scope 仍带早期“no SMP qualification”旧注释；这里的硬件证明来自前面的逐核 timer/SGI/本核回调验收，不能把旧通用 scope 字符串当成测试结果。

当时 `memSizeActual=0` 还暴露 loader/kernel 的 boot_args 布局不一致，XNU 走原有 memSize 回退。原检查点完整保留该问题；本页不称它是当前未修问题，也不称已经接管全部 12 GiB。跨核负载/TLB 压力、AMP、DVFS、CPU_OFF/挂起/唤醒、性能和功耗均不能由这份有限记录推出。

0274 与当前用户态、C/D、触摸验收相隔多个阶段，不能拿其当时的单核/HID/密钥注释覆盖新的原版 HID 或加密 Data 证据。新增原始资料只包含公开 ABI/哈希、设备状态与诊断文本，不发布固件或密钥材料。
