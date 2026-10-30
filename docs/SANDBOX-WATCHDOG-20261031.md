# Sandbox 与原版 watchdog 授权：0383–0386 实机验收

已解决原版 Sandbox 加入后 watchdogd 无法打开本机 watchdog 服务、随后引发关键进程退出 panic 的问题。完整 SpringBoard 目标仍在进行，不能把这个小目标视为全部用户态功能完成。

## 根因与实现

原版策略池的硬件服务类条件为 `AppleARMWatchdogTimer`；实际板级服务此前为 `KebabWatchdog : IOWatchdog`。0379 实机在服务打开检查中返回 EPERM，原版 watchdogd 在 IOServiceOpen 失败路径 BRK。不是隐藏日志或放宽超时能解决的问题。

正式补丁0289将真实板级类命名为 `KebabPlatformWatchdog : IOWatchdog`。原版 IOWatchdog、用户客户端、管理员检查、启用/心跳/截止时间及关键进程退出策略保留；后端仍是观察原版真实截止时间的软件实现，不声称独立 Qualcomm 硬件 watchdog 已接通。

`adapt-watchdog-sandbox-policy.py` 对**显式 owner 策略**进行固定映射：原始 Sandbox 的只读策略数据中，把上述21字节类名替换为同长的真实本机类名。序列化布局、操作码、其他字节和全部原版执行代码不变。固定20H392输入SHA、只读段、唯一上下文及原版代码SHA均需匹配，否则拒绝打包。此策略数据经过设备适配，**不是未修改的 Apple 策略，也不声称通过 Apple 签名认证**。

没有按进程名跳过 MACF、没有改写权限检查返回值、没有更改原版 watchdog 监控或伪造 Apple 硬件类。原有策略条件作用于真实 native metaclass，普通 IOService 打开和用户客户端检查继续由原版 Sandbox 执行。

## 真实验证

- 0383、0384 两次完整内核重启和0386正常配置恢复，真实 watchdogd 的服务打开检查均为 policy0；同一个真实任务对无关 `IOResources` 的检查均为 policy1。后者只做原版只读权限检查，没有实际打开服务。
- 原版用户客户端每次只打开一次，原版 selector1 连续心跳、真实锁内序列/截止时间持续推进，原版 IOKit 监控保持开启。前两次观察都超过180秒，无关键进程或内核 panic；最终0386再次观察原版心跳。
- 真实硬件 nonce/Data Root 恢复、原版 MKB_INIT、旧C/D文件明文与四条原始密文、NVRAM及RTC记录复用通过；未生成新根密钥或重建已有数据。
- 0385运行原版签名 `fsck_apfs -n -l`，Data OK、crypto-io enabled、退出0；严格警告检查仅保留原有 APFS版本和overallocation两条，没有修复或增加白名单。已恢复正常0386，未把fsck固件留作默认启动。
- 407项真实候选镜像变异/只读策略映射检查通过；当前实际C++ watchdog的30项生命周期、截止时间及权限拒绝测试通过，三项有意破坏实现均被测试拒绝。IOKit/原版API在主机测试中为明确替身，不能替代上述实机验证。
- KDK相关编译单元、完整构建通过；268 common +4 host补丁回放后5239个源文件与Mac实际编译输入逐一相等。新增公共回放脚本也独立重现该结果。

原生内核0383复用于0384/0385/0386：18883416字节，SHA256 `6b481ac41096d1cb3bc9de11f4edadb7872c33103f1da6a624abef4985bf0051`。原始kernelcache SHA256 `2ec297bac95bcba01046fd0148e97e7e5885c05a6c18827f39f534ba375c1221`。完整收据为 `SANDBOX-WATCHDOG-ACCEPTED-0386.json`，其中记录各次运行日志SHA和真实心跳时间。

## 还要继续完成的工作

原来configd、lockdownd、mediaserverd、locationd、tccd、backboardd等被监测的信号崩溃在两次窗口内未复现；这只能证明这些已知异常路径没有再次发生。现有 `PROC?` 只列最近64个进程，不能据此证明较早启动的全部服务持续存在，需要补齐完整有界快照和非零退出状态证据。

真实class B文件密钥请求仍返回Unsupported；AKS17/35/102/103尚未全部实现，不能降级保护类别或假返回成功。蓝牙等任务有重复启动，还须追踪实际退出状态和资源依赖。SpringBoard仍在FBSDisplayMonitor初始化处断言。

下一步先补齐上述用户态功能与证据，再依据最新构建重新审核GPU、显示和输入指南，然后开始对应开发，目标为真正可见、可用的SpringBoard。此前指南“审核后停止”的旧限制已被用户最新授权替代，但开发顺序仍需遵守。每天保留owner_fast1，不重复全镜像校验；存储变更仍做相应完整验收。

参考规范：[官方驱动开发](https://unixnu.com/zh/drivers/overview)、[源码与补丁](https://unixnu.com/zh/xnu/source-and-patches)、[bring-up顺序](https://unixnu.com/zh/porting/bring-up)。每个后续实机验收小目标均整理补丁、测试、证据和文档，推送到GFander/UniXNU-kebab；固件、IPSW、硬件密钥和私人数据不随源码发布。
