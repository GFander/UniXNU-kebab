# 0477：排除原版 FairPlay 防附加失败假设

2026-11-01。继[原版服务身份](MIGRATION-SERVICE-IDENTITY-20261101.md)，
0357增加首个原版fairplayd.H2进程的有界BSD返回观察。
**已排除PT_DENY_ATTACH失败；真实初始化退出、系统迁移和桌面登记尚未修复。**

## 实机结论

- 原UUID/UID501/name匹配的PID87共记录138次完成的BSD调用，封存最后32次
  （107–138），另保留首次ptrace26/request31，序号100。
- PT_DENY_ATTACH返回error0，实际P_LNOATTACH置位、P_LTRACED未置位，
  PE_i_can_has_debugger为0。因此不改原ptrace、防附加或debug policy。
- 保留环内open出现一次ENOENT，openat两次ENOENT；尚未记录路径，不能把
  正常配置探测直接当作退出原因。最后workq_kernreturn为EJUSTRETURN(-2)，
  属于内核内部返回控制，不把它当作用户错误。首次原退出仍signal0/status0。
- 本观察不覆盖Mach/IOKit调用、完整用户栈或每次重启的退出原因；不证明原服务
  从未短暂登记，不证明上述ENOENT是根因。迁移/system-shell等待仍在。

## 实现与证据范围

原systemcalls.c两个实际返回点新增void observer，kern_exit.c原退出观察旁新增
一次封存；原两个编译单元的代码行全部保留。精确UUID/UID/name/current_proc
过滤，首PID锁定，32环及首deny固定保存；退出一次封存。进程锁与环spin锁
分开，锁内不打印/睡眠/读取用户数据；锁外输出短行。不读取参数/结果缓冲、
硬件密钥或消息内容，不改变syscall/AUDIT/MACF/ptrace/exit结果。

| 层次 | 已验证范围 |
| --- | --- |
| 主机 | 生产helper114检查、16个实际编译行为变异；8线程并发、身份拒绝、环覆盖/封存/饱和、状态及224B输出预算 |
| 模型边界 | kernel proc/UUID、调度、锁和生命周期仍是明确模型，不冒充内核实测 |
| 原生 | 336 common+4 host、5280源逐项回放/postbuild相同；两个TU0error，27/14 warnings未屏蔽；三个真实callback调用边核对 |
| 实机 | 固定DRV/PROC/UI/MIG/BBOARD/MIG实际完成，原退出环完整封存；watchdog321.572秒 |
| 加密回归 | 原A/B/C/D密钥/旧负载/密文和securityd6–11首次UNWRAP保持；锁定A原已打开cached/direct I/O拒绝且零传输，解锁恢复；Root/nonce/bank/systembag、RTC/NVRAM/defaults保持 |

原生内核18,981,944字节，SHA-256：

```text
2d605e3632671201dc20dd9c67184fd7d6dbda28e0134a02b76f415d0d72d975
```

[封存收据](qualification/0477/0477-fairplay-syscalls-qualified.json)、
[原始日志](qualification/0477/0477-media-boot.log)、
[可重跑校验器](../scripts/tests/run-fairplay-syscalls.py)。
syscall名称只注释固定原syscalls.master表，非独立编译分支证明；条件enosys
回退不覆盖命名实现。381为__mac_syscall、372为thread_selfid。

未重新全盘扫描/Data fsck/采触摸/测120Hz，不声称全AppleKeyStore、SEP、口令或
全RAM擦除。kern_exit新增观察代码，不能称所有安全文件逐字节不变；原加密源
未变，原syscall/安全/退出结果保留。

下一步核对原版服务初始化的真实资源/接口及登记生命周期，保留硬件密钥、加密
Data、权限和watchdog；不跳过服务/迁移、不删库/重建key/降保护类，不伪造回复。
暂不扩展GPU、显示、输入，蓝牙延期。完整目标继续进行。
