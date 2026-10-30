# Watchdog / calendar 实机验证（0363–0365）

本轮修复了 watchdogd 原先因缺少服务而 SIGTRAP 退出的路径，以及跨重启的 Data 日历回退。冷启动复测和最终原版只读 fsck 通过。**完整 iOS 启动仍未完成：现在停在 `defaults` 服务的 60 秒 IOKit busy timeout。** 未开始 GPU、显示或输入开发。

## Watchdog

补入原版 IOWatchdog/IOWatchdogUserClient 的平台后端，保留权限、独占打开、原始派发、开关及心跳处理。KebabWatchdog 每秒在原始锁下读取 enabled/sequence/deadline；真实到期会 panic，未伪造心跳或扩大期限。它是软件期限后端，不能覆盖 CPU/调度器完全停摆，不宣称已有 SM8250 独立硬件 WDT。期限失效行为已通过生产代码主机测试，未单独在手机注入期限失效。

0362 实际用户栈归一化为 `0x10000b82c -> 0x100009bb0 -> 0x100007498`，精确命中原版查询 `IOService + IOPropertyMatch{IOPMUPrimary:1}` 返回空后的断点。记录：`boot123-20261029/watchdog-matching-contract.json`。因此在真实 PM8150 PON 服务 KebabPon 上补上主 PMU 身份，发布前必须读取到实际 Gen2 PON（实机 subtype=4）。IOPMUBootStage 为明确标记的易失 RAM 属性，仅允许 root 和单一 0–255 数值；无猜测的 PMIC/AON 写入。该 setter 的权限/边界已主机测试；尚未观察到原版用户态实际提交启动阶段值。

0363/0364 两次实机：原版 type1 客户端各成功打开一次，selector0 状态查询和 selector9 IOKit 监控查询返回成功，真实 selector1 心跳分别成功 **7 次 / 8 次**。原始锁下观察到 sequence `3 -> 6`、冷启动 `4 -> 7`，enabled=1、期限真实向前。watchdogd 没有重启或再次 SIGTRAP；IOKit 监控和关键退出策略保留。

两次随后分别在启动后约105秒/96秒由原版 watchdogd 的 IOKit 监控路径触发：`busy timeout[0], (60s): 'defaults' (a,4020001) @IOService.cpp:5690`。这是当前实际停止点，不能把本轮限定范围验收写成系统已稳定启动。

原版 watchdog 执行段14536B SHA256 `9bebb0c0f90d50c7703c99db551ac0894ddcda8eab83d8ded5fdf11bd3b13efb` 在最终三份 prelink 中逐字节保持；171 个本机虚槽、5 个绑定和11处字段指令已核对。

## Calendar / Data

0357 只读读到 PM8150 SID0/PPID060/APID2 control=0x80、counter546788，而内核 calendar 只有50秒，确认原来的计数器未接入日历。0274 采用只读 counter+offset 接到平台 UTC 回调。首次校准来自已有开发主机 UTC，**不是硬件 UTC 或防回滚安全时钟**。

偏移记录 `/private/var/db/hios-rtc-offset-v1` 为现有真实加密 Data 上的 root:wheel/0600/classD 文件，两个独立64B记录；SHA256是损坏检测，不是MAC。0358 首次 VFS 写入、fsync、UFS flush；之后冷启动均恢复同一个偏移1792825531，在 Data RW 前应用。偏移未变化不重写，PMIC 计数器不写入。若计数器复位/倒退，恢复拒绝并阻止继续到 Data RW；主动用户日期设置的持久化路径尚未单独实机验收。

0361 和最终0365 原版签名 `fsck_apfs -n -l /dev/disk1s2` 均返回0、DataOK、crypto-io enabled。严格告警门禁通过，原先未来时间告警消失；只保留已有APFS版本/overallocation两条备注，没有扩展允许名单或修补时间元数据。

原版 systembag/MKB_INIT 冷重用通过。原有硬件 Root、nonce、device class bank 保留；C/D各65559B明文和四段原始密文与0343基线一致。System/Preboot只读，没有重新格式化、换Root或生成新classbank。范围仍为工厂无口令C/D，不宣称AppleSEP/口令A/B兼容。

## 验证与工件

- RTC生产记录 helper：590项损坏/边界/写入中断，ASan/UBSan通过。
- Watchdog生产后端：30项生命周期/期限/拒绝，3项行为破坏均被拒绝；IOKit/原始API明确mock，实际客户端及心跳另有实机日志。
- PMU生产属性方法：272项权限/数值/后端拒绝，ASan/UBSan通过；IOKit和observer明确mock，实际PM8150读取另有实机日志。
- 最终全量构建0363：257 common +4 host补丁，5238源文件与Mac实际构建输入逐SHA一致；0364/0365复用完全相同native二进制。
- native18829184B SHA256 `30a47a400e9a0612f6475a4c89be989066075c2432afa8940566f1eae7022f1f`。
- 总验收：`boot123-20261029/WATCHDOG-CLOCK-ACCEPTED-0365.json`。原始日志/采集SHA/刷写回执/ABI审计均保留。每次刷写均检查Sending/Writing两个OKAY。
- 日常fast保留；不可变完整System/SystemOS内容引用0341，不重复全扫。没有commit/push。

手机最后已确认 fastboot `<device-serial>`、slot b；boot_b为0365只读诊断。

## 下一步

追踪 `defaults` 的真实匹配/忙计数/资源等待生命周期，修复实际未完成的服务或资源发布；不要关闭IOKit监控、缩短/伪造忙计数或吞超时。其后再按实际调用补齐AKS17/102/103及其他用户态依赖，保持硬件密钥和加密Data。不开始GPU/显示/输入。

## 一手参考

- [上游 RTC 平台偏移模型](https://github.com/rootstockos/ArmDTPlatform/blob/asteroids/ArmDTPlatform.cpp)，读取时blob4796e47a747bfa0a2dd1eaf71141239c3f68923d。
- [QcomPmicRtc](https://github.com/rootstockos/UniXNU/blob/asteroids-dev/drivers/QcomPmicRtc/QcomPmicRtc.cpp)，blob2db159a2d87eeb282f7c879dc9e5e80b6371e44a；[Linux PMIC RTC](https://github.com/torvalds/linux/blob/master/drivers/rtc/rtc-pm8xxx.c)，读取时blobe624f848c22b5799d23107518bbfd01b2e93d150。
- [上游原版 watchdog 边界](https://github.com/mouse233/unixnu-fuxi/blob/f8e7c17c6d9eb2cf7534a764a36fe2e5820966a1/diagnostics/ram-root/watchdog-service.md)与[实现](https://github.com/mouse233/unixnu-fuxi/blob/f8e7c17c6d9eb2cf7534a764a36fe2e5820966a1/package/RamWatchdog.cpp)。实现blob29b2a369bc1d6b8ff3ea5b27320fcca5e9e403d9；未套用iOS17私有地址。相关许可见boot123-20261029/WATCHDOG-UPSTREAM-NOTICE.txt。

开发开始2026-10-30，最终记录2026-10-31（香港时间）。
