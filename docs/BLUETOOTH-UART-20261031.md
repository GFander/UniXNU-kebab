# 真实 SE6 UART 字节通路阶段验收（2026-10-31）

蓝牙整体仍不可用。本阶段只接受 SoC 内部 UART 字节通路和恢复逻辑，后续必须让真实 QCA 控制器初始化，并通过 iOS 扫描、连接和传输数据才能宣布蓝牙完成。

0414 和完整内核重启后的 0415 各完成至少三次实机测试。每次 9 组长度 1/3/4/63/64/65/255/257/513，共发送和实际接收 1225 字节，逐字节比较一致。覆盖部分 FIFO 字、跨 FIFO、连续调用。最终独立只读快照确认 FIFO/引擎/IRQ 空闲、UART/RCG 恢复，六个 GPIO 配置和电平保持原值。

固定 GPLL0 只读核对为 LUCID、600 MHz、偶数输出分频2。USB诊断入口使用原版BSD发布的真实PID1状态，BSD尚未完成时在时钟、映射和锁操作前拒绝；两轮实机均验证该早期拒绝后正常启动、再执行真实字节测试。实际为硬件 FSM ACTIVE+LOCK，软件 OPMODE0；按上游硬件 FSM 路径判断，不修改 USB/UFS 共用 PLL。仅临时设置 SE6 子 RCG 为 7.3728 MHz，串口按上游 HALF_SAMPLING16、divider4 配置名义115200/8N1。没有外部线速测量，内部回环也不能代替外部 HCI 验证。

重复调用的崩溃转储读取先校验加载镜像的不可变头部，再限定读取原生控制台环形缓冲和最后MMIO记录。实际定位在第二次写GENI_FORCE_DEFAULT(0x20)。最终按端口生命周期仅初始化一次，状态只在真实写入完成后生效；每次实机均证明三轮真实收发只发生一次该硬件写入，未知是否完成会报错并隔离，底层意外重复请求也返回错误而非假成功。

实机还揭示 TX 水位 IRQ 在阈值恢复前确认会重新置起。最终实现等待CANCEL/ABORT完成及引擎inactive，在串口时钟运行时确认完成中断，再检查FIFO、恢复配置和时钟，最后清次级/主级IRQ并确认无待处理中断，最后恢复汇总路由。清理不明确时隔离后续 UART 写操作，保留明确错误；没有跳过 busy 或屏蔽错误日志。超时同时受单调时间和轮询次数约束。

288 common + 4 host 补丁完整回放，5248 源码文件一致。原生产字节逻辑主机模型 3434 个用例、13 个行为变异；原生映射/上下文边界及5个变异；ASAN/UBSAN和独立公开测试入口通过。MMIO、FIFO、时钟及屏障在主机测试中明确替身，不作为实机证据。KDK单元编译与全量构建，以及两轮原版用户态、Root/nonce、原版 system bag、C/D 加密 Data、defaults、原版 watchdog 超过180秒回归通过。

本次只改 UART/USB 命令入口和构建登记，存储、密钥、既有GENI触摸传输代码与0402相同。最近独立原版只读 Data fsck 为0404，本阶段没有重新运行；保留每日开发快速启动策略，不声称每次完整校验 System/SystemOS。

仍待完成：控制器供电资源和持有/释放；引脚及外部CTS/RTS；实际HCI ROM版本；匹配版本的patch/NVM下载；真实地址来源；IBS睡眠/唤醒和失败恢复；原版iOS用户态接口；实际扫描、连接、业务数据及重启恢复。没有生成地址、发布占位IOService或让bluetoothd假成功。长驻IRQ和完整电源生命周期也尚未验收。

原始测试内核 SHA256：`851642251a6b9c0517468124f50efec34e27c96673e50d310c35ed4c17378715`。公开收据见 [qualification/0415/README.md](qualification/0415/README.md)。硬件密钥、加密 Data 和原版 watchdog 保持；A/B、AKS17/35、其余daemon、GPU/显示/输入和可用SpringBoard仍未完成。

主参考为固定 LineageOS 源码 [msm_geni_serial.c](https://github.com/LineageOS/android_kernel_oneplus_sm8250/blob/98a7970af93b7e01ee77156e7fa0eb7e059a57f0/drivers/tty/serial/msm_geni_serial.c)、[vendor GENI helper](https://github.com/LineageOS/android_kernel_oneplus_sm8250/blob/98a7970af93b7e01ee77156e7fa0eb7e059a57f0/drivers/platform/msm/qcom-geni-se.c)、[GCC](https://github.com/LineageOS/android_kernel_oneplus_sm8250/blob/98a7970af93b7e01ee77156e7fa0eb7e059a57f0/drivers/clk/qcom/gcc-kona.c) 和 [LUCID PLL](https://github.com/LineageOS/android_kernel_oneplus_sm8250/blob/98a7970af93b7e01ee77156e7fa0eb7e059a57f0/drivers/clk/qcom/clk-alpha-pll.c)。新代码按寄存器事实独立实现。
