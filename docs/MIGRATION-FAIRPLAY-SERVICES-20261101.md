# 0479：原版 FairPlay 确实查询了缺失的内核服务

2026-11-01。继[0478文件探测](MIGRATION-FAIRPLAY-RESOURCES-20261101.md)，0359
补原版服务匹配、open、iterator-next的结果元数据。**取得实际消费证据：原版
fairplayd.H2查询com_apple_driver_FairPlayIOKit，返回成功但迭代器为NULL，随后退出。**
当前prelink闭包没有该原版组件；现在有具体接口需要补齐，但装载/初始化、唯一
退出因果、迁移和桌面完成仍未证明，不以注册假设备或伪造回复替代它。

## 实机结果与边界

- 首原PID87在六次已知ENOENT后，实际plural matching查询上述类。原kr=0，
  iterator-created=false；这是NULL输出，不是观察器额外推进后的“空迭代器”。
- 7/32事件均完整、无copy错误/截断/字符替换。只有上述一次kind5，未观察到
  kind1/2/3/4/6。不代表全IOKit/Mach覆盖或后续每次启动同因。
- 只有kind6观察原iterator-next返回，不额外next/retain/release；kind5 aux是
  iterator创建，绝不是匹配设备数。事件ticket排序不是原子RPC配对。
- 原BSD总138、末32次及首PT31成功/NOATTACH置位/debugger能力0保持，原退出
  signal0/status0。不能保证补组件后没有下一处真实错误。

## 实现及分层验证

两源新增观察：原singular/plural匹配已解析且仍存活的字典，只复制IOProviderClass
或IONameMatch的字符串；原open只看service metaClass/result/connect_type；原
iterator-next只看原返回对象metaClass/ret。早期main_port拒绝、坏字典和MACF
open拒绝有原结果观察，其余早期guard/通知/OOL递归路径不冒充完整覆盖。

固定原UUID/name/UID501/currentproc/首PID，先eligibility快照，预留再检查；共享
封存/32事件/128字符预算。锁外复制/净化/打印两短行，不读属性、文件值或密钥。
所有原代码行及MACF/ACL/Sandbox/ptrace/退出结果保持，原加密及硬件Root源未改。

| 层次 | 本轮证据 |
| --- | --- |
| 生产C helper | 789检查、35实际编译行为变异；含旧环/资源回归与新资格/结果/类型/边界/预算 |
| 生产C++观察体 | 7字典模型检查、3编译行为变异；原class优先、name回退、非字符串/NULL、不符合身份时不访问字典 |
| 模型范围 | proc/UUID/锁/调度/copyinstr及IOKit字典/对象明确为模型，非真实内核并发或完整MIG集成测试 |
| 原生 | 338 common+4 host，5280源逐项回放/postbuild相同；2TU0error、36/14 warnings；10实际callback边及matching/eligibility引用 |
| 实机 | 固定DRV/PROC/UI/MIG/BBOARD/MIG完整；实际类查询/NULL迭代器及原退出，watchdog321.176秒有限窗口 |
| 加密保留 | 原A/B/C/D旧key、负载/密文及securityd6–11首次UNWRAP保持，A锁定已打开cached/direct读写零传输拒绝/解锁恢复，Root/nonce/bank/systembag/RTC/NVRAM/defaults保持 |

原生18,965,904字节，SHA-256：

```text
69c31ddc43501bcd7bc15ec2d9b276413a06542acef7117070943b4c33c92cc0
```

[封存验收](qualification/0479/0479-fairplay-services-qualified.json)、
[可重跑源码/生产体/证据校验](../scripts/tests/run-fairplay-services.py)。
无新全盘扫描、Data fsck、触摸/GUI/120Hz验收，非全AKS/SEP/口令/全RAM擦除。

## 下一步：原版组件的受控 prelink

上游iPadOS17不显式选择FairPlayIOKit，不能替代本20H392的实际消费者证据。
现有严格loader对本组件的symbol-relative addend和不分类编码拒绝，不能跳检查
就声称接入。只读盘点：34个指针位置涉及23个非零symbol-relative目标；120个
被拒编码都紧随原BR（119个）或B（1个），已知直接分支/被记录指针/导出符号
没有指向它们。**不等于证明间接控制流不可达或register dataflow安全。**

目前只准备完整donor/text哈希、符号/位置及逐word哈希元数据，未改生产loader，
未发出或刷入新组件。闭源原码、完整反汇编和私有Data不发布。后续须保持原字节、
严格核对允许的重定位，区分源码/链接验证与实机原服务初始化、真实回复验证。
不能用任意nearest-symbol、无条件跳门控、假成功设备或替代daemon掩盖失败。

完整密钥/数据库/迁移/桌面目标继续；保留现有Root/nonce/bank/systembag和加密Data，
不删库/重建key/降类/跳迁移/关闭watchdog，暂不扩展GPU/显示/输入，蓝牙延期。
