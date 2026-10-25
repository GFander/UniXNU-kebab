# UniXNU-kebab

一加 8T（kebab，SM8250）的 UniXNU 设备仓库：**16 KiB 页**的 XNU 移植，目标是用
iPhone X 的身份启动 iOS 16.7.16 用户态。

仓库按[项目规范](https://unixnu.com/zh/contributing/repository)组织：设备仓库只放
板级描述与设备专属补丁集，每个驱动单独一个公开仓库、以子模块引用。

```
board.toml                   unixnu 读取的板级描述
configs/                     defconfig 与 iPhone X 的 /product 子树
patches/xnu-8796-kebab/      本机内核补丁集（52 个，按文件名顺序应用）
scripts/make-kebab-adt.py    设备树（ADT）生成器
drivers/KebabCPU/            子模块 -> GFander/KebabCPU（PSCI / 多核）
drivers/KebabEarlyConsole/   子模块 -> GFander/KebabEarlyConsole（面板控制台）
crypto-bearssl/              备用密码学补丁集（当前未启用，见文末）
docs/BRING-UP-STATE.md       详细的移植状态记录
```

克隆时加 `--recurse-submodules`。

---

## 1. Mu-Silicium 适配情况

固件在 `Mu-Silicium` 的 `Platforms/OnePlus/kebabPkg` 下，分支
`experiment/kebab-xnu-ramdisk`，产出 `Mu-kebab.img` / `SILICIUM_UEFI.fd`。

### 已完成的适配

| 项目 | 状态 |
| --- | --- |
| 设备包（`kebab.dsc` / `kebab.fdf` / `kebab.dec` / `DeviceBuild.py`）| ✅ |
| UFS 初始化不再致命，能正常进入 BDS | ✅ |
| 启动诊断（协议探测、USBCFG 候选）| ✅ |
| 只读 RAM FAT 卷（BlockIo producer）| ✅ |
| 把预编译的 `UniXNU.efi` / `UniXNUDxe.efi` / `DarwinBoot.efi` 打进 FV | ✅ |
| 从 `PlatformBootManagerAfterConsole` 注册 UniXNU 启动项 | ✅ |
| 配置卷 `\EFI\UniXNU\unixnu.toml`（菜单、Restart / Power off）| ✅ |
| `Reboot to bootloader` 菜单项，misc 分区靠解析 GPT 定位 | ✅ |
| 图形输出（GOP）：`0x9C000000`，1080×2400，行距 4320 字节，32 bpp | ✅ |
| USB（主机 / 设备 / USB-C）| ⚠️ 仅排入 APRIORI，未打通 |

### ⚠️ USB 尚未适配完成

USB 相关组件只是被**排进了 DXE APRIORI**，真机上还没有打通任何一条路径：

| 组件 | 现状 |
| --- | --- |
| `UsbfnDwc3Dxe` / `UsbDeviceDxe` / `UsbConfigDxe`（设备模式）| 仅排入 APRIORI，未验证 |
| `UsbBusDxe` / `UsbKbDxe` / `UsbMassStorageDxe`（主机模式）| 仅排入 APRIORI，未验证 |
| `UsbPwrCtrlDxe`、QMP PHY、Type-C 角色切换与充电 | 未适配 |

也就是说：**设备模式（fastboot / CDC-ACM 之类）、主机模式（U 盘、键盘）、
USB-C 角色切换三条路都还不通**。目前固件与内核的所有输出都依赖屏幕，没有
任何 USB 侧的输入/输出通道。

### 已知行为与限制

* **固件固定从 `boot_b` 读取载荷**，与当前活动槽位无关 —— 刷 bring-up 镜像时
  必须写 `boot_b`。
* 载荷卷位于 boot 镜像偏移 `0x800000`（`signed.img` 96 MiB，载荷拼接在此处）。
* 屏幕顶部那条**绿色横条是 DarwinBoot 的 `DarwinEnter` 画的**（表示内核入口代码
  已运行），不是内核输出。
* 固件自身的日志走 GOP 文本输出，速度较慢但可读。
* 暂未实现按槽位自动探测载荷卷；需要重新构建固件才能支持。

---

## 2. UniXNU 走到哪一步了

### 启动链

```
ABL → boot_b 上的镜像 → Mu-Silicium UEFI → UniXNU 菜单
    → DarwinBoot（校验输入、ExitBootServices、x0 = boot_args 物理地址）
    → 内核 _start（start.s）
```

### 内核已验证到达的位置

截至本文档，内核在真机上已经跑过：

| 阶段 | 状态 |
| --- | --- |
| `start.s`：引导页表、帧缓冲映射、开 MMU、KVA 交接 | ✅ |
| `arm_init` → `PE_init_platform` → `ml_parse_cpu_topology` → `cpu_init` | ✅ |
| `arm_vm_init`：建立正式内核 pmap、关闭 bootstrap TTBR0 | ✅ |
| `pmap_bootstrap` → `machine_startup` → `kernel_bootstrap` | ✅ |
| 引导线程启动、**第一次上下文切换**（`sched_startup`）| ✅ |
| `thread_daemon_init` / `thread_call` / `thread_bind` / `mapping_adjust` / `clock_service_create` | ✅ |
| `kdp_init` / `coredump handler` / `bootprofile_init` | ✅ |
| `sysctl` 阶段（phase 15）| ✅ |
| **`PE_init_iokit`**：IONVRAM 解析、"name" not a kext | ✅ |
| `early_boot`（phase 16）、内核 PRNG 注册 | ✅ |
| **XNU panic + stackshot + 栈回溯** | ✅ 完全可用 |
| `trust_cache_init`（缺 AMFI/Image4 时降级）| ✅ |
| `mac_policy_initmach` / `vnguard_policy_init` / `dtrace_early_init` | ✅ |
| `OSKextRemoveKextBootstrap` / `machine_lockdown` / `kernel_do_post` | ✅ |
| **`PE_lockdown_iokit`（内核自身初始化收尾）** | ✅ |
| **`KebabPlatformExpert` 匹配成功，IOKit 开始建 nub** | ✅ |
| `kperf_init_early`（`ml_set_max_cpus` 死锁已修）| ✅ |
| **`bsd_init()` 进入 BSD 层** | ✅ |
| `pthread_init()`（内置 libpthread-514.100.2）| ✅ |
| **UFS 驱动：找到 `userdata` 分区（229043 MiB），发布 `ufsroot0`** | ✅ |
| 挂载根文件系统 | ⛔ 当前阻塞点（内核里没有任何文件系统）|
| 根卷挂载（`ufsroot0`，UFS 驱动已就位）| ⬜ 待 pthread 之后验证 |

### 已修复的关键问题

1. **KVA 映射偏移 16 MiB** —— 16 KiB 粒度下 32 MiB 块的低 25 位来自虚拟地址，
   链接地址必须与物理加载地址的低位一致。内核改为链接在 `0xfffffe0006004000`。
2. **ARMv8.4 指令** —— SM8250 的 A77/A55 是 ARMv8.2，`mrs x, DIT` 会触发未定义
   指令异常，第一次上下文切换因此崩溃。已对 KEBAB 关闭 `__ARM_ARCH_8_4__`。
3. **没有 crypto kext** —— `g_crypto_funcs` 永远为 NULL。SHA-1/MD5/SHA-256/512
   改用 BearSSL 实现，内核 PRNG 用 HMAC-SHA256 DRBG，kmem 分配器用 SplitMix64。
4. **设备树缺节点** —— 补上 `interrupt-controller = "master"`、`timer`、`pram`
   与 `/chosen/embedded-panic-log-size`，`ml_init_timebase` 才真正执行。
5. **早期控制台** —— 手机没有可用串口，日志镜像到帧缓冲；`kprintf` 与 panic
   都直接打屏。
6. **异常向量** —— 早期自建向量表会吞掉中断，现已交还 XNU 的
   `ExceptionVectorsBase`，IRQ/FIQ 正常处理。
7. **缺少平台驱动** —— IOKit 找不到匹配 `oneplus,kebab` 的 `IOPlatformExpert`，
   兜底的 `IOPanicPlatform` 直接 panic。已加 `KebabPlatformExpert`
   （`0060-kebab-platform-expert.patch`）。
9. **没有文件系统** —— 开源树只带 `devfs`/`nullfs`/`mockfs`。HFS 需要 vendored 进内核，
   并且要阻止 `mockfs` 抢走根设备（它的 `vfc_mountroot` 会接受任何设备）。
10. **`ml_wait_max_cpus()` 永久睡眠** —— Apple 由 CPU 启动路径调用
   `ml_set_max_cpus()`，我们没有那条路径，`kperf_init_early()` 的第一句就永远
   等下去。改在引导线程里发布 `max_cpus`（`0062`）。
11. **探针会骗人** —— 早期内联汇编探针硬编码恒等映射地址，pmap 切换后自己崩掉，
    看起来像内核卡死。现在有原生 panic + 栈回溯，探针已全部移除。
8. **AMFI / Image4 缺失** —— trust cache 初始化改为降级为空运行时而不是 panic
   （`0059-kebab-trustcache.patch`）。

> 教训：探针本身也会骗人。早期把内联汇编探针硬编码到恒等映射地址，而
> `arm_vm_init` 之后该映射已关闭，探针自己崩溃、伪装成内核卡死。

### 当前阻塞

块设备层已经打通：`bsd_init` 走到 `vfs_mountroot()`，我们的 UFS 驱动：

```
ufs "userdata" on LUN 0, first block 0x2c7420, 229043 MiB, read-only
```

它读到了 UFSHCI 寄存器、发了轮询 SCSI 命令、解析了 GPT、用 READ CAPACITY 拿到容量。
（同时也证明了 SMMU 没有阻断内核的 DMA 缓冲区。）

失败点是 **`ENODEV`** —— 开源 xnu 树里**没有任何文件系统**（HFS/APFS 在 Apple 那边
都是 kext）。所以下一步是把 Apple 开源的 **HFS** 编进内核，做法与参考移植一致。

### 存储：UFS 根卷驱动已就位

`bsd/dev/kebab_ufs.c`（`0070`–`0072` 补丁）：复用固件留下的 UFS 主控状态，
轮询式发 SCSI 命令，按名字找到 GPT 分区并以 `ufsroot0` 暴露，供
`rd=ufsroot0` 挂根。**只读**。全部 DMA 走一块连续内存，并用 `dc civac`/`dc ivac`
手工维护缓存（8796 没有 `invalidate_dcache64`）。

---

## 3. 后续要跟进的工作

### 内核

1. **验证 trust cache 降级** —— 期望内核越过 BSD 初始化继续前进。
2. **AMFI / Image4 占位实现** —— 代码签名、entitlements 相关的调用需要一个最小
   可用的替身，否则用户态启动会被挡住。
3. **IOKit 平台驱动匹配** —— `AppleARMPE` 需要匹配的设备树节点；显示、UART、
   USB 控制器都还没有驱动。
4. **中断链路端到端验证** —— GIC 与虚拟定时器已初始化（`KEBAB GIC: virtual
   timer INTID 27`），但真正的定时器中断处理还没在真机上确认。
5. **多核** —— 目前 `cpus=1`；PSCI 与 GIC 重分发器需要在真机上逐个验证。
6. **内存窗口** —— 内核只管理 DarwinBoot 交出的 256 MiB，设备有 12 GiB，
   后续需要扩大窗口或让内核接管其余 DRAM。

### 固件（Mu-Silicium）

1. **USB 栈适配**（当前最大的固件缺口）：
   * 设备模式：`UsbfnDwc3Dxe` → `UsbDeviceDxe` → `UsbConfigDxe`，目标是
     fastboot / CDC-ACM 之类可用的下载与调试通道；
   * 主机模式：`UsbBusDxe` + `UsbKbDxe` / `UsbMassStorageDxe`，目标是键盘与
     U 盘枚举；
   * USB-C：`UsbPwrCtrlDxe`、QMP PHY、角色切换与充电。
2. 载荷卷**按活动槽位自动探测**（当前写死 `boot_b`）。
2. 把 panic 日志写入 `pram`，重启后可回读。
3. 菜单交互（音量键选择）与超时策略。

### 内核服务（当前阶段）

1. **libpthread 内置化** —— 把 `libpthread-514.100.2` 编进内核，填满
   `pthread_functions` 表，让 `pthread_init()` 与线程创建可用。

### 块驱动与根卷

**设计决定：不使用 RAMDisk。** 内核必须从真实块设备挂载根卷，而不是从内存镜像。
loader 里也不保留 `RAMDisk` 映射项。

1. **内核块驱动（下一步的重点）** —— 在 IOKit 里实现 UFS：
   * 控制器初始化：QMP PHY、时钟、电源域（需要内核侧的时钟/RPMh 支持）；
   * UIC 命令、描述符读取、传输请求队列；
   * 对外发布 `IOBlockStorageDevice` → `IOMedia`；
   * GPT 解析后应出现 `OS` / `SystemOS` 分区。
2. **根卷匹配**：`/chosen/root-matching` 的 UUID 要指向 `userdata` 里的系统卷，
   让 `vfs_mountroot()` 能选到它。
3. `launchd` 从该卷启动。

参考材料：Mu-Silicium 的 `UfsDxe`（UEFI 侧）里有完整的寄存器序列、PHY 与
UIC 配置，可以作为写内核驱动时的对照；但它本身是 UEFI 代码，不能直接搬进内核。

### 用户态
2. **kext 加载**：设备树 `/chosen/memory-map` 里的 booter kext 目前是空的，
   后续需要把 AMFI 等内核扩展塞进载荷。
4. **AMFI / Image4 最小实现**：代码签名与 entitlements 校验需要能通过。
5. 启动 `launchd` → 加载 iOS 16.7.16 的 `/System/Library` 组件。
6. 用 iPhone X（`D22AP`）的身份完成平台相关校验。

---

## 构建与刷机

内核与载荷由 GitHub Actions（`macos-dxb.yml`，`blacktop/darwin-xnu-build`）构建，
产物发布在 `GFander/UniXNU` 的 `kebab-latest` release：

```sh
# 载荷 -> boot 镜像
gunzip -c payload.img.gz > payload.img          # 必须正好 67,108,864 字节
python pack.py                                   # 拼接到 signed.img 的 0x800000

# 刷机（务必是 boot_b）
fastboot flash boot_b XNU-<n>.img
fastboot reboot
```

回到 LineageOS：`fastboot flash boot_a <lineage boot.img>`。

## 备用密码学补丁集

`crypto-bearssl/` 里是完整替换 `libkern/crypto` 包装的 BearSSL 补丁（SHA-1、
MD5、SHA-256/384/512、HMAC-DRBG、AES-CBC），应用、编译与 982 组测试向量均已
验证通过。当前内核只需要 SHA-1 与 PRNG，因此尚未启用；等后续阶段需要 MD5、
SHA-384/512 或 AES 时再应用（注意 `0050` 会取代现有 SHA-1 补丁）。
