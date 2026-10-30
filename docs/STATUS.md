# 当前验收状态（2026-10-31 / 0386）

当前分支为iOS16设备移植。目标用户态为iPhone X的16.7.16/20H392，原生XNU固定8796.141.3、16KiB页；源码回放为268 common +4 host /5239文件一致。

| 范围 | 已验证行为 | 边界 |
| --- | --- | --- |
| 原版APFS/Cryptex | 原版System/Preboot只读、真实加密Data，独立SystemOS库路径已接通 | 不是Apple SSV认证；完整系统镜像原始参考0341保留 |
| 硬件密钥 | 既有QSEE硬件Root/nonce恢复，原版MKB_INIT及factory无口令C/D文件明文/密文保持 | 完整A/B、口令、SEP/AKS接口尚未完成 |
| 资源生命周期 | 原版AppleImage4/defaults正常返回且busy归零 | 未关闭watchdog监控 |
| watchdog/Sandbox | owner平台类映射；真实watchdogd允许、本任务无关服务拒绝；原版心跳保持 | 后端是原版截止时间的软件观察器，不是独立硬件WDT |
| 维护验收 | 原版只读fsck退出0、DataOK、crypto-io；已恢复正常0386 | 没有修复或放宽警告白名单 |
| 用户态/UI | 前述被监测的daemon信号崩溃在两次窗口未复现 | classB密钥失败、部分重复启动仍需追踪；SpringBoard显示监控断言未解决 |

详见[Sandbox/watchdog报告](SANDBOX-WATCHDOG-20261031.md)、[实机收据与公共源码回放](qualification/0386/README.md)、[上一轮崩溃分类](USERLAND-CRASHES-20261031.md)。源代码、构建通过和实机行为是独立验收项，不能相互替代。

下一顺序：补齐真实密钥和daemon功能 → 结合最新构建重新审核GPU/显示/输入指南 → 实现对应驱动并实机验证 → 可见可用SpringBoard。全部目标仍在进行，每个后续验收小目标整理并上传一次。

已发布源码检查点：[801f64859479](https://github.com/GFander/UniXNU-kebab/commit/801f648594796822d45ef904447dbe37e2fee817)（ios16分支）；[发布核对](qualification/0386/publication.json)。完整SpringBoard目标仍在进行。
