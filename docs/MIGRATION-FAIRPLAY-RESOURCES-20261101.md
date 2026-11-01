# 0478：核实 FairPlay 失败文件探测的实际名字

2026-11-01。继[0477返回现场](MIGRATION-FAIRPLAY-SYSCALLS-20261101.md)，
0358只补有界资源名观察。**取得六次完整路径快照；初始化退出、迁移和桌面未修复。**

## 实机结果

| post-return ENOENT路径 | 次数 |
| --- | --- |
| /System/Library/Caches/com.apple.dyld/ | 1 |
| /usr/sbin/Info.plist | 3 |
| /Library/Preferences/Logging/com.apple.diagnosticd.filter.plist | 2 |

这些是失败返回后的copyinstr快照，存在用户态并发修改窗口，不冒充原文件lookup
瞬间参数。均copy成功、未截断/净化替换，6/32预算未耗尽。只记录名字，未读取文件
内容。尚不能将配置/缓存探测当成退出原因，未为消除ENOENT创建文件。

已覆盖的原fromPath、普通property和bin property结果入口没有捕获注册表事件；
不等于覆盖所有IOKit/Mach调用、早期拒绝分支、OOL/递归接口或所有重启实例。
原FairPlay仍signal0/status0退出，BSD返回总138、最后32次与首PT31证据保持。
PT_DENY_ATTACH成功/NOATTACH置位/debugger能力0，不改变防附加策略。

## 实现与验证

- 精确原UUID/name/UID501/currentproc及首PID过滤；共享封存状态、32事件上限。
  每名字128字节缓冲；copyinstr失败或超长不输出部分用户数据，日志字符净化，
  以ticket配对两条短行。锁内仅预留，锁外复制/打印。不读property值或密钥。
- 原systemcalls两个返回点只在exact open/openat sysent且ENOENT时取原uu_arg
  的path/dirfd；原IOUserClient入口结果旁加void观察。三源原代码行保留，原结果
  不变，A/B/C/D、keychain和硬件Root源未改；不称全部安全源码逐字节未改。
- 生产helper257检查、25实际编译行为变异；保留旧环/8线程并发模型并新增路径
  分流、身份/限额/封存、EFAULT/超长部分数据拒绝、字段/边界/净化/224B日志检查。
  kernel proc/UUID/调度/锁/copyinstr是明确模型。三个原生TU0error，32/14/14
  warnings未屏蔽；337 common+4 host、5280源全回放/postbuild一致，八实际调用边。
- 固定DRV/PROC/UI/MIG/BBOARD/MIG真实完成。原A/B/C/D旧key/负载/密文、原
  securityd6–11先UNWRAP及Root/nonce/bank/systembag/RTC/NVRAM/defaults保持；
  A锁定已打开cached/direct读写零传输拒绝、解锁恢复，watchdog313.871秒有限窗口。

原生18,949,360字节，SHA-256：

```text
5ff6bb402b791f10b1565fef87ce4bd8b9f8d8927d603f0b903a8d2eba53ed6d
```

[验收](qualification/0478/0478-fairplay-resources-qualified.json)、
[源码/主机/实机重检](../scripts/tests/run-fairplay-resources.py)。
无新全盘扫描、Data fsck、触摸/GUI/120Hz验收；非全AKS、SEP、口令或全RAM擦除。

## 上游核对和下一步

当前[UniXNU组件表](https://github.com/UniXNU/UniXNU/blob/main/components.toml)
固定XnuPrelink为b74ed7c74a07f0eb5f81089c660905ecc4cabb7f；该版本仍按exact kernel
symbol绑定，并严格拒绝外部PC相对编码，没有发现FairPlay特殊装载路径。
[iPadOS17配方](https://github.com/UniXNU/UniXNU/blob/main/profiles/os/ipados-17.7.11-21H461/profile.toml)
也没有显式选择FairPlayIOKit/文本加密组件。主分支配方只是本日读取快照，不冒充
已固定提交或iOS16兼容证明。见[读取记录](qualification/0478/fairplay-upstream-review-20261101.json)。

供体确有上述组件，当前未选；但不能仅据缺组件认定本次退出根因。私有只读
prelink盘点发现23个非零symbol-relative addend和120个被严格门控拒绝的编码。
真正emit探针被拒绝，没有绕过门控或刷入试验组件；闭源代码/反汇编不发布。

接下来确认原版服务登记、实际IOKit/Mach初始化失败和回复生命周期，再修真正
缺失的接口。完整密钥/数据库/迁移/桌面目标继续；保留硬件密钥、加密Data、权限
及watchdog，不删库/重建key/降类/跳迁移/伪造回复。暂不扩展GPU/显示/输入，蓝牙延期。
