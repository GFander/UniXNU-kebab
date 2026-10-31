# 标准化开发检查点（2026-10-29）

更新：2026-10-29 09:23:21 中国标准时间。最新已验证0225。窗口已结束；在途构建/捕获均收尾，续跑自动化已暂停。

这轮已修复旧SPMI并发争用，完成SPMI/PMIC和GENI/TCM的主要分层，补齐devfs发布时间、真实服务诊断和复位策略边界；同时证实APFS Preboot通用持久写。完整iOS仍停在Image4 nonce服务，未到桌面。

## 驱动现在重写了哪些

仍是9个硬件/平台功能域，另计Surface兼容层和AES辅助组件。当前manifest列明25个主要实现文件、19个类、11个真实registry角色；拆文件和加类不等于新增硬件能力。

| 功能域 | 本轮完成并验证 | 尚未完成 |
| --- | --- | --- |
| SPMI/PMIC | 共享普通事务mutex、bus/客户端分离、scoped nubs及3个类型化服务；持普通锁panic仍可无锁复位 | 通用动态枚举、HID及完整电源消费者 |
| GENI/触摸 | GENI/bitbang传输与TCM协议分离，完整事务锁，真实Bus/Touch服务 | 控制器identify/采样仍未通；完整触摸事件和HID |
| UFS/块存储 | 既有descriptor/完成/RO边界保持，原版10绑定/22槽重审；Preboot真实持久写/重启全SHA | 正常控制器provider/IRQ/SMMU，以及Data内容保护 |
| USB/恢复 | IMEM/PON/PSCI策略和私有状态移出DWC3；正常同步复位及锁内panic FASTBOOT通过 | 普通传输/协议、轮询线程、断连重连的服务生命周期 |
| 按键 | 采样/去抖保持，devfs就绪后发布，24字节记录/真实VCHR资格通过 | 用户消费、HID按下/释放整链 |
| PlatformExpert | 资源/personality及provider范围核对，固定20H392原版ABI来源 | PMGR开发stub仍非真实板级电源管理 |
| CPU | 单核拓扑/SGI/定时器基线保持 | SMP、跨核TLB/cache和IRQ路由 |
| RPMh/VRM | RSC与SPMI回读分层，白名单/状态机和类型化状态保持 | 消费者投票、真实调压/时钟、睡眠唤醒/IOPM |
| 显示 | 既有factory/引用/关闭合约与自测持续回归 | 原版用户ABI/selector74、DPU直扫、真实vsync/120Hz |
| Surface另计 | 连接/参数/共享对象清理的原生资格保持 | 真实iOS框架、跨进程映射及backboardd |

四个带固定供体地址的实现已加KEBAB编译范围保护；板内正文相同，其他板预处理不产生这些代码/绑定。

FWLOG满环自覆盖故障已修复：打印前复制，normal/panic各有固定缓冲；不分配、不等待驱动锁，保持调用者IRQ状态。主机24种精确内容/边界/IRQ、2种重入及panic打断normal案例通过；设备普通/panic命令与恢复另留证据。两份64KiB固定数据缓冲另有状态字段，当前只资格化cpus=1。

## iOS主线的实际进展

- B1/B3：System APFS只读根持续工作。Preboot真实独占创建65559字节文件、APFS同步/UFS cache flush通过；重启只读完整SHA一致，checkpoint261。System launchd/dyld完整SHA不变。Data被原版APFS拒绝未加密卷，仍未挂载/配对。
- B2：PID1具体等待为`security.mac.img4.ignition_blob`。官方SoftwareAES移植经20H392重审，CBC128/192/256、descriptor/raw KAT真机通过；错误预期答案拒绝注册。原版Image4需要专用nonce selector2212，后端未实现；AES默认关闭。详见IMAGE4-BLOCKER-20261029.md。
- 固定SCM查询实测SMCCC1.1、SCM64可用、ICE invalidate/set availability1、QSEE version availability0。无密钥操作，不能据此认定2212后端可用。
- B4/B5/B6/B7：原版静态信任缓存已加载；动态信任、真实Preboot/Cryptex、DSC运行时映射和完整launchd任务未通过。离线映射计划通过不是用户态已通过。
- B8/B9/B10：先做SMMU再A650/Metal，随后DPU直扫/真实vsync/120Hz/HID，最后backboardd和SpringBoard。CPU/GOP画面仍是调试出口。

## 构建与验证证据

共同补丁176个，主机补丁4个。0217时代的4205、后来的4208仅是列明扩展名范围；干净目录构建发现遗漏的脚本/exports/xcconfig后，已扩至5192个源目录文件核对。0222真正从重放的源码完成clean全量构建和设备回归，保留完整旧工作树备份。主机Python检查器保留真实语法/tab/ASCII检查，修复collections模块遮蔽，空文件和负向用例通过。后续使用`start-full-build.py VERSION --prepared-source`，跳过builder的Git patch循环；Git只在Ubuntu操作。

原版供体代码、storage 10绑定/22槽、AES181槽/162继承与逐类ABI均核对。当前根卷、Preboot全SHA、两文件全SHA、11 registry角色、原诊断/keys、panic FASTBOOT通过；0220正常sync FASTBOOT和0221持锁panic也分别实测。完整SDK/KDK/Xcode不是由5192这个计数覆盖，外部输入的范围和哈希另见BUILD-BASELINE-20261029.md。

最新native SHA256：`e7cb391447d5a6aaf2feb45a3e5d48191996ca775f179a40adecc38f09b1655b`。
最新boot SHA256：`4a9601ee935d4530bac165e16322faba3705a27546995b377f15d18b142080cf`。

不整体重刷userdata，不重复执行216写模式。Mac旧raw仍为checkpoint260；设备已为261，不能用旧raw覆盖。实际proof路径固定且O_EXCL，SHA256为`3f2f47b49455f2d5cce7d745829440ccf0e8f1beda6c5120641c25213f78c957`。

补丁、说明和测试已镜像到Ubuntu仓库工作区；本轮尚未commit/push，GitHub页面尚不反映这些修改。既有未提交内容保留。

最终常规配置通过18分钟、4个只读检查点：PID1等待地址/CPU时间不变，PON及两份系统文件全SHA一致。观察后再次完整转储65536字节FWLOG，正常同步返回fastboot；活动槽b，无本轮在途构建/刷机/串口捕获。统一检查17 PASS、0 FAIL、4个可选IPSW SKIP。
