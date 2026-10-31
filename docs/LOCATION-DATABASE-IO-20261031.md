# 原版 locationd 数据库写入失败：0461–0462

0462 实机确认原版 locationd 反复写入
`/private/var/root/Library/Caches/locationd/cache_encryptedB.db-journal`
时，在带 `FOF_OFFSET` 的原始 write 路径返回 `EPERM(1)`。对应文件元数据是
root/root、0644、保护 class2。多次失败前紧邻原版 APFS 的
`AKS new_key failed for class = 2, error = e00002c7`。0462中C类数据库及WAL的
barrier操作真实成功；0461另有C类数据库/journal的fsync成功记录，不是所有APFS写入均失败。

之前 0460 已通过原始代码和真实线程栈定位到 SQLite IOERR10 后 sleep5 的重试分支；
0461 补上文件路径/类别，0462 再补失败读写结果。0462 的两次线程快照仍经过原版
重试位置。现在有实际失败操作、文件类别及 errno，不再仅凭“进程还活着”推断。

这不证明 Unix mode 错误，也不授权 chmod、删除数据库、移除 journal 或把 B 降成
C/D。原版 APFS 调用的本机文件密钥后端目前明确只支持 factory C/D，对 class2
请求返回 Unsupported；这是真实功能缺口。原版用户态还请求未实现的 selector10，
其精确 ABI 与 B 类公钥/密钥服务的关联仍须按固定供体审计，不能凭数字猜测。

**本轮没有实现 B 类密钥，没有修复数据库重试或完成迁移，更没有显示 SpringBoard。**
下一工程目标是接通真正的 B 类服务和生命周期，再用真实 journal 写入、原版迁移进度
及安全/存储回归来证明修复。见 [B 类开发要求](CLASS-B-BACKEND-NEXT-20261031.md)。

## 新增代码的边界

- 0346 在原版 open/open_dprotected、fcntl、fsync 的原清理路径之后观测返回值。
  精确匹配当前进程的完整 locationd 名称与固定 20H392 executable UUID；不改变 UID、
  参数、retval、errno、MACF、ACL、原版调用和清理流程。
- 路径复制最多512字节、控制字符清洗；只读路径/元数据，不读数据库内容、密钥或
  I/O 用户数据缓冲。最多64份 open、128份 FD 报告，IRQ/panic/不可抢占上下文拒绝。
- 0347 对经过原始 fileproc 清理的 read/write 错误补充同类观测；成功读写不查路径，
  原始 preparefile 拒绝也保留。普通 scalar/vector 与带偏移路径共享内部函数，
  `command` 是原 flags（FOF_OFFSET=1），不是自行推断出的用户态 syscall 名。
- `result` 对 read/write 观测固定为未采集的0占位，不表示实际传输字节；错误码是原返回。
  FD 路径/属性在原操作结束后重新引用查询，存在并发 close/reuse 的一般竞态边界，
  不是与原 write 原子取得的 vnode 快照；重复的同路径/类别/errno与原版 APFS 日志一并保留。
- 仍保留所有原版 daemon 崩溃、APFS 错误和 watchdog 行为；没有日志屏蔽、超时放宽、
  虚假 IOService、密钥回退或更改原版二进制。

## 验证

三个0461原文件、两个0462原文件去掉明确新增观测点后，均逐字节还原父版本；原始授权、
fcntl switch、syscall 返回与 fileproc 清理正文保持。完整生产观测片段在明确的内核 API
模型中执行 ASAN/UBSAN；0461的8项、0462的9项故意破坏保护的版本均实际编译并因行为
错误被拒绝，未把编译失败算作通过。主机模型不代表真实 VFS/APFS 执行。

两版均完成全量原生构建、真实编译宏、MACF/protection/audit 保留、原 HID207槽、
watchdog/storage/AKS/prelink与镜像回读。0461为325+4补丁，0462为326+4补丁，
全部5261源码文件各自与Mac实际构建输入精确一致，独立公开回放也通过。

两版均经过 boot_b Sending/Writing OKAY 和正常启动。原硬件 Root/nonce、原版MKB/
systembag、旧C/D冷读取与密文样本、RTC/NVRAM/defaults和启用中的原版watchdog回归通过，
各有超过300秒的心跳窗口；串口查询完成且无内核panic/查询超时。watchdog仍为软件
截止时间观察器，不是独立硬件WDT。0459人工触摸验收沿用，无新采样或物理120Hz证明。

完整资料见 [0461](qualification/0461/) 与 [0462](qualification/0462/)。本轮没有格式化、
重建任何现有密钥或替换 userdata，也没有新增全量镜像扫描/只读fsck验收。
