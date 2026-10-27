# 固件侧：KFLG 日志环与 RPMh 探针

## 一、先读这一段

**这个目录不是设备仓库构建链的一部分。** `kebab-dev` 只保存 XNU 补丁、板级描述和
文档；固件源码不在这里，而是来自 `board.toml` 的 `[firmware]` 所引用的 `{uefi}`
检出（Mu-Silicium）。本目录只是把要放进那棵树的两个 DXE 驱动和集成说明留在这里。

**Mu-Silicium 固件树本身不在这个仓库里**：它不在这份 git 历史中，也不在
`firmware/` 里；这里没有 `MdePkg`、`MdeModulePkg`、`.dsc`/`.fdf`，也没有构建
产物。`firmware/` 只保存要复制进那棵树的源文件。

也就是说：

* 只把文件放在 `firmware/`，固件不会变；
* 固件树必须真的拿到这些源文件、真的在 `.dsc` 和 `.fdf` 里登记，才会被编译和打包；
* 判断有没有真的进镜像，唯一可靠的办法是构建后 `strings` 搜 `KRP:` 和 `KFLG`。
  **如果这两个字符串不在 `SILICIUM_UEFI.fd` 里，驱动就不在镜像里，任何日志都不会
  包含它们**，刷机后也不会有 `FWLOG?` 输出可读。

本目录**没有做过整棵固件的 EDK2 构建**：没有跑 `.dsc`/`.fdf` 链接，也没有产出
`SILICIUM_UEFI.fd`。写这份说明时只对三个 `.c` 做了单文件语法检查（`clang
-fsyntax-only`，aarch64 target，头文件取自 Mu_Basecore 的 MdePkg/MdeModulePkg），
三个文件都通过；这不等于固件能构建、能链接或能启动。XNU 侧的补丁 0129 才是实际
编译并验证过的；固件侧见第六节。

## 二、文件

| 文件 | 作用 |
| --- | --- |
| `KebabFwLog.h` | 固定物理页、`KFLG` 头和 256 KiB 环的布局；拷到 `SiliciumPkg/Include/` |
| `KebabFwLog.c` | `KebabFwLogAppend()` 的独立实现（参考实现，当前集成里不被任何 `.inf` 编译） |
| `KebabFwLogDxe.c` / `.inf` | 保留 `0x9FF00000` 的 64 页并写入 magic 的 DXE 驱动 |
| `KebabRpmhProbeDxe.c` / `.inf` | 只读 RPMh 探针：`t5` 与 `eod` 两次转储 |

`KebabFwLog.h` 必须放在包的公共 include 目录 `SiliciumPkg/Include/`，因为
`FrameBufferSerialPortLib.c` 用的是 `#include <KebabFwLog.h>`（尖括号），只有包的
`Include/` 在编译器的搜索路径里。放到驱动自己的目录里，串口库找不到它。

## 三、我检查到的 Mu-Silicium 现状

写这份说明时，Linux 主机上的 Mu-Silicium 检出
`/home/albert/Codex/UEFI/Mu-Silicium`（不在本仓库内）已经有一部分集成：

| 项 | 现状 |
| --- | --- |
| `Silicon/Silicium/SiliciumPkg/Drivers/KebabFwLogDxe/` | 已存在，含 `.c` 和 `.inf` |
| `Silicon/Silicium/SiliciumPkg/Include/KebabFwLog.h` | 已存在 |
| `kebab.dsc` `[Components]` | 已列 `KebabFwLogDxe.inf` |
| `kebab.fdf` `[FV.FvMain]` | 已列 `INF .../KebabFwLogDxe.inf` |
| `FrameBufferSerialPortLib.c` | `SerialPortWrite()` 已在渲染前调用 `KebabFwLogAppend()`，库内有一份同语义的静态实现 |
| `Silicon/Silicium/SiliciumPkg/Drivers/KebabRpmhProbeDxe/` | 目录存在，但还是旧的探针源码 |
| `kebab.dsc` / `kebab.fdf` 中的探针 | **没有**，所以当前构建不会包含 `KRP:` |

结论：**KFLG 这一半已经接好；探针这一半还没有进 `.dsc`/`.fdf`。** 从干净的
`{uefi}` 检出开始时，下面四步对两个驱动都要做；用现有检出时，只需补探针的
`.dsc`/`.fdf` 两行，并用本目录的新 `KebabRpmhProbeDxe.c/.inf` 覆盖旧文件。

## 四、集成清单

### (a) 两个目录（以及公共头文件）

在固件树里准备：

```text
Silicon/Silicium/SiliciumPkg/Drivers/KebabFwLogDxe/
    KebabFwLogDxe.c
    KebabFwLogDxe.inf
Silicon/Silicium/SiliciumPkg/Drivers/KebabRpmhProbeDxe/
    KebabRpmhProbeDxe.c
    KebabRpmhProbeDxe.inf
Silicon/Silicium/SiliciumPkg/Include/
    KebabFwLog.h
```

`KebabFwLog.c` 是 `KebabFwLogAppend()` 的参考实现。当前集成里真正跑在
`FrameBufferSerialPortLib.c` 里的是同一段代码的静态副本；如果以后要把两份合成
一份，应该建一个 library instance 并让串口库和驱动都链接它，而不是把 `.c` 同时
塞进两个模块（会各自得到一份副本，虽然语义相同）。**两份实现的语义必须保持
一致：magic 之前 no-op、64 位取模环、`head` 用 release store 发布。**

### (b) `.dsc` 的 `[Components]`

在包的 `kebab.dsc`（`Platforms/OnePlus/kebabPkg/kebab.dsc`）的 `[Components]`
里加两行。路径相对于 `PACKAGES_PATH` 里的 `Silicon/Silicium`，所以是
`SiliciumPkg/...` 而不是 `Silicon/Silicium/SiliciumPkg/...`：

```ini
  SiliciumPkg/Drivers/KebabFwLogDxe/KebabFwLogDxe.inf
  SiliciumPkg/Drivers/KebabRpmhProbeDxe/KebabRpmhProbeDxe.inf
```

### (c) `.fdf` 里放进 FV

在 `kebab.fdf` 的 `[FV.FvMain]` 里加：

```ini
  INF SiliciumPkg/Drivers/KebabFwLogDxe/KebabFwLogDxe.inf
  INF SiliciumPkg/Drivers/KebabRpmhProbeDxe/KebabRpmhProbeDxe.inf
```

`FvMain` 开头 `!include Include/DXE.inc` 已经把 `RpmhDxe` 排在前面；探针即使先
被 dispatch，第一次转储在 5 秒定时器，协议在那之前已经安装。FwLogDxe 的 depex
是 `TRUE`，运行得越早，串口库能记下的早期输出越多；如果要记录最早的 DXE 输出，
把它挪到 `Include/DXE.inc` 的前部（APRIORI/早期 DXE），仅放在 `FvMain` 末尾会
丢掉驱动运行前的日志。

`SILICIUM_UEFI.fd` 固定 3 MiB（`FD_BASE=0x9fc00000`、`FD_SIZE=0x300000`）。两个
模块都很小，一般放得下；如果链接报 FV 溢出，要同时改 `FD_SIZE`/`FD_BLOCKS` 和
boot 头，不能用 `repack-firmware.py` 硬塞一个更大的 FV（脚本会拒绝尺寸不一致）。

### (d) 构建后按字符串验收

```sh
strings Build/kebabPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd | grep -E "KRP:|KFLG"
```

期望两行都能看到：

```text
KRP: probe armed: t5 in 5 s, eod on EndOfDxe
... KFLG ...
```

* 只有 `KFLG`：日志驱动进了镜像，探针没有（`.dsc`/`.fdf` 没加，或旧文件没覆盖）；
* 两个都没有：两个模块都没进镜像；
* 两个都有：可以刷机，内核里 `FWLOG?` 才有内容可读。

这个检查只对 **DEBUG** 构建成立。本平台用 `-b DEBUG`，`DEBUG((DEBUG_ERROR, ...))`
里的字符串会进镜像；RELEASE 构建会把 `DEBUG()` 编译掉，`KRP:`/`KFLG` 可能不在
字符串表里，那时不能拿字符串缺失反推模块没进镜像（应看 `.map` 和 FV 内容）。

## 五、两个驱动的行为

### 5.1 KFLG 环

固定物理地址 `0x9FF00000`，64 页 = `0x40000`（256 KiB），头部 16 字节：

```text
偏移    类型   含义
+0x00   u32    magic = 0x474c464b（小端字节 'K','F','L','G'）
+0x04   u32    size  = 数据区字节数 = 0x3FFF0
+0x08   u64    head  = 历史累计写入字节数（单调）
+0x10   u8[]   data  = 环；绝对偏移 p 的字节在 data[p % size]
```

`KebabFwLogDxe` 用 `AllocatePages(AllocateAddress, EfiReservedMemoryType, 64)` 保留
这一页，依次写 `Magic=0`、`Size`、`Head=0`、fence、`Magic='KFLG'`。成功后这一页在
UEFI 内存图里是 reserved，内核能按固定物理地址找到它；失败时它只打一行
`[KFLG] reserve ... failed`，magic 不会写，串口库的 append 一直是 no-op，内核会
报 `no 'KFLG' magic`。

生产者是 `FrameBufferSerialPortLib` 的 `SerialPortWrite()`：它在把字节画到 GOP 之前
调用 `KebabFwLogAppend(Buffer, NumberOfBytes)`。所以固件屏幕上的文本同时进了环；
`head` 用 release store 发布，读侧不需要锁。

注意两点：

* `0x9FF00000` 正好在 FD 结束处（`0x9FC00000 + 0x300000`）。保留失败通常说明这段
  地址已被别的描述符占用；不要改内核单方面地址，三处必须一致：
  `KebabFwLog.h` 的 `KEBAB_FWLOG_PA`、串口库里的副本、内核 0129 的
  `KEBAB_FWLOG_PA`。
* 驱动跑起来之前串口库的输出不会被记录（magic 还是 0）。这是有意的：DXE core
  还拥有那页内存时，不能往里写。

### 5.2 RPMh 探针

只读：`LocateProtocol` 之外不调用协议函数，不写 RPMh/RSC 寄存器。

每次转储（`t5` 和 `eod`）输出：

```text
KRP: t5 guid=e1d0b573-f29f-495d-9352-ca0dd42a0a74 status=Success (0x0) iface=0x...
KRP: t5 image base=0x... size=0x...
KRP: t5 iface rva=0x...
KRP: t5 raw words (8)
KRP: t5 raw[0] 0x... rva=0x...
KRP: t5 raw[1] 0x... not-code
...
KRP: t5 iface as table, 64 entries
KRP: t5 iface[0] 0x... rva=0x...
...
KRP: t5 cand[0] table=0x... rva=0x... entries=64
KRP: t5 cand[0][0] 0x... rva=0x...
...
KRP: t5 cand[1] 0x... not-code, skipped
```

* 第一行在 `LocateProtocol` 失败时也会打印，`status` 是 `Not Found` 之类的名字，
  `iface=0x0`；随后一行说明没有东西可转储。
* `image base/size` 只解析一次：第一次成功找到包含 interface 的 loaded image 后
  缓存，之后所有 `rva=`/`not-code` 判断只用缓存，不再枚举 handle。
* `raw[0..7]` 是 interface 处的 8 个原始 64 位字。落在镜像范围内打印 `rva=`，
  否则 `not-code`。
* `iface[0..63]` 是把 interface 直接当函数表读，最多 64 项（靠近镜像末尾时会
  按剩余字节截断）。
* `cand[0]`/`cand[1]` 分别对应 `raw[0]`/`raw[1]`：只有它们落在镜像内才继续当表
  转储；落在外面打印 `not-code, skipped`。
* `t5` 是驱动加载后 5 秒的一次性定时器；`eod` 是 `gEfiEndOfDxeEventGroupGuid`。
  两次输出带同一个 tag，可以逐行比较地址、RVA 和表内容。

怎么读：如果 `iface` 本身是函数表，`iface[i]` 应是一串 `rva=`；如果 `raw[0]` 和
`iface[0]` 相同且是一个 `rva=`，说明第一层是**指针**，真正的函数表在
`cand[0]` 里；如果 `raw[0]` 是 `not-code`，说明它指向镜像外（MMIO、另一个
image 或根本不是指针）。这正是旧探针把 interface 当表读时看不出来的那层间接。

`eod` 发生在 BDS 连接控制台之前，可能根本到不了屏/串口；`t5` 在 5 秒后通常会。
如果只看到 `t5`，不代表 `eod` 没执行，只代表那次输出没有可用的控制台。

## 六、验证边界

已经验证的：

* 0129 补丁按文件名顺序应用在 `1b191cb` 上，`scripts/verify-all.sh` 报
  `108/108 patches apply ...`；
* XNU 内核在 macOS 14.6 + Xcode 15.4 的构建机上编译通过，产物
  `kernel.development.kebab`，新字符串和符号见设备仓库的
  `docs/BRING-UP-STATE.md` 第五十九节。

**没有验证的：**

* 没有做整棵固件的 EDK2 构建：没有 `.dsc`/`.fdf` 链接，没有 `SILICIUM_UEFI.fd`，
  也没有刷机；只做了三个 `.c` 的单文件 `clang -fsyntax-only`（aarch64，头文件
  来自 Mu_Basecore 的 MdePkg/MdeModulePkg），全部通过；
* 固件日志在真机上的内容、`head` 的回绕、RPMh 协议的实际地址和表布局，都要等
  固件构建、刷入并在内核里执行 `FWLOG?` 后才能确认；
* 如果固件树没有做第四节的 (a)～(d)，内核会打印
  `KEBAB FWLOG: absent: no 'KFLG' magic ...`，这是“固件没集成”，不是内核 bug。
