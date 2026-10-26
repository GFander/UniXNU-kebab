# iOS 用户态

用 iPhone X（`iPhone10,3` / D22AP）的身份启动 iOS 16.7.16 用户态，需要真实二进制：
`dyld`、`launchd`、`libSystem` 和它依赖的一切。它们都在 IPSW 的根文件系统里。

这份文档记录从 IPSW 里取这些东西的流程、已经打通的环节，以及卡住的那一步。

## 从 IPSW 取用户态的流程

### 一、固件

目标版本 16.7.16 是存在的，而且就是 16.x 里最高的一个。`api.ipsw.me` 的
`iPhone10,3` 列表里 16.x 共 32 个版本，最后一个正是它，也是目前唯一还在签名的
16.x：

| 项 | 值 |
| --- | --- |
| 版本 | 16.7.16 |
| Build | `20H392` |
| 发布日期 | 2026-05-11 |
| 签名 | 是 |
| 大小 | 6,171,035,648 字节（5.75 GiB）|
| SHA-256 | `ece1194f7e233e9109c5790a8d23a4564a10a254e15ec5b09978a343265568fb` |
| URL | `https://updates.cdn-apple.com/2025FallFCS/fullrestores/122-77006/1158E502-D76D-4226-AA06-ED5E30CD1111/iPhone10,3,iPhone10,6_16.7.16_20H392_Restore.ipsw` |

所以不必退到 16.7.15 —— 目标版本本身可用。校验和取自 ipsw.me，与 CDN 返回的
`x-amz-meta-digest-sha256` 一致。

### 二、成员布局

IPSW 就是一个普通 ZIP，但 6.17 GB 里只有 83 个文件成员，体积几乎全压在几个大件上：

| 成员 | 压缩后 | 解压后 | 用途（读自 BuildManifest）|
| --- | --- | --- | --- |
| `087-86698-044.dmg` | 4,730,814,044 | 4,730,814,044 | `OS`，即根文件系统，**stored**（不压缩）|
| `087-86593-047.dmg` | 1,097,727,251 | 2,743,074,816 | `Cryptex1,SystemOS` |
| `087-86622-047.dmg` | 93,769,851 | 109,051,931 | `RestoreRamDisk` |
| `087-86719-047.dmg` | 91,939,528 | 106,954,779 | `RestoreRamDisk` |
| `087-86715-047.dmg` | 2,381,197 | 10,485,760 | `Cryptex1,AppOS` |
| `Firmware/087-86698-044.dmg.mtree` | 22,031,630 | 22,314,023 | 路径清单，见下 |
| `kernelcache.release.iphone10b` | 18,176,350 | 18,204,358 | 内核缓存 |
| `BuildManifest.plist` | 27,322 | 449,774 | |
| `SystemVersion.plist` | 281 | 493 | |

用途这一列不是猜的：`BuildManifest.plist` 的 `Manifest` 表里每个条目都有
`Info.Path` 和身份名，`087-86698-044.dmg` 对应 `OS`。另外注意 iOS 16 把一部分系统
内容放在 cryptex 里（`Cryptex1,SystemOS` 有 2.7 GB），**拼根卷时不能只取 `OS`
这一个 DMG**。

归档 6,171,035,648 字节 > 4 GiB，**中央目录是 ZIP64**：EOCD 里的目录偏移是
`0xffffffff`，真实值在 ZIP64 locator 指向的记录里。读目录必须处理这一点，
否则连 `--list` 都做不了。

`BuildManifest.plist` / `SystemVersion.plist` 取出后自证版本：
`ProductVersion 16.7.16`、`ProductBuildVersion 20H392`。

### 三、三个目标路径不在 ZIP 里

这是必须要面对的事实：`/usr/lib/dyld`、`/sbin/launchd`、`/usr/lib/libSystem.B.dylib`
都不是 ZIP 成员，它们在最大的那个 DMG 里。脚本会直接说清楚：

```
$ python3 scripts/extract-ios-userland.py "$URL" -o out \
    -m /usr/lib/dyld -m /sbin/launchd -m /usr/lib/libSystem.B.dylib
not a zip member: /usr/lib/dyld
not a zip member: /sbin/launchd
not a zip member: /usr/lib/libSystem.B.dylib
  the iOS root filesystem is inside the largest .dmg (087-86698-044.dmg,
  4,730,814,044 bytes), not a top-level member
```

### 四、根 DMG 的实际格式

`087-86698-044.dmg` 是 stored 的，所以它的**尾部 512 字节可以直接用一次 range
请求拿到**，不必先下载 4.7 GB。`koly` 解析出来是这样：

| 字段 | 值 |
| --- | --- |
| koly 版本 / 头长 | 4 / 512 |
| 字节序 | **大端**（`hdiutil` 写的是小端，Apple 的 IPSW 是大端）|
| data fork | 4,730,590,018 字节 |
| xml plist | 223,514 字节，位于 data fork 之后 |
| 镜像大小 | 11,006,019 扇区 = 5,635,081,728 字节 |
| 校验 | 类型 2，32 字节 |

三个数字对得上，说明解析是对的：
`4,730,590,018 + 223,514 + 512 = 4,730,814,044`，正好等于成员大小。

BLKX 分区表（Apple 这里用的签名是 `mish` 而不是 `blkx`，同样是大端，run 里的
扇区号是**相对于分区起始扇区**的）给出 6 个分区：

| 分区 | 起始扇区 | 扇区数 | 字节 | 压缩后 |
| --- | --- | --- | --- | --- |
| MBR | 0 | 1 | 512 | 53 |
| Primary GPT Header | 1 | 1 | 512 | 108 |
| Primary GPT Table | 2 | 32 | 16,384 | 209 |
| Apple_APFS | 34 | 11,005,952 | 5,635,047,424 | 4,730,589,331 |
| Backup GPT Table | 11,005,986 | 32 | 16,384 | 209 |
| Backup GPT Header | 11,006,018 | 1 | 512 | 108 |

压缩字节合计 4,730,590,018 = data fork 长度；扇区合计 11,006,019 = koly 的
`sector_count`。**这个 DMG 不是 UDZO，是 ULFO（LZFSE）**：每个 chunk 的类型码是
`0x80000007`，但块首的 magic 是 `bvx2`/`bvxn`，也就是 LZFSE。类型码只是约定，
magic 才是事实 —— 有几个格式表把 `0x80000007` 记成 LZMA，这里按实测写。

而且每个 BLKX run 都是一个**自带结束符 `bvx$` 的独立 LZFSE 流**。这一点很关键：
它意味着可以只解某一块（1 MiB），不需要碰整个 data fork。`--dmg-probe` 就是靠它
在 2 秒内读到 GPT 和 APFS 容器头，总共只取了 16.9 MB。

读出来的 APFS 容器头（分区起点 17,408 字节处）：

| 字段 | 值 |
| --- | --- |
| 块大小 | 4096 |
| 块数 | 1,375,744（= 5,635,047,424 字节）|
| UUID | `0e25f3316279458a9cfef39598da17aa` |
| omap oid | 19917（`omap_phys`，其树根在块 19918）|
| spaceman oid | 37365 |

### 五、脚本做什么

`scripts/extract-ios-userland.py`：

- 自己实现 range 请求（`http.client`），带重定向、重试、短读补齐；服务器若返回
  200 而不是 206 就报错退出，绝不退化成整包下载。
- 读中央目录：EOCD → ZIP64 locator → 中央目录（9,739 字节）。六万分之一秒级的
  操作：`--list` 总耗时 0.19 秒。
- 按需取成员，流式解压（stored / deflate），CRC32 与成员大小都校验，先写
  `*.part` 再改名。
- 幂等 + 可续：目标文件大小对得上就跳过；stored 成员的 `*.part` 会从已有长度接着
  下（deflate 流不可随机定位，会重下）。
- 清单里给成员名、压缩后/解压后大小，以及 Mach-O 头解析（magic、CPU 类型、文件
  类型）。IPSW 里的固件大多是 DER/IMG4 包着的 `IM4P`，脚本会拆 DER 取
  IA5String 与 OCTET STRING，载荷是 LZFSE 时再解一层（需要 `lzfse` 模块）。
- `--dmg-info` 解析 koly + BLKX，`--dmg-probe` 再顺着 chunk 表读分区表和文件系统头。

用法：

```sh
# 看目录
python3 scripts/extract-ios-userland.py "$URL" --list [--sort-size]

# 取几个小成员
python3 scripts/extract-ios-userland.py "$URL" -o ~/ios-userland/ipsw \
    -m BuildManifest.plist -m SystemVersion.plist -m kernelcache.release.iphone10b

# 根 DMG 是什么格式、多大
python3 scripts/extract-ios-userland.py "$URL" --dmg-info

# 再往里一层：GPT + APFS 容器（需要 LZFSE 解码器）
python3 -m venv ~/ios-userland/venv
~/ios-userland/venv/bin/pip install lzfse
~/ios-userland/venv/bin/python scripts/extract-ios-userland.py "$URL" --dmg-probe
```

`lzfse` 只影响两件事：`--dmg-probe`，以及看 `IM4P` 里被 LZFSE 压过的载荷。没有它
其余功能照常工作，只是这两处会少打几行。

### 六、验证过的

都是在 `ubuntualbert` 上对着上面那个 URL 实跑的：

| 项目 | 结果 |
| --- | --- |
| `--list` | 0.19 秒，83 个成员，ZIP64，最大成员 4,730,814,044 字节 |
| 取 6 个小成员（40 MB）| 2.4 秒，CRC32 全部通过，目录结构保留 |
| 幂等 | 再跑一次 0.20 秒，三个成员全部 `skipped`，不重下 |
| 续传 | 本地测试归档里预置 2,500,000 字节的 `.part`，续传后 SHA-256 与源一致 |
| Mach-O 解析 | `Mach-O 64 arm64 execute, 3 cmds, 4096 bytes of load commands` |
| 本地文件源 | 同一脚本直接读本地 `.ipsw`（`LocalRangeFile`）|
| DMG 尾部 | 只需 512 字节 range，koly 三个数字自洽到字节 |
| BLKX/GPT | 6 个分区，压缩字节、扇区数与 koly 完全一致 |
| APFS | 容器头可读（块大小、块数、UUID、omap），见上表 |
| kernelcache | `IM4P 'krnl'`（DER）→ 18,204,284 字节 LZFSE → 50,937,884 字节 fat Mach-O（1 个 arm64 slice）|

### 七、没做成的

**取不出 `/usr/lib/dyld`。** 卡在 APFS 的 B 树，不是磁盘、不是网络：

- `nx_fs_oid[0]` 是 1026，但这是**虚拟 oid**，要经容器的 omap B 树翻译成物理块。
- omap 对象在块 19917（类型 `0x4000000b`），树根在块 19918：level 0 的叶节点，
  **只有 1 个 key**，`(oid 1026, xid 255)` 就躺在节点偏移 504 处 —— 路是对的，只是
  叶节点里 value 的定长布局还没读对：按 `omap_val_t{flags,size,paddr}` 试出来的块号
  里没有 `APSB`，`BTNODE_FIXED_KV_SIZE` 下 kvoff 的语义要照 `apfs.h` 再对一遍。
- 另一个坑：容器块 0 的超级块 xid 是 255，是最新的一份；checkpoint 区里块 2、4…22
  存着 xid 229～239 的旧副本，omap 指针各不相同。挑错就会读到已经作废的 omap。

另外 `Firmware/087-86698-044.dmg.mtree` 取出来了，但它本身是
`DER → IM4P('msys') → 'pbze' → 4 MiB 一块的 LZFSE`。这是**最便宜的路径清单**：
解开它就是全量路径 + 大小，不用碰 APFS。这次没解完（`pbze` 块内的 `bvx2` 流要逐块
走边界，`lzfse.decompress` 直接喂整块会失败）。

### 八、下一步

1. **先解 mtree**，拿到路径清单和大小，确认 `dyld`/`launchd`/`libSystem` 的确切
   体积，再决定要取哪些文件。工作量最小，收益最直接。
2. **补 APFS 读取器**：`omap` 叶节点 value 布局按 `apfs.h` 的
   `omap_val_t{flags,size,paddr}` 重新对一遍（当前解析在叶节点 504 字节处拿到
   key、随后 16 字节的 value 与实际块号对不上，需要按 `BTNODE_FIXED_KV_SIZE`
   的 kvoff 语义重新算），然后走 fs-tree（`apfs_root_tree_oid` 同样要经 omap）。
   `DmgReader` 已经把"按块取 1 MiB"这件事做完了，读一个 B 树节点就是一次
   range 请求。
3. **不要走"整包下载 + 挂载"的路**：DMG 4.73 GB + 展开 5.63 GB = 10.4 GB，这台
   机器只有 9.1 GB 可用；而且 `dmg2img`/`libfsapfs-utils` 虽然源里有，装它们要
   root。按块取才是这台机器上唯一可行的路子。

拿到二进制之后才是 P1 的活：把它们拼成可启动根卷，让 `dyld` 加载共享缓存并跳到
`launchd` 入口（见 `docs/ROADMAP.md` 的 P1）。

## mtree 清单

这一节解决第七节里"mtree 没解完"的部分；APFS 那条线仍然是第八节第 2 条。
`Firmware/087-86698-044.dmg.mtree` 现在可以完整解成路径清单，代码在
`scripts/decode-ios-mtree.py`，输出在 `~/ios-userland/mtree-paths.txt`。

### 一、容器布局（按字节实测）

IPSW 成员本身是 22,031,630 字节（deflate），解压后 22,314,023 字节。头 27 字节是 DER：

| 成员内偏移 | 字节 | 含义 |
| --- | --- | --- |
| 0 | `30 84 01 54 7c 21` | SEQUENCE，长 0x01547c21 = 22,314,017；6 + 22,314,017 = 22,314,023，正好到文件尾 |
| 6 | `16 04 49 4d 34 50` | IA5String `IM4P` |
| 12 | `16 04 6d 73 79 73` | IA5String `msys` |
| 18 | `16 01 30` | IA5String `0` |
| 21 | `04 84 01 54 7c 0c` | OCTET STRING，长 0x01547c0c = 22,313,996 |
| 27 | `70 62 7a 65` | 载荷开始：`pbze` |

`pbze` 的头只有 12 字节：magic + 一个大端 u64 块大小。

| 成员内偏移 | 字段 | 值 |
| --- | --- | --- |
| 27 | magic | `pbze` |
| 31 | u64be block_size | 4,194,304（4 MiB）|
| 39 | 第 0 块头 | usize=4,194,304，csize=2,146,326 |
| 55 | 第 0 块 LZFSE 流 | `62 76 78 32`（`bvx2`）… `62 76 78 24`（`bvx$`）|

块表不是偏移表，而是顺序的 `(uncompressed_size, compressed_size)` 对，每块一个
自结束的 LZFSE 流：

```
u64be uncompressed_size
u64be compressed_size
compressed_size 字节的 LZFSE 流（bvx2 ... bvx$）
```

23 个块的压缩字节逐块累加，再加回 12 字节 `pbze` 头、23 × 16 字节块头和 27 字节
DER 头，正好到文件尾：

| 块 | usize | csize | 块 | usize | csize |
| --- | --- | --- | --- | --- | --- |
| 0 | 4,194,304 | 2,146,326 | 12 | 4,194,304 | 226,565 |
| 1 | 4,194,304 | 2,273,487 | 13 | 4,194,304 | 228,320 |
| 2 | 4,194,304 | 2,252,542 | 14 | 4,194,304 | 274,473 |
| 3 | 4,194,304 | 2,285,200 | 15 | 4,194,304 | 166,358 |
| 4 | 4,194,304 | 2,271,255 | 16 | 4,194,304 | 230,980 |
| 5 | 4,194,304 | 2,330,498 | 17 | 4,194,304 | 265,126 |
| 6 | 4,194,304 | 2,324,015 | 18 | 4,194,304 | 265,323 |
| 7 | 4,194,304 | 2,283,579 | 19 | 4,194,304 | 261,699 |
| 8 | 4,194,304 | 960,109 | 20 | 4,194,304 | 243,885 |
| 9 | 4,194,304 | 232,822 | 21 | 4,194,304 | 281,483 |
| 10 | 4,194,304 | 227,619 | 22 | **726,180** | **52,125** |
| 11 | 4,194,304 | 229,827 | 合计 | **93,000,868** | **22,313,616** |

`22,313,616 + 12 + 23 × 16 + 27 = 22,314,023`。前 22 块都是整 4 MiB，最后一块
726,180 字节；每块首 4 字节 `bvx2`、尾 4 字节 `bvx$`，LZFSE 输出长度逐块等于
usize。`pbze` 后面没有索引区，也没有对齐填充——块头里的 csize 就是下一块的起点。

### 二、Apple Archive

93,000,868 字节解压流不是直接文本，而是一个 Apple Archive（`AA01` 记录）：

```
"AA01" + u16le header_size + 字段... [+ DATB 指定长度的数据]
字段 = 3 字节 ASCII 键 + 1 字节类型码：
  1 = u8, 2 = u16le, 4 = u32le, 8 = u64le,
  P = u16le 长度 + 字节（路径）,
  T = 12 字节（u64le 秒 + u32le 纳秒）,
  B = u32le 长度（数据长度；数据紧跟在 header 后）
```

三条记录的长度完全闭合：

| # | header 偏移 | header 长 | 路径 | TYP1 | 数据长 |
| --- | --- | --- | --- | --- | --- |
| 0 | 0 | 70 | （空，根目录）| 68 `D` | 0 |
| 1 | 70 | 87 | `digest.db` | 70 `F` | 34,996,224 |
| 2 | 34,996,381 | 87 | `mtree.txt` | 70 `F` | 58,004,400 |

`70 + 87 + 34,996,224 + 87 + 58,004,400 = 93,000,868`，正好等于解压流长度。
根记录的字段是 `UID1 0 / GID1 80 / MOD2 0700 / FLG1 0`，`digest.db` 是 0600，
`mtree.txt` 是 0644。

### 三、路径清单

`mtree.txt` 是 58,004,400 字节、1,265,127 行，开头：

```
#	   user: root
#	machine: ggbp-spark-worker-deploy-prod-us-west-2n-7455d846b7-j48px
#	   tree: /Volumes/SydneyUpdate20H392.D22D221OS
#	   date: Wed Apr 29 21:10:13 2026
```

文本是层级式，不是每行一个绝对路径：

```
# ./usr/lib
lib             type=dir inode=1152921500312216305 siblingid=0 nxattr=0
    dyld        xattrsdigest=authapfs.1152921500312216350 \
                inode=1152921500312216349 siblingid=0 nxattr=2
...
# ./usr/lib
..
```

`# ./dir` 开一个目录，下一行是目录自身，缩进行是它的非目录子项，最后再一个
`# ./dir` 加 `..` 关闭；目录各有独立小节，所以每条路径只出现一次。名字里的
空格用 `\040` 转义（`#` 注释里则是字面空格），解析时要还原。

结果：**308,564 条路径**，0 重复，0 个孤儿父目录：

| 类型 | 数量 |
| --- | --- |
| 目录 | 130,751 |
| 普通文件 | 175,489 |
| 符号链接 | 2,324 |
| 设备节点 | 0 |

设备节点是 0：这个卷里没有 `type=block`/`type=char`；`/dev` 是 devfs，不在根卷。

四条验收路径：

| 路径 | 结果 | 类型 | inode | xattrsdigest |
| --- | --- | --- | --- | --- |
| `/usr/lib/dyld` | 在 | file | 1152921500312216349 | authapfs.1152921500312216350 |
| `/sbin/launchd` | 在 | file | 1152921500312216239 | authapfs.1152921500312216240 |
| `/usr/lib/libSystem.B.dylib` | **不在** | — | — | — |
| `/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64e` | **不在** | — | — | — |

最近匹配：

- `libSystem.B.dylib`：`/usr/lib/libSystem.B_asan.dylib`（file）、
  `/usr/lib/libSystem_asan.dylib`（link）、`/System/DriverKit/usr/lib/libSystem_debug.dylib`。
- `dyld_shared_cache_arm64e`：`/System/Library/Caches` 只有 `apticket.der`、
  `com.apple.factorydata/`、`com.apple.kernelcaches/`；全清单 `dyld_shared_cache`
  0 命中，`com.apple.dyld` 也是 0。

**尺寸：mtree 不记录。** `grep -c " size="` 和 `grep -c " mode="` 都是 0；
208 个含 "size" 的行全是文件名（如 `dalSiriFeetSize.cat`）。每条记录只有
`type`、`inode`、`siblingid`、`nxattr`、`xattrsdigest`（目录另有
time/btime/ctime/ptime）。所以清单给路径和 inode，不给字节数；大小要从 APFS
inode 记录读，或从别的清单读。

### 四、哪些是证实的，哪些是推断的

证实的（字节算术或重复解码支持）：

- DER 三层长度闭合到文件尾：6 + 22,314,017 = 22,314,023；OCTET STRING 22,313,996。
- `pbze` 的 23 个 `(usize, csize)` 加 12 + 23 × 16 字节头，正好 22,313,996；
  再加 27 字节 DER 头是 22,314,023。
- 每块 `bvx2`…`bvx$` 自结束，LZFSE 输出长度逐块等于 usize。
- Apple Archive 三条记录 header + DATB 累加正好 93,000,868。
- 本地文件解码与 HTTP range 解码输出 SHA-256 相同：
  `18caef0a13f37831abe0789a055fd68dcbb9f6e7dda268c0f9a4f57e2c228af2`（47,393,198 字节）。
- mtree 文本 58,004,400 字节；308,564 条路径，0 重复，0 孤儿父目录。

推断的（一致性支持，但没有规范逐条对照）：

- Apple Archive 字段类型码 1/2/4/8/P/T/B 的语义，是从本文件三条记录的字节形状
  和数值合理性推出来的；`TYP1` 的 68/70 对应 `D`/`F`，与同一路径在 mtree 里的
  `type=dir`/默认 file 一致。
- 层级 mtree 的"`# path` 开、`# path` + `..` 关、目录单独成节"规则，是从
  0 重复、0 孤儿、父子类型一致反推并验证的。
- `xattrsdigest=authapfs.<id>` 的 `<id>` 是 `digest.db` 的键：同一个 id 出现在
  `digest.db` 的记录里；但 `digest.db` 的记录布局这次没有继续解。

### 五、对 P1 根卷的意义

- 这个清单是**骨架**，不是可挂载的根卷：没有 mode、没有 size、没有 symlink
  目标、没有文件内容。搭 P1 根卷时它只能用来定路径、定 inode、定要取哪些文件。
- 根卷有 308,564 条路径，但只有 175,489 个普通文件；iOS 16 把大量系统内容放在
  `Cryptex1,SystemOS`（2.7 GB）。清单里没有 `libSystem.B.dylib`，也没有
  `dyld_shared_cache_arm64e`，正好印证：**只拿 `OS` 这个 DMG 拼根卷，dyld
  起来时会找不到共享缓存和 libSystem**。
- `/usr/lib/dyld` 和 `/sbin/launchd` 在 `OS` 卷里，这是好消息：P1 最先需要的两个
  入口还在。共享缓存和 libSystem 要去 cryptex 里取。
- 清单的 inode 列可以直接和 APFS fs-tree 的 inode 记录 join：一旦第八节第 2 条
  的 fs-tree 读通，就能把 308,564 条路径补上 mode/size/内容块号。

### 六、复现

```sh
# 本地成员（上一节已经取下来的那个）
~/ios-userland/venv/bin/python scripts/decode-ios-mtree.py \
    --mtree ~/ios-userland/ipsw/Firmware/087-86698-044.dmg.mtree \
    --out ~/ios-userland/mtree-paths.txt --stats --check

# 或者直接从 IPSW 用 range 取（只下 22 MB 的成员，2.1 秒）
~/ios-userland/venv/bin/python scripts/decode-ios-mtree.py --url "$URL" \
    --out ~/ios-userland/mtree-paths.txt --stats
```

输出是 TSV：`type  mode  size  inode  xattrsdigest  path`；`mode`/`size` 固定为
`-`，因为 mtree 没有这两个字段。`--check` 打印四条验收路径和最近匹配。

## APFS 进展：omap → 卷超级块 → inode

第七节卡住的 omap value 布局已经解开，而且一路读到了卷超级块和 inode 记录。
问题不在加密，也不在 omap 选错，而在 B 树节点的两个偏移基址。

### 一、两个基址

`btree_node_phys` 头是 56 字节，TOC 在 `56 + table_space.off`。key 区和 value
区的基址不一样：

```
key   = 56 + table_space.off + table_space.len + koff
value = block_size - (40 if ROOT else 0) - voff
```

- `table_space.len` 不是"TOC 有多少字节"这么简单：容器 omap 的根节点 19918 是
  `(off=0, len=448)`，key 区从 `56 + 448 = 504` 开始；卷 omap 的根节点 87756 是
  `(off=0, len=576)`，key 区从 632 开始。之前把 `koff` 当成"从块尾倒着数"，
  在根节点上就会读空。
- 40 字节的 `btree_info_t` footer **只有根节点有**。根节点的值区到
  `block_size - 40` 为止，非根节点一直到 `block_size`。容器 omap 的叶节点
  19918 是根节点（flags 0x7 = ROOT|LEAF|FIXED_KV），所以 value 在
  `4096 - 40 - 16 = 4040`；非根节点必须用 `4096 - voff`，多减 40 会读偏。

用这两条重读 19918：

| 项 | 值 |
| --- | --- |
| TOC | `k=0, v=16`（key 在 504，value 在 4040）|
| key | `(oid 1026, xid 255)` |
| value | `{flags=0, size=4096, paddr=19916}` |
| 块 19916 | `APSB`，卷超级块 |

### 二、卷超级块与卷 omap

块 19916 的 `APSB`：

| 字段 | 值 |
| --- | --- |
| 卷名 | `SydneyUpdate20H392.D22D221OS` |
| 文件 / 目录 / 符号链接 | 142,313 / 136,387 / 2,324 |
| 卷 UUID | `dadd93f8dbbf4b17a1df0f11e64172f7` |
| 卷 omap 对象 | 块 87,684（oid 87684, xid 251）|
| `apfs_root_tree_oid` | 1028 |
| extentref / snap meta | 88,421 / 89,162 |

卷 omap 的树根在块 87,756：level 2，3 个 separator，footer 记着
`36,173 keys / 421 nodes`。把 oid 1028 代进去，解析到块 66,076（xid 250）：

| 项 | 值 |
| --- | --- |
| fs-tree 根 | 块 66,076，oid 1028，level 3，5 keys |
| 根上的 separator | `(1, type 9)`、`(...13553, 3)`、`(...27791, 4)`、`(...3e592, 4)`、`(...527fb, 5)` |

fs-tree 的 key 比较不是把 64 位打包值当无符号数比，而是先比低 60 位的 object
id、再比高 4 位的 type；否则根上的 separator 看起来是乱序的。

### 三、走到 inode

dyld 的 oid 是 `1152921500312216349`，launchd 是 `1152921500312216239`。从根
1028 按 `(obj_id, type)` 下降：

| 对象 | 路径（oid/level/paddr）|
| --- | --- |
| `/usr/lib/dyld` | 1028(3) → 28300(2, 88606) → 36294(1, 88147) → 36318(0, 101073) |
| `/sbin/launchd` | 1028(3) → 28300(2, 88606) → 36294(1, 88147) → 36311(0, 101045) |

inode 记录是 `j_inode_val_t` 固定 92 字节，`uncompressed_size` 在偏移 84，
xfield 从 92 开始。读出来：

| 路径 | inode | parent | mode | uid/gid | size |
| --- | --- | --- | --- | --- | --- |
| `/usr/lib/dyld` | 1152921500312216349 | 1152921500312216305 | 0100755 | 0/0 | **922,912** |
| `/sbin/launchd` | 1152921500312216239 | 1152921500312216232 | 0100755 | 0/0 | **541,600** |

两个 parent 正好是 mtree 清单里的 `/usr/lib` 和 `/sbin`（inode 分别为
1152921500312216305 和 1152921500312216232），inode 的 `private_id` 也等于
mtree 的 inode 列。xfield NAME 分别是 `dyld` 和 `launchd`。所以这不是"读到一个
像 inode 的块"，而是和 mtree 同一条记录。

### 四、结论

- 第七节的死结是 value 基址用错：根节点有 40 字节 footer，非根节点没有。
- mtree 缺的 size/mode 可以从 APFS inode 补齐：清单给 308,564 条路径和 inode，
  APFS 给 inode 的 mode/size，两边按 inode join。
- `/usr/lib/dyld` 922,912 字节、`/sbin/launchd` 541,600 字节，都是 0100755。
- `libSystem.B.dylib` 和 `dyld_shared_cache_arm64e` 在这个 OS 卷里确实不存在，
  要把同一条链走到 `Cryptex1,SystemOS` 的 APFS 才能拿到。

复现：

```sh
python3 scripts/probe-ios-apfs.py --url "$URL" --known
```

## 读出文件内容

上一节停在 inode 的 `size`，这一节把它变成磁盘上的字节。新增工具
`scripts/apfs-get.py`：从根目录 oid 2 开始，按 `j_drec` 记录逐级解析路径，
再顺着 `j_file_extent_val_t` 读数据流，最后按 decmpfs 解压。

```sh
python3 scripts/apfs-get.py --url "$URL" \
    --get /usr/lib/dyld --get /sbin/launchd --out ~/ios-userland/extracted
```

### 一、extent 实测

extent 记录在 fs-tree 里的 key 是 `(obj_id, type=8)` 加上 8 字节
`logical_addr`，value 是 `j_file_extent_val_t`：

```text
key   1e 23 05 00 ff ff ff 8f  00 00 00 00 00 00 00 00
      ^ obj_id_and_type          ^ logical_addr = 0
        (obj_id 1152921500312216350, type 8)
value 00 00 05 00 00 00 00 00  56 de 11 00 00 00 00 00  00 00 00 00 00 00 00 00
      ^ len_and_flags=0x50000  ^ phys_block_num=0x11de56  ^ crypto_id=0
```

- `len = len_and_flags & 0x00ffffffffffffff`，单位是字节；`flags =
  len_and_flags >> 56`。这条 extent 长 **327,680** 字节，flags 为 0。
- `phys_block_num` 是 APFS 容器块号，字节偏移为 `block * block_size`
  （block_size = 4096）。
- 压缩文件的 extent **不在 inode oid 下**，而在 `com.apple.ResourceFork`
  xattr 指向的数据流 oid 下。dyld 的 xattr 原始 value 是：

```text
01 00 30 00                          flags=1 (DATA_STREAM), xdata_len=48
1e 23 05 00 ff ff ff 0f              xattr_obj_id = 1152921500312216350
0a f8 04 00 00 00 00 00              dstream.size = 325,642
00 00 05 00 00 00 00 00              dstream.alloced_size = 327,680
00 00 00 00 00 00 00 00              crypto_id = 0
8a f6 04 00 00 00 00 00              total_bytes_read = 325,258
```

所以对压缩文件，extent 的 `length` 是 **分配长度**（327,680），dstream
的 `size` 才是资源叉的逻辑长度（325,642）；读取时取
`min(extent.length, dstream.size)`。对未压缩文件，extent 直接挂在 inode 的
`private_id` 下。

五个文件的 extent 实测（都是单 extent、logical 0）：

| 文件 | 数据流 oid | logical | extent len | paddr | 实际读 |
| --- | --- | --- | --- | --- | --- |
| `/usr/lib/dyld` 资源叉 | 1152921500312216350 | 0 | 327,680 | 1,171,030 (0x11de56) | 325,642 |
| `/sbin/launchd` 资源叉 | 1152921500312216240 | 0 | 208,896 | 1,170,621 | 205,547 |
| `/usr/bin/fileproviderctl` 资源叉 | 1152921500312216274 | 0 | 90,112 | 1,170,751 | 89,331 |
| `/usr/bin/zprint` 资源叉 | 1152921500312216304 | 0 | 12,288 | 1,170,990 | 11,486 |
| `SystemVersion.plist` 本体 | 1152921500312031246 | 0 | 4,096 | 221,049 | 574 |

读取器按 `logical_addr` 排序，遇到 `logical > 已写长度` 的空洞补零，遇到重叠
extent 从重叠点续读，`crypto_id != 0` 则直接报错而不是输出密文。**本卷
`APFS_FS_UNENCRYPTED`（卷超级块 flags@264 = 0x1），这五个文件的 crypto_id
都是 0，所以没有解密步骤。** 这五个文件没有多 extent 或空洞，多 extent/sparse
代码路径因此没有被这组样本触发，但实现已经在 `read_stream()` 里。

### 二、压缩：两个目标都是 decmpfs type 4（zlib 资源叉）

两个文件的 inode 都有 `bsd_flags = 0x20`（`UF_COMPRESSED`），
`internal_flags = 0x44000`（含 `INODE_HAS_UNCOMPRESSED_SIZE`）。inode 偏移
84 的 `uncompressed_size` 就是解压后大小，和 decmpfs 头一致。

dyld 的 `com.apple.decmpfs` 原始 16 字节：

```text
66 70 6d 63   magic = 0x636d7066 ("cmpf"，小端存储)
04 00 00 00   compression_type = 4
20 15 0e 00 00 00 00 00   uncompressed_size = 922,912
```

**实测 type 4 是“64 KiB 分块的 zlib 资源叉”，不是 LZFSE。** 每个 chunk 的开头
是 `78 5e`（zlib），`zlib.decompress()` 成功；LZFSE 的 decmpfs 类型是 11/12。
工具按类型分派：3 = xattr 内 zlib，4 = 资源叉 zlib，7/8 = xattr/资源叉
LZVN，9/10 = 原始数据，11/12 = LZFSE。LZVN 用 `lzfse` 模块的 C 解码器：
把裸 LZVN chunk 包成 `bvxn + n_raw + n_payload + payload + bvx$` 的单块
LZFSE 流再解。本卷这五个文件只命中 type 4 和“未压缩”，7/8 分支没有样本。

type 4 的资源叉布局（dyld，325,642 字节）：

```text
00 00 01 00   data_offset   = 0x100 (256)
00 04 f7 d8   map_offset    = 325,592
00 04 f6 d8   data_length   = 325,336
00 00 00 32   map_length    = 50
...
0x104: 0f 00 00 00             chunk_count = 15 (little-endian)
0x108: 7c 00 00 00 21 81 00 00  chunk 0: offset=124, length=33,021
       ...
0x104 + 124 = 0x180: 78 5e ...  zlib stream
```

chunk 的 `offset` 相对 `0x104`，不是相对文件头，也不是相对表尾；这是最初把
zlib 数据读偏 124 字节的原因。dyld 15 个 chunk 解出
`14 * 65,536 + 5,408 = 922,912`；launchd 9 个 chunk 解出
`8 * 65,536 + 17,312 = 541,600`。chunk 首字节为 `0xFF` 时表示原样存储
（跳过该字节），这两个文件没有命中。

### 三、验证

`scripts/apfs-get.py` 写出的文件（`~/ios-userland/extracted/`）：

| 路径 | inode | 字节数 | sha256 | Mach-O |
| --- | --- | --- | --- | --- |
| `/usr/lib/dyld` | 1152921500312216349 | 922,912 | `5806834564fe6462ae981d0915d612b1eb2f6f168822b5d278bee56254933757` | 64-bit arm64 **dylinker**，ncmds 18 |
| `/sbin/launchd` | 1152921500312216239 | 541,600 | `12370e5ee35bf28ae479b6c4b4455f4f4693ab803e37c5eb3032537c18cac61c` | 64-bit arm64 **executable**，ncmds 19 |
| `/usr/bin/fileproviderctl` | 1152921500312216273 | 259,248 | `9bc96d8ab9c5c9db54594bcdbcbc0d25de20396fdd9212866615aecef9c05251` | executable，ncmds 33 |
| `/usr/bin/zprint` | 1152921500312216303 | 88,352 | `a140e627295f7b45cfafbdc8c8f008a0db6505736022557dfeb930543cc587df` | executable，ncmds 22 |
| `SystemVersion.plist` | 1152921500312031246 | 574 | `526de9e26c80f93fae3089725f9e24cf1df55997e828d571db1e0f6480bf1962` | XML plist |

`file(1)` 对前四个的判断：

```text
usr/lib/dyld:      Mach-O 64-bit arm64 dynamic linker, flags:<NOUNDEFS|DYLDLINK|TWOLEVEL>
sbin/launchd:      Mach-O 64-bit arm64 executable, flags:<NOUNDEFS|DYLDLINK|TWOLEVEL|PIE>
usr/bin/fileproviderctl: Mach-O 64-bit arm64 executable, flags:<NOUNDEFS|DYLDLINK|TWOLEVEL|PIE>
usr/bin/zprint:    Mach-O 64-bit arm64 executable, flags:<NOUNDEFS|DYLDLINK|TWOLEVEL|PIE>
```

Mach-O 头按 32 字节解析（magic `0xfeedfacf`，cputype `0x0100000c`）：

| 文件 | LC_UUID | LC_BUILD_VERSION | 辨识字符串 |
| --- | --- | --- | --- |
| dyld | `a5419919-ee95-5430-8e54-ae6387a9fa9a` | platform 2 (iOS), minos 16.7.0, sdk 16.7.3 | `dyld-1067` |
| launchd | `07db155c-bcb3-f33d-b62f-3e498b993b8e` | platform 2, minos 16.7.0, sdk 16.7.3 | `/sbin/launchd` |
| fileproviderctl | `01cd2cd0-3396-023d-aa11-64101405751d` | platform 2, minos 16.7.0, sdk 16.7.3 | `/usr/lib/dyld` |
| zprint | `d46c14f4-8b08-c431-861d-ec0ec9dcf605` | platform 2, minos 16.7.0, sdk 16.7.3 | `/usr/lib/dyld` |

dyld 的 `dyld-1067` 是二进制内部的版本串；launchd 的 `/sbin/launchd` 是它自己的
路径/名字。SystemVersion.plist 的内容进一步自证：

```text
<key>ProductBuildVersion</key><string>20H392</string>
<key>ProductVersion</key><string>16.7.16</string>
```

输出大小和 inode 的 `uncompressed_size` 全部逐字节相等（`size_ok=True`）；
plist 的 inode 偏移 84 为 0，真正大小在 inode 尾部偏移 128 的
`j_dstream_t`（size=574, alloced=4096），extent 分配 4096 字节，只读 574 字节。

### 四、range 请求成本

单独抽 dyld 的计数（包含 ZIP 目录、DMG koly/XML、APFS 元数据，不只是文件
数据）：

| 场景 | HTTP GET | 收到字节 | DMG chunk | chunk 压缩字节 |
| --- | --- | --- | --- | --- |
| 单 chunk 缓存（旧行为） | 115 | 25,133,339 (24.0 MiB) | 105 | 23.7 MiB |
| 32 chunk LRU（当前） | 24 | 3,664,368 (3.5 MiB) | 14 | 3.2 MiB |
| 五个文件一次抽（当前） | 31 | 7,322,711 (7.0 MiB) | 21 | 6.7 MiB |

旧行为不是网络慢，而是缓存抖动：dyld 只访问 16 个不同的 APFS 块，但走了 112
次 `read_block`；单 chunk 缓存每次都被下一个块顶掉，于是 16 个块反复触发
105 次 1 MiB 块的下载。给 `DmgReader` 加 32 个解压 chunk 的 LRU 后，字节数
降到 1/6.9，请求数降到 1/4.8。`extract-ios-userland.py --list` 和
`--dmg-info` 仍正常，缓存只影响重复读的性能。

### 五、对 P1 根卷的意义

- **单文件路径已经打通**：路径 → drec → inode → extent → decmpfs，五个不同
  大小的文件都逐字节对上了 inode/decmpfs 的大小。`apfs-get.py --get` 是
  P1 取 `/usr/lib/dyld`、`/sbin/launchd` 及其依赖的现成入口。
- **整卷不能按“每个文件重新走一遍路径 + 单 chunk 缓存”硬抽**。OS 卷有
  1,375,744 个 4 KiB 块（5,635,047,424 字节）、142,313 个文件；单文件 3.5 MiB
  的元数据开销乘以几十万会回到几百 GiB 级。正确做法是：
  1. 顺序遍历一次 fs-tree 叶子，按 `(obj_id, type)` 归并 inode/drec/extent，
     不按路径重复下降；
  2. 把 32 chunk 的 LRU 扩大或落盘（3,638 个 BLKX run，全放内存约 3.6 GB），
     顺序读 extent，避免随机 4 KiB 读；
  3. 按目录流式写文件，跳过大 xattr/稀疏空洞，先只抽 P1 需要的
     `/usr/lib`、`/sbin`、`/System/Library` 子集。
- **磁盘预算要算清**：OS DMG 是 4.73 GB（stored），APFS 卷是 5.63 GB，当前
  `/home/albert` 只剩约 8.4 GB。整卷解压写盘会顶到上限，P1 根卷应分阶段：
  先解出启动必需的用户态，再按需补库；不要把 4.7 GB 压缩镜像和 5.6 GB
  解压结果同时留在盘上。

## SystemOS cryptex

`087-86593-047.dmg` 在 BuildManifest 里是 `Cryptex1,SystemOS`。它**不是 DMG**：
最后 512 字节全零，没有 koly、没有 BLKX、没有分区表，0x20 处直接就是 `NXSB`。
处理它的方式和 OS 卷完全不同。

### 形态

| 项目 | 值 |
| --- | --- |
| 成员大小 | 1,097,727,251 压缩 → 2,743,074,816 |
| 压缩方式 | **deflate**（不是 stored） |
| 容器 | 4096 字节块 × 669,696 = 恰好等于成员大小 |
| 容器 UUID | `07490cba83ee4f50a4153de222723455` |
| 卷 | `SydneyUpdate20H392.D22D221SystemCryptex` |
| 内容 | 1,094 文件 / 634 目录 / 2 符号链接 |

deflate 是**一条连续流**，所以 OS 卷那种按 1 MiB 块 range 读取的做法在这里用不了：
必须整体解压（1.09 GB 成员，9.5 秒）。解压之后块地址就是 `块号 × 4096`，可以随机访问。

### 里面是什么

90.9% 是**拆分的 dyld 共享缓存**：

```
dyld_shared_cache_arm64        393,216
  + .01 … .40                  40 个分片
  + .27/.41.dylddata
  + .28/.42.dyldlinkedit
  + .symbols
= 44 个文件，2,384,232,448 字节
```

头部取自块 4405，内容为 `dyld_v1   arm64`。

其余是 `/System/{DriverKit,Library/{Accounts,Caches,CoreServices,Frameworks,PrivateFrameworks}}`
和 `/usr/lib` —— 5 个 Frameworks、17 个 PrivateFrameworks，全部是 WebKit/Safari。

### 三条路径的结果

| 路径 | 结果 |
| --- | --- |
| `System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64e` | **该名字不存在**；缓存以拆分形式存在（见上）|
| `usr/lib/libSystem.B.dylib` | **不存在**。`/usr/lib` 只有 6 个条目，OS 卷里也没有 |
| `usr/lib/dyld` | **不在 cryptex**，在 OS 卷（922,912 字节）|

### 对 P1 的结论

根卷必须同时包含两块：

```
OS 卷        → /usr/lib/dyld
SystemOS     → 挂到 /System/Cryptexes/OS，提供 44 个缓存分片
             → 以及 WebKit/Safari 的 22 个框架
```

**libSystem 在任何地方都不是一个文件**，它由共享缓存提供。缓存没挂上，libSystem 就
不存在 —— P1 会在第一条 libSystem 调用上失败。

### 技术要点

OS 卷那边卡在容器 omap 的虚拟 oid 转换上；cryptex 这边**不需要转换**：每个 fs-tree
节点都是物理对象（`o_subtype 0xe`），扫块头就能找到叶子节点。

唯一的坑是 value 基址：应当是 `block_end - voff`，而不是 `- voff - vlen`。
用前者子节点 id 100% 落在范围内，后者只有 47%。

634 个目录全部走完，正好等于卷超级块里的 `apfs_num_directories`，说明遍历是完整的。
