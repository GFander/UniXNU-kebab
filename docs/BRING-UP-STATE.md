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
