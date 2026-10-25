# 接手说明：kebab 移植当前状态

写于 2026-10-26 01:20。官方七阶段的前 6 阶段已完成，阶段 7 的核心门槛也已跨过。
本文档只写"现在是什么状态"和"卡在哪"，不复述历史。

## 一、已经打通的东西（有串口/屏幕日志为证）

```
KEBAB UFS: "userdata" on LUN 0: first block 0x2c7420, 229043 MiB, read-only
BSD root: ufsroot0, major 1, minor 0
hfs: mounted KebabRoot on device b(1, 0)
KEBAB: root volume is not sealed (error 25); continuing
bsd_init: done
bsd_init: bsd_do_post - done
KEBAB: load_init_program enter
load_init_program: attempting to load /sbin/launchd
KEBAB: exec_mach_imgact enter
KEBAB: load_machfile -> 0
KEBAB: exec P1 ～ P9（地址空间切换、旧 map 释放、线程状态、用户栈全部成功）
KEBAB: syscall 5 arg0 0x1000003c4
KEBAB: open1 enter
```

结论：内核从真实 UFS 存储上的 HFS+ 卷加载了 Mach-O、切换地址空间、建立用户栈，
并**成功返回用户态**；用户代码发出了第一个系统调用 `open("/dev/console")`。

## 二、补丁结构（72 个，全部可依序应用）

关键补丁：

- `0070` UFS 驱动（读 + 写，`ufsrw=1` 开关）＋ `/dev/kebabcon` / `/dev/ufsroot0`
- `0085` 根卷不认证也继续（无 SSV 链路）
- `0086` 根卷可读写（`rootrw=1`，允许脏的非日志卷）
- `0088` 内置 AMFI 接口（不授予任何 entitlement）
- `0090` 控制台 printf 修复（`_cnputs` 忽略 `disableConsoleOutput`）
- `0091` **临时探针**，纯打印，稳定后整条删除

## 三、当前唯一的阻塞

PID 1 调用 `open("/dev/console")` 时卡在 `open1()` 内部：进了 `open1`，但不再返回，
既不成功也不 panic。已经插桩确认：

```
KEBAB: syscall 5 arg0 0x1000003c4     ← syscall 5 = open，arg0 指向 "/dev/console"
KEBAB: open1 enter                    ← 进入 VFS，之后无输出
```

已知 `/dev/console` 的设备链是 `cnopen() -> cndev() -> km_tty[0]->t_dev ->
cdevsw[12].d_open = kmopen()`，而 `kminit()` 确实执行过（日志有
`bsd_autoconf: calling kminit`）。

## 四、想和 Codex 讨论的问题

1. **`open1` 卡在哪里最可能？** 在只有 `kminit()`、没有 IOKit `kmDevice`／
   `KeyboardOpen()` 的移植上，`kmopen()` 会不会阻塞（等 tty 行规程、等键盘驱动）？
   有没有已知的"没有显示/键盘驱动时 `/dev/console` 打不开"的坑？
2. **最小正确修法是什么？** 是给 km 驱动补一个能被 devfs 克隆的节点，还是让
   `cndev()` 在 `km_tty[0]` 不可用时回退到内核控制台（`cons_ops`），
   还是干脆不提供 `/dev/console`？
3. **我们自作主张加的 `/dev/kebabcon`（写 → 直接画帧缓冲）可以接受吗？**
   它会不会和将来真正的 `/dev/console`（launchd 需要 tty 作 stdio）冲突？
4. **阶段 7 之后应该先做哪一件？** 候选：
   - (a) 真 launchd：需要 dyld + libSystem + dyld shared cache，最小可行路径是什么？
   - (b) IOKit 图形：IOMobileFramebuffer / IOSurface，让 CoreAnimation 软件渲染能出画面。
   - (c) AppleKeyStore / AMFI 扩展：让带 entitlement 的进程能起来。
   请给一个带验收标准的有序计划。
5. **AMFI 桩的语义检查**：我们对 entitlement 查询一律返回失败。真 launchd 是否会
   因为缺少某个 entitlement 而在 exec/启动早期就退出？如果是，最小授予集合是什么？
6. **请复核 `0086`（根卷读写 + 脏卷例外）与 `0090`（printf 修复）**，看有没有
   明显错误或隐患。

## 五、硬件与构建事实（避免重复摸索）

- 设备：OnePlus 8T（kebab，SM8250，12 GiB，序列号 c073a488，已解锁）。
- 固件一律从 `boot_b` 读载荷，与实际活动槽无关。
- 内核：xnu-8796.141.3，arm64，16 KiB 页，DEBUG/DEVELOPMENT，`cpus=1`。
- 启动参数：`-v -enable_kprintf_spam startup_debug=1 serial=3 debug=0x14e cpus=1
  rd=ufsroot0 ufsroot=userdata ufsrw=1 rootrw=1 keepsyms=1`
- 根卷：Mac 上 `hdiutil` 生成的 64 MiB HFS+（非 HFSX、非日志），含
  `/sbin/launchd`（手写静态 arm64 Mach-O）与 `/dev` `/etc` `/tmp` 等目录。
- 关键约束：`__PAGEZERO` 必须覆盖低 4 GiB（`vm_map_has_hard_pagezero` 检查），
  ld64 默认布局即可。
- 构建：GitHub Actions（`macos-14`），约 8 分钟；产物
  `https://github.com/GFander/UniXNU/releases/download/kebab-latest/payload.img.gz`
- 刷机：`fastboot flash boot_b <96 MiB boot image>`、`fastboot flash userdata <root.img>`。
