# backboardd 等待状态观测：0435

本轮只完成真实服务的有界等待状态观测，继续定位原版 CADisplay 初始化失败。没有屏蔽 SpringBoard/PreBoard 崩溃，也没有创建假的主屏对象、跳过断言或宣称 GPU/显示可用。

0322 增加固定 BBOARD? 命令，复用原 PID1 的任务/线程引用与内核等待快照逻辑。最多扫描512个进程、32个线程，每条保留内核栈最多16帧；仅读取等待且不在运行的线程，先释放锁再输出，不暂停线程、不读取用户内存、不修改寄存器或权限。USB 原 panic 分发条件保留，panic 中拒绝这种任务观测，FASTBOOT 和已有 FWLOG 路线不变。

0434 实机揭示诊断器错误地要求服务 UID=0，原版 backboardd 虽存活却没有被选择。因此这次诊断功能被拒绝验收；其正常安全/存储/watchdog回归单独保留。0323改为精确名称及launchd父进程选择并报告实际UID；0435实机确认PID57/UID501。它不是提升服务权限或绕过Sandbox。

0435先后获得完整线程快照。实际内核续接点包括 ipc_mqueue_receive_continue、workq_unpark_continue 和 ulock_wait_continue；主线程在等Mach消息。这些续接点不能区分用户态服务器端口、消息内容或具体缺失资源，也不能证明原版UI完成初始化。下一步需定位原用户态调用位置及RenderServer/MIG服务生命周期，不能凭单个等待点杜撰硬件依赖。

两套独立回放确认302 common+4 host补丁、5260个源码文件逐项一致；目标KDK及全量原生内核构建通过。全部207个原版HID绑定槽复核通过。主机测试直接核对完整当前源码与夹具，ASAN/UBSAN执行进程选择、身份、引用、锁、失败和容量边界，以及完整USB解析与panic拒绝；六个行为变异必须触发断言失败。调度、内存布局、I/O及重启硬件在主机测试中明确为替身，实机证据单独列明。

0434/0435正常原硬件Root/nonce、原版MKB/systembag、真实加密Data C/D、RTC/NVRAM、defaults与原版watchdog回归通过超过180秒窗口。没有生成新密钥/密钥包或格式化Data。正常启动继续development-fast；最近独立只读Data fsck为0416、完整System/SystemOS内容检查为0341，没有重复全量哈希。

S3908真实应用状态0及完整128字节报告配置/原版HID匹配保持；真实触摸采样按用户要求待其回来后进行，不能计为通过。音量键资格沿用0430，蓝牙仍延期到可用SpringBoard之后。
