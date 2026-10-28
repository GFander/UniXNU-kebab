# UniXNU-kebab 迁移指南（ios16）

> 这份文件是冷启动入口。先看第 0 节「现在在哪 / 下一步做什么」，再按任务查后面的参考。
>
> 快照：2026-10-28，分支 `ios16`，HEAD `98e5432`，122 个补丁，板子 stranded（无真机）。
> 事实以仓库为准：代码、`scripts/verify-all.sh`、`docs/BRING-UP-STATE.md`。本文与仓库冲突时，以仓库为准，并顺手修正本文。
>
> 证据分级如下（表格里的「证据」列用简写）：
>
> - **实测**：有真机日志或真机寄存器读数。
> - **主机验证**：在主机上跑过脚本、编译、真实文件校验，但没有上板。
> - **推断**：只按代码路径推出来的预期，没有运行证据。
> - **未实现**：代码或功能还不存在。
>
> 没有写「实测」的内容，不要当成已经通过。

---

## 0. 现在在哪

### 0.1 一句话

主线是 P1：把 16 GiB HFS+ 根卷、KIST v2 骨架、最小用户态和 44 个拆分共享缓存装到 `userdata`，然后去掉 `kebab_noinit` 启动，让真 `launchd` 跑起来。内核已经能启动到 BSD、挂 HFS+、exec `/sbin/launchd`；早期静态 init 已经返回过用户态。最近一次真机日志里，真 `launchd` 死在 exec 的 `copyout_strings`（namespace 9、code 0x6），补丁 `0140`/`0141` 只加了诊断和一处无关的 `status.c` 修复，**没有修 copyout 本身**。共享缓存映射（`0142`/`0143`）只做了主机和代码审查。板子从 `0133` 起就没有真机日志。

### 0.2 已核对的数字（2026-10-28）

| 项 | 当前值 | 怎么核对 |
| --- | --- | --- |
| 分支 / HEAD | `ios16` / `98e5432` `fix(dsc): make the shared-cache plan dump opt-in` | `git log -1` |
| 补丁 | `patches/xnu-8796-kebab/` 共 **122** 个文件（`0000` 加 `0001`–`0143`，编号有空洞），按文件名顺序应用到 `1b191cb`（`xnu-8796.141.3`） | `ls patches/xnu-8796-kebab/ \| wc -l` |
| 整树校验 | `scripts/verify-all.sh` **PASS**：7 passed，0 failed，4 skipped（缺本地 IPSW，属预期） | 直接运行；patch 项为 `122/122` |
| 内核 | `kernel.development.kebab` **17,208,944 B**，`XNU Build Done`，`kebab_dyldcache.c` 0 warning / 0 error | `docs/BRING-UP-STATE.md` 第七十三节第 8 小节；macOS VM 的 `build-local.sh` |
| 文档 | `docs/BRING-UP-STATE.md` 共 **75** 个二级标题（编号到 七十三，含 六之二/六之三 等插入节），526,783 B | `grep -c '^## '` |
| 板子 | **stranded**：COM7 可能还在枚举，但 bulk-OUT 超时、`fastboot devices` 为空，只有物理断电才能恢复 | 最近一次现场记录 |

最后一次真机证据覆盖：VRM 上电、触摸芯片身份、64 MiB 根卷上的安装通道。`0133`–`0143` 全部只有主机验证或编译验证。

### 0.3 下一步做什么

**A. 板子还没回来（现在就能做）**

1. `ssh ubuntualbert`，`cd /home/albert/kebab-dev-ios16`，`git fetch`，确认 HEAD 仍是 `98e5432`、补丁仍是 122 个。
2. 跑完整的 `./scripts/verify-all.sh`（不要加 `--fast`），确认 `122/122` 与 PASS。
3. 检查 `work/p1/` 的产物；不在就按 `scripts/make-root-volume.py`、`scripts/make-minimal-userland.py` 和 payload 打包流程重新生成（产物清单见第 3.2 节），再跑 `python3 scripts/bring-up.py --dry-run --work-dir work/p1`。
4. 读第 6 节的三个判定表。下一次上电是测量，不是继续盲改代码。
5. 不要按顺序通读 75 节文档；按第 7 节的子系统地图查。

**B. 板子回来后的第一条命令**

1. 物理断电：长按电源约 10 秒。这是 stranded 状态下唯一可靠的恢复手段，不要先试 KDP 或反复重刷。
2. 确认 `fastboot devices` 和 `fastboot getvar current-slot`（必须是 `b`）；没有 fastboot 时，让编排器用控制台发 `IMEM?` 再发 `FASTBOOT`。
3. 在连接板子的主机上跑 `python3 scripts/bring-up.py --from vol`。脚本默认 `COM7` 和 `D:\TOOL\platform-tools\fastboot.exe`；在别的机器上用 `--port` / `--fastboot` 覆盖。
4. 失败时先读运行目录的 `failure.txt`：checkpoint、expected、seen、最后 30 行控制台、两份日志路径都在里面。不要对同一个流盲目重试。
5. 成功时抓到 `launchd logging initialized` 或 zprint 的 `zone name` / `ZONE TOTALS`，把完整日志补进 `docs/BRING-UP-STATE.md`，再更新本文。

### 0.4 下一次启动会落在五类结果

| 结果 | 日志特征 | 含义 | 去哪里看 |
| --- | --- | --- | --- |
| 9/6 copyout | `namespace 9 ... code 0x6`，紧跟 `KEBAB EXEC: copyout stage=...` | exec 在往新用户栈拷字符串/指针时失败 | 第 6.1 节，`0140` 四行字段 |
| 9/e 平台门 | `namespace 9 ... code 0xe` | `unentitled_ios_sim_launch` 没有生效，或被命令行显式置 0 | 第六十六节；`0136` |
| namespace 6 code 1 | `initproc exited -- exit reason namespace 6 subcode 0x1` | exec 已过 copyout，dyld 找不到 `libSystem`，共享缓存没映射上 | 第 6.2 节 |
| launchd 真跑起来 | `launchd logging initialized` 或 zprint 的 zone 表 | P1 的门槛过了 | 第 4 节，下一步是 job/显示 |
| panic / 重启 | `panic(cpu ...)`、`KEBAB USB: panic console cannot be serviced ...`、新启动横幅 | `0134` 可能在按设计重启，不一定是新故障 | 第 6.3 节 |

不要把 `CS_KILLED` 当成默认解释。`0136` 已经证明 9/6 和 9/e 是两条与 CodeDirectory/信任缓存无关的路径；只有 panic 文本里出现 `CS_KILLED`，或 `load_cs`/`imgp_cs` 带 `CS_HARD`/`CS_KILL`，才回到签名线。

---

## 1. 30 分钟接手顺序

1. **5 分钟核对仓库**：`git log -5`、`ls patches/xnu-8796-kebab/ | wc -l`、`./scripts/verify-all.sh`。如果数字与第 0.2 节不同，先更新本文，不要继续按旧数字工作。
2. **10 分钟读关键节**：`docs/BRING-UP-STATE.md` 的 六十九（P1 序列）、七十（骨架更正）、七十二（copyout 诊断）、七十三（共享区审查）、六十四（panic 恢复）、六十六（exec 两条真实原因）。其余按第 7 节地图查。
3. **10 分钟核对产物**：`work/p1/` 下三张 boot 镜像、`KebabRoot.vext`、`skel.kist`、`base.kist`、`cache.kist` 是否齐全；跑 `--dry-run`。产物不在 git 里，缺了就是缺了，不会从 clone 自动出现。
4. **5 分钟确认约定**：第 9 节的 git、构建树、证据规则。特别是「同一时间只让一个 worker 占用 `~/kebab-build`」。

---

## 2. 仓库、机器与路径

| 用途 | 位置 |
| --- | --- |
| 仓库 / 构建入口 | `ssh ubuntualbert`，`/home/albert/kebab-dev-ios16`，分支 `ios16` |
| 纯净 xnu 树 | `/home/albert/ios-work/xnu8796`（`verify-all.sh` 默认；只读，补丁打到临时 clone） |
| macOS 构建 VM | `albertlee@192.168.30.165`，树 `~/kebab-build`，`build-local.sh`；macOS 14.6.1，Xcode 15.4，KDK 13.5 |
| 板子 / fastboot | 当前未固定；脚本默认 `COM7`、`D:\TOOL\platform-tools\fastboot.exe`，设备序列号 `c073a488` |
| 提取的 iOS 用户态 | `/home/albert/ios-userland`；拆分缓存在 `cryptex-cache/`，静态信任缓存 `static.trustcache` |
| 远程 | `github.com/GFander/UniXNU-kebab`，分支 `ios16` |

板子现在不在任何主机上可用。不要假设旧文档里的 Mac 刷机主机（`100.90.157.20` / `192.168.30.164`）仍然可达；先 `fastboot devices`，没有就断电重启，再判断。

### 2.1 哪份文档可信

从高到低：

1. **当前代码 + `scripts/verify-all.sh`**：唯一能证明「序列能应用、脚本自洽」的东西。
2. **`docs/BRING-UP-STATE.md` 的最新节**（六十三–七十三）：当前状态的主要记录。
3. **`scripts/bring-up-README.md`**：操作序列、检查点、退出码。
4. **`README.md` / `docs/ROADMAP.md`**：较早的总览，部分状态已过时。README 说「固件 USB 栈未适配」仍然成立（指 UEFI 侧），但内核侧 CDC-ACM 控制台早就可用，不要把两者混在一起。
5. **`RESUME.md`**：停在 72 个补丁、阻塞在 `open("/dev/console")`，不要拿它判断当前状态。
6. **`MIGRATION.md`（本文）**：快照。与仓库冲突时以仓库为准。

两个具体的过时说法，见到就改：

- `RESUME.md` 的「72 个补丁」「唯一阻塞是 open(/dev/console)」。
- `docs/ROADMAP.md` 的「S6SY761 / I2C 0x48」；第二十七节已更正为 **Synaptics S3908 / I2C 0x4b**。

### 2.2 git 与访问约定

- 所有 git 只在 Ubuntu 上做：`ssh ubuntualbert`。绝不在 Windows 上执行 git——那里的 credential helper 会弹 GUI 并挂起。
- 提交作者固定 `GFander <gfander@users.noreply.github.com>`，用 `git -c user.name=... -c user.email=... commit`，不改全局配置。
- 推送前 `git fetch origin`，再 `git rebase origin/ios16`；**永不强推**。
- 提交信息用英文 conventional commit。
- 构建树 `~/kebab-build` 是共享的：同一时间只让一个 worker 编译；改代码后先删 `build/xnu.obj/kernel.development.kebab` 这个符号链接，否则 `build.sh` 会跳过构建。

---

## 3. 硬件会话：精确操作序列

### 3.1 前置：先断电，再只信 fastboot

板子 stranded 的典型现场：CDC-ACM 仍然枚举，主机能打开端口，但每次 bulk-OUT 都超时；`fastboot devices` 为空；没有看门狗复位。原因可能是 panic 后控制台发送门被停住的 CPU 占住（`0134` 之前的死路），也可能只是上一次实验留下的状态。**只有物理断电（长按电源约 10 秒）能可靠恢复。**

恢复后：

- `fastboot devices` 是唯一可信的状态。COM 口存在不代表内核活着；内核 halted 时 PnP 条目仍然在。
- 如果已在 fastboot：直接开始。
- 如果不在 fastboot 但控制台有回答：发 `IMEM?` 确认命令通道，再发 `FASTBOOT`（需要 `kebab_pon_write=1`），40 秒内轮询 `fastboot devices`。
- 两者都没有：前置失败，回到物理断电；不要猜端口状态，也不要反复重刷。

`C01` 断言 fastboot 设备出现；`C02` 断言 `current-slot` 为 `b`。

### 3.2 产物：三张镜像、一条卷流、三条 KIST 流

`fastboot flash boot_b` 只能整张替换镜像；启动参数不在 Android boot 头的 `cmdline`（那里是空的），而在镜像偏移 `0x800000` 的 payload TOML 的 `args = "..."` 里。所以每个阶段必须有自己的镜像。

| 默认路径 | 内容 | 关键要求 |
| --- | --- | --- |
| `work/p1/boot-vol.img` | 写卷镜像 | 100,663,296 B；args 含 `kebab_vol=1` |
| `work/p1/boot-inst.img` | 安装镜像 | 同上，但**不含** `kebab_vol=1` |
| `work/p1/boot-final.img` | 最终启动镜像 | 不含 `kebab_vol=1`、不含 `kebab_noinit=1`；含 `kebab_dsc=1 kebab_dsc_map=1` |
| `work/p1/KebabRoot.vext` | 16 GiB 裸 HFS+ 写卷流 | 1,769,472 B；3 extents；1,744,896 data bytes；内嵌 HFS+ 头 4,194,304 块 |
| `work/p1/skel.kist` | 根卷骨架，KIST v2 | 16 条（10 dir + 3 link + 3 file）；153 B installed；65,536 B wire |
| `work/p1/base.kist` | 最小用户态，5 条 | 1,619,682 B installed；589,824 B wire（576 KiB） |
| `work/p1/cache.kist` | 拆分共享缓存，44 条 | 3,330,359,296 B installed；1,067,909,120 B wire（约 1018.44 MiB） |

补充：

- `KebabRoot.vext` 的 SHA-256 每次生成都不同（`newfs_hfs` 时间戳），但大小、extent 数、data 字节稳定；不要拿旧 SHA 硬套。`--dry-run` 校验的是后三者（`verify-all.sh` 不检查 VEXT）。
- 44 个缓存文件 = 43 个被内核映射的文件 + `.symbols`（内核只报告、不映射）；目录里另有 `MANIFEST.txt`，所以 `verify-all.sh` 的 shared-cache 项显示 45 files。
- `work/p1/` 不在 git 里。缺产物时先按 `scripts/make-root-volume.py`、`scripts/make-minimal-userland.py` 和 payload 打包流程生成；生成后先 `--dry-run`，不要直接上板。真机运行需要主机装 `pyserial`；`--dry-run` 和 fake-board 测试不需要。

### 3.3 阶段顺序

固定顺序：

```
vol -> install -> skel -> base -> cache -> final
```

| # | 阶段 | 镜像 | 做什么 |
| ---: | --- | --- | --- |
| 1 | `vol` | `boot-vol.img` | 刷入并重启；`VOL DRY` 校验流；`VOL BEGIN <wire> <volume> CONFIRM` 写入 16 GiB 卷；`REBOOT` 后要求 HFS+ 挂载 4,194,304 块 |
| 2 | `install` | `boot-inst.img` | 刷入并重启；`INST?` 要求 `root rw`、4,194,304 块、17,179,869,184 字节、free > 0 |
| 3 | `skel` | `boot-inst.img` | 发 64 KiB `skel.kist`；内核创建 10 个目录、3 个符号链接、3 个文件，并读回 uid/gid 0 与精确 mode |
| 4 | `base` | `boot-inst.img` | 逐条 `INST CHECK` 5 个目标；全部 same 则不发；否则发完整流，内核按条 `SKIP` |
| 5 | `cache` | `boot-inst.img` | 逐条 `INST CHECK` 44 个目标；全部 same 则不发；否则发完整流；记录耗时 |
| 6 | `final` | `boot-final.img` | 不带 `kebab_noinit` 启动；读到 launchd 或第一个失败 |

`skel.kist` 每次整条重发（一个 64 KiB chunk），因为 `INST CHECK` 只能描述文件，不能预检目录/符号链接。`base`/`cache` 只有在**每一条**都 same 时才跳过；部分完成仍发完整流，由内核逐条 `SKIP`。`--force-vol` 会跳过「已是 16 GiB 就跳过 vol」的保护，是破坏性操作；只在确实要重写卷时用。

常用入口：

```sh
python3 scripts/bring-up.py --from vol          # 完整序列
python3 scripts/bring-up.py --from install      # 卷已经是 16 GiB
python3 scripts/bring-up.py --from skel --to skel
python3 scripts/bring-up.py --from cache --to cache
python3 scripts/bring-up.py --from final
python3 scripts/bring-up.py --dry-run --work-dir work/p1
```

### 3.4 每阶段的启动参数

公共集（`board.toml` 的基础参数，加上编排器 `BASE_ARGS` 额外要求的 `kebab_pon_write=1` 与 `kebab_cs=15`）：

```
-v -enable_kprintf_spam startup_debug=1 serial=3 debug=0x14e cpus=1
rd=ufsroot0 ufsroot=userdata ufsrw=1 rootrw=1 kebabusb=1 keepsyms=1
kebab_pon_write=1 kebab_cs=15
```

阶段增量：

| 阶段 | 相对公共集 | 为什么 |
| --- | --- | --- |
| `vol` | `+ kebab_vol=1 + kebab_noinit=1 + kebab_install=1` | 打开 VOL 写门、把 init 停在核内、允许 INST 诊断；旧根只读 |
| `install` | `+ kebab_noinit=1 + kebab_install=1`，**不带** `kebab_vol` | 新卷按 `rootrw=1` 读写挂载；带 `kebab_vol` 会让新根又被强制只读 |
| `skel` / `base` / `cache` | 同 `install`，参数不变 | 都在安装镜像里跑 |
| `final` | **不带** `kebab_vol`、**不带** `kebab_noinit`；`+ kebab_dsc=1 + kebab_dsc_map=1` | 让 PID 1 真正 exec launchd，并让 DSC 在 first exec 时映射共享缓存 |

两个最容易漏的点：

1. `ufsrw=1` 是写路径的门，不是可选项。只写 `rootrw=1` 时 `kebab_vol_begin()` 会打印 `VOL refused: userdata is read-only; boot with ufsrw=1`，一个字节都不写。安装路径同理。
2. `kebab_cs=15` 不等于「签名问题已解决」。它只打开 trustcache/platform、entitlement、unsealed shared cache 四位；launchd 是否被杀仍取决于 cdhash 是否命中静态信任缓存。

`kebabkdp=1` 默认被编排器拒绝（除非 `--allow-kdp`）：它是唯一会恢复「panic 停在 KDP 等 LLDB」的参数，会破坏 `0134` 的自动重启语义。只有确实要挂 LLDB 时才用。

### 3.5 检查点：每个断言什么

| ID | 断言 |
| --- | --- |
| `C01` `precondition.fastboot` | `fastboot devices` 列出板子；否则控制台 `IMEM?` 有回答后发 `FASTBOOT`，40 秒内重新枚举 |
| `C02` `precondition.slot` | `fastboot getvar current-slot` 为 `b` |
| `C-ARGS` `precondition.args` | 镜像 payload 的 `args` 含本阶段必需参数，不含禁止参数（`kebab_vol`、`kebab_noinit`、`kebab_dsc*`、`kebabkdp`） |
| `C-REENTER` | 控制台 `FASTBOOT` 后确实进了 bootloader |
| `C-BOOT-BANNER` | 重启后出现 `KEBAB USB: event thread started, waiting for the host`（`running (DSTS ...)` 在 `ud.running` 之前打印，只上面板；见 BRING-UP-STATE 第七十九节） |
| `C-BOOT-NOINIT-*` | `KEBAB: kebab_noinit=1: staying in the kernel, no init program` |
| `C-VOL-STREAM` | VEXT 大小/3 extents/内嵌 HFS+ 头 `sig 0x482b version 4 bsize 4096 blocks 4194304` |
| `C-VOL-STATUS` | `VOL? gated 1 ... writable 1`，partition 行与磁盘卷头都对 |
| `C-VOL-OLD` | 3 行 `VOL OLD vh` + 32 行 `VOL OLD bytes`（512 B 旧卷头），`VOL DRY` 与 `VOL BEGIN` 各一次 |
| `C-VOL-DRY` | `VOL PLAN extents 3 bytes 1744896 wire 1769472 volume 17179869184`；dry 的结束行是 `VOL PLAN`，不是 `VOL DONE` |
| `C-VOL-WRITE` | `VOL NEW ... bsize 4096 blocks 4194304` 与 `VOL DONE extents 3 ...`；`VOL NEW` 的 512 B 是设备读回逐字节比较后的结果 |
| `C-VOL-REBOOT` | `KEBAB REBOOT: PEHaltRestart(kPERestartCPU)` |
| `C-VOL-MOUNT` | `KEBAB HFS: KebabRoot: ... total blocks 4194304`，这是新卷替代旧卷的挂载证据 |
| `C-INST-GEOM` | `INST? root rw`、`space ... of 17179869184`、`bsize 4096 blocks 4194304 free > 0` |
| `C-SKEL-STREAM` | `INST STREAM v2 skeleton entries 16 bytes 153 wire 65536` |
| `C-SKEL-ATTRS` | 16 条每条一行 `OK <path> ... mode 0%o uid 0 gid 0`；目录/文件 mode 必须精确（含 `01777`）；链接允许 `SKIP ... link exists`；uid/gid 不是 0:0 直接失败 |
| `C-{BASE,CACHE}-PRECHECK` | 每个目标一条 `INST CHECK`，报告 same/missing/different |
| `C-SEND-{SKEL,BASE,CACHE}` | `INST BEGIN ... write`、`INST ACK`、`INST DONE entries N ok+skipped=N ...`；不能出现 `INST DONE with ... failure(s)` |
| `C-VERIFY-{BASE,CACHE}` | 抽样（含最大 `.symbols`）的 size 与 SHA-256 与流头一致 |
| `C-FINAL` | 无 `CS_KILLED`、`initproc ...`、`pid 1 exited`、`Library not loaded`、`devfs_kernel_mount`、`KEBAB DSC: ... NOT mapped`；出现 `launchd logging initialized` 或 zprint 的 `zone name`/`ZONE TOTALS` |

### 3.6 失败输出、退出码与 resume

失败输出固定四段，并写进运行目录的 `failure.txt`：

```
checkpoint: vol.dry
expected:   VOL PLAN extents 3 bytes 1744896 wire 1769472 volume 17179869184
seen:       VOL PLAN extents 3 bytes 1744896 wire 1769472 volume 67108864
hint:       ...
last 30 console lines:
  | ...
run log:     work/p1/logs/run-.../run.log
console log: work/p1/logs/run-.../console.log
```

| 退出码 | 含义 |
| ---: | --- |
| 0 | 序列完成 |
| 2 | 前置失败（无 fastboot、无控制台、槽位不对、缺产物） |
| 3 | 检查点失败，或设备拒绝流 |
| 4 | 阶段中途检测到 panic/重启 |
| 5 | 控制台丢失且无法重开 |
| 6 | dry-run 验证失败 |
| 130 | 中断 |

panic 检测器在阶段 ready 之后 arm；以下任一成立即报 `PanicDetected`（退出码 4）：新的 `KEBAB USB: running`/`event thread started`/`Darwin Kernel Version`；`panic(cpu ...)` 或 `KEBAB USB: panic console cannot be serviced`；控制台重新枚举（`console reopened`）或消失超过 10 秒；`KEBAB REBOOT: PEHaltRestart returned ...`。`0134` 之后，「控制台消失几秒又出现并从头启动」本身就是 panic 恢复的证据，不是 ACK 慢。

resume 规则：

- 控制台 `INST?`/`VOL?` 报 4,194,304 块：`--from vol` 默认跳过 vol；`--from install` 直接从安装开始。
- 报 16,384 块：卷还是 64 MiB，必须从 vol 开始。
- 已在 fastboot、几何未知：`--from auto` 先刷 install 探测；小卷则回 vol 再重试 install。
- `base`/`cache` 逐条 precheck；全部 same 才跳过；部分完成发完整流，内核 `SKIP`。
- `skel` 没有 precheck，每次整条重发。

---

## 4. 现在什么是真的（按子系统）

| 子系统 | 现在到哪 | 证据 |
| --- | --- | --- |
| 引导链 / 内核 | ABL → UEFI → DarwinBoot → XNU `_start` → BSD → UFS/HFS+ → exec `/sbin/launchd`；早期静态 init 返回过用户态 | 真机日志（`0133` 之前） |
| 调试控制台 | CDC-ACM（`VID 1209` / `PID 0001`）、panic 报告约 8.9 KB、端口重开、`EP0?`、serial KDP、`REBOOT`、`FASTBOOT` | 真机日志（`0133` 之前） |
| panic 自动恢复 | `0134` 在 `panic_active()` 时无条件接管发送门；接管失败/控制器 dead 就 PSCI 重启；默认 `debug=0x14e` 不再等 KDP | 仅代码 + 编译，**未上板** |
| 根卷 / 安装通道 | 64 MiB 卷上的安装通道、单文件真写、吞吐已实测；16 GiB 卷（`0133`）、KIST v2 骨架（`0138`）只有主机/编译 | 64 MiB 为实测；16 GiB 为未上板 |
| P1 / launchd | 三阶段镜像、编排器、检查点、resume 规则齐全；真 `launchd` 最近死在 exec 9/6 copyout | 编排器为 fake-board 回环；9/6 为真机 |
| 共享缓存 | 44 个文件、43 个映射文件、盘上 51 条 mapping，`0142` 追加 1 条动态页 = 52 条；算术、UUID、slide-info、dyld 预期都已审 | 主机验证 + 代码审查，**无真机** |
| 代码签名 | `launchd`/`dyld` 的 cdhash 命中静态信任缓存；`0136` 把 KEBAB 的 `unentitled_ios_sim_launch` 默认置真；`9/e` 与 `9/6` 已分开 | 主机 cdhash 校验通过；平台门未上板 |
| 触摸 | S3908、I2C `0x4b`、两路 LDO 上电、application mode、身份读出；报告解码 `0135` 只有主机测试 | 身份/上电为实测；触点解码未上板 |
| 按键 | `0139` 电源键 + PM8150 GPIO6/7 音量键，`/dev/kebab_keys` | 主机测试 + 完整构建，未上板 |
| 显示 | IOMFB/IOSurface ABI 补到 backboardd 子集；逻辑 1125×2436、物理 1080×2400；`SwapEnd` 入队 | 编译 + ABI 审查，未在真实 backboardd 验证 |
| 图形 | 决定走软件合成，不追 AGX；保留 CPU 拷贝的 IOMFB | 决策文档，**未实现** |
| 固件侧 | RPMh/EBS 探针源码与主机测试；固件 EDK2 构建、刷写未做；UEFI USB 栈仍未适配 | 主机/静态，未上板 |

---

## 5. 硬学到的教训（不要再踩）

### 5.1 固件在活动槽，载荷在 GPT `boot_b`

ABL 从**当前活动槽**加载 boot 镜像（Mu-Silicium 固件）。固件里的载荷加载器从 GPT 分区 **`boot_b`** 的偏移 `0x800000` 读 64 MiB 载荷，目前写死，不按活动槽自动探测。

后果：只把固件刷到非活动槽，`fastboot` 返回成功，但没有任何代码从那里运行；把固件刷到活动槽 `a` 而载荷仍在 `boot_b`，固件变了、内核没变。两种情况下行为都不变，看起来像「刷了没用」。

正确做法：`fastboot getvar current-slot` 必须是 `b`；整张 96 MiB 镜像刷 `boot_b`。判据是启动横幅、payload `args`、内核版本，不是 fastboot 的退出码。回 Android 时才刷 `boot_a` 的 Lineage 镜像；bring-up 期间不要动它，这是回退约定。

### 5.2 `PE_halt_restart` 是 NULL

`iokit/Kernel/IOPlatformExpert.cpp` 里只有 `int (*PE_halt_restart)(unsigned int type) = NULL;`，全树无人赋值；`IOPlatformExpert::haltRestart()` 在 NULL 时直接 `return -1`。所以早期日志打印了 `MACH Reboot` 却不重启。

`0105` 在 `KebabPlatformExpert::haltRestart()` 里用 PSCI `SYSTEM_RESET`（`0x84000009`）补上 `kPERestartCPU`；`0134` 又把 `kPEPanicRestartCPU` / `kPEPanicRestartCPUNoCallouts` 接进同一条路。只加 `PEHaltRestart()` 调用而不补后端，仍然是空操作。

### 5.3 `ml_dbgwrap_cpu_is_halted` 在本板永远返回 FALSE

它只读 Apple CoreSight UTT 窗口的 `DBGWRAP.DBGACK`。KEBAB 的 ADT 由 `scripts/make-kebab-adt.py` 生成，CPU 节点没有 `coresight-reg` / `cpu-IMPL`，`configure_coresight_registers()` 不映射 `CORESIGHT_UTT`；函数在 `coresight_base[CORESIGHT_UTT] == 0` 这一句就返回 `FALSE`。而且它表达的是「外部调试器 halt 过」，与 panic 停其它 CPU 的机制本来就不重合。

旧 `ud_panic_takeover()` 要求「owner 是自己，或 owner 已被证明 halt」，于是永远拒绝接管；控制台端口仍然枚举，但没人服务端点，每次 bulk-OUT 超时，板子一停几十分钟。`0134` 的修法：`panic_active()` 为真时跳过该检查，只要 `ud.owner`/`ud.rx_owner` 非零就做一次 core reset 并无条件接管；接管失败累计约 1 秒、控制器 dead、控制台未 running 时，打印原因并 PSCI 重启，不再停车。

### 5.4 拆分缓存缺了 dyld 无条件要读的动态配置页

dyld 的 `reuseExistingCache()` 只要 `shared_region_check_np()` 成功，就会把 `cacheBaseAddress` 当 `DyldSharedCache*`，并立刻读 `base + dynamicDataOffset` 上的 `dyld_cache_dynamic_data_header`。原计划 51 条 mapping 的 extent 是 `0xaade4000`，缓存头声明的 `sharedRegionSize` 是 `0xaade8000`，差的正好是这一页 `0x4000`。缺了它，dyld 在第一次碰缓存时就落在空页上，不是「退回文件系统」，而是 bad access。

`0142` 在主缓存 `files[0]` 的 mapping 数组**第二条**补一条 `r--`、`VM_PROT_ZF` 的匿名零页：`0x180000000 + 0xaade4000 = 0x22ade4000`，大小 `0x4000`。不能放第 0 条：`sr_first_mapping` 会变成 `0xaade4000 + slide`，dyld 算出的 slide 就全错。

### 5.5 `sms_slide_size` 从来没有被赋值

`kebab_dsc_read_mappings()` 记录了 `sms_slide_start`，却没写 `sms_slide_size`；`kebab_dsc_apply_slide()` 只在 `sms_slide_size != 0` 时才置上 `VM_PROT_SLIDE`，`vm_shared_region_map_file()` 也只按这一位统计需要 rebase 的 mapping。结果非零 slide 时四条数据 mapping 都是普通文件映射：地址滑了、`sr_slide` 也滑了、pager 没装，`__DATA` 里的指针从未 rebase。

`0137` 补上 `sms_slide_size = size`。这个 size 必须精确等于 blob 大小：太大，`copyin()` 越界返回 `KERN_INVALID_ADDRESS`；太小，`vm_shared_region_slide_sanity_check_v2()` 要求 `40 + (page_starts + page_extras) * 2 <= size`，少一个字节就失败。四块的真实大小恰好等于这个下界。

### 5.6 控制台 ring 只有 16 KiB，计划日志曾吃掉 70%

`0142` 的逐条计划日志在真实 44 个缓存文件上重放是 **102 行 / 11,560 B**；整条 `kebab_dsc_map=1` happy path 是 **118 行 / 13,294 B**，超过 16 KiB ring 的 70%。一次启动就能把 panic 文本挤掉。

`0143` 把逐文件/逐 mapping 表、成功侧 header 校验行、slide-info 解析行移到 `kebab_dsc_verbose=1` 之下，默认只留一行计划摘要：`kebab_dsc_map=1` happy path 6 行 / 652 B，dry run 3 行 / 360 B；计划段从 102 行降到 1 行（154 B）。失败路径不省：计划被拒或映射失败时仍完整 dump。真机第一次仍建议先 dry run 看摘要，再开 `kebab_dsc_map=1`。

### 5.7 Ubuntu 的 `/tmp` 是 quota 限制的 tmpfs

往 `/tmp` 写日志/临时文件会失败，程序可能以 **120** 退出；看起来像「五个检查同时坏了」，其实是 scratch 目录写不进去。用 `$HOME` 或 `$HOME/.cache`。`scripts/verify-all.sh` 已经按这个规则选 scratch：`TMPDIR` 不可写就退到 `$HOME/.cache`。

### 5.8 构建 VM 和 Linux 主机都没有免密 sudo

macOS VM `albertlee@192.168.30.165`（macOS 14.6.1 23G93）：`sudo -n true` 返回 `sudo: a password is required`；`/bin/true`、`hdiutil`、`newfs_hfs`、`fsck_hfs`、`chown`、`chmod` 的 NOPASSWD 规则全部要求密码；`/etc/sudoers.d/` 为空。Linux 主机也没有免密 sudo，且没有 `mkfs.hfsplus` / `fsck.hfsplus`，根分区只剩 1.7 GB。

无 root 可以 `hdiutil attach -nomount` + `newfs_hfs`（设备属主是 `albertlee`），但 `chown 0:0` 一定 `EPERM`。所以根卷骨架不能由主机侧写入；`0138` 改成 KIST v2 + `skel.kist`，由内核在写卷之后、`base.kist` 之前以 uid 0 创建。裸卷根目前仍是 `501:20`，这是已知的唯一属主缺口。

### 5.9 macOS `/usr/bin/python3` 是 3.9

部分仓库脚本在 3.9 下会失败。用 Homebrew 的 python3（`/opt/homebrew/bin` 在 PATH 前），不要用 `/usr/bin/python3`。构建脚本的 PATH 里同时有 `$HOME/Library/Python/3.9/bin`，那是给别的工具用的，不代表 `/usr/bin/python3` 够新。

### 5.10 `build.sh` 看到符号链接就跳过构建

只要 `build/xnu.obj/kernel.development.kebab` 这个符号链接存在，`build.sh` 就认为已经构建过，直接跳过。改完代码不删它，会拿到旧内核，还以为编译了。每次完整构建前：

```sh
rm -f "$HOME/kebab-build/dxb/build/xnu.obj/kernel.development.kebab"
```

然后确认日志里有 `CC` 和 `XNU Build Done`，并核对产物的字节数/时间戳。

### 5.11 其他操作陷阱

- **启动参数在 payload TOML，不在 Android boot 头**。改参数必须重打 payload/镜像，不能像 cmdline 那样只改一个字段。
- **控制台命令必须在一次 bulk OUT 里发完**。`PANIC!`、`REBOOT`、`FASTBOOT`、`IMEM?`、`INST?`、`VOL?` 都是按一次完成的缓冲区匹配；拆成两次写不会触发。
- **`ufsrw=1` 是 VOL/INST 写路径的门**，不是可选项；只写 `rootrw=1` 会得到 `userdata is read-only`。
- **`FASTBOOT` 需要 `kebab_pon_write=1`**。没有它，控制台会明确回答 `disabled`；这时只能手动进 fastboot 或重打镜像。
- **`kebab_cs=15` 不是签名豁免**。它只打开四个具名位；launchd 是否被杀仍看 cdhash 与信任缓存。
- **`kebab_dsc=1 kebab_dsc_map=1` 是最终启动的硬要求**。缺了它，症状是 `dyld[1]: Library not loaded` / `KEBAB DSC: ... NOT mapped`，不是签名失败。
- **不要用 KDP 在 panic 现场恢复执行**。改 PC、写 NOP 会破坏保存状态，板子挂死；panic 之后正确做法是复位重启。
- **不要同时占用构建树**。`~/kebab-build` 是共享的；两个 worker 同时 `build.sh` 会互相覆盖产物。编译前先问/看有没有别人在跑。

---

## 6. 下一次启动的三个诊断

### 6.1 exec copyout（`0140` 的四个字段）

`0136` 的失败行仍然以 `KEBAB EXEC: copyout stage=...` 开头；`0140` 在后面最多再加四行（只在失败时打印，约 400 B）：

```text
KEBAB EXEC: copyout map cur=0x... alloc=0x... same=1 pid=1 recpid=1
KEBAB EXEC: copyout fault far=0x... esr=0x... dfsc=0x7 wnr=1 fast=-1 vm=1
KEBAB EXEC: copyout pmap va=0x... tte=0x0 lvl=3 valid=0 af=0 ap=0 uxn=0 pxn=0
KEBAB EXEC: copyout checks user_eq_alloc=1 str_eq_user=1 argc_ge_guard=1
```

字段含义：

- **map 行**：`cur` 是 `current_map()`（copyio 实际用的 map）；`alloc` 是 `create_unix_stack()` 记录的 map；`same` 是两者指针相等；`pid`/`recpid` 是当前进程和记录里的进程。记录只有一格，**`recpid != pid` 时 map 行和 checks 行的几何属于另一次 exec**，`same` 没有意义；fault/pmap 仍描述本次失败。
- **fault 行**：`far`/`esr` 是异常的 `FAR_EL1`/`ESR_EL1`；`dfsc = esr & 0x3f`；`wnr = (esr >> 6) & 1`（1 = 写）；`fast` 是 `arm_fast_fault()` 返回值，`-1` 表示没调用；`vm` 是 `vm_fault()` 返回值。没记录到 fault 时打印 `copyout fault none`。
- **pmap 行**：`tte` 是叶级表项原值；`valid` 是 bit0；`af` 是 bit10；`ap` 是 bit6–7；`uxn`/`pxn` 是 NX 位；`lvl` 是走到哪一层断的（L1–L3）。压缩页会显示 `tte=0x8000...` 且 `valid=0`。
- **checks 行**：三条包含关系，`user_eq_alloc`（`p->user_stack` 等于分配基址）、`str_eq_user`（字符串区末尾等于 `user_stack`）、`argc_ge_guard`（argc 槽不低于 guard 基址）。

判定表（先看 `checks`，再看 `same`/`fault`，最后看 `valid`/`af`/`ap`）：

| `checks` | `same` / `fault` | `valid` / `af` / `ap` | `dfsc` | `fast` / `vm` | 结论 |
| --- | --- | --- | --- | --- | --- |
| 有 0 | — | — | — | — | **几何错**：先修 `USRSTACK64` 对齐、ANYWHERE 回退或 guard 长度，地址可能都没意义 |
| 全 1 | `same=0` | — | — | — | **map 不对**：copyout 跑在别的 map 上，栈的分配不在这里 |
| 全 1 | `fault none` | — | — | — | **没碰到页表**：`EFAULT` 来自 `copy_validate()` 的范围检查；通常伴随 `same=0`，先查 map |
| 全 1 | `same=1` | `valid=0` | `0x4`–`0x7` | `vm=1` | **翻译缺失**：VM map 里有分配，pmap 没建表项；查 `mach_vm_allocate_kernel()` → `pmap_enter()` |
| 全 1 | `same=1` | `valid=1, af=0` | `0x9`–`0xB` | `fast=5`，`vm=1/2` | **AF 没置**：表项在，access flag 是 0；查 `pmap_enter()` 的 AF 路径 |
| 全 1 | `same=1` | `valid=1, af=1, ap=0/2/3` | `0xD`–`0xF` | `fast=5`，`vm=2` | **权限**：`ap` bit0=0 表示 EL0 不可访问（0/2），`ap=3` 是 EL0 只读而本路径是写；guard 或写保护映射是典型 |
| 全 1 | `same=1` | `valid=1, af=1, ap=1` | 任意 | `vm=1/3/5/9` | **取页失败**：表项允许 EL0 读写，但 VM 层没解决。`1`=invalid address，`3`=no space，`5`=failure，`9`=memory failure |

`ap` 取值：`0` = EL1 读写 / EL0 不可访问；`1` = EL1/EL0 读写；`2` = EL1 只读 / EL0 不可访问；`3` = EL1/EL0 只读。用户可写页必须是 `1`。

如果这次 copyout 成功，不要以为 P1 就过了：按 `0136` 的推断，接下来是 dyld 的 `abort_with_payload()`，namespace **6** code **1**（`libSystem` 只在尚未映射的共享缓存里），然后才轮到 6.2。

### 6.2 共享缓存映射（第七十三节第 4 小节的 dyld 预期）

dyld 拿到共享区之后，`loadDyldCache()` 的第一条路是 `reuseExistingCache()`：只要 `shared_region_check_np()` 成功，它就把共享区基址当缓存，并按下面这些条件检查。内核必须同时满足：

| dyld 读什么 | 在哪 | 要求 | 内核实际给出 |
| --- | --- | --- | --- |
| `shared_region_check_np()` 返回值 | `DyldDelegates.cpp:270`、`SharedCacheRuntime.cpp:790` | 0（非空区） | 0；区为空时返回 `ENOMEM` |
| 缓存基址 `cacheBaseAddress` | 同上 | `0x180000000 + slide` | `sr_base_address + sr_first_mapping` |
| `DyldSharedCache::slide()` | `common/DyldSharedCache.cpp:822` | `base - mapping[0].address` | `(0x180000000 + slide) - 0x180000000 = slide` |
| `Atlas::SharedCache::_slide` | `common/ProcessAtlas.cpp:889` | `base - sharedRegionStart` | 同上，`sharedRegionStart` 也是 `0x180000000` |
| `base + dynamicDataOffset` 可读 | `DyldDelegates.cpp:275`、`SharedCacheRuntime.cpp:799` | 必须已映射 | `0142` 后是 `0x22ade4000` 的零页，`0x4000` 字节 |
| `strcmp(magic, "dyld_v1   arm64")` | `SharedCacheRuntime.cpp:247` | 匹配 | 缓存第一页原样映射 |
| `header.cacheType` | `SharedCacheRuntime.cpp:795` | 2（multi-cache）不拒绝 | 2 |

两个 slide 算法要同时等于真 slide，等价于要求 `mapping[0].address == sharedRegionStart`。这就是「计划基址必须等于缓存头声明」的具体形式，不是美观问题。`sr_first_mapping` 又由「第一条成功建立的 mapping」决定，所以动态页必须追加在主缓存 mapping 数组的第二条，不能放第 0 条。

判读：

- **`KEBAB DSC: plan 43 files / 52 mappings, ... dynamic config page appended`**：`0142` 生效；52 = 盘上 51 条 + 1 条动态页。
- **dyld 打印 `mapped cache does not contain dynamic config data`**：这是**最强的好消息**。它证明 dyld 已经越过 `check_np()`、读到了 `base + 0xaade4000` 这一页、并且没有崩；内容是零，所以 magic 不匹配，dyld 继续跑。
- **`existing shared cache in memory is not compatible`**：magic 或基址不对；随后 dyld 会尝试 `mapSplitCacheSystemWide()`，但共享区已被占，syscall 返回 `KERN_FAILURE`，最后退回逐个 dylib 从文件系统加载，表现是一堆 `file not found`——看起来像缺文件，其实是映射没人认。
- **bad access 在 `0x22ade4000` 附近**：`0142` 没生效或动态页没映射。
- **bad access 在 `0x180000000` 附近**：主缓存第一页没映射上。
- **`HALF MAPPED`**：`vm_shared_region_map_file()` 失败但区里还有映射，内核 undo 没清干净；这是必须先修的内核问题，不是移植层的。
- **默认日志预算**：`0143` 后 `kebab_dsc_map=1` happy path 6 行 / 652 B，dry run 3 行 / 360 B；逐条表要 `kebab_dsc_verbose=1`。失败路径仍完整 dump。不要把「日志变少」当成「检查变少」。

### 6.3 panic 恢复（第六十四节第 5 小节的日志判据）

默认 `debug=0x14e`、不带 `kebabkdp=1`：panic 不再等远程调试器。`0134` 之后按下面的签名判读：

| 情形 | 日志签名 | 含义 / 下一步 |
| --- | --- | --- |
| A. 控制台活下来，发送门本来是空的 | panic 报告从原 ttyACM 出来；EP0 dump 里 `fences` 不变、`takeover=0`、`owners=0/0/<panic cpu+1>`；`IMEM?`/`REBOOT` 直接有回答 | 最干净的情况；不需要重开端口 |
| B. 控制台活下来，但发送门被留住过 | 主机看到一次 USB 断开/重连（同一 VID/PID，`/dev/ttyACM0` 可能改名）；重开后 `fences` 比 panic 前多 1、`takeover=<非零位图>`、`owners=0/0/<panic cpu+1>`；panic 文本可能重连后才到 | `0134` 的 core reset 生效；重新打开端口即可，`IMEM?`/`REBOOT` 正常 |
| C. 重启了（控制台不可服务） | 先看到 `KEBAB USB: panic console cannot be serviced (...); rebooting instead of parking`、`KEBAB PANIC: no console to service; rebooting via PSCI SYSTEM_RESET`、`KEBAB REBOOT: PEHaltRestart(kPERestartCPU)`；随后从头启动 | `0134` 按设计重启；控制器 dead 时可能一行都发不出，「几秒内重新枚举 + 新 boot 日志」本身就是证据 |
| D. 仍然卡住（只应在固件拒绝复位时） | `KEBAB REBOOT: PEHaltRestart returned <err>, the machine did not reset` / `KEBAB USB: panic reboot returned; the machine did not reset` | 只有这里才需要物理断电；普通 panic 不应走到 |
| E. 显式 `kebabkdp=1` | `Using serial KDP.` + `Waiting for remote debugger connection.`，然后停住等 LLDB | 这是显式请求的预期行为，不是缺陷复发；判断前先看启动参数 |

启动参数与 panic 行为对照：

| 启动参数 | `panicDebugging` | panic 之后 |
| --- | --- | --- |
| `debug=0x14e kebabusb=1`（默认） | FALSE | 控制台可用则进 panic 泵；发送门被留住时 reset + 重连；控制台不可用则 PSCI 复位 |
| 默认 + `kebabkdp=1` | TRUE | 先 trap KDP，打印 `Waiting for remote debugger connection`，等 LLDB；不自动重启 |
| `debug=0x14e` 但无 `kebabusb=1` | FALSE | 没有控制台可服务，panic 重启类型直接 PSCI reset |
| `debug=0x14f ...` | FALSE，但 `halt_in_debugger=1` | `DB_HALT` 路径未改；配合 `kebabkdp=1` 时仍在调试器停住 |
| 任意 + `DB_REBOOT_ALWAYS` | 不变 | 在 KDP 之前就重启 |

`0134` 只有代码和编译验证，没有真机；上面的 A–E 是代码路径的预期，不是实测。

---

## 7. 文档地图：75 节，不要顺序读

`docs/BRING-UP-STATE.md` 从 一 到 七十三，中间有 六之二、六之三 等插入节，共 75 个二级标题。按子系统读：

| 子系统 | 先读这些节 | 说明 |
| --- | --- | --- |
| 引导链 / 内核基线 | 一、二、三 | 已验证到哪、按发现顺序修过什么 |
| 调试手段 / 探针 | 四、五 | 帧缓冲控制台、`KEBAB EARLY`、探针自身会崩的教训 |
| USB CDC / 早期 bring-up | 六之三、八（USB 调试串口与 panic 捕获）、十五 | CDC-ACM、EP0 重写 |
| panic / KDP / 重启 | 十、十七、十八、六十四 | KDP 线上格式、`REBOOT`/`FASTBOOT`、`0134` 自动恢复 |
| fastboot / PSCI / SPMI / PON | 十九、二十、二十一、二十二 | PMIC PON、`SYSTEM_RESET2`、SPMI arbiter |
| 触摸 | 九、十三、二十四–二十九、六十一、六十五 | 从 S6SY761 误判到 S3908/0x4b、上电、身份、报告解码 |
| 显示 / 图形 | 十一 + `docs/PANEL-DSI.md` + `docs/GPU-ASSESSMENT.md` | IOMFB ABI、面板、软件合成决策 |
| 代码签名 / exec | 十二、六十六、七十二 | AMFI 策略、9/6 与 9/e、`0140` 取页诊断 |
| 共享缓存 | 十四、十六、六十七、七十三 | 计划、slide、算术验证、代码审查、日志预算 |
| 根卷 / 安装通道 | 六十二、六十三、六十八、六十九、七十 | KIST、VEXT、16 GiB 卷、最小用户态、P1 序列、骨架更正 |
| 按键 | 七十一 | PM8150 PON + GPIO6/7、`0139` |
| 固件 / RPMh / VRM / EBS | 二十三、三十–六十 | 构建警告、RPMh TCS、VRM、EBS 探针、固件日志 |

最短路径：P1 任务读 **六十九 → 七十 → 七十二 → 七十三 → 六十四 → 六十六**；触摸任务读 **二十七 → 六十一 → 六十五**；固件任务读 **五十九 → 三十–五十八**。

---

## 8. 真正未知的（只列硬件才能回答的）

### 8.1 下一次上电会回答

1. **16 GiB 根卷**：`0133` 的 `VOL DRY`/`VOL BEGIN`/`VOL NEW`/`REBOOT` 挂载是否在真机上成立；旧 64 MiB 卷的 512 B 头是否按预期打印。
2. **KIST v2 骨架**：`0138` 的 `vn_create` + `vnode_setattr`、`01777` 持久化、`VNOP_SYMLINK`、`vnode_getattr` 读回是否在真实 HFS+ 上成立；裸卷 `/` 仍是 `501:20` 是否影响 launchd。
3. **USB 流控与 3.3 GB 传输**：64 KiB chunk/ACK、`INST DONE`、真实耗时与失败恢复；`skel.kist` 从未走过 USB。
4. **真 launchd 的 exec**：9/6 copyout 是否仍在；若在，`0140` 的 `checks`/`same`/`valid`/`af`/`ap` 落在哪一行；若不在，9/e 是否真的被 `0136` 的平台门消掉；再往后是 namespace 6/1 还是别的。
5. **共享缓存映射**：dyld 是否接受 52 条 mapping、是否读到动态页、`check_np` 的 base/slide 是否匹配；`slide != 0` 的路径（当前默认 0）也从未上机。
6. **panic 恢复**：`0134` 的 A–E 哪一种发生；发送门竞态是否真的被 core reset 覆盖；PSCI 重启是否可靠。
7. **触摸报告**：`0135` 的 TCM 报告解码是否给出正确触点；`0139` 的电源键/音量键与 RESIN 实际接线。
8. **显示**：`0093` 的 IOMFB/IOSurface 选择器是否被真实 `backboardd` 接受；`SwapWait` 单位、83 的输出类型、`kIOReturnUnsupported` 是否被接受。

### 8.2 更后面、但仍是硬件才能回答

- 真实 `backboardd`/SpringBoard 能否在软件合成路径上出画面；DPU/DSI 直驱是否可行。
- `cpus>1` 时 PSCI/GIC 重分发器是否一致；内核目前只管理 256 MiB，12 GiB DRAM 的窗口如何扩。
- 固件侧 USB 栈（设备模式、主机模式、Type-C 角色切换）在真机上的行为。

### 8.3 已知未实现（不是「未知」）

- DPU/DSI 直驱、GPU 加速（AGX/Adreno 原生）、IOHIDFamily、用户态触摸消费者。
- 固件 EDK2 完整构建、固件 USB 栈适配。
- `launchd` 之后的 daemon/SpringBoard 启动链。

这些是「还没做」，不要写成「不知道」；它们的阻塞不是缺数据，而是缺实现或优先级。

---

## 9. 证据规则与工作约定

### 9.1 必须区分「实测 / 主机验证 / 推断 / 未实现」

每个报告、每次交接、每条提交信息，都要能回答：

- 这是真机日志证明的，还是主机脚本证明的，还是只按代码推的？
- 如果是真机，日志在哪、哪一行、什么参数？
- 如果是主机，跑了哪个脚本、输入是什么、输出是什么？
- 如果没有，明确写「未验证」，不要用「应该可以」代替。

不允许在没有真机日志时声称某个功能「可用」「通过」「已修复」；只能说「编译通过」「主机验证通过」「代码路径如此」。`verify-all.sh` PASS 只证明补丁序列和主机脚本自洽，不证明硬件。

### 9.2 回复与符号

- 回复用中文。
- 不要用对勾/叉号这类符号；用 `PASS`/`FAIL`、`是`/`否`、文字结论。
- 技术名词、命令、日志原文保留英文。

### 9.3 git

- 只在 Ubuntu 上做 git：`ssh ubuntualbert`。Windows 上的 credential helper 会弹 GUI 并挂起。
- 作者固定 `GFander <gfander@users.noreply.github.com>`：

```sh
git -c user.name=GFander -c user.email=gfander@users.noreply.github.com commit -m "..."
```

- 推送前 `git fetch origin`，`git rebase origin/ios16`；永不强推。
- 提交信息用英文 conventional commit。

### 9.4 构建与并发

- `~/kebab-build` 是共享构建树；同一时间只让一个 worker 编译。
- 编译前删 `build/xnu.obj/kernel.development.kebab` 符号链接。
- 不要在别人的构建窗口里重置树或改全局配置；需要独立树时先说明。

### 9.5 硬件操作

- 刷机只写 `boot_b`；`boot_a` 保留 Lineage 回退镜像，bring-up 期间不要动。
- 只信 `fastboot devices`；COM 口存在不代表内核活着。
- 控制台命令一次 bulk OUT 发完。
- panic 后不要用 KDP 恢复执行；复位重启。
- 板子 stranded 时，物理断电是第一步，不是最后一步。

---

## 10. 本文怎么维护

- 每次 HEAD 变化、补丁数变化、`verify-all.sh` 结果变化、内核大小变化、`docs/BRING-UP-STATE.md` 新增关键节，都要更新第 0.2 节。
- 新的真机日志先写进 `docs/BRING-UP-STATE.md`（带日期、镜像、启动参数、日志原文），再把结论摘要进本文；不要只改本文。
- 任何「已修复」「已验证」必须能指到补丁号或日志行；指不到就降级为「推断」。
- 本文在仓库根目录 `MIGRATION.md`。Windows 的 `D:\LOS\HiOS\MIGRATION.md` 只是镜像；提交以 Ubuntu 仓库为准，提交后再同步 Windows 副本。

---

## 附：一页速查

```text
仓库        ssh ubuntualbert:/home/albert/kebab-dev-ios16   branch ios16
HEAD        98e5432   122 patches   verify-all PASS
内核        kernel.development.kebab 17,208,944 B
板子        stranded；第一步物理断电约 10 秒
操作        python3 scripts/bring-up.py --from vol
顺序        vol -> install -> skel -> base -> cache -> final
镜像        boot-vol.img / boot-inst.img / boot-final.img（96 MiB，args 在 0x800000）
卷          KebabRoot.vext 1,769,472 B / 3 extents / 1,744,896 data / 4,194,304 blocks
骨架        skel.kist 16 条 / 153 B installed / 65,536 B wire
用户态      base.kist 5 条 / 1,619,682 B installed / 589,824 B wire
缓存        cache.kist 44 条 / 3,330,359,296 B installed / 1,067,909,120 B wire
最终启动    必须带 kebab_dsc=1 kebab_dsc_map=1
panic       默认 debug=0x14e 不等待 KDP；kebabkdp=1 才等
三个诊断    exec copyout（0140）/ DSC dyld 预期（七十三.4）/ panic A-E（六十四.5）
```
