# 当前状态：0544真实边缘触点来源，用户确认返回、回桌面和多任务三项边缘手势均恢复

2026-11-02。0541全部布局和滑动已获用户确认；0542触摸坐标、0543内建屏幕关联两轮单独修正后，三项边缘手势均人工失败。进一步核对固定20H392原版UIKit，确认初始触点必须携带FromEdgeTip0x800来源，只有边缘坐标不足以进入边缘识别。

0544在原生物理触屏路径按真实首次落点分类并保留到真实抬起，中央起手不改判；原HID解析、触点生命周期和原UIKit手势识别器保留。根据当前TCM坐标软件分类，不声称Apple触控芯片形态/压力检测；没有合成触点、Home键或完成手势。仅刷boot_b，原Root/bank/systembag、已有密钥和加密Data/安全及原watchdog基础回归304.447秒通过。用户确认返回、回桌面和多任务三项边缘手势均恢复。

399共同+4主机补丁、5290源码文件与实际完整原生构建配对匹配。生产分类/透传524,519主机检查和8运行失败变异、原版UIKit判断指令离线执行、原219/37槽及最终219槽/children accessor ABI均通过。真实子事件增加0x800观察：True。原显示布局、硬件密钥、加密Data、MACF/ACL/Sandbox和原watchdog保留，无新手工持久化偏好修改。

见[0544修正、验证与边界](EDGE-GESTURES-0544.md)及[限定收据](qualification/0544/edge-qualified.json)。有限窗口不替代长时GUI稳定性；网络、GPU/DPU/120Hz、完整SEP、AMP及完整12GiB仍未验收，蓝牙继续延期。本次发布包含配对源码、可重跑测试及限定收据。

# 历史0543：事件关联修正后手势仍全部失败

2026-11-02。用户已确认0541全部布局和滑动正常；0542修正触摸坐标盲区后，返回/回桌面/多任务人工复验仍全部失败。固定20H392的backboardd单独读取事件0xb0019字段区分直接触屏和触控板路径，不能用服务注册表DisplayIntegrated代替。

0543保留原版IOHIDEventDriver解析与分发，仅给本机真实内建触屏的digitizer事件补充该屏幕关联；未合成触点或Home按键。仅刷boot_b并重启，原Root/bank/systembag、已有密钥和加密Data/安全及原watchdog基础回归304.505秒通过。用户复验手势仍失败，继续追踪。原布局/显示、密钥/Data、MACF/ACL/Sandbox和原watchdog保留，无新手工持久化偏好修改。

398+4/5289源码与完整原生构建配对一致。驱动219槽/事件37槽、最终子类219槽、限定原类虚表调用及原版ARM64字段访问执行已核对；生产包装函数210组透传与提供者/原失败保留检查及7个运行失败变异通过。实际事件0→1观察：True。基础回归不替代长时GUI稳定性；GPU/DPU/120Hz、网络、完整SEP、AMP与完整12GiB仍未验收，蓝牙继续延期。

见[0543修正与边界](EDGE-GESTURES-0543.md)和[限定收据](qualification/0543/edge-qualified.json)。源码尚未提交/推送，既有dirty工作保留。

# 历史0542：坐标盲区修正后手势仍全部失败

2026-11-02。用户确认0541全部布局和滑动正常；当前新问题是返回、回桌面、多任务等边缘手势失效。已核对触摸转换仍使用旧(22,18)居中偏移，无法到达逻辑屏幕四边；0418改为原传感器端点映射完整1125×2436逻辑范围。原HID描述符/驱动、显示布局、既有安全与加密Data均保持。

0542已仅刷入boot_b并重启；原密钥/加密Data/安全与原watchdog基础回归304.445秒通过。返回/回桌面/多任务手势的人工GUI复验仍全部失败，不能以基础回归或HID计数替代。

397+4补丁、5288源码文件与完整原生构建配对匹配。生产转换/HID编码5,268,910项主机检查、4个编译后的运行失败变异及193项规范TCM检查通过；没有新的手工持久化偏好修改。详见[0542修正与边界](EDGE-GESTURES-0542.md)和[限定收据](qualification/0542/edge-qualified.json)。蓝牙、GPU/DPU/120Hz、AMP和完整12GiB继续延期。

# 历史0541：逻辑画布统一

2026-11-02。参照两个 hackios 仓库并核对原版20H392的实际canvas选择，修正provider发布物理尺寸导致的逻辑画布不一致。0541实际CA渲染缓冲已从1080×2400中间合成再复制，变为直接1125×2436；真实GOP仍为1080×2400、stride4320。原版屏幕类型缓存4→3，没有覆盖用户态类型选择或系统偏好。

真实保留Surface的锁屏像素中，红色rdar回退条已消失，底部两个按钮左右对称。原始wire、连续图像与PNG逐像素复核通过；这张图来自Surface，不宣称抓取时屏幕点亮。一次自动GOP抓取命中熄屏得到黑帧，失败边界保留。

396+4补丁、5288文件与完整原生构建输入一致；原Root/bank/systembag、加密Data、安全/defaults/RTC/NVRAM及原watchdog基础回归304.467秒通过。0539匹配原版CREATE2类型授权规则的改动仍保留，真实横滑/应用滚动尚待用户返回复验，控制中心和键盘边缘也不能记为全部通过。

见[0541修正及边界](LAYOUT-SCALING-0541.md)、[限定范围收据](qualification/0541/layout-qualified.json)、[真实锁屏Surface](qualification/0541/lockscreen.png)。没有新手工Data偏好修改，原始加密Data与回退备份保留。蓝牙、GPU/DPU/120Hz和完整12GiB边界不变。

# 历史0535/0536：离线初始化后已显示桌面，滑动异常现场

2026-11-02。0535执行一次性受测量约束的文件初始化；0536复用同一封存原生0535内核，移除一次性计划和私人DataArk路径后冷启动，已实际捕获SpringBoard图标桌面。用户确认能进入桌面，但横向滑动及应用内滑动会闪退，不能记为稳定可用桌面。

原版mobileactivationd实际选择显式guest development分支并自行报告Activated；没有预写Activated、创建签名激活记录、连接Apple服务器或宣称Apple官方激活/SEP。原版迁移状态保持success/20H392；全部直接受影响文件和元数据在主机备份，手机保留旧inode，未格式化/替换Data或重建既有密钥。

0535/0536基础加密/旧密钥、defaults/RTC/NVRAM、MACF及原watchdog均通过超过300秒窗口。391+4/5,288源码清单精确匹配。该回归不证明GUI稳定。实际SIGABRT经0537只读诊断确认原版异常为NSInternalInconsistencyException / Failed to create work interval；继续追踪权限与原生创建返回值，不以无条件成功或关闭监控处理。

见[完整阶段报告](OFFLINE-SETUP-0536.md)、[分项收据](qualification/0536/offline-setup-qualified.json)、[实际桌面截图](qualification/0536/springboard-desktop.png)。A设置UI、B原服务本地状态、C稳定交互、D重复重启、E加密回归分别记录。真实网络、官方Apple激活、口令/SEP、GPU/DPU/120Hz和完整12GiB尚未实现；蓝牙继续延期。

# 历史0527状态：无PIN密码请求修复，实机进入设置引导

更新时间：2026-11-02。已通过真实扩展keybag只读查询修复错误密码请求。SpringBoard原版缓存由0526的PasscodeSet=1变为0，FirstUnlock=1；用户及实际GOP确认原版设置引导进入“选取Wi-Fi网络”页面。没有强置用户态状态、伪造验证成功、重建bank或清空Data。

0405仅允许原版只读selector35复用已认证factory状态helper。现有DER状态字节、bank、硬件Root、加密Data和其他操作权限保持。1,163项实际生产分派/状态函数主机检查及5个运行失败变异通过；原版ARM64解码器重新执行通过。完整原生构建384个共同补丁加4个主机补丁，5,287个源码文件逐项一致。正常密钥/Data/安全/defaults/RTC/NVRAM及原watchdog基础回归304.414秒通过。

当前1080×2400实机GOP截图经独立CRC、完整内容、日志分流和PNG逐通道复核并实际查看。界面为设置引导，不是可用桌面；未声称联网、激活、新密码设置/验证、完整SEP或长时间稳定性通过。红色UIKit回退条仍可见，旧0523约2012秒的SpringBoard180秒watchdog问题仍未排除。蓝牙、原生GPU/DPU及完整12GiB仍延期。

见[0527修复与边界](PASSCODE-FIX-0527.md)、[验收元数据](qualification/0527/0527-factory-no-pin-qualified.json)、[原版状态观察](qualification/0527/0527-original-state-observations.log)。此前的[0526只读定位](PASSCODE-READONLY-0526.md)保持为修复前证据。以下0485资料仅作历史基线。

# 历史0485基线（原文保留）

更新时间：2026-11-01。完整功能总览见[仓库首页](../README.md)，资料入口见[文档索引](README.md)。本页取代此前逐轮追加的“最新状态”；旧页原文保存在[历史归档](history/STATUS-before-0460.txt)。

## 已验收与未验收

| 范围 | 当前结论 | 证据 |
| --- | --- | --- |
| 正常启动 | 最近正常运行0485，原版check-in成功完成标志和首份帧复制边界已实测，原版 iOS 用户态运行；原硬件密钥、MKB/systembag、C/D 加密 Data、RTC/NVRAM/defaults 与 watchdog 回归通过 | [0485收据](qualification/0485/0485-shell-frame-qualified.json)、[原始日志](qualification/0485/0485-media-boot.log) |
| 共同源码 | 343个共同补丁 + 4 个 Mac 主机补丁，全部 5,281 个源码文件与实际编译输入一致；固定 Apple OSS 基提交 `1b191cb58250d0705d8a51287127505aa4bc0789` | [公共回放](qualification/0485/public-all-source-replay-0485.json)、[源码清单](qualification/0485/all-mac-source-0485.json) |
| CPU / 内存 | 8 核与 8 GiB−2 MiB（8,587,837,440 字节，约 7.998 GiB）连续窗口已实测；非完整 12 GiB、AMP 或功耗验收 | [平台历史验收](PLATFORM-BASELINE-20261029.md) |
| 触摸 | 0459 人工五点、横竖滑动、双指分离/保持与依次抬起通过原版 HID；新增 2,030 份报告，零队列丢失 | [实际记录](TOUCH-PHYSICAL-20261031.md)、[收据](qualification/0459-touch/0459-touch-live-qualified.json) |
| 音量键 | 真实音量上/下的按下与释放已进入原版 HID；不等于 GUI 消费 | [0429/0430](HID-INPUT-AND-DATA-IDENTITY-20261031.md) |
| 显示 | GOP 控制台可见；原版显示枚举、显示区域及缩放修复已有实测 | [枚举](GOP-DISPLAY-ENUMERATION-20261031.md)、[区域](DISPLAY-AREA-20261031.md)、[缩放](DISPLAY-SCALE-20261031.md) |
| 桌面 | **尚未显示可用 SpringBoard**；原版check-in成功完成标志已置位，桌面像素尚未进入本后端，不把进程存活算作桌面完成 | [UI 等待](FIXED-UI-WAITS-20261031.md)、[迁移现场](CORELOCATION-WAIT-20261031.md) |
| 图形 / 120 Hz | A650/Metal、完整合成呈现、DPU/DSI 生命周期与真实面板 120 Hz 尚未验收 | 120 Hz 为用户提供的硬件规格/目标，不能用软件定时器证明 |
| 蓝牙 | 控制器无线功能不可用；资源、固件与内部 UART 前置证据已归档，按用户安排延期 | [后续入口](BLUETOOTH-DEFERRED-20261031.md) |

0485原生内核：18,967,016字节，SHA-256
`638a4491c6ebc77a9a0301782fe0f8c289f0f8cc53cd2b5009b4c47664095fae`。
0360/0361 只改 NativeAES 和既有 IOSurface OOL 传输；原 nonce KDF、A/B/C/D 与 keychain 代码不变。不是完整 FairPlay、AKS、迁移或桌面完成。

## 当前直接阻塞

0485 的固定原版状态读取先得到 `state=0/checkin-completed=0`，封存回归后的独立快照得到 `state=2/checkin-completed=1`。精确20H392代码核对证明nil-error完成回调才signal该原子字节；同时原版迁移偏好仍为success=true/buildVersion=20H392。这里证明原版初始成功完成状态，不保证后台断连/重登的完整生命周期。

晚期帧计数为SwapBegin序号485、排队2、present调用1。首份/最后处理记录id176、mask0x80000007，但本后端未选择到非零Surface ID、实际复制0字节。原版null-surface路径也设置掩码位，所以不能把它当作已有显示内容。当前证据把边界限定到有效Surface/桌面图层提交进入CPU复制之前；尚未确定上游只形成此类记录的唯一原因，不能据此宣布GPU或DPU故障。

原版UIKit与CA服务线程仍在运行；watchdog318.586秒、旧A/B/C/D与六类keychain、硬件Root/nonce/bank/systembag/defaults/RTC/NVRAM回归通过。完整密钥/FairPlay、可见桌面、GPU/Metal及真实120Hz未验收，蓝牙延期。详见[源码、实机与边界](SHELL-FIRST-FRAME-20261101.md)。

## 必须保留的边界

- 持久bank策略为factory无口令C/D，B与keychain AK/AKU从现有Root/bank用途隔离派生；锁屏口令、多用户、完整keychain/Apple SEP尚未完成；A/B缓存均为内部加密句柄，factory无PIN；非全RAM擦除证明。systembag envelope 承载本后端认证 bank，不冒充 Apple SEP 格式。
- 原版 watchdog 客户端、心跳与截止时间保持；其后端为软件观察器，未声称独立硬件 WDT。0479聚合心跳收据覆盖到321.176秒；这是有限窗口。
- Sandbox 执行代码保留；owner 策略中的硬件类条件做了精确板级映射，不能称策略数据未修改或具有 Apple 签名认证。
- development-fast 是正常开发策略；完整 System/SystemOS 内容检查保留 0341 证据，最近独立原版只读 Data fsck 为 0416。本次没有重新执行全盘扫描或 fsck。
- 0459 真实触摸只验到原版 HID 接收；GUI 消费、屏幕像素对齐、超过两指及物理采样率未验收。
- 部分旧阶段文档仍描述当时的 HFS+、单核或触摸未采样状态，作为历史证据保留，当前结论以本页和对应更新的验收收据为准。
