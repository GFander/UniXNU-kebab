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
