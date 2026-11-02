# 桌面登记成功完成状态与首帧边界

2026-11-01。0485正常boot_b实机已观察到原版check-in成功完成标志，仍没有可用SpringBoard画面。[主收据](qualification/0485/0485-shell-frame-qualified.json)、[冷启动封存日志](qualification/0485/0485-media-boot.log)、[独立晚期日志](qualification/0485/0485-shell-frame-1.log)。

## 登记与迁移

0483已持久读回原版DMLastMigrationResults.success=true、20H392；0485再次只读验证该结果。偏好原文/private传输不公开，公开[审核摘要](qualification/0485/0485-migration-state-summary-reviewed.json)。哨兵缺失不是成功依据。

原版FB singleton、BKS service和BSAtomicSignal按准确20H392全局、对象偏移及objc_debug_isa常量读取，先验证原SpringBoard executable UUID/UID501和cacheUUID。nil-error回调才signal，错误路径跳过；原x1错误参数的objc_retain_x1入口也已核对。固定窗口95.233秒读到state0/completed0，独立晚期读取result0/state2/completed1；这是成功完成状态，未证明所有间接控制流、原子快照、后台断连重登或完整登记生命周期。

UUID必须在proc_iterate仍持proc引用时保存；task引用不保证稍后get_bsdtask_info仍非NULL。0484在刷前复核发现该窗口，未刷并被guard阻止；0364/0485修正后上机。没有暂停线程、修改成功位、换回复或绕过保护。

## 第一份记录在哪里停止

晚期SwapBegin序号485、队列收到2次、present执行1次，first/last id176、mask0x80000007；选中/找到Surface均0、缺失/复制拒绝分支均0、复制字节0，当前电源状态off。数字属于各自采样窗口，不能把SwapBegin序号当桌面帧数。

准确原版_kern_SwapEnd使用selector5/0x46c载荷，从context+0x18发送；SurfaceID槽在wire+0x54、掩码在+0x104。原null-surface清层路径也置位掩码，故mask非零不等于启用内容。当前本后端没有取得非零Surface进入复制，不能将SwapEnd/result0或CompletedID当像素完成。边界在有效Surface/桌面层进入本CPU后端之前；上游为何尚未提交桌面内容仍需追踪，不编造单一GPU/硬件故障。

SpringBoard主线程仍UIApplicationMain/_run/GSEventRunModal，backboardd CA RenderServer/IOMFBServer线程处于runloop。快照无暂停、非原子，不证明整段期间没有执行或某个唯一死锁；缺少exec base的主程序帧不猜ASLR地址。

## 验证与保留功能

- 24项实际core/swap/present模型与13编译行为变异；6项实际selector/report身份及退出窗口模型与5编译变异。VM、IOKit、像素与进程退出都是模型，完整caller/registry需原生/实机。
- 四个初始TU通过后单独修正BSD TU；0485完整构建，343共同+4主机补丁/5281文件与postbuild一致；实际UI/DRV直连新观察器、原版三个基类ABI、宏/MACF/保护/ACL/旧B调用边通过。
- 0485旧A/B/C/D密文与负载、原六类keychain冷UNWRAP、硬件Root/nonce/bank/systembag/defaults/RTC/NVRAM和watchdog318.586秒通过。真实原2201调用仍result0。没有重新生成密钥、改类、停watchdog或新全盘扫描/fsck。
- 0362只读固定迁移文件、0363观察状态/计数、0364修正proc身份生命周期；原密钥、安全、回复、绘制和电源路径保持。未增加GPU/显示/输入功能，蓝牙延期；面板120Hz与可见桌面未验收。

后续应追SpringBoard层树/事务、CA渲染结果与有效Surface提交的实际返回和生命周期；这次没有声称修复了该上游生产问题。
