0449 在原版迁移包装进程内定位到了系统 App 迁移等待：120.298秒，SystemAppMigrator 在 synchronouslyCancelAllAppStoreRequests 的限时信号量等待；285.042秒，同一包装进程198/UID501、线程1737已经继续到 attemptToInstallSystemAppWithBundleID，经 IXAppInstallCoordinator cancelCoordinatorForAppWithIdentity 等待同步 NSXPC 回复。对应 installcoordinationd 在本轮反复崩溃，具体退出根因待查。不能把前一个限时等待称为永久死锁，也不能把进程存活称为迁移完成。

0335 只扩展既有正常态 MIG?：固定 launchd 子进程完整名称，并以原版包装可执行文件 UUID 拒绝截断名称的其他可执行文件；名称/UUID是诊断选择条件，不是安全授权。保持原 UI/PID1/backboardd 观察器与 USB 命令/panic 路径不变，按实际 UID 保留并释放任务引用，无暂停/寄存器改写/文件写入。

通过原 TASK_DYLD_INFO 读取64位公开 dyld 前缀与 image_info，最多512映像、8个 /System/Library/DataClassMigrators/ 模块、256字节ASCII路径、128条/16384字节加载命令。用户映射和加法范围检查、Mach-O/LC_UUID校验、前缀复读后才输出。前后前缀相同仍是 best-effort，不证明原子快照、无ABA或无加载/卸载。仅返回已校验的模块路径/base/UUID。

实机两份模块清单均含4个原系统插件。SystemAppMigrator 的运行时UUID 462f5fa733b037f28aa7aadf09475ac5 与只读原文件匹配（SHA256 88cde211c8aa344e011e204b5a05dee6bc3908e8721fbc5989c22508980e9401，71个ObjC方法/112个函数）。这是 vmaddr0 的 MH_BUNDLE，符号化保留其原地址基准。采样中的原版代码未改写，系统文件/缓存/密钥/镜像不发布。

全量原生构建、两个目标单元编译、314common+4host补丁重放与5261源码文件精确一致、原IOHIDDevice全部207槽通过。完整BSD/Mach生产函数的主机测试覆盖旧观察器和新dyld/UUID/边界/fault/引用路径；16个实际可编译的行为变异被拒绝，ASAN/UBSAN通过。任务/调度/映射/nofault/UUID API为明确模型，不代替硬件验证。USB源文件与0446已测正常/panic分派逐字节相同。

boot_b实机Sending/Writing均OKAY；原硬件Root/nonce恢复、MKB/systembag、C/D加密Data、NVRAM/RTC、defaults与原watchdog334.017秒回归通过。SpringBoard27/UID501在154.858/317.727秒都等待原BKSSystemShellService::_checkIn，尚非可用界面。selector35和A/B/passcode/SEP仍不支持，不提供虚假成功。

下一步追查 installcoordinationd 的首次实际退出原因与初始化所需资源；不能跳过迁移插件、取消原安装流程或屏蔽崩溃。120Hz为用户给出的硬件目标，目前FrontBoard曾上报60Hz；CPU16667us定时器不是物理vsync。真实120Hz、GPU/呈现/可用SpringBoard未验收；用户不在设备旁，真实触摸采样待回来，蓝牙延期。每日快速启动保持，最后Data只读fsck0416、完整System/SystemOS0341；本轮不重做全量内容verify。
