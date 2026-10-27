# kebab 移植状态记录

本文件按时间顺序记录关键结论；面向读者的总览见仓库根目录的 `README.md`。

## 一、引导路径（已验证）

```
ABL → boot_b 镜像 → Mu-Silicium UEFI → UniXNU 菜单 → DarwinBoot
    → ExitBootServices → 内核 _start（x0 = boot_args 物理地址）
```

刷机必须写 `boot_b`：固件固定从该槽位读取载荷卷（偏移 `0x800000`）。

## 二、内核进度

已验证到达（真机日志）：

```
start_first_cpu → arm_init → PE_init_platform → ml_parse_cpu_topology
arm_vm_init（正式 pmap，bootstrap TTBR0 已关闭）→ pmap_bootstrap
machine_startup → kernel_bootstrap → load_context（首次线程切换成功）
kernel_bootstrap_thread:
    idle_thread_create / sched_startup / thread_daemon_init / thread_call /
    thread_bind / mapping_adjust / clock_service_create
kdp_init / coredump / bootprofile_init / sysctl（phase 15）
PE_init_iokit（IONVRAM 解析）
early_boot（phase 16）+ 内核 PRNG 注册
mac_policy_initmach / vnguard_policy_init / dtrace_early_init
trust_cache_init（缺 AMFI/Image4 时降级）
OSKextRemoveKextBootstrap / machine_lockdown / kernel_do_post
PE_lockdown_iokit（内核自身初始化收尾）
KebabPlatformExpert 匹配成功，IOKit 开始建 nub
kperf_init_early（ml_set_max_cpus 死锁已修）
bsd_init() 进入 BSD 层
pthread_init（内置 libpthread-514.100.2 已注册）
vfs_mountroot → UFS 驱动找到 userdata（229043 MiB）并发布 ufsroot0
文件系统 ← 当前阻塞（ENODEV：内核里没有 HFS/APFS）
```

## 三、已修复的问题（按发现顺序）

1. **帧缓冲地址** `0xB8000000` → `0x9C000000`（start.s、控制台、映射三处统一）。
2. **设备树指针** DarwinBoot 给的是内核虚拟地址；早期在恒等地址运行时需要换算。
3. **`wfe_timeout` 除零 panic** —— 时基为 0 的连锁反应，根因是设备树没解析成功。
4. **ADT 缺 `name` 属性重复** —— 生成器 `node()` 已自动加 `name`，重复会被
   DarwinBoot 判为 `COMPROMISED_DATA` 而拒绝启动。
5. **KVA 映射偏移 16 MiB** —— 16 KiB 粒度下 32 MiB 块的低 25 位来自虚拟地址，
   链接地址必须与物理加载地址低位一致；内核链接地址改为 `0xfffffe0006004000`。
6. **ARMv8.4 `mrs x, DIT`** —— SM8250 是 ARMv8.2，该指令未定义；第一次上下文
   切换因此崩溃。对 KEBAB 关闭 `__ARM_ARCH_8_4__`。
7. **crypto 表为 NULL** —— 没有 Apple 的 crypto kext；改用 BearSSL 摘要实现 +
   树内 HMAC-SHA256 DRBG + SplitMix64（kmem 分配器）。
8. **ADT 缺节点** —— `interrupt-controller = "master"`、`timer`、`pram`、
   `/chosen/embedded-panic-log-size`，补齐后 `ml_init_timebase` 才执行。
9. **异常向量被覆盖** —— 自建向量表会吞掉 IRQ；已交还 XNU 的
   `ExceptionVectorsBase`。
10. **trust cache 依赖 AMFI** —— 缺 kext 时降级为空运行时而不是 panic
    （参考 negroni 移植的做法）。

## 四、调试手段

* **面板控制台**：`kprintf` 直接镜像到帧缓冲；手机没有可用串口。
* **`KEBAB EARLY n: <label>`**：`arm_init` 与引导线程的关键阶段标记。
* **XNU panic**：原生 panic + stackshot + 栈回溯；`lr` 地址可用
  `llvm-objdump -d --arch-name=arm64 --start-address=<lr-0x40>` 对照内核符号解析。
* **启动阶段跟踪**：`startup_debug=1` 会打印每个注册项（已对 KEBAB 过滤掉逐条
  输出，只保留 `reached phase N`）。

## 五、探针的教训

早期用内联汇编在固定像素位置画"面包屑"，但硬编码了恒等映射地址
`0x9C000000`；`arm_vm_init` 关闭 TTBR0 后该地址不再映射，探针自身触发数据
异常并停机，看起来像内核卡死。**任何内联探针都必须从 `early_fb` 读取运行时
地址**，且探针本身要先验证不会崩。

## 六、阶段 6 与阶段 7（2026-10-26 完成）

官方七阶段中的 1-6 全部通过，阶段 7 的核心门槛也已跨过。

### 存储与根卷（阶段 6）

| 日志 | 含义 |
| --- | --- |
| `KEBAB UFS: "userdata" on LUN 0 ...` | 复用 UEFI 协商好的 UFS 链路 |
| `BSD root: ufsroot0, major 1, minor 0` | `bdevsw` 驱动发布分区 |
| `hfs: mounted KebabRoot on device b(1, 0)` | 内置 HFS 挂上真实块设备 |
| `KEBAB: root volume is not sealed (error 25); continuing` | 无 SSV 链路，降级放行 |
| `devfs_kernel_mount` 无报错 | 根卷里必须有 `/dev` 目录，否则 ENOENT |

### 首个用户进程（阶段 7）

`/sbin/launchd` 是一个手工构建的静态 arm64 Mach-O（见 `rootfs/launchd` 与
`scripts/make-init.py`）。exec 全流程的探针 `P1`-`P9` 全部通过，最后看到
`KEBAB: syscall enter` —— 内核已把控制权交给用户态，用户代码发出了第一个系统调用。

### 这一段踩过的坑

1. **`__PAGEZERO` 必须覆盖低 4 GiB。** arm64 的 `load_machfile()` 要求
   `vm_map_has_hard_pagezero(map, 0x100000000)` 成立。用 `-pagezero_size` 缩小它
   反而会让 exec 返回 `LOAD_BADMACHO`。ld64 的默认布局就是对的。
2. **AMFI 是 exec 的必经之路。** `IOTaskHasEntitlement()` 无条件解引用 `amfi`
   接口指针，没有 AMFI kext 时会在 `+0x1b0` 处数据异常。`0088` 注册一个"什么都不
   授予"的内置实现后通过。
3. **`printf` 会被图形控制台暂停吞掉。** `_cnputs()` 在
   `disableConsoleOutput` 为真时直接返回，而 `kprintf` 走另一条路径。于是
   `load_init_program` 的加载提示和我们所有 `printf` 探针全部消失，看起来像卡死。
   `0090` 修掉了这一点——**这是本次最长的一次误导**。
4. **PID 1 没有继承任何文件描述符。** 真正的 `launchd` 自己打开
   `/dev/console`；测试程序必须先 `open()`，否则 `write(1, ...)` 返回 `EBADF`
   并静默空转。
5. **失败必须留证据。** 早先的测试 init 在 `open` 失败后原地空转，把 errno 完全
   丢掉。现在改为逐级降级后 `exit(0)`，让内核 panic 把原因打出来。

## 六之二、用户态输出打通（2026-10-26 03:53）

```
bsd_init: bsd_do_post - done
kebab: console open
kebab: userland alive
```

阶段 7 的最后一项验收（用户态有可见输出）达成。

### 根因：PAN 从未生效

`__ARM_PAN_AVAILABLE__` 只由 Apple 自家 SoC 头文件（`H13.h`、`VMAPPLE.h`）定义。
缺了它：

- `osfmk/arm/arm_init.c` 里的 PAN=1 初始化整段不编译
- `osfmk/arm64/copyio.c::user_access_enable()` 编译成空操作
- `proc_reg.h` 的 `PSR64_KERNEL_DEFAULT` 里没有 `PSR64_PAN`

而 `SCTLR_EL1.SPAN = 0` 时，**异常进入 EL1 会由硬件把 PAN 置 1**。于是：

1. 每个系统调用进入内核时 PAN 已是 1
2. `_bcopyout` 用普通 `stp` 写用户地址 → permission fault
3. `sleh.c::handle_kernel_abort()` → `arm_fast_fault()` 发现 PTE 可写（AF=1、AP 正常）
   → 返回 `KERN_SUCCESS` → 提前 return → **重试同一条 stp** → 再次 fault
4. 表现为"系统调用既不返回、也不 panic、也没有缺页日志"

### 修法

在**板级头文件** `pexpert/pexpert/arm64/KEBAB.h` 里声明：

```c
#define __ARM_PAN_AVAILABLE__     1
```

一处声明让 PAN 的整个生命周期（初始化、线程切换检查、copyio 开关）重新生效。
只放在 `copyio.c` 里是不够的 —— 那样只有那一个编译单元知道 PAN 存在。

### 这一段学到的

- 排查时**探针位置**必须先核对代码路径（我把探针放进了 `O_CREAT` 分支，白烧一轮）
- **"没有日志"不等于"没进那段代码"**：`arm_fast_fault` 的成功提前返回会跳过所有后续探针
- 只在"看起来相关"的 `.c` 文件里定义平台宏，会静默漏掉别的编译单元
- 用户态第一次能写内存后，`write()` 的 `uiomove` 才真正开始工作 —— 之前所有 `open`/`gettimeofday`
  的挂起都是同一个原因

## 六之三、USB CDC-ACM 调试串口（进行中）

目标：把内核日志经 DWC3 设备模式送到主机，主机侧直接是一个串口（Windows 的
`USB 串行设备 (COMx)`），不再靠拍屏幕。补丁 `0091`（驱动）+ `0092`（接线），
`kebabusb=1` 才启用。

### 已经确认走通的部分

```
KEBAB USB: reset done after N ms, GCTL ... GSNPSID ...
KEBAB USB: event buffer at 0x...
KEBAB USB: VBUS override, HS ... -> ... , SS ... -> ...
KEBAB USB: running (DSTS ...), DMA at ...
KEBAB USB: event thread started, waiting for the host
KEBAB USB: connected, speed 0, ep0 packet 64     ← 主机检测到设备，高速
KEBAB USB: configured, notify=5 bulk_out=2 bulk_in=3
```

主机侧曾经报 **"未知USB设备（设备描述符请求失败）"** —— 与内核日志里
`ep0 cmd 0x6 status 1` 对应。

### 逐条对照 Linux drivers/usb/dwc3 修掉的协议错误

| 现象 | 根因 | 依据 |
| --- | --- | --- |
| 每个 `STARTTRANSFER` 都返回 status 1 | **TRB 地址高低 32 位传反**：`DEPCMDPAR0` 应是低半、`DEPCMDPAR1` 是高半 | `dwc3_gadget_ep_cmd()` 里 `params[0] = lower_32_bits(trb)` |
| 状态阶段提交即失败 | 必须在 **XferNotReady(status=2)** 之后才提交状态 TRB | `dwc3_ep0_xfer_complete()` |
| 有数据阶段的传输收不了尾 | 三阶段传输的状态阶段要用 **STATUS3**，两阶段用 **STATUS2** | `dwc3_ep0` 的 `ep0_next_event` |
| 打开串口时卡住 | `SET_LINE_CODING` 的 7 字节 OUT 数据阶段没有人接收 | `f_acm.c` |
| 复位后第一个控制传输失败 | 总线复位会丢掉端点配置，必须重建 EP0（DEPSTARTCFG → SETEPCONFIG → SETTRANSFRESOURCE → DALEPENA） | `dwc3_gadget_reset_interrupt()` |
| 启动即 panic | `kmem_alloc` 用 `VM_KERN_MEMORY_NONE` 做 wired 分配的 tag 会触发 `vm_resident` 断言 | `vfs_cprotect.c` 的写法 |
| 分配后第一件事就崩 | `lck_mtx_alloc_init(0, 0)` 传空锁组会断言 | `lock_mtx.c` |

### 关键硬件事实（来自 kebab 的设备树）

- DWC3 基址 `0x0A600000`（与参考移植 negroni 的 SM8450 相同）
- Qualcomm QSCRATCH 在 `0x0A6F8800`，HS_PHY_CTRL 偏移 `0x10`、SS_PHY_CTRL `0x30`；
  曾经误用 `DWC3+0xC100`，那会打到 GEVTEN/GSTS
- 触摸是 Samsung **S6SY761**（QUPv3 SE13，I2C 0x48），与 OnePlus 9 的 S6SY792 同族
- 面板有 4 家供应商（ana6705 / ana6706 / dd305 / samsung_oneplus），
  所以显示要走"把 GOP 帧缓冲包装成 IOMobileFramebuffer"的路线，而不是逐面板写 DPU

## 七、待办

近期顺序：

1. 让 `/sbin/launchd` 真正打印到控制台（`open("/dev/console")` 的返回值待确认）。
2. AMFI / Image4 最小占位继续扩展（entitlement 查询目前一律返回失败）。
3. 根卷换成大小写敏感的 HFSX + 日志，或保持"脏卷直接读写"的策略。
4. IOKit 平台驱动匹配（显示 / UART / USB）。
5. 中断链路端到端验证（GIC + 虚拟定时器）。
6. 多核（PSCI + GIC 重分发器）。
7. 扩大内核可用内存窗口（当前仅 256 MiB）。

## 八、临时调试补丁

`patches/xnu-8796-kebab/0091-kebab-debug-probes.patch` 是**纯脚手架**：它只加
`printf`，不改变任何行为。移植进入稳定期后应当直接删除该补丁，其余补丁不受影响。

## 八、USB 调试串口与 panic 捕获（本轮）

目标是彻底摆脱"崩溃只能拍照"。结论：**正常日志与 panic 报告都已经能从 USB 读到**，
设备模式串口成为日常调试通道。

### 走通的部分

* DWC3 设备模式 + CDC-ACM：枚举、控制请求、bulk 双向全部正常，Windows 与 macOS 双主机验证
* 内核日志经 16 KiB 环形缓冲流到主机
* **panic 报告完整送出约 8.9 KB**，含栈回溯与 Stackshot
* 显示驱动 `AppleCLCD` 启动，发布 IOSurface

### 关键教训（按发现顺序）

| 现象 | 根因 |
| --- | --- |
| 每个 STARTTRANSFER 返回 status 1 | 缺 `DEPSTARTCFG`，控制器还没建立端点资源 |
| 提交后设备进 EDL | **TRB 地址高低位传反**（`DEPCMDPAR0` 是高半，见 `drivers/usb/dwc3/ep0.c`）|
| 复位后第一个传输失败 | 总线复位会丢弃端点配置，必须重建 EP0 |
| 控制传输的状态阶段永远不完成 | 必须等 `XferNotReady(status=2)` 再提交状态 TRB |
| `SET_LINE_CODING` 卡住主机重试 | OUT 传输容量要按最大包长对齐（64），不能只给 7 字节 |
| 数据端点全死 | bulk 缓冲要按包长（512）对齐，`0xB00` 只对 64 对齐 |
| 端口能开但收不到日志 | 日志钩子被写在 `#if !defined(KEBAB)` 里，编译器整个丢掉 |
| panic 文字一个字节都没有 | 钩子挂在 `_cnputs`，而 panic 走 `_cnputc` 的无缓冲通路 |

### 仍然开放

* panic 后重新连接：`haltRestart` 钩子生效，泵循环在跑，但主机无法重新配置端口
* KDP over CDC-ACM 交互式调试器
* `debug=0x14f` 未能停在调试器，需要运行时探针确认 `panicDebugging` 与 panic options

## 九、触摸驱动（S6SY761）

补丁 `0098-kebab-touch.patch`：新增 `bsd/dev/kebab_touch.c`，并在 `bsd/conf/files`
与 `bsd/kern/bsd_init.c` 接线。`kebab_touch=1` 才启用；它和 USB 串口在同一个位置
（`bsd_init` 开头）启动，两者可以单独开关。

### 地址来源（设备自己的固件，不是猜的）

* **SE13 = `0x00A94000`**（长度 `0x4000`）：设备树 `/soc/i2c@a94000`，
  `reg = <0xa94000 0x4000>`、`compatible = "qcom,i2c-geni"`、`status = "ok"`；
  子节点 `/soc/i2c@a94000/sec-s6sy761@48` 就是触摸芯片，
  `reg = <0x48>`、`chip-name = "SY771"`、`touchpanel,tx-rx-num = <0x10 0x24>`、
  `touchpanel,panel-coords = <0x437 0x95f>`（1079x2399）。这棵树取自本机保留的
  LineageOS boot 镜像 `kebab-repodtb-losramdisk.img` 里的 DTB，未做改动。
* **QUPv3_1 包装器 = `0x00AC0000`**（长度 `0x2000`）：上面 i2c 节点的
  `qcom,wrapper-core` 指向 `/soc/qcom,qupv3_1_geni_se@ac0000`。
* **TLMM = `0x0F000000`**：`qupv3_se13_i2c_active` 把 `gpio36`/`gpio37` 复用到
  `qup13`；触摸节点的 `reset-gpio` 是 38、`irq-gpio` 是 39。

### 已经实现

* 映射 QUPv3_1 包装器与 SE13，按 Linux geni 驱动的顺序初始化：清中断、IO 模式、
  FIFO 模式、打包配置、SCL 计数器（默认 100 kHz，`kebab_touch_hz=400000` 可换）
* 用 Synaptics TCM 的 identify(0x02) / boot info(0x10) / app info(0x20) 读身份，
  打印版本、最大坐标、对象数
* 发一份固定的触摸报告配置（每个活动对象 5 字节：index 4 / class 4 / x 12 /
  y 12 / z 8 位），内核线程每 5 ms 轮询，解码第一根手指并按约 200 ms 限速打印
* `kebab_touch_raw=1` 时转储 SE 寄存器、TLMM 引脚状态和原始报文

### 已经验证

* 只能验证到**"能编译"**：`build-local.sh` 产出
  `build/xnu.obj/DEVELOPMENT_ARM64_KEBAB/kernel.development.kebab`
  （16 MiB，17,049,296 字节），本文件没有 warning
* 全部 78 个补丁按文件名顺序应用到 `xnu-8796.141.3`（`1b191cb`）干净通过：**78/78**

### 还没有确认的硬件事实

* **引导链有没有给 QUPv3_1/SE13 开时钟、有没有把 GENI 协议选成 I2C**。驱动只读
  `GENI_FW_REVISION_RO` 的协议位；读到 0 或 `0xffffffff` 说明没有时钟，这不是软件
  能绕过的（GCC 分支时钟不在本驱动里）
* **芯片是不是真的说 TCM**。厂商树里这个节点用的是 Samsung `sec_ts` 驱动，命令集
  不同；TCM 是本驱动选择去试的那一套，第一次上机要靠原始报文判断
* 复位极性，以及 mux/pull/驱动强度是否与设备树一致（本驱动按设备树的
  `qupv3_se13_i2c_active` 写）
* **中断没有接**：只轮询，`irq-gpio` 39 没有使用

### 用启动参数逐条探测

| 参数 | 作用 |
| --- | --- |
| `kebab_touch=1` | 启用驱动（必需）|
| `kebab_touch_raw=1` | 转储 SE/TLMM 寄存器与原始报文，用来判断协议 |
| `kebab_touch_se=0x...` | 覆盖 SE 基址 |
| `kebab_touch_pins=0` | 跳过 TLMM 复用 |
| `kebab_touch_reset=0` / `=2` | 跳过复位脉冲 / 用相反极性 |
| `kebab_touch_hz=400000` | 100 kHz 换成 400 kHz |

第一次上机先看两行：`KEBAB TOUCH: wrapper QUP_HW_VER ...` 与
`KEBAB TOUCH: ... protocol ...`；它们决定后面的输出有没有意义。若芯片没有 TCM
回应，驱动不会启动轮询线程，而是直接打印提示。

## 十、serial KDP over CDC-ACM

把 USB CDC-ACM 控制台同时当作串行 KDP 的物理层：KDP 帧仍按
`osfmk/kdp/kdp_serial.c` 的格式在同一条 bulk 字节流里传输，主机侧由
`scripts/kdp-cdc-bridge.py` 解帧后转给 LLDB 的 UDP 端口。

### 接了什么

* `osfmk/arm/pal_routines.c`：`kebabkdp=1` 且 USB 控制台已配置时，
  `pal_serial_putc_nocr()` / `pal_serial_getc()` / `pal_serial_init()` 分别改走
  `kebab_usb_log()` / `kebab_usb_getc()` / `kebab_usb_kdp_start()`；否则原样调用
  UART 的 `serial_putc()` / `serial_getc()` / `serial_init()`，普通日志路径不变。
* KDP 是一帧一帧调用 `kdp_serial_out()` 的，PAL 层按终止符 `0xFB` 攒完整帧，
  再一次性写进 USB 日志环并同步冲刷。调试器进入后事件线程不再运行，只入环
  不冲刷会让应答留在设备里。
* `bsd/dev/kebab_usb.c`：`kebab_usb_start()` 加了幂等保护；新增
  `kebab_usb_kdp_start()`（提前启动并禁止主机输入回显）、
  `kebab_usb_kdp_pending()`、`kebab_usb_log_flush()`；bulk OUT 输入环从 256
  字节扩到 4096 字节，因为一个转义后的 KDP 帧最长约 3 KB。
* `osfmk/kern/startup.c`：`kebabkdp=1` 时在 `kdp_init()` 之前启动 USB 控制台，
  否则 `debug=0x14f` 的 `DB_HALT` 会在控制台存在之前就停在调试器里。
* `osfmk/kern/debug.c`：arm64 的 panic 只有 `kdp_match_name` 明确要求时才进入
  KDP；`kebabkdp=1` 现在也算这个请求，所以 panic 能停在调试器里等 LLDB。
* `osfmk/kdp/kdp_udp.c`：新增 `kdp_serial_panic_reenter()`。panic 泵收到新的
  `0xFA` 帧头时把字节流交回 KDP，复用 panic 时保存的 `kdp.saved_state`，并
  按调试器上下文把 `not_in_kdp` 清零。

KDP 字节不经过 `_cnputc`，所以不会出现在面板和内核日志里；KDP 模式下主机
写入的字节也不会再被当成键盘输入回显成 `KEBAB USB: host said ...`。

### 启动参数

在现有参数后加 `kebabkdp=1`（USB 控制台本身仍要 `kebabusb=1`）：

```
debug=0x14e serial=3 kebabusb=1 kebabkdp=1
```

* `0x14e`：正常启动，USB 控制台在 `bsd_init()` 起来；之后 panic（或 NMI，
  如果硬件能触发）进入 KDP，等主机连接。
* `0x14f`：多了 `DB_HALT`，`kdp_init()` 会在启动时直接停在
  “Waiting for remote debugger connection.”；此时 USB 控制台已由
  `kernel_bootstrap` 提前拉起。第一次上机建议先用这个参数验证连接。
* 不要加 `0x200`（`DB_KDP_GETC_ENA`）：它会让键盘轮询和 KDP 抢同一个字节流。
* `serial=3` 保留原来的串口控制台位；`kebabkdp` 未设置时 PAL 仍走 UART，
  正常日志和 panic 报告路径不变。

### 主机步骤

```
python3 scripts/kdp-cdc-bridge.py --list-ports
python3 scripts/kdp-cdc-bridge.py /dev/ttyACM0 --baud 115200 --udp-port 41139 --verbose
```

Windows 上是 `COM7` 之类的名字：

```
python scripts\kdp-cdc-bridge.py COM7 --verbose
```

看到 `KDP CDC-ACM bridge` 和 `waiting for the debugger` 后，在另一个终端：

```
lldb
(lldb) kdp-remote 127.0.0.1:41139
```

`--verbose` 会逐帧打印内层 Ethernet/IP/UDP/KDP 摘要；连接成功后应先看到
`KDP_CONNECT`，随后是 `KDP_READREGS`、`KDP_READMEM64` 等请求。

### 帧格式（与 `osfmk/kdp/kdp_serial.c` 一致）

* 定界：`START=0xFA`、`END=0xFB`、`ESC=0xFE`。
* 转义：`START`、`END`、`ESC` 和 `'\n'` 写成 `ESC` 后跟原字节的按位取反。
* 一帧 = `START` + 转义后的 4 字节小端 CRC32 + `START` + 转义后的内层包 +
  `END`；CRC 是 zlib 多项式、初值 0，只覆盖内层包。
* 内层包最大 1518 字节，布局是 Ethernet(14) + IPv4(20) + UDP(8) + KDP 头；
  KDP 的 UDP 目的端口固定 41139（`KDP_REMOTE_PORT`）。LLDB 发给桥的每个 UDP
  数据报就是这一整个内层帧，桥不改写它。
* 日志文本和 KDP 帧共用 bulk IN 字节流；桥只认 `START` 开头且 CRC 正确的帧，
  帧外文本直接跳过，因此日志照常输出。
* 转义后的帧最长约 3 KB，设备端输入环是 4096 字节；输出仍走 16 KiB 日志环，
  panic 时按 511 字节一块同步冲刷。

### 本轮验证

* 79 个补丁按文件名顺序应用到 `xnu-8796.141.3`（`1b191cb`）干净通过：**79/79**。
* `~/kebab-build/build-local.sh` 同一套步骤在构建机产出
  `build/xnu.obj/DEVELOPMENT_ARM64_KEBAB/kernel.development.kebab`，
  **17,065,976 字节（16 MiB）**；符号表里有 `kebab_usb_getc`、
  `kebab_usb_log_flush`、`kebab_usb_kdp_start`、`kebab_usb_kdp_pending`、
  `kebab_usb_kdp_enabled` 和 `kdp_serial_panic_reenter`。
* `python3 scripts/kdp-cdc-bridge.py --selftest`：**16/16 passed**，不打开任何
  串口，覆盖 CRC 向量、转义、分块解析、日志文本混流、坏 CRC/截断/超长帧、
  双向 UDP 转发。

### 还没有验证的部分

* **没有手机可接**：LLDB 的 `kdp-remote` 握手、读寄存器/内存、continue 全部
  没有在真机上跑过；目前只有编译和主机端自测。
* `debug=0x14f` 的早期启动路径：`kernel_bootstrap` 在 `kdp_init()` 前调用
  `kebab_usb_start()`，依赖 `kmem_alloc()`、`kernel_thread_start()` 在该上下文
  可用。这是正常线程上下文，但尚未上机确认；USB 枚举与 KDP 等待之间也可能
  有时序竞争。
* panic 后重连：`kdp_serial_panic_reenter()` 复用 panic trap 的
  `kdp.saved_state`，寄存器应指向 panic 现场；LLDB 是否接受这次第二次会话、
  断点和单步是否可靠，未验证。
* 吞吐：CDC-ACM 实际是 USB 全速/高速，`--baud` 只是主机侧行速率，设备端
  `SET_LINE_CODING` 被忽略；大块内存读写可能触发 KDP 超时重传，未测。
* Windows 串口后端只做了静态检查和端口枚举，没有在真实 CDC-ACM 上打开过；
  POSIX 后端同样只做了语法检查与自测。
* 如果主机要求 DTR/RTS 才发数据，设备端 CDC 控制请求处理能否满足所有主机
  组合，未验证。
## 十一、显示栈 ABI 补全

本节记录 `0093-kebab-display.patch` 的选择器补全。范围是内核侧
`IOMobileFramebuffer` / `IOSurface` 的用户客户端 ABI，不含 DPU、DSI 点屏或触摸。
结论先行：**代码编译通过、补丁从 `1b191cb` 干净应用，但没有任何一项在真实
`backboardd` 上验证过**；下表的“现在”一列是内核侧的行为承诺，不是上机结果。

### 选择器缺口（按 0093 源码逐个复核）

* `SwapWait` 是选择器 **6**。它原来落在 `default`，返回 `kIOReturnSuccess`、
  输出清零，但不等待。
* IOMFB 原实现只有 3/4/5/6/7/8/18/28/72；`backboardd` 用到而原来没有的是
  12/15/17/22/27/29/33/50/55/57/68/74/79/83/84。
* IOSurface 原实现是 0/4/13/34/35 + trap 0–5。评估稿把 `0x22/0x23` 也列为
  缺失，但 34/35 就是 0x22/0x23，早就实现了；真正缺的是 9（SetValue）、
  0xa（UpdateNonBulkValueCache）、0xb（RemoveValue）和 0x1b
  （SetBulkAttachments，十进制 27）。
* 评估稿把 83（`CopyLayerDisplayedSurface`）列进 `backboardd` 的调用集；
  ios-sweet 的日志只证明 57/15/22/29/33/79/74/12/6/27/17/84/50 以及首次
  swap 后的 3/4/5/68/55，**83 没有调用证据**。本移植仍按“返回该层已显示
  surface 的端口名”实现，并在表里标明。

调用集的证据来自 ios-sweet 在同一份 iPhone X 16.7.16 用户态（20H392）上的
`SWEET FB: <proc> sel N` 日志（`NOTES.md` L83、L86），不是 kebab 自己的日志。

### 选择器对照表

IOMobileFramebuffer（十进制，与 framework 一致）：

| 选择器 | 以前 | 现在 | 为什么 |
| --- | --- | --- | --- |
| 3 GetLayerDefaultSurface | 面板 surface 的端口名 | 不变 | 原本就能工作 |
| 4 SwapBegin | `++fSwapID`，返回 id | 不变，另记最近入队的 id | SwapWait 需要完成语义 |
| 5 SwapEnd | 调用线程里同步 memcpy 四个图层 | 只把 0x46c 记录拷进一层队列并返回 | 后端可替换；调用方不被 10.4 MB 拷贝阻塞 |
| 6 SwapWait | default：success、不等待 | 有界等待；success / timeout / notfound | 区分“完成 / 超时 / 从未入队” |
| 7 GetID | 0 | 0 | 主显示 id |
| 8 GetDisplaySize | 1080×2400（物理） | 1125×2436（逻辑） | iPhone X 身份；FBSDisplayMode 的硬门槛 |
| 12 RequestPowerChange | default | 记录开关；off 清黑一次并停 present，on 重画最后一帧 | 面板自扫描、无背光，这是能兑现的电源语义 |
| 15 SetDebugFlags | default | 保存并回显；bit0 打开每帧日志 | 调试开关本来就是我们自己的状态 |
| 17 SetGammaTable | default | `kIOReturnUnsupported` | 没有 DPU/gamma 管线，success 等于谎称已配置 |
| 18 IsMainDisplay | 1 | 1 | 主显示 |
| 22 SetDisplayDevice | default | 只接受 0，其他 `kIOReturnBadArgument` | 只有内置面板 |
| 27 GetGammaTable | default | `kIOReturnUnsupported` | 同上，没有可读的 gamma |
| 28 GetDotPitch | 395（物理 ppi） | 458（iPhone X） | 与逻辑显示身份一致 |
| 29 GetDisplayArea | default（全 0 结构） | `{0, 0, 1125, 2436}` | 显示区描述 |
| 33 EnableDisableVideoPowerSavings | default | 记录开关 | 没有省电模式可切 |
| 50 SetBrightnessCorrection | default | `kIOReturnUnsupported` | 没有背光/补偿硬件 |
| 55 SetMatrix | default | `kIOReturnUnsupported` | 没有可应用的矩阵 |
| 57 GetColorRemapMode | default | 0（无 remap） | 状态查询 |
| 68 SetParameter | default | `kIOReturnUnsupported` | 参数块语义未知 |
| 72 Enable/DisableNotifications | 有实现；vsync 载荷 `(count, ts, 0)` | 选择器不变；载荷改为 `(ts, ts, 0)`；参数不足返回 BadArgument | 原载荷把帧计数当成了时间戳 |
| 74 SupportedFrameInfo | default（全 0） | 1125×2436、3x、60 Hz、sRGB、SDR、rotation 0、BGRA | 显示模式/时序描述；结构布局为本移植定义 |
| 79 GetBlock | default | `kIOReturnUnsupported` | 没有 block 数据，不能假造 |
| 83 CopyLayerDisplayedSurface | default | 该层已显示 surface 的端口名；没有则 `kIOReturnNoDevice` | 状态可查（调用证据不足） |
| 84 SetContrast | default | `kIOReturnUnsupported` | 没有对比度硬件 |
| 其他 | success、输出清零 | `kIOReturnUnsupported`，记录一次 | 不能让上层以为调用成功 |

IOSurface（十进制；0x 前缀是评估稿的写法）：

| 选择器 | 以前 | 现在 | 为什么 |
| --- | --- | --- | --- |
| 0 create | 实现 | 不变（初始化 values 状态） | 原有创建路径 |
| 4 lookup | 实现 | 不变 | 原有查找路径 |
| 9 SetValue | default | 合并 `[value, key]` 或 dictionary；`gen=(gen+2)\|1`；写 state+0x20/+0x28；返回 gen | 真实 values，客户端能从 state page 读到 |
| 10 / 0xa UpdateNonBulkValueCache | default | `{u32, gen, 二进制序列化 dict}`；缓冲区不够时 NoSpace + 需要的大小 | ios-sweet 记录的 0xa ABI |
| 11 / 0xb RemoveValue | default | 删一个 key（空 key 清空），更新并返回 gen | 真实 values |
| 13 limits | 实现 | 不变 | 原有 |
| 27 / 0x1b SetBulkAttachments | default | 按 changed mask 与 valid size 把 15 组字段写进 state+0x30 | backboardd 会调；字段位按 ios-sweet 逆向（NOTES L97） |
| 34 / 0x22 lookup from port | 实现 | 不变 | 评估稿误列为缺失 |
| 35 / 0x23 create port | 实现 | 不变 | 同上 |
| trap 0–5 | 实现 | 不变 | 原有 |
| 其他 | success、输出清零 | `kIOReturnUnsupported` | 同上 |

### 物理面板与逻辑显示

* 物理：面板和 bootloader 帧缓冲都是 **1080×2400**，`canvas-width/height` 与
  `GetLayerDefaultSurface` 返回的默认 surface 也保持这个尺寸。
* 逻辑：`GetDisplaySize` 返回 **1125×2436**，`GetDotPitch` 458（iPhone X），
  `GetDisplayArea` 与 74 的模式描述也按 1125×2436 / 3x / 60 Hz / sRGB。
* 呈现时逻辑坐标减去面板原点 `(22, 18)`，只拷 1080×2400 的中心，边缘被裁掉。
  DPU 后端以后应自己缩放/居中，但用户态 ABI 不变。
* 不要把它“改回”1080×2400：`FBSDisplayMode` 按 1125×2436 / 3x 校验，
  ios-sweet 在 `1080x2400/0` 上死在 `-[FBSDisplayMode _initWith...]`
  （`NOTES.md` L85）。

### SwapEnd 与 SwapWait 的完成语义

* `SwapEnd`（5）只把 0x46c 记录拷进一层队列并返回；`present()` 在 60 Hz vsync
  定时器里做 CPU 拷贝，然后记 `fCompletedID`、更新时间戳并唤醒等待者。
* `SwapWait`（6）参数是 `[swap id, timeout, 0]`；timeout 按**毫秒**解释
  （这个单位没有逆向证据，是本文的约定），上限 1000 ms。返回值：
  * `kIOReturnSuccess`：该 swap 已上屏（`id <= fCompletedID`）；
  * `kIOReturnTimeout`：等到超时仍未完成；
  * `kIOReturnNotFound`：这个 id 从没被 `SwapEnd` 入队（只有 `SwapBegin`）。
* 后端替换点只有 `AppleCLCD::present()` 和完成点；换成 DPU flip 时 ABI 不变。

### 已验证 / 待验证

已验证（可复现）：

* 79 个补丁按文件名顺序应用到 `xnu-8796.141.3`（`1b191cb`）：**79/79**。
  （任务书写的是 78；实际有 79 个文件，`0099-kebab-kdp-usb.patch` 在任务书之后加入。）
* `~/kebab-build/build-local.sh`（`MACHINE_CONFIG=KEBAB`、
  `KERNEL_CONFIG=DEVELOPMENT`）产出
  `build/xnu.obj/DEVELOPMENT_ARM64_KEBAB/kernel.development.kebab`，
  **17,066,704 字节**，SHA-256 `9dfbc0cbbd6e39f6ee0f59931841809c519e6b349f2403527d287316d181d18e`；
  `KebabSurface.h`、`KebabSurface.cpp`、`KebabFramebuffer.cpp` 编译**无 warning**。
* 内核字符串里有新日志（`presented as %ux%u`、`display %s` 等），说明改动确实进了镜像。

待验证（必须有真实 `backboardd` / 真机）：

* 选择器 74 的结构布局是本移植自己定义的，不是逆出来的；首次调用会把请求的
  输出长度写进日志，方便以后按真实布局修正。29 的 `{x, y, w, h}` 同理。
* 83 的输出类型（端口名还是 surface id）没有调用证据。
* 9/0xa/0xb 的入参形态（id 在 scalar 还是在结构头）两种都接受了，但真实
  `IOSurface.framework` 用哪一种没有实测。
* `SwapWait` 的 timeout 单位按毫秒处理是假设。
* 12 的 off 语义：面板自扫描、没有背光控制，这里只清黑一次并停止 present；
  真实 DPU 的关屏行为不同。
* 返回 `kIOReturnUnsupported` 的 17/27/50/55/68/79/84 需要真实 `backboardd`
  确认它是否接受错误；ios-sweet 当年一律返回 success，没有这条数据。
* 触摸：`0098` 的 S6SY761 仍按 1079×2399 原始坐标上报；显示逻辑尺寸变成
  1125×2436 后，HID 映射要单独核对（不在本次范围，也不改 `kebab_touch.c`）。

## 十二、代码签名与 AMFI 的开发策略

本节记录 P1-3 的实际结论：一个已签名的 Apple 二进制（`/usr/lib/dyld`、`/sbin/launchd`）
从 `execve` 到第一条指令之间，内核到底检查了什么；哪些检查在本构建里存在、哪些是空的；
以及为了让它们按"开发移植"的方式通过，加了什么、放弃了什么。

补丁是 `0100-kebab-codesign.patch`，代码在 `bsd/kern/kebab_codesign.c`。全部行为只由
`kebab_cs` 启动参数控制，**默认（不传该参数）与打补丁前完全一致**，`#if defined(KEBAB)`
之外不编译任何东西。

### 一、结论先说

1. **签名结构校验和页哈希校验在内核里，而且是可用的。** 真实 Apple 二进制只要没被改动，
   这两步会自然通过：本内核支持 CodeDirectory 的 4K/16K 页大小，以及 SHA-1、SHA-256、
   SHA-256-truncated、SHA-384 四种哈希（`ubc_subr.c` 的 `cs_find_md()`；SHA-256 走内核内
   libtomcrypt，SHA-384 走 BearSSL）。
2. **CMS 签名人验证从来不在 xnu 里。** 它属于 AMFI/CoreTrust。本移植没有 AMFI，所以
   "是不是 Apple 签的"在内核里没有答案——只有"cdhash 在不在 Apple 的静态信任缓存里"这个
   间接答案。本节第五小节给出后者的来源。
3. **真正挡住真实二进制的不是签名校验，而是两个"身份"问题**：
   - `CS_PLATFORM_BINARY` 只有 AMFI 会设置，本内核永远是 0；
   - dyld 映射共享缓存时，`shared_region_map_and_slide_2_np` 的 SIP 检查要求
     `SF_RESTRICTED`，而本移植的根镜像没有这个标志。
4. **库校验（library validation）在内核里没有调用点**，`cs_require_lv()` 只有定义，
   没有使用者；它的执行完全在 AMFI。本构建里这是死代码，不需要"修"。
5. 所有的放宽都用 `kebab_cs` 的具名位表达，没有改任何共享路径的默认值。

### 二、追出来的调用链

```
execve
  exec_mach_imgact (bsd/kern/kern_exec.c)
    load_machfile / parse_machfile (bsd/kern/mach_loader.c)
      LC_CODE_SIGNATURE:
        load_code_signature()
          ubc_cs_blob_get() / ubc_cs_blob_add()      ← 解析并挂上 cs_blob
            cs_blob_init_validated()                  ← 结构校验（cs_validate_csblob）
            reconstitute_code_signature()             ← 本构建未启用
            mac_vnode_check_signature()               ← MACF 钩子（无策略 → 直接放行）
            kebab_cs_classify_blob()                  ← 本补丁：平台二进制标记
            verify_code_signature_monitor()           ← CODE_SIGNING_MONITOR=0，编译掉
            accelerate_entitlement_queries()          ← 走内置 AMFI 提供者
            csblob_parse_teamid()
          memory_object_signed(ui_control, TRUE)      ← 从此该 vnode 的页要做哈希校验
        初次页校验：cs_validate_range()
      后续页错误：vm_page_validate_cs() → cs_validate_page()
  进程属性：proc_csflags / task_set_platform_binary()
```

共享缓存是另一条路（dyld 起来之后）：

```
__shared_region_map_and_slide_6 / shared_region_map_and_slide_2_np  (syscall 536)
  _shared_region_map_and_slide()
    shared_region_map_and_slide_setup()   ← 全部检查都在这里
      fd → vnode、必须 VREG、可读
      mac_file_check_mmap()               ← AMFI 钩子，无策略则空操作
      trust cache 捷径                   ← 需要 cs_blob；本构建不可达
      root 拥有（uid 0）
      CSR：SF_RESTRICTED 要求             ← 本移植的实际阻塞点
      scdir 目录检查（scdir_enforce=0）
    vm_shared_region_map_file()           ← 按请求地址映射，不校验缓存内容
```

### 三、逐项判定

| 检查 | 位置 | 本构建状态 | 对真实 Apple 二进制 | 处理 |
| --- | --- | --- | --- | --- |
| 必须有 `LC_CODE_SIGNATURE` | `mach_loader.c`（`!got_code_signatures && cs_process_global_enforcement()`）| `cs_process_enforcement_enable = 0`，不拦 | 有签名，不触发 | 不构成门槛 |
| SuperBlob / CodeDirectory 结构 | `ubc_subr.c` `cs_validate_csblob()`、`cs_validate_codedirectory()` | 编译进内核，逐项检查 | 应通过 | 保留 |
| 页大小与哈希类型 | 同上 | 支持 4K/16K、SHA-1/256/256-trunc/384 | 应通过 | 保留 |
| CMS 签名人验证 | 不在 xnu（AMFI/CoreTrust） | 无 | 不验证 | 无法在离线环境实现 |
| MACF `mac_vnode_check_signature` | `ubc_subr.c`（`CONFIG_MACF=1`） | 内核里没有任何策略模块，钩子返回 0，`cs_flags` 原样 | 等于空操作 | 不修（本来就没有策略） |
| 平台二进制 `CS_PLATFORM_BINARY` | AMFI 的 MACF 钩子 | 永远 0 | 0 | **本补丁按信任缓存设置** |
| entitlements | AMFI `OSEntitlements` | `csb_entitlements` 恒为 NULL；查询走 `kebab_amfi.c` | 全部 DENIED | **由 `kebab_cs` 位决定是否全授予** |
| `get-task-allow` | 同上；`CS_ALLOWED_MACHO` 不含该位 | 永不置位 | 不置位 | 不合成；`task_for_pid` 仍受原限制 |
| 库校验 `cs_require_lv()` | `kern_cs.c` | **无调用点** | 不执行 | 说明，不修 |
| hardened runtime（`CS_HARD`/`CS_KILL`/`CS_RUNTIME`） | 从 CodeDirectory 带入；`cs_invalid_page()` | 保留 | 保留 | 不放松 |
| 初次页哈希 | `mach_loader.c` → `cs_validate_range()` | 保留；失败在 `CS_HARD/KILL` 下致命 | 应通过 | 保留 |
| 每次缺页的页哈希 | `vm_fault.c` → `cs_validate_page()` | 保留（`memory_object_signed` 已置位） | 应通过 | 保留 |
| TrustCache KPI | `kern_trustcache.c` | 补丁 0059 令 `trust_cache_rt = NULL`；`query_trust_cache()` 一律 NOT_FOUND | 不可用 | 不改；用自带的表替代 |
| 静态信任缓存段 | `arm_vm_init.c` 的 `chosen/memory-map:TrustCache` → `segTRUSTCACHE` | 引导器没有提供该段；AMFI 的 libTrustCache 不在树内，`trust_cache_rt` 无法初始化 | 不可用 | 改为从根卷读文件（第五小节） |
| 共享缓存 trust cache 捷径 | `vm_unix.c` | 需要缓存 vnode 上已挂 cs_blob；没有 AMFI 就永远为 NULL | 走不到 | 判定为不可达 |
| 共享缓存 root 拥有 | `vm_unix.c` | 生效 | 根镜像里 uid 0 即可通过 | 保留 |
| 共享缓存 `SF_RESTRICTED` | `vm_unix.c`（`CONFIG_CSR`） | `csr_config=0`（ADT 无 `/chosen/asmb/lp-sip0`），要求该标志；镜像没设 | **EPERM** | `kebab_cs` 位 0x8 |
| 共享缓存目录检查 | `vm_unix.c` | `scdir_enforce=0`（内核按 `XNU_TARGET_OS_OSX` + arm64 编译） | 不生效 | 不处理 |
| 共享缓存内容哈希 | —— | 内核不哈希缓存内容 | —— | 不是签名门禁 |

补充两点事实：

- 本内核按 `XNU_TARGET_OS_OSX` 编译（`-DXNU_TARGET_OS_OSX`），所以共享缓存走的是
  macOS arm64 的那套检查，不是 iOS 的那套。`scdir_enforce` 在非 x86_64 上默认 0。
- iPhone X（A11）没有 PAC，目标二进制的 CPU 子类型是 arm64，共享缓存文件名也是
  `dyld_shared_cache_arm64`。因此 `kern_exec.c` 里针对 arm64e "preview ABI" 的拒绝
  （`load_result.platform_binary == 0` 时拒绝）在本移植不适用。如果以后换到 A12+ 的
  设备，这一条会变成平台二进制标记的另一个理由。

### 四、策略：`kebab_cs` 位掩码

`kebab_cs` 在 `STARTUP(CODESIGNING, STARTUP_RANK_LAST)` 里解析，早于任何 exec。
不传 = 0 = 全部关闭。

| 位 | 名字 | 作用 | 代价 |
| --- | --- | --- | --- |
| 0x1 | `KEBAB_CS_ALLOW_UNSIGNED` | 显式把 `cs_process_enforcement_enable` 钉为 0，接受没有签名的 Mach-O | 无（本配置默认就是 0；这一位只是把既成事实写进代码和日志） |
| 0x2 | `KEBAB_CS_TRUSTCACHE_PLATFORM` | 从根卷读 Apple 静态信任缓存；命中 cdhash 的 blob 被标成平台二进制 | 信任缓存文件在可写根卷上，不是可信来源（见第八小节） |
| 0x4 | `KEBAB_CS_GRANT_ENTITLEMENTS` | 内置 AMFI 的 entitlement 查询一律返回 `KERN_SUCCESS` | 等于"所有 entitlement 都授予"；内核无法分辨查询的是哪一个 |
| 0x8 | `KEBAB_CS_UNSEALED_SHARED_CACHE` | 根卷上、root 拥有的共享缓存不再要求 `SF_RESTRICTED` | 放松了共享缓存的 SIP 检查；只影响本进程根卷上的文件 |

开发预设是 `kebab_cs=15`。一个完整的启动参数例子（沿用第 10 节的调试配置）：

```
debug=0x14e serial=3 kebabusb=1 kebab_cs=15
```

`kebab_trustcache=<path>` 可以覆盖信任缓存路径；`kebab_trustcache=0` 明确禁用。
默认路径是 `/System/Library/Caches/com.apple.kernelcaches/static.trustcache`。
文件不存在时只打印一行 `KEBAB CS: no static trust cache at ...`，不影响启动，
只是没有平台二进制。

为什么不用现成的全局开关：`amfi_get_out_of_my_way`、`amfi_allow_any_signature`、
`cs_enforcement_disable` 都只在 `DEVELOPMENT || DEBUG` 下解析，而且对所有平台生效
（`cs_enforcement_disable` 还要求 `PE_i_can_has_debugger()`）。它们是"降低整条路径"，
不是"这个移植做了什么"。`kebab_cs` 只影响 KEBAB，且每一位都在日志里回显：

```
KEBAB CS: policy 0xf unsigned trustcache-platform entitlements unsealed-shared-cache
```

### 五、静态信任缓存：从 IPSW 到内核

**它不是 `apticket.der`，也不在 `com.apple.kernelcaches` 里。** 这两者分别是
personalization 的 APTicket 和内核缓存文件，都不含 cdhash 列表。真正的组件是 IPSW 里的
独立成员，`BuildManifest.plist` 里叫 `StaticTrustCache`：

```
Firmware/087-86698-044.dmg.trustcache    66,289 字节
  DER SEQUENCE { IM4P, trst, "1", OCTET STRING }
  payload = trust_cache_module v2
    u32 version = 2
    u8  uuid[16] = 9f368a73d90b40ce943c827e13e72eee
    u32 numEntries = 2760
    2760 × 24 字节：cdhash[20] + hashType + flags + constraintCategory + reserved
    hashType: SHA-256 2758 条，SHA-1 2 条；按 cdhash 升序
```

提取和校验用 `scripts/extract-ios-trustcache.py`：

```sh
# 摘要 + 原样写出成员（内核读的就是这个文件，不需要预处理）
python3 scripts/extract-ios-trustcache.py --ipsw iPhoneX-16.7.16.ipsw --verify \
    --write ~/ios-userland/static.trustcache

# dyld / launchd 提取出来之后，一条命令验证它们是否在 Apple 的列表里
python3 scripts/extract-ios-trustcache.py --ipsw iPhoneX-16.7.16.ipsw \
    --check /path/to/usr/lib/dyld
```

把写出的文件放进根卷的默认路径，`kebab_cs` 带 0x2 时内核会在 `bsd_init` 里加载它：

```
bsd_init → bsdinit_task → kebab_cs_trustcache_load() → vnode_lookup + vn_rdwr
  → kebab_tc_unwrap_im4p()（也接受裸 module）
  → kebab_tc_parse()：校验 v0/v1/v2、条目数、文件长度、排序
  → 保留整块 kalloc 内存；cdhash 二分查找
```

命中时 `ubc_cs_blob_add()` 里把 blob 标成平台二进制（`csb_platform_binary=1`、
`csb_flags |= CS_PLATFORM_BINARY`、清空 team id），效果与 AMFI 对 trust-cached cdhash
做的事情一致。日志形如：

```
KEBAB CS: static trust cache v2: 2760 entries (66264 bytes), hash types sha1=2 sha256=2758 ..., sorted
KEBAB CS: cdhash <40 hex> is in Apple's static trust cache: platform binary
```

局限必须说清楚：

- 真机上这个缓存是**引导链**通过 `chosen/memory-map:TrustCache` 交给内核的（
  `arm_vm_init.c` 记录 `segTRUSTCACHE`）。本移植的引导器没有提供这个段；AMFI 的
  libTrustCache 也不在 xnu 源码里，所以 `trust_cache_rt` 仍然是 NULL（补丁 0059 的
  降级行为不变），`query_trust_cache()` 仍然一律 NOT_FOUND。本补丁做的是一张**并行的、
  只用于平台二进制判定的表**，没有冒充 TrustCache KPI。
- 表来自根卷上的文件，而根卷可写、未密封。它证明的是"这个 cdhash 在 Apple 为 20H392
  发布的列表里"，不是"这个文件没有被替换过"。要变成可信来源，需要引导器把该段作为
  启动链组件传进来（内核侧 `load_static_trust_cache()` 的路径已经存在，缺的是提供者和
  AMFI 的运行时）。

### 六、共享缓存（44 个文件）

iOS 16 的共享缓存是拆分缓存：`dyld_shared_cache_arm64` 加上 `.01`–`.40`，外加
`.dylddata`、`.dyldlinkedit`、`.symbols`。内核不解析缓存头，也不知道"这是主缓存还是
子缓存"；dyld 通过 `shared_region_map_and_slide_2_np` 一次把 N 个文件、M 个映射传进来，
内核对每个文件独立走一遍 `shared_region_map_and_slide_setup()`。

内核必须做到的事情：

1. 每个 fd 都是可读的普通文件（VREG），进程对根目录的视图与共享区域的根一致；
2. 每个文件 root 拥有（uid 0）；
3. 通过 `mac_file_check_mmap`（无 AMFI 时为空）；
4. 通过 trust cache 捷径或 root 检查二选一。**捷径需要缓存 vnode 上已挂 cs_blob**，
   而没有 AMFI 就没人给它挂，所以本构建永远走 root 检查；
5. `CONFIG_CSR` 下要求 `SF_RESTRICTED`（或 `CSR_ALLOW_UNRESTRICTED_FS`，本机 ADT 没配
   `/chosen/asmb/lp-sip0`，所以是 0）；
6. `vm_shared_region_map_file()` 按请求地址建立映射。**缓存内容一个字节都不哈希。**

所以：**共享缓存这条路上没有任何"签名"门禁**；实际门禁是 SIP 的 `SF_RESTRICTED`。
44 个文件每个都要过同样的检查，缺一个 dyld 就映射失败。

严格的做法是在镜像构建时给这 44 个文件设 `SF_RESTRICTED`。HFS+ 的编解码本身支持这一位
（`hfs_catalog.c`：`adminFlags = ca_flags >> 16`，`SF_RESTRICTED = 0x00080000` 落在
bit 19），但本移植的根镜像没有设，而且卷可写、未密封，这个位也不构成边界。因此本补丁
用 `kebab_cs` 的 0x8 位显式放行，条件限定为：策略打开、文件 root 拥有、且与进程的根
目录在同一个 mount 上。日志只打印一次：

```
KEBAB CS: shared cache on the unsealed root volume accepted without SF_RESTRICTED
```

### 七、改了哪些文件

| 文件 | 改动 |
| --- | --- |
| `bsd/kern/kebab_codesign.c` | 新增。策略解析、信任缓存加载/解析/查找、blob 分类、共享缓存判定 |
| `bsd/kern/kebab_codesign.h` | 新增。位定义与四个入口 |
| `bsd/conf/files` | 注册 `bsd/kern/kebab_codesign.c`（`standard`） |
| `bsd/kern/ubc_subr.c` | `ubc_cs_blob_add()` 里 MACF 之后、team id 解析之前调用 `kebab_cs_classify_blob()` |
| `bsd/kern/kebab_amfi.c` | 四个 entitlement 查询函数按 `kebab_cs` 返回 SUCCESS/DENIED；顶部注释同步 |
| `bsd/kern/bsd_init.c` | `load_init_program()` 之前调用 `kebab_cs_trustcache_load()` |
| `bsd/vm/vm_unix.c` | `shared_region_map_and_slide_setup()` 的 CSR 检查前加 kebab 判定 |

### 八、让出了什么，保留了什么

**让出：**

- **CMS 真实性**：本来就不在内核验证，这次也没有假装验证。平台二进制的唯一依据是
  cdhash 在 Apple 的静态信任缓存里；而这个缓存文件放在可写根卷上，能被替换。要恢复这一
  点需要 Image4 和真正的 libTrustCache，本移植都没有。
- **entitlement 边界**（0x4）：所有查询一律成功。任何依赖 `IOTaskHasEntitlement()` 的
  VFS/IOKit 检查（快照、dataless、fsevents、网络等）都变成"通过"。这是刻意的开发策略，
  不是疏漏；默认不开。
- **共享缓存的 SIP 检查**（0x8）：只对进程根卷上、root 拥有的文件生效，且卷本身未密封，
  所以放松的不是一个真实边界。
- **未签名二进制**（0x1）：本来就是本配置的默认（`cs_process_enforcement_enable = 0`）。
  这一位只是把它写死并打印出来，避免以后配置变化时行为漂移。

**保留（没有放松）：**

- SuperBlob / CodeDirectory 的结构校验，包括页大小、哈希类型、offset 边界；
- 初次页与每次缺页的哈希校验。**改动过的 Apple 二进制仍会被 `CS_HARD`/`CS_KILL` 杀掉**；
- `CS_HARD`、`CS_KILL`、`CS_RESTRICT`、`CS_RUNTIME` 这些从 CodeDirectory 带出来的标志；
- W^X、可执行映射权限、`exec` 的权限检查；
- `CS_REQUIRE_LV` 依旧没有内核执行点（AMFI 缺失），也就是说库校验在本移植里始终是关的；
  这不是本补丁关掉的，而是它从来没有被执行过。

需要特别说明的是"未签名"和"已签名"在页校验上的区别：没有签名 blob 的 vnode 不会被
`memory_object_signed()` 标记，页校验根本不启动；一旦有 blob，所有页都必须与
CodeDirectory 对得上。所以 0x1 不会让"签名错误"的二进制混过去——签名错误仍然按
`CS_HARD/CS_KILL` 处理。

### 九、没有硬件时无法验证的部分

- **目标二进制的 cdhash 是否真的在表里**：`/usr/lib/dyld`（922,912 字节）和
  `/sbin/launchd`（541,600 字节）目前只从 APFS inode 拿到了大小和权限，文件内容还在
  提取中（P1-1）。拿到之后用 `extract-ios-trustcache.py --check` 一条命令即可验证；
  在那之前只能说"机制就位"，不能说"这两个文件已验证"。
- **启动参数解析和打印时序**：`kebab_cs` 的 STARTUP 函数、`bsd_init` 里的加载、
  `printf` 在真机控制台上的实际顺序，都没有真机日志。
- **`vn_rdwr` 在 `bsdinit_task()` 上下文读 66 KB 文件**：按 HFS+ 的 vnode/ubc 实现和
  `load_init_program()` 的调用位置推断可行，但没有在真机上跑过。
- **共享缓存映射的实际返回值**：44 个文件是否都在、是否 root 拥有、`vm_shared_region`
  创建是否成功，只有真机能回答。
- **平台二进制标记的下游效果**：`task_set_platform_binary()`、共享区域选择、后续
  IOKit 的 entitlement 查询，都没有消费者级别的验证。
- **引导器提供 TrustCache 段的路径**（`segTRUSTCACHE`）未验证，因为本移植的引导器
  不提供。

### 十、复现

```sh
# 打补丁（在 xnu-8796.141.3 / 1b191cb 的干净树上，按文件名顺序）
for p in patches/xnu-8796-kebab/*.patch; do git apply "$p"; done   # 80/80

# 构建（构建机，macOS 13.5 + KDK_13.5_22G74）
export PATH="$HOME/bin:$HOME/Library/Python/3.9/bin:/opt/homebrew/bin:$PATH"
export MACOS_VERSION=13.5 MACHINE_CONFIG=KEBAB KERNEL_CONFIG=DEVELOPMENT
export KDKROOT="$HOME/kebab-build/KDKs/KDK_13.5_22G74.kdk"
rm -f "$HOME/kebab-build/dxb/build/xnu.obj/kernel.development.kebab"   # 否则会跳过构建
cd "$HOME/kebab-build/dxb" && bash build.sh

# 产物
build/xnu.obj/DEVELOPMENT_ARM64_KEBAB/kernel.development.kebab
17,051,216 字节（16.26 MiB），2026-10-27 03:50
```

- 补丁系列：**80/80** 干净应用（原有 79 个 + `0100-kebab-codesign.patch`）。
- 新增文件 `kebab_codesign.c` 在 `-Weverything` 下 **0 warning**；`ubc_subr.c`、
  `bsd_init.c`、`vm_unix.c` 的 warning 都是原有的
  `-Wdeclaration-after-statement` / `-Wunused-but-set-variable`，行号不在本次新增的
  代码上；`kebab_amfi.c` 没有 warning。
- 符号表里有 `kebab_cs_policy`、`kebab_cs_classify_blob`、
  `kebab_cs_shared_cache_allowed`、`kebab_cs_trustcache_load`、
  `kebab_cs_grants_entitlements`，以及 `__startup_CODESIGNING_entry_...`。

## 十三、触摸到输入栈

本节对应 `0101-kebab-touch-input.patch`（接在 `0100-kebab-codesign.patch` 之后，
系列共 81 个补丁）。它记录两件事：这个内核里到底有哪些输入设施，以及在没有
HID 的前提下触摸能走到哪一步。结论先行：**没有 HID 栈，也没有 evdev；内核里
唯一能读的接口是字符设备**。所以本次实现的是坐标变换、固定队列和
`/dev/kebab_touch`，没有伪造一个 `IOHIDEventService`。

### 1. 清点：有什么，没有什么

查过的树是 `/home/albert/ios-work/verifySc2`（`xnu-8796.141.3` + 全部补丁）。

不存在的（源码、`conf/files` 条目、导出符号都没有）：

| 名字 | 结果 |
| --- | --- |
| `iokit/hid` 目录 | 不存在 |
| `IOHIDSystem`、`IOHIDEvent`、`IOHIDEventService`、`IOHIDDevice`、`IOHIDEventDriver` | 没有 |
| `AppleHID*` 系列 | 没有 |
| `evdev` / `/dev/input` | 没有；`bsd/net/bpf.h` 的 `DLT_LINUX_EVDEV` 只是一个抓包链路类型 |
| `iokit/conf/files`、`files.arm64` 里的 HID 条目 | 没有 |

全树 `grep -rn IOHID` 只有下面几处，没有一处是 HID 实现：

* `iokit/IOKit/IOReturn.h` 与 `iokit/DriverKit/IOReturn.h` 的
  `sub_iokit_hidsystem`：错误子域编号。
* `iokit/IOKit/IOTimeStamp.h` 的 `IODBG_HID`：kdebug 代码。
* `bsd/sys/kdebug.h` 的 `DBG_IOHID`：kdebug 子类编号（`DBG_IOPOINTING`
  是它的别名）。
* `iokit/Kernel/IOCatalogue.cpp` 的 `gIOHIDInterfaceClassName =
  "IOHIDInterface"`：catalogue 用来识别旧式 NDRV HID 接口的类名过滤器，
  没有对应的类实现。
* `libkern/libkern/c++/bounded_ptr.h` 里的一句注释。

构建配置本身也把内核 HID 关掉了：`config/MASTER` 有
`options NO_KERNEL_HID`，`config/MASTER.arm64.kebab` 的 `IOKIT_BASE`
带 `no_kernel_hid`，编译命令行里是 `-DNO_KERNEL_HID`。

**kext 这条路同样走不通。** `libkern/kxld` 的源码在树里，但
`libkern/conf/files` 把它们标成 `optional config_kxld`，KEBAB 没有开这个
选项。已构建内核里：

```
$ nm kernel.development.kebab | grep -ci kxld
0
$ strings kernel.development.kebab | grep -i kxld
Refusing to link non-prelinked kext: %s (no kxld support)
```

构建机的 KDK 里确实有 `IOHIDFamily.kext`，但它是 macOS 的：
`com.apple.iokit.IOHIDFamily` 2.0.0，通用二进制 `x86_64 + arm64e`，
2,146,688 字节，`CFBundleSupportedPlatforms = ["MacOSX"]`，
`DTPlatformName = macosx`。本内核是 **arm64**（`file` 输出
`Mach-O 64-bit executable arm64`），没有 KXLD，也没有可用的 kext 加载
链路，所以这个二进制既不能加载，也不是 iPhoneOS 的构建。

存在的、这次用到的东西：

* `devfs` + `cdevsw` + `uio` + `msleep`/`lck_mtx`。UFS 驱动的
  `ufsroot0`/`rufsroot0` 和 `kebabcon` 就是同一套接口，本驱动照那个形状
  加 `/dev/kebab_touch`。
* IOKit 本身（`iokit/Kernel`、`iokit/IOKit`），但没有 HID 类。
* 控制台输出 `_cnputc`，控制台输入 `_vcgetc`（`bsd/dev/arm/cons.c`）。
* `bsd/dev/arm/km.c` 是 km 键盘多路复用器，`kminit` 会执行，但没有键盘
  硬件驱动接上；触摸不走这条路径。

### 2. 三个坐标空间与变换

| 空间 | 尺寸/范围 | 来源 |
| --- | --- | --- |
| sensor | 0..1079 × 0..2399 | 触摸报告里的 12 位字段；`max` 来自 app info，设备树 `touchpanel,panel-coords = <0x437 0x95f>` |
| panel | 1080 × 2400 像素 | 引导器帧缓冲、`IOMobileFramebuffer` 默认 surface、`canvas-width/height` |
| logical | 1125 × 2436，3x | `0093` 的 `GetDisplaySize`/`GetDisplayArea`，`backboardd` 按这个尺寸布局 |

驱动把 `1079`/`2399` 当作**最大下标，不是数量**：`0..1079` 是 1080 个
位置，正好覆盖 1080 像素宽的面板，所以 sensor 到 panel 是 1:1。逻辑显示
把 panel `(0,0)` 放在 logical `(22,18)`（`(1125-1080)/2` 向下取整、
`(2436-2400)/2`），因此 `logical = panel + (22,18)`。

变换只在 `bsd/dev/kebab_touch.c` 的 `kt_transform()` 一个函数里：

```c
panel_x   = (raw_x * (1080 - 1) + max_x / 2) / max_x;   /* 四舍五入 */
panel_y   = (raw_y * (2400 - 1) + max_y / 2) / max_y;
logical_x = panel_x + 22;
logical_y = panel_y + 18;
```

* `max_x`/`max_y` 取 app info 的值；没有 app info（或值不合理）时用
  1079/2399。当前固件下 `1079 -> 1079`、`2399 -> 2399`，比例是 1；公式仍按
  比例写，换固件时才不会把坐标缩到面板一角。
* 例子：raw `(0,0)` -> panel `(0,0)` -> logical `(22,18)`；raw
  `(1079,2399)` -> panel `(1079,2399)` -> logical `(1101,2417)`；raw
  `(540,1200)` -> logical `(562,1218)`。
* 逻辑坐标的 `0..21`、`1102..1124` 列与 `0..17`、`2418..2435` 行下面没有
  面板，点不到。这是 1080×2400 面板放进 1125×2436 逻辑显示的结果，不是
  驱动的缺陷。
* 越界处理是保守的：12 位字段不会为负；大于 `max` 的值（典型是手指抬起
  时的 `0xfff`）先夹到 `max`，事件里带 `KT_EVENT_FLAG_CLAMPED`，不向面板
  外推算。

### 3. 已实现

**事件记录**（`struct kt_event`，48 字节，`_Static_assert` 固定布局）：

```c
struct kt_event {           /* offset */
    uint32_t magic;         /*  0  'KTEV' = 0x4b544556 */
    uint32_t seq;           /*  4  报告序号，同一报告的事件相同 */
    uint64_t timestamp_ns;  /*  8  开机以来的纳秒数 */
    int32_t  raw_x, raw_y;  /* 16  sensor 空间 */
    int32_t  panel_x, panel_y;   /* 24  0..1079 / 0..2399 */
    int32_t  logical_x, logical_y; /* 32  22..1101 / 18..2417 */
    uint16_t pressure;      /* 40  报告里的 z */
    uint8_t  phase;         /* 42  1=down 2=move 3=up */
    uint8_t  slot;          /* 43  报告里的触点 index */
    uint8_t  flags;         /* 44  bit0 = clamped */
    uint8_t  pad[3];        /* 45 */
};
```

**队列**：64 槽环形缓冲，64 × 48 = 3072 字节，静态数组，不用分配器。
生产者是 5 ms 轮询线程，消费者是 `read(2)`，两边共用一把 `lck_mtx`。
没人读时覆盖最旧的一条并累加 `dropped`；容量不会超过 64 条，所以没有
消费者只损失历史，不会增长内存。`ktq_push()` 只做拷贝和索引移动，不分配。

**`/dev/kebab_touch`**（`cdevsw`，`UID_ROOT`/`GID_OPERATOR` 0440）：

* `read(2)` 一次返回整数条 48 字节记录，最旧的在前；默认阻塞，`O_NONBLOCK`
  时无数据返回 `EAGAIN`；多个读者共用同一个 tail（事件被分走，不复制）。
  buffer 小于 48 字节时返回 0。
* 只在报告解码打开（`kt.decode`）且队列自检通过时创建设备节点；节点在
  轮询线程起来之后创建，线程创建失败时不会留下一个永远读不到东西的节点。

**触摸语义**：按报告里的触点 index 维护 16 位 active 位图；新出现的 index
发 down，持续存在的 index 发 move，从报告里消失的 index 用最后的坐标发 up。
最多 10 个触点（`touchpanel,max-num-support`）。

**启动自检**：在设备节点和轮询线程存在之前，向真实队列压
3 × 64 = 192 条、完全不消费；检查只剩 64 条、`dropped == 128`、按顺序
drain 出 64 条、再 pop 为空，然后清零计数。成功时打印：

```
KEBAB TOUCH: queue selftest: 64 slots, 192 pushed, 128 dropped without a reader, 64 drained: ok
```

**控制台输出**：down 和 up 每次都打印一行（`obj`/`raw`/`panel`/`logical`/
`z`）；move 默认每 200 ms 最多一行；`kebab_touch_dump=1` 时在每个 down
额外打印该报告的完整对象列表。

### 4. 启动参数

| 参数 | 作用 |
| --- | --- |
| `kebab_touch=1` | 启用驱动（必需，原有）|
| `kebab_touch_dump=1` | 每个 down 打印解码后的完整对象列表 |
| `kebab_touch_move_ms=N` | move 行的最小间隔，默认 200；0 关闭 move 行（down/up 仍打印）|
| `kebab_touch_raw=1` | 原有：SE/TLMM 寄存器与原始报文 |
| `kebab_touch_se=` / `pins=` / `reset=` / `hz=` | 原有，见第九节 |

### 5. 没有实现、不能声称的

* 没有 `IOHIDEvent`，没有 IORegistry 节点，没有 `IOHIDEventService`。
* 没有中断：仍是 5 ms 轮询，`irq-gpio` 39 没有使用。
* 没有 `select`/`poll`/`ioctl`；`read(2)` 是唯一接口，也没有 `mmap`。
* 没有把触摸送进 SpringBoard/backboardd 的路径。用户态现在能做的只有
  `open("/dev/kebab_touch")` + `read` 48 字节记录，而本移植的用户态还
  没有到能跑这种进程的程度。
* 没有校准、温度补偿或边缘补偿；映射是固定比例加原点。
* 没有在真机上验证过：本节所有结论都来自编译、启动自检代码和内核字符串，
  没有一次真实触摸。

### 6. 到用户态还差什么

要到 `backboardd`/SpringBoard 能收到触摸，缺的是整个 `IOHIDFamily`，
不是一个补丁：

* 需要 Apple 的 `IOHIDFamily` 源码（独立项目；`docs/GPU-ASSESSMENT.md`
  记录 ios-sweet 用的是 1915 版源码），其中至少包括
  `IOHIDEventService`、`IOHIDEventDriver`、`IOHIDEvent`/`IOHIDEventQueue`
  和用户客户端；用户态还需要 `IOKit.framework` 的
  `IOHIDEventSystemClient`，`backboardd` 正是通过它取事件的。
* 两条落地路径：
  1. **静态链接**：把 `IOHIDFamily` 的源码并进这个内核构建（和
     `0070`/`0093` 的做法一样）。它依赖 IOKit/libkern 的平台细节，需要
     逐项移植；好处是不需要 KXLD。
  2. **打开 KXLD 并把 IOHIDFamily 做成 kext**：要开 `config_kxld`，
     用与本内核一致的 arm64/iPhoneOS 平台构建，并解决 prelink/符号集。
     KDK 里那个 macOS `x86_64 + arm64e` 的二进制不能直接用。
* 体量：KDK 里同版本的 macOS `IOHIDFamily` 二进制本身是 2,146,688 字节，
  源码是完整的 Apple kext 工程；再叠加用户态 `IOKit.framework` 的 HID
  客户端。这不是"再写一个驱动文件"的量级，应作为独立阶段排期。
* 过渡方案：写一个用户态进程读 `/dev/kebab_touch`，可以驱动自定义的
  demo，但 iOS 的 `backboardd` 不会读它；在 `IOHIDFamily` 到位之前，
  这条路径到不了真实 UI。

### 7. 应用与构建结果

* 81 个 kebab 补丁按文件名顺序应用到 `xnu-8796.141.3`（`1b191cb`）的
  干净索引：**81/81** 成功（`git apply --cached`，无 FAILED）。
* 构建：`~/kebab-build/build-local.sh`（`MACHINE_CONFIG=KEBAB`、
  `KERNEL_CONFIG=DEVELOPMENT`，macOS 13.5 + `KDK_13.5_22G74`），
  日志 `89 patches staged`（81 个 kebab 补丁 + 8 个构建用补丁），
  `0101-kebab-touch-input.patch` 显示 `Applying patch`。
* 产物：
  `build/xnu.obj/DEVELOPMENT_ARM64_KEBAB/kernel.development.kebab`，
  **17,051,184 字节（16.26 MiB）**，arm64，
  SHA-256 `2052a141fe7357f5cb34e6ecf44b6f86b4efa516066773169c5aad78fcb00dc5`，
  构建时间 2026-10-27 03:59:54。
* `kebab_touch.c` 本身 **0 warning**；编译它时出现的 3 条来自已有头文件
  （`kern/kcdata.h`、`mach/vm_param.h` 的 `-Wdeclaration-after-statement`
  和 `pexpert/arm64/KEBAB.h` 的 `KEBAB` 宏重定义），原文件已经包含同一批
  头（`kern/clock.h`、`pexpert/pexpert.h`），不是本次新增。
* 内核字符串里有 `KEBAB TOUCH: queue selftest: %u slots, ...` 和设备节点名
  `kebab_touch`，确认代码进了镜像。

复现（构建机上）：

```sh
# 构建脚本不会自己删这个符号链接；不删可能跳过重新链接
rm -f "$HOME/kebab-build/dxb/build/xnu.obj/kernel.development.kebab"
cd "$HOME/kebab-build" && ./build-local.sh
```

## 十四、共享缓存的映射计划

本节做 P1 的前一半：内核能解析拆分 dyld 共享缓存、按 UUID 找齐 44 个文件、
校验并生成共享区映射计划；真正的映射放在第二个启动开关后面。本节不包含
dyld 本身，也不包含 slide/重定位。所有结论来自代码、host checker 和编译，
**没有真机启动日志**。

### 1. 代码结构

新增两个文件：

| 文件 | 作用 |
| --- | --- |
| `bsd/kern/kebab_dyldcache.h` | 两个入口：`kebab_dsc_boot()`（`bsd_init`）和 `kebab_dsc_exec_hook()`（第一次 exec）|
| `bsd/kern/kebab_dyldcache.c` | 头解析、目录扫描、UUID 解析、计划、映射 |

接入点：

* `bsd/conf/files` 增加 `bsd/kern/kebab_dyldcache.c standard`。
* `bsd/kern/bsd_init.c` 在根卷挂好、静态 trust cache 加载之后，
  `load_init_program()` 之前调用 `kebab_dsc_boot()`。
* `bsd/kern/kern_exec.c` 在 `exec_prefault_data()` 之后、`vm_map_switch(old_map)`
  之前调用 `kebab_dsc_exec_hook(p)`；此时新地址空间和它的 shared region
  已经是 current map。
* 两个文件整体包在 `#if defined(KEBAB)` 里，非 KEBAB 构建不编译任何内容。

内核里的结构（偏移用 `offsetof()` 静态断言固定，不依赖编译器布局）：

| 结构 | 大小 | 关键字段 |
| --- | ---: | --- |
| `struct kebab_dsc_header` | 456 | `magic[16]`、`mappingOffset/Count`、`codeSignatureOffset/Size`、`uuid`、`cacheType`、`imagesTextOffset/Count`、`platform`、`formatVersion`、`sharedRegionStart/Size`、`maxSlide`、`mappingWithSlideOffset/Count`、`subCacheArrayOffset/Count`、`symbolFileUUID`、`imagesOffset/Count` |
| `struct kebab_dsc_mapping_info` | 32 | `address`、`size`、`fileOffset`、`maxProt`、`initProt` |
| `struct kebab_dsc_subcache_entry` | 56 | `uuid[16]`、`cacheVMOffset`、`fileSuffix[32]` |
| `struct kebab_dsc_plan` | — | 目录、region、文件数/映射数、总映射字节、最高结束地址、`.symbols` 信息、`1 + 64` 个文件槽 |

`imagesOffset/imagesCount` 在 448/452，不是头部的 24/28；24/28 是
`imagesOffsetOld/imagesCountOld`，在 44 个文件里都是 0。`subCacheArrayOffset`
在 392，`symbolFileUUID` 在 400。这些值和 `dyld_cache_format.h` 一致，
`bsd/kern/kebab_dyldcache.c` 的 `static_assert` 会逐项检查。

### 2. 校验规则

主缓存和每个子缓存都走同一套检查；任一失败都打印文件名、字段和实际值，
然后返回错误，不生成计划：

1. `magic` 必须是 16 字节 `dyld_v1   arm64`（末尾 NUL）。
2. `formatVersion` 的低 8 位必须是 0。
3. `cacheType` 必须是 2（multi-cache）。
4. `platform` 必须是 2（iOS）。
5. `imagesOffset + imagesCount*32` 必须等于 `imagesTextOffset`。
6. `subCacheArrayOffset + subCacheArrayCount*56` 必须小于等于
   `codeSignatureOffset`。
7. `codeSignatureOffset + codeSignatureSize` 必须在文件长度内；
   `imagesTextOffset + imagesTextCount*32` 也必须在文件长度内。
8. `mappingCount` 必须在 1..8；32 字节映射表必须在文件长度内。
9. 有 `mappingWithSlideCount` 时，56 字节表必须在文件内，且数量必须等于
   `mappingCount`（44 个文件都满足）。
10. `sharedRegionStart`/`sharedRegionSize` 必须非零、16 KiB 对齐、加法不
    溢出。
11. 子缓存的 `sharedRegionStart` 必须等于
    `sharedRegionStart + cacheVMOffset`；它的第一条映射地址也必须等于这个值。
12. 每条映射的地址和大小必须 16 KiB 对齐，范围必须在共享区内；文件映射的
    `fileOffset + size` 必须在文件长度内。
13. `initProt`/`maxProt` 只允许 r/w/x（以及 zero-fill 标志）；不认识的位
    一律拒绝。
14. 42 个 `subCacheArray` 条目必须全部按 UUID 找到文件；重复 UUID 是错误，
    找不到的文件会逐个打印（不会静默跳过）。
15. 必须各有 2 个 `.dylddata` 和 2 个 `.dyldlinkedit`。少一个就意味着
    `__DATA`/`__DATA_CONST` 或 `__LINKEDIT` 缺一块，dyld 走到第一个 GOT
    就会 fault。

`.symbols` 用主头的 `symbolFileUUID` 找，找到后只报告名字、大小和 UUID，
**不进计划、不映射**。44 个文件里它是 592,101,376 字节，只用于符号化。

### 3. 解析和计划

`kebab_dsc_build_plan()` 的步骤：

1. 打开目录下的主缓存 `dyld_shared_cache_arm64`，读 456 字节头，校验。
2. 读主头里的 32 字节映射表（1 条：`0x180000000`，`0x5c000`，r-x）。
3. 读 `subCacheArray`（42 条，每条 56 字节）。
4. 用 `VNOP_READDIR` 扫目录，只收 `dyld_shared_cache_arm64*` 的普通文件；
   逐个读 456 字节头，按头里的 `uuid` 建索引。
5. 对 42 个条目逐个按 UUID 匹配，校验子缓存头、`sharedRegionStart`、第一条
   映射地址和每条映射，然后把映射表转换成
   `struct shared_file_mapping_slide_np`（slide 字段填 0）。
6. 统计映射数、总字节、最高结束地址；核对 `.dylddata`/`.dyldlinkedit` 数量；
   按 `symbolFileUUID` 找 `.symbols`。

计划规则：

* 主缓存放在 `sharedRegionStart`，它自己的映射地址就是 `0x180000000`。
* 每个子缓存的第一条映射放在 `sharedRegionStart + cacheVMOffset`；后面的
  映射用子缓存头里的绝对地址。保护位来自子缓存自己的 32 字节映射表，
  `initProt`/`maxProt` 原样保留。
* `.symbols` 不映射。

真实 44 个文件的结果：

| 项 | 值 |
| --- | --- |
| 要映射的文件 | 43（主缓存 + 42 个子缓存）|
| 映射条目 | 51（38 个编号分片各 1 条；4 个数据文件共 12 条；主缓存 1 条）|
| 共享区 | `0x180000000` + `0xaade8000`，结束 `0x22ade8000` |
| 总映射字节 | `0xa2de4000`（2,732,474,368）|
| 最高映射结束 | `0x22ade4000`，距共享区末尾 `0x4000` |
| 未映射 | `dyld_shared_cache_arm64.symbols`，592,101,376 字节 |

编号分片 `.01`–`.40` 都是 1 条 r-x 映射；两个 `.dylddata` 各 4 条
（头页 r-x、`__DATA_CONST` init r--/max rw、`__DATA` rw、一条 r--）；
两个 `.dyldlinkedit` 各 2 条（头页 r-x、`__LINKEDIT` r--）。`.01` 的
`0x18005c000` 正好接在主缓存的 `0x5c000` 后面，`.42.dyldlinkedit` 的
最后一条映射结束在 `0x22ade4000`。

### 4. 已实现与 dry-run

* **已实现**：头解析、目录扫描、42/42 UUID 解析、所有校验、计划生成、
  逐文件/逐映射日志、`.symbols` 排除、`.dylddata`/`.dyldlinkedit` 强制检查。
* **默认是 dry-run**：`kebab_dsc=<目录>`（或 `kebab_dsc=1` 用
  `/System/Library/Caches/com.apple.dyld`）只解析、校验、打印计划，不映射。
* **真实映射在第二个开关后面**：`kebab_dsc_map=1`。映射用
  `vm_shared_region_map_file()`，也就是
  `shared_region_map_and_slide_2_np()` 背后的同一个原语。
* **为什么不在 bsd_init 映射**：`bsd_init` 的 current task 是 kernel task，
  没有 shared region。`kebab_dsc_boot()` 会明确打印
  `no shared region at boot (kernel task); deferred to first exec (ENODEV)`，
  不会假装映射成功。真正的映射发生在 pid 1 第一次 exec 的
  `kebab_dsc_exec_hook()`：此时 `vm_map_exec()` 已经建好 shared region，
  且新 map 是 current map。
* **映射前检查**：如果 shared region 已经非空（例如 dyld 已经自己映射过），
  打印 `EEXIST` 并原样保留，不覆盖。任何 `vnode_lookup`、UUID/大小变化、
  `ubc_getobject` 或 `vm_shared_region_map_file()` 失败都会打印 errno，
  并把 `shared cache NOT mapped` 写进日志。
* **没有实现**：slide/重定位。计划里的 `sms_max_prot` 不带
  `VM_PROT_SLIDE`，`sms_slide_start/size` 为 0；`mappingWithSlide` 表只校验
  数量，不消费。缓存按 unslid、slide=0 映射到 `0x180000000`。
* **没有真机验证**：本节所有启动行为都是代码路径，没有一次在 OnePlus 8T
  上带 3.3 GB 缓存启动过。

### 5. 启动参数

| 参数 | 作用 |
| --- | --- |
| `kebab_dsc=<绝对目录>` | 解析并打印计划（dry-run）|
| `kebab_dsc=1` | 同上，目录固定为 `/System/Library/Caches/com.apple.dyld` |
| `kebab_dsc_map=1` | 在计划通过后，第一次 exec 时真正映射 |
| `kebab_cs=8`（或 `15`）| 以后 dyld 自己走 `shared_region_map_and_slide_2_np()` 时，放行未封存根卷上的共享缓存；内核直接映射这条路径不经过该 SIP 检查 |

不给 `kebab_dsc` 时整个模块不运行，和没有这个补丁一样。

### 6. 离线校验

`scripts/check-shared-cache.py` 是内核解析/计划逻辑的 host 版本：同样的
字段偏移、同样的 15 条校验、同样按 UUID 扫目录、同样的计划输出。它只读
每个文件的前 4 KiB（主缓存多读到 `subCacheArray` 末尾），不读 3.3 GB。

在真实提取目录上：

```sh
python3 scripts/check-shared-cache.py ~/ios-userland/cryptex-cache
```

结果：42/42 子缓存按 UUID 解析，`dylddata=2`、`dyldlinkedit=2`，
`.symbols` 找到但不映射，43 个文件 / 51 条映射 / `0xa2de4000` 字节，
最高结束 `0x22ade4000`，共享区剩余 `0x4000`。完整输出见本节末尾。

### 7. 到 dyld 能用还差什么

1. **缓存要真的在根卷上**：43 个文件共约 2.7 GB 映射量，磁盘上 44 个文件
   共 3,330,359,296 字节。现在的根镜像是 64 MiB，放不下；要么扩根镜像，
   要么把缓存放到另一个可读卷并把 `kebab_dsc` 指过去。
2. **真机跑一次 dry-run**：`kebab_dsc=/...` 加普通启动参数，确认
   HFS+ 的 `VNOP_READDIR` 能列出 44 个文件、42/42 解析、计划数字和 host
   checker 一致。这是最便宜的一步，不需要 `kebab_dsc_map=1`。
3. **真机跑一次映射**：加 `kebab_dsc_map=1`，预期日志是 boot 阶段
   `ENODEV`、第一次 exec `mapping complete`；然后用一个最小用户程序调
   `shared_region_check_np()`，应返回 `0x180000000`，并在该地址读到
   uuid `8f970452c9e33e88b114feb1864bc9e0`。
4. **slide/重定位**：现在固定 slide=0。若 dyld 需要非零 slide，下一阶段
   要消费 `mappingWithSlideOffset/Count` 里的 56 字节表，设置
   `VM_PROT_SLIDE` 并正确填 `sms_slide_start/size`；否则 dyld 只能把缓存
   当 unslid 用。
5. **dyld 本身**：`/usr/lib/dyld` 仍是 OS 卷里的 922,912 字节文件，不在这
   44 个里；`libSystem.B.dylib` 才来自缓存。P1 只是让内核先把缓存放好，
   还没有把 dyld、launchd 和真正的用户态接上。
6. **签名策略**：内核直接 `vm_shared_region_map_file()` 的路径不经过
   `shared_region_map_and_slide_setup()` 的 root/SF_RESTRICTED 检查；这是
   `kebab_dsc_map=1` 的内核专属路径。将来 dyld 自己调 syscall 时仍需要
   `kebab_cs=8`，否则未封存根卷会被 SIP 检查拒绝。
7. **`.symbols`**：不映射不影响执行；符号化/崩溃解析仍需要它在盘上。

### 8. 应用与构建结果

* 82 个 kebab 补丁（原 81 + `0102-kebab-dyld-shared-cache.patch`）按文件名
  顺序应用到 `xnu-8796.141.3`（`1b191cb`）的干净索引：**82/82** 成功
  （`git apply --cached`，无 FAILED）。
* 构建：`~/kebab-build/build-local.sh`（内部调用 `dxb/build.sh`；`MACHINE_CONFIG=KEBAB`、
  `KERNEL_CONFIG=DEVELOPMENT`，macOS 13.5 + `KDK_13.5_22G74`）。
* 产物：`build/xnu.obj/DEVELOPMENT_ARM64_KEBAB/kernel.development.kebab`，
  **17,068,976 字节（16.28 MiB）**，Mach-O 64-bit arm64，
  SHA-256 `147d819986c1e7f7a033a4cc5d02d9c1f30fb44bd6a988cad4da6c5b5c4541b6`。
* `kebab_dyldcache.c` 本身 **0 warning**（编译日志里
  `kebab_dyldcache.c:<行>:<列>: warning` 为 0 条）；全树其余 warning
  都是既有文件/头文件的，本次没有新增。
* 内核字符串里有 `KEBAB DSC: dry run: ...`、`KEBAB DSC: plan ...`、
  `KEBAB DSC: mapping complete; ...` 等，确认代码进了镜像。

host checker 的完整输出（106 行，节选头部和尾部）：

```text
KEBAB DSC: host checker: /home/albert/ios-userland/cryptex-cache
KEBAB DSC: main dyld_shared_cache_arm64: 393216 bytes, uuid 8f970452c9e33e88b114feb1864bc9e0
KEBAB DSC: validate magic=b'dyld_v1   arm64\x00' formatVersion=0 cacheType=2 platform=2
KEBAB DSC: validate images 624 + 2710*32 = 87344 == imagesTextOffset 87344
KEBAB DSC: validate subcache array 174064 + 42*56 = 176416 <= codeSignatureOffset 376832
KEBAB DSC: region start=0x180000000 size=0xaade8000 end=0x22ade8000 maxSlide=0x55214000
KEBAB DSC: resolved 42/42 subcaches by uuid; dylddata=2 dyldlinkedit=2
KEBAB DSC: scanned 44 cache files in directory
KEBAB DSC: symbols dyld_shared_cache_arm64.symbols: 592101376 bytes, uuid 51ccba451fc23f19aed047050bdf009a, NOT mapped
KEBAB DSC: plan 43 files / 51 mappings / 0xa2de4000 bytes (2732474368)
KEBAB DSC: highest mapped end 0x22ade4000, region slack 0x4000
KEBAB DSC: file 00 main dyld_shared_cache_arm64                size=0x60000     mapped=0x5c000     vmOffset=0x0          uuid=8f970452c9e33e88b114feb1864bc9e0
KEBAB DSC:      map 0x180000000 size=0x5c000     file=0x0         init=r-x max=r-x
...
KEBAB DSC: file 42 sub  dyld_shared_cache_arm64.42.dyldlinkedit size=0x13ea0000  mapped=0x13e00000  vmOffset=0x96fe4000   uuid=68ac84bcf0723a7e9806c21472133857
KEBAB DSC:      map 0x216fe4000 size=0x5c000     file=0x0         init=r-x max=r-x
KEBAB DSC:      map 0x217040000 size=0x13da4000  file=0x5c000     init=r-- max=r--
KEBAB DSC: dry run: validated only; the kernel maps nothing unless kebab_dsc_map=1
```

## 十五、EP0 重写（外部贡献合入）

本节记录把外部贡献的 EP0/事件重写合入 `bsd/dev/kebab_usb.c` 的结果，对应
`patches/xnu-8796-kebab/0091-kebab-usb-cdc-acm.patch` 的新版本。

### 1. 合入前的差异清点

外部工作目录给了 `kebab_usb.before.c` 和 `kebab_usb.after.c` 两份快照。先按
要求做 `diff -u`，结果是：

* `before.c` 与 `ios16` 上 `0091` 解出的驱动逐字节相同：48,516 字节，
  md5 `0e18412c55d5ad08e2bf9b32c345f3aa`。
* 因此任务描述里“live 比 snapshot 多出 EP0 探针和 serial KDP”这一条在这棵
  树上不成立：稿件写作时依据的快照已经包含这两项。没有需要补种的 live
  功能，`after.c` 本身也已经带有两者。
* 仍然逐项核对过：13 个 `kebab_usb_*` 外部函数、探针的 9 个既有记录点、
  `ep0_history[32]`、`ep0_progress`、4 KiB `in_ring`、0xFA 判断和
  `kdp_serial_panic_reenter()` 调用都在 `after.c` 中存在。

所以合入方式不是把旧实现重新抄一遍，而是：以 `after.c` 为基线，对每个 live
功能做存在性和调用点核对，只在确实冲突的调用点补注释，说明为什么改走新状态
机的路径。

### 2. 重写本身改了什么

* **事件与 DMA 归属**：一个 IRQ-masked、非递归的 CPU gate 覆盖 worker、
  console flush、输入、探针输出和 panic 轮询；每个四字节事件推进一次
  `evt_pos` 并只确认四字节，新增 `events`/`event_acked`/`event_discarded`
  和 `takeover_active` 计数。panic 接管继承在途 DMA，不伪造 busy 标志。
* **EP0 状态机**：SETUP、DATA、WAIT_STATUS、STATUS、ABORT 分开；CDC
  `SET_LINE_CODING` 先收 7 字节再发 IN 状态，`GET_LINE_CODING` 用存储值
  并按 wLength 截断，`SET_CONTROL_LINE_STATE` 在状态完成后提交；短 IN 在
  64 字节边界挂 ZLP，短/畸形 OUT 走 stall；`SETUP_PENDING`、重复 SETUP、
  未知请求和方向错误都有单独路径。
* **复位/断开恢复**：先按保存的 resource index 结束活动传输，控制端点不加
  ForceRM，非控制端点加；等 ENDTRANSFER 的 EPCMDCMPLT 而不是 CMDACT；
  之后才清 data endpoint stall、用 MODIFY 恢复 EP0。`DEPSTARTCFG(0)` 只
  留给初始化/核心复位。
* **可重复配置与 CLEARSTALL**：`SET_CONFIGURATION(0)` 停止并 fence data
  endpoint，`SET_CONFIGURATION(1)` 每次（包括重复的 1）都重建 resource、
  清 data stall、恢复 endpoint 后再确认状态；`CLEAR_FEATURE(HALT)` 真正发
  `DEPCMD_CLEARSTALL` 并在 fence 后重新 arm。
* **通知端点**：`SERIAL_STATE` 描述符 MPS、端点 MPS 和传输长度统一为 10，
  内容 `a1 20 00 00 00 00 02 00 03 00`；一次只持有一个通知 resource，
  变化合并到下一次完成。
* **运行时打印**：transport-owned 路径不再 `printf`，只保留启动诊断。

### 3. 探针的合入

`after.c` 保留阶段 1–7 和 16–17 的原有编码，并为重写新增的阶段定义了
8–27：wait-status、abort、stall、END issued、END confirmed、configure、
halt、disconnect、connect、takeover、fault、control-lines commit、
line-coding commit、controller fence、deferred SETUP、notification、
producer takeover。每个新增迁移都有 `kebab_usb_ep0_note()` 调用；探针的
32 项环和打包格式没有变。

有两处与旧探针文档的路径不同，已在代码注释里写明：

* `kebab_usb_ep0_probe_send()` 不再从 panic 循环直接向 bulk IN 提交，
  而是把格式化后的报告追加到 log ring；原因是重写后 log pump 是 bulk IN
  的唯一 owner（`ep[].busy`/`tx_pending`），直接提交会与它竞争。
  panic 循环下一轮 `ud_poll_locked()` 按顺序把报告发出去，仍然不经过
  `printf`，也不会递归进 console。
* live 版本在首次 configure 时用 `ep0_probe_printed` 打一行基线；重写把
  运行时 `printf` 从 transport 路径删掉了，这一行基线也随之去掉。完整
  快照仍可从 panic 报告和 KDP 关闭时的 `EP0?` 命令取得。这是有意的取舍，
  不是遗漏。

### 4. KDP 的合入

`kebab_usb_log_flush()`、`kebab_usb_kdp_start()`、`kebab_usb_kdp_pending()`、
`kebab_usb_kdp_enabled()` 都保留；`in_ring` 仍是 4096 字节，足够放一帧
序列化后的 KDP 报文。`kebab_usb_panic_spin()` 在新的轮询之后、释放
transport gate 之后，仍检查输入环首字节是否为 0xFA，并调用
`kdp_serial_panic_reenter()`，把重连的调试器交回 KDP。

`kebab_usb_console_ready()` 从 live 的 `running && configured` 改成
`running && !dead`。这是为了让 panic spin 在设备尚未重新枚举时也能继续
运行；KDP 的 `pal_serial` 因此可能在配置完成前看到“ready”，此时
`kebab_usb_log_flush()` 不会发送，字节留在 log ring，等枚举完成后由
log pump 发出。

### 5. 已验证的内容

* 用 xnu-8796.141.3 头文件在 WSL 里按 arm64 freestanding 方式编译
  `kebab_usb.c`：0 error，只有 1 条来自 `vm_kern.h` 的既有 warning。
* 外部稿件自带的 `test_driver.py`（MMIO/cache/CPU 替身，ASan/UBSan）
  六组测试全部 PASS，覆盖 CDC 方向与提交、短传输、SETUP_PENDING、halt、
  重复配置、复位顺序、事件记账和接管。
* 在 `xnu-8796.141.3` 的干净索引 `1b191cb` 上按文件名顺序应用全部
  **82/82** 个 kebab 补丁，无失败。
* 在构建机 `~/kebab-build/build-local.sh` 上构建成功：
  `build/xnu.obj/DEVELOPMENT_ARM64_KEBAB/kernel.development.kebab`，
  **17,069,408 字节（16.28 MiB）**，Mach-O 64-bit arm64，SHA-256
  `10f6a38db88c09089a3949a7d1fc58d6619820a645a7c59703df414c5239c22d`。
* `kebab_usb.c` 在本轮构建里有 40 条 warning，全部是
  `-Wdeclaration-after-statement`；live 版本有 17 条 warning，其中 11 条
  是同一类。新增 29 条同类告警，没有新增告警类别；live 的 4 条
  `-Wmissing-prototypes`（`kebab_usb_start`/`getc`/`console_ready`/
  `panic_spin`）和 2 条 `-Wmissing-noreturn`（`ud_thread`/
  `kebab_usb_panic_spin`）在重写里消失。

### 6. 仍未验证的内容

* 真机上的 close/reopen、panic 后重新枚举和 CDC 控制请求完成状态；
* DWC3 命令/事件时序、cache 维护和 DMA 一致性；
* Windows 主机在重开端口时的行为；
* KDP 重连后 `kdp_serial_panic_reenter()` 的实际会话恢复；
* 探针基线行删除后的现场可读性；完整快照仍待一次真实 panic 抓取。

本轮没有运行硬件测试，以上构建和静态/模拟验证不能替代真机结论。

## 十六、共享缓存的 slide

本节给第十四节的预映射计划补上 slide。结论来自源码、host checker 和一次
`xnu-8796.141.3` 干净索引应用 + 构建机编译；**没有真机启动日志**。第十四节
记录的 82/82 和内核哈希早于 `0103`，本节记录的是加入 `0103` 之后的 83/83 和
新哈希。

### 1. 这棵树里的 slide 机制

正常路径是 dyld 调 `shared_region_map_and_slide_2_np()`：

1. `bsd/vm/vm_unix.c:2899` 读入 `shared_files[0].sf_slide` 作为 `maxSlide`，
   取随机数，然后：

```c
/* bsd/vm/vm_unix.c:2977-2981 */
if (max_slide != 0) {
        read_random(&random_val, sizeof random_val);
        slide_amount = ((random_val % max_slide) & SLIDE_AMOUNT_MASK);
} else {
        slide_amount = 0;
}
```

`SLIDE_AMOUNT_MASK` 在非 x86_64 上是 `~SIXTEENK_PAGE_MASK`，所以 slide 是
16 KiB 对齐、严格小于 `maxSlide` 的值。

2. 同一个系统调用把 slide 加到每个 mapping 的地址和 slide-info 地址上，再
   进入公共映射函数：

```c
/* bsd/vm/vm_unix.c:3008-3011 */
mappings[m].sms_address += slide_amount;
if (mappings[m].sms_slide_size != 0) {
        mappings[m].sms_slide_start += slide_amount;
}
```

3. `_shared_region_map_and_slide()`（`vm_unix.c:2721`）调用
   `vm_shared_region_map_file()`（`vm_shared_region.c:1934`）。它的第一部分
   `vm_shared_region_map_file_setup()`（`vm_shared_region.c:1404`）先处理
   slide 值：

```c
/* osfmk/vm/vm_shared_region.c:1503-1511 */
if (shared_region->sr_slide == 0 && srfmp->slide != 0) {
        shared_region->sr_slide = srfmp->slide;
} else if (shared_region->sr_slide != 0 &&
    srfmp->slide != 0 &&
    shared_region->sr_slide != srfmp->slide) {
        /* more than 1 non-zero slide value */
        kr = KERN_INVALID_ARGUMENT;
        break;
}
```

紧接着（`vm_shared_region.c:1520`）拒绝非 16 KiB 对齐的 slide。随后按已经
加过 slide 的 `sms_address` 建立所有映射，并用 `sr_first_mapping`
（`vm_shared_region.c:1816`）记下第一个映射的相对地址。

4. setup 完成后，`vm_shared_region_map_file()` 第二遍只对
   `sms_max_prot & VM_PROT_SLIDE` 的 mapping 调
   `vm_shared_region_slide()`（`vm_shared_region.c:2012`），把普通文件映射
   替换成 lazy rebase pager：

```c
/* osfmk/vm/vm_shared_region.c:2012-2019 */
kr = vm_shared_region_slide(shared_region->sr_slide,
    mappings_to_slide[i]->sms_file_offset,
    mappings_to_slide[i]->sms_size,
    mappings_to_slide[i]->sms_slide_start,
    mappings_to_slide[i]->sms_slide_size,
    slid_mappings[i],
    slid_file_controls[i],
    mappings_to_slide[i]->sms_max_prot);
```

`vm_shared_region_slide()`（`vm_shared_region.c:3664`）进入
`vm_shared_region_slide_mapping()`（`vm_shared_region.c:2548`）。后者从
`sms_slide_start` 这个**用户地址** `copyin()` slide info：

```c
/* osfmk/vm/vm_shared_region.c:2593 */
error = copyin(slide_info_addr, slide_info_entry, (size_t)slide_info_size);
```

然后做 `vm_shared_region_slide_sanity_check()`，用
`shared_region_pager_setup()`（`vm_shared_region.c:2682`）建立 pager。v2 的
sanity check 在 `vm_shared_region.c:2790` 要求
`page_size == PAGE_SIZE_FOR_SR_SLIDE`，这个常量在
`osfmk/vm/vm_shared_region.h:98` 固定为 **4096**（不是 16 KiB）。

5. 缺页时，pager（`osfmk/vm/vm_shared_region_pager.c:707`）按 4 KiB chunk
   调 `vm_shared_region_slide_page()`，v2 分支是
   `vm_shared_region_slide_page_v2()`（`vm_shared_region.c:3095`）：读
   `page_starts`/`page_extras`，用 `delta_mask`/`value_add` 找链，再把
   `si->si_slide` 加到重定位后的指针上。`si->si_slide` 就是第 3 步的
   `sr_slide`。

6. dyld 看到的是 `shared_region_check_np()`（`bsd/vm/vm_unix.c:2091`）。它返回
   `sr_base_address + sr_first_mapping`（`vm_shared_region.c:1020-1027`）。
   slide 之后 `sr_first_mapping` 是 main cache 的 slid 地址，所以返回值是
   `sharedRegionStart + slide`；dyld 用 header 里的
   `sharedRegionStart` 反推出同一个 slide。`sr_slide`（pager 用）和这个
   base（dyld 用）必须由同一个数产生，不能只改其中一个。

7. `maxSlide` 占的是真实 shared region 的空间，不是 cache 自己的
   `sharedRegionSize`。这棵树 arm64 的 region 是
   `[0x180000000, 0x280000000)`（`SHARED_REGION_BASE_ARM64` +
   `SHARED_REGION_SIZE_ARM64`，4 GiB）。真实 cache 的
   `sharedRegionStart + sharedRegionSize = 0x22ade8000`，
   再加 `maxSlide 0x55214000` 得到 `0x27fffc000`，仍在 region 内，剩
   `0x4000`。因此校验不能拿 `0x22ade8000` 当上界，否则任何非零 slide 都会
   被误判为越界。

**机制本身是完整的**：`vm_shared_region_slide()`、v1–v4 的
`vm_shared_region_slide_page()`、`shared_region_pager_setup()` 都在这个内核
里编译，没有 `#if` 掉。缺的只是预映射路径没有消费
`dyld_cache_mapping_and_slide_info`、没有填 `sms_slide_start/size`、没有设
`VM_PROT_SLIDE`、也没有把 `srfmp->slide` 交进去。本节补的就是这四件事。

### 2. 真实 cache 的 slide 数据

用 `scripts/check-shared-cache.py` 直接读真实的 44 个文件：51 条映射里只有
**4 条**带 slide info，全部在 `.dylddata` 的 `__DATA_CONST`/`__DATA` 上；
编号 text 分片和 `.dyldlinkedit` 都没有 slide info。

| 文件 | 映射 | 文件内 slide info | 大小 | v2 页数 |
| --- | ---: | --- | ---: | ---: |
| `.27.dylddata` | 1 | `0xb207c08` | `0xa788` | 21,424 |
| `.27.dylddata` | 2 | `0xb1fc000` | `0xbc08` | 24,048 |
| `.41.dylddata` | 1 | `0x618ef58` | `0x5350` | 10,644 |
| `.41.dylddata` | 2 | `0x6188000` | `0x6f58` | 14,232 |

四条都是 `version=2`、`page_size=0x1000`、`page_starts_count = mapping_size /
4096`、`page_extras_count=0`、`delta_mask=0x00ffff0000000000`、
`value_add=0`。slide info 都落在同一个文件最后那条 r-- 映射（map 3）的
文件范围内，所以映射建立后可以按 `map3.address + (slideInfoFileOffset -
map3.fileOffset)` 读到；这不要求 slide info 和被 slide 的映射是同一条。

### 3. 实现

内核侧（`bsd/kern/kebab_dyldcache.c`）：

* 读 56 字节的 `dyld_cache_mapping_and_slide_info`，要求它的
  address/size/fileOffset/maxProt/initProt 与 32 字节 mapping 表逐项相等；
  `slideInfoFileOffset` 和 `slideInfoFileSize` 要么都为 0，要么都非 0。
* 在每个文件内找覆盖 slide info 的映射，把 `sms_slide_start` 先填成
  **unslid** 地址；找不到就拒绝，不做“假定它在某处”的映射。
* 只接受 cache 实际使用的 v2：检查 40 字节头、`page_size==4096`、
  `page_starts_count * 4096 == mapping size`、表项不越界、`delta_mask` 是
  可用的 64 位掩码。v1/v3/v4 内核 pager 支持，但本模块不替它们做预校验，
  遇到会明确拒绝，而不是映射到一半才失败。
* `kebab_dsc_slide=<bytes>`（默认 0）选择 slide。校验：16 KiB 对齐、
  `< maxSlide`、slide 映射数不为 0、能放进 32 位 `srfmp->slide`、且平移后
  每条映射和每个 slide-info blob 都在真实 arm64 region 内。任何一条不满足
  就拒绝整个计划，不映射。
* 非零 slide 时，把所有映射地址和所有非零 `sms_slide_start` 加同一个值；
  对 4 条带 slide info 的映射设 `sms_max_prot |= VM_PROT_SLIDE`；把同一个
  值写进每个 `srfmp->slide`。slide=0 时完全保持原来的 unslid 路径，不设
  `VM_PROT_SLIDE`，不建立 pager。
* 计划日志只打印一次 `slide 0x... (boot arg|default)`；映射成功后额外调
  `vm_shared_region_start_address()`，把
  `shared_region_check_np()` 会返回的 base 和
  `plan->region_start + slide` 对比并打印，便于真机日志核对。
* 映射日志同时打印 `map: slide 0x...` 和 `mapped ... at 0x...`（slid 地址）。

host 侧（`scripts/check-shared-cache.py`）：

* 新增 `--slide N`（十进制或 `0x...`，默认 0）。
* 解析同一张 mappingWithSlide 表、做同样的字段交叉检查、找 blob、校验 v2
  （额外解析 page starts/extras 链，确认没有 `EXTRA` 但 extras 为 0 的页）。
* 用同样的 16 KiB 对齐、`maxSlide`、4 GiB region 边界检查，把同一组地址和
  `slideInfo=` 地址打印出来；`--json` 里也带 `slide`、`slideMappings` 和
  每条映射的 slideInfo。
* 打印格式与内核逐行对应，方便把 host 输出和 `KEBAB DSC:` 启动日志并排比较。

### 4. host checker 的真实输出（非零 slide）

命令：

```sh
python3 scripts/check-shared-cache.py --slide 0x1000000 ~/ios-userland/cryptex-cache
```

关键行（完整输出 100+ 行，这里保留 slide 相关部分）：

```text
KEBAB DSC: region start=0x180000000 size=0xaade8000 end=0x22ade8000 maxSlide=0x55214000
KEBAB DSC: slide 0x1000000 (--slide); 4 mapping(s) carry slide info; shared region [0x180000000, 0x280000000)
KEBAB DSC: dyld_shared_cache_arm64.27.dylddata: mapping 1 slide info v2 at file 0xb207c08+0xa788: page_size=0x1000 pages=21424 extras=0 deltaMask=0xffff0000000000 valueAdd=0x0
KEBAB DSC: dyld_shared_cache_arm64.27.dylddata: mapping 2 slide info v2 at file 0xb1fc000+0xbc08: page_size=0x1000 pages=24048 extras=0 deltaMask=0xffff0000000000 valueAdd=0x0
KEBAB DSC: dyld_shared_cache_arm64.41.dylddata: mapping 1 slide info v2 at file 0x618ef58+0x5350: page_size=0x1000 pages=10644 extras=0 deltaMask=0xffff0000000000 valueAdd=0x0
KEBAB DSC: dyld_shared_cache_arm64.41.dylddata: mapping 2 slide info v2 at file 0x6188000+0x6f58: page_size=0x1000 pages=14232 extras=0 deltaMask=0xffff0000000000 valueAdd=0x0
KEBAB DSC: highest mapped end 0x22bde4000 (unslid 0x22ade4000), cache extent end 0x22ade8000
KEBAB DSC: file 27 sub  dyld_shared_cache_arm64.27.dylddata    size=0xb270000   mapped=0xb214000   vmOffset=0x55908000   uuid=07058ec474213a43b25eec4254f216d7
KEBAB DSC:      map 0x1d6908000 size=0x5c000     file=0x0         init=r-x max=r-x
KEBAB DSC:      map 0x1d6964000 size=0x53b0000   file=0x5c000     init=r-- max=rw- slideInfo=0x1e5b0fc08+0xa788
KEBAB DSC:      map 0x1ddd14000 size=0x5df0000   file=0x540c000   init=rw- max=rw- slideInfo=0x1e5b04000+0xbc08
KEBAB DSC:      map 0x1e5b04000 size=0x18000     file=0xb1fc000   init=r-- max=r--
KEBAB DSC: dry run: plan only (slide 0x1000000), nothing mapped; the kernel maps nothing unless kebab_dsc_map=1
```

边界检查也实际跑过：`--slide 0x1000` 报“不是 0x4000 对齐”，
`--slide 0x55214000` 报“不低于 maxSlide”，`--slide 0x55210000` 通过并给出
`highest mapped end 0x27fff4000`（仍在 `0x280000000` 内）。

### 5. 应用和构建结果

* 在 `xnu-8796.141.3`（`1b191cb`）的干净索引上按文件名顺序应用 83 个 kebab
  补丁（LF 副本）：**83 / 83** 成功，无失败；`0103` 应用到 `0102` 创建的
  `bsd/kern/kebab_dyldcache.c` 后，索引内容与 `verifySc2` 工作树逐字节相同。
* 构建：`~/kebab-build/build-local.sh` 同一套 `dxb/build.sh`，
  `MACHINE_CONFIG=KEBAB`、`KERNEL_CONFIG=DEVELOPMENT`、macOS 13.5 +
  `KDK_13.5_22G74`。
* 产物：`build/xnu.obj/DEVELOPMENT_ARM64_KEBAB/kernel.development.kebab`，
  **17,069,536 字节（16.28 MiB）**，Mach-O 64-bit arm64，SHA-256
  `d2c4678cb896e39da764c5926f3e6f13a633c0da807fdd9fd225a9628e085eda`。
* `kebab_dyldcache.c` 本身 **0 条 warning**；全树日志 14,246 条 warning，
  都是既有文件/头文件的。镜像字符串里能看到
  `KEBAB DSC: slide 0x%llx ...` 和
  `KEBAB DSC: map: shared_region_check_np base ...`，确认新代码进了内核。

### 6. 启动参数

| 参数 | 作用 |
| --- | --- |
| `kebab_dsc=1` 或 `kebab_dsc=<绝对目录>` | 解析并打印计划（dry-run）|
| `kebab_dsc_map=1` | 第一次 exec 时真正映射 |
| `kebab_dsc_slide=<字节>` | 强制 slide，默认 0；支持十进制和 `0x...` |

示例（真机）：

```text
kebab_dsc=1 kebab_dsc_map=1 kebab_dsc_slide=0x1000000
```

三个参数都只在有 `kebab_dsc` 时生效。非法 slide（未对齐、达到/超过
`maxSlide`、没有 slide 映射、越出 region）会让计划直接失败，日志写明原因，
不会退化成“日志有 slide、实际映射 unslid”。

### 7. 仍未验证的内容

* **真机启动**：没有在 OnePlus 8T 上带 3.3 GB cache 启动过。`copyin()` 的
  地址现在是 shared region 内的 slide-info 页；映射建立后该页可读，但在
  `sr_mapping_in_progress` 为真时从 shared region 缺页，是否会与
  `vm_shared_region_map_file()` 的串行化冲突，只有真机能回答。
* **pager 的实际重定位**：v2 的 `page_starts`/`delta_mask` 已按真实 blob
  校验，但 `vm_shared_region_slide_page_v2()` 在 4 KiB chunk 上执行、映射
  本身按 16 KiB VM 页建立，只有真机 fault 才能确认。
* **dyld 的视角**：`shared_region_check_np()` 应返回
  `0x180000000 + slide`，dyld 再从 header 反推 slide；内核日志会打印这个
  base，但还没有一个用户态程序实际调用过它。
* **只校验 v2**：内核 pager 支持 v1–v4，本模块只对 v2 做预校验；别的版本
  会被拒绝，而不是猜着映射。
* **slide=0 路径**：保持第十四节的 unslid 行为，没有建立 pager；这条路径
  同样没有真机验证。
* 第十四节列出的根卷容量、`.symbols` 不映射、dyld 本身仍来自 OS 卷等问题
  仍然存在；它们与 slide 无关。

本节的所有 slide 结论都来自源码、真实 cache 文件、host checker 和一次成功
编译，不能替代真机结论。

## 十七、KDP 的接收路径

第十节把 serial KDP 接到了 CDC-ACM 上，真机结果是：panic 报告能出来，日志停在
`Using serial KDP.` / `Waiting for remote debugger connection.`，而 LLDB 的
`kdp-remote` 报 `error: KDP_REATTACH failed`，桥一个字节都没打印。本节先把调用
链和门的归属查清楚，再说改了什么。

### 1. 两个方向的真实调用链

接收（调试器 → 内核），全部在 `osfmk/kdp/kdp_udp.c` 的
`kdp_connection_wait()` 里循环：

```c
static void
kdp_serial_receive(void *rpkt, unsigned int *rpkt_len, unsigned int timeout)
{
	...
	for (clock_get_uptime(&now); now < deadline; clock_get_uptime(&now)) {
		readkar = pal_serial_getc();
		if (readkar >= 0) {
			if ((packet = kdp_unserialize_packet((unsigned char)readkar, rpkt_len))) {
```

`pal_serial_getc()` 在 `0099` 之后改走 USB（`osfmk/arm/pal_routines.c`）：

```c
	if (kebab_usb_kdp_selected() && kebab_usb_console_ready()) {
		return kebab_usb_getc(false);
	}
	return serial_getc();
```

`kebab_usb_getc()` 是唯一把主机字节从 `ud.in_ring` 里取出来的函数，它在改写后长
这样：

```c
	if (ud_enter(&irq)) {
		ud.console_input = true;
		ud_poll_locked();
		if (ud.in_tail != ud.in_head) {
			c = (unsigned char)ud.in_ring[ud.in_tail];
```

`ud_poll_locked()` 才是真正服务控制器的地方：读 `GEVNTCOUNT`、逐条派发事件、
在 `ud_service()` 里重新装填 bulk OUT、在 `ud_pump_log()` 里发 bulk IN。也就是
说，**接收的每一步都以拿到 `ud.owner` 为前提**。

发送（内核 → 调试器）走另一条路，但门是同一把：

```c
void
kdp_serialize_packet(unsigned char *packet, unsigned int len, void (*outFunc)(char))
```

`outFunc` 是 `pal_serial_putc_nocr()`，它按 `0xFB` 攒整帧，然后
`kebab_usb_log_flush()` → `ud_enter()` → `ud_flush_from_panic()` →
`ud_poll_locked()` → `ud_pump_log()` → `ud_start(UD_TRB_BULKIN, EP_BULK_IN, ...)`。
panic 报告之所以能整篇出来，就是因为 `kebab_usb_log()` 里有这一句：

```c
	if (panic_active()) kebab_usb_log_flush();
```

### 2. panic 期间门到底在谁手里

`kebab_usb_panic_spin()`（`panic_spin_forever()` 的终端循环）确实是“拿走整条传输”
的那一方：它先 `os_atomic_cmpxchg(&ud.panic_cpu, 0u, cpu, acq_rel)`，再由
`ud_panic_takeover()` 接管。但它**没有**把门带进 KDP：

```c
		if (ud.running && ud_enter(&irq)) {
			ud_poll_locked();
			if (ud.probe_requested && ud.configured && !ud.dead)
				kebab_usb_ep0_probe_send();
			ud_leave(irq);
		}
		if (kebab_usb_kdp_enabled() && kebab_usb_kdp_pending()) {
			...
			(void)kdp_serial_panic_reenter();
```

`ud_leave()` 在 `kdp_serial_panic_reenter()` 之前，交还 KDP 时门是空的。而且本次
实测连这一步都没到：panic 报告之后是 `debugger_collect_diagnostics()` 里的
`kdp_raise_exception()` 直接进的 KDP，`ud.panic_cpu` 还是 0，连 panic CPU 栅栏
都不存在。

再看一个反证：`Using serial KDP.` / `Waiting for remote debugger connection.`
是 `handle_debugger_trap()` 里 `DebuggerHaltOtherCores()` **之后**打印的，而这两行
只可能通过 `kebab_usb_log()` → `kebab_usb_log_flush()` → `ud_enter()` 出来。它们
出现在主机上，就说明那一刻门是拿得到的。

所以最初的假设——“panic/KDP 路径一直握着门，`kebab_usb_getc()` 进不去”——**在
这次真机会话里不成立**。代码能证明的是另一件事：`ud_enter()` 是**不可重入**的，
而且拿不到就立刻返回，`kebab_usb_getc()` 于是**静默返回 -1、连一次 poll 都不做**：

```c
	if ((terminal && terminal != cpu) ||
	    !os_atomic_cmpxchg(&ud.owner, 0u, cpu, acquire)) {
		ml_set_interrupts_enabled(*irq);
		return false;
	}
```

谁握着门都可能让一次读失败：panic 泵、任何一次 console 冲刷、事件线程，或者
`ud.panic_cpu` 这道永久栅栏（它一旦设置，除 panic CPU 外的任何 CPU 都进不来）。
从外面看，这跟“主机没发数据”完全一样——这正是上一轮无法判断的原因。

### 3. 这次收不到应答的真正原因

请求没有变成“KDP 包”。`kdp_poll()` 只接受完整的 Ethernet/IPv4/UDP 信封：

```c
	if (pkt.len < (sizeof(struct kdp_ether_header) + sizeof(struct kdp_udpiphdr))) {
		return;
	}
	pkt.off += (unsigned int)sizeof(struct kdp_ether_header);
	if (ntohs(eh->ether_type) != ETHERTYPE_IP) {
		return;
	}
	...
	if (ntohs(ui->ui_dport) != KDP_REMOTE_PORT) {
```

三个 `return` 都不打印、不计数。串行帧本身是透明的（`kdp_serial.c` 只管
`0xFA`/`0xFB` 定界和 CRC），所以只要桥把 LLDB 的裸 KDP 数据报原样塞进帧里，
`kdp_poll()` 就会在解析成 KDP 请求之前把它丢掉：内核不会回任何东西，桥（只在
成功解出一帧时才打印）也就一个字节都不显示。这解释了全部现象，而且和“`EP0?`
能用”不矛盾——`EP0?` 是驱动自己在 `ud_bulk_out_done()` 里匹配的文本，根本不经过
KDP。

主机侧桥现在会把裸 KDP 数据报补上信封再发（见 `scripts/kdp-cdc-bridge.py`），
本节的内核侧改动解决的是另一件事：让这类失败不再无声。

### 4. 改了什么

`bsd/dev/kebab_usb.c`（`0104-kebab-kdp-receive.patch`）：

* **接收锁与发送门分开**。新增 `ud_rx_enter()` / `ud_rx_leave()`，只保护“排空事件
  缓冲 + 重新装填 bulk OUT”，`kebab_usb_getc()` 只用它，不再碰 `ud.owner`。接收
  因此不依赖发送归谁：panic 泵、console 冲刷、事件线程谁握着发送都不影响读。
* **接收锁不对 CPU 设栅栏**。它不看 `ud.panic_cpu`，任何正在运行的 CPU 都能取；
  取不到就返回，不等待，不睡眠（调试器本来就是轮询）。
* **两把锁都可重入**。同一 CPU 再次进入只加深度计数，所以“panic 泵在临界区里
  再交还 KDP”这种同一栈上的嵌套不会自锁。顺序是单向的：持发送锁时可以取接收
  锁，反过来不允许，因此不会成环。
* **发送仍然单所有者**。bulk IN 的提交点只有 `ud_poll_locked()` 和
  `ud_flush_from_panic()` 的循环，两者都持有接收锁，所以 `tx_pending`/`ep[].busy`
  仍然一次只有一个写者；`ud.owner` 的语义（panic 之后只有 panic CPU 能驱动冲刷）
  没有变，只是它的持有者可以重入。
* **接管时也处理被卡住的接收锁**。`ud_panic_takeover()` 原来只对 `ud.owner` 做
  “CPU 确实已停机 → 控制器复位 → 清锁”，现在对 `ud.rx_owner` 用同一条规则：事件
  派发停在半路和传输停在半路一样，都不能靠猜。
* **panic 泵的 KDP 交还条件改成“环里有 `0xFA`”**。原来是只看队首字节
  （`in_ring[in_tail] == 0xfa`），一个残留的 console 字节或半截帧会让交还永远不发生，
  这些字节又没人消费，调试器整轮都连不上。序列化器会把帧内所有 `0xFA` 转义，所以
  环里出现 `0xFA` 只可能是帧头；KDP 的反序列化器本来就会丢弃它之前的字节。

### 5. 新的可观测计数

`KEBAB EP0 STATE:` 那一行**没有改**（`events * 4 == acked`、`discarded`、行编码
的既有判定规则都不变），在它后面加一行：

```text
KEBAB KDP STATE: host_in=%u in_drop=%u kdp_in=%u kdp_frames=%u kdp_out=%u tx_bytes=%u owners=%u/%u/%u depths=%u/%u
```

| 字段 | 含义 |
| --- | --- |
| `host_in` | 从 bulk OUT 收进 `in_ring` 的字节数 |
| `in_drop` | 因为 `in_ring` 满而丢掉的字节数 |
| `kdp_in` | 交给字节流读者（`pal_serial_getc`）的字节数 |
| `kdp_frames` / `kdp_out` | 交给发送的调试器帧数 / 这些帧的线上字节数 |
| `tx_bytes` | bulk IN 实际完成的字节数 |
| `owners` | `ud.owner` / `ud.rx_owner` / `ud.panic_cpu`，都是 CPU+1，0 表示空 |
| `depths` | 两把锁的重入计数 |

判读方法就是这一节存在的理由：`host_in` 一直是 0 → 一个字节都没到设备；
`host_in` 涨而 `kdp_in` 不涨 → 字节到了、读者没跑（锁/上下文问题）；
`kdp_in` 涨而 `kdp_frames` 一直是 0 → 内核读到了却没打算回答（信封/协议层）；
`kdp_frames` 涨而主机看不到帧 → 发送侧问题。`owners` 里出现非 0 的第三方 CPU 就是
锁被卡住的直接证据。

### 6. 真机验证

启动参数不变（`debug=0x14e serial=3 kebabusb=1 kebabpanic=1 kebabkdp=1`，不要加
`0x200`），主机上先起桥再让内核 panic：

```text
python3 scripts/kdp-cdc-bridge.py /dev/ttyACM0 --udp-port 41139 --verbose
```

看到 panic 报告和 `Waiting for remote debugger connection.` 之后，在另一台机器上：

```text
lldb -o "kdp-remote <bridge-host>:41139" -o "register read" -o "continue" -o "quit"
```

判定顺序：

1. `register read` 能打出 `x0..x30`/`pc`/`cpsr`，说明 REATTACH、READREGS 两个来回
   都通了，接收路径成立。
2. 若仍失败，让内核再 panic 一次并在 panic 循环里发一次 `PANIC!`，然后重新打开
   端口、发 `EP0?` 取快照，读新增的 `KEBAB KDP STATE:` 一行，按第 5 节的四种情形
   定位：0 字节 / 到了没读 / 读了没答 / 答了没出去。
3. 回归项照旧：整篇 panic 报告、panic 后关端口再打开、`EP0?` 有应答、
   `events * 4 == acked`、`discarded == 0`、`line=` 保持主机设置的行编码。

### 7. 本轮的证明

* `0104` 加入后整串 **84 / 84** 按文件名顺序应用到 `1b191cb`（独立 worktree，
  `reset --hard 1b191cb` + `clean -fdx`，逐个 `git apply`）。
* `~/kebab-build/build-local.sh` 同一套 `dxb/build.sh`（`MACHINE_CONFIG=KEBAB`、
  `KERNEL_CONFIG=DEVELOPMENT`、macOS 13.5 + `KDK_13.5_22G74`）产出
  `build/xnu.obj/DEVELOPMENT_ARM64_KEBAB/kernel.development.kebab`，
  **17,085,984 字节（16.29 MiB）**，SHA-256
  `a1bd7e3f86e33503d9fff22901a2539056a3436e235c627a071dbbd1468df070`
  （加 `0104` 之前是 17,069,536 字节）。镜像里有
  `KEBAB EP0 STATE:` 和新的 `KEBAB KDP STATE: host_in=%u ...` 两条格式串，
  `_kebab_usb_kdp_tx_frame`、`_kebab_usb_getc`、`_kebab_usb_log_flush`、
  `_kebab_usb_panic_spin` 都在符号表里。
* `kebab_usb.c`：38 条 warning，全部是既有的
  `-Wdeclaration-after-statement`（改前 35 条；这个文件一直是 C99 风格，构建配置
  本来就用 `-Wno-error=declaration-after-statement` 放行），**0 error**；
  `pal_routines.c` 0 warning。

本轮没有真机，本节所有结论都来自源码、`84/84` 的补丁应用和一次内核编译，不能
替代上面这次握手。

## 十八、控制台的重启命令

本节记录给 CDC-ACM 调试控制台加的两条恢复命令，以及为它们补上的平台重启后端。
代码是 `patches/xnu-8796-kebab/0105-kebab-console-reboot.patch`；没有重生成
`0091`，因为 `0104` 已经示范了在 `0091` 创建的 `bsd/dev/kebab_usb.c` 上追加
补丁的写法。

### 1. 命令与匹配方式

| 命令 | 一次 bulk OUT 里的字节 | 作用 |
| --- | --- | --- |
| `REBOOT` | `REBOOT\n`（7 字节） | 走平台重启路径重启机器 |
| `FASTBOOT` | `FASTBOOT\n`（9 字节） | 写 IMEM 的 `restart_reason`，再走同一条重启路径 |

两条命令都在 `ud_bulk_out_done()` 里匹配，匹配窗口就是**这一次** bulk OUT
完成的 `received` 字节，命令被拆成两次写入不会触发。普通控制台会扫描整个
完成窗口（和 `PANIC!` 的写法一致）；`kebabkdp=1` 时字节流属于调试器，只认
从传输首字节开始的命令，因为 KDP 帧固定以 `0xFA` 开头，不会和 `REBOOT` 的
首字母 `R` 混淆。

命令只记录在 `ud.reboot_command`，执行点不在匹配函数里：`ud_thread()` 和
`kebab_usb_panic_spin()` 都在 `ud_leave()` 之后才调用 `ud_run_reboot_command()`。
这样重启路径里的 `printf` 可以正常冲刷日志，也不会在还握着 `ud.owner` 时
调用 `PEHaltRestart()`。`EP0?` 和 `PANIC!` 的原有路径没有改动。

### 2. REBOOT：平台入口原来没有后端

`REBOOT` 调用 `PEHaltRestart(kPERestartCPU)`，没有直接写复位寄存器。但检查
这棵树后发现，平台入口本身是空的：

* `iokit/Kernel/IOPlatformExpert.cpp` 里只有
  `int (*PE_halt_restart)(unsigned int type) = NULL;`，全树没有任何地方给
  它赋值；
* `IOPlatformExpert::haltRestart()` 在 `PE_halt_restart == NULL` 时直接
  `return -1;`
* `IODTPlatformExpert::haltRestart()` 只是 `return super::haltRestart(type);`
* `KebabPlatformExpert` 原来的 `haltRestart()` 只处理 panic 停留，最后仍然
  落到这条空路径。

也就是说，只加 `PEHaltRestart(kPERestartCPU)` 会返回 -1，机器不会重启。
`0105` 因此在 `KebabPlatformExpert::haltRestart()` 里补了
`kPERestartCPU` 的后端：用 `smc #0` 调 PSCI 的 `SYSTEM_RESET`
（`0x84000009`），和 `KebabCPU.cpp` 里 `PSCI CPU_ON` 用的是同一个固件接口。
入口仍然是 `PEHaltRestart()`，不是驱动自己的复位寄存器；PSCI 成功时调用
不返回，返回的值是错误码，由驱动打印。

panic 循环里还有第二个坑：`haltRestart()` 看到 `panic_active()` 且控制台
ready 时会调用 `kebab_usb_panic_spin()`。如果控制台自己请求重启，这会第二次
进入 panic 循环；新循环在 `ud.panic_cpu` 的 `cmpxchg` 失败后永远等下去，机器
仍然不会重启。驱动因此导出 `kebab_usb_restart_armed()`，平台专家在
`haltRestart()` 的 panic 停留判断里排除“控制台已经请求重启”这一种情况。

### 3. FASTBOOT 的 IMEM 地址与证据

地址不是猜的，来自设备自己的 DTB。本机缓存的
`/home/albert/tmpwork/uefisheng/Resources/DTBs/kebab.dtb` 解出的节点是：

```text
/soc/qcom,msm-imem@146bf000
        compatible = "qcom,msm-imem"
        reg = <0x146bf000 0x1000>
        ranges = <0x00 0x146bf000 0x1000>

/soc/qcom,msm-imem@146bf000/restart_reason@65c
        compatible = "qcom,msm-imem-restart_reason"
        reg = <0x65c 0x04>
```

同一代 SM8250 的 `alioth.dts` 里也是同样的 `<0x146bf000 0x1000>` 和
`<0x65c 0x04>`。驱动在 `kebab_usb_start()` 里用 `ml_io_map()` 把这一页
**预先映射**好（正常上下文，不在 panic 里分配 VM），`FASTBOOT` 时直接对
`imem + 0x65c` 做 4 字节读写。这样 panic 循环不需要临时 `kmem_alloc()`，
也就不会在 panic 上下文里等 VM 锁。

### 4. cookie 值只是假设

* 默认值 `0x77665501` 是社区资料里“bootloader/fastboot”的值，**没有在本机
  真机上验证过**。
* 启动参数 `kebab_fastboot_cookie=<hex>` 可以在不重编内核的情况下换值；例如
  `kebab_fastboot_cookie=0x77665501`。不写这个参数时用默认值。解析用
  `PE_parse_boot_argn()`，支持 `0x` 前缀。
* 写之前先读旧值，日志会同时打印旧值、新值和字段地址；写完执行 `dsb sy`，
  再调用 `PEHaltRestart()`。
* 如果启动时 IMEM 映射失败，日志会明确打印“cannot map IMEM”，然后回退到
  普通的 `REBOOT`，不会假装已经写了 cookie。
* 如果 `PEHaltRestart()` 返回错误（说明没有重启成功），驱动会把旧值写回去并打印，
  避免下一次手动重启意外进入 fastboot。

**风险要写清楚**：cookie 不对时，机器可能进入 recovery，也可能进入 EDL。进入
recovery 还可以再重启；进入 EDL 需要 firehose/QFIL 这一类工具，本项目没有，
恢复成本高。因此在真机上应先只验证 `REBOOT`，确认平台重启路径真的能复位之后，
再单独试 `FASTBOOT`；测试循环的日常重启应当用 `REBOOT`。

### 5. 启动参数与主机侧用法

控制台仍然需要 `kebabusb=1`；用 `PANIC!` 触发测试 panic 还需要 `kebabpanic=1`，
真实 panic 不需要它（`kebabpanic` 只控制 `PANIC!` 钩子）。`kebabkdp=1` 时
字节流是 KDP 的，文本命令只认传输首字节开始的形式；如果要在 panic 后用文本
命令，建议这一轮不要加 `kebabkdp=1`。

主机一次写入整条命令，例如：

```python
import serial
ser = serial.Serial("COM7", 115200, timeout=0.2)
ser.write(b"REBOOT\n")     # 一次 write，不能逐键敲
ser.flush()
```

`FASTBOOT` 同理：

```python
ser.write(b"FASTBOOT\n")
```

### 6. 设备落到意外位置时怎么恢复

* 停在 fastboot 界面：主机执行 `fastboot reboot`（或 `fastboot continue`），
  不要继续刷未知镜像。
* 停在 recovery：直接重启，或按现有流程回 Android。
* 停在 EDL（9008）：需要 firehose/QFIL，本项目没有这套工具；这是
  `FASTBOOT` 最坏的结果，也是先验证 `REBOOT` 的原因。
* 完全没有 USB 响应：长按电源键约 10 秒强制重启；仍然不行时按 README 的
  刷机流程处理。`boot_a` 的 Android 镜像在测试期间不要动，这是既有的回退约定。

### 7. 本轮的证明与未验证项

* 在 `xnu-8796.141.3` 的 `1b191cb` 上，独立 worktree 先
  `git reset --hard 1b191cb`、`git clean -fdx`，再按文件名顺序应用
  `0000`–`0104` 和 `0105`：**85 / 85 成功，0 失败**。
* 构建机 `~/kebab-build/build-local.sh`（`MACHINE_CONFIG=KEBAB`、
  `KERNEL_CONFIG=DEVELOPMENT`、macOS 13.5 + `KDK_13.5_22G74`）产出
  `build/xnu.obj/DEVELOPMENT_ARM64_KEBAB/kernel.development.kebab`，
  **17,086,064 字节（16.29 MiB）**，Mach-O 64-bit arm64，SHA-256
  `77a8e4610009b1b2e21250cd82a6e2dd0f467338f83efff5e2a7485f119a15b1`。
* 镜像字符串里有 `KEBAB REBOOT: PEHaltRestart(kPERestartCPU)`、
  `KEBAB FASTBOOT: IMEM restart_reason ...`、`kebab_fastboot_cookie` 等格式串；
  符号表里有 `_kebab_usb_restart_armed` 和 `_PEHaltRestart`。
* `kebab_usb.c` 本轮 **45 条 warning，全部是同一类
  `-Wdeclaration-after-statement`**（这个文件本来就是 C99 风格，构建配置用
  `-Wno-error=declaration-after-statement` 放行），0 error；
  `KebabPlatformExpert.cpp` 只有 1 条既有的 `-Wshadow-field`
  （`processTopLevel` 的 `root` 参数），不是新代码引入的。

没有真机，以下都还没有验证：

* `PEHaltRestart(kPERestartCPU)` → PSCI `SYSTEM_RESET` 是否真的复位；
  PSCI 的 SMC conduit 和函数号来自同树的 `CPU_ON` 用法，但没有在 kebab 上
  单独调用过 `SYSTEM_RESET`。
* ABL 是否真的读取 `restart_reason@65c` 并识别 `0x77665501`；cookie 的语义
  目前只是假设。
* IMEM 写入是否在 PSCI 复位后仍然保留（按 IMEM 的定位应当保留，但未实测）。
* 普通控制台路径和 panic 循环路径都只做了编译与补丁应用验证，没有在硬件上
  分别发过 `REBOOT\n` / `FASTBOOT\n`。
* `kebabkdp=1` 时的首字节匹配、以及 PM 关闭通知对 USB 事件线程的影响，都
  需要真机会话确认。

## 十九、Fastboot 进入条件的诊断

本轮要回答的问题：控制台 `FASTBOOT` 已经把 Qualcomm 的 restart reason 写进 IMEM
并复位，为什么板子没有进 fastboot。

测量用的镜像是 `XNU-151-FbDiag.img`（`0106-kebab-fastboot-diagnosis.patch`，
CI run 40449214321），刷 `boot_b`。启动参数与基线一致：

```text
-v -enable_kprintf_spam startup_debug=1 serial=3 debug=0x14e cpus=1
rd=ufsroot0 ufsroot=userdata ufsrw=1 rootrw=1 kebabusb=1 kebabpanic=1
kebabkdp=1 kebab_fastboot_cookie=0x77665500 keepsyms=1
```

主机侧：Windows，COM7（CDC-ACM，`VID_1209&PID_0001`），
`D:\TOOL\platform-tools\fastboot.exe`。命令必须一次 bulk OUT 写完，否则内核不匹配。

### 1. PSCI：先测，不猜

`0106` 在 `kebab_usb_start()` 里用 SMC 调 `PSCI_VERSION`，再对
`SYSTEM_RESET`（`0x84000009`）和 `SYSTEM_RESET2`（`0xC4000012`）各调一次
`PSCI_FEATURES`（`0x8400000A`）。启动日志被 `-v` 的 16 KiB 环冲掉，
所以下机后又用 `PSCI?` 复测，结果一致：

```text
KEBAB PSCI: version 0x00010001; FEATURES(0x84000009)=0 FEATURES(0xc4000012)=0
KEBAB PSCI: SYSTEM_RESET2 NOT_SUPPORTED
```

读数是 **0**（PSCI_FEATURES：0 = not supported）。按规则记
`SYSTEM_RESET2 = NOT_SUPPORTED`，没有换成 vendor 私有的 reset type，
`FASTBOOT2` 命令因此拒绝执行（`ud.psci_reset2 != 1` 时只打印、不复位），
也没有强行调用未声明的功能。

**这条读数自相矛盾，是独立的一条发现**：PSCI 版本 1.1 里 `SYSTEM_RESET`
是强制功能，而且它实测有效（`REBOOT` 和 `FASTBOOT` 都靠它复位），
`PSCI_FEATURES(0x84000009)` 却返回 0。也就是说这颗固件的
`PSCI_FEATURES` 是一个对已实现功能也回答 0 的桩：
`0xC4000012 -> 0` 只能说明"固件没有声明支持"，不能证明 `SYSTEM_RESET2`
一定不存在。**在这台机器上，任何依赖 PSCI_FEATURES 做能力探测的东西都不可信**，
包括以后想用 `SYSTEM_RESET2`、`SYSTEM_OFF` 或其它可选功能的代码。

### 2. IMEM 写入路径：写进去了，也确实读得回来

`FASTBOOT` 现在先打印旧值，写 `0x77665500`，`dsb sy` 后回读，再冲刷日志、
等 300 ms 才复位。实测：

```text
KEBAB FASTBOOT: cookie 0x77665500 from kebab_fastboot_cookie
KEBAB FASTBOOT: IMEM restart_reason at 0x146bf000+0x65c is 0x0b39bda4; writing 0x77665500
KEBAB FASTBOOT: read back 0x77665500 (MATCH)
KEBAB REBOOT: PEHaltRestart(kPERestartCPU)
```

回读 **MATCH**，说明 `0x146BF65C` 的映射、写和读都成立，写操作本身没有丢。
写前值是 `0x0b39bda4`，既不是 0 也不是 cookie。

### 3. 三次复位的实测矩阵

每一次都记录：写前 IMEM、写后回读、PSCI 调用、是否进 fastboot、是否正常启动、
是否出现 crash dump、下次启动时 IMEM 的值。时间以命令发出为零点。

| # | 命令 | 复位调用 | 写前 IMEM | 回读 | 进 fastboot | 正常启动 | crash dump | 下次启动 IMEM |
|---|------|----------|-----------|------|-------------|----------|------------|----------------|
| 0 | 刷机后 `fastboot reboot` | ABL 冷启动 | — | — | 否 | 是 | 无 | `0x0b39bda4` |
| 1 | `FASTBOOT` | `PEHaltRestart(kPERestartCPU)` → `SYSTEM_RESET` | `0x0b39bda4` | `0x77665500` MATCH | **否** | 是，1.0 s 端口消失 / 19.8 s 回来 | 无 | `0x836999ac` |
| 2 | `REBOOT`（回归） | 同上，`SYSTEM_RESET` | — | — | 否 | 是，1.0 s / 19.8 s | 无 | `0x532989ac` |
| 3 | `REBOOT`（收尾再验一次） | 同上，`SYSTEM_RESET` | — | — | 否 | 是，1.0 s / 19.8 s | 无 | `0x616998ac` |

crash dump 的判据是主机上不出现 `VID_05C6&PID_900E`（QUSB_BULK）；
复位后 USB 上只有 `VID_1209&PID_0001`，没有别的 Qualcomm 设备。
"下次启动 IMEM" 是板子重新枚举后在控制台上立刻发 `IMEM?` 读到的值。
启动时的 `KEBAB USB: IMEM mapped ... boot read 0x...` 探针也在，但这段早于
USB 枚举的日志被 `-v` 的启动刷屏冲掉了；`IMEM?` 在同一启动内读三次值都一样
（例如 `0x836999ac` ×3），所以它和启动时的读数等价。

**最重要的一条结果在第 2、3 行**：`REBOOT` 根本没有写 IMEM，复位后 `0x65c`
仍然从 `0x836999ac` 变成 `0x532989ac`、再变成 `0x616998ac`。四次启动四个值，
没有一个等于 OS 写的 cookie。也就是说，每次启动都有比内核更早的环节
（XBL / ABL / UEFI 中的一个或多个）在重写这个单元，OS 在复位前写的 cookie
到不了 ABL 做启动模式判断的那个时刻。

复位调用不是差别：`REBOOT` 和 `FASTBOOT` 走的是同一条 `SYSTEM_RESET`，
一个能复位，另一个也能复位（端口都在 1.0 s 消失、19.8 s 回来），
区别只在那个没被保住的 IMEM 值。**所以本机这条启动链上，
"写 IMEM + `SYSTEM_RESET`" 这条路本身不成立，不是复位方式选错了。**

### 4. vendor 源码核对（为 Stage B 打底）

全部来自 LineageOS `android_kernel_oneplus_sm8250`，分支 `lineage-24.0`，
不是凭记忆：

* `drivers/power/reset/msm-poweroff.c`，`msm_restart_prepare()`：
  `cmd = "bootloader"` 时先 `qpnp_pon_set_restart_reason(PON_RESTART_REASON_BOOTLOADER)`，
  再 `__raw_writel(0x77665500, restart_reason)`。同一个 `else` 分支写的是
  `0x77665501`。**所以 `0x77665500` 才是 bootloader 值**，启动参数里的
  `kebab_fastboot_cookie=0x77665500` 是对的，`0105` 原来默认的 `0x77665501`
  反而是"普通重启"。
* `arch/arm64/boot/dts/vendor/qcom/pm8150.dtsi`：`qcom,pm8150@0` 的
  `reg = <0x0 SPMI_USID>`（SID 0），`qcom,power-on@800`（PON base `0x0800`），
  并且带 `qcom,store-hard-reset-reason`（没有这个属性
  `qpnp_pon_set_restart_reason()` 直接返回 0，不写）。
* `drivers/input/misc/qpnp-power-on.c`：
  `QPNP_PON_PERPH_SUBTYPE(pon) = base + 0x05` → **`0x0805`**；
  `QPNP_PON_SOFT_RB_SPARE(pon) = base + 0x8F` → **`0x088F`**；
  `is_pon_gen2()` 是 subtype `0x04`（GEN2_PRIMARY）或 `0x05`（GEN2_SECONDARY）；
  Gen2 分支 `masked_write(0x088F, GENMASK(7,1)=0xFE, reason << 1)`，
  `PON_RESTART_REASON_BOOTLOADER = 0x02` → 写进 bit7:1 的值是 **`0x04`**；
  Gen1 分支才是 `GENMASK(7,2)=0xFC`、`reason << 2`。
* `arch/arm64/boot/dts/vendor/qcom/kona.dtsi`：
  `qcom,msm-imem@146bf000` + `restart_reason@65c`（4 字节）；
  SPMI arbiter 是 `qcom,spmi@c440000`，`reg-names = "core","chnls","obsrvr","intr","cnfg"`，
  对应 `0xc440000` / `0xc600000` / `0xe600000` / `0xe700000` / `0xc40a000`，
  `qcom,ee = <0>`。

### 5. Stage B 的判定：BLOCKED（SPMI 不存在）

Stage A 没有成功，所以按计划做 Stage B。Stage B 的第一件事是只读探测 PON 的
`0x0805`，确认这颗 PMIC 是不是 Gen2；但这个问题在本内核上**无法回答**：

* XNU 树里没有任何 SPMI 控制器驱动。`grep -ri spmi` 只命中
  `EXTERNAL_HEADERS/acpi/Acpi_v1.h` 里一个无关的词；`bsd/conf/files`、
  `iokit/conf/files*`、`configs/kebab_defconfig` 里都没有 SPMI 相关项。
* KDK 里的 `AppleSPMI.kext` 是 Apple Silicon 的 macOS 驱动（二进制），
  不参与这个内核的构建，也不匹配 kebab 的设备树。
* `package/BootReset.c` 里确实有一套直接操作 SPMI PMIC-arbiter 的代码，
  但那是 **sheng（SM8550）** 的，arbiter 地址（`0xc400000` 等）和
  kona（`0xc440000` 等）不是一套；它也没有编进 kebab 内核。
  不能把它当成"已经有 SPMI"。
* 没有任何东西"已经占用"arbiter：XNU 侧没有驱动，也就没有 APID/EE 的
  ownership 声明。

因此 Stage B 的结论是 **BLOCKED**：没有 SPMI 事务，读不了 `0x0805`，
确认不了 Gen2；**Stage C 的 `0x088F` 写操作 NOT_RUN**，一个 PMIC 寄存器都没写。

如果将来补上 SPMI，要做的事是明确的（可直接照此实现）：

1. **读 subtype**：SID 0、PON 基址 `0x0800`、寄存器 `0x0805`
   （`QPNP_PON_PERPH_SUBTYPE`）。只有读到 `0x04` 或 `0x05` 才是 Gen2；
   `0x01`/`0x02`/`0x03` 是 Gen1/1REG，掩码和移位都不同，必须停下重算。
2. **写 reason（仅当 Gen2）**：SID 0、寄存器 `0x088F`
   （`QPNP_PON_SOFT_RB_SPARE`），读改写
   `new = (old & 0x01) | (0x02 << 1)`，即 mask `0xFE`、值 `0x04`；
   写完回读，确认 bit7:1 = `0b0000010`。
3. **缺的基础设施**（这是 BLOCKED 的具体内容）：
   * 一个 SM8250 `qcom,spmi-pmic-arb` 驱动，映射 core `0xc440000`、
     chnls `0xc600000`、obsrvr `0xe600000`、intr `0xe700000`、
     cnfg `0xc40a000`；
   * EE0 的 APID 映射：在 cnfg 空间里为 SID 0 / PON 外设找到 APID，
     再核对 ownership 表（`qcom,ee = <0>` 要求 owner 是 EE0）；
   * 命令编码与超时轮询：`PMIC_ARB_CMD`（core+`0x00`）、
     `PMIC_ARB_CONFIG`（+`0x04`）、`PMIC_ARB_STATUS`（+`0x08`）、
     `WDATA0/1`、`RDATA0/1`，单次最多 8 字节，超时按 1000 µs 量级处理；
   * 没有这些，"读一个 PMIC 寄存器"都不是一条已建立的操作，
     更不能拿一个猜的地址去写。

### 6. 判定：FASTBOOT 仍然不可用，失败点不在 cookie，也不在写入

| 项 | 结果 | 读数 / 说明 |
|----|------|-------------|
| A1 `PSCI_FEATURES(0xC4000012)` | **PASS**（测量完成） | `0` → 记 `NOT_SUPPORTED`；同一次 `SYSTEM_RESET` 也返回 0，故 FEATURES 是桩，0 不等于不存在 |
| A2 IMEM 写入 + 回读 | **PASS** | `0x0b39bda4` → 写 `0x77665500` → 回读 `0x77665500` MATCH |
| A3 `SYSTEM_RESET2` 单测 | **NOT_RUN** | 固件未声明支持；按规则不强行调用未声明功能，也不换 vendor reset type |
| A4 每次尝试的记录 | **PASS** | 见第 3 节矩阵（写前值、回读、PSCI 调用、fastboot、正常启动、crash dump、下次启动值） |
| A5 启动后最早可观察的 restart reason | **PASS**（有保留） | 探针已加；启动日志被 `-v` 刷屏冲掉，用同一启动内的 `IMEM?` 等价读数；不是 ABL 读它的那一刻 |
| B1 vendor 源码确认 | **PASS** | SID 0、`0x0800`、`0x088F`、`0x0805`、Gen2=`0x04/0x05`、mask `0xFE`、`0x02<<1`，全部有出处 |
| B2 只读探测 `0x0805` | **BLOCKED** | 没有 SPMI 控制器驱动，发不出 SPMI 事务 |
| B3 SPMI 是否可用 | **PASS**（调查完成） | 无驱动、无 owner；`BootReset.c` 是 SM8550 的另一套地址 |
| B4 读数不一致就停 | **PASS** | 一个 PMIC 寄存器都没写 |
| C `SOFT_RB_SPARE` 原型 | **NOT_RUN** | B 已 BLOCKED |
| 回归：`REBOOT` 基线 | **PASS** | 收尾再验一次：1.0 s 端口消失、19.8 s 回来，控制台可用 |

**结论要写清楚**：

* `FASTBOOT` 命令**仍然不可用**——它写 IMEM、复位，但板子正常启动，不进 fastboot。
* 失败**不是** cookie 值不对：vendor 源码确认 `0x77665500` 就是 bootloader 值，
  启动参数里的值是对的（`0105` 原来的默认 `0x77665501` 才是普通重启值）。
* 失败**也不是**写没写进去：回读 MATCH，映射和读写都成立。
* 失败**也不是**复位方式：`REBOOT` 和 `FASTBOOT` 用同一条 `SYSTEM_RESET`，
  都能复位。
* 已证明的失败点是：**OS 写进 `0x65c` 的 cookie 不保留到下一次启动**。
  四次启动四个不同的值，`REBOOT` 没写它也会变，说明有更早的启动环节在
  每次启动时重写这个单元；ABL 做判断时看到的不是 OS 写的值。
* 与 vendor 行为对照，缺的另一半是 **PMIC PON 的 restart reason**
  （`PON_RESTART_REASON_BOOTLOADER`，Gen2 下写 `0x088F` 的 bit7:1）。
  本内核没有 SPMI，无法复现 vendor 的完整路径，所以这部分是 BLOCKED，
  不是"已经排除"。

### 7. 本轮刷机与测试日志

| 镜像 | 启动参数 | 命令 | 板子的反应 |
|------|----------|------|------------|
| `XNU-151-FbDiag.img`（96 MiB，payload 来自 CI 40449214321） | 见本节开头 | `fastboot flash boot_b` + `fastboot reboot` | 约 20 s 后 COM7 出现，控制台可用；`IMEM?` = `0x0b39bda4` |
| 同上 | 同上 | `PSCI?` | `version 0x00010001`，两个 FEATURES 都是 0 |
| 同上 | 同上 | `FASTBOOT\n`（一次写入） | 旧值/新值/回读 MATCH；1.0 s 端口消失；19.8 s 回来；正常启动；无 fastboot、无 crash dump；`IMEM?` = `0x836999ac` |
| 同上 | 同上 | `REBOOT\n`（一次写入） | 1.0 s 端口消失；19.8 s 回来；正常启动；`IMEM?` = `0x532989ac` |
| 同上 | 同上 | `REBOOT\n`（收尾回归） | 1.0 s 端口消失；19.8 s 回来；正常启动；`IMEM?` = `0x616998ac` |

所有命令都是主机侧一次 `write()` 发出的完整一行（`FASTBOOT\n` 9 字节、
`REBOOT\n` 7 字节、`PSCI?\n` 6 字节、`IMEM?\n` 6 字节），没有逐字符输入。
整个过程手机没有落入 recovery / fastboot / EDL：每次复位后都回到 XNU 控制台。

## 二十、SYSTEM_RESET2 与 PSCI 特性探测的更正

本节更正第十九节的两处结论：`PSCI_FEATURES` 返回 `0` 的含义，以及 IMEM 证据
能证明的范围。代码是 `0107-kebab-psci-reset2.patch`（提交 `560b3aa`），
镜像 `XNU-0107.img` 由 CI run 40452530585 构建；内核 17,119,032 字节，
SHA-256 `b68bb4bbdf59ddcd893c4c6ce8f6919bb06cc1a9947a5eef4deaa590c1ee7997`。

### 1. 更正的 PSCI_FEATURES 语义

PSCI 规范对 `PSCI_FEATURES`（`0x8400000A`）的返回值规定如下：

| 返回值 | 含义 |
| --- | --- |
| `0xFFFFFFFF`（`PSCI_RET_NOT_SUPPORTED`） | 该 FID 不支持 |
| `0` | 支持，没有 feature flags |
| 其它非负值 | 支持，返回值是 feature flags |

Linux 的 `drivers/firmware/psci.c` 只判断 `!= PSCI_RET_NOT_SUPPORTED`：

```c
feature = psci_features(psci_fn_ids[i].fn);
if (feature == PSCI_RET_NOT_SUPPORTED)
	continue;
```

第十九节把 `0` 读成"not supported"，于是把 `SYSTEM_RESET2` 记成
`NOT_SUPPORTED`；同一节"这颗固件的 `PSCI_FEATURES` 是桩"的结论也来自同一个
错误：`SYSTEM_RESET` 返回 `0` 不是桩，而是"支持、无 flags"的正常回答，和它
实测能复位完全一致。

`0107` 因此改了实现：

* `kebab_psci_features()` 返回原始 `uint32_t`，不再转成 `int` 再由调用者猜；
* 判定统一为 `value != 0xFFFFFFFF`；
* `PSCI?` / `PSCINEG?` 打印原始值，不在内核里改写语义；
* `FASTBOOT2` 只在"负控通过 + `FEATURES(0xC4000012) != 0xFFFFFFFF`"时执行。

### 2. 负控与两个真实 FID 的原始读数

`PSCI?` 先调一个不可能存在的 FID `0x8400FFFF` 作负控，再读两个真实 FID。
负控的作用是证明探针不是"对任何输入都回答 0"的桩：如果负控返回 `0`，后续
任何"支持"的结论都不成立，`FASTBOOT2` 会拒绝执行。

**负控结果：`PSCI_FEATURES(0x8400FFFF) = 0xFFFFFFFF`，探针可用。**

| FID | 功能 | 原始返回 | 按更正后的规则 |
| --- | --- | --- | --- |
| `0x8400FFFF` | 负控（不存在的 FID） | `0xFFFFFFFF` | 必须是 `PSCI_RET_NOT_SUPPORTED` |
| `0x84000009` | `SYSTEM_RESET` | `0x00000000` | **支持**（无 flags） |
| `0xC4000012` | `SYSTEM_RESET2` | `0x00000000` | **支持**（无 flags） |

同一次启动的完整控制台记录（`PSCI?` 与 `PSCINEG?` 的读数一致）：

```text
KEBAB PSCI: version 0x00010001
KEBAB PSCI: FEATURES(0x8400ffff)=0xffffffff negative control OK
KEBAB PSCI: FEATURES(0x84000009)=0x00000000 SYSTEM_RESET supported
KEBAB PSCI: FEATURES(0xc4000012)=0x00000000 SYSTEM_RESET2 supported
```

这只说明 `SYSTEM_RESET2` 这个 FID 存在，不保证它接受 `reset_type = 0`
（架构 warm reset）；下一节的实测正好把这两件事分开。

### 3. FASTBOOT2 实验

`FASTBOOT2` 的执行顺序（`0107`）：

1. 打印当前 IMEM `restart_reason`（物理 `0x146BF000 + 0x65C`）；
2. 写 cookie `0x77665500`（`kebab_fastboot_cookie` 可覆盖）；
3. 回读并打印 MATCH/MISMATCH；
4. 执行 `dsb sy`；
5. 调用 `SYSTEM_RESET2(0xC4000012)`，`x1 = 0`（架构 warm reset），
   `x2 = 0`（cookie）；
6. 如果调用返回：打印完整 64 位 `x0`，恢复旧 IMEM 值，然后**停止**，
   不自动落到 `SYSTEM_RESET`。

门控条件：负控必须为 `0xFFFFFFFF`，且
`FEATURES(0xC4000012) != 0xFFFFFFFF`；否则只打印原因，不复位。

**实测**：命令发出后端口没有消失，`SYSTEM_RESET2` 返回
`0xFFFFFFFFFFFFFFFE`，板子仍停在同一个 XNU 启动里。

```text
KEBAB FASTBOOT: cookie 0x77665500 from kebab_fastboot_cookie
KEBAB FASTBOOT: IMEM restart_reason at 0x146bf000+0x65c is 0x012999ec; writing 0x77665500
KEBAB FASTBOOT: read back 0x77665500 (MATCH)
KEBAB REBOOT: PSCI SYSTEM_RESET2(reset_type=0, cookie=0)
KEBAB REBOOT: SYSTEM_RESET2 returned 0xfffffffffffffffe, the machine did not reset
KEBAB FASTBOOT: restored restart_reason to 0x012999ec
```

| 项 | 读数 |
| --- | --- |
| 写前 IMEM | `0x012999ec` |
| 写入 | `0x77665500`（来自 `kebab_fastboot_cookie`） |
| 回读 | `0x77665500` MATCH |
| PSCI 调用 | `SYSTEM_RESET2(0xC4000012, x1=0, x2=0)` |
| 调用返回 | `0xFFFFFFFFFFFFFFFE` = `PSCI_RET_INVALID_PARAMETERS`（`-2`） |
| 是否进 fastboot | 否（没有发生复位） |
| 是否正常启动 | 不适用（同一启动继续，没有重启） |
| crash dump | 无（没有出现 `VID_05C6` 设备） |
| 下次启动 IMEM | 不适用；同一启动内恢复后为 `0x012999ec` |
| 控制台是否回来 | 没有离开（0 s） |
| 10 s 后板子状态 | XNU 控制台，`/dev/cu.usbmodem00011` |

**解释**：`FEATURES(0xC4000012) = 0` 说明这个 FID 在固件里存在，但
`reset_type = 0` 被固件以 `PSCI_RET_INVALID_PARAMETERS` 拒绝。也就是说上一节
的 `NOT_SUPPORTED` 是错的，但"`SYSTEM_RESET2` 能进入 fastboot"同样不成立：
架构 warm reset 这条路在这颗固件上走不通。按本轮"一次只改一个变量"的约定，
没有改用 vendor 私有的 `reset_type`（那会再引入一个未经验证的变量），也没有
在返回后补一次 `SYSTEM_RESET`。

### 4. REBOOT 回归

在 `SYSTEM_RESET2` 返回、IMEM 恢复之后，收尾验证原来的 `REBOOT` 路径：

| 项 | 读数 |
| --- | --- |
| 命令 | `REBOOT\n` |
| 复位调用 | `PEHaltRestart(kPERestartCPU)` → PSCI `SYSTEM_RESET` |
| 端口消失 | 0.45 s |
| 控制台回来 | 20.2 s，可继续读写 |
| 是否进 fastboot | 否 |
| 是否正常启动 | 是 |
| crash dump | 无 |
| 下次启动 IMEM | `0x8929b17c` |

`REBOOT` 仍然工作，说明这次失败的 `SYSTEM_RESET2` 和 IMEM 恢复没有破坏平台
复位路径。

### 5. IMEM 证据能证明什么、不能证明什么

把本轮和上一轮的所有启动读数放在一起（`REBOOT` 不写 IMEM）：

| 启动 | 触发 | 下一次内核启动读到的 `0x65C` |
| --- | --- | --- |
| 1 | 刷机后第一次启动 | `0x0b39bda4` |
| 2 | `FASTBOOT`（写 cookie + `SYSTEM_RESET`） | `0x836999ac` |
| 3 | `REBOOT`（未写） | `0x532989ac` |
| 4 | `REBOOT`（未写） | `0x616998ac` |
| 5 | 本轮刷机后第一次启动 | `0x012999ec` |
| 6 | 本轮 `REBOOT` 后 | `0x8929b17c` |

**能证明的**：

1. `0x146BF65C` 的映射、写、回读都成立（`FASTBOOT` 回读 MATCH；本轮
   `FASTBOOT2` 也回读 MATCH）。
2. OS 写的 cookie 在**下一次内核启动后**读不到；六次启动六个不同的值。
3. `REBOOT` 根本不写这个单元，值也会变——每次启动都有比内核更早的环节
   （XBL / ABL / UEFI 之一或多个）在重写它，而且这发生在下一次内核读它之前。

**不能证明的**：

* 不能证明 ABL 在**做 fastboot 判断的那一刻**没有看到 cookie。下一次内核
  读到的值晚于 ABL 的判断；ABL 完全可能读到了、消费了或重写了它。
* 不能证明是复位销毁了 cookie，也不能证明不是复位销毁的。
* 不能区分下面两个（还可以有第三个）假设。

**仍然开放的假设**：

* **H1（复位破坏）**：`SYSTEM_RESET` 或更早的复位/启动环节在 ABL 读取之前
  清掉或覆盖了 IMEM，cookie 没有到达判断点。
* **H2（ABL 消费/重写）**：ABL 读到了（或消费了）cookie，但 vendor 路径还要求
  PMIC PON 的 `PON_RESTART_REASON_BOOTLOADER`；OS 只写了 IMEM，PMIC 侧没有
  这个 reason，所以 ABL 仍然正常启动，并在启动过程中重写 `0x65C`。
* 还可以有 **H3**：这条启动链根本不把 `0x65C` 当作 fastboot 的判断依据，
  只看 PMIC PON。只要没有读到 ABL 的代码或 PMIC 寄存器，H3 也不能排除。

本轮的 `FASTBOOT2` **没有关闭任何一个假设**：`SYSTEM_RESET2` 在复位发生前
就返回了 `-2`，所以没有产生"复位后 IMEM 是否保留"的新数据。它只证明了
架构 warm reset 这条复位路径不可用，不能用来判断 H1/H2。

因此，上一节"ABL 做判断时看到的不是 OS 写的值"应改成：
**"下一次内核启动时看到的不是 OS 写的值；ABL 判断时看到什么，现有数据不能
确定。"** 要关闭 H1/H2，需要 (a) PMIC PON 的实际读数/写入，或 (b) 在 ABL
读取 IMEM 的时刻观察它；两点都不是当前内核能做的。

### 6. PMIC PON 路线的实现规格（本轮不写驱动）

因为 `SYSTEM_RESET2` 没有进入 fastboot，按本轮约定**不开始写 SPMI 驱动**。
下面是补上这一段所需的精确规格；它是实现清单，不是已完成的代码。

#### 6.1 目标与顺序

vendor 的 fastboot 路径不是只写 IMEM：

1. `qpnp_pon_set_restart_reason(PON_RESTART_REASON_BOOTLOADER)` 写 PMIC PON；
2. `__raw_writel(0x77665500, restart_reason)` 写 IMEM；
3. 复位。

本内核只有第 2 步。补第 1 步必须严格按"先只读、再单次写"的顺序：

1. **只读 subtype 探测**：SID 0，PON 基址 `0x0800`，寄存器 `0x0805`；
   读到 `0x04`/`0x05` 才是 Gen2；
2. **单次 masked RMW**：仅当 subtype 为 Gen2，读 `0x088F`，写回
   `(old & 0x01) | 0x04`，回读校验；
3. 只有在第 1、2 步都成功且回读正确后，才允许 `SYSTEM_RESET`。

#### 6.2 当前缺失的 SPMI 基础设施

* XNU 树里没有 SPMI 控制器驱动：`grep -ri spmi` 只命中
  `EXTERNAL_HEADERS/acpi/Acpi_v1.h` 的一个无关词；`bsd/conf/files`、
  `iokit/conf/files*`、`configs/kebab_defconfig` 都没有 SPMI 项。
* 没有 `qcom,spmi-pmic-arb` 驱动：kona 的 arbiter 是
  `qcom,spmi@c440000`，`reg-names = "core","chnls","obsrvr","intr","cnfg"`，
  对应 `0xC440000` / `0xC600000` / `0xE600000` / `0xE700000` / `0xC40A000`，
  `qcom,ee = <0>`；还需要从 DT 读 `qcom,channel`（Linux 要求该属性）。
* 没有 APID 映射/ownership 处理、没有命令编码和超时轮询、没有 PMIC 子设备
  模型，也没有 `qpnp-power-on` 的等价物。
* KDK 里的 `AppleSPMI.kext` 是 Apple Silicon 的二进制，不参与这棵树的构建，
  也不匹配 kona 的 DT。
* `package/BootReset.c` 是 sheng（SM8550）的另一套地址（`0xC400000` 等），
  没有编进 kebab 内核，不能当作"已经有 SPMI"。
* 没有任何驱动占用 arbiter，也就没有 APID/EE 的 ownership 声明；反过来说，
  EE0 是否真的拥有 PON 的 APID，必须靠下面的 ownership 表读出来确认。

#### 6.3 只读 subtype 探测（第一步）

1. `ml_io_map()` 映射 core / chnls / obsrvr / cnfg 五段。
2. 读 `core + 0x0000`（`PMIC_ARB_VERSION`）。kona 预期是 v5
   （`0x50000000 <= ver < 0x70000000`）；不是 v5 就停下重新推导，不能套用
   v5 的偏移。
3. 读 `core + 0x0004`（`PMIC_ARB_FEATURES`，APID 数量低 11 位），
   上限 `PMIC_ARB_MAX_PERIPHS`。
4. 建 APID→PPID 表：v5 的映射表在 `core + 0x900 + 4*apid`，
   `ppid = (regval >> 8) & 0xFFF`，bit24 是 IRQ owner；
   ownership 在 `cnfg + 0x700 + 4*apid`，低 3 位是 write EE。
5. 目标外设：SID 0，PON 的 PPID 是 `(0 << 8) | (0x0800 >> 8) = 0x0008`。
   找到 `ppid == 0x0008` 且 `write_ee == 0` 的 APID；找不到就停，不猜 APID。
6. 事务寄存器：v5 的 RW 通道基址是 `chnls + 0x10000*apid`；
   命令 `+0x00`、状态 `+0x08`、写数据 `+0x10`、读数据 `+0x18`。
   命令格式（v5/v2）：
   `cmd = (opc << 27) | ((addr & 0xFF) << 4) | (len - 1)`；
   读 `opc = 0xF`，写 `opc = 0xE`，1 字节时 `len - 1 = 0`。
7. 写事务先把数据写到 `WDATA0`，再写 `CMD`；轮询 `STATUS` 的 bit0
   （DONE），超时按 1000 µs；bit1 = FAIL、bit2 = DENIED、bit3 = DROPPED，
   任一置位都要中止并打印原始 `STATUS`。读事务在 DONE 后从 `RDATA0`
   取低字节。
8. 只读 `addr = 0x0805`（PON base `0x0800` + subtype `0x05`）一次：
   * `0x04`（GEN2_PRIMARY）或 `0x05`（GEN2_SECONDARY）：Gen2，继续；
   * `0x01`/`0x02`/`0x03`：Gen1/1REG，掩码和移位不同，**停止**；
   * 其它值、超时、DENIED：**停止**，不写任何 PMIC 寄存器。

#### 6.4 单次 masked RMW（第二步，仅 Gen2）

1. 读 `addr = 0x088F`（`QPNP_PON_SOFT_RB_SPARE`）1 字节，得到 `old`。
2. 计算 `new = (old & 0x01) | (0x02 << 1)`，即 `new = (old & 0x01) | 0x04`；
   只改 bit7:1，bit0 原样保留。
3. 写 `addr = 0x088F`，值 `new`，长度 1。
4. 立即回读 `0x088F`，要求 `(readback & 0xFE) == 0x04` 且
   `(readback & 0x01) == (old & 0x01)`；不满足就报告失败，**不要再写第二次**。
5. 成功后才调用 `SYSTEM_RESET`（`0x84000009`），仍然走
   `PEHaltRestart(kPERestartCPU)`，不要用刚被证明不可用的
   `SYSTEM_RESET2(reset_type=0)`。

#### 6.5 接线、门控与验收

* 建议把 arbiter/PMIC 访问放在新的 `KebabSPMI.cpp`（或
  `KebabPlatformExpert.cpp` 的独立函数）里，只做轮询、不开中断；不要在 USB
  驱动里直接碰 PMIC。
* 用启动参数（例如 `kebabpon=1`）门控写路径；默认只做只读 subtype 探测，
  日志打印 version、APID、PPID、ownership、subtype 的原始值。
* 写路径只允许单次；失败后不清零、不重试、不换 reset type，避免把 PMIC
  带进未知状态。
* 验收标准：subtype 读到 `0x04`/`0x05`；`0x088F` 回读
  `(x & 0xFE) == 0x04`；随后 `SYSTEM_RESET` 后板子进入 fastboot（主机
  `fastboot devices` 能看到序列号），而不是正常启动或 EDL。
* 如果仍正常启动：记录下一次启动的 IMEM、PON `0x088F` 回读值和 USB 状态，
  再判断是 ABL 不认这个 reason，还是复位路径又清掉了它。

## 二十一、SPMI Arbiter 只读探测

本节实现并验证第二十节 6.3 的只读部分。补丁 `0108` 增加 `bsd/dev/kebab_spmi.c`
与两个控制台命令；`0110` 把 `SPMI?` 的输出压回 16 KiB 控制台环；`0109`/`0111`/
`0112` 是同一轮真机中触屏驱动的三个独立修复（见第 7 节）。**本节没有写任何
PMIC 寄存器**：`SPMI?`/`PON?` 只读版本、映射表、ownership 表和 observer 数据，
SPMI-4 的 masked RMW 仍未实现。

### 1. 代码与门控

| 项 | 值 |
| --- | --- |
| 内核补丁 | `0108-kebab-spmi-readonly.patch`（只读探测）、`0110-kebab-spmi-report-fit.patch`（输出长度） |
| 系列 | 92 个补丁，92/92 按文件名顺序应用到 `1b191cb`（`verify-all.sh` PASS） |
| 文件 | `bsd/dev/kebab_spmi.c`（新）、`bsd/conf/files`、`bsd/dev/kebab_usb.c` |
| CI | run `40455214113`，`kernel_sha256 d9061e8a2cc7f37f3063089fbeae3d6841a2e4e67ef7fc93f692d181d51dc3d1`，17,103,000 字节 |
| 映射 | core `0x0C440000+0x1100`、obsrvr `0x0E600000+0x100000`、cnfg `0x0C40A000+0x26000` |
| 未映射 | chnls `0x0C600000+0x2000000`、intr `0x0E700000+0xA0000` |
| EE / channel | 0 / 0（`kona.dtsi` 的 `qcom,ee` / `qcom,channel`） |
| 命令 | `SPMI?\n`（SPMI-1+2）、`PON?\n`（SPMI-3），必须一次 bulk OUT 到达 |

`chnls`/`intr` 不映射的原因见第 5 节：内核 `io_map` 的 submap 只有 8 MiB
（`osfmk/arm/io_map.c`，`IO_MAP_SIZE`），而 chnls 资源是 32 MiB；只读路径
不使用读写通道。命令匹配沿用 `ud_bulk_out_done()` 的单次传输规则。

### 2. SPMI-1：arbiter 版本

```text
KEBAB SPMI: === SPMI-1: arbiter version ===
KEBAB SPMI: core 0x0c440000+0x1100 mapped; core+0x0000 = 0x50010000
KEBAB SPMI: version family v5
KEBAB SPMI: SPMI-1 PASS
```

| 项 | 原始值 | 结论 |
| --- | --- | --- |
| `core + 0x0000` | `0x50010000` | v5（`>= 0x50000000`） |
| 版本族 | v5 | 与 vendor `spmi-pmic-arb.c` 的 v5 分支一致 |

**SPMI-1：PASS（真实读）。**

`0111` 触屏内核上复测（`SPMI?` 输出 915 字节，不再截断）：

```text
KEBAB SPMI: core 0x0c440000+0x1100 mapped; core+0x0000 = 0x50010000
KEBAB SPMI: version family v5
KEBAB SPMI: SPMI-1 PASS
```

### 3. SPMI-2：PPID `0x008` 的 APID 映射

PPID 公式是 `(sid << 8) | (addr >> 8)`；PM8150 PON 在 SID 0、peripheral
`0x08`，所以目标 PPID 是 `0x008`。v5 的映射表在 `core + 0x900 + 4*apid`，
ownership 表在 `cnfg + 0x700 + 4*apid`，扫描上限由 core 资源大小
`0x1100` 决定（512 项，APID `0x000..0x1ff`）。

负控：PPID `0xf00`（SID `0x0f`，板上没有这个 PMIC）必须解析不到 APID。
实测 `0 APID(s) (expected 0)`，说明扫描确实读的是映射表，而不是一个
"对任何输入都给答案"的桩。

目标 PPID 找到三个 APID：

```text
KEBAB SPMI: PPID 0x008 APID 0x001 map[core+0x904]=0x00000800 own[cnfg+0x704]=0x00000001 -> write_ee 0x1 irq_owner 0 irq_ee 0xff
KEBAB SPMI: PPID 0x008 APID 0x00d map[core+0x934]=0x01000800 own[cnfg+0x734]=0x00000000 -> write_ee 0x0 irq_owner 1 irq_ee 0x0 (owned by this EE)
KEBAB SPMI: PPID 0x008 APID 0x075 map[core+0xad4]=0x00000800 own[cnfg+0x8d4]=0x00000004 -> write_ee 0x4 irq_owner 0 irq_ee 0xff
```

| APID | mapping 寄存器 | PPID | ownership 寄存器 | `write_ee` | `irq_owner` | `irq_ee` |
| --- | --- | --- | --- | --- | --- | --- |
| `0x001` | `0x00000800` | `0x008` | `0x00000001` | 1 | 0 | `0xff` |
| `0x00d` | `0x01000800` | `0x008` | `0x00000000` | **0** | 1 | **0** |
| `0x075` | `0x00000800` | `0x008` | `0x00000004` | 4 | 0 | `0xff` |

**SPMI-2：PASS（真实读）。** 关键点是 APID `0x00d` 的 `write_ee = 0`，
正好是内核运行的 EE 0；`pmic_arb_offset_v5()` 的写权限检查会接受它。
另外两个 APID 分别属于 EE 1 和 EE 4，不能用来写。

`0111` 内核上的干净输出（915 字节，扫描计数完整）：

```text
KEBAB SPMI: scan APID 0x000..0x1ff: core+0x900 + 4*apid, 512 entries bounded by core size 0x1100
KEBAB SPMI: negative control PPID 0xf00 -> 0 APID(s) (expected 0)
KEBAB SPMI: scanned 512 entries: 352 non-zero mapping(s), 3 map PPID 0x008
...（三行 APID 明细同上）...
KEBAB SPMI: SPMI-2 PASS: 3 APID(s) map PPID 0x008
```

> 说明：`SPMI?` 最初还打印整张非零映射表，输出超过 16 KiB 控制台环，
> 真机捕获在 16383 字节处被截断，丢掉了 `SPMI-2 PASS` 行和匹配明细；
> 上表来自同一次扫描的 `PON?` 输出。`0110` 删掉整表打印，只保留扫描上下界、
> 负控、三个计数（扫描项数、非零映射数、匹配数）、匹配明细和门结果，
> 使一次报告不可能超过环容量。

### 4. SPMI-3：通过 observer 读 PON 寄存器

v5 的 observer 地址算术与读写通道不同：

```text
observer = obsrvr + 0x10000 * ee + 0x80 * apid
         = 0x0E600000 + 0x10000*0 + 0x80*0x00d
         = 0x0E600680
```

命令格式 `(opc << 27) | ((addr & 0xff) << 4) | (len - 1)`，读 `opc = 0xF`。
两次独立读的结果：

```text
KEBAB SPMI: REG 0x0805 read 1: APID 0x00d obsrvr+0x680 cmd 0x78000050 status 0x00000001 -> 0x04
KEBAB SPMI: REG 0x0805 read 2: APID 0x00d obsrvr+0x680 cmd 0x78000050 status 0x00000001 -> 0x04
KEBAB SPMI: PON subtype 0x0805 reads 0x04 / 0x04 -> STABLE
KEBAB SPMI: REG 0x088f read 1: APID 0x00d obsrvr+0x680 cmd 0x780008f0 status 0x00000001 -> 0x00
KEBAB SPMI: REG 0x088f read 2: APID 0x00d obsrvr+0x680 cmd 0x780008f0 status 0x00000001 -> 0x00
KEBAB SPMI: SOFT_RB_SPARE 0x088f reads 0x00 / 0x00 -> STABLE
KEBAB SPMI: PON generation: Gen2 (subtype 0x04); the Gen2 SOFT_RB_SPARE layout would apply
KEBAB SPMI: SPMI-3 PASS
```

| 寄存器 | 读 1 | 读 2 | 稳定性 | 含义 |
| --- | --- | --- | --- | --- |
| PON subtype `0x0805` | `0x04` | `0x04` | STABLE | Gen2（Gen2 定义值 `0x04`/`0x05`） |
| `SOFT_RB_SPARE` `0x088f` | `0x00` | `0x00` | STABLE | 写前旧值；masked 写得到 `0x04`（bit0 保持 0，bit7:1 写 `0x02`） |

两次读的 status 都是 `0x00000001`（DONE，无 FAILURE/DENIED/DROPPED）。

**SPMI-3：PASS（真实读）。**

### 5. 对 SPMI-4 的意义与新约束

1. **写权限已经确认**：PPID `0x008` 存在 `write_ee = 0` 的 APID `0x00d`，
   所以 vendor 驱动在 `pmic_arb_offset_v5()` 里的 `-EPERM` 检查不会阻止
   一次 EE 0 的写。这消除了上一节 6.2 中"EE0 是否真的拥有 PON APID"的
   不确定性。
2. **Gen2 已直接确认**：subtype `0x04`，`SOFT_RB_SPARE` 旧值 `0x00`。
   按第二十节 6.4 的规格，`new = (old & 0x01) | (0x02 << 1) = 0x04`。
3. **写通道不能整块映射**：chnls 是 32 MiB，`io_map` submap 只有 8 MiB，
   整块 `ml_io_map` 会像触屏驱动的 16 MiB TLMM 一样在 `kmem_alloc` 里
   panic。SPMI-4 必须只映射目标 APID 的一个 4 KiB 窗口：
   `chnls + 0x10000 * apid = 0x0C600000 + 0x10000 * 0x00d = 0x0C6D0000`，
   大小 `0x1000`。寄存器在窗口内：`CMD +0x00`、`STATUS +0x08`、
   `WDATA0 +0x10`、`RDATA0 +0x18`。
4. **写命令格式**：`opc = 0xE`（`PMIC_ARB_OP_WRITE`），
   `cmd = (0xE << 27) | ((0x8f & 0xff) << 4) | 0 = 0x700008F0`；把新值写到
   `WDATA0`，再写 `CMD`，轮询 `STATUS` 的 DONE，然后通过 observer 回读
   `0x088F` 校验 `(readback & 0xFE) == 0x04` 且 `(readback & 0x01) == 0`。
5. **SPMI-4 必须是独立补丁、独立刷机、启动参数默认关闭**。写之前重新确认
   subtype 与两次读稳定；只写一次，不回读失败后重试，不换 reset 类型；
   写成功后仍走 `PEHaltRestart(kPERestartCPU)`（PSCI `SYSTEM_RESET`），
   不用已经被证明不接受 `reset_type=0` 的 `SYSTEM_RESET2`。

### 6. 仍然未知

* ABL 是否真的读取 PMIC PON 的 `PON_RESTART_REASON_BOOTLOADER` 作为
  fastboot 判断的一部分。本节只证明内核能读到 PON；不能证明 ABL 会看它。
* 复位路径（XBL/ABL/UEFI）是否在 ABL 读取前清掉或重写 PON reason。
  `SOFT_RB_SPARE` 现在是 `0x00`，但这是本次启动的读数，不是复位后的读数。
* 即使 ownership 允许，arbiter 硬件是否接受这次写（status 是否 DENIED）
  只能在 SPMI-4 真机上回答。
* `SOFT_RB_SPARE` 其它位的语义没有文档；本节只按第二十节的 mask `0xFE`
  准备，不改 bit0。

### 7. 组合镜像中的触屏首启与三个独立修复

第一次组合镜像使用 `kebab_touch=1 kebab_touch_dump=1`（内核 `0108`），
内核在 `kebab_touch_start()` 里 panic：

```text
panic(cpu 0 caller 0xfffffe0006ab3810): kmem_alloc(..., 16777216, 0x100021): failed with 3 @vm_kern.c:178
```

符号化后是 `kebab_touch_start -> io_map -> kmem_alloc`：TLMM 节点在
`kona-pinctrl.dtsi` 里是 `0x0F000000+0x1000000`（16 MiB），但 `ml_io_map()`
从 8 MiB 的 `io_submap` 分配并带 `KMA_NOFAIL`，超限请求在 `kmem_alloc`
里直接 panic。`0109` 把 `KT_TLMM_SIZE` 改成 `0x40000` 并在注释里记录原因；
这个尺寸本身仍是对的，错的是下面的基址。

`0111` 增加一条可选的 bit-banged GPIO I2C 传输（`kebab_touch_bb=1`），
用同一 TCM 命令层绕开 QUPv3/SE13；QUPv3 仍是默认。bit-bang 路径在改 mux
之前先打印 GPIO 36/37/38/39 的 `TLMM_CTL`/`TLMM_IO` 原始值，然后把
SDA/SCL 设为 mux 0（plain GPIO）、pull-up、输出关闭，开漏时序约 5 µs
半周期。

**第一次 bit-bang 真机启动（`0111`）没有 panic，但也没有碰到任何引脚。**
四个引脚的读数全部是零，写 mux 也没有粘住：

```text
KEBAB TOUCH: bit-bang pin 36 ctl 0x00000000 (mux 0 pull 0 drive 0 oe 0) io 0x00000000
KEBAB TOUCH: bit-bang pin 37 ctl 0x00000000 (mux 0 pull 0 drive 0 oe 0) io 0x00000000
KEBAB TOUCH: bit-bang pin 38 ctl 0x00000000 (mux 0 pull 0 drive 0 oe 0) io 0x00000000
KEBAB TOUCH: bit-bang pin 39 ctl 0x00000000 (mux 0 pull 0 drive 0 oe 0) io 0x00000000
KEBAB TOUCH: bit-bang pin 36 ctl 0x00000000 -> 0x00000000 (mux 0 GPIO, pull-up, released)
KEBAB TOUCH: bit-bang start failed at phase 0 (SDA 0 SCL 0)
```

对照 vendor `drivers/pinctrl/qcom/pinctrl-kona.c` 后找到根因：**Kona 的
TLMM 是分 tile 的**。该文件定义 `NORTH 0x900000`、`SOUTH 0x500000`、
`WEST 0x100000`、`EAST 0`，每个引脚地址是 `tile + 0x1000 * pin`；
GPIO 36..39 在 SOUTH tile，所以真实地址是
`0x0F000000 + 0x500000 + 0x1000*pin`，而驱动原来算的是
`0x0F000000 + 0x1000*pin`。后者落在 tile 之间的空洞里：读恒为 0，
写被忽略。`0112` 改为映射 `KT_TLMM_PA + 0x500000`（`0x40000`，仍覆盖
SOUTH tile 的 pin 0..63），每引脚偏移相对 tile 基址。

这同时解释了 QUPv3 路径：它的 `kt_pins_config()` 和复位脉冲一直在写
同一批错误地址，所以**两条传输在 0112 之前都没有真正配置过引脚**。
`0112` 的 SOUTH 窗口是 `0x0F500000..0x0F53FFFF`，四个引脚的绝对寄存器
地址因此是：GPIO36 CTL `0x0F524000` / IO `0x0F524004`，GPIO37
`0x0F525000`，reset 38 `0x0F526000`，IRQ 39 `0x0F527000`。CI run
`40459994589` 构建成功（kernel sha256
`391d365fdc5854f80a980214bc3afe6ffb53ba3f811744fec9cdfae4fd4f4c42`，
17,103,048 字节）。截至本节写入时，`0112` 的真机复测（引脚真实读数、
bit-bang TCM identify）尚未进行；因此本节只记录到"已定位并修复基址"，
不把触屏写成已打通。

### 8. 工具缺陷记录

* 控制台环固定 16 KiB，单条命令的输出在命令返回前全部入环；超过就会
  静默丢尾。`SPMI?` 的整表打印正好撞上这一点（捕获 16383 字节），`0110`
  改为有界报告。
* `ml_io_map()` 的 `KMA_NOFAIL` 会把"映射太大"变成 panic，而不是返回 0；
  `bsd/dev/kebab_*.c` 的尺寸常量因此是硬约束。已审计的调用：UFS `0x30000`、
  USB `0xD000+0x100+0x1000+0x10000`、touch SE `0x4000`+wrap `0x2000`、
  SPMI `0x1100+0x100000+0x26000`；TLMM 的 16 MiB 是唯一的超限请求。
* TLMM 的地址不是平坦的：`0109` 只修了尺寸，`0112` 才修 tile 基址。两次
  都是"地址/尺寸假设"错误，与 PMIC、触屏芯片本身无关；先读后写、把原始
  值打进日志的做法在这里直接给出了证据。

## 二十二、PMIC PON 重启原因与 fastboot 进入

### 1. 结论

**这块板子的 ABL 认的是 PMIC PON 里的重启原因，不是 IMEM 里的
`restart_reason`。** 两者一起写入、再用已证明可靠的 PSCI `SYSTEM_RESET`
复位，控制台命令 `FASTBOOT` 可以在约 2～3.3 秒内把板子带进 bootloader 的
fastboot 模式（`fastboot devices` 返回 `c073a488`，`getvar product` 为
`kona`）。这是本轮之前一直缺的那一块：IMEM 的 `0x77665500` 单独写入从未
生效，因为每次复位后 IMEM 都是新的随机值，ABL 根本读不到它；而 PON 的
`SOFT_RB_SPARE` 能跨复位存活，ABL 消费后才清零。

最终形态只有一条受支持的入口：`kebab_pon_write=1` 启动参数打开写门控，
控制台输入 `FASTBOOT`，它先写 PON 原因、再写 IMEM cookie，最后走 PSCI
复位。门控关闭时 `FASTBOOT` 直接拒绝并提示改用 `REBOOT`，不写任何
PMIC 寄存器，也不写 IMEM。`REBOOT` 仍然只是普通 PSCI 复位。

本节补的是第二十一节 5.4 计划里的 SPMI-4 写路径；实现与当时的草案有两处
不同，以代码为准：单字节写用 `PMIC_ARB_OP_EXT_WRITEL`（opcode 0、byte
count 0，vendor `pmic_arb_fmt_cmd_v2()` 的编码），不是
`PMIC_ARB_OP_WRITE`（0xE）；命令字因此是 `0x000008F0`，不是
`0x700008F0`。

### 2. 源码语义（不是猜的）

来自 vendor 树 `drivers/input/misc/qpnp-power-on.c`、
`drivers/power/reset/msm-poweroff.c`、`include/dt-bindings/input/qcom,qpnp-power-on.h`
和 `kona.dtsi`：

* `pm8150.dtsi` 的 PON 节点带 `qcom,store-hard-reset-reason`，所以
  `qpnp_pon_set_restart_reason()` 不会像没有该属性时那样直接返回 0。
* Gen2（本板 PON subtype `0x04`）写 `SOFT_RB_SPARE`（PON 基址 `0x800`，
  寄存器 `0x88F`）：mask `GENMASK(7,1)` = `0xFE`，值为 `reason << 1`。
  `PON_RESTART_REASON_BOOTLOADER` 是 `0x02`，因此字段值是 `0x04`，bit 0
  不在 mask 内、必须保持不变。
* `msm_restart_prepare("bootloader")` 的顺序是：
  1. `qpnp_pon_check_hard_reset_stored()` 为真（本板为真），且命令不是
     `edl`/dload，所以 `need_warm_reset = false`；
  2. `qpnp_pon_system_pwr_off(PON_POWER_OFF_HARD_RESET)`；
  3. `qpnp_pon_set_restart_reason(PON_RESTART_REASON_BOOTLOADER)`；
  4. `__raw_writel(0x77665500, restart_reason)`（IMEM `+0x65C`）；
  5. `deassert_ps_hold()`，其兜底路径是向 TCSR `0x0C264000` 写 0。
* `qpnp_pon_reset_config()`（Gen2）配置的是
  `PS_HOLD_RST_CTL2`（`0x85B`）的 bit 7 `QPNP_PON_RESET_EN` 和
  `PS_HOLD_RST_CTL`（`0x85A`）低四位的掉电类型；
  `PON_POWER_OFF_TYPE_WARM_RESET = 0x01`、
  `PON_POWER_OFF_TYPE_HARD_RESET = 0x07`。
* PON 寄存器和 PON subtype 同在 PPID `0x008`；本板映射到 APID `0x00d`，
  `write_ee = 0`，与本地 EE 0 一致。

本轮没有使用 PMIC 掉电路径。PSCI 复位已经足够把板子带进 fastboot，而
`deassert_ps_hold()` 一旦触发就没有撤销手段，因此最终补丁只保留原因写入，
不把未验证的掉电复位路径放进内核。

### 3. SPMI 写路径

只读部分见第二十一节。写路径补的是 v5 的 read/write channel：

* 只映射一个 4 KiB 切片：`chnls + 0x10000*apid`，PON 的 APID `0x00d`
  对应 `0x0C6D0000`。整个 `chnls` 是 32 MiB，装不进内核 8 MiB 的
  `io_map` 子映射。
* 单字节写用 `PMIC_ARB_OP_EXT_WRITEL`（opcode 0）编码成
  `((addr & 0xff) << 4) | 0`，数据放 `WDATA0`，然后轮询 `STATUS` 的
  DONE/FAILURE/DENIED/DROPPED，超时 1000 µs。
* 每次写之前重新读 APID mapping 和 cnfg ownership，要求该 APID 仍映射
  PPID `0x008` 且 `write_ee == 0`；否则拒绝。这和
  `pmic_arb_offset_v5()` 对 read/write channel 的检查一致。
* 写后通过 observer 读回（最多 20 次、每次间隔 1 ms），同时校验目标字段
  和 mask 之外的 bit 0。
* 通道切片在 `kebab_spmi_init()` 里、与 IMEM 一起在正常上下文映射；命令
  可能从 panic 循环执行，那里不能调用 `ml_io_map()`。

真机第一次读-改-写（`PON-RB`，当时还是独立诊断命令）：

```text
KEBAB SPMI: PON write APID 0x00d, write_ee 0x0 (this EE); chnls slice 0x0c6d0000
KEBAB SPMI: REG 0x088f read 1: APID 0x00d obsrvr+0x680 cmd 0x780008f0 status 0x00000001 -> 0x00
KEBAB PON: SOFT_RB_SPARE 0x088f old 0x00; mask 0xfe; reason 0x02 << 1 = 0x04; new 0x04
KEBAB SPMI: write REG 0x088f: APID 0x00d chnls-slice+0xd0000 cmd 0x000008f0 wdata 0x04
KEBAB SPMI: write REG 0x088f status 0x00000001 DONE
KEBAB SPMI: REG 0x088f read 2: APID 0x00d obsrvr+0x680 cmd 0x780008f0 status 0x00000001 -> 0x04
KEBAB PON: read back 0x04, expected 0x04
KEBAB PON: PASS: SOFT_RB_SPARE 0x00 -> 0x04, field 0x04, bit 0 unchanged 0x0
```

旧值 `0x00`，写入 `0x04`，读回 `0x04`，mask 外的 bit 0 保持 `0`。
最终补丁的输出压缩成摘要行，原始寄存器行由 `kebab_pon_verbose=1` 打开。

### 4. 真机结果

所有尝试都在同一块 kebab（SM8250）上，镜像经 `fastboot flash boot_b`
写入，固件固定从 `boot_b` 读载荷。门控关闭的镜像用于先证明"不写 PMIC 也
能正常启动、`REBOOT` 仍工作"；门控打开的镜像用于写原因并复位。

| # | 镜像 / 门控 | PON 原因（写前 → 写后，读回） | IMEM 写入 | 复位方式 | 45 s 内出现 fastboot | 下次启动看到的 PON / IMEM |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | `XNU-pon-off.img`，`kebab_pon_write` 未设置 | 未写；`PON-RB` 输出 `kebab_pon_write is off; refusing to write SOFT_RB_SPARE`，`PON?` 读到 `0x00` | 未写 | `REBOOT`（PSCI） | — | 启动正常；`IMEM?` = `0x0939992c`（随机） |
| 1 | `XNU-pon-on.img`，`kebab_pon_write=1` | `0x00` → `0x04`，读回 `0x04`，bit 0 不变 | `0x77665500`，读回 MATCH | `FASTBOOT`（PSCI SYSTEM_RESET） | **是，约 2 s**；`c073a488`，`product: kona` | `PON?` = `0x00`；`IMEM?` = `0x836991fc`（随机） |
| 2 | 同上，从普通启动再来一次 | `0x00` → `0x04`，读回 `0x04` | `0x77665500`，读回 MATCH | `FASTBOOT`（PSCI） | **是，3.33 s** | `PON?` = `0x00`；`IMEM?` = `0x016999fc`（随机） |
| 3 | 同上，第三次 | `0x00` → `0x04`，读回 `0x04` | `0x77665500`，读回 MATCH | `FASTBOOT`（PSCI） | **是，3.33 s** | — |

关键观察：

* 表 0–3 使用的是中间构建的镜像（95 个补丁文件，含当时的诊断命令 `PON-RB`、
  `PON-RST`、`FASTBOOT-PMIC`）。最终补丁删除了这些诊断命令，只保留
  `FASTBOOT`，并把 PON 写入和 IMEM 写入一起放到门控后面；`FASTBOOT`
  的写入与复位行为与表中完全一致，差异只在日志行数。
* 每次进入 fastboot 后再 `fastboot reboot` 回内核，PON 原因都已经是
  `0x00`，IMEM 都是新的随机值。说明 ABL 在消费 fastboot 请求时把 PON
  原因清掉了；IMEM 的 cookie 在复位/启动过程中没有被保留下来。既然 ABL
  自己会清，内核启动时不需要、也没有任何代码去清 `SOFT_RB_SPARE`。
* 因此不存在"`fastboot reboot` 又回到 fastboot"的循环：原因被消费后，
  下一次普通启动就是正常内核启动。实测两次从 fastboot 回内核分别用了
  23.6 s 和 21.5 s（含固件与内核启动）。
* 三条成功路径的差别只有启动次数，PON 旧值都是 `0x00`，写入和校验都
  一样；fastboot 出现时间在 2～3.33 s。
* 归因：IMEM 的 `0x77665500` 在每次复位后都不存在（本轮之前六次启动也
  都是随机值），所以此前"写了 IMEM 却仍然普通启动"不是写失败，而是
  ABL 根本不使用它。真正起作用的是 PON `SOFT_RB_SPARE` 的
  `PON_RESTART_REASON_BOOTLOADER`。

### 5. 使用方式

启动参数（payload 的 TOML `args`，实测使用的那一行）：

```text
-v -enable_kprintf_spam startup_debug=1 serial=3 debug=0x14e cpus=1 rd=ufsroot0 ufsroot=userdata ufsrw=1 rootrw=1 kebabusb=1 kebabpanic=1 kebab_pon_write=1 kebab_fastboot_cookie=0x77665500 keepsyms=1
```

其中与本节直接相关的是 `kebabusb=1`（USB 控制台）、`kebab_pon_write=1`
（PMIC 写门控）和 `kebab_fastboot_cookie=0x77665500`（IMEM 值；默认就是
这个值，可以不写）。最小集是 `kebabusb=1 kebab_pon_write=1`。

可选：`kebab_pon_verbose=1` 打开 SPMI 原始寄存器读写行，默认关闭。

控制台命令：

```text
FASTBOOT     写 PON 原因（0x00 -> 0x04，读回校验）和 IMEM
             0x77665500，然后 PSCI SYSTEM_RESET；约 2～3.3 秒进入 fastboot
REBOOT       普通 PSCI 复位，不写任何原因
PON?         只读检查：PON subtype、SOFT_RB_SPARE、APID 归属
```

`FASTBOOT` 在 `kebab_pon_write` 未设置时输出
`KEBAB FASTBOOT: disabled because kebab_pon_write=0; use REBOOT for a plain restart`
并直接返回。门控打开时，正常输出是：原因写入行、原因校验行、IMEM 写入行、
IMEM 回读行、复位行。

### 6. 补丁范围与构建

| 项 | 值 |
| --- | --- |
| 内核补丁 | `0114-kebab-pon-reason-write.patch` |
| 改动文件 | `bsd/dev/kebab_spmi.c`（写路径）、`bsd/dev/kebab_usb.c`（`FASTBOOT` 门控与 PON 调用） |
| 不需要改动 | `bsd/conf/files` 和 `bsd/kern/bsd_init.c`：`0108` 已把 `kebab_spmi.c` 加进 `files`，并已从 `kebab_usb_start()` 调用 `kebab_spmi_init()`；IMEM 也是在同一函数里映射的 |
| 系列 | `verify-all.sh`：`94/94` 个补丁文件按文件名顺序应用到 `1b191cb`（`xnu-8796.141.3`）；编号补丁 `0001`–`0114` 共 93 个 |
| 构建 | 本地 macOS 14.6.1 VM（Xcode 15.4、KDK 13.5）：`kernel.development.kebab` sha256 `54d4a0addcb12b447681796d22b4e7d7959dc3a39350e2acc834a26822f811d1`，17,136,232 字节；0 error。`kebab_spmi.c`/`kebab_usb.c` 共 60 条 warning，其中 4 条是 `0114` 新增的 prototype/格式 warning，均不阻断构建（这 4 条已由 `0115` 清除，见第二十三章） |

### 7. 仍未闭合的部分

* PMIC 的 `PS_HOLD` 掉电复位路径（`0x85A/0x85B` + `0x0C264000`）按
  vendor 语义实现过，但没有在真机上执行；PSCI 路径已经达成目标，而这条
  路径一旦触发就没有撤销手段，所以补丁不携带它。若以后 PSCI 路径失效，
  再按 `qpnp_pon_system_pwr_off()` + `deassert_ps_hold()` 的顺序单独验证。
* ABL 具体在什么时刻清掉 `SOFT_RB_SPARE` 还没有直接观测：内核只能在
  fastboot 之后的下一次启动里看到 `0x00`。要看到"ABL 读取后立即清零"的
  瞬间，需要 ABL/XBL 侧日志或 SPMI 抓包。
* IMEM 的 `restart_reason` 是否被复位清掉、还是被 ABL 改写，仍无法区分；
  但已经不影响结论——PON 原因才是 ABL 使用的信号。

## 二十三、0114 的构建警告清理（0115）

### 1. 四条新增警告的来源

0114 引入的警告里有四条此前不存在，全部落在它自己的新代码上：

| 类型 | 位置 | 说明 |
| --- | --- | --- |
| `-Wmissing-prototypes` | `kebab_spmi_pon_writes_enabled()` 的定义 | 函数非 static；`kebab_usb.c` 里有 extern 声明，但定义所在文件没有原型 |
| `-Wmissing-prototypes` | `kebab_spmi_pon_reason_write()` 的定义 | 同上 |
| `-Wformat` | `kebab_spmi_init()` 里 chnls 切片映射成功的那条打印 | `%08x` 打印 `vm_offset_t`（`unsigned long`） |
| `-Wformat` | 同一函数里映射失败的那条打印 | 同上 |

### 2. 0115 的改法

- 在 `kebab_spmi.c` 顶部、`extern void kebab_usb_log_flush(void);` 之后补上两个原型，
  与 `kebab_usb.c` 里的声明一致，定义于是有声明可对照；
- 两处 `%08x` 改成 `%08lx`，并把 `slice` 显式转换成 `unsigned long`；
- 行为不变：映射的仍是同一个 4 KiB 切片，门控仍是 `kebab_pon_write`，
  两个值打印出的数字也相同。

### 3. 实测：A/B 编译

用构建日志里 `kebab_spmi.c` 自己那条 clang 命令，在 `bsd` 的对象目录下
（`-include meta_features.h` 需要这个 cwd），对同一文件跑两遍：一遍 0114 的代码，
一遍应用 0115 之后的代码。

| 版本 | 警告总数 | `kebab_spmi.c` 内 | 其中属于本次四条的 |
| --- | --- | --- | --- |
| 0114 代码 | 17 | 14 | 4（`181`、`524` 两条缺原型；`637`、`640` 两条格式） |
| 0115 代码 | 13 | 10 | 0 |

剩下的 13 条与本补丁无关：3 条来自 `kcdata.h`、`vm_param.h`、`KEBAB.h` 三个头文件，
10 条在文件内（`kebab_spmi_init`、`kebab_spmi_report`、`kebab_spmi_pon_report`
的缺原型，6 条 `-Wshadow`，1 条 `-Wdeclaration-after-statement`），0108 到 0113
就一直存在。

### 4. 实测：整树构建

本地 macOS 14.6.1 VM（Xcode 15.4、KDK 13.5），在 0114 的树上应用 0115 后增量构建：

| 项 | 值 |
| --- | --- |
| 结果 | 0 error，`XNU Build Done` |
| 内核 | `kernel.development.kebab` 17,119,848 字节（0114 为 17,136,232 字节） |
| sha256 | `6d5d436b171ca2176976b5b4d106aa738cf5a59a577e97ec21019a82c81737e5` |
| 该文件的警告 | 重新编译过，`kebab_spmi.c` 内 10 条，没有本次四条 |

### 5. 验证边界

- 实测：`verify-all.sh` 95/95 个补丁文件按文件名顺序应用到 `1b191cb`，整脚本
  6 passed、0 failed、4 skipped；A/B 编译的警告清单；整树构建成功。
- 实测：这个 0115 内核随后刷进了 `boot_b` 做触摸实验，见第二十四章；0115 本身只改
  函数原型与格式说明符，不碰寄存器访问、映射或门控，没有需要单独验证的新行为。
- 同时改了 `README.md` 的补丁计数：`93 个` 改为 `95 个文件`。

## 二十四、0115 内核的触摸实测：第一次越过引脚配置

### 1. 为什么不需要“合并 0113”

触屏改动分布在 `0101`、`0108`、`0109`、`0111`、`0112`、`0113` 六个补丁里，按文件名
顺序应用；`0114` 和 `0115` 都没有再动 `bsd/dev/kebab_touch.c`。所以 0115 的树里就是
0113 的追踪版，不存在需要合并的分支。要做的只是用 0115 的内核重新打一个载荷：

```text
python pack-payload.py ci239-payload.img kernel-0115.macho payload-0115-bb.img unixnu-touch-bb.toml
python pack-boot.py signed.img payload-0115-bb.img XNU-0115-bb.img
```

启动参数与 `XNU-0113-bb.img` 里的那一条逐字相同（`debug=0x14a`、`kebab_touch=1`、
`kebab_touch_dump=1`、`kebab_touch_bb=1`）。内核 sha256 以 `6d5d436b171ca2` 开头，
载荷镜像 100,663,296 字节，sha256 以 `9660101b96e66ea0` 开头。

### 2. 实测结果

`fastboot flash boot_b` 之后从串口发 `REBOOT`，抓完整一次启动（`touch-0115.log`，
3,228 字节）：

| 阶段 | 日志 | 含义 |
| --- | --- | --- |
| 读引脚 | 四条 `bit-bang pin N ctl 0x00000001 ... io 0x00000000` | mux 0、下拉、OE=0，输入读 0 |
| 写引脚 | `writing pin 36 ctl 0x00000001 -> 0x00000003`，读回一致 | mux 0 加下拉改上拉，写入生效 |
| 复位 | `reset ctl 0x00000200 io 0x00000003` | 拉高后 OUT=1、IN=1，输入跟随输出 |
| TCM | `bb write 3 bytes (first 0x02)` 之后 `SCL did not rise within 1000 us (SDA 0)` | 起始条件就失败 |
| 收尾 | `the controller did not answer TCM` | 0x02、0x10、0x20 三条命令都没写出去 |

0113 补丁说明里预期的“挂起前最后一行是 reading pin 36”没有出现：驱动没有挂起，
四颗引脚都配置完，复位脉冲也打了出去，停在第一条 I2C 事务上。

### 3. 这说明什么

复位引脚拉高后输入读回 1，说明 TLMM 基址（SOUTH tile `0x0f500000`）、每引脚
`0x1000` 的步进、CTL 与 IO 的偏移都对，`0x3`（mux 0 加上拉）也能写进去读回来。
0112 的 tile 修复是有效的。

SDA 和 SCL 在释放后仍读 0。两条线同时为低更像总线被外部拉低：控制器没有供电时，
其 I/O 上的 ESD 二极管会把线钳在低电平；也可能是控制器自己把总线按住。
这两种都没有直接测量。

### 4. 下一步

- 查设备树里 `sec-s6sy761@48` 节点的 `vdd`/`vio` 供电与 `reset-gpio` 极性，确认这条
  路径上是否有内核必须打开的稳压器；
- 若要一次读数就区分“没供电”和“被拉低”，先只做电源使能加一次 `TLMM_IO` 读取，
  不碰 I2C。

### 5. 0115 上的 FASTBOOT 复验

把 0115 内核按开门的参数重打成 `XNU-0115-fb.img`（`kebab_pon_write=1`、
`kebab_pon_verbose=1`，不带触摸参数），刷入 `boot_b` 后从串口发 `FASTBOOT`：

```text
KEBAB SPMI: write REG 0x088f: APID 0x00d chnls-slice+0xd0000 cmd 0x000008f0 wdata 0x04
KEBAB PON: restart reason verified: read back 0x04, field 0x04, bit 0 unchanged 0x0
KEBAB FASTBOOT: IMEM read back 0x77665500 (MATCH)
KEBAB REBOOT: PEHaltRestart(kPERestartCPU)
```

- 发出命令到 `fastboot devices` 出现设备：3.35 秒；
- `fastboot reboot` 到串口重新出现：19.9 秒；
- 门控关掉时同一条命令只回 `disabled because kebab_pon_write=0`，不写任何寄存器，
  这一点在换镜像之前先测过。

之后把触摸参数并进同一个镜像（`XNU-0115-touch-fb.img`，`kebab_pon_write=1` 加
`kebab_touch=1 kebab_touch_dump=1 kebab_touch_bb=1`），刷入后复测：`FASTBOOT` 到
fastboot 3.37 秒，`fastboot reboot` 回串口 20.0 秒；触摸追踪输出与第 2 节逐行相同。
`boot_b` 现在是这个镜像。

### 6. 验证边界

- 实测：刷机、串口抓到整段启动、上表每一行日志。
- 推断：总线被拉低的原因是供电或控制器状态；没有量电压，也没有抓 I2C 波形。

## 二十五、触摸改为只读探针与控制台门控（0116）

### 1. 上一轮真正停在哪

QUPv3 那一轮的日志停在 `reset pulse done`。按 `kebab_touch_start()` 的顺序，之后
下一步是 `kt_se_init()`，它要写 `SE_GENI_M_IRQ_CLEAR` 等 GENI 寄存器。所以失败点
在“第一次 GENI 写”，不是 I²C 事务；之前把它说成“卡在第一次 I²C 传输”不准确。

### 2. 0116 的改动

- 启动路径只映射 MMIO，不再自动碰硬件；要跑旧路径需显式 `kebab_touch_boot=1`，
  要只读探针需 `kebab_touch_probe=1`；
- 控制台新增 `TOUCH?`（只读探针）与 `TOUCHINIT`（引脚、复位、`kt_se_init`、识别）；
- 新增只读 GCC 探针：SE13 的三路时钟（`GCC_QUPV3_WRAP1_S5_CLK`、
  `GCC_QUPV3_WRAP_1_M_AHB_CLK`、`GCC_QUPV3_WRAP_1_S_AHB_CLK`），只映射两个
  4 KiB 页，不映射 2 MiB 的 GCC 块；
- 新增 GENI/TLMM 只读转储与 `kt_se_init()` 每组写之前的 trace。

### 3. 实测：TOUCH?

```text
KEBAB TOUCH: GCC 0x52008 = 0x063c83c0: wrap1_s5 0 m_ahb 1 s_ahb 1
KEBAB TOUCH: GCC halt m_ahb 0x00000000 s_ahb 0x80000002 wrap1_s5 0x80004220
KEBAB TOUCH: probe wrapper QUP_HW_VER 0x20050000
KEBAB TOUCH: probe GENI_FW_REVISION_RO 0x00000303 protocol 3
KEBAB TOUCH: probe GENI_STATUS 0x00000000 M_IRQ_STATUS 0x00000000 S_IRQ_STATUS 0x00000000
KEBAB TOUCH: probe DMA_MODE_EN 0x00000000 CGC_CTRL 0x0000007f
KEBAB TOUCH: probe SER_M_CLK_CFG 0x00000011 SCL_COUNTERS 0x00000000
KEBAB TOUCH: probe TX_FIFO_STATUS 0x00000000 RX_FIFO_STATUS 0x00000000
KEBAB TOUCH: probe pin 36 ctl 0x00000001 io 0x00000000
KEBAB TOUCH: probe pin 37 ctl 0x00000001 io 0x00000000
KEBAB TOUCH: probe pin 38 ctl 0x00000001 io 0x00000000
KEBAB TOUCH: probe pin 39 ctl 0x00000001 io 0x00000000
```

### 4. 结论

SE13 的两路 AHB 时钟是开的，**核心时钟 `WRAP1_S5` 没开**（`0x52008` 的 bit 27 为 0）。
GENI 寄存器能通过 AHB 读，但 GENI 状态机没有核心时钟就不会推进；这与“读完
`FW_REVISION_RO` 之后第一次写 GENI 就失去可观测性”相吻合。

`GENI_STATUS`、主从中断状态、`DMA_MODE_EN` 全为 0，`SCL_COUNTERS` 也是 0，
说明这个 SE 从来没有被配置过 I²C 时序，之前能读到的协议号来自固件寄存器本身。

### 5. 验证边界

- 实测：构建 0 error；`TOUCH?` 的完整输出；启动路径不再碰硬件。
- 推断：核心时钟缺失是 GENI 写失败的原因；尚未置位验证，也没有量过时钟。
- 下一步（0117）：先只读 S5 的 RCG（`0x18600` 一带），再单独置位 `0x52008`
  bit 27，回读 halt 寄存器；确认时钟真的跑起来之后，才用 `TOUCHINIT` 去写 GENI。

## 二十六、SE13 时钟与第一次真正的 GENI 命令（0117）

### 1. 0117 做了什么

- 新增控制台命令 `TOUCHCLK`：只读转储 S5 的 RCG 块（`0x18600` 起八个字），然后
  只置位 `0x52008` 的 bit 27，回读使能位与 halt 寄存器；
- `TOUCHINIT` 保持不变，仍是引脚、复位、`kt_se_init`、识别，但它跑在时钟打开之后。

### 2. 实测：打开 S5 时钟

```text
KEBAB TOUCH:   RCG+0x00 = 0x80000000
KEBAB TOUCH:   RCG+0x04 = 0x00100000
KEBAB TOUCH:   RCG+0x14 = 0x00000021
KEBAB TOUCH: TOUCHCLK: 0x52008 0x063c83c0 -> 0x0e3c83c0
KEBAB TOUCH: TOUCHCLK: 0x52008 now 0x0e3c83c0 (s5 1), halt 0x00004220
```

置位前 halt 是 `0x80004220`，置位后变成 `0x00004220`：bit 31 清掉，说明这一路
分支时钟真的起来了。这一步没有触发任何复位或总线访问，控制台全程正常。

### 3. 实测：时钟打开后的 TOUCHINIT

```text
KEBAB TOUCH: reset pulse (mode 1), ctl 0x00000200 io 0x00000003
KEBAB TOUCH: se_init: clear IRQs
KEBAB TOUCH: se_init: force default, output ctrl
KEBAB TOUCH: se_init: packing + byte granularity
KEBAB TOUCH: se_init: clock select + SCL counters
KEBAB TOUCH: TCM command 0x02, 0 payload byte(s)
KEBAB TOUCH: write watermark timeout, status 0x00000000
KEBAB TOUCH: command 0x02 was not written
```

上一轮在这一步会把 USB 控制台一起带走，这一轮四组 GENI 写全部走完，控制台还在。
所以上一轮的失联就是**SE 核心时钟没开**：AHB 能读寄存器，GENI 状态机不推进，
写命令后总线挂住。

之后 `TOUCH?` 读到：

```text
GENI_STATUS 0x00000041 M_IRQ_STATUS 0x00000000
SER_M_CLK_CFG 0x00000071 SCL_COUNTERS 0x00a02c1a
pin 36 ctl 0x00000004  pin 37 ctl 0x00000004
```

`GENI_STATUS` 的 bit 0 是 master 命令仍在执行：命令发出去了，但总线上的事务永远
不完成，也没有 NACK 中断。SCL 计数器已经按 100 kHz 写入，引脚也复用到 qup13。

### 4. 这说明什么

- 时钟这一层已经解决：SE13 可以安全地配置，不再需要担心写 GENI 会带走控制台；
- 现在卡在总线层：命令 active、无中断、无 NACK，最可能是从机不响应（没供电或
  总线被拉低），而不是 GENI 配置错误；
- 之前 bit-bang 观察到 SDA/SCL 释放后仍为 0，与“从机侧没电”一致。

### 5. 芯片身份出现矛盾（需要在真机上定）

- 项目早先记录：设备 DTB 里是 `sec-s6sy761@48`，`chip-name = "SY771"`（Samsung）；
- LineageOS 的 `arch/arm64/boot/dts/vendor/oplus/kebab/kona-mtp.dtsi` 里，
  `qupv3_se13_i2c` 下是 `synaptics@4B`，`compatible = "synaptics-s3908"`，
  `reg = <0x4B>`，供电 `vdd_2v8-supply = <&pm8150_l13>`、
  `vcc_1v8-supply = <&pm8150a_l1>`，reset 仍是 GPIO 38、中断仍是 39。

两者地址和驱动都不同，必须先确认手里这台机器到底是哪一颗，再谈协议。确认方法：
给触摸供电后，在 0x48 和 0x4B 各做一次只读地址探测（ACK/NACK 就能区分），
或者从设备的原厂 DTB 里直接读节点。

### 6. 验证边界

- 实测：时钟置位与 halt 变化、`TOUCHINIT` 全程不丢失控制台、`TOUCH?` 的寄存器读数；
- 推断：命令不完成是因为从机不响应；芯片型号与地址尚未在本机确认；
- 没有做：供电使能、I2C 实际读写、协议层。

## 二十七、触摸芯片定为 S3908，GENI FIFO 握手修复（0118、0119）

### 1. 身份结论：S3908，地址 0x4B

从本机保留的 kebab 厂商镜像里取出 DTB（`Mu-kebab-debug-6c675501.img`，
FDT 在偏移 `0x10d191`，长 912,906 字节），用 dtc 反编译后，
`i2c@a94000` 下有两个互斥的触摸节点：

| 节点 | 地址 | chip-name | project-name | panel 列表 |
| --- | --- | --- | --- | --- |
| `sec-s6sy761@48` | `0x48` | `SY771` | `19821` | 0x6b9、0x6ba、0x6bb、0x699 |
| `synaptics-s3908@4B` | `0x4B` | `S3908` | `19805` | `0x698`、`0x699`、`0x69a` |

panel phandle 解析：

| phandle | 面板 |
| --- | --- |
| `0x698` | BOE |
| `0x699` | Samsung DD305 |
| `0x69a` | Samsung AMB655X |
| `0x6b9` | Samsung 通用 |
| `0x6ba` | Samsung ANA6705 |
| `0x6bb` | Samsung ANA6706 |

本机面板是 Samsung AMB655X，只出现在 S3908 的列表里；同一份 DTB 的
bootargs 里还有 `androidboot.project_name=19805`。所以这台机器的触摸芯片是
**Synaptics S3908，I²C 地址 0x4B**，此前“S6SY761 / 0x48”的记录是错的。

供电两个节点一致：`vdd_2v8 = pm8150_l13`（3,008,000 µV）、
`vcc_1v8 = pm8150a_l1`（1,800,000 µV），reset 都是 GPIO 38、中断都是 GPIO 39。

### 2. 0118：GENI FIFO 握手与中断位

| 项 | 0117 之前 | Qualcomm Linux | 0118 之后 |
| --- | --- | --- | --- |
| TX watermark | 从不在发命令后置位 | 命令后写 1 | 命令后写 1 |
| NACK 位 | bit 9 | `M_GP_IRQ_1_EN` = bit 10 | bit 10 |
| 超时后的命令 | 只打印，保持 active | cancel 后 abort | 新增 `kt_se_abort()` |
| 只读探针 | 无 watermark/IRQ_EN | — | 增加两项 |

`kt_se_abort()` 写 `M_CMD_ABORT_EN` 到 `SE_GENI_M_CMD_CTRL_REG`，有界轮询
abort/done/failure 状态，然后清中断与 TX watermark，与
`geni_cancel_m_cmd()`/`geni_abort_m_cmd()` 的顺序一致。

### 3. 0119：地址与命名

`KT_I2C_ADDR` 改为 `0x4b`，驱动标题、启动横幅、两处注释一并改成 S3908；
TCM 命令族保留（厂商对 S3908 用的就是 Synaptics 自己的 TCM 风格驱动）。

### 4. 验证边界

- 实测：DTB 反编译结果、panel phandle 映射、两份 DTB 供电一致；
- 编译：整树构建 0 error，`kebab_touch.c` 只剩两条既有警告
  （`-Wformat-nonliteral`、`-Wdeclaration-after-statement`）；
- 未做：这一版没有刷机，也没有发送新的 I²C 事务；`TOUCHINIT` 与
  `TOUCHCLK` 的下一次实机验证要等镜像刷入。

## 二十九、Abort 控制位修正与 Gate B–D（0120）

### 1. 0120 修了什么

0118 的 `kt_se_abort()` 把 `M_CMD_ABORT`（IRQ 状态位 5）写进了
`SE_GENI_M_CMD_CTRL_REG`，而控制寄存器的位定义是 cancel = bit 2、abort = bit 1
（`geni-se.h`）。写 0x20 是空操作，超时恢复其实从未生效。

0120 把两类位分开命名（`M_GENI_CMD_CANCEL_REQ`/`M_GENI_CMD_ABORT_REQ` 对
`M_CMD_CANCEL`/`M_CMD_ABORT`），并把恢复流程改成：

1. 读 `GENI_STATUS`，bit 0 不置位就直接清 watermark/中断返回成功；
2. 仍 active 时写 cancel，有界等待 cancel/done 或 active 清零；
3. 仍 active 才写 abort，继续有界等待；
4. 清 watermark 与中断，再读一次 `GENI_STATUS`；
5. 只有 active 清零才返回成功，否则置 `kt.se_dead`，本次运行不再发任何 I²C。

`kt_i2c_error()` 也改成先保留原始状态再决定是否恢复；写/读入口在 `se_dead` 时
直接拒绝。

新增两条控制台命令：`TOUCHSE`（引脚、复位、`se_init`，要求 S5 时钟已开）与
`TOUCHPING`（向 0x4B 发一个字节的 FIFO 写事务，走修好的 watermark 路径）。

### 2. 实测（Gate A–D）

- Gate A：刷入后串口 20.0 秒回来，启动不自动初始化触摸；
- Gate B：`TOUCH?` 显示 `GENI_STATUS 0`、`M_IRQ_EN 0x33c0007e`，SE 空闲；
- Gate C：`TOUCHCLK` 把 `0x52008` 从 `0x063c83c0` 改成 `0x0e3c83c0`，halt 的
  bit 31 清零；
- Gate D：`TOUCHSE` 四组 `se_init` 全部完成（此后 `M_IRQ_EN 0x7fc0007f`，
  FIFO 位已使能），`TOUCHPING` 一个字节写 0x4B：

```text
KEBAB TOUCH: ping 0x4b: one byte, FIFO path
KEBAB TOUCH: write did not complete, status 0x00000000
KEBAB TOUCH: recover (write completion timeout): GENI_STATUS 0x00000041 M_IRQ_STATUS 0x00000000
KEBAB TOUCH: recovered: GENI_STATUS 0x00000000, last status 0x00000021
```

`0x21` = `M_CMD_DONE | M_CMD_ABORT`，说明 cancel/abort 这条恢复路径这次真的
生效了，SE 回到空闲。

### 3. 结论

- watermark 置位与恢复路径都已验证可用；
- 事务仍然停在 active、没有 watermark、没有 NACK，说明 I²C 根本没有开始；同一次
  会话里 `TOUCH?` 读到的引脚 36/37 `io = 0`（释放状态仍为低），与“触摸供电没开、
  外部上拉没有电源”一致；
- 因此下一步不是继续改 GENI，而是给触摸供电：DTB 里两个节点共用
  `pm8150_l13`（3,008,000 µV）与 `pm8150a_l1`（1,800,000 µV）。

### 4. 验证边界

- 实测：上述四段日志、恢复路径、`M_IRQ_EN` 前后值；
- 推断：总线被拉低来自供电；没有量电压，也没有执行任何 PMIC 写。

## 三十、供电只读复核与 RPMh 审核（P1/P2）

### 1. P1 实测：SPMI 与 PON 只读

`SPMI?`：arbiter v5；扫描 512 个 APID，352 个非零映射，其中 3 个映射 PPID
`0x008`（APID `0x001`/`0x00d`/`0x075`），只有 `0x00d` 的 `write_ee = 0`（本 EE）。

`PON?`：PON subtype `0x0805` 两次读 `0x04`（Gen2），`SOFT_RB_SPARE 0x088f` 两次读
`0x00`。两条只读通路都正常，用来做后续供电实验前的基线。

### 2. P2 静态：两路触摸电源归 RPMh 管

| 触摸供电 | RPMh 资源 | 目标电压 | DTS 节点 |
| --- | --- | --- | --- |
| `pm8150_l13` | `ldoa13` | 3,008,000 µV | `rpmh-regulator-ldoa13` |
| `pm8150a_l1` | `ldoc1` | 1,800,000 µV | `rpmh-regulator-ldoc1` |

`kona-regulators.dtsi` 里这两路都是 `compatible = "qcom,rpmh-vrm-regulator"`，
不是 SPMI 寄存器节点。RPMh VRM 的资源内偏移是 VOLTAGE `0x0`、ENABLE `0x4`、
MODE `0x8`，地址本身来自 SMEM 里的 command DB，不是 PMIC 的 SPMI 地址。

RPMh 的通道是 `/soc/rsc@18200000`（APPS RSC，`drv-id = 2`）的 TCS。也就是说：

- 用 PON 那套 SPMI 写路径直接改 LDO 是错的；
- 正确做法要么走 RPMh 命令，要么先只读确认状态。

### 3. 对上一轮结论的更正

上一轮把 Gate D 描述成“没有 watermark”，不准确。日志里的
`write did not complete` 是**第二段等待**（FIFO 写完之后等 CMD_DONE），说明
watermark 阶段已经通过、数据已经进了 FIFO；之后命令一直 active、没有 NACK、
没有完成。所以更准确的判断是：FIFO 握手成立，事务卡在总线完成阶段，从机是否
上电仍未证实。

### 4. P3 的前提（尚未执行）

读 RPMh 状态不是纯 MMIO 读：需要从 SMEM 解析 command DB 得到资源地址，再通过
TCS 发 READ 命令并等完成，涉及通道所有权与共享状态。开始之前必须先确定哪些
寄存器读有副作用、驱动的 TCS 占用规则，以及两路 LDO 是否还有别的消费者。

### 5. 0121：门控加固

- 新增独立的 `se_initialized`：只有 `se_init` 成功才置位，`TOUCHPING` 以它为
  前置条件，不再用“SE 被映射过”当条件；
- `TOUCHSE` 不再把 `se_dead` 清零：恢复失败的 SE 只能靠重启清除；
- `TOUCH?` 增加一行 `initialized/dead` 状态。

### 6. 验证边界

- 实测：`SPMI?`、`PON?` 的完整输出；0121 的编译结果见提交说明；
- 未做：任何 PMIC 或 RPMh 写；0121 未刷机。

## 三十一、PWR? 只读电源探针：两路 LDO 都是 Disabled（0122）

### 1. 实现

新增控制台命令 `PWR?`（`bsd/dev/kebab_spmi.c`），全程只读：

- 按 PPID 查 APID 映射，列出命中的 APID 与 `write_ee`；
- 先读 `+0x04` type、`+0x05` subtype，各读两次要求一致，空设备（0x00/0xff）即停；
- 通过后才读 `+0x46` enable、`+0x40` range、`+0x41` set，同样两次一致；
- 另外做一次只读映射表survey：按 SID 统计映射数量，并检查 `0x4000`/`0x4c00`
  两个 LDO 页在 SID 0–7 上有没有映射；
- 不写任何 PMIC 寄存器，observer 命令只写 arbiter 自己的 observer 通道。

### 2. 第一次结果：候选 SID 全落空

按最初假设（SID 0 / SID 4）：两个 PPID（`0x04c`、`0x440`）都没有 APID 映射，
连 type 都读不到。

### 3. 映射表 survey 纠正了 SID

```text
KEBAB SPMI: PWR: mappings per SID: 0:66 1:51 2:67 3:5 4:43 5:53 6:5 7:0
KEBAB SPMI: PWR: mapping present for SID 1 base 0x4000 (PPID 0x140)
KEBAB SPMI: PWR: mapping present for SID 1 base 0x4c00 (PPID 0x14c)
KEBAB SPMI: PWR: mapping present for SID 2 base 0x4000 (PPID 0x240)
KEBAB SPMI: PWR: mapping present for SID 5 base 0x4000 (PPID 0x540)
```

LDO 页挂在 SID 1 和 SID 5 的 USID 上，而不是 0 和 4；SID 2 的 `0x4000` 是
PM8150B 自己的 LDO1。把目标改成 SID 1 / SID 5 后读取成功。

### 4. 实测：两路都是 Disabled

```text
PWR: pm8150_l13: SID 1 base 0x4c00 PPID 0x14c
PWR: pm8150_l13: APID 0x0c9 write_ee 0x4 irq_owner 1
PWR: pm8150_l13: type 0x04 subtype 0x7b
PWR: pm8150_l13: enable 0x00 (bit7 0), range 0xc0, set 0x0b

PWR: pm8150a_l1: SID 5 base 0x4000 PPID 0x540
PWR: pm8150a_l1: APID 0x11b write_ee 0x4 irq_owner 1
PWR: pm8150a_l1: type 0x04 subtype 0x71
PWR: pm8150a_l1: enable 0x00 (bit7 0), range 0x08, set 0x07
```

两路 `enable` 都是 `0x00`，bit7 为 0：**两路 LDO 当前都是关闭状态**。type `0x04`
是非空外设，subtype `0x7b`/`0x71` 分属不同 LDO 家族（PM8150 与 PM8150L）。

电压没有解码：`range`/`set` 只是原始值，且 enable 为 0 时输出本来就不成立。

### 5. 两个重要附带结论

- 两个 APID 的 `write_ee` 都是 4：这两路 peripheral 在 SPMI 上归 EE4（RPMh 侧）
  所有，EE0 的写事务会被 arbiter 拒绝；所以即使将来要写，也不该走 SPMI；
- 读取能成功是因为 observer 通道只读、不需要所有权。这也说明此前“总线被拉低”
  的猜测与“两路 LDO 关闭”一致：控制器确实没有供电。

### 6. 验证边界

- 实测：`PWR?` 的完整输出（两次读取一致）、启动与 COM7 正常；
- 未做：任何 PMIC 写、任何 RPMh 命令、任何 I²C 事务；
- 未证实：LDO 使能位为 0 只说明控制状态；输出端电压没有测量，也没有 ADC 路径。

## 三十二、RPMh 只读发现：Command DB 与 drv-2 TCS（0123）

### 1. 静态核对

`kona.dtsi` 与实际一致：

| 项 | 值 |
| --- | --- |
| Command DB | `0x80860000`，`0x20000`（128 KiB），`compatible = "qcom,cmd-db"`，`no-map` |
| APPS RSC | `0x18200000`，窗口 `drv-0/1/2` 各 `0x10000`，`drv-id = 2` |
| drv-2 窗口 | `0x18220000` |
| TCS | `qcom,tcs-offset = <0xd00>`，ACTIVE 2、SLEEP 3、WAKE 3、CONTROL 1 |

布局取自厂商 `cmd-db.c`（`cmd_db_header` 144 字节、`rsc_hdr` 16 字节、
`entry_header` 24 字节、魔数 `db 30 03 0c`）与 `rpmh-rsc.c`
（TCS 步进 672、命令步进 20，`CMD_ENABLE 0x1c`、`MSGID 0x30`、`ADDR 0x34`、
`DATA 0x38`、`STATUS 0x3c`）。

### 2. `RPMH?` 只读探针

新增命令 `RPMH?`：映射 cmd-db 与 drv-2，先校验魔数，再按 slave 槽遍历条目，
只打印 `ldoa13`/`ldoc1` 两条；随后打印 drv-2 公共寄存器与两个 active TCS 的
`enable/msgid/addr/data/status`。全程只读，每个偏移都先做窗口边界检查。

### 3. 实测

```text
KEBAB RPMH: cmd-db 0x80860000+0x20000: version 0x00000001 magic 0x0c0330db (match)
KEBAB RPMH: slave 3: 22 entries at +0x0
KEBAB RPMH: slave 3: 10 entries at +0x350
KEBAB RPMH: slave 5: 37 entries at +0x458
KEBAB RPMH: slave 4: 67 entries at +0x8f8
KEBAB RPMH: ldoa13: addr 0x00040c00 len 4 offset 104
KEBAB RPMH: ldoc1: addr 0x00042f00 len 4 offset 192
KEBAB RPMH: drv-2 0x18220000, tcs_base +0xd00
KEBAB RPMH: common irq_en 0x00000001 irq_status 0x00000000 control 0x01010000 status 0x00000001 cmd_enable 0x00000003
KEBAB RPMH: tcs 0: enable 0x00000003 msgid 0x00010108 addr 0x00030000 data 0x00000005 status 0x00010101
KEBAB RPMH: tcs 1: enable 0x00000000 msgid 0x00000000 addr 0x00000000 data 0x00000000 status 0x00000000
```

### 4. 资源地址怎么读

`addr` 的 bit 19:16 是加速器类型（`ACCL_TYPE`），`4` = VREG；低 16 位是 VRM
资源基址。所以：

| 资源 | cmd-db addr（完整） | ENABLE（`+0x4`） |
| --- | --- | --- |
| `ldoa13` | `0x00040c00` | `0x00040c04` |
| `ldoc1` | `0x00042f00` | `0x00042f04` |

注意这里的地址必须保留完整 32 位：bit 19:16 是加速器类型（4 = VREG），
它属于 TCS 命令地址的一部分，不能截断成 `0x0c04`/`0x2f04`。早先版本写成截断值，
已在 0124 这一轮更正；VRM 资源内偏移仍是 `VOLTAGE 0x0 / ENABLE 0x4 / MODE 0x8`。

### 5. 第一次跑的一个边界 bug

第一次把条目数上限写成 64，而 VREG slave 有 67 条，正好被当成“不可信”跳过，
所以没打印出两条地址。改成 1024，并保留逐条窗口检查（`offset + 24 > 0x20000`
即停止），第二次才得到上面的结果。

### 6. TCS 状态

`common irq_en = 1`、`status = 1`、`cmd_enable = 3`（两个 active TCS 均可用）；
TCS0 里残留一条已完成请求（`msgid 0x00010108`、`addr 0x00030000`、`data 5`，
type 3 = CLK），TCS1 全零空闲。这条残留不是本内核发的，读它不影响任何状态。

### 7. 验证边界

- 实测：`RPMH?` 的完整输出、cmd-db 魔数与版本、两个资源地址、drv-2 与 TCS 状态；
- 未做：任何 RSC/TCS 写、任何 VRM 请求、任何 PMIC 写；
- R4（请求编码与离线测试）尚未实现，等这一步的日志确认后再做。

## 三十三、RPMh VRM 编码与主机测试（R4，未进内核）

### 1. 地址纠正

上一节把 ENABLE 地址写成 `0x0c04`/`0x2f04`，那是截断后的值。cmd-db 返回的 32 位
地址整体参与命令，bit 19:16 的加速器类型（4 = VREG）不能被去掉；`qcom-rpmh-regulator.c`
也是把完整 `vreg->addr` 加上寄存器偏移。正确值是 `0x00040c04` 与 `0x00042f04`，
上一节的表已改。

### 2. 模块

`package/KebabRpmhVrm.{h,c}`：纯逻辑，不访问 MMIO、不依赖内核。

- 固定资源表只有 `ldoa13`（`0x00040c00`，3008 mV）与 `ldoc1`（`0x00042f00`，1800 mV）；
- 编码只产生 `{msgid 0x00010108, addr, data}`；ENABLE 数据固定 1，电压只接受表里的值；
- `kebab_rpmh_address_known()` 只认两个完整地址，截断值一律拒绝；
- cmd-db 快照解析：魔数 `0x0c0330db`、版本 1、8 个 slave 槽、每条 24 字节、
  越界即错、同名重复即错；
- TCS 状态机：`FREE → BUSY →(submit)→ INFLIGHT → DONE/TIMEOUT/ERROR`，
  timeout 与 error 都是终态，`release` 只把 `DONE` 放回 `FREE`；
- 唯一可能的硬件出口是调用者传入的 `kebab_rpmh_sink` 函数指针，测试里传的是记录器。

### 3. 主机测试

`package/KebabRpmhVrmHostTests.c`，42 项断言，全部 PASS：

```text
$ TMPDIR=$PWD/tmp gcc -std=c11 -Wall -Wextra -Werror -o t \
      KebabRpmhVrm.c KebabRpmhVrmHostTests.c && ./t
...
42 checks, 0 failure(s)
```

覆盖面：完整地址保留、截断地址/伪造资源拒绝、只允许两条资源、电压只接受 DTS 值、
cmd-db 缺失/坏魔数/重复/越界、TCS 占用与重复提交、超时与错误后禁止重试、
负路径上 sink 一次都不被调用。

### 4. 第一次跑暴露的两个问题

- 模块：`submit` 成功后没有进入 in-flight 状态，同一通道可以连续提交两次。
  新增 `KEBAB_TCS_INFLIGHT`，`complete/timeout/fail` 只从 in-flight 迁移；
- 测试：越界用例的长度选错（200 字节仍装得下两条），改成 180 并查 `ldoc1`
  才真正触发越界检查。

### 5. 验证边界

- 只有主机测试：没有编译进内核、没有刷机、没有 MMIO 访问；
- 真实 TCS 的所有权判断、完成回收策略和“是否存在可到达的硬件写入口”仍是 R5 的
  审核内容；本轮没有新增内核侧入口。

## 三十四、R4.1 加固与 R5-A 只读审计（0125）

### 1. R4.1：审核提出的两处修复

- 编码器不再接受调用者构造的资源结构：`kebab_rpmh_encode_enable/voltage` 改为按
  名字查表，地址与电压都由表决定，伪造结构没有入口；
- 新增 `kebab_rpmh_cmd_allowed()`：通道只接受编码器可能产出的四条命令
  （两个资源 × ENABLE/VOLTAGE），伪造地址、错误数据、错误 msgid 一律
  `KEBAB_RPMH_ERR_NOT_ALLOWED`；
- `kebab_tcs_submit()` 在调用 sink 之前进入 `INFLIGHT`，sink 返回失败则进入
  `ERROR`；`complete/timeout/fail` 只从 `INFLIGHT` 迁移，sink 内同步完成不会再被
  覆盖回 in-flight；
- cmd-db 解析要求 name→addr 与表一致，且条目必须位于 VREG slave（id 4）；
  交叉映射（`ldoa13` 条目带 `ldoc1` 的合法地址）与错误 slave 都拒绝。

### 2. R4.1 主机测试

57 项断言全部 PASS，新增覆盖：伪造 3300 mV 命令、enable data=0、错误 msgid、
交叉映射、错误 slave、重复完成、sink 内同步完成、sink 内失败、拒绝型 sink。

### 3. R5-A：drv-2 全槽只读审计

探针扩展到 common 的 `cmd_wait` 与 TCS 0–8 的
`enable/msgid/addr/data/status/resp`：

```text
common irq_en 0x00000001 irq_status 0x00000000 cmd_wait 0x00000000 control 0x01010000 status 0x00000001 cmd_enable 0x00000003
tcs 0: enable 0x00000003 msgid 0x00010108 addr 0x00030000 data 0x00000005 status 0x00010101 resp 0x00000000
tcs 1..8: enable 0x00000000 msgid 0x00000000 addr 0x00000000 data 0x00000000 status 0x00000000 resp 0x00000000
```

- TCS1–8 完全为零，睡眠/唤醒/控制槽没有持有者痕迹；
- TCS0 还留着一条 CLK 类请求（`addr 0x00030000`、`data 5`、`status 0x010101`），
  common `control = 0x01010000` 含 AMC 位。这条残留不是本内核发的，但在没有
  明确回收策略前，TCS0 不能当作可用通道；
- 因此第一次真实发送若要做，只能考虑 TCS1，并且仍需要所有权与完成回收的判断，
  这两项是 R5-C 的内容。

### 4. 验证边界

- 实测：上述只读输出、57 项主机测试；
- 未做：任何 TCS/VRM/PMIC 写、任何 RPMh 命令；没有新增内核侧写入口。

## 三十五、R4.2 返回值语义与 R5-A2 逐槽审计（0126）

### 1. R4.2：submit 的返回值不再等于“成功”

`kebab_tcs_submit()` 现在按 sink 返回后的最终状态返回：

| 最终状态 | 返回值 |
| --- | --- |
| `DONE`（sink 内同步完成） | `KEBAB_RPMH_OK` |
| `ERROR` / `TIMEOUT` | `KEBAB_RPMH_ERR_SINK_FAILED` |
| `INFLIGHT`（仍在等硬件） | `KEBAB_RPMH_PENDING` |

调用者不能再把 PENDING 当成“电源已就绪”。主机测试 57 项全部通过，其中同步失败的
用例现在断言返回错误而不是成功。

### 2. R5-A2：逐槽只读审计

探针把 drv 级寄存器（`+0x00/0x04/0x08/0x0C`）与每个 TCS 的
`wait/control/status/enable` 分开打印，并遍历每条命令槽（步进 20）。

```text
drv irq_en 0x00020000 irq_status 0x04010100 irq_clear 0x04800414 prnt_chld 0x800c8104
tcs 0: wait 0x00000000 control 0x01010000 status 0x00000001 enable 0x00000003
tcs 0 slot 0: msgid 0x00010108 addr 0x00030000 data 0x5 status 0x00010101 resp 0
tcs 0 slot 1: msgid 0x00010108 addr 0x00030010 data 0x5 status 0x00010101 resp 0
tcs 1: wait 0x00000000 control 0x00010000 status 0x00000001 enable 0x00000000
tcs 2..7: wait 0 control 0x00000000 status 0x00000001 enable 0
tcs 8: 全零
```

### 3. 解读

- 上一轮打印的 `common control 0x01010000` 其实是 **TCS0 的窗口**（`tcs_base + 0x14`），
  不是全局寄存器；真正的 drv 级寄存器是 `+0x00/0x04/0x08/0x0C`；
- `prnt_chld = 0x800c8104`，按 `DRV_NUM_TCS_MASK/SHIFT` 取 drv-2 的
  `max_tcs = (config >> 12) & 0x3F = 8`：硬件只有 8 个 TCS，TCS8 不存在。
  设备树里的 `2+3+3+1` 必须按 Linux 的规则把 CONTROL_TCS 排除在连续数组之外理解；
- TCS0 两个命令槽都被使能，且都留着 CLK 类请求（`0x30000`、`0x30010`），
  状态 `0x010101`；继续把 TCS0 排除；
- TCS1 的 `control` 是 `0x00010000`，与 TCS2..7 的 `0` 不同，说明它不是“完全未配置”；
  `enable = 0`、没有命令残留，但还不能据此认定可以安全触发，需要确认 bit16 的
  AMC 语义；
- `irq_en = 0x00020000`：TCS1 的完成中断没有使能，所以第一次真实发送只能用轮询，
  不能等中断。

### 4. R5-C1 没有在本轮实现

AMC trigger 的 `CONTROL` 位定义还需要对着 `rpmh-rsc.c` 的触发路径逐项确认；
在把“已触发”和“已完成”分开之前写 sink，风险大于收益。下一轮先补这段静态分析，
再按批准的条件实现编译期不可达的写路径。

## 三十六、IRQ 寄存器归位与 TCS1 完整状态（0127）

### 1. 修正：IRQ 寄存器在 tcs_base，不在 drv base

`rpmh-rsc.c` 里 `read_tcs_reg(drv, RSC_DRV_IRQ_STATUS, 0, 0)` 的地址是
`tcs_base + reg + 672*tcs_id`，所以：

| 寄存器 | 物理地址 |
| --- | --- |
| `IRQ_ENABLE` | `0x18220d00` |
| `IRQ_STATUS` | `0x18220d04` |
| `IRQ_CLEAR` | `0x18220d08` |
| `DRV_PRNT_CHLD_CONFIG` | `0x1822000c` |

0126 从 `drv+0x00/0x04/0x08` 读到的三个数（`0x00020000` 等）不是 RSC 中断状态，
0127 已改回 `tcs_base+0x00/0x04/0x08`。实测 `irq_en = 0x00000001`，即只有 TCS0
的完成中断被使能，TCS1 没有。

### 2. `prnt_chld` 解码

`0x800c8104` → drv-2 `tcs = (config >> 12) & 0x3F = 8`，
`ncpt = (config >> 27) & 0x1F = 16`。硬件每个 TCS 有 16 个命令槽，而设备树的
`2+3+3+1` 要把控制 TCS 排除在连续数组之外；探针现在只遍历 TCS0–7。

### 3. TCS 实测

```text
irq_en 0x00000001 irq_status 0x00000000 irq_clear 0x00000000 prnt_chld 0x800c8104
prnt_chld decode: drv-2 tcs 8, ncpt 16
tcs 0: control 0x01010000 (amc_en 1 amc_trig 1) status 0x1 enable 0x3
tcs 0 slot 0: msgid 0x00010108 addr 0x00030000 data 0x5  status 0x00010101
tcs 0 slot 1: msgid 0x00010108 addr 0x00030010 data 0x5  status 0x00010101
tcs 0 slot 2..7: addr 0x00050004/0x000500a4/0x00050044/0x00050028/0x00050074/0x0005007c
tcs 1: control 0x00010000 (amc_en 1 amc_trig 0) status 0x1 enable 0
tcs 2..7: control 0x00000000 status 0x1 enable 0
```

- TCS0 实际有 **8 条**残留命令（不止两条），`0x30000/0x30010` 是 CLK 类，
  `0x500xx` 是另一类资源；`status` 前两条是 `0x00010101`，其余为 0。TCS0 继续完全排除；
- TCS1 的 AMC 已使能、Trigger 未置位、使能字为 0、16 个命令槽全空；
  这比 TCS0 干净得多，但仍不等于“所有权已交给 XNU”；
- `irq_en` 只有 TCS0 的位，所以第一次真实发送只能用轮询，不能等中断。

### 4. R5-C1：写路径已写成但编译期关闭

`kebab_spmi.c` 里新增了 `kebab_rpmh_tcs_send()`（只针对 TCS1、只接受编码器能产出的
命令、每次写回都有界等待、完成后立即把通道标记为不可再用），整段包在
`#if KEBAB_RPMH_WRITES` 里，宏为 0，所以**这一版内核里不存在该函数**：
没有控制台命令、没有启动参数、没有运行时开关能到达它。

需要说明：因为宏是 0，这段代码没有被编译器检查过，属于待审草案；R5-C2 的离线模拟
和真正的编译验证要在打开宏之后单独做。

### 5. 验证边界

- 实测：上述逐槽输出、`prnt_chld` 解码、IRQ 寄存器归位；
- 未做：任何 TCS/RSC 写；写路径未编译进内核。

## 三十七、TCS1 发送状态机的虚拟 MMIO 验证（R5-C2，0128）

### 1. 草案的四个阻塞问题都改了

| 问题 | 处理 |
| --- | --- |
| 入口没有命令白名单 | `kebab_rpmh_tcs_send()` 先检查空指针、`kebab_rpmh_cmd_allowed()`，再检查通道必须是 TCS1 |
| 完成只看 COMPL | 要求 `ISSUED` 与 `COMPL` 同时成立；预检发现残留 COMPL 直接拒绝，旧完成位不能冒充本次完成 |
| 失败路径无条件清理 | 触发前只撤销本次写入（CONTROL 清 trigger、ENABLE/WAIT 清零）；触发后不清理、保留现场、标记 dead 并返回超时 |
| 一次性限制被当成完整供电 | 注释与文档明确：单条命令实验模式，不等于两路 LDO 的完整上电序列 |

### 2. 同一套状态机跑虚拟寄存器

`package/KebabRpmhTcsHostTests.c` 用注入的 `kebab_tcs_io` 驱动与内核将要用同一份
`kebab_rpmh_tcs_send()`，33 项断言全过，覆盖：

- `CONTROL=0x00010000`（AMC 已使能、Trigger 未置位）可以进入；
- 正常路径的写序列与顺序（msgid→addr→data→wait→enable→control→trigger→回收）；
- 未授权命令零次 MMIO 写；
- `ENABLE≠0`、`TRIGGER=1`、残留 `COMPL` 都拒绝且零写；
- 回读不匹配时有界超时，不无限循环；
- 只有 COMPL 没有 ISSUED、或只有 ISSUED 没有 COMPL 都不判成功；
- 触发后超时：不再写任何寄存器，`enable` 保持原值留作证据，通道标记不可用；
- 一次性门控：第二次调用直接拒绝且零写；
- TCS0/TCS2 一律拒绝。

### 3. 内核侧

0128 删除了 0127 里那段未编译的 `#if KEBAB_RPMH_WRITES` 草案，改成一条指向
`package/KebabRpmhVrm.c` 的说明。理由：同一份状态机不应在树里存在两份，一份
已通过虚拟测试、一份从未被编译器检查过。生产镜像仍然没有任何写路径。

### 4. 验证边界

- 实测：33 项虚拟 MMIO 断言、整树构建 0 error；
- 未做：没有把启用写路径的镜像刷到设备，没有真实 TCS 写入；
- 未解决：TCS1 的实际所有权（AMC 已使能、Trigger 未置位只是必要条件）。

## 三十八、R5-C3：隔离编译与故障注入补强

### 1. 四个边界问题都改了

| 问题 | 处理 |
| --- | --- |
| 触发前失败无条件回滚 | 用 `staged` 位记录本次真正写成功的阶段（WAIT/ENABLE/CONTROL），`tcs_undo_pre_trigger()` 只撤销这些阶段，且每次回滚都读回确认 |
| 失败后还能重试 | 任何一次失败（含回滚失败）都把 `lock->dead` 置位，本次启动不再尝试 |
| 成功后回收失败被忽略 | 回收的三次写改为 `tcs_write_sync()` 并检查结果；失败返回新的 `KEBAB_RPMH_ERR_RECLAIM`，阶段记为 `COMPLETE_UNCLEAN`——“命令完成”和“通道已安全回收”从此是两个结论 |
| 重新初始化绕过一次性限制 | `dead/used` 移出通道结构，放进调用者持有的 `kebab_rpmh_lock`；`kebab_rpmh_tcs_init()` 不再碰这两个字段 |

### 2. 故障注入测试：40 项

新增/扩展的用例：

- 第一条命令写失败：只尝试一次写，不做任何回滚写；
- 回读不匹配：有界返回、回滚只覆盖已写入阶段、锁关闭；
- 状态寄存器读失败：预检阶段拒绝，零次写；
- 清理阶段写失败：返回 `RECLAIM`、阶段 `COMPLETE_UNCLEAN`、锁永久关闭；
- 成功后重新 `init`：第二次发送仍被拒绝且零写；
- 触发后超时：不写任何清理寄存器，`enable` 原样保留作证据。

`40 checks, 0 failure(s)`；加上编码/授权那套 57 项，共 97 项主机断言。

### 3. R5-C3 第一阶段：隔离内核编译

用 XNU 自己的编译命令（从构建日志取 `kebab_spmi.c` 那条，替换源文件）编译
`package/KebabRpmhVrm.c`，加 `-DKEBAB_RPMH_KERNEL`：

- 头文件切换为 `sys/types.h` + `libkern/libkern.h`，用户态那套 `stdint/stddef/string`
  在宏打开时不再引入；
- 结果：0 error、3 warning，全部来自既有头文件（`KEBAB` 宏重定义、`kcdata.h`
  的 declaration-after-statement 等），没有一条指向本模块；
- 符号导出正常：`kebab_rpmh_encode_*`、`kebab_rpmh_cmd_db_lookup`、
  `kebab_rpmh_tcs_send` 等全部生成，未解析符号只有 `_strcmp`/`_strlen`
  （内核本来提供）。

产物只是目标文件，没有链接进内核，也没有刷机镜像。

### 4. 验证边界

- 实测：97 项主机断言、内核标志编译的告警与符号；
- 未做：没有把 `kebab_tcs_io` 绑到真实 RSC 寄存器，没有控制台写入口，没有真机写入；
- 未解决：TCS1 的独占所有权；超时仍是循环次数上限，不是按 XNU 时钟标定的时间上限。

## 三十九、R5-C4 时钟超时与 R5-O1 所有权审计

### 1. C4：五个边界

| 问题 | 处理 |
| --- | --- |
| 预检读失败没有关锁 | 任何预检读失败都置 `lock->dead`（fail-closed） |
| `staged` 只记录确认写入 | 拆成 `attempted` 与 `confirmed`：只要存在“已尝试但未确认”的写，就不假定回滚成功，保留现场并锁死 |
| 旧 `ISSUED` 未被拒绝 | 预检要求 `ISSUED` 与 `COMPL` 都为零，旧状态不能与新的 Complete 拼成本次完成 |
| Trigger 依赖读回 | Trigger 改为单次写、不读回；硬件是否接受由 `ISSUED/COMPL` 判定，与厂商 `__tcs_set_trigger()` 一致 |
| 并发 | `kebab_rpmh_lock` 增加 `busy` 占用位，重入直接拒绝 |

超时也改成两层：每阶段既有单调时钟预算（回读 10 ms、完成 100 ms），也保留轮询次数
上限；`kebab_tcs_io.now_us` 缺失时只用次数上限，存在时任何时钟停滞或跳变都不会
造成死循环。

### 2. C4 主机测试：48 项

新增用例：预检读失败即锁死、旧 `ISSUED` 拒绝且零写、Trigger 不回声仍能成功、
时钟停滞仍能返回超时、`busy` 重入拒绝。加上编码/授权 57 项，共 105 项主机断言。

### 3. O1：谁在写 APPS RSC

已证实：

- 本仓库的 XNU 树里**没有任何 RPMh/RSC 写入路径**。`0x18220000`、`0x18200000`、
  `0x80860000` 只出现在 `bsd/dev/kebab_spmi.c` 的只读探针里；
- 该文件里所有对 RSC/TCS 的访问都是 `kebab_spmi_rd()`，`kebab_spmi_wr()` 只用于
  SPMI arbiter（PON 写路径），不涉及 RSC；
- 因此“XNU 当前不写 RSC”是可证实的。

尚未证实：

- TCS0 里那 8 条 CLK/BUS 命令是谁留下的（ABL/XBL 或上一次 Android 启动），
  内核侧看不到写入者；
- TCS1 为什么是 `AMC_ENABLE=1`、`TRIGGER=0`：看起来是固件初始化过但没有提交；
- XNU 是否 APPS RSC 的唯一软件管理者：没有交接约定，ABL 可能保留状态跨过 payload 启动。

被阻塞：

- 要回答“谁写的”，需要 UEFI/ABL 侧日志或反汇编证据，当前拿不到；
- 直接写 TCS1 做实验属于 NO-GO，不能用“写一下看看”代替所有权证据。

### 4. 结论

TCS1 目前只能算“候选通道”，不能声明已获得独占写入权。剩下的选择是：拿到
ABL/UEFI 侧的 RPMh 初始化与交接证据，或者与固件约定一个明确的交接点；在那之前，
真实 VRM 请求继续保持 NO-GO。

## 四十、R5-C5 时钟与并发修复、R5-O2 分层审计

### 1. C5：两个真实漏洞

- **完成轮询没有时间预算**：`KEBAB_TCS_CMPL_TIMEOUT_US` 只是定义，循环里只比对了
  次数。现在完成轮询同时使用 `now_us()` 的 100 ms 预算与 10000 次上限；时钟正常
  时由时间结束，时钟停滞或倒退时由次数上限结束。
- **`busy` 不是原子获取**：原来“检查 dead/used/busy，再写 busy=1”是两个普通内存
  操作，两个线程可以同时通过。现在用 `__atomic_exchange_n(busy, 1, ACQ_REL)` 抢占，
  抢占后再复核 dead/used，失败则释放。

### 2. C5 测试：56 项

- 正常时钟下完成超时的读次数在 100–3000 之间，证明是时间预算而不是 10000 次上限
  结束的；
- 冻结时钟：读次数 ≥1000，仍能返回超时；
- 倒退时钟：立即结束，不因无符号相减而长时间循环；
- 无时钟回调：退回次数上限；
- 两个线程同时调用：恰好一个执行、另一个被抢占拒绝，寄存器只收到一笔事务（10 次写）。

顺带修正：上一轮 C16 用“永不完成 + 时钟每步前进”，实际没有冻结时钟；现在
`now_mode = 1` 才是真正的冻结。

### 3. O2：三层证据，必须分开

| 层次 | 结论 | 依据 |
| --- | --- | --- |
| 软件访问审计 | 已证实：当前 XNU 不写 APPS RSC | 全树 grep：三个地址只出现在 `kebab_spmi.c`，访问全为读；`kebab_spmi_wr()` 只写 SPMI arbiter |
| 启动链状态 | 受阻：无法归因 | `unixnu` 加载器源码不在本仓库，ABL 闭源；TCS0 的 8 条 CLK/BUS 命令、TCS1 的 `AMC_ENABLE=1` 只能观察 |
| 所有权交接 | 受阻：没有约定 | 没有任何文档或代码规定 XNU 何时接管 drv-2 的 Active TCS |

已有的只读快照（`RPMH?`）能长期稳定地显示 TCS1 空，但“长期为空”不等于“已获得
管理权”。要推进，需要 unixnu/ABL 侧的证据或一个明确的交接协议，而不是再加几次
寄存器快照。

### 4. 验证边界

- 实测：56 项主机断言（含并发）、105 项编码/授权与故障注入断言；
- 未做：真实 RSC/TCS 写入；`XNU-0127.img` 未变。

## 四十一、C6 全局锁与 O3-A/B 启动链审计

### 1. C6：全局唯一锁与并发压力

- 新增 `kebab_rpmh_tcs_init_global()`：内核端口必须使用模块内的单一
  `kebab_rpmh_global_lock`，不再允许两个调用点各自建锁绕过“每次启动一次”；
- 并发测试改为 50 轮、`pthread_barrier` 同步起跑的双线程竞争：每轮恰好一个赢家、
  一个被拒绝、寄存器只收到一笔事务（10 次写）；另加一条断言确认两个通道拿到的是
  同一个全局锁对象；
- 本套 58 项断言全过。

### 2. O3-A：实际镜像对应哪份固件源码

- `signed.img` 前 8 MiB 里保留了固件构建路径
  `/home/albert/Codex/UEFI/Mu-Silicium/Build/kebabPkg/DEBUG_CLANGPDB/AARCH64/...`；
- 该源码树在 Ubuntu 上存在：`/home/albert/Codex/UEFI/Mu-Silicium`，HEAD 为
  `7013966c`，但工作区有未提交修改（`APRIORI.inc`、`DXE.inc`、`DeviceBuild.py`、
  `BootManager.c` 等），**所以刷入的 UEFI 不等于 7013966c 这个提交**；
- 结论：能定位到树，但还不能把某个 git 提交声明为设备上运行的那一份；需要构建
  记录或镜像哈希才能锁定版本。

### 3. O3-B：UEFI/UniXNU/DarwinBoot 是否碰 RSC

| 组件 | 结果 |
| --- | --- |
| GFander/UniXNU（公开） | 无 `18200000`/RSC 访问；只有一个 Python 脚本提到 rpmh/GMU votes |
| UniXNU/DarwinBoot（公开） | 完全没有匹配 |
| Mu-Silicium kebabPkg | `APRIORI.inc`/`DXE.inc` 都加载 `Binaries/kebab/QcomPkg/Drivers/RpmhDxe/RpmhDxe.inf` |

`RpmhDxe.efi`（65,536 字节）里能读出 53 处 “TCS” 和 6 处 “Rpmh” 字符串，说明
UEFI 阶段确实带了一个 RPMh 驱动。这解释了 TCS0 的 CLK/BUS 残留与 TCS1 的
`AMC_ENABLE=1` 可能来自哪里，也说明“XNU 是唯一 RPMh 使用者”不成立。

### 4. 对交接协议的影响

- 最后一个可控阶段（UEFI）既然能访问 RPMh，就必须由它显式声明是否在退出前停止
  使用 TCS1，并把当时的只读状态写进交接记录；
- 现有公开的 UniXNU/DarwinBoot 源码没有 RSC 访问，说明交接记录可以放在它们这一层
  传递，而不必改 ABL；
- 但 `RpmhDxe` 是二进制、行为未知，交接前还需要知道它是否会留下未完成的请求。

### 5. 验证边界

- 实测：58 项主机断言（含 50 轮并发）、UEFI 源码树与镜像构建路径、RpmhDxe 字符串；
- 未做：没有解析 `RpmhDxe.efi` 的实际行为，没有改启动链，没有真机 RPMh 写入。

## 四十二、C6 锁身份收紧与 O3-C0/C1 启动链取证

### 1. C6：内核构建只能使用全局锁

- `kebab_rpmh_tcs_init(t, io, lock)` 与 `kebab_rpmh_global_lock_get()` 现在只在
  非 `KEBAB_RPMH_KERNEL` 构建里可见；
- 内核构建下 `kebab_rpmh_tcs_send()` 会校验 `t->lock == &kebab_rpmh_global_lock`，
  调用者自带锁直接返回 `TCS_STATE`；
- 两套主机测试仍全过（58 + 57）。

### 2. O3-C0：当前源码确实调用 ExitBootServices

- 本机实际用于构建的 DarwinBoot 源码在 `/home/albert/tmpwork/dboot`，与公开仓库
  版本只差两处打印字符串，**没有绕过分支**；`DarwinBoot.c` 第 686–691 行仍是
  `GetMemoryMap` → `ExitBootServices(mImage, Key)`，循环最多三次；
- 历史记录里的“EBS 卡在 CoreTerminateMemoryMap 后绕过”在这次源码里找不到，
  说明那是另一次构建或另一个分支；
- 但**镜像级验证受阻**：`signed.img` 里的 FV 是压缩的，搜不到 `DARWINBOOT`
  的宽字符或 ASCII 字符串，无法直接证明刷入的固件就是这份源码编出来的。
  要闭环需要：构建产物的哈希与 FV 对照，或在 UEFI 阶段加一条可见输出。

另外 Mu-Silicium 里存在 `KernelErrataPatcherLib`，它会挂钩 `gBS->ExitBootServices`；
但 kebabPkg 的 dsc/fdf 没有引用它，所以这条 hook 在 kebab 构建里是否生效同样未知。

### 3. O3-C1：RpmhDxe 目前只能静态观察

- `RpmhDxe.efi` 65536 字节，`.depex` 18 字节（无依赖表达式），字符串里有
  `Rpmh` 与 53 处 `TCS`，没有 `ExitBootServices` 字样；
- 它是二进制、无源码，无法判断它是否在 EBS 前完成所有 TCS 事务；
- 若实际启动路径跳过 EBS，那么它的退出回调（如果有）就不会执行，残留状态可能
  比正常退出更多。

### 4. O3-C2/C3 的结论

- 在拿到“是否调用 EBS”的镜像级证据之前，不能依赖“固件正常退出”作为交接前提；
- 交接记录（`KebabRscHandoff`）应设计为在**最后一次进入 XNU 之前**由引导器写入，
  而不是依赖 EBS 通知；
- 对 TCS0/TCS1 的早期残留统一标记 `PREEXISTING_UNKNOWN`，不声明所有权。

### 5. 验证边界

- 实测：两套主机测试、DarwinBoot 源码对比、RpmhDxe 二进制字符串；
- 未做：未反汇编 RpmhDxe，未证明镜像中的 DarwinBoot 版本，未做真机 RPMh 写入。

## 四十三、C6-F 私有化与 O3-C2 镜像级闭环

### 1. C6-F

`kebab_rpmh_tcs_init()` 在内核构建里已不存在：实现改为 `static
kebab_rpmh_tcs_init_impl()`，带自定义锁的包装函数只在非 `KEBAB_RPMH_KERNEL` 构建
里定义。发送入口的锁身份检查保留。两套主机测试仍全过（58 + 57）。

### 2. O3-C2：把 signed.img 拆开看

- FV 头 `_FVH` 在文件偏移 `0x1098`，FV 从 `0x1070` 开始；
- 用 `uefi_firmware` 递归解包（FV 是压缩的，原始字节里搜不到 GUID），共 106 个 FFS；
- 找到 `RpmhDxe`：FFS GUID `cb29f4d1-7f37-4692-a416-93e82e219766`，文件 65,608
  字节；内部三段：
  - `0x13` DEPEX 18 字节：`02 42 b9 37 ae 7f 45 91 4c a1 96 d9 66 9f d3 47 a3 08`，
    即 PUSH GUID `AE37B942-457F-4C91-A196-D9669FD347` + END；
  - `0x10` PE32 65,536 字节，SHA-256
    `166f49bdbc6955a74718be9a2907bc1dfbd15c6e2e28c1b7707ede941c17eb67`；
  - `0x15` UI 16 字节（`RpmhDxe`）。

### 3. 与本地构建产物比对

`Binaries/kebab/QcomPkg/Drivers/RpmhDxe/RpmhDxe.efi` 的 SHA-256 与镜像里的 PE32
**完全一致**，`.depex` 逐字节相同。这说明镜像里的 RpmhDxe 就是本仓库 `Binaries/`
下那份二进制，出处确定。

### 4. APRIORI 把 DEPEX 绕过去了

FV 里还有一个 1,284 字节的 FFS，GUID `fc510ee7-ffdc-11d4-bd41-0080c73c8881`
（UEFI PI 的 APRIORI 文件 GUID），其内容包含 RpmhDxe 的 GUID。按 PI 规范，
APRIORI 列出的驱动按列表顺序调度，**DEPEX 条件不参与调度判断**。

结论：这个镜像里 RpmhDxe 会被早期调度，不能用“DEPEX 依赖的协议没装”来推断它
没有运行。UEFI 确实执行了 RPMh 驱动，XNU 不是唯一的 RPMh 使用者，TCS0/TCS1 的
残留有了明确候选来源。

### 5. 仍未证明

- RpmhDxe 具体写了哪些 TCS、是否在退出前完成事务（需要反汇编或运行时探针）；
- 引导器是否调用 EBS 仍未在镜像级闭环（FV 里的 DarwinBoot 模块未按名字找到，
  可能在 payload 或另一个 FV）；
- 因此 TCS1 所有权仍是 UNKNOWN，真实写入继续 NO-GO。

## 四十四、O3-C3 初步：RpmhDxe 的静态指纹

### 1. 基准

`RpmhDxe.efi`（65,536 字节，SHA-256 `166f49bd…`）与镜像 FV 内的 PE32 逐字节一致，
本节所有偏移都以这份文件为准。

### 2. PE 结构

| 项 | 值 |
| --- | --- |
| Machine | `0xaa64` |
| EntryPoint RVA | `0x1000` |
| `.text` | RVA `0x1000`，`0xd000` 字节 |
| `.data` | RVA `0xe000`，`0x1000` |
| `.reloc` | RVA `0xf000` |

### 3. 字符串证据（文件偏移 = RVA，均在 .text 内）

| RVA | 字符串 |
| --- | --- |
| `0xbe5b` | `configuring TCS (DRV_ID: %d)(TCS_ID: %d)(AMC?: %d)` |
| `0xbeaa` | `TCS SENT (TCS INDEX: %d) (TCS_ID: %d)` |
| `0xbe44` | `TCS IDLE (TCS_ID: %d)` |
| `0xbe31` | `!!!!TCS STUCK!!!!` |
| `0xbfc4` | `Invalid BaseAdresss/numcmdspertcs for drv, %d, passed to rscHalGetNumC…` |
| `0xc364` | `Invalid TCS Id, %d, in RscHalTriggerTCS` |
| `0xc3ff` | `Invalid TCS Id, %d, in RscHalConvertTCStoAMC` |
| `0xc491` | `Invalid TCS Id, %d, in RscHalConvertAMCtoTCS` |
| `0xbc01` | `rpmh_exit_sleep, flushing wake AMCs (DRV_ID: 0x%08x)` |
| `0xbba3` | `flushing sleep AMCs` |

这些名字说明驱动内部有一层 RSC HAL：TCS 触发（`RscHalTriggerTCS`）、TCS 与 AMC
互转、每 DRV 的基址与每 TCS 命令数配置、以及一个 `rpmh_exit_sleep` 退出路径。

### 4. 基址不是硬编码在指令里

扫描 `.text` 里的 `ADRP+ADD`，没有任何目标落在 `0x18xxxxxx`；而文件偏移 `0xe22c`
（`.data` 内）出现小端常量 `00 00 20 18`，即 `0x18200000`。也就是说 RSC 基址来自
数据表/配置结构，代码通过指针访问——这与“只搜 `0x18220000` 找不到写入者”的
预期一致。

### 5. 当前能回答到什么程度

| 问题 | 结论 | 证据等级 |
| --- | --- | --- |
| RpmhDxe 是否初始化 drv-2 | 未知 | 有 HAL 代码与基址表，但没有调用链 |
| 是否分配/使用 TCS1 | 未知 | 存在 TCS/AMC 转换与触发函数 |
| 是否有退出时完成/回收 | 存在 `rpmh_exit_sleep` 与 flush 字符串 | 函数存在，可达性未证 |
| 进入 XNU 前 TCS1 是否已不归固件管理 | 不能证明 | — |

同时修正一处表述：APRIORI 只证明该驱动被列为优先调度、DEPEX 不作为调度阻塞，
不等于入口已执行、更不等于 RPMh 初始化成功。镜像层面的结论应保持
“包含指定版本并列入 APRIORI”。

### 6. 下一步的逆向目标（按优先级）

1. 从 `RscHalTriggerTCS` 字符串反向找引用它的函数，建立“初始化 → 配置 TCS →
   触发 → 等待完成”的调用图；
2. 找到 `.data` 里基址表的引用者，确认它对应哪个 DRV 与 TCS 数量；
3. 找到 `rpmh_exit_sleep` 的调用点，判断是否注册了 EBS 或 Exit 事件；
4. 只有在调用图能连到“进入内核前”这条路径上，才谈得上 TCS1 的交接证据。

真实写入仍 NO-GO。

## 四十五、O3-C4 进展与术语修正

### 1. 术语修正：`rpmh_exit_sleep` 不是驱动退出

`rpmh_exit_sleep`、`flushing sleep AMCs`、`flushing wake AMCs` 这一组字符串属于
RPMh 的**电源状态转换**（sleep/wake），与 UEFI 的 `ExitBootServices`、驱动 Unload
不是同一类。之前把它列为“固件退出时完成或回收”的证据是错的，本轮撤回。

要分开两张调用图：

- A：RPMh 初始化与命令执行（配置 TCS → AMC → Trigger → 完成）；
- B：UEFI 驱动生命周期（DXE 入口 → 事件注册 → EBS/Unload 处理）。

只有 B 的路径能连到 A 的回收动作，才谈得上 EBS 触发 TCS 回收。

### 2. `.data` 里那个基址

文件偏移 `0xe22c` 属于 `.data`（raw `0xe000` ↔ RVA `0xe000`），所以它同时也是
RVA `0xe22c`。周边是按 4 字节排列的字段：

```text
... 0x00000000 0x18200000 0x00000002 0x00000000 0x00000001 ...
```

`0x18200000` 紧邻的值是 `2`，看起来像 `{base, count}` 形式的表项（可能是 DRV 数或
TCS 组数），但这只是结构猜测，尚未证明代码如何索引它。

### 3. 字符串引用没有走 ADR/ADRP

对 `TCS IDLE`、`configuring TCS`、`TCS STUCK`、`TCS SENT`、`RscHalTriggerTCS`
错误串、`rpmh_exit_sleep` 等 RVA 做 ADR/ADRP 直接引用扫描，命中数为 0。说明这些
字符串不是通过 ADR/ADRP 直接取址，更可能是经由指针表、`ldr` 字面量或间接计算。
这同时也说明：之前“按字符串找函数”的方法需要换成“先找引用表/字面量，再回溯
函数边界”。

### 4. 本轮没有建立调用图

- C4-A（Trigger 日志的 XREF 与调用者）：未完成；
- C4-B（基址表引用者与 DRV 索引）：只做到结构旁证；
- C4-C/D（退出路径与事件注册）：未开始；
- TCS1 所有权：仍 UNKNOWN。

### 5. 下一步的具体做法

1. 在 `.data` 里找指向字符串的 4/8 字节表项（RVA 或 image-relative），再找引用该
   表的代码；
2. 在 `.text` 里扫描 `ldr` 字面量池，匹配字符串或基址结构的地址；
3. 用函数边界（`pacibsp`/`stp x29,x30` 序言与 `ret`）把命中点归到具体函数，
   记录 RVA；
4. 沿调用者向上，直到 DXE 入口，才可能回答“是否存在 TCS 回收”。

真实写入继续 NO-GO；设备保持 `XNU-0127.img`。

## 四十六、O3-C4-A：从配置入参到全局指针的第一条链

### 1. 地址空间

`ImageBase = 0x0`，因此本模块里 RVA 与首选 VA 相同；`.text` 覆盖
`0x1000–0xe000`，`.data` 在 `0xe000`。下面所有地址都是 RVA。

### 2. `.data` 里的描述符表

```text
0x0e1d0: 000000000000bf00 0000000200000002
0x0e200: 000000000000e1d0 00000000000061e8
0x0e210: 0000000000006268 0000000000006280
0x0e220: 0000000000006298 1820000000000002
0x0e230: 0000000000000002 0000000000000001
0x0e240: 000000000000e228 0000000000000000
```

- `0xe228` 处 64 位字是 `1820000000000002`：低 32 位 `2`、高 32 位 `0x18200000`；
  换句话说 `0xe22c` 只是这个字的高半部分，它后面的 `0xe230` 是 `{2, 1}`；
- `0xe200` 指向 `0xe1d0`，`0xe240` 指向 `0xe228`（自引用指针）；
- `0x61e8/0x6268/0x6280/0x6298` 都落在 `.text`，是函数指针候选。

目前**没有**任何代码用 `ADRP+ADD/LDR` 直接引用 `0xe228` 或 `0xe200`；这几处只被
数据互相指向，说明它们可能通过“配置结构指针”间接到达，而不是取静态地址。

### 3. 第一条可验证的链

```text
0x123c(x0, x1)
  -> bl 0x1c2c
       0x1c38  str x0, [x8, #0x258]      ; 全局 0xe258 = x0
       0x1c44  str x1, [x9, #0x260]      ; 全局 0xe260 = x1
       0x1c4c  ldr x8, [x1, #0x60]
       0x1c54  str x8, [x10, #0x268]     ; 全局 0xe268 = x1->0x60
       0x1cbc  ldr x8, [x1, #0x58]
       0x1cc4  str x8, [x9, #0x270]      ; 全局 0xe270 = x1->0x58
```

- `0x1c2c` 是初始化函数：把两个入参和 `x1` 的两个字段存进四个全局指针；
- 任一入参为空时走 `bl 0x19e0`（断言/日志），字符串地址在 `0xb27c`、`0xb297`、
  `0xb2b4`、`0xb2c8`；
- 调用者 `0x123c` 只是转交 `x0/x1`，说明真正的配置结构来自更上一层。

这条链证明了：RpmhDxe 内部**确实**有一个“配置结构 → 全局指针”的初始化步骤，
`0x18200000` 所在的表很可能就是这类结构的来源之一，但还没有证据把它接到
`0x1c2c` 的入参上。

### 4. 下一步

1. 找 `0x123c` 的调用者，追到给 `x1` 赋值的指令，确认配置结构来自 `.data`
   的哪一段（是否就是 `0xe1d0`/`0xe228` 那块）；
2. 找到之后再沿 `0xe260/0xe268` 的读方（例如 `0x1138` 的 `ldr x1, [x8, #0x260]`）
   追到 MMIO 访问；
3. 用 `.reloc` 的 DIR64 记录核对哪些数据指针在加载时被重定位，避免把“文件里的
   相邻字段”误当成指针。

### 5. 证据等级

| 项 | 等级 |
| --- | --- |
| 0x1c2c 把入参写入全局 | DIRECT（指令可见） |
| 0x123c 调用 0x1c2c | DIRECT |
| 0xe228 表项与 0x18200000 的关系 | INFERRED（结构旁证） |
| 该表被 0x123c 使用 | UNRESOLVED |
| TCS1 初始化/回收 | UNRESOLVED |

真实写入继续 NO-GO。

## 四十七、更正：`0x1c2c` 是 UEFI 服务表初始化，不是 RSC 配置

### 1. 入口证据

`EFI_SYSTEM_TABLE`（64 位）字段偏移：`RuntimeServices` = `0x58`，
`BootServices` = `0x60`，`NumberOfTableEntries` = `0x68`，`ConfigurationTable` = `0x70`。

`0x1000` 的入口代码：

```text
0x1000  cmp  x0, #0
0x1004  b.eq #0x10bc
0x1018  cmp  x1, #0
0x1020  ldrb w8, [x1]        ; 检查 x1 开头的三个字节
0x1024  cmp  w8, #0x49
0x102c  ldrb w8, [x1, #1]
0x1030  cmp  w8, #0x42
0x1038  ldrb w8, [x1, #2]
0x103c  cmp  w8, #0x49
0x104c  ldr  x2, [x1, #0x68] ; NumberOfTableEntries
0x1050  ldr  x3, [x1, #0x70] ; ConfigurationTable
```

`x1` 被按 `EFI_SYSTEM_TABLE` 的 `0x68`/`0x70` 字段访问，且每个字段都是 64 位，
与“x1 = SystemTable”吻合。入口处还检查了 `x1` 开头三个字节是否为 `I B I`
（`0x49 0x42 0x49`），这一步目前无法解释，先记为未解决细节。

### 2. 因此 `0x1c2c` 的语义要改

```text
0x123c(x0 = ImageHandle, x1 = SystemTable)
  -> 0x1c2c
       0xe258 = x0            ; gImageHandle
       0xe260 = x1            ; gST
       0xe268 = [x1 + 0x60]   ; gBS   = SystemTable->BootServices
       0xe270 = [x1 + 0x58]   ; gRT   = SystemTable->RuntimeServices
```

四个全局的用途与 EDK II 的 `gImageHandle/gST/gBS/gRT` 完全一致。这条链属于
**UEFI 驱动生命周期（B 图）**，不是 RPMh 配置（A 图）。上一节的“配置结构”判断
撤回。

### 3. 证据等级重排

| 发现 | 等级 |
| --- | --- |
| `0x123c → 0x1c2c` 调用 | DIRECT |
| `0x1c2c` 四次全局存储 | DIRECT |
| `x1+0x58/0x60` 对应 RT/BS | HIGH CONFIDENCE（入口按 0x68/0x70 访问同一结构） |
| `0x1c2c` 为服务表初始化 | HIGH CONFIDENCE |
| `0xe228` 与 `0x18200000` 的字节关系 | DIRECT |
| `0xe228` 字段含义 | UNRESOLVED |
| `0xe228` 被这条链使用 | UNRESOLVED（已证伪为“配置结构”） |

另外说明：`ImageBase = 0` 只说明文件里的首选 VA 等于 RVA，运行时地址仍由加载器
和重定位决定。

### 4. 下一步（两条并行）

- **C4-B1**：确认 `0x1000 → 0x123c` 的调用关系，并检查 `0xe268/0xe270` 的读方
  是否通过 `blr` 间接调用 Boot/Runtime Services 函数表；成立即可把四个全局
  正式命名为 `gImageHandle/gST/gBS/gRT`；
- **C4-A2**：从 `0xe200` 的函数指针候选 `0x61e8/0x6268/0x6280/0x6298` 出发恢复
  函数边界与间接调用者，并独立分析 `0xe240 → 0xe228` 的数据关系，找真正形成
  RSC 基址并访问寄存器的路径。函数地址出现在数据结构里不等于被调用，必须找到
  `blr` 或结构被传递的证据。

TCS1 所有权仍 UNRESOLVED，真实写入 NO-GO。

## 四十八、O3-C4-B2：入口图闭环与 Boot Services 用法

### 1. 入口到初始化

- `0x1194` 处 `bl 0x123c`，即前一节那个保存 `gImageHandle/gST/gBS/gRT` 的
  初始化函数；
- `0x1000` 起是 DXE 入口（检查 `x0 != 0`、`x1` 的 `EFI_SYSTEM_TABLE` 签名
  `IBI`、读 `+0x68/+0x70`），控制流在入口函数内延伸到 `0x1194`；
- 因此 `0x1000 → 0x1194 → 0x123c → 0x1c2c` 这条链可以标为 DIRECT。

签名检查只比较了前三个字节（`0x49 0x42 0x49`），不是完整的八字节 `IBI SYST`；
这不影响语义判断，但严格说这是“部分签名检查”。

### 2. Boot Services 的实际用法（taint 跟踪 gBS）

| 偏移 | UEFI 字段 | 使用点 |
| --- | --- | --- |
| `0x30` | `FreePages` | `0x2488` |
| `0x98` | `HandleProtocol` | `0x11bc`（随后 `blr x9`） |
| `0x150` | `InstallConfigurationTable` | `0x14d4`（随后 `blr x4`） |
| `0x170` | `UnloadImage` | `0x1514` |

**没有**出现 `0x178`（`ExitBootServices`），也没有 `0x50`（`CreateEvent`）或
`0x70`（`CloseEvent`）。

### 3. 这意味着什么

- RpmhDxe 自身既不调用 `ExitBootServices`，也没有通过 `gBS->CreateEvent` 注册
  EBS 事件；它的 TCS 事务不会在 EBS 回调里被回收；
- 它在 `0x14d4` 调用了 `InstallConfigurationTable`，参数里的表指针是 `0xe0d8`；
- `0xe0d0` 一带的数据是：
  ```text
  0x0e0d0: a0003f8e 3b7269c9 00010001 00000000
  0x0e0e0: 000015a4 000015bc 000015f8 00001634
  ```
  前 16 字节像 GUID 加版本，后面是四个指向 `.text` 的函数指针
  （`0x15a4/0x15bc/0x15f8/0x1634`）。这更像一张“发布给 OS 的表”，而不是
  驱动退出路径。

### 4. 对所有权问题的影响

- “固件在 EBS 时回收 TCS”这一假设，在这个驱动上不成立：它没有 EBS 回调；
- 因此 TCS0/TCS1 的残留如果要被清理，只可能发生在别处（ABL、其他 DXE 驱动或
  根本没人清理）；
- `InstallConfigurationTable` 发布的那张表值得后续分析：它可能是 RPMh 相关状态
  交给 OS 的通道，也可能是 RSC 配置的来源之一。

### 5. 证据等级

| 项 | 等级 |
| --- | --- |
| `0x1194 → 0x123c` | DIRECT |
| `0x123c` 保存四个 UEFI 全局 | HIGH CONFIDENCE |
| gBS 偏移 0x30/0x98/0x150/0x170 的使用 | DIRECT |
| 无 `ExitBootServices`/`CreateEvent` 调用 | HIGH CONFIDENCE（taint 窗口内） |
| 配置表 `0xe0d0/0xe0d8` 的内容与用途 | UNRESOLVED |
| TCS1 所有权 | UNKNOWN |

真实写入继续 NO-GO。

## 四十九、更正：RpmhDxe 确实注册了 EBS 事件

### 1. Boot Services 偏移表纠正

| 偏移 | 正确函数 | 上一轮的错误命名 |
| --- | --- | --- |
| `0x30` | `FreePages` | 正确 |
| `0x98` | `HandleProtocol` | 正确 |
| `0x148` | `InstallMultipleProtocolInterfaces` | 未识别 |
| `0x150` | `UninstallMultipleProtocolInterfaces` | 误称 `InstallConfigurationTable` |
| `0x170` | `CreateEventEx` | 误称 `UnloadImage` |
| `0xC0` | `InstallConfigurationTable` | 没有证据 |
| `0xE0` | `UnloadImage` | 没有证据 |
| `0xE8` | `ExitBootServices` | 没有调用 |

`0x178` 是结构体尾部，不是 `ExitBootServices`；上一轮按 `0x178` 搜索是错的。

### 2. `CreateEventEx` 调用现场（0x1514）

```text
0x14f8  adrp x2, #0x1000
0x1508  add  x2, x2, #0x49c      ; NotifyFunction = 0x149c
0x14fc  adrp x4, #0xe000
0x150c  add  x4, x4, #0x28       ; EventGroup = 0xe028
0x1504  adrp x5, #0xe000
0x1510  add  x5, x5, #0x248      ; &Event = 0xe248
0x1518  mov  w0, #0x200          ; Type = EVT_NOTIFY_SIGNAL
0x151c  mov  w1, #8              ; NotifyTpl = TPL_CALLBACK
0x1520  mov  x3, xzr             ; Context = NULL
0x1524  blr  x9                  ; gBS->CreateEventEx
```

`0xe028` 的 16 字节是 `55 f0 ab 27 b8 b1 26 4c 80 48 74 8f 37 ba a2 df`，
即 **`27ABF055-B1B8-4C26-8048-748F37BAA2DF` = `EFI_EVENT_GROUP_EXIT_BOOT_SERVICES`**。

上一轮“RpmhDxe 不注册 EBS 事件、不会在 EBS 回收 TCS”的结论**撤回**。

### 3. EBS 回调做了什么

`0x149c`（NotifyFunction）：

```text
0x149c  stp  x29, x30, [sp, #-0x10]!
0x14a4  bl   #0x14b0
0x14ac  b    #0x3ba4
```

`0x14b0`：

```text
x10 = gBS
x0  = [0xe250]                    ; 镜像句柄
x1  = 0xe068                      ; 协议 GUID
x2  = 0xe0d8                      ; 接口实例
x3  = 0
x4  = [gBS + 0x150]               ; UninstallMultipleProtocolInterfaces
br  x4
```

`0x3ba4`（尾调用）遍历一张表：全局指针 `[0xbef8]`，元素步长 `0x30`，对每个元素
调用 `0x7fdc` 与 `0x80c4`。

也就是说，EBS 时驱动会注销自己的协议接口，再走一遍“客户端/命令表”的清理。
这是**真实的 EBS 清理路径**，其内部是否完成或丢弃 TCS 事务，取决于 `0x7fdc`、
`0x80c4` 两个函数。

### 4. 之前误读的 `0xe0d8`

`0xe0d0` 那 16 字节并不是 UEFI Configuration Table 的 GUID，而是协议接口结构；
`InstallMultipleProtocolInterfaces` 在 `0x1568`、`UninstallMultipleProtocolInterfaces`
在 `0x14d4`，配对使用同一个 GUID（`0xe068`）与接口指针（`0xe0d8`）。

### 5. 状态

| 项 | 状态 |
| --- | --- |
| EBS 事件注册 | 已证实（GUID 与 CreateEventEx 参数均对齐） |
| EBS 回调内容 | 注销协议 + 表清理（`0x7fdc`/`0x80c4`） |
| 回调是否触及 RSC/TCS | UNRESOLVED，下一步 |
| drv-2 TCS1 所有权 | UNKNOWN |
| 真实写入 | NO-GO |

下一步只有三件事：反汇编 `0x7fdc` 与 `0x80c4`、解析 `[0xbef8]` 的表结构、
确认回调清理的是命令队列还是 TCS 硬件状态。

## 五十、O3-C5：EBS 回调的清理内容与内存服务风险

### 1. `0x7fdc` / `0x80c4` 做什么

两者结构几乎相同：

```text
0x7fec  adrp x22, #0xe000 ; x22 = 0xe850
0x7ffc  mov  x0, x22
0x08000  mov  w1, w21     ; w21 = 第一个参数（id）
0x08004  bl   #0x65f0     ; 查表/查询
0x08008  mov  w19, w0
0x0800c  cbz  w19, #0x8044
0x08018  adrp x3, #0xc000
0x0801c  add  x3, x3, #0x2c3   ; 出错日志字符串
0x0802c  bl   #0xa49c          ; 日志输出
```

查询失败时打印日志返回；成功分支在 `0x8044`/`0x812c` 处用 `id * 0x18` 计算
表项地址（元素 24 字节）。两条路径都**没有**任何指向 `0x18xxxxxx` 的地址构造，
也没有写 RSC 寄存器的迹象——它们更像“按 id 清理客户端/命令状态”，不是 TCS 硬件
回收。

### 2. DXE Core 的 EBS 顺序（Mu 源码）

`Mu_Basecore/MdeModulePkg/Core/Dxe/DxeMain/DxeMain.c`：

```text
790:  CoreNotifySignalList (&gEfiEventBeforeExitBootServicesGuid);
802:  Status = CoreTerminateMemoryMap (MapKey);
807:    CoreNotifySignalList (&gEventExitBootServicesFailedGuid);
816:  CoreNotifySignalList (&gEfiEventExitBootServicesGuid);
```

即：先通知 BeforeEBS，再终止内存映射，成功后才通知 EBS。RpmhDxe 注册的正是
`gEfiEventExitBootServicesGuid`，所以它的回调（`0x149c`）**运行在内存映射已经
终止之后**。

### 3. 风险链

```text
CoreTerminateMemoryMap() 成功
  -> CoreNotifySignalList(ExitBootServicesGuid)
    -> RpmhDxe 0x149c -> 0x14b0
      -> UninstallMultipleProtocolInterfaces(gBS+0x150)
        -> EDK II 可能进入 CoreFreePool / 释放协议或句柄路径
```

UEFI 规范要求在 EBS 通知回调里不得调用会改变内存映射的服务；EDK II 的协议卸载
路径在移除最后一个协议时可能释放内存。这与历史上“EBS 卡在
`CoreTerminateMemoryMap` 之后、`CoreNotifySignalList` 处理中”的位置吻合。

**这仍是假设**：需要证明事件注册在真机上成功、回调确实被触发、且卸载路径进入了
释放分支。但它是目前最有解释力的线索，优先级高于继续猜测 TCS1 状态。

### 4. 状态

| 项 | 判定 |
| --- | --- |
| `0x7fdc/0x80c4` 为软件表清理 | HIGH CONFIDENCE（无 MMIO 地址构造） |
| EBS 回调运行在内存映射终止之后 | DIRECT（Mu 源码顺序） |
| 回调可能在 EBS 期间释放内存 | 高风险，待证明 |
| 与历史 EBS 挂死的因果 | 未证明 |
| TCS1 是否被释放 | UNKNOWN |
| 真实写入 | NO-GO |

### 5. 下一步

1. 反汇编 `0x65f0` 与表 `0xe850`，确认 `0x7fdc/0x80c4` 清理的对象；
2. 在 `0x14b0` 的卸载路径上确认 EDK II 是否会走到 `CoreFreePool`；
3. 若要验证历史挂死，最省事的是在 UEFI 侧加一条“EBS 前后”的预留内存记录，
   而不是再改内核。

## 五十一、O3-C6-E1/E2：在真实 Mu 源码里确认释放路径

### 1. 协议卸载必然走到 `CoreFreePool`

`Mu_Basecore/MdeModulePkg/Core/Dxe/Hand/Handle.c`（实际用于构建的树）里，
`CoreUninstallProtocolInterface()` 的成功分支：

```c
Prot = CoreRemoveInterfaceFromProtocol (Handle, Protocol, Interface);
if (Prot != NULL) {
    gHandleDatabaseKey++;
    Handle->Key = gHandleDatabaseKey;
    RemoveEntryList (&Prot->Link);
    Prot->Signature = 0;
    CoreFreePool (Prot);          /* line 881 */
    Status = EFI_SUCCESS;
}
```

释放的是 `PROTOCOL_INTERFACE` 本身，**与“是否最后一个协议”无关**；随后才判断
`IsListEmpty (&Handle->Protocols)` 决定是否释放 Handle。

`Mem/Pool.c` 里还有 `CoreFreePoolPagesI()`（line 630），调用点在 681、813、871，
说明池释放可能下降到页级释放。

结论：在 RpmhDxe 的 EBS 回调里调用 `UninstallMultipleProtocolInterfaces()`，只要
接口移除成功，就会进入 `CoreFreePool(Prot)`；这一步发生在
`CoreTerminateMemoryMap()` 之后，属于规范禁止的“EBS 回调中调用内存服务”形状。

### 2. RpmhDxe 的注册与安装顺序

```text
0x1514  blr [gBS+0x170]        ; CreateEventEx(EBS group)
0x1528  x0 = 0x1584, x1 = 0
0x1534  bl 0x2218              ; 回调注册辅助
0x153c  失败则 0x18dc 打印 "Rpmh ... callback registration failed"（0xa779）
0x1554  ldr x4, [gBS+0x148]    ; InstallMultipleProtocolInterfaces
0x1568  x0 = 0xe250, x1 = 0xe068, x2 = 0xe0d8, x3 = 0
0x1580  br x4                  ; 尾调用，未检查返回值
```

即：先注册 EBS 事件，再安装协议接口；安装是尾调用，返回值不在本函数里检查。

### 3. 证据状态

| 项 | 判定 |
| --- | --- |
| Mu Handle.c 成功卸载后 `CoreFreePool(Prot)` | DIRECT（源码 line 881） |
| 池释放可能进入页级释放 | DIRECT（Pool.c 630/681/813/871） |
| RpmhDxe EBS 回调调用卸载 | DIRECT |
| 回调运行在内存映射终止之后 | DIRECT（DxeMain.c 790/802/816） |
| 真机上回调实际到达释放点 | UNKNOWN |
| 与历史 EBS 挂死的因果 | UNKNOWN |
| TCS1 硬件回收 | UNKNOWN |

### 4. 边界

- `0x7fdc/0x80c4` 只证明“没有直接 MMIO 地址构造”，不能排除它们调用的函数间接
  触碰硬件；上一轮的 HIGH CONFIDENCE 只适用于这个局部判断；
- 这套结论基于当前 Mu_Basecore 工作区（`7013966c` + 本地修改），与刷入固件的
  对应关系仍是“同一棵树，未锁定提交”。

### 5. 下一步

1. C6-E3：确认卸载时接口移除成功的条件（GUID/接口指针是否与安装一致），
   以及 Mu 的 Pool 实现在 EBS 后是否直接进入页级路径；
2. C6-A1：解析 `0x65f0`、`0xe850` 与 `0x3ba4`，把软件清理和间接硬件调用分开；
3. C6-E4：设计 EBS 阶段探针（预留内存 + 直接写 framebuffer 两条通道），先离线。

## 五十二、O3-C6-E3：卸载成功的三个前提

`Mu_Basecore/.../Hand/Handle.c` 的 `CoreUninstallProtocolInterface()` 顺序：

```c
if (Protocol == NULL) return EFI_INVALID_PARAMETER;
CoreAcquireProtocolLock ();
Status = CoreValidateHandle (UserHandle);              /* 句柄仍有效 */
if (EFI_ERROR (Status)) goto Done;
Prot = CoreFindProtocolInterface (UserHandle, Protocol, Interface);
if (Prot == NULL) { Status = EFI_NOT_FOUND; goto Done; }  /* GUID+接口指针必须匹配 */
Status = CoreDisconnectControllersUsingProtocolInterface (UserHandle, Prot);
if (EFI_ERROR (Status)) goto Done;                      /* 有驱动拒绝释放 */
Prot = CoreRemoveInterfaceFromProtocol (Handle, Protocol, Interface);
if (Prot != NULL) { ...; CoreFreePool (Prot); }
```

因此进入 `CoreFreePool(Prot)` 需要同时满足：

1. `[0xe250]` 里的句柄在 EBS 时仍有效；
2. `0xe068` 的 GUID 与 `0xe0d8` 的接口指针仍登记在该句柄上（与
   `0x1568` 安装时的参数一致，这一点反汇编已对上）；
3. `CoreDisconnectControllersUsingProtocolInterface()` 成功——它会调用驱动
   binding 的 `Stop()`，也就是执行第三方驱动代码，本身就可能分配/释放内存。

第 3 点意味着：即使最终没有走到 `CoreRemoveInterfaceFromProtocol`，卸载过程也
已经进入了会调用任意驱动 `Stop()` 的路径；错误分支不能当作“没有副作用”。

### 1. 安装侧的返回值

`0x1568` 是 `br x4` 尾调用 `InstallMultipleProtocolInterfaces`，返回值直接交回
调用者，本函数不检查。也就是说，安装是否成功在这里没有记录；但 `0x14b0` 在 EBS
时无条件尝试卸载，失败会返回错误给 DXE Core。

### 2. 探针设计（E4，离线）

阶段记录只用预分配的固定内存和已映射的 framebuffer，不在 EBS 期间调用
`Print()`、分配或动态日志：

| 阶段 | 位置 | 用途 |
| --- | --- | --- |
| E1 | `CoreTerminateMemoryMap` 返回 | 证明内存映射终止完成 |
| E2 | EBS 事件分发前 | 证明进入通知阶段 |
| E3 | 调用 RpmhDxe 回调前 | 证明回调被执行 |
| E4 | 进入协议卸载 | 证明风险路径到达 |
| E5 | `CoreFreePool(Prot)` 前 | 证明即将释放 |
| E6 | `CoreFreePool(Prot)` 后 | 判断释放是否返回 |
| E7 | RpmhDxe 回调返回 | 判断是否卡在回调内部 |
| E8 | EBS 返回 DarwinBoot | 确认整体退出完成 |

E1/E2/E5 需要改 `Mu_Basecore`（可控源码）；E3/E4/E7 可以在 RpmhDxe 里打点，但那是
二进制，优先级低于 DXE Core。framebuffer 通道用于 EBS 卡死时也能看到最后阶段。

### 3. 状态

| 项 | 判定 |
| --- | --- |
| 卸载成功三前提 | DIRECT（源码） |
| 卸载错误分支仍有驱动 `Stop()` 调用 | DIRECT |
| 安装返回值未检查 | DIRECT（尾调用） |
| 真机是否到达 E5 | UNKNOWN |
| 历史挂死因果 | UNKNOWN |
| TCS1 所有权 | UNKNOWN |

下一步：C6-E3 的剩余部分（实际协议打开记录与 `Stop()` 行为）与 C6-E4 的离线探针
实现；真实 RPMh 写入继续 NO-GO。

## 五十三、O3-C6-E3b/E4a：OpenList 也会释放内存，探针设计

### 1. Mu 源码：卸载路径里不止一次内存操作

`CoreDisconnectControllersUsingProtocolInterface()`（Handle.c:727）实际做三件事：

1. 只对 `EFI_OPEN_PROTOCOL_BY_DRIVER` 的记录调用
   `CoreDisconnectController()`——也就是说，**只有存在驱动使用者时才会执行第三方
   驱动的 `Stop()`**；
2. 成功之后，对 `BY_HANDLE_PROTOCOL`/`GET_PROTOCOL`/`TEST_PROTOCOL` 的记录执行
   `RemoveEntryList` + `CoreFreePool (OpenData)`；
3. 若仍有未释放的打开记录或断开失败，调用 `CoreConnectController(...)` 重新连接，
   返回 `EFI_ACCESS_DENIED`。

因此即使最终没有走到 `CoreFreePool(Prot)`，这条路径也可能已经：
- 执行了第三方驱动的 `Stop()`；
- 释放了 `OPEN_PROTOCOL_DATA`；
- 通过 `CoreConnectController` 重新分配/连接。

这三项都发生在 EBS 通知之后，与“EBS 回调不得依赖内存服务”的约束冲突。

### 2. 探针（E4a）设计要点

记录区：

```c
typedef struct {
    uint32_t magic;        /* 'KRSC' */
    uint32_t last_stage;   /* E1..E8 */
    uint64_t stamp[16];    /* 每次阶段切换的时间戳/序号 */
    uint64_t arg[8];       /* handle/guid/interface/prot 等 */
} kebab_ebs_record;
```

- 在 DXE 早期（`CoreInitializeEventServices` 之后、最后一次 `GetMemoryMap` 之前）
  用 `AllocatePages` 分配，内存类型 `EfiReservedMemoryType`，并在 `gBS->Exit`
  之前不再分配；
- 通过 `InstallConfigurationTable`（在正常 Boot Services 阶段）登记一个 GUID 指向
  该记录，供 XNU 启动后读取；
- 阶段写入只做 `volatile` 写，不调用 `Print()`、`Allocate*`、`Free*`、`GOP`。

插桩点（Mu_Basecore，均可控源码）：

| 阶段 | 位置 | 记录内容 |
| --- | --- | --- |
| E1 | `CoreExitBootServices` 中 `CoreTerminateMemoryMap` 返回处 | 返回值 |
| E2 | `CoreNotifySignalList(&gEfiEventExitBootServicesGuid)` 之前 | 事件组 |
| E3 | 事件分发器调用 NotifyFunction 之前 | 回调地址（重定位后）、Event |
| E4 | `CoreUninstallProtocolInterface` 入口 | handle、GUID、interface |
| E4a | `CoreFindProtocolInterface` 之后 | 是否找到 Prot 指针 |
| E4b | `CoreDisconnectControllersUsingProtocolInterface` 前后 | 返回值、是否进入 Stop |
| E4c | `CoreFreePool(OpenData)` 前后 | OpenData 指针 |
| E5/E6 | `CoreFreePool(Prot)` 前后 | Prot 指针 |
| E7 | 事件分发器返回后 | 回调是否返回 |
| E8 | DarwinBoot 拿到 EBS 成功返回后 | 完整退出 |

上下文归属：E5/E6 不能只记“调用了 CoreFreePool”，必须记录 `Prot` 指针并与 E4
的接口指针比对，否则其他驱动的释放会被误判。

显示通道：预取 framebuffer 基址（此前已确认 `0x9C000000` 这条路径存在），在
EBS 之后直接写像素表示 `last_stage`，不调用 GOP。

### 3. 探针的边界

- 需要修改 Mu_Basecore 并重新构建 UEFI，属于固件改动，必须先离线构建与静态核对，
  再决定是否刷机；
- E3/E7 在 DXE Core 的事件分发器里记录，不改闭源 RpmhDxe；
- 记录区分配必须在最后一次 `GetMemoryMap` 之前完成，EBS 之后只写不分配。

### 4. 状态

| 项 | 判定 |
| --- | --- |
| OpenList 清理会 `CoreFreePool(OpenData)` | DIRECT（Mu 源码） |
| 失败路径会 `CoreConnectController` | DIRECT（Mu 源码） |
| 只有 BY_DRIVER 才执行 `Stop()` | DIRECT（Mu 源码） |
| 探针设计 | 已完成离线设计，未实现 |
| 历史挂死根因 | 仍未证实 |
| TCS1 所有权 | UNKNOWN |

真实 RPMh 写入继续 NO-GO。

## 五十四、O3-C6-E4b：探针记录模块（主机侧）

### 1. 三处修正已落实

- **身份匹配**：比较的是 `Prot->Interface` 与安装时的接口指针，加上 handle 与
  GUID；`PROTOCOL_INTERFACE` 对象地址被明确拒绝（测试里单独有一条断言）。
- **事件序列**：结构改为固定 64 条的事件数组，每条含 `seq/stage/context/
  object/status`，满后置 `overflow`，不扩容；另保留 `last_stage` 给 framebuffer。
- **时间戳**：不再设计成 EBS 期间调用 UEFI 时间服务，第一版只用单调递增的
  `seq`。

### 2. 模块与测试

`package/KebabEbsProbe.{h,c}`：无分配、无 UEFI 服务依赖的纯记录模块；可选
`kebab_ebs_mirror` 回调把 `stage` 直接写显示通道。`KebabEbsProbeHostTests.c`
18 项断言，覆盖顺序、嵌套（E4→E4a→E4b→E4c→E4b→E5→E6 全保留）、溢出标记、
身份匹配与“Prot 对象地址不算匹配”。

```text
18 checks, 0 failure(s)
```

### 3. 可见性与交接（待实现部分）

- `volatile` 只约束编译器，不保证 Cache 写回；实现时需要一个 `ProbeWrite()`
  统一做固定内存写入 + `dsb`/`dmb` 屏障，是否做 Cache 维护取决于记录页的实际
  内存属性；
- 记录页在最后一次 `GetMemoryMap` 之前用 `EfiReservedMemoryType` 分配，通过
  `InstallConfigurationTable` 发布；`EfiReservedMemoryType` 只避免被普通分配
  复用，不会自动给 XNU 建立映射；
- 交接链必须显式验证：**UEFI ConfigurationTable → DarwinBoot 取物理地址 →
  ADT/boot_args 传递 → XNU 映射并读取**，缺一段都不能算可见。

### 4. 状态

| 项 | 判定 |
| --- | --- |
| 记录模块（主机侧） | 已实现，18 项断言通过 |
| 身份匹配修正 | 已实现并测试 |
| 嵌套/溢出 | 已实现并测试 |
| Cache 可见性与 ProbeWrite | 未实现 |
| XNU 交接链 | 未实现 |
| Mu 树内插桩 | 未开始 |
| 历史挂死根因 | UNKNOWN |
| TCS1 所有权 | UNKNOWN |

真实 RPMh 写入继续 NO-GO；本轮没有修改固件或设备镜像。

## 五十五、O3-C6-E4b-F：记录器的六项收紧

### 1. 改动

| 编号 | 问题 | 处理 |
| --- | --- | --- |
| F1 | 身份匹配与写入分离 | `kebab_ebs_event_add()` 内部先比较 handle/GUID/接口指针，不匹配直接返回 0，**不碰 `last_attempted_stage`、不调 mirror** |
| F1b | GUID 只有指针 | 目标结构拆成 `guid_ptr` 与 `guid_lo/hi`，目前比较指针，16 字节值比较留给接入时使用 |
| F2 | 重入无保护 | 用 `__atomic_fetch_add(&claimed, 1)` 领取槽位，两个线程不会拿到同一槽；越界只置 `lost`，不覆盖 |
| F3 | 进入与提交不分 | `last_attempted_stage` 在写前设置，`last_committed_stage` 在 `seq` 用 release 存储发布后才设置；未提交槽的 `seq == 0` |
| F4 | 记录区与 mirror 生命周期不一致 | 单一全局记录区 + 单一 mirror，`g_sealed` 保证一次启动只能 init 一次 |
| F5 | E4b/E4c 进入返回同码 | 拆成 `E4B_ENTER/E4B_RETURN`、`E4C_ENTER/E4C_RETURN`、`E5_ENTER/E6_RETURN` |
| F6 | magic 字节序 | 改为 `0x4353524b`（内存里是 `4b 52 53 43` = “KRSC”），结构加 `version`、`record_size`、`capacity` |

### 2. 测试 24 项

新增/改写的用例：

- 错误接口、错误 GUID 指针：返回 0，`committed == 0`，`last_attempted_stage` 不变，
  `lost == 1`；
- 全零目标：拒绝一切写入；
- ENTER/RETURN 顺序、`seq` 从 1 开始、`last_committed_stage` 跟随最后一次成功写入；
- 未使用槽 `seq == 0`（“进入但未提交”可与“已提交”区分）；
- **两个线程各写 33 次**：64 条全部唯一序号，第 65 次置 `lost`；
- init 之后 `sealed`，第二次 init 不触碰别的记录区。

```text
24 checks, 0 failure(s)
```

### 3. 仍未做

- `ProbeWrite()` 的屏障与 Cache 维护；
- ConfigurationTable → DarwinBoot → ADT/boot_args → XNU 的交接验证；
- Mu_Basecore 内的插桩与隔离编译。

### 4. 状态

| 项 | 判定 |
| --- | --- |
| 身份过滤内聚 | 已实现并测试 |
| 原子槽位与丢失标记 | 已实现并测试 |
| 进入/提交/返回区分 | 已实现并测试 |
| 记录区生命周期 | 单实例、一次性初始化 |
| 固件集成 | 未开始 |
| 历史挂死根因 | UNKNOWN |
| TCS1 所有权 | UNKNOWN |

真实 RPMh 写入继续 NO-GO。

## 五十六、O3-C6-E4b-F2：并发计数、阶段一致性与入口拆分

### 1. 修复

| 问题 | 处理 |
| --- | --- |
| `committed` 可倒退 | 删除共享计数；槽位以 release 发布的 `seq` 为准，读者逐槽检查 `seq != 0` |
| 阶段字段数据竞争 | 用一个 64 位原子 `newest = (seq << 32) | stage`，晚提交的旧槽不会让显示回退，阶段与序号不会错配 |
| 全零目标测试假阳性 | 测试先 `test_reset()` 再以全零目标 init，单独断言 `target_valid() == 0`、`claimed == 0` |
| GUID 只比指针 | 目标可保存 16 字节 GUID；有值时按内容比较，指针只作快速关联 |
| 全局/目标事件混用 | 拆成 `event_add_global()`（仅 E1/E2/E7/E8）与 `event_add_target()`（需完整身份） |
| `lost` 含义混杂 | 拆成 `overflow`（容量耗尽）与 `rejected`（身份不符） |

### 2. 可复现的乱序提交测试

新增测试钩子 `test_delay_slot(slot, spins)`：让先领取 slot 0 的线程在发布前停留，
另一个线程先提交 slot 1。测试断言：

- `ev[0].seq == 1`、`ev[1].seq == 2`（两者都提交）；
- `newest >> 32 == 2`（不是 1，说明旧槽晚提交没有让阶段/序号倒退）。

### 3. 测试

```text
24 checks, 0 failure(s)
```

覆盖全局/目标入口互斥、全零目标、接口/GUID 指针/GUID 内容三种身份拒绝、
ENTER/RETURN 顺序、乱序提交、溢出与拒绝分开计数。

### 4. 仍未做

- `ProbeWrite()` 的屏障与 Cache 维护；
- ConfigurationTable → DarwinBoot → ADT/boot_args → XNU 交接；
- Mu_Basecore 插桩与隔离编译；
- `newest` 的读者侧约定要写进 XNU 读取代码（acquire load + 逐槽 `seq`）。

### 5. 状态

| 项 | 判定 |
| --- | --- |
| 原子领取唯一槽位 | 通过 |
| 提交计数并发正确性 | 已改为逐槽 `seq`，乱序测试通过 |
| 最新阶段一致性 | 单 64 位原子 |
| 身份（含 GUID 内容） | 已实现并测试 |
| 全局/目标归属 | 已拆分 |
| 固件集成 | 未开始 |
| 历史挂死根因 | UNKNOWN |
| TCS1 所有权 | UNKNOWN |

真实 RPMh 写入继续 NO-GO。

## 五十七、O3-C6-E4b-F3：mirror 单调性与回调身份

### 1. 修复

| 问题 | 处理 |
| --- | --- |
| mirror 可能显示旧阶段 | mirror 回调改收**已发布的 `newest` 值**（`seq<<32|stage`），不再收调用者自己的 stage；旧槽晚提交时显示保持新阶段 |
| 乱序测试不确定 | 新增 `test_block_slot()/test_release_slot()`：slot 0 在发布 `seq` 前阻塞，slot 1 完整提交后才放行，确定性复现“旧槽晚提交” |
| E3/E7 无身份 | 新增 `kebab_ebs_set_callback()` 与 `event_add_callback()`：E3/E7 必须匹配注册时的 NotifyFunction 与 Event 对象；E7 从全局白名单移除 |
| 全局/目标边界 | 全局入口只收 E1/E2/E8；目标入口拒收 E3/E7 |

### 2. 测试 26 项

新增：E7 走全局入口被拒、E3/E7 的错误 NotifyFunction/Event 被拒、E3 走目标入口被拒、
阻塞式乱序提交（slot 1 先提交、slot 0 后提交）、mirror 从未看到更小值且最终显示
`seq == 2`。

```text
26 checks, 0 failure(s)
```

### 3. 仍未做

- `ProbeWrite()`：屏障与按内存属性决定是否 `dc cvac`，`dsb` 不等于写回缓存；
- `ConfigurationTable → DarwinBoot → ADT/boot_args → XNU` 交接；
- 在 Mu_Basecore 内插桩并隔离编译；
- 目标身份的动态绑定：RpmhDxe 先注册事件再安装协议，Handle 只能在安装后确定。

### 4. 状态

| 项 | 判定 |
| --- | --- |
| mirror 单调性 | 已实现并测试 |
| 确定性乱序提交 | 已实现并测试 |
| E3/E7 回调身份 | 已实现并测试 |
| 固件集成 | 未开始 |
| 历史挂死根因 | UNKNOWN |
| TCS1 所有权 | UNKNOWN |

真实 RPMh 写入继续 NO-GO。

## 五十八、O3-C6-E4c：一次性身份绑定与接口清理

### 1. 本轮改动

- 新增一次性绑定：`kebab_ebs_bind_callback()` 与 `kebab_ebs_bind_target()`，
  首次成功后 `g_cb_bound`/`g_target_bound` 置位，后续绑定一律拒绝，防止后来者
  改写已记录的身份；
- 绑定目标时同时写入 16 字节 GUID（有则按内容比较）；
- 清理头文件：删除已无实现的 `kebab_ebs_test_delay_slot()` 声明与重复 `#endif`；
- 修正注释：E7 是 E3 的返回侧，走回调入口，不属于全局白名单。

### 2. 测试 32 项

新增 6 项：第一次绑定成功、第二次拒绝、身份字段保持不变；目标绑定同样一次性，
且 GUID 内容被保存。

```text
32 checks, 0 failure(s)
```

### 3. framebuffer 的定位

按评审意见，framebuffer 只作 **best-effort 故障显示**：`mirror` 收到的是已发布的
`newest`，显示端自己再做单调保护；权威证据始终是逐槽 `seq` 的内存记录。真正接入
Mu 时若事件记录与显示都在同一执行上下文串行完成，就不需要额外的锁。

### 4. 仍未做（E4c 剩余）

- `ProbeWrite()` 的 AArch64 后端：`dsb` 与按内存属性决定的 `dc cvac`；
- 记录页属性检查（MAIR/页表），不能只看 `EfiReservedMemoryType`；
- `ConfigurationTable → DarwinBoot → ADT/boot_args → XNU` 交接与读取侧校验；
- Mu_Basecore 插桩与五类故障注入。

### 5. 状态

| 项 | 判定 |
| --- | --- |
| 一次性身份绑定 | 已实现并测试 |
| 头文件清理 | 完成 |
| ProbeWrite / Cache 可见性 | 未实现 |
| 交接链 | 未实现 |
| 历史挂死根因 | UNKNOWN |
| TCS1 所有权 | UNKNOWN |

真实 RPMh 写入继续 NO-GO。

## 五十九、固件日志与 RPMh 探针

> 本节原计划编号“二十三”，但任务下发后文档已经扩展到五十八节，编号冲突，故顺延
> 为五十九。内容对应设备仓库新增的 `firmware/` 目录（两个 DXE 驱动与集成说明）
> 和内核补丁 `0129-kebab-fwlog-reader.patch`。

### 1. 固件日志环的布局

UEFI 侧在固定物理地址留一个 256 KiB 的环，内核只读地把它映射进来：

| 项 | 值 |
| --- | --- |
| 物理地址 | `0xF44C0000` |
| 大小 | 64 页 = `0x40000`（256 KiB） |
| 头部 | 16 字节：`u32 magic`、`u32 size`、`u64 head` |
| magic | `0x474c464b`（小端字节 `'K' 'F' 'L' 'G'`） |
| size | 数据区字节数，合法范围 `1 .. 0x40000-16`；正常值 `0x3FFF0` |
| head | 历史累计写入字节数（单调），release store |
| data | 从 `+0x10` 开始；绝对偏移 `p` 的字节在 `data[p % size]` |

这一页的归属由 `kebabPkg` 的 `MemoryMapLib` 描述符 `{"Kebab FwLog", 0xF44C0000,
0x00040000, ..., Reserv, ...}` 决定：PEI 阶段就把它标成 `EfiReservedMemoryType`，
DXE 的 `AllocatePages()` 必然拿不到（`EFI_NOT_FOUND`），`KebabFwLogDxe` 只把失败
当提示，照常写 `Magic = 0`、`Size`、`Head = 0`、fence、`Magic = 'KFLG'`。描述符
才是契约。第一次真机测试用的 `0x9FF00000` 恰好是 `SEC Heap`（`0x9FF00000` +
`0x8C000`，`BsData`），`AllocatePages` 失败后驱动直接返回，magic 从未写入；内核
读到的是 SEC 堆里的 `0x00380001`，于是报 `no firmware log attached`。

生产者是 `FrameBufferSerialPortLib` 的 `SerialPortWrite()`：它在把字节画到 GOP
之前调用 `KebabFwLogAppend(Buffer, NumberOfBytes)`，所以屏幕上的固件文本同时进了
环。`KebabFwLogAppend()` 不分配、不调用 UEFI 服务：magic 未置位时直接返回；写了
就按 64 位取模推进，最后用 release store 发布 `head`。

`0xF44C0000` 是从原 `DXE Heap`（`0xB0000000 + 0x04500000`）顶部切出的 256 KiB，
`DXE Heap` 相应缩短为 `0x044C0000`；它在交给内核的 `0xA0000000..0xB0000000` 之外，
也不与 `SEC Heap`、`CPU Vectors`、`MMU PageTables`、`UEFI Stack`、`Log Buffer`、
`Info Blk` 重叠。地址在三处必须一致：`KebabFwLog.h` 的 `KEBAB_FWLOG_PA`、串口库
里的副本、内核 0129 的 `KEBAB_FWLOG_PA`。

### 2. 内核侧：`bsd/dev/kebab_fwlog.c`（0129）

新增 `bsd/dev/kebab_fwlog.c` 并在 `bsd/conf/files` 登记，不再往
`kebab_spmi.c` 末尾追加代码；SPMI/RPMh 驱动保持原样。

* `kebab_fwlog_attach()`：`ml_io_map(0xf44c0000, 0x40000)` 只做一次，校验 magic
  和 size 后缓存映射；`io_map` 子图只有 8 MiB，所以即使 header 非法也不第二次
  映射。缺失、magic 不对、size 不在范围内，各打一行带地址和读到的值的日志。
* `kebab_fwlog_report(max_bytes)`：从最新处往回取 `max_bytes`，按 oldest-first
  输出。起点在窗口**前半段**内找 `'\n'` 对齐（`n/2` 的界限保证不会把整段跳
  过去），跳过 `'\r'`，每约 192 字节批量 `printf` 一次，避免逐字符 flush。头行
  打印 `ring=` 和 `written=`，尾行打印实际输出的字节数。
* `kebab_fwlog_boot_summary()`：`bsd_init` 在 `kebab_usb_start()` 之后调用，只打
  一行 `ring N bytes, written M bytes`；attach 失败时不重复，原因已经由 attach
  打过。
* 控制台命令沿用 `SPMI?`/`PON?` 的写法：`FWLOG?` 输出 4096 字节，`FWLOG? all`
  请求整环。两者都要求在**一次 bulk-OUT 完成**内到达；KDP 接管字节流时按首字节
  锚定，和 `REBOOT`/`SPMI?` 一致。

整环请求有一个明确的截断：USB 控制台环是 `16384` 字节（可用 `16383`），把
`0x3FFF0` 字节直接灌进去会静默丢字节。因此 `max_bytes == 0` 在实现里截到
`12288`，并在头行写 `(whole ring capped at 12288 for the 16384-byte console
ring)`；尾行仍然是实际输出的字节数。

输出格式示例（数值为占位）：

```text
KEBAB FWLOG: ring=262128 written=812345 dump=4096
...固件文本...
KEBAB FWLOG: ---- end (4096 bytes) ----

KEBAB FWLOG: ring=262128 written=812345 dump=12288 (whole ring capped at 12288 for the 16384-byte console ring)
...固件文本...
KEBAB FWLOG: ---- end (12288 bytes) ----
```

### 3. RPMh 探针：从“问对了问题”到“能回答”

旧探针（`firmware/KebabRpmhProbeDxe.c` 的前身）有三个问题：`LocateProtocol`
失败时只打一句 `protocol not found`，看不到 status 和 interface；默认 interface
本身就是函数表；每个指针都重新枚举一遍 loaded image handle。

新的 `firmware/KebabRpmhProbeDxe.c`：

* 每次转储都打印 GUID、`LocateProtocol` 的 status 和 interface 指针，失败也打；
* 第一次成功定位后解析 interface 所属的 loaded image，缓存 base 和 size，之后
  所有指针分类只用缓存，不再枚举 handle；
* 打印 interface 处 8 个原始 64 位字，逐个给 `rva=` 或 `not-code`；
* 把 interface 当表，最多打印 64 项（靠近镜像末尾时按剩余字节截断）；
* 对 `raw[0]`、`raw[1]`，只有落在镜像内才继续当候选表打印；
* 5 秒一次性定时器（`t5`）和 `gEfiEndOfDxeEventGroupGuid`（`eod`）各转储一次，
  两次输出逐行可比；
* 只读：除了 `LocateProtocol` 不调用协议函数，不写 RPMh/RSC 寄存器。

输出格式示例（地址为占位）：

```text
KRP: t5 guid=e1d0b573-f29f-495d-9352-ca0dd42a0a74 status=Success (0x0) iface=0x9f123450
KRP: t5 image base=0x9f100000 size=0x40000
KRP: t5 iface rva=0x123450
KRP: t5 raw words (8)
KRP: t5 raw[0] 0x9f1a1234 rva=0xa1234
KRP: t5 raw[1] 0x0 not-code
...
KRP: t5 iface as table, 64 entries
KRP: t5 iface[0] 0x9f1a1234 rva=0xa1234
...
KRP: t5 cand[0] table=0x9f1a1234 rva=0xa1234 entries=64
KRP: t5 cand[0][0] 0x9f1b0000 rva=0xb0000
...
KRP: t5 cand[1] 0x0 not-code, skipped

KRP: eod guid=... status=Success (0x0) iface=0x9f123450
...
```

读法：如果 `iface` 本身就是函数表，`iface[i]` 应是一串 `rva=`；如果 `raw[0]`
和 `iface[0]` 相同且是 `rva=`，说明第一层是**指针**，真正的函数表在 `cand[0]`
里；如果 `raw[0]` 是 `not-code`，它指向镜像外（MMIO、另一个 image，或根本不是
指针）。旧探针把 interface 直接当表读，这层间接就被淹没了。

`eod` 发生在 BDS 连接控制台之前，可能根本到不了屏/串口；`t5` 在 5 秒后通常会。
只看到 `t5` 不代表 `eod` 没执行，只代表那次输出没有可用的控制台。

### 4. 固件集成（不在本仓库）

固件源码来自 `board.toml` 的 `{uefi}`（Mu-Silicium），不在设备仓库里。要让
`KRP:` 和 `KFLG` 真的进镜像，固件树必须做四件事：

1. 拿到两个目录和公共头文件：
   `Silicon/Silicium/SiliciumPkg/Drivers/KebabFwLogDxe/`、
   `Silicon/Silicium/SiliciumPkg/Drivers/KebabRpmhProbeDxe/`，以及
   `Silicon/Silicium/SiliciumPkg/Include/KebabFwLog.h`（串口库用尖括号包含它）；
2. 在 `kebab.dsc` 的 `[Components]` 里列出两个 `.inf`；
3. 在 `kebab.fdf` 的 `[FV.FvMain]` 里把两个模块放进 FV；
4. 构建后 `strings Build/kebabPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd | grep -E
   "KRP:|KFLG"`；搜不到就说明驱动不在镜像里，任何日志都不会包含它们。

写这份说明时检查到的 Mu-Silicium 检出（`/home/albert/Codex/UEFI/Mu-Silicium`，
不在本仓库）已经有一部分集成：`KebabFwLogDxe` 的目录、头文件、`.dsc` 和 `.fdf`
条目都在，`FrameBufferSerialPortLib.c` 也已经在渲染前调用
`KebabFwLogAppend()`；但探针的目录里还是旧源码，`.dsc`/`.fdf` 里**没有**探针，
所以当前构建不会包含 `KRP:`。从干净 `{uefi}` 开始时四步都要做；用这个检出时
只需补探针的两行，并用本仓库的 `KebabRpmhProbeDxe.c/.inf` 覆盖旧文件。

`FvMain` 是 3 MiB（`FD_BASE=0x9fc00000`、`FD_SIZE=0x300000`）。两个模块都很小；
如果 FV 溢出，要同时改 `FD_SIZE`/`FD_BLOCKS` 和 boot 头，不能让
`repack-firmware.py` 硬塞一个更大的 FV。

完整的集成说明、环布局、输出读法和“哪些没编译”的边界写在
`firmware/README.md`（中文）。

### 5. 验证

* **补丁系列**：`scripts/verify-all.sh` 在 pristine `1b191cb`
  （xnu-8796.141.3）上按文件名顺序应用，结果
  `108/108 patches apply in filename order to 1b191cb (xnu-8796.141.3)`。
  0129 的 diff 以当前 `ios16`（含 0128）为基线生成；0128 只改
  `bsd/dev/kebab_spmi.c`，与 0129 改的四个文件不重叠，所以顺序应用无冲突。
* **内核构建**：macOS 14.6.1 构建机（Xcode 15.4、KDK 13.5），
  `MACHINE_CONFIG=KEBAB`、`KERNEL_CONFIG=DEVELOPMENT`，产物
  `build/xnu.obj/DEVELOPMENT_ARM64_KEBAB/kernel.development.kebab`，
  17,169,928 字节，sha256
  `544265fae709a920ef0cb6554c22caa0215fdf9ff010a66d76fcfc9bbdbd2a42`，0 error。
* **符号与字符串**：`nm` 能看到
  `_kebab_fwlog_attach`、`_kebab_fwlog_report`、`_kebab_fwlog_boot_summary`
  （`__TEXT_EXEC,__text`）；`strings` 能看到 `FWLOG?`、`FWLOG? all`、
  `KEBAB FWLOG: ring %u bytes, written %llu bytes`、`no 'KFLG' magic ...` 等
  全部新增格式串。
* **固件源码语法检查（不是构建）**：三个 `.c`（`KebabFwLog.c`、
  `KebabFwLogDxe.c`、`KebabRpmhProbeDxe.c`）用 Mu_Basecore 的
  MdePkg/MdeModulePkg 头、aarch64 target 跑过
  `clang -fsyntax-only -Wall -Wextra`，全部通过；没有链接，没有 `.dsc`/`.fdf`
  的 EDK2 构建。
* **未验证**：没有产出 `SILICIUM_UEFI.fd`，没有刷机；环在真机上的内容、`head`
  回绕、RPMh 协议的真实地址和表布局，都要等固件构建、刷入并在内核里执行
  `FWLOG?` 后才能确认。

### 6. 状态

| 项 | 判定 |
| --- | --- |
| 0129 在 `1b191cb` 上随全集应用 | 108/108，PASS |
| 内核编译 | 通过，0 error |
| `kebab_fwlog_*` 符号 / 新增字符串 | 已确认 |
| 固件驱动源码与集成说明 | 已写入 `firmware/` |
| 固件源码单文件语法检查 | 通过（非 EDK2 构建） |
| 固件 EDK2 构建与 `KRP:`/`KFLG` 字符串 | **未做**，需要完整构建 |
| 真机 `FWLOG?` / RPMh 探针输出 | **未做**，需要刷机 |

## 六十、VRM 上电命令

> 目标：让 QUPv3 SE13 上 I2C `0x4b` 的 Synaptics S3908 可被探测。它由两颗
> RPMh 拥有的 LDO 供电：`pm8150_l13`（ldoa13，SID1 `0x4c00`）与
> `pm8150a_l1`（ldoc1，SID5 `0x4000`）。两颗的 `write_ee` 都是 `0x4`，EE0
> 不能走 SPMI 写，只能按 RPMh VREG 的方式让 RSC 的代发通道（TCS）发一条
> ENABLE。本节记录三件事：后端到底还剩什么、补丁 0130 补了什么、真机每一步
> 量到的寄存器值与目标是否应答。

### 1. 后端核查：0128 之后内核里没有写路径

先把“树上已有什么”查清楚，结论是**生产镜像里没有任何 TCS 写路径**：

* `0127` 在 `bsd/dev/kebab_spmi.c` 里加过一版 `#if KEBAB_RPMH_WRITES` 草稿
  （`kebab_rpmh_tcs_wr_wait()`、`kebab_rpmh_tcs_send()`），编译期关闭。
* `0128` 把整块草稿删掉，只留下注释“状态机在 `package/KebabRpmhVrm.c`，
  MMIO 端口等矩阵通过后再做”，并把 `KEBAB_RPMH_WRITES` 一起删除。
* 在打完 108 个补丁的 xnu 树里 grep `kebab_tcs_io`、`kebab_rpmh_tcs_send`、
  `kebab_rpmh_encode_enable`、`KEBAB_RPMH_WRITES`，唯一命中就是那条注释
  （`bsd/dev/kebab_spmi.c:996`）。没有 `kebab_tcs_io` 实现，没有调用者。
* `bsd/conf/files` 只登记 `kebab_ufs/usb/fwlog/spmi/touch`；`package/` 下的
  任何模块都不进内核构建（本仓库没有这种机制），`package/KebabRpmhVrm.c`
  只被两台主机测试驱动。

也就是说：**只剩测试注入**（`KebabRpmhTcsHostTests.c` 里的假寄存器文件），
真机上没有任何命令能碰到 TCS。要补的是“状态机的内核端口”，不是“再写一版
状态机”。

### 2. 补丁 0130：把测过的状态机编进内核，并补上 MMIO 绑定

`KebabRpmhVrm.h` 本来就是为这个端口写的（`KEBAB_RPMH_KERNEL` 选内核头文件，
`kebab_rpmh_tcs_init_global()` 只给内核用），所以 0130 不再手抄一版：

* 新增 `bsd/dev/kebab_rpmh_vrm.{c,h}`，由 `scripts/sync-rpmh-vrm.py` 从
  `package/KebabRpmhVrm.{c,h}` 生成：除横幅、`#define KEBAB_RPMH_KERNEL 1`
  和头文件名外**逐字节相同**，脚本的 `--check` 用来证明这一点。这样内核跑的
  就是 70 条主机断言驱动的那份代码，不会出现“第二版草稿慢慢漂移”。
* `bsd/conf/files` 登记 `bsd/dev/kebab_rpmh_vrm.c standard`。
* `bsd/dev/kebab_spmi.c` 补上 `kebab_tcs_io` 的生产绑定，几何全部取自 0127
  的实测而非记忆：

| 项 | 值 |
| --- | --- |
| drv-2 窗口 | `0x18220000`，映射 `0x10000` |
| tcs_base | `drv + 0xd00` = `0x18220d00` |
| TCS 间距 | 672 字节 |
| TCS1 | `tcs_base + 1*672` = `0x18220fa0` |
| 用到的偏移 | `0x10` CMD_WAIT、`0x14` CONTROL、`0x18` STATUS、`0x1c` CMD_ENABLE、`0x30/0x34/0x38/0x3c` 槽、`0x40` RESP_DATA |
| IRQ 三件套 | `tcs_base + 0x0/0x4/0x8`（0127 修正后的位置） |

  io 只做三件事：`kebab_spmi_rd/wr()` 加 `mach_absolute_time()` 换算的微秒
  时钟；任何越过一个 TCS 窗口（`off + 4 > 672`）的访问直接返回失败，坏偏移
  不可能读到隔壁 TCS。窗口在 `kebab_spmi_init()` 里映射一次（控制台命令可能
  从 panic 路径跑，那里不能再 `ml_io_map`）。

* **一次性门限从“每次启动一条”改成“每条命令一次”**：两颗 LDO 是两个
  ENABLE 寄存器，控制台要分两步走，原来的 `dead=1` 会把第二步挡死。现在用
  `used_mask`（每对 (资源, 寄存器) 一位）记录：ldoa13 ENABLE 是 `0x1`，
  ldoc1 ENABLE 是 `0x4`，同一颗第二次发送仍然被拒；任何失败仍然把整条通道
  标死（`dead=1`），失败之后连另一颗也不许发。主机测试从 58 条增到 70 条，
  新增 C23（两颗按顺序各发一次，重复被拒）与 C24（第一颗失败后第二颗被拒）。

### 3. 命令与启动参数

| 命令 | 作用 | 备注 |
| --- | --- | --- |
| `VROMON` | 只给 ldoa13 发 ENABLE | 一次启动只允许一次；重复会打印“…already attempted this boot…” |
| `VRMON ldoc1` | 只给 ldoc1 发 ENABLE | 要求 ldoa13 已经 COMPLETE，否则拒绝并提示先 `VROMON` + `PWR?` |
| `TCS?` | 只读转储 TCS0/TCS1 的 CONTROL、CMD_ENABLE、CMD_WAIT、CMD_STATUS 与 IRQ 三件套 | 不写、不清、不恢复 |
| `PWR?` | 走 SPMI observer 读两颗 LDO 的 `EN`（`base+0x46`） | 0122 已有，本次未改 |

* 启动参数 **`kebab_vrm_write`**，默认 `0`；只有 `kebab_vrm_write=1` 时
  `kebab_spmi_init()` 才映射 TCS1 窗口，`VROMON` 才可能发出命令。未设置时
  命令只打印 `kebab_vrm_write is off; refusing to send ldoa13`，无人值守的
  启动永远不会给任何一颗供电。
* 命令必须在**一次 bulk-OUT 完成**内到达，匹配方式与 `SPMI?`/`PON?`/`FWLOG?`
  完全一致；KDP 接管字节流时按首字节锚定。
* 白名单之外的形式（如 `VRMON ldoa12`）不会静默忽略，会打印白名单并声明
  没有发送；`VROMON` 只认识 `ldoa13`（裸命令）与 `ldoc1` 两种。
* 编码器与白名单本身没有改：`kebab_rpmh_encode_enable()` 只从自己的表里取
  `{"ldoa13", 0x00040c00}`、`{"ldoc1", 0x00042f00}`，`kebab_rpmh_cmd_allowed()`
  只放行 `WRITE_ACK` 到对应 ENABLE 寄存器（`data == 1`）。

### 4. 真机结果

镜像 `XNU-vrm.img`（0130 + `kebab_vrm_write=1` + `kebab_touch=1`），`boot_b`。
下列是同一套步骤的两次完整记录（第一次 02:2x、第二次重启后重跑），两次
数值一致；下面贴第二次的原文。

**第一步：`TCS?`（任何写之前）与 `PWR?` 基线**

```text
KEBAB TCS: === read-only TCS dump, APPS RSC drv-2 ===
KEBAB TCS: drv-2 0x18220000, tcs_base = drv + 0xd00 = 0xfffffe8ff76ccd00, stride 672
KEBAB TCS: rsc irq: enable[tcs_base+0x0] 0x00000001 status[+0x4] 0x00000000 clear[+0x8] 0x00000000
KEBAB TCS: tcs 0 at 0xfffffe8ff76ccd00: CONTROL[+0x14] 0x01010000 (AMC_ENABLE 1, AMC_TRIGGER 1) CMD_ENABLE[+0x1c] 0x00000003 CMD_WAIT[+0x10] 0x00000000 CMD_STATUS[+0x3c] 0x00010101 (ISSUED 1, COMPL 1)
KEBAB TCS: tcs 1 at 0xfffffe8ff76ccfa0: CONTROL[+0x14] 0x00010000 (AMC_ENABLE 1, AMC_TRIGGER 0) CMD_ENABLE[+0x1c] 0x00000000 CMD_WAIT[+0x10] 0x00000000 CMD_STATUS[+0x3c] 0x00000000 (ISSUED 0, COMPL 0)
KEBAB TCS: VERDICT: TCS1 is idle and clean; a kernel-side send is plausible
KEBAB TCS: done; no register was written, cleared or recovered

KEBAB SPMI: PWR: pm8150_l13: APID 0x0c9 write_ee 0x4 irq_owner 1
KEBAB SPMI: PWR: pm8150_l13: type 0x04 subtype 0x7b
KEBAB SPMI: PWR: pm8150_l13: enable 0x00 (bit7 0), range 0xc0, set 0x0b
KEBAB SPMI: PWR: pm8150a_l1: APID 0x11b write_ee 0x4 irq_owner 1
KEBAB SPMI: PWR: pm8150a_l1: type 0x04 subtype 0x71
KEBAB SPMI: PWR: pm8150a_l1: enable 0x00 (bit7 0), range 0x08, set 0x07
```

`TCS0` 是**别的执行环境留下的残局**（`AMC_TRIGGER=1`、`CMD_ENABLE=0x3`、
`CMD_STATUS=0x00010101`），与 0127 当时的判断一致；`TCS1` 干净。`TCS?` 只看
不碰，也不清理 TCS0——它可能承载别的子系统的资源票。

**第二步：`VROMON`（ldoa13），然后 `PWR?`**

```text
KEBAB VRM: encode ldoa13 ENABLE -> OK msgid 0x00010108 addr 0x00040c04 data 0x00000001
KEBAB VRM: whitelist: WRITE_ACK to the ENABLE register of ldoa13 only, slot 0x1
KEBAB VRM: send ldoa13 -> OK (0)
KEBAB VRM: phase COMPLETE, CMD_STATUS 0x00010101 (ISSUED 1, COMPL 1)
KEBAB VRM: ldoa13 ENABLE acknowledged by RPMh; confirm the rail with PWR? before the next step

KEBAB SPMI: PWR: pm8150_l13: enable 0x80 (bit7 1), range 0xc0, set 0x0b
KEBAB SPMI: PWR: pm8150a_l1: enable 0x00 (bit7 0), range 0x08, set 0x07
```

**第三步：`VROMON ldoc1`，然后 `PWR?`**

```text
KEBAB VRM: encode ldoc1 ENABLE -> OK msgid 0x00010108 addr 0x00042f04 data 0x00000001
KEBAB VRM: whitelist: WRITE_ACK to the ENABLE register of ldoc1 only, slot 0x4
KEBAB VRM: send ldoc1 -> OK (0)
KEBAB VRM: phase COMPLETE, CMD_STATUS 0x00010101 (ISSUED 1, COMPL 1)
KEBAB VRM: ldoc1 ENABLE acknowledged by RPMh; confirm the rail with PWR? before the next step

KEBAB SPMI: PWR: pm8150_l13: enable 0x80 (bit7 1), range 0xc0, set 0x0b
KEBAB SPMI: PWR: pm8150a_l1: enable 0x80 (bit7 1), range 0x08, set 0x07
```

汇总：

| 步骤 | `pm8150_l13` EN | `pm8150a_l1` EN |
| --- | --- | --- |
| 写之前 | `0x00` | `0x00` |
| `VROMON` 之后 | **`0x80`** | `0x00` |
| `VROMON ldoc1` 之后 | `0x80` | **`0x80`** |

两颗都按预期变化，且每次只变一颗：命令确实是一步一颗，`PWR?` 的位置就是
两步之间的验证点。

**第四步：触摸传输**

`TOUCHCLK` → `TOUCHSE` → `TOUCHPING`（这条树里现成的传输命令，目标就是
`0x4b`）：

```text
KEBAB TOUCH: ping 0x4b: one byte, FIFO path
KEBAB TOUCH: ping 0x4b: rc 0 GENI_STATUS 0x00000000
KEBAB TOUCH: ping: ACK, one-byte write completed
```

上电之前同一条一字节写在 `0x4b` 上是以 `state 0x21` + `ABORT` 结束、且没有
NACK 的；现在 `rc 0`、`GENI_STATUS` 全零、打印 `ACK`。**ABORT 变成了 ACK**，
说明目标已经供电并应答。

### 5. 拒绝路径与运维注意

* 重复 `VROMON`：`KEBAB VRM: ldoa13 was already attempted this boot (it
  completed); not retried`。
* 白名单之外：`KEBAB VRM: (no argument) is not a rail this command may power;
  the whitelist is ldoa13 (VROMON) and ldoc1 (VROMON ldoc1), nothing sent`。
  括号里的 `(no argument)` 是控制台匹配层传给驱动的哨兵值（它只告诉驱动
  “这条不在白名单里”，不传用户原文），白名单本身就在同一行里。
* **`FASTBOOT` 需要 `kebab_pon_write=1`**：本次第一版镜像只带了
  `kebab_vrm_write=1`，重启后想再刷机时内核回答
  `KEBAB FASTBOOT: disabled because kebab_pon_write=0; use REBOOT for a plain
  restart`，于是这块板子只能靠手动进 fastboot 才能换镜像。这条与 VRM 无关，
  是既有设计的连带后果；下一次做镜像时把 `kebab_pon_write=1` 一起写进 toml。
* 本节所有写操作都发生在 `kebab_vrm_write=1` 的镜像上；默认镜像里
  `VROMON` 只打印拒绝，`PWR?`/`TCS?`/`RPMH?` 仍然可用。

### 6. 如果通道被固件占住（本次未发生）与固件侧后备

`TCS?` 的意义是把“通道坏了”和“电没上来”分开。判读规则：全零 = 空闲可发；
`AMC_TRIGGER=1` 或 `CMD_ENABLE!=0` = 有命令在飞或卡住；`COMPL` 已置位 =
脏通道（可能是别人留下的完成位），状态机会拒绝而不是把陈旧完成当成自己的。
本次 `TCS1` 全零，所以内核侧发送是可行的；`TCS0` 的残局属于别的 EE，没有
清理。

如果将来 `TCS1` 也是卡死/脏的，正确处理是：报告寄存器值、判定这两颗轨从
内核侧不可达、**停在那里**，不要换地址重试、不要复位 RSC、更不要清别人的
TCS（它可能承载另一个子系统的资源票）。

那条路上的后备在固件侧：在 `ExitBootServices` 之前用 `RpmhDxe` 协议发 VRM
请求。该协议的函数表已经拿到：entry `[4]` = `0x1634`（commit），entry
`[1]` = `0x15A4`（flush）。本节只把这个方向记下来，不启动它——内核侧的路
已经通了，没有必要再动固件。


## 六十一、触摸自动上电与芯片识别

> 目标：让触摸驱动自己在 bring-up 时点亮它自己的两路电源，并在复位之后真正读出
> 芯片的身份，而不是继续假设。本节记录：启动参数、顺序与顺序的理由、上电前后的
> 真机输出、芯片回的身份字节，以及它到底在 application 还是 bootloader 模式。

### 1. 启动参数与门限

| 参数 | 默认 | 作用 |
| --- | --- | --- |
| `kebab_touch_power` | `0` | 让 `kebab_touch_start()` 在**任何 I2C 传输之前**给 `ldoa13` 与 `ldoc1` 上电 |

* 它**不是**替代 `kebab_vrm_write`，而是在它之上再加一道门：`kebab_vrm_write=0`
  时 `kebab_spmi_vrm_boot_power()` 直接打印拒绝并返回失败，没有人值守的启动
  永远不会给任何一颗 LDO 发命令。
* 两颗 LDO 仍然各只有一次机会（`vrm.ldoa13_done` / `vrm.ldoc1_done`）。开机时
  已经上过电的那一颗，控制台再 `VRMON` 会被拒；反过来，开机时读到 `EN=0x80`
  而跳过的轨道**不**占用这次机会（`done` 保持 `false`），只是把 `ldoa13_ok`
  置真，好让第二步 `ldoc1` 的前置条件成立。
* 传输、复位、识别仍然要 `kebab_touch_boot=1`；只给 `kebab_touch_power=1` 时
  驱动只上电、然后停在"hardware untouched"，方便先用 `PWR?` 验证电源。

### 2. 顺序，以及为什么是这个顺序

```
kebab_touch_start()
  ├─ 3 s 控制台窗口（原有，给主机打开 CDC-ACM 的时间）
  ├─ kebab_spmi_vrm_boot_power()          ← 本补丁新增
  │    ├─ 用 PWR? 的同一条 SPMI observer 路径读 EN
  │    ├─ EN == 0x80 → 跳过这一颗
  │    ├─ 编码并发送 ldoa13 的 ENABLE，等 TCS 完成，回读 EN
  │    ├─ 再对 ldoc1 做同样的一步
  │    └─ 任一步失败 → 返回 -1，通道按模块既有规则关闭
  ├─ kt_sleep_ms(15)                       ← 第二颗之后 10–20 ms
  ├─ TOUCHCLK（使能 SE13 的 S5 分支时钟）   ← 本次实测补上的必要一步
  ├─ 引脚 mux、复位脉冲（原有）
  ├─ kt_sleep_ms(20)                       ← 复位释放之后再等一次
  └─ se_init → identify → boot info
```

为什么必须先电后复位：两颗 LDO 的 ENABLE 是 RPMh 的 VREG 寄存器，每次重启都回到
`0x00`；而没有供电的 SE13 目标**不会 NACK**，它让传输以 `GENI_STATUS` 的
`state 0x21` + `ABORT` 结束。所以上电必须在第一个字节之前，而不是在失败之后。

为什么时钟要单独补：0116 当时就量到"第一次写 GENI 寄存器之后控制台不再回来"，
那一次和这一次都停在同一行 `se_init: force default, output ctrl`。控制台上
`TOUCHCLK` → `TOUCHSE` 一直是能跑通的，差别就是时钟。把这一步放进启动路径之后，
`se_init` 一次通过，识别命令才真正发到 `0x4b`。这一条不是"顺手加的"：没有它，
自动上电只是把电加上去，永远走不到芯片。

### 3. 真机输出

镜像：0132 + `kebab_touch=1 kebab_touch_boot=1 kebab_touch_power=1
kebab_vrm_write=1 kebab_pon_write=1`，`boot_b`。

**上电之前**（`PWR?`，与 0130 记录一致）：两颗都是 `enable 0x00`；同一条一字节
写在 `0x4b` 上以 `state 0x21` 结束——这是"上电前"的基线。

**上电（本次实测，原文）**：

```text
KEBAB TOUCH: console window over, starting MMIO
KEBAB VRM: === boot power: touch rails, one ENABLE each ===
KEBAB VRM: encode ldoa13 ENABLE -> OK msgid 0x00010108 addr 0x00040c04 data 0x00000001
KEBAB VRM: whitelist: WRITE_ACK to the ENABLE register of ldoa13 only, slot 0x1
KEBAB VRM: send ldoa13 -> OK (0)
KEBAB VRM: phase COMPLETE, CMD_STATUS 0x00010101 (ISSUED 1, COMPL 1)
KEBAB VRM: boot power: ldoa13 COMPL, phase COMPLETE, CMD_STATUS 0x00010101 (ISSUED 1, COMPL 1), EN 0x00 -> 0x80
KEBAB VRM: encode ldoc1 ENABLE -> OK msgid 0x00010108 addr 0x00042f04 data 0x00000001
KEBAB VRM: whitelist: WRITE_ACK to the ENABLE register of ldoc1 only, slot 0x4
KEBAB VRM: send ldoc1 -> OK (0)
KEBAB VRM: phase COMPLETE, CMD_STATUS 0x00010101 (ISSUED 1, COMPL 1)
KEBAB VRM: boot power: ldoc1 COMPL, phase COMPLETE, CMD_STATUS 0x00010101 (ISSUED 1, COMPL 1), EN 0x00 -> 0x80
KEBAB VRM: boot power: ldoa13 and ldoc1 both read 0x80
KEBAB TOUCH: both rails up, waited 15 ms before the reset
```

**时钟、复位、SE 初始化**：

```text
KEBAB TOUCH: TOUCHCLK: 0x52008 0x063c83c0 -> 0x0e3c83c0
KEBAB TOUCH: TOUCHCLK: 0x52008 now 0x0e3c83c0 (s5 1), halt 0x00004220
KEBAB TOUCH: reset pulse (mode 1), ctl 0x00000200 io 0x00000003
KEBAB TOUCH: reset pulse done, waited 20 ms
KEBAB TOUCH: se_init: clear IRQs
KEBAB TOUCH: se_init: force default, output ctrl
KEBAB TOUCH: se_init: packing + byte granularity
KEBAB TOUCH: se_init: clock select + SCL counters
KEBAB TOUCH: TCM identify begin
```

上电之前这一段是停不住的：写 `GENI_OUTPUT_CTRL` 之后控制台就没了。现在它继续走
到识别。

**身份读取（原文）**：

```text
KEBAB TOUCH: TCM command 0x02, 0 payload byte(s)
KEBAB TOUCH: command 0x02: trailing byte 0x00, vendor padding is 0x5a
KEBAB TOUCH: identify: message 0x10, 24 payload byte(s)
KEBAB TOUCH: identify payload[0] a5 03 01 01 73 33 39 30 38 2d 31 35 2e 30 2e 30
KEBAB TOUCH: identify payload[16] 00 00 00 00 b9 ba 3c 00
KEBAB TOUCH: identify: part "s3908-15.0.0...." at payload offset 4
KEBAB TOUCH: identify: version 0x01 mode 0x01 (application), the two bytes in front of the part number
KEBAB TOUCH: identify: 2 byte(s) in front of those are left uninterpreted
KEBAB TOUCH: identify prefix[0] a5 03
KEBAB TOUCH: identify: build 0x003cbab9
KEBAB TOUCH: boot info: message 0x0e, 0 payload byte(s)
KEBAB TOUCH: boot info: code 0x0e (not implemented), 0 payload byte(s): this firmware does not report boot status
KEBAB TOUCH: identity: APPLICATION MODE (mode 0x01), the touch firmware is running
```

### 4. 身份字节与判定

identify 的 24 字节 payload：

| 偏移 | 字节 | 含义 |
| --- | --- | --- |
| 0–1 | `a5 03` | 固件在结构前多带的两个字节，**不解释**，原样打印 |
| 2 | `01` | version |
| 3 | `01` | **mode = MODE_APPLICATION** |
| 4–19 | `73 33 39 30 38 2d 31 35 2e 30 2e 30 00 00 00 00` | `"s3908-15.0.0"`，NUL 补齐 |
| 20–23 | `b9 ba 3c 00` | build（packrat）`0x003cbab9` |

判定方法刻意不是"按下标取"：part number 是这份 payload 里唯一位置自明的字段
（可打印 ASCII），而厂商 `syna_tcm_identification` 里 version/mode 正好在它前面
两个字节。所以先找 part 串，再取它前面两字节，并把更前面的字节原样打印成
"未解释前缀"。这样固件多带两个字节时，part 名不会被整体错位，模式也不会被读成
`0xa5`。

**结论：application mode。** 旁证是 `GET_BOOT_INFO`（0x10）回 `0x0e`
`not implemented`——boot info 是 bootloader 才会答的命令，application 固件不实现
它。也就是说这颗芯片**不需要下载固件**，它已经在跑触摸固件（15.0.0）。

反过来说，如果 mode 不是 `0x01`（`0x02` host download、`0x0b`/`0x0c`
bootloader、`0x0e` production test），驱动会打印
`touch unavailable: ... needs a firmware download, which this driver does not
implement` 并 `return -1`：不建线程、不建设备节点、不假装已经能出数据。
0120 里那一步"发 `CMD_RUN_APPLICATION_FIRMWARE` 再重新 identify"已经删掉，因为
那是一个本驱动从未验证过的模式切换。

### 5. 失败路径

* 读 EN 失败、编码/白名单拒绝、TCS 没有 COMPLETE、回读不是 `0x80`：每一种都
  打印是哪一颗、哪一步、`phase` 与 `CMD_STATUS`，然后
  `touch unavailable: a rail did not come up, so no transfer is attempted; the
  boot continues without touch`，`kebab_touch_start()` 返回 `-1`。
* `kebab_touch_start()` 由 `void` 改为 `int`，`bsd_init()` 仍然忽略返回值：
  这条路径不 panic、不重试、不循环。失败的命令本身已经把通道关掉，重试正是
  那道一次性门限要防的事。
* 识别阶段失败同样只影响触摸：没有 TCM 应答，或 mode 不是 application，都
  只打印 verdict 并返回，启动继续。

### 6. 边界

* **没有加 HID**：事件路径一行没动，`/dev/kebab_touch` 的记录格式与队列不变。
* 这颗固件的报文尾部不是厂商的 `0x5a` padding，`GET_APP_INFO`（0x20）与
  `SET_TOUCH_REPORT_CONFIG`（0x26）会因此错位：本次输出里
  `command 0x20: payload 23040 too large` 与 `report config answered 0x0d` 就是
  这个原因。识别与模式判定不受影响。既然没有解析出报文的布局，驱动就不启动
  轮询线程：这一次先看到的是相反的情况——线程起来了，每 200 ms 打一条
  `report padding 0xa5, not 0x5a`，把这唯一的可读通道刷掉——所以最终版本把
  "有没有解码器"作为起线程的条件。事件路径本身保持原样。
* `launchd` 在启动末尾仍然会 panic（`unexpected SIGKILL of launchd (CS_KILLED)`），
  那是 AMFI/签名那条线的问题，发生在触摸驱动返回之后，与本补丁无关。

## 六十二、用户态安装通道

> 目标：把 3.3 GB 的用户态放到手机的可写根卷上。这一步只造通道，不消费它：
> `/sbin/launchd`、`/usr/lib/dyld`、44 个拆分 dyld 缓存、静态信任缓存和一组
> `LaunchDaemons` 都要落到 `KebabRoot`，而现实是——64 MiB 的 payload 卷装不下，
> 主机也没有通往 HFS+ 卷的块级路径。内核手上只有三样东西：可用的 UFS 块访问、
> 可读写的 HFS+ 根、以及一条已经在跑命令的 USB CDC-ACM bulk-OUT。

这一节记录五件事：流的格式、控制台协议与它的流控、允许清单、恢复规则、实测
吞吐，最后是"真正装进 3.3 GB 之前还差什么"。

### 1. 主机侧打包器 `scripts/pack-userland.py`

输入是一棵目录树（`--root`）或一份显式的"源路径 → 卷内目的路径"清单
（`--spec` 文件，每行 `SRC<TAB>DEST[<TAB>MODE]`；`--map SRC:DEST` 是同一件事的
单条形式）。输出是**一个**自描述流文件，格式叫 KIST v1，全部小端：

```
流头 32 字节
  0   4  magic 'KIST'        4   2  version 1
  6   2  flags（bit0：流里有 deflate 条目）
  8   4  entry_count         12  4  header_size = 32
  16  8  total_usize         24  8  total_wire（流头之后的全部字节，含填充）
条目头 192 字节 + 载荷，重复 entry_count 次
  0   4  magic 'KENT'        4   2  path_len（1..127）
  6   2  mode                8   2  flags（bit0：载荷是 zlib 流）
  10  2  reserved            12  8  wire_size（线上字节）
  20  8  file_size           28  32 SHA-256（**未压缩**内容的哈希）
  60  128 path（UTF-8）      188 4  reserved
尾部：0 填充到 --chunk（默认 65536）的整数倍
```

三个设计约束直接来自传输本身：

* **接收端不能 seek。** 解析器只向前走：先把 32 字节流头读完，再一条条读
  192 字节条目头，然后按 `wire_size` 吃掉载荷。文件多长、下一个条目是什么，
  都不需要预先知道。填充保证最后一个 bulk-OUT 传输也是满缓冲区，否则
  "正好 512 的整数倍" 的短包不会让 TRB 完成。
* **条目可以整条跳过。** 条目头自带 `file_size` 和 SHA-256，接收端先算目的
  文件的这两个值；一致就打印 `SKIP` 并把载荷丢掉（见第 5 节）。
* **压缩是可选的、按条目的。** `--compress none|zlib|auto`：`auto`（默认）
  先压一遍，只有省下至少 `--min-gain`（默认 12.5%）才保留 deflate，否则原样
  存。实测 64 MiB 的 `.01` 分片压到 **35.1%**（100 MB/s 左右），`launchd`
  36.3%、`dyld` 33.8%——缓存分片并不像预想的那样"内部已经压过"，所以这条
  链路值得压。内核侧按条目增量 inflate，不需要把整条载荷放进内存。

`--verify` 只读流文件本身，不碰源树：重算每条载荷的 SHA-256（deflate 条目
先解压）、核对声明的总数和填充字节，逐条打印 `OK/FAIL`。

`--write-manifest` 输出 `path<TAB>size<TAB>sha256`，给下面的 `--probe` 用。

### 2. 内核侧安装器：`bsd/dev/kebab_install.c`（补丁 0131）

新增 `bsd/dev/kebab_install.{c,h}`，在 `bsd/conf/files` 里登记。它只做流状态机
和文件系统，**不碰控制器**：DMA 缓冲区和 ACK 行仍然由 `kebab_usb.c` 独占。
装一个条目要做的事：

1. 校验条目头：magic、`path_len`、路径规则（见第 4 节）、`wire_size` 不超过
   流里剩余字节、store 条目必须 `wire_size == file_size`。
2. **恢复检查**：目的路径存在、大小等于 `file_size`、SHA-256 等于条目头，
   就打印 `SKIP <path> <size>` 并丢弃载荷。
3. **空间检查**：`file_size > 空闲字节 + 旧文件占用的字节` 就拒绝这一条并打印
   两个数字。旧文件会被 `O_TRUNC` 释放，所以覆盖安装只会算差额；这一步是为了
   不把"卷满了"伪装成"传输失败"。
4. 逐级 `namei(CREATE)+vn_create` 建父目录（`vn_create` 对 `VDIR` 走的就是
   `mkdir1at` 那条路），然后 `vnode_open(O_CREAT|O_TRUNC|FWRITE, mode)`。
   `vnode_open` 只在**新建**时用 cmode，所以再用 `vnode_setattr` 把 mode 落一次。
5. 载荷边到边写：store 条目直接 `vn_rdwr(UIO_WRITE, …)`；deflate 条目先喂
   `inflate()`，输出进 64 KiB 缓冲再写。两种都同时喂 SHA-256。
6. 写完关文件，**重新打开读一遍**再算一次 SHA-256：载荷哈希证明的是链路，
   回读哈希证明的是卷。一致才打印 `OK <path> <size> mode <octal>`。

**fail soft**：短读、写失败、哈希不符、inflate 出错、块比流剩下的还长、60 秒
没有新块——任何一条都打印 `FAIL …` 或 `INST ABORT …`、结束本次流、把已经写下去
的东西留在原地，然后把控制台还给命令模式。这里没有 `panic`，也没有一处相信
线上的长度而不做上界检查。

### 3. 控制台协议与流控

命令族沿用 `FWLOG?` / `SPMI?` 的匹配方式（一次 bulk-OUT 传输里匹配完整命令），
但 INST 的参数必须从**同一次**传输里读出来，所以它有自己的扫描器
`ud_inst_command()`，两个匹配分支（`kebabkdp=0` 的扫描分支和 `kebabkdp=1` 的
锚定分支）都要调用它——这个坑真的踩到了：第一次刷进去的镜像只挂在前一个分支，
`kebabkdp=1` 一开，`INST?` 就完全没反应，而 `IMEM?`/`PSCI?` 照常工作。

| 命令 | 作用 |
| --- | --- |
| `INST BEGIN <wire_bytes>\n` | 接下整条流，写文件 |
| `INST DRY <wire_bytes>\n` | 接下整条流，只校验和打印计划，不建目录、不写一个字节 |
| `INST?\n` | 门禁、是否在跑、根卷 ro/rw、块大小、进度、空闲空间 |
| `INST CHECK <path>\n` | 目的路径当前的大小和 SHA-256（或 `MISSING`） |

一次传输一块，块大小 `KEBAB_INST_CHUNK = 65536`：

```
主机                                   设备
INST BEGIN 1507328\n   ───────────────▶  取流、校验流头
                       ◀───────────────  INST BEGIN 1507328 write chunk 65536
                       ◀───────────────  INST STREAM v1 entries 2 bytes 1464512 wire 1507328
<65536 字节>           ───────────────▶  写/校验这一块
                       ◀───────────────  INST ACK 65536
<65536 字节>           ───────────────▶  …
                       ◀───────────────  OK/SKIP/PLAN 每条一行
                       ◀───────────────  INST ACK 1507328
                       ◀───────────────  INST DONE entries 2 ok 2 skipped 0 bytes … wire … ms 183
```

流控就是"发一块、等 `INST ACK <n>`、再发下一块"，`n` 是**累计**字节数：

* ACK 在字节真正写完或丢掉之后才打印，所以主机永远不会跑到设备前面；
* 设备侧一次只挂一个 TRB，未挂的时候控制器直接 NAK，主机想抢跑也写不进来；
* 块还没处理完（`inst_pending`）以及"可能改变模式"的命令还在排队时，端点不
  重新挂载，下一次挂载一定用对的缓冲区和长度；
* 收流时 bulk-OUT 的字节**不进** 4096 字节的输入环、也不进命令匹配器，
  所以"不冲垮输入环"在这里是"根本不走那条路"，命令模式照旧喂环；
* 60 秒没有新块就 `INST TIMEOUT`，把控制台还回来，主机崩了不用整机重启。

DMA 池从一页 4 KiB 变成连续的 68 KiB（`UD_SIZE 0x11000`），新增
`UD_INST 0x1000` 的 64 KiB 块缓冲区。`kmem_alloc_contig` 之后逐页用
`pmap_find_phys` 验证物理连续，因为块路径用**一个** TRB 描述整块，跨页不连续
会让控制器按一个地址写进别人的页。命令模式仍然用原来的 512 字节
`UD_BULKOUT`；一次完成属于哪种模式，由**它挂载时用的缓冲区**（`ud.out_off`）
决定，而不是由当前模式决定——模式可以在传输在途时改变，按模式解释会把命令
读成载荷。

这套东西不是可选项：按实测 2.4 ms 的控制台往返，512 字节一块只有
**200 KB/s**，3.3 GB 要五个小时；64 KiB 一块把同一往返摊到 128 个包上，
实测 **10.6 MiB/s**（DRY），三分钟量级。

### 4. 允许清单与路径规则

目的路径必须是绝对的、可打印 ASCII、没有空组件、没有 `..` 组件、长度小于
128 字节，并且以这六个前缀之一开头：

```
/sbin/
/usr/lib/
/usr/standalone/
/System/Library/Caches/com.apple.dyld/
/System/Library/Caches/com.apple.kernelcaches/
/System/Library/LaunchDaemons/
```

不在清单里的条目在**建目录之前**就被拒绝（`FAIL <path> rejected`），整条流
随即结束。加上启动参数门禁 `kebab_install=1`（默认关闭），一次普通启动既不能
写文件，也不能建目录。`rootrw=1` 不是门禁的一部分，但没有它根卷是只读的，
`INST?` 会直接说明这一点。

### 5. 恢复规则

两级，都不需要重传已经对的内容：

* **流内**：条目开始时先算目的文件的大小和 SHA-256，与条目头一致就 `SKIP`，
  载荷照常收下但不写、不校验（目的已经是好的）。这一级保证"重发同一条流"是
  安全的，代价是载荷仍然占用链路。
* **流外**：`INST CHECK <path>` 逐条问设备现在的大小和哈希，
  `send-userland.py --probe plan.txt --skip-list have.txt` 把已经正确的路径写成
  跳过清单，`pack-userland.py --skip-list have.txt` 重新打包时**根本不把这些
  条目放进流**。这一级才是省带宽的那一级。

### 6. 实测吞吐（boot_b，`kebab_install=1 rootrw=1 kebab_pon_write=1`）

硬件：kebab（SM8250），控制台 COM7（CDC-ACM），块大小 65536 字节。

块大小不是随便定的。先用现有控制台量了一次往返：写一条命令、等到它的回应，
40 次的中位数是 **2.4 ms**。按 512 字节一块算，512 B / 2.4 ms = **200 KB/s**，
3.3 GB 要五个小时；一块 64 KiB 把同一次往返摊到 128 个包上，才有下面这些数字。

| 场景 | 数据量 | 结果 |
| --- | --- | --- |
| `INST?` | — | `gated 1 active 0 dry 0 root rw chunk 65536` |
| DRY，2 条目 | 1.4 MiB | `PLAN` 两条全对，**10.62 MiB/s**（11.13 MB/s） |
| 真写，`launchd`+`dyld` | 1.4 MiB | `OK` 两条、回读哈希一致，**7.84 MiB/s**，`INST DONE … ms 183` |
| 再发同一条流 | 1.4 MiB | `SKIP` 两条，`INST DONE … skipped 2`，10.96 MiB/s |
| 真写，592 MiB 的 `.symbols` | 592 MB | **失败：ENOSPC**，见第 7 节 |

7.84 MiB/s 意味着 3.3 GB（3,331,890,097 字节）大约 **7 分钟**——这条通道本身
不是瓶颈。

### 7. `KebabRoot` 只有 64 MiB：P1 的真正前置条件

这是这一轮更重要的结果，比吞吐重要得多。

592 MB 的分片写到 **64,028,672 字节** 时 `vn_rdwr` 返回 `ENOSPC`（错误 28）。
两次重试都停在同一个字节、同一段内容上（两次 `INST CHECK` 的哈希相同：
`ca8d64b1e6d88d28ee442d953be4ca1c700e0f37ddac9763dd994edbd90ca694`），说明
前 64 MB 写得完全正确，失败点是卷的容量，不是链路。紧接着往 `/usr/standalone/`
写一个 1 MiB 的小文件，立刻 `ENOSPC`——卷上已经没有空闲块。

把这些数字合起来，卷的参数是：

| 项 | 值 | 来源 |
| --- | --- | --- |
| 失败时已写入的单文件 | 64,028,672 B | `INST CHECK`（两次相同） |
| 卷上此前已有 | 541,600（launchd）+ 922,912（dyld）= 1,464,512 B | 前面的 `OK` |
| 写入总量 | 65,493,184 B | 上两项之和 |
| 推断卷容量 | 64 MiB = 67,108,864 B | 65,493,184 + 约 1.6 MB HFS+ 元数据 |
| 块大小 / 总块数 | 4,096 B / 16,384 | 同上（非日志卷，元数据很小） |
| 空闲块（失败后） | ≈ 0 | 1 MiB 写入立即 `ENOSPC` |

补丁 0131 的 `INST?` 现在直接打印这三个数，下一次刷机就能把它们从内核侧记下来
（`INST? space <free> of <total> bytes` 与 `INST? bsize <n> blocks <total> free <n>`）；
本节的容量是**推断值**，块级数字等下一次启动核对。没有对卷做任何格式化或重建。

结论很直接：**3.3 GB 根本装不进 64 MiB，P1 的下一个前置条件不是 dyld，而是一块
装得下共享缓存的根卷。** 两条路都可以，但都要在镜像这一侧解决：

1. 在同一块 `userdata` 分区上建一个更大的 HFS+ 卷——内核直接挂这个卷，卷头里的
   总块数决定容量，所以尺寸必须在 `newfs_hfs` 那一步就给足。按 47 条计划
   3,331,890,097 字节算，留一倍余量至少 **8 GiB**，最好直接把整个分区
   （229,843 MiB）给它。
2. 或者单独建一个缓存卷挂在 `/System/Library/Caches` 下；允许清单已经按路径前缀
   工作，多挂一个卷不需要改安装器。

在这一条解决之前，`pack-userland.py` 打出来的 3.3 GB 流只能跑 `INST DRY`
（它不写一个字节，只校验和打印计划），真装一定会在某一条上被空间检查挡住并
打印"这一条要多少、卷上还有多少"。

### 8. 这一次还没做到的

1. **一次完整的 3.3 GB 真装**：被上面的容量挡住。DRY 和单文件真写都通过了，
   完整真装要等新卷。
2. **`LaunchDaemons` 这一组的内容**：允许清单里留了
   `/System/Library/LaunchDaemons/`，但真正要哪几个 plist 属于下一阶段的事，
   现在放什么都消费不了。
3. **大文件在正常上下文下的复测**：这一次的会话跑在 `load_init_program` 失败
   之后的 panic 自旋里（根卷上没有能 exec 的 init），panic 路径会关中断，
   文件 I/O 的可靠性没有保证。等新卷能放下 `launchd`+`dyld`+缓存之后，应该在
   正常上下文里复测一次吞吐。
4. **压缩默认值的真机复核**：35.1% 是主机 zlib 量的；设备侧 inflate 的 CPU
   开销和 HFS+ 写谁先到瓶颈，要等卷够大跑完整流才知道。

### 9. 已知缺陷：`CS_KILLED` 的 panic 会把控制台留在死状态

这一轮踩到一个和安装通道无关、但会挡住所有真机实验的缺陷，记在这里。

**现象。**

* `kebab_cs=0` 的启动：`load_init_program` 之后 panic，控制台在 panic 之后仍然
  应答 `IMEM?` / `PSCI?` / `INST?` 数分钟——本节的 1.4 MiB 真写、`SKIP` 复验、
  592 MB 尝试全都是在 panic 自旋里跑完的。
* `kebab_cs=15` 的启动：panic 文本是
  `unexpected SIGKILL of launchd (CS_KILLED) … namespace 9 code 0x6`
  （0087 在 `kern_sig.c` 里把 pid 1 的意外 SIGKILL 变成 `panic_plain`）。
  此后 CDC-ACM **仍然枚举**（`VID 0x1209 / PID 0x0001`），但：

  - bulk-OUT 的每一次写都超时（`Write timeout`），`IMEM?` / `PSCI?` / `INST?`
    全部没有回应；
  - `fastboot devices` 看不到设备，20 分钟没有看门狗复位；
  - 主机的 PnP 禁用/启用（需要管理员）拿不到权限，做不到 USB 重新枚举；
  - 只能物理断电。

**代码位置与推断。** `ud_panic_takeover()` 只有在

```c
	uint32_t old = os_atomic_load(&ud.owner, acquire);
	if (old) {
		bool stopped = old == cpu;
		if (!stopped && old <= MAX_CPUS)
			stopped = ml_dbgwrap_cpu_is_halted((int)old - 1);
		if (!stopped) return false;      /* 不抢，永远不抢 */
		...
	}
```

时才接管发送门：owner 是自己，或者 `ml_dbgwrap_cpu_is_halted()` 认为那个 CPU
已经停了。后者是**调试器语义**的"CPU 已停"，而 panic 路径停其它 CPU 用的是
另一套机制，两者不重合时，只要 console 事件线程在 panic 那一刻正好持有发送门
（它在每 1 ms 的循环里都会短暂持有一会儿），panic 自旋就永远拿不到门，
`panic_spin_forever()` 的循环里也就不会再服务控制器——端口从此静默。

`kebab_cs=0` 那几次"能用"是运气：panic 恰好发生在门空闲的时候。所以这不是
"cs=15 引入的 bug"，而是一个一直在的竞态，只是 CS 杀 pid 1 的 panic 更容易
撞上（panic 前一刻内核正在密集打印 CS/exec 相关的日志，事件线程更可能在泵）。

**规避（本轮采用）。** 把 `/sbin/launchd` 换成一个 16,419 字节的静态桩：
写一行 `kebab: idle init, console stays up`，然后 `b .` 自旋。pid 1 永不退出，
也就不会有 `CS_KILLED` 的 panic，控制台一直可用。桩由
`scripts/make-init.py` 生成，只把 `write(2)` 之后的三条指令（`x0=0`、
`x16=1`、`svc`）换成 `b .; nop; nop`。

**正确修法（未做）。** panic 路径在 owner 是"本机已被 panic 停掉的 CPU"时
强制 `ud_controller_restart()` 再接管；或者 `ud_enter` 在 `panic_active()` 时
直接放弃门、让 panic 自旋无条件接管。两条都需要真机验证，属于下一轮。

## 六十三、把 userdata 做成真正的根卷

> 目标：把 `userdata` 上那个 64 MiB 的玩具 `KebabRoot` 换成一个装得下 3.3 GB
> 用户态的 HFS+ 卷。容量由卷头里的 `totalBlocks` 决定，不是分区大小，所以必须
> 重建卷本身。内核里不实现 HFS+：卷在主机上用 Apple 的 `newfs_hfs` 造好，只有
> 新卷实际分配的块被打包成一条流，内核通过 UFS 块路径原样写进 `userdata`。

这一节记八件事：尺寸怎么定、主机工具与 extent 格式、内核侧写卷路径、控制台协议
与安全门、真机上的旧/新卷参数、销毁与恢复、大于 64 MiB 的文件，以及装共享缓存
之前还差什么。

### 1. 尺寸：16 GiB

先把计划里的数字加起来：

| 载荷 | 字节 |
| --- | ---: |
| 44 个拆分 dyld 共享缓存 | 3,331,890,097 |
| `dyld_shared_cache_arm64.symbols`（592 MB 分片） | 592,101,376 |
| `launchd` + `dyld` | 1,464,512 |
| 合计 | 约 3.93 GB |

8 GiB 的卷装得下，但只剩约 4.1 GiB 余量（约 1.04 倍计划），共享缓存以后长一点
就会再次撞墙；16 GiB 留 12 GiB 余量（约 4.9 倍计划），同时仍然只占 `userdata`
分区（约 229 GiB；确切字节数以内核 `VOL?` 为准，见第 5 节）的 **7%**。
再大没有意义：卷越大，HFS+ 的分配位图越大（16 GiB 只要 512 KiB），而块级写入
和挂载路径并不因为卷大而变快。

于是选 **16 GiB = 17,179,869,184 字节**，卷名 `KebabRoot`，分配块 4096 字节，
共 **4,194,304 块**。三个选择都是被现有路径约束出来的：

* **4096 字节分配块**：UFS 设备的块就是 4096，内核页是 16 KiB；4 KiB 分配块
  让每一笔 UFS 写都是整数个设备块，不触发 `kebab_ufs_strategy()` 的
  "读-改-写" 分支。旧卷也是 4096，挂载路径一行不用改。
* **非日志（non-journaled）**：旧卷就是非日志，补丁 0086 已经允许脏的非日志
  根卷；日志会多出至少 8 MiB 元数据和一个回放路径，bring-up 阶段不需要。
* **无分区表**：镜像从字节 0 就是 HFS+，和设备上 `userdata` 的布局一致；
  `hdiutil` 用 `diskimage-class=CRawDiskImage` 把普通文件当裸盘挂上，再跑
  `newfs_hfs`，不会写 GPT。

`newfs_hfs` 在 16 GiB 卷上默认会把 catalog 预分配到 32 MiB、attributes 到
64 MiB；这些块全都置零但被位图标记为已分配。主机工具用
`-c c=256,e=32,a=8 -I c=256,e=32,a=8` 把 clump/初始大小钉小，把已分配块压到
**426 块（1.7 MiB）**，这才让"只送已分配块"这件事真正便宜。

### 2. 主机侧：`scripts/make-root-volume.py` 与 VEXT v1

用法（macOS，需要 `hdiutil` / `newfs_hfs`）：

```sh
scripts/make-root-volume.py --size 16g \
    --image /tmp/KebabRoot.img --stream /tmp/KebabRoot.vext
```

它做四步：

1. `mkfile -n 16g` 造一个稀疏文件（逻辑 16 GiB，实际只占元数据块）；
2. `hdiutil attach -nomount -noverify -imagekey diskimage-class=CRawDiskImage`
   把它挂成 `/dev/diskN`；
3. `newfs_hfs -v KebabRoot -b 4096 -c c=256,e=32,a=8 -I c=256,e=32,a=8
   /dev/rdiskN`，然后 detach。`hdiutil` 和 `newfs_hfs` 都是 Apple 自己的实现，
   卷的每一个字节都由它们产生，内核不参与格式决策；
4. 读新卷头里的 allocation file fork，取分配位图，按位图把**已分配块**合并成
   extent，写出 VEXT v1 流。

只送"非零字节"是不够的：实测 426 个已分配块里只有 11 个块含非零字节（45,056
字节），其余 415 个是 catalog / extents / attributes 的预分配零块。这些块在
HFS+ 眼里是已分配的 B-tree 节点区，如果跳过它们，旧卷的字节会留在新卷的
metadata 文件里；虽然未使用的节点不会被读，但"新卷"就不干净了。所以工具送的是
**位图标记为已分配的全部块**，零块也送。代价是 1.7 MiB，按 7.8 MiB/s 不到
0.3 秒；收益是新卷的每一个已分配块都等于镜像。

VEXT v1 的格式（全部小端）：

```
流头 32 字节
  0   4  magic 'VEXT'          4   2  version 1
  6   2  header_size 32        8   8  volume_bytes（整个卷的字节数）
  16  4  block_size 4096       20  4  extent_count
  24  8  data_bytes（所有 extent 长度之和）
extent 头 16 字节 + 数据，重复 extent_count 次
  0   8  offset（卷内字节偏移，4096 对齐）
  8   4  length（4096 对齐，非零）
  12  4  reserved = 0
尾部：0 填充到 65536 字节的整数倍
```

4096 对齐是内核侧的硬要求：写入路径只接受整块，半块会被
`kebab_ufs_raw_write()` 直接拒绝。填充保证最后一个 bulk-OUT 传输是满 64 KiB，
否则"正好 512 整数倍"的短包不会让 TRB 完成——这条约束和 KIST 流完全一样。

实测的主机输出（macOS 14.6.1，2026-10-28）：

```
volume size 17179869184 bytes (16384 MiB), block size 4096, total blocks 4194304
free blocks 4193878 (16382 MiB), allocated blocks 426
extents     3, data 1744896 bytes (1704 KiB), non-zero 45056 bytes, allocated-but-zero blocks 415
stream      KebabRoot.vext, 1769472 bytes (27 chunks), padding 24496,
            sha256 e37713002c463d99635ec790263eb21b7799c18388768a45b3910ace53f920eb
```

三个 extent 是：`0x0 + 0xa9000`（块 0..168，含卷头和 catalog 等）、
`0xf9000 + 0x100000`（块 249..504）、`0x3fffff000 + 0x1000`（最后一个块里的
备用卷头）。总线上 27 个 64 KiB 块，1.77 MB。

工具自己会把写出的流再读一遍：核对每个 extent 头和载荷、总长度、填充字节，任何
不一致都直接失败，不会把一个坏流交给内核。

### 3. 内核侧：`bsd/dev/kebab_vol.c`（补丁 0133）

新增 `bsd/dev/kebab_vol.{c,h}` 和 `bsd/dev/kebab_ufs.h`，并在 `bsd/conf/files`
登记。代码分三层：

**（1）流状态机。** 和安装器一样只向前读：流头 → extent 头 → extent 数据 →
填充。每读一个字段先验上界，绝不相信线上的长度：

* `volume_bytes` 必须等于命令参数、必须是 4096 的倍数、必须不超过
  `userdata` 分区字节数；
* 每个 extent 的 offset/length 必须 4096 对齐、非零、按 offset 递增不重叠、
  完全落在声明的卷内；第一个 extent 必须从 0 开始；
* 所有 extent 数据之和必须等于流头声明的 `data_bytes`，加上头部后必须正好等于
  `VOL BEGIN` 的 wire 字节数，填充必须全零；
* **卷头在写第一个块之前校验**：从流里截住偏移 1024 的 512 字节，核对
  `sig == 0x482b`、`version == 4`、`blockSize == 4096`、
  `totalBlocks * blockSize == volume_bytes`。不一致就 `VOL ABORT`，块 0 一个
  字节都没动。

**（2）块写入。** 64 KiB 暂存缓冲按 extent 顺序累积，满了就调
`kebab_ufs_raw_write()`；extent 末尾的余数也必须是整块。这个函数在
`kebab_ufs.c` 里，绕过 buffer cache，且每次都在 UFS 锁内把
`offset + length` 和 `userdata` 分区重新比一遍——不信任解析器传来的任何范围。
写路径就是原有的 `ufs_write_blocks()`，和 HFS+ 读写走同一套 WRITE(10)。

**（3）写后验证。** 流结束后从设备重新读 512 字节卷头，和流里截住的 512 字节
逐字节比较；一致才打印 `VOL NEW` 和 `VOL DONE`。旧卷头在开始前同样读一遍，
字段和 512 字节原始十六进制都打印到控制台——不依赖主机保存文件。

`kebab_vol=1` 还会在 `bsd/vfs/vfs_subr.c` 里压过 `rootrw=1`，让旧根卷以只读
方式挂载。这不是洁癖：旧 HFS+ 如果带着脏 buffer 在写完之后回写一个缓存的卷头，
新卷头会被悄悄盖掉，故障要等到下一次启动才出现。新卷下一次普通启动
（不带 `kebab_vol`）仍然按 `rootrw=1` 读写挂载。

另外给测试加了一个默认关闭的启动参数 `kebab_noinit=1`：init 线程在
`load_init_program()` 之前看到它就打印一行并阻塞在核内（不返回用户态，因为那是
init task 的 BSD AST，返回会回到不存在的用户上下文），USB 事件线程、控制台和
文件系统路径继续工作。
当前镜像的 `launchd` 会以 `CS_KILLED` panic，panic 会把正在写卷或正在装 565 MB
文件的控制台打断；这个参数让 bring-up 测试有一个稳定的内核控制台。普通启动
不带它，行为不变。

HFS+ 挂载日志在补丁里加了一行，卷的大小不再需要推断：

```
KEBAB HFS: KebabRoot: block size 4096, total blocks 4194304 (16384 MiB),
           free blocks 4193878 (16382 MiB)
```

### 4. 控制台协议与安全门

命令族沿用 INST 的匹配方式：一次 bulk-OUT 传输里匹配完整命令行，参数从同一次
传输里读出来。所有数字都做上界检查。

| 命令 | 作用 |
| --- | --- |
| `VOL?` | 门禁、分区字节数、磁盘上的卷头字段、当前根卷的空闲空间 |
| `VOL DRY <wire_bytes>` | 接下整条流、完整校验、打印计划，不写一个字节 |
| `VOL BEGIN <wire_bytes> <volume_bytes> CONFIRM` | 接下整条流并写 `userdata` |

一次一块，块大小 65536：

```
主机                                     设备
VOL BEGIN 1769472 17179869184 CONFIRM ──▶ 读旧卷头、打印、校验命令
                     ◀──────────────────  VOL OLD vh …（字段 + 512 字节）
                     ◀──────────────────  VOL BEGIN 1769472 volume … chunk 65536
<65536 字节>         ──────────────────▶  校验流头，开始写第一个 extent
                     ◀──────────────────  VOL ACK 65536
<65536 字节>         ──────────────────▶  …
                     ◀──────────────────  VOL EXTENT 3/3 off … len …
                     ◀──────────────────  VOL ACK 1769472
                     ◀──────────────────  VOL NEW vh …（读回的字段 + 512 字节）
                     ◀──────────────────  VOL DONE extents 3 bytes … wire … ms …
```

ACK 在块真正写完（`ufs_write_blocks()` 返回）之后才打印，`n` 是累计字节数；
设备一次只挂一个 TRB，块没写完不重新挂载，主机想抢跑也写不进来。60 秒没有
新块就 `VOL TIMEOUT`，把控制台还回来。

安全门有六层，任何一层不满足都只打印拒绝，不 panic：

1. 启动参数 `kebab_vol=1`（默认关）；
2. `ufsrw=1` 且 UFS 盘确实可写，否则 `VOL refused: userdata is read-only`；
3. 真写必须带字面量 `CONFIRM`，且 `volume_bytes` 必须与流头一致；
4. `volume_bytes` 必须不超过 `userdata` 分区（`kebab_ufs_partition_bytes()`），
   超过就打印两个数字并拒绝；
5. 卷头/流/分区三者必须自洽，见上一节；
6. `kebab_vol=1` 强制旧根只读，避免旧 HFS+ 回写。

所有写都经过 `kebab_ufs_raw_write()`，它只认 `su.first_block..first_block+
su.nblocks` 这一段；代码里没有任何直接写 UFS 寄存器或别的分区的路径。

### 5. 真机验收（待真机）

> 到这份文档提交时，板子仍卡在另一个 worker 留下的 panic 里：COM7 能枚举，但
> 任何写入都超时，`fastboot devices` 为空，只有用户物理断电才能恢复。因此这一节
> 是**尚未执行的验收步骤和预期输出**，不是实测记录。已经证明的是第 3.1 节的主机
> 重建（同一份 `kebab_vol.c` 跑真实流）、`verify-all.sh` 的 112/112、从零构建，
> 以及两个已打包的镜像。

#### 5.1 写卷镜像 `boot-vol.img`

完整命令行见第 5 节开头的表。刷入 `boot_b` 后预期看到（关键行）：

```
KEBAB UFS: "userdata" on LUN 0: first block 0x2c7420, ... MiB, read-write
BSD root: ufsroot0, major 1, minor 0
hfs: mounted KebabRoot on device b(1, 0)
KEBAB HFS: KebabRoot: block size 4096, total blocks 16384 (64 MiB), free blocks <n> (<n> MiB)
KEBAB VOL: kebab_vol=1 keeps the old root read-only
KEBAB: kebab_noinit=1: staying in the kernel, no init program
```

`KEBAB HFS:` 这一行是补丁 0133 新加的；旧卷的实际块数和空闲块以真机日志为准，
如果与第 5.6 节的 16,384 / 0 不同，按日志更新表格。`VOL?` 的预期输出：

```
VOL? gated 1 active 0 dry 0 chunk 65536 writable 1
VOL? partition <bytes> bytes (<MiB> MiB)
VOL? DISK vh sig 0x482b version 4 attributes 0x... ... bsize 4096 blocks 16384 free <n> ...
```

#### 5.2 `VOL DRY`：校验整条流，不写

```sh
python work/vol/send-volume.py --dry work/vol/KebabRoot.vext
```

预期最后一行：

```
VOL PLAN extents 3 bytes 1744896 wire 1769472 volume 17179869184
```

#### 5.3 `VOL BEGIN`：旧卷头的 512 字节先落日志

```sh
python work/vol/send-volume.py --yes work/vol/KebabRoot.vext
```

预期的顺序是：`VOL partition …`、`VOL OLD vh …` 三行字段、32 行
`VOL OLD bytes <off> <32 字节十六进制>`、然后才是 `VOL BEGIN …`。**这 32 行旧
卷头字节要在本节抄录一份，再继续**。随后是：

```
VOL STREAM volume 17179869184 bytes (16384 MiB), block 4096, extents 3, data 1744896 bytes, wire 1769472
VOL EXTENT 1/3 off 0x0 len 0xa9000
VOL EXTENT 2/3 off 0xf9000 len 0x100000
VOL EXTENT 3/3 off 0x3fffff000 len 0x1000
VOL NEW vh sig 0x482b version 4 attributes 0x80000100 ... bsize 4096 blocks 4194304 free 4193878 ...
VOL DONE extents 3 bytes 1744896 wire 1769472 ms <n>
```

`VOL NEW` 的 512 字节是从设备重新读回来、和流里那 512 字节逐字节比较通过之后才
打印的。旧的 512 字节在写入之前打印，新的 512 字节在写入之后打印，两者都在同一
份控制台日志里。

#### 5.4 重启：HFS+ 挂载新卷

写完后发 `REBOOT`（`boot-vol.img` 的 `kebab_vol=1` 仍然在，所以新卷先只读
挂载一次），预期：

```
hfs: mounted KebabRoot on device b(1, 0)
KEBAB HFS: KebabRoot: block size 4096, total blocks 4194304 (16384 MiB), free blocks 4193878 (16382 MiB)
```

这一行 `total blocks 4194304` 就是"内核的 HFS+ 挂载成功并报告更大卷"的验收点。

#### 5.5 `INST?`：新卷的空闲空间

刷 `boot-inst.img`（去掉 `kebab_vol=1`，保留 `rootrw=1` 和 `kebab_noinit=1`）
之后：

```sh
python send-userland.py --status
```

预期：

```
INST? gated 1 active 0 dry 0 root rw chunk 65536
INST? wire 0/0 entries 0/0 ok 0 skip 0 fail 0
INST? space 17178124288 free of 17179869184 bytes
INST? bsize 4096 blocks 4194304 free 4193878
```

（`space` 的数字以 `f_bavail × f_bsize` 为准，可能因 HFS+ 的保留块与
4,193,878 略有出入，报告两个数。）

#### 5.6 前后对比（旧卷数字来自第 5.1 节的真实日志）

| 项 | 旧卷 | 新卷 |
| --- | ---: | ---: |
| 卷大小 | 64 MiB = 67,108,864 B | 16 GiB = 17,179,869,184 B |
| 分配块大小 | 4096 | 4096 |
| 总块数 | 16,384（待真机确认） | 4,194,304 |
| 空闲块 | 0（待真机确认） | 4,193,878 |
| 空闲空间 | 0 | 16,382 MiB |
| 文件系统 | HFS+，非日志，`KebabRoot` | HFS+，非日志，`KebabRoot` |

旧卷的 512 字节卷头以真机日志为准；本节不会用推断值代替它。

### 6. 销毁了什么，怎么恢复

`userdata` 上原来只有三样东西，全部在写卷时被覆盖：

| 路径 | 大小 | 说明 |
| --- | ---: | --- |
| `/sbin/launchd` | 541,600 B | 手写静态 arm64 Mach-O |
| `/usr/lib/dyld` | 922,912 B | 用户态 dyld |
| `/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64.symbols` | 64,028,672 B | 592 MB 分片的**前 64 MiB**，两次都在这里 ENOSPC |

旧卷头的 512 字节在写之前由 `VOL BEGIN` 打印到控制台（第 5.3 节），不依赖主机
保存的二进制文件。主机上的原件都在，且哈希可查：

| 主机文件 | 大小 | SHA-256 |
| --- | ---: | --- |
| `ios-userland/extracted/sbin/launchd` | 541,600 | `12370e5e…18cac61c` |
| `ios-userland/extracted/usr/lib/dyld` | 922,912 | `58068345…254933757` |
| `ios-userland/cryptex-cache/dyld_shared_cache_arm64.symbols` | 592,101,376 | `e763b0ea…5126b308` |
| `ios-userland/static.trustcache` | 66,289 | `63e01c03…c6100ef4b` |

恢复路径就是安装器本身，不需要新工具：

```sh
# 三个小文件（launchd、dyld、静态信任缓存），576 KiB 流
python send-userland.py work/vol/restore.kist

# 592 MB 的 symbols，565 MB 流
python send-userland.py big.kist
```

`restore.kist` 的第三条目的地是
`/System/Library/Caches/com.apple.kernelcaches/static.trustcache`，正好是补丁
0100 的 `KEBAB_TC_DEFAULT_PATH` 默认读取路径；`kebab_cs=15` 的下一次启动会把它
当作平台信任缓存加载。按第 62 节在 64 MiB 卷上实测的 7.84 MiB/s，1.4 MB 的
launchd+dyld 不到一秒，565 MB 的 symbols 约 72 秒；等 3.3 GB 的 44 个缓存分片
就位，整卷恢复约 7 分钟。恢复只还原文件，不解决 `launchd` 的 `CS_KILLED`——那
是签名/AMFI 那条线，见第 8 节。

### 7. 大于 64 MiB 的文件（待真机）

这是旧卷上不可能的写入：两次都停在 64,028,672 字节并返回 ENOSPC。新卷上同一
条流完整跑完，安装器写完后重新打开文件、再算一次 SHA-256：

```sh
python send-userland.py big.kist
```

预期（`big.kist` 已在主机上 `--verify` 通过：1 条 entry、592,101,376 字节、
store、SHA-256 `e763b0ea…5126b308`）：

```
OK   /System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64.symbols 592101376 mode 0644
INST DONE entries 1 ok 1 skipped 0 bytes 592101376 wire 592117760 ms <n>
```

再单独复核：

```sh
python send-userland.py --probe big.manifest --skip-list work/vol/logs/have.txt
```

预期 `SAME    /System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64.symbols`。
旧卷的失败点 64,028,672 B 正好是 64 MiB（67,108,864 B）减去 HFS+ 元数据后的
可用空间；新卷 16 GiB 上，同一个文件只占 3.4% 的卷。

### 8. 装共享缓存之前还差什么

1. **完整的 3.3 GB 真装**：本节只准备了最大的单个文件（592 MB symbols）。44 个
   分片的 KIST 流总量 3,331,890,097 字节，按 7.84 MiB/s 约 7 分钟；卷的空闲
   空间足够，剩下的只是把整条流跑完并核对 44 条 `OK`。
2. **`LaunchDaemons` 的内容**：允许清单里留了
   `/System/Library/LaunchDaemons/`，但真正要哪几个 plist 还没有定；现在放进去
   也没有东西消费它们。
3. **共享缓存的消费路径**：0133 只保证卷能装下缓存；`kebab_dsc` 的映射、
   slide、UUID 校验是另一条线，和卷大小无关。
4. **真 `launchd` 的签名**：当前 `launchd` 仍然 `CS_KILLED`。这是 AMFI/codesign
   的问题，与根卷无关；`kebab_noinit=1` 只是让 bring-up 测试不被这个 panic
   打断。普通启动不带 `kebab_noinit`，行为和以前一样。
5. **恢复后不带 `kebab_noinit` 的一次启动**：`restore.kist` 把 launchd、dyld 和
   静态信任缓存放回新卷之后，再启动一次**不带** `kebab_noinit` 的镜像，就能直接
   回答签名问题：如果 `launchd` 不再 `CS_KILLED`，它应该死在缺少共享缓存
   （`libSystem`）的更后面一步；如果仍然 `CS_KILLED`，说明信任缓存路径或
   `kebab_cs` 的策略位和预期不符，下一步要对比期望/实际 cdhash。
6. **恢复**：写卷之后先跑 `restore.kist`，再跑 44 个缓存分片；主机上的原件和
   哈希见第 6 节。

## 六十四、panic 不再需要按电源键

> 目标：panic 之后板子要么还有一条能用的控制台，要么自己重启。不能停在
> `Waiting for remote debugger connection` 里，也不能停在永远拿不到发送门的自旋里
> 等人按电源键。这一节记三件事：两条死路分别是怎么形成的、补丁 0134 改了什么、
> 下次在日志里怎么区分"控制台活下来 / 重启了 / 仍然卡住"。

### 1. 两条独立的死路

现场看到的"CDC-ACM 还枚举、每次 bulk-OUT 都超时、没有 fastboot、20 分钟没有
看门狗复位"可以由两条互不依赖的路径产生。补丁 0134 对两条都做了处理。

**第一条：KDP 等待。** `debug=0x14e` 在 arm64 上带 `DB_NMI`（0x4），
`panic_init()` 因此把 `panicDebugging` 置为 `TRUE`。panic 走到
`handle_debugger_trap()` 时，只要 `kdp_explicitly_requested` 为真
（本板由 `kebabkdp=1` 置位），就会在 `panic_spin_forever()` 之前调用
`kdp_raise_exception()`；`kdp_connection_wait()` 打印

```
Using serial KDP.
Waiting for remote debugger connection.
```

然后在同一个 CDC-ACM 端口上轮询 KDP 包，直到调试器连上。没有 LLDB 的时候这个
循环不会退出：端口被 KDP 收走，主机的 `IMEM?` / `PSCI?` / `INST?` 不是控制台
命令而是被当成 KDP 输入，所以全部没有回应。这是**确定性**的，不需要任何竞态。

**第二条：发送门被 panic 停住的 CPU 留住。** 控制台自己有一套发送门：

* `ud_enter()` 在真正发数据前用 `os_atomic_cmpxchg(&ud.owner, 0, cpu+1)` 记录
  "哪个 CPU 持有发送门"，并把 IRQ 关掉；`ud_leave()` 在退出时清 `ud.owner`。
* console 事件线程 `ud_thread()` 没有绑定 CPU，每 1 ms 醒来一次，
  `ud_enter()` → `ud_poll_locked()` → `ud_leave()`；普通 `printf`、EP0 dump、
  KDP 回包也都走同一道门。
* panic 路径用 `DebuggerHaltOtherCores()` → `DebuggerXCallEnter()` 停其它 CPU：
  先发 `SIGPdebug` IPI，等 `debug_ack_timeout`，超时后再尝试
  `ml_dbgwrap_halt_cpu()`。如果 panic 恰好撞上事件线程在 `ud_enter()` 里
  （IRQ 已关，IPI 送不到），那个 CPU 既没有 ack，也没有被正常停住，而
  `ud.owner` 仍然是它的编号 + 1。

`ud_panic_takeover()` 原来的判据是：

```c
uint32_t old = os_atomic_load(&ud.owner, acquire);
if (old) {
        bool stopped = old == cpu;
        if (!stopped && old <= MAX_CPUS)
                stopped = ml_dbgwrap_cpu_is_halted((int)old - 1);
        if (!stopped) return false;      /* 不抢，永远不抢 */
        ...
}
```

这个判据在本板上不可能为真：

* `ml_dbgwrap_cpu_is_halted()` 只读 Apple CoreSight UTT 窗口里的
  `DBGWRAP.DBGACK`。KEBAB 的 ADT 由 `scripts/make-kebab-adt.py` 生成，CPU 节点
  只有 `state` / `reg` / `device_type` / `compatible` / `timebase-frequency` /
  `bus-frequency`，没有 `coresight-reg` 或 `cpu-IMPL`；于是
  `configure_coresight_registers()` 不映射 `CORESIGHT_UTT`，
  `ml_dbgwrap_cpu_is_halted()` 在 `coresight_base[CORESIGHT_UTT] == 0`
  这一句就返回 `FALSE`。
* 即使映射了 CoreSight，被 `SIGPdebug` 停住的 CPU 停在 debugger 的等待循环里，
  也不会置 `DBGACK`；这个 API 表达的是"外部调试器 halt 过"，不是"panic 已经
  把调度器停了"，两者本来就不重合。

于是接管永远被拒绝，`kebab_usb_panic_spin()` 的旧循环是：

```c
taken = ud_panic_takeover();
if (!taken) { delay(1000); continue; }
```

它既不轮询控制器，也不重启，就停在这两行上。DWC3 的 pull-up 和已经完成的
枚举还在，所以主机仍然看得到 ttyACM；但没有任何代码再服务端点，每次写都超时。
这块板没有能把它踢醒的看门狗，于是一停就是几十分钟。

这两条路里，第一条是 `kebabkdp=1` 时必然发生的；第二条是竞态，panic 撞不上
持有发送门的 CPU 时不会发生（第 62 节里 `kebab_cs=0` 那几次 panic 之后控制台
还能用，就是门恰好空闲）。所以修复也必须分成两半：**KDP 默认策略**和
**panic 接管**，缺一不可。

### 2. 修复一：panic 期间无条件接管发送门

`ud_panic_takeover()` 现在先看 `panic_active()`：

* `TRUE`：跳过 `ml_dbgwrap_cpu_is_halted()`。只要 `ud.owner` 或 `ud.rx_owner`
  非零，就调用 `ud_controller_restart()` 做一次 core reset，把在途的 endpoint
  command / DMA 全部收回，然后清 `owner` / `owner_depth` / `rx_owner` /
  `rx_depth`。`ud.log_busy` 也直接清掉（生产者在 panic 里不可能还活着）。
* `FALSE`：保留原来的"必须证明对方已停"的检查；这条路径不会被 panic 循环
  调用，只是防御性的。拒绝时打印一行
  `KEBAB USB: console takeover refused: owner ... is not halted`。

core reset 是必要的：被停住的 CPU 可能留下了未完成的 TRB 或 endpoint command，
只清软件锁会复用一块所有权还不知道的 DMA 内存。reset 完成后 EP0 重新初始化，
主机会看到一次断开/重连，重新打开 `/dev/ttyACM0` 即可；panic 日志在 ring 里，
重连后由 EP0 dump 和 log pump 一起送出。

### 3. 修复二：服务不了就重启

`kebab_usb_panic_spin()` 不再是"接管失败就永远等"：

* 接管失败累计 1000 次（每次 1 ms，约 1 秒）后，打印
  `KEBAB USB: panic console cannot be serviced (transport takeover refused); rebooting instead of parking`
  然后走 `ud_platform_restart()` → `PEHaltRestart(kPERestartCPU)` →
  PSCI `SYSTEM_RESET`。
* 接管成功但 `ud.dead` 或 `ud.running == 0`（控制器已经 reset 失败 / 端口从未
  起来）时，同样打印一行，原因分别为 `controller dead` / `console not running`，
  然后重启。
* `PEHaltRestart()` 真的返回（固件拒绝 reset）时再打印
  `KEBAB USB: panic reboot returned; the machine did not reset` 并停在原地；
  这是唯一还允许"停住"的分支，因为已经没有别的复位手段了。

平台层也补了一个缺口：`KebabPlatformExpert::haltRestart()` 原来只对
`kPERestartCPU` 做 PSCI reset。panic 的 `kPEPanicRestartCPU` /
`kPEPanicRestartCPUNoCallouts` 如果没进控制台泵（console 没起来），会落到
`IODTPlatformExpert::haltRestart()` 返回 -1，随后 `halt_all_cpus()` 把板子
停死。现在这两个 panic 重启类型也会走 PSCI reset，并打印
`KEBAB PANIC: no console to service; rebooting via PSCI SYSTEM_RESET`。

### 4. 修复三：默认不再等调试器

`osfmk/kern/debug.c` 的 `panic_init()` 里，KEBAB 现在做：

```c
if (!kdp_explicitly_requested) {
        panicDebugging = FALSE;
}
```

`kdp_explicitly_requested` 只由两个显式请求置位：`kdp_match_name` 或
`kebabkdp=1`。因此：

* 普通启动（`debug=0x14e` 但没有 `kebabkdp=1`）：`DB_NMI` 不再把 panic 变成
  "停在调试器等 KDP"。panic 会正常走到重启决策；如果 USB 控制台可用，
  平台层把它接成 panic 控制台泵（panic 报告、EP0 dump、端口重开都保留），
  如果控制台不可用就直接 PSCI 复位。
* `kebabkdp=1`：`panicDebugging` 保持 `TRUE`，panic 仍然先 trap 到 KDP，
  `Waiting for remote debugger connection` 和 LLDB attach 的行为不变。
  这是显式请求，等待调试器是预期行为。
* `halt_in_debugger` 没有动：`debug=0x14f` 的 `DB_HALT` 仍然在显式
  `Debugger()` 入口停住；`DB_REBOOT_ALWAYS` 也仍然优先于 KDP。

启动参数与 panic 行为对照：

| 启动参数 | `panicDebugging` | panic 之后 |
| --- | --- | --- |
| `debug=0x14e kebabusb=1`（默认） | FALSE | 控制台可用则进 panic 泵；发送门被留住时 reset + 重连；控制台不可用则 PSCI 复位 |
| 默认 + `kebabkdp=1` | TRUE | 先 trap KDP，打印 `Waiting for remote debugger connection`，等 LLDB；不自动重启（显式请求） |
| `debug=0x14e` 但无 `kebabusb=1` | FALSE | 没有控制台可服务，panic 重启类型直接 PSCI reset |
| `debug=0x14f ...` | FALSE（但 `halt_in_debugger=1`） | `DB_HALT` 路径未改；配合 `kebabkdp=1` 时启动/显式 `Debugger()` 仍停在调试器 |
| 任意 + `DB_REBOOT_ALWAYS` | 不变 | 在 KDP 之前就重启 |

### 5. 日志判据

**A. 控制台活下来，发送门本来是空的。** panic 报告照常从原来的 ttyACM 出来；
之后 EP0 dump 里：

```
KEBAB EP0 PROBE: progress 0x........ seq ... req ... phase ... status ...
KEBAB EP0 STATE: resets=... disconnects=... fences=<不变> ... takeover=0 ...
KEBAB KDP STATE: host_in=... in_drop=... kdp_in=... kdp_frames=... kdp_out=...
                 tx_bytes=... owners=0/0/<panic cpu+1> depths=0/0
```

`fences` 不变（没有 core reset），`owners=0/0/...` 说明发送门和接收锁都已经
在 panic CPU 手里。端口不用重开，`IMEM?` / `PSCI?` / `INST?` / `REBOOT` 直接
有回应。

**B. 控制台活下来，但发送门被留住过。** 主机会看到一次 USB 断开/重连
（同一个 VID/PID，`/dev/ttyACM0` 可能改名）；重新打开端口后，同一组 EP0 dump
里 `fences` 比 panic 前多 1，`takeover=<非零位图>`（panic 时还 busy 的端点），
`owners=0/0/<panic cpu+1>`。panic 文本可能在重连后才到，因为它在 ring 里等
log pump。之后的 `IMEM?` / `REBOOT` 正常。

**C. 重启了（控制台不可服务）。** 如果主机在复位前还读得到，会先看到下列之一：

```
KEBAB USB: panic console cannot be serviced (transport takeover refused); rebooting instead of parking
KEBAB USB: panic console cannot be serviced (controller dead); rebooting instead of parking
KEBAB PANIC: no console to service; rebooting via PSCI SYSTEM_RESET
```

随后是 `KEBAB REBOOT: PEHaltRestart(kPERestartCPU)`，板子从 boot 重新开始
（boot 日志、`KEBAB USB: ... running ...` 全部重来）。如果控制器已经 dead，
上面那行可能根本发不出去；**"没有新日志 + 几秒内重新枚举并从头启动"本身就是
重启成功的证据**。

**D. 仍然卡住（只应在固件拒绝复位时出现）。** 会看到

```
KEBAB REBOOT: PEHaltRestart returned <err>, the machine did not reset
KEBAB USB: panic reboot returned; the machine did not reset
```

这时才需要物理断电；普通 panic 不应该再走到这里。

**E. `kebabkdp=1` 的预期日志。** `Using serial KDP.` +
`Waiting for remote debugger connection.`，然后 LLDB attach。没有 attach 时
停在这里是显式请求的结果，不是本缺陷复发；判断方法是看启动参数里有没有
`kebabkdp=1`。

### 6. 验证状态与下一步

补丁 0134 只做了代码和编译验证：`kernel.development.kebab` 在
`MACOS_VERSION=13.5 / KEBAB / DEVELOPMENT` 下构建通过，改动文件没有引入新的
编译告警（原有的 `-Wdeclaration-after-statement` 误报仍在，属本文件既有风格）。
板子当前处于 stranded 状态，真机验证要等下一次上电：

1. 默认参数（不带 `kebabkdp`）启动，带 `kebabpanic=1`，在控制台敲 `PANIC!`；
   预期 A 或 B。重复多次以覆盖发送门竞态。
2. 带 `kebabkdp=1` 启动再 `PANIC!`；预期 E，LLDB 能 attach，`continue` 后
   按 KDP 的语义走。
3. 不带 `kebabusb=1` 启动并制造一次 panic（需要另一个 panic 触发点，或临时
   把 `kebab_usb_start()` 的入口关掉）；预期 C 的
   `KEBAB PANIC: no console to service; ...` + 从头启动。
4. 只有看到 D 才说明固件复位也失败了，需要另行处理 PSCI 路径。

## 六十五、触摸报告解码

> 目标：把 S3908 从"读得出身份"推到"解得出触点"。这一节记录五件事：应用的 TCM
> 命令序列与每一步的理由、报告程序与报告布局、位域抽取（用一条合成报告逐位走
> 一遍）、主机测试清单，以及真机日志里到底看哪几行才能判断解码是对的。
> 补丁 0135。

### 1. 为什么先读 application info，再写报告程序

应用信息（`CMD_GET_APPLICATION_INFO`，0x20）的 payload 是厂商的
`struct syna_tcm_app_info`，46 字节，全部是 16 位小端：

| 偏移 | 字段 | 本驱动怎么用 |
| --- | --- | --- |
| 0 | version | 打印 |
| 2 | status | 非 0 只警告（可能还在启动） |
| 4 / 6 | static / dynamic config 大小 | 打印 |
| 8 / 10 | app config 起始块 / 大小 | 打印 |
| **12** | **max_touch_report_config_size** | **报告程序缓冲区多大**：决定 `SET_TOUCH_REPORT_CONFIG` 发多少字节 |
| **14** | **max_touch_report_payload_size** | **一条报告最大多少字节**：打印，并用来判断 128 字节的接收上限够不够 |
| 16 | customer_config_id[16] | 打印 |
| **32 / 34** | **max_x / max_y** | **传感器几何**，三空间变换的输入 |
| **36** | **max_objects** | 解码时最多解几个触点 |
| 38 / 40 / 42 / 44 | buttons / rows / cols / hybrid | 打印 |

关键在顺序：报告程序的**长度**由 12 号字段决定，报告里 x/y 的**量程**由 32/34 号
字段决定。厂商驱动也是先读 app info 再设报告程序，并且把
`max_touch_report_config_size < 128` 当成错误——那 128 是它自己的缓冲区大小，不是
协议常量。本驱动没有 128 的硬要求：app info 给出的值在 13..128 之间就按厂商的做法
把程序零填充到那么长（程序解析器读到 `TOUCH_END`=0 就停，所以零填充是给终止符留
位置）；给不出（没读到、为 0、大于 128）就只发程序本身，并打印原因。

app info 读不到不是致命错误：`max_x/max_y` 回退到设备树的 1079/2399，`max_objects`
回退到 10，报告程序按未填充发送。**但报告程序本身必须被接受**，否则没有布局可解，
驱动不启动轮询线程，也不建 `/dev/kebab_touch`。

### 2. TCM 命令序列

```text
identify            0x02  -> 报告 0x10（24 字节）   读 part/version/mode/build
get-boot-info       0x10  -> 状态（本固件 0x0e）     application 模式不实现，正常
get-app-info        0x20  -> 状态 0x01 + 46 字节    几何、对象数、两个报告尺寸
set-report-config   0x26  -> 状态 0x01              报告程序（带 z，13 字节）
```

报文格式是厂商的：`0xa5, code, len_lo, len_hi` + payload + `0x5a`。两条规则决定了
这次能和不能信什么：

* **代码范围**。命令应答的 code < 0x10（`STATUS_OK` 是 0x01）；报告 code >= 0x10
  （identify 是 0x10，触摸是 0x11，status report 是 0x1b）。identify 要的是报告
  0x10，不是状态；其它命令要的是状态。等错东西的消息会被读出来、打印、继续等，
  不会被当成答案解。
* **长度必须先校验再读**。header 里的长度大于 128 就不是本驱动能读的报文，四个
  字节原样打印后**逐字节滑窗重同步**（最多 4 字节）。这不是防御性编程，是实测：
  0132 那次 bring-up 里，identify 的 payload 后面跟了一个 `0x00`，接着读到的四个
  字节是 `a5 01 00 5a`，被当成"长度 0x5a00 = 23040"的报文，于是 app info 报
  `payload 23040 too large`，后面的 report config 收到 0x0d（上一条命令未完成）。
  新代码对这四个字节的回答是
  `header a5 01 00 5a rejected (bad length), sliding`，然后继续找真正的报文头。

**每个应答都原样打印**，不只 `kebab_touch_raw=1` 时：命令号、code、payload 长度、
尾字节、payload 的十六进制。这样"布局不对"和"框架不对"在日志里是两种不同的
现象，而不是一个静默的错误解析。

### 3. 报告程序与报告布局

`SET_TOUCH_REPORT_CONFIG`（0x26）的 payload 就是一段小程序，本驱动发的是：

```text
01 06 04 07 04 08 0c 09 0c 0a 08 03 00
```

| 字节 | 值 | 含义 |
| --- | --- | --- |
| 0 | 0x01 | `TOUCH_FOREACH_ACTIVE_OBJECT` |
| 1,2 | 0x06, 4 | `TOUCH_OBJECT_N_INDEX`，4 位 |
| 3,4 | 0x07, 4 | `TOUCH_OBJECT_N_CLASSIFICATION`，4 位 |
| 5,6 | 0x08, 12 | `TOUCH_OBJECT_N_X_POSITION`，12 位 |
| 7,8 | 0x09, 12 | `TOUCH_OBJECT_N_Y_POSITION`，12 位 |
| 9,10 | 0x0a, 8 | `TOUCH_OBJECT_N_Z`，8 位 |
| 11 | 0x03 | `TOUCH_FOREACH_END` |
| 12 | 0x00 | `TOUCH_END` |

每个活动触点 4+4+12+12+8 = **40 位 = 5 字节**，触点之间没有填充，所以一条报告
的 payload 长度是 5 的整数倍（一根手指 5，两根 10，全抬起 0）。厂商默认程序没有
z（32 位，4 字节）；如果固件拒绝带 z 的程序，驱动会退回那个默认程序，解码器读
哪个程序就按哪个布局解——z 字段不存在时压力恒为 0，其它字段一位不差。

程序不是写死在解码器里的：`kebab_tcm_decode_report()` 先扫程序，找
`FOREACH_ACTIVE_OBJECT` 的循环体、累加循环体里每个字段的宽度（得到"每个触点多少
位"），再按同一段程序逐字段解码。报告程序被接受，等于布局被协商过；解码器读的
就是这份协商结果。

### 4. 位域抽取

规则与厂商 `touch_get_report_data()` 一致：**字节内从 bit 0 开始，跨字节时先读到
的是低位**（小端位序）。合成样例（测试里用的那条）：

```text
payload: 13 c4 02 5a 7f      触点 3，class 1，x 708，y 1440，z 127
```

逐字段走一遍：

| 字段 | 位区间 | 宽度 | 取自 | 计算 | 结果 |
| --- | --- | --- | --- | --- | --- |
| index | 0..3 | 4 | byte0 低半字节 | 0x13 & 0xf | **3** |
| classification | 4..7 | 4 | byte0 高半字节 | 0x13 >> 4 | **1** |
| x 低 8 位 | 8..15 | 8 | byte1 | 0xc4 = 196 | |
| x 高 4 位 | 16..19 | 4 | byte2 低半字节 | 0x02 & 0xf = 2 | x = 196 + 2×256 = **708** |
| y 低 4 位 | 20..23 | 4 | byte2 高半字节 | 0x02 >> 4 = 0 | |
| y 高 8 位 | 24..31 | 8 | byte3 | 0x5a = 90 | y = 0 + 90×16 = **1440** |
| z | 32..39 | 8 | byte4 | 0x7f | **127** |

两个最容易错的地方正好都在这条样例里：

* **x 跨字节**：x 的 12 位横跨 byte1 和 byte2 的低半字节。把 x 当成 8 位，或者从
  bit 9 开始读，得到的是 0x162 = 354（正好是 708 >> 1）——测试里两个值都断言了。
* **y 从半字节中间开始**：y 的 12 位从 byte2 的**高**半字节开始，先取 4 位再取
  byte3 的 8 位。少读一位、多读一位都不会得到 1440：把 y 的宽度改成 13 位，会
  把 z 的最低位并进来，得到 1440 + 4096 = 5536（测试断言了这个值）。

解码器对报告长度也是保守的：只有当一个触点的 40 位全都还在 payload 里才解它；
剩下的位不够一个触点时，算作 `truncated_bits`，**不**解成触点。全零的尾巴是字节
填充（解出来对象数不变，不产生 up）；非零的尾巴说明报告被截断，整条丢弃并计数
打印。这样"手指还按着但报告被截断"不会变成"所有手指抬起"。

### 5. 触点状态机与事件

`kebab_tcm_track()` 拿上一条报告的活动掩码比较：

* 这一条里有、上一条没有 → **down**；
* 这一条里有、上一条也有 → **move**（记录每条都进队列，打印受 `kebab_touch_move_ms`
  限速，默认 200 ms）；
* 上一条有、这一条没有 → **up**，坐标用该槽位最后一次的 x/y，压力 0。

class 1（finger）和 class 2（gloved finger）算触点；class 0（lift）不算，它所在的
槽位会按"消失了"产生 up；index 超过 10 的对象被忽略。压力就是 z 字段（0..255），
这是这颗固件在报告里给出的唯一压力代理。

事件进 `/dev/kebab_touch` 的 48 字节记录不变（raw / panel / logical 三套坐标 +
z + phase + slot + timestamp），三空间变换仍然只有一处
（`kebab_tcm_transform()`）：

```c
panel_x   = (raw_x * (1080 - 1) + max_x / 2) / max_x;
panel_y   = (raw_y * (2400 - 1) + max_y / 2) / max_y;
logical_x = panel_x + 22;
logical_y = panel_y + 18;
```

`max` 取 app info 的 32/34 号字段；为 0 或大于 0xfff 时用设备树的 1079/2399（12 位
字段的 0xfff 是手指抬起时的常见值，先夹到面板边缘并置 `KT_EVENT_FLAG_CLAMPED`，
不向面板外推算）。启动时的队列自检（64 槽、推 192 条、丢 128 条、收 64 条）没有改。

### 6. 主机测试

`package/KebabTouchTcmHostTests.c` 驱动 `package/KebabTouchTcm.c`，不碰 I2C、不碰
内核、不碰板子，16 组合成报文流共 **193 项断言全部 PASS**：

```text
$ gcc -std=c11 -Wall -Wextra -Werror -o t KebabTouchTcm.c KebabTouchTcmHostTests.c && ./t
KebabTouchTcm host tests: framing, payloads, program, decode, tracking, transform
...
193 checks, 0 failure(s)
```

| 组 | 覆盖 |
| --- | --- |
| C1 | 命令写入 `{cmd, len_lo, len_hi, payload}`；缓冲区不够时拒绝而不是截断 |
| C2 | 报文 = 头 + payload + 一个 0x5a；长度、消费字节数、padding 错、截断、空指针 |
| C3 | **实测的 23040**：`a5 01 00 5a` 被 `bad length` 拒绝且四个字节不被改写；缺 marker 是另一种错误；前导杂字节能被滑窗跳过 |
| C4 | **实测的 identify 应答**：`a5 03 01 01 "s3908-15.0.0" b9 ba 3c 00` → mode 0x01 application、version 1、build 0x003cbab9、前缀 2 字节原样保留 |
| C5 | 厂商布局（无前缀）的 identify；无位置放 version/mode、无 part 串、part 提前结束 |
| C6 | app info 46 字节**逐字段**偏移（每字段值不同，错一位就失败）；45 字节拒绝 |
| C7 | 报告程序字节逐一比对（带 z / 厂商默认）；每触点 40 / 32 位；未知字段码、缺 `FOREACH_END` 拒绝 |
| C8 | 报告配置 payload 零填充到固件尺寸；固件尺寸为 0 发程序本身；小于程序时拒绝 |
| C9 | 单触点解码：五个字段 + 直接调位读取器的 5 个断言；**从 bit 9 读 x 得 0x162**；跨半字节的触点 |
| C10 | 一条手指的 down → move（x 709）→ up（用最后坐标、压力 0）；重复空报告不再产生事件 |
| C11 | 两触点：两个 down、活跃掩码 0x3；下一帧只有第二个 → move(slot1) 在前、up(slot0) 在后 |
| C12 | **截断**：32 位载荷解出 0 个触点、尾巴 32 位非零；1 个触点 + 32 位尾巴；8 位零尾巴算填充；报文层截断是 `short` |
| C13 | **命令回显**：code=0x02 不是 identify 报告；0x26 既不是状态也不是报告；0x11 在状态槽被拒；0x01 不是触摸报告 |
| C14 | 布局由程序决定：x 宽 11 位 → y 变 2880；y 宽 13 位 → y 变 5536；整条流错 1 位一定解出不同触点；无 z 程序解出的 z 恒 0 |
| C15 | tracker：class 2 算按下、class 0 触发 up、index 12 被忽略、事件数组不够时状态仍然更新 |
| C16 | 三空间变换：0→(0,0)/(22,18)、1079/2399→(1079,2399)/(1101,2417)、中间点、0xfff 夹紧、max=0 回退、2000 量程按比例缩放、16 位量程不被采用 |

测试第一次跑就抓到两个真问题，都是"在板子上很难看出来"的那种：

1. `kebab_tcm_parse_identify()` 把 part 串里 NUL 填充的字节替换成了 `.`，于是
   `s3908-15.0.0` 打印成 `s3908-15.0.0....`——字符串比较立刻失败。改为遇到 NUL
   结束。
2. y 宽度 13 位的用例一开始只有 5 字节载荷：40 位装不下 41 位的触点，解出 0 个
   对象，断言"y == 5536"失败。这条失败本身证明"载荷不够就不解触点"是对的；测试
   改成 6 字节载荷后通过。

### 7. 真机上要看什么

镜像带 `kebab_touch=1 kebab_touch_boot=1 kebab_touch_power=1
kebab_vrm_write=1 kebab_pon_write=1`，正常启动后按顺序看这几段：

**(1) 四个命令都有应答，且 app info 有 46 字节：**

```text
KEBAB TOUCH: command 0x20 answer: code 0x01, 46 payload byte(s), trailing byte 0x5a
KEBAB TOUCH: app info payload[0] ...
KEBAB TOUCH: app info: version 0x.... status 0x0000, max_x 1079 max_y 2399 objects 10 ...
KEBAB TOUCH: app info: max touch report config 128 bytes, max touch report payload 50 bytes
```

判据：`max touch report config` 至少 13（程序长度），`max touch report payload` 至少
5×max_objects；`max_x`/`max_y` 与设备树的 `touchpanel,panel-coords = <0x437 0x95f>`
（1079/2399）一致。对不上就先记下实际值，不要改代码去迁就。

**(2) 报告程序被接受：**

```text
KEBAB TOUCH: report program: 13 bytes, 40 bits per contact, 128 bytes sent
KEBAB TOUCH: report program[0] 01 06 04 07 04 08 0c 09 0c 0a 08 03 00
KEBAB TOUCH: command 0x26 answer: code 0x01, 0 payload byte(s), trailing byte 0x5a
KEBAB TOUCH: report config accepted: 40 bits per contact, z included
```

如果这里是 `answered code 0x0d (previous command pending)` 或 `0x0e`，看第 (1) 步
是否也失败：0x0d 通常是上一条命令的应答没读干净，新框架会把四个字节和 code 打进
日志。若退回 `vendor program without z`，说明带 z 的程序被拒，功能仍然可用但压力
恒为 0。

**(3) 第一条报告的长度是 5 的倍数：**

```text
KEBAB TOUCH: message code 0x11 len 5
KEBAB TOUCH: down obj 0 raw 540 1200 panel 540 1200 logical 562 1218 z 41
```

判据（这就是"数字对不对"）：

* `raw` 与 `panel` 完全相等（因为 max_x/max_y = 1079/2399，比例是 1:1）；如果是
  `1079 -> 540` 这种一半的关系，说明 app info 的 max 是 2158 之类，不是错；
* `logical = panel + (22,18)`，永远；
* 手指向右移动时 `raw x` 增大、`raw y` 基本不变；上下移动相反。**若左右移动改变
  的是 y，就是 x/y 的位偏移错了**；
* `z` 随按压力度变化（轻按小、重按大），抬起时 up 行 `z 0`；
* 两指时 `len 10`、两行 down；抬起一根时先 move 后 up，up 的坐标是它最后的坐标；
* 若出现 `report of N byte(s) discarded: ... too short for another`，说明实际布局
  与程序不符（常见：固件按 4 字节触点发、或 max_objects 比实际小）；若出现
  `report discarded: the payload and the program disagree`，是报文本身坏了（长度不是
  5 的倍数）。

**(4) 没有持续的错误行。** 正常触摸时不应反复出现 `padding ... not the vendor
0x5a`（偶尔一次说明固件用 0x00 填充，无害；每条都打印说明确实如此，把它记下来）、
`message header ... rejected`（流错位）、`no usable answer`（命令无应答）。

### 8. 边界与没有做的事

* **传输和电源序列一行没改**：`kebab_spmi_vrm_boot_power()`、TCS 状态机、时钟、
  复位、GENI 初始化、I2C 读写全部原样。补丁只改 TCM 层、报告解码和两条记录路径，
  因为那两处是实测过的。
* **没有 HID**。事件仍然只进 `/dev/kebab_touch` 的 48 字节记录，队列与自检不变。
* identify 的 payload 里那两个字节（`a5 03`）仍然不解释；解码器只保证 part 串不被
  错位。
* 本节的所有数字（193 项断言、合成样例、程序字节）都是主机上的结果。**板子上的
  验收还没做**：stranded，无法烧写。第 (1)–(4) 步就是板子回来后的验收清单。
