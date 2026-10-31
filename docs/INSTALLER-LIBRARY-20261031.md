0452 修复了原版 installcoordinationd 初始化数据目录时的 EACCES；0453 用相同内核完整重启验证了持久性。原版 SystemAppMigrator 已越过旧 cancelCoordinator 同步等待，实际进入 IXPlaceholder / IXCopyItemAtURL / copyfile。系统 App 安装和迁移尚未完成，SpringBoard 仍未验收为可用界面。

0450 首次真实退出证据显示 signal6、reason18/2：创建数据目录返回 POSIX13。原只读安装服务 SHA7d0e9a07620988920586425c3b43e5be448df757abaffab524efc028a8b61119、UUID95fcf56cc4483c6d83ca914de612df63；退出调用栈落在 IXGlobalConfiguration createDirectories+0x4f8 的原 _os_crash_msg。服务仍以原 launchd mobile 用户运行，原用户记录 UID/GID501/501；原目录创建调用明确使用 mode0751/class4。

0451 两次只读快照确认固定路径 /private/var/containers/Shared/SystemGroup/systemgroup.com.apple.installcoordinationd/Library 的 UID/GID 为4294967294、mode0000、flags0、class0；InstallCoordination 子目录不存在。异常目录最初形成的历史原因尚未证明，不推断为所有目录或全局 vnode ABI 损坏。

0336 只把截断后的原 p_comm installcoordinat 加入已有首次真实退出观察名单，保留正常退出、原信号/崩溃和重启行为。0337 在既有正常态 MIG? 内查看七个固定目录的元数据，无任意路径参数/目录枚举；拒绝任意符号链接，namei/vnode引用释放后才打印，仅将 supported 属性视为有效。诊断字段清零覆盖失败路径；未改变 USB/panic 分派。

0338 在原 PID1、受认证 Data/硬件密钥/已完成模板门控下，仅对上述固定目录的精确异常元数据调用原 vnode_setattr。kdi_lookup 继续执行 NAMEI_NOFOLLOW_ANY 和精确 Data mount 校验；仅设置 mobile UID/GID、0751和class4，复读验证后执行原 VFS_SYNC 和 UFS cache sync。保留目录对象/内容，不显式删除ACL、不递归改权限、不替换原服务、不改密钥或对A/B/SEP返回虚假成功；缺失目录和其他元数据保留给原初始化流程。

0452 在33.737秒 legacy1/error0，实际读取501/501/0751/class4；原服务随后自行创建同样正确元数据的 InstallCoordination 子目录。到394.169秒的封存窗口只拉起一次安装服务、未见其退出/崩溃；实际 SystemAppMigrator 进入 setPlaceholderPromiseForCoordinator 和 copyfile，证明旧同步请求已返回。0453 完整重启在33.704秒 legacy0/error0，没有再次改写，目录和原服务新建的子目录均保留。安装服务再次只启动一次且未见退出/崩溃，原插件走到 IXPlaceholder setConfigurationCompleteWithError 的同步回复等待；这不证明配置或安装已完成。到364.114秒原有功能回归通过。

两轮原硬件 Root/nonce 恢复、原 MKB/systembag、C/D加密Data、RTC/NVRAM、defaults和原watchdog保留；A/B/passcode/SEP/selector35仍未实现。317common+4host/5261源码与编译清单逐字节一致；0450/0451也分别完成315/316+4重放。原IOHIDDevice207槽全部核对。完整生产函数模型测试43例、9个实际可编译行为变异拒绝，ASAN/UBSAN通过；VFS/lookup模型不代替硬件持久性证明。公开 runner 固定源码/函数/fixture哈希，并核对未变的 USB 和已有 Data nofollow/mount 契约。

0452 的 copyfile fstatx/listxattr 曾实际进入原 ACL evaluator，再到 kauth_cred_getguid / cache_lookup / resolver_submit；函数位置在变化，不能只据栈断言永久死锁。0453 当前调用方则在 setConfigurationCompleteWithError 等待同步回复，仍需追踪实际接收端，不能把新的等待直接归因同一ACL路径。下一步核对原iOS身份解析契约及接收端资源，不删除ACL或绕过授权。GPU/真正呈现/120Hz仍未完成；120Hz是用户硬件目标，现有软件60Hz/16667us定时器不是物理vsync证据。真实触摸等用户回来，蓝牙延期。日常快速启动保持，System/SystemOS未改；无格式化/分区变更/全量System内容verify重做。
