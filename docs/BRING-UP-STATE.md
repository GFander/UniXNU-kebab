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
