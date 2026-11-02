# 离线首次初始化与桌面现场（0535 / 0536）

2026-11-02。0535 完成一次受测量约束的初始化；0536 复用同一已封存原生内核，并移除了初始化计划和私人 DataArk 路径。用户确认能进入桌面，实际 GOP 已捕获图标页，但滑动会触发 SpringBoard 异常退出，因此尚未验收为稳定可用桌面。

## 原始阻塞与原版服务结果

0527–0533 的设置引导停在联网页。原版 mobileactivationd 自身日志明确显示 should_hactivate=false、找不到 activation record、Activation State: Unactivated。这与缺少网络驱动同时存在，不是 Activation Lock 的证明。Setup 偏好也缺少完成标志；原版迁移状态已经是 success/20H392，不能再覆盖迁移。

对照固定 20H392 MobileGestalt 与 mobileactivationd，四个 chosen 属性按 U32LE 显式设置为 0/0/1/1。设备类型仍为 iPhone10,3/D22AP，原版 DeviceType 实际选择 devfused-undemoted 分支，原服务自身报告 “Activation State: Activated”。这只是实验机 guest development profile 的原生本地状态，不代表 Apple 官方激活、真实 Apple 硬件熔丝、SEP 证明、服务器签名记录或联网能力。

参考了[UniXNU 离线设置指南](https://unixnu.com/zh/guide/skip-setup)、[其 seed 脚本](https://github.com/UniXNU/UniXNU/blob/main/scripts/seed-data-skip-setup.py)和[ios-sweet 工具](https://github.com/dipa-studio/ios-sweet/blob/main/tools/sweet-root-tweaks.py)。没有把上游整套 seed 写入当前 Data，也没有预写 Activated、套用 21H461、修改迁移状态或停用 watchdog。

## 受控持久化与回退

按照用户确认，完整备份覆盖全部直接受影响文件及元数据：原 MobileGestalt cache、PurpleBuddy 偏好、legacy 与原 daemon 实际使用的两个 DataArk。电脑保存原始内容、完整原 ACL、属主/组 UUID、时间、权限、保护类别和校验；手机通过原 VFS 原子交换保留旧 inode。实际 DataArk 的特殊 gid=-2 与一个 ACL 项完整保留，未改为普通 mobile 组。

预先完整检查所有条目后，两个 DataArk 先做内容相同的检查点；旧 MG cache 留作备份，由原版系统重建；PurpleBuddy 只合并 SetupDone=true、SetupFinishedAllSteps=true、SetupVersion=11。原 setupMigratorVersion=12 与迁移偏好保留。启动后原版进程还更新了 lastPrepareLaunchSentinel；这是回读中单独记录的运行期变化，不把整个文件误称为只变化三个键。

独立回读证明四个旧备份的内容、原 inode、属主/权限/保护类别、ACL 形状、UUID 和创建/访问/修改/备份时间保持。当前 DataArk 原始 ACL 与原备份相同；备份 ACL 原字节由原生事务完整校验，尚未通过独立 USB 接口再次导出。正常 rename 会更新 ctime，未声称恢复法证意义的原 ctime。

生产事务核心主机测试325项、68个中断点及其已提交状态的逆向恢复、5个实际编译并运行失败的行为变异通过；实际固定 VFS 包装器39项、只读缓存/ACL测试40项通过。计划工具另有20项测试。它们不是实机断电恢复证明。已用当前新 inode/hash 生成回退计划，尚未执行实机整体回退；使用前必须重新获取当前状态，不能盲用旧回退计划，也不能以刷回旧 boot 替代 Data 回退。

## 分项验收

| 要求 | 实际结果 |
| --- | --- |
| A 设置引导不再显示 | 0535 后已见控制中心；0536 冷启动已见图标桌面 |
| B 原版激活服务状态 | 原版服务日志实际报告本地开发分支 Activated；没有签名激活记录或官方激活证明 |
| C 可交互桌面 | 用户确认进入桌面；实际图标页截图已保存；滑动会 SIGABRT，稳定交互未通过 |
| D 重启重复进入 | 已验证从一次性0535切到无计划0536的完整内核重启；两轮同日常镜像、稳定滑动与应用启动未完成 |
| E 加密与服务 | 两次正常基础回归均超过300秒；旧 Root/nonce/bank/systembag、A/B/C/D文件原密文和keychain6–11恢复、MACF/defaults/RTC/NVRAM及原watchdog通过有限窗口 |

0535/0536 使用原生 SHA-256 `d729cb1d05d2bc94f906312056f5cd74a208827ff3e328679eceda1c5c9e2a3f`，19,067,240字节。391个共同补丁加4个Mac主机补丁，5,288个源码文件与实际构建输入逐项一致。0536是镜像配置版本，明确没有声称重新编译内核。

## 新的真实故障

0536 在209.619秒出现真实触摸，209.694秒 SpringBoard pid27 以 signal6 退出。固定原版共享缓存符号化显示 abort / C++ terminate / objc_exception_rethrow / CFRunLoopRunSpecific，209.746秒系统拉起新进程。此现场是未处理异常的终止路径，尚不足以确定异常名称或底层根因。基础 watchdog 回归通过不等于 SpringBoard 不崩溃。

小组件比例、控制中心居中和红色 UIKit fallback 条仍未修复。显示后端当前同时具有1080×2400真实面板/CA装配缓冲和1125×2436 D22逻辑目标；需继续核对原版屏幕布局读取路径，不能仅通过整体缩放掩盖问题。0537仅增加有界只读原版 CrashReporter 注释观察，原生构建/实机资格独立记录，不算功能修复。

## 保留边界

没有格式化/替换 userdata、重建现有密钥、清除 systembag、修改原服务可执行代码、关闭 Sandbox 或 watchdog。factory 无 PIN、现有硬件封装 Root 与有限类别服务仍是当前安全边界；完整口令/SEP、官方 Apple 激活、Wi-Fi/蜂窝、完整 GPU/DPU/120Hz仍未实现。本报告和截图不得作为这些功能通过的证据。
