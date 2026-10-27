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
