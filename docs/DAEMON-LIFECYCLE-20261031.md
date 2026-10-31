# 0387：完整进程快照与真实退出证据

本小目标只验收诊断准确性，不宣称所有 daemon 或蓝牙功能修好。0290 补丁将普通启动的 PROC? 上限由64调整为512，补记选定进程的正常退出，并记录 AKS 输出缓冲区大小。没有改变进程退出、权限检查、watchdog 或密钥结果；panic 路径仍拒绝进程遍历。

原生内核 SHA256 `7c2ef66249ccf420160f09a3c6111f541014e402d73abf695d5622424388251d`，18883416字节；269 common +4 host，5239源文件与实际构建输入逐一一致。完成 KDK 编译和全量构建。普通固件已刷写实机，保留硬件根密钥、原版 MKB_INIT、加密 Data C/D、defaults 生命周期和原版 watchdog 心跳。最近独立只读 Data 检查仍为0385；本次没有把诊断改动说成新的 fsck 结果。

两次完整 PROC? 间隔76秒，分别158、157个进程，均未触及512上限。ReportCrash、coreduetd、securityd、tccd、navd、lockdownd、backboardd、locationd、watchdogd、mediaserverd、configd 在两个快照中 PID 相同。这只证明观察窗口内的进程存在，不能替代服务功能测试。

bluetoothd、fairplayd.H2 实际 signal=0/status=0，属于主动正常退出；SpringBoard 仍有真实 SIGTRAP。符号定位补入原版 dyld export trie，并对保存的返回地址按调用点回退4字节，修正此前只按局部符号寻找最近地址造成的误定位。bluetoothd 实际经过 `_errx` → `_verrx` → `__exit`，不是凭错误的最近符号推断随机数函数失败。

蓝牙完整功能仍未实现，详见 [硬件接入和验收要求](BLUETOOTH-BRINGUP-20261031.md)。其他真实缺口包括 AKS 17/35/102/103、A/B文件密钥和后续 daemon 行为。完成这些后，按最新构建重审 GPU/显示/输入指南并开发到可用 SpringBoard。全目标仍进行中。
