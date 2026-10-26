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
