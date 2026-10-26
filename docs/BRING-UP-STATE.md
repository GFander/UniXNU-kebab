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
