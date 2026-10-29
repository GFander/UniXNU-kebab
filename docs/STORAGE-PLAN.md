# HiOS 存储方案：参考 256GB iPhone X / iOS 16

2026-10-28。用户已指定手机专用于 HiOS，要求分区容量参考真实 256GB iOS 设备。主机镜像与布局通过验证后，用户已明确批准覆盖 userdata；部署完成，原版 APFS System 只读根挂载已真机通过。

## 已核实的依据

- Apple 的 [APFS 说明](https://support.apple.com/guide/security/role-of-apple-file-system-seca6147599e/web)确认：容器内各卷按需共享空间，iOS 至少包含 System 和 Data。该说明不提供某台 256GB iPhone 的逐卷固定容量。
- 本地原始 iPhone X IPSW 是 iOS 16.7.16 / 20H392，选用 d22ap 的 Erase identity；`MinimumSystemPartition=5374`，`SystemPartitionPadding[256]=1280`。恢复客户端也会传递该 padding 表，见 [idevicerestore](https://github.com/libimobiledevice/idevicerestore/blob/master/src/restore.c)。
- 解码后的 OS APFS 容器恰为 5374 MiB。这验证了上述最小系统容量的单位；`5374+1280=6654 MiB` 可作本方案系统空间预算参考，**不是实测真机固定分区或强制 APFS quota**。
- 尚无与本版本相同的真实 256GB iPhone X 完整磁盘转储。本方案按官方卷模型、原始恢复输入及本机实测容量设计，不能称为原机 GPT 的逐扇区复制。

## 容量和卷模型

本机现有 userdata：241,008,750,592 字节，229,843 MiB，约 224.457 GiB / 241.009 GB。256GB 是参考设备的标称容量，不能把它直接写成此分区的容量。

| 内容 | 规划 |
| --- | --- |
| 主 APFS 容器 | 已在主机工作镜像中扩容为 241,008,750,592 字节，与 userdata 完全一致；外层不再嵌套 GPT |
| System | 保留 iOS 16 原版 System 卷、卷 UUID 和内容；当前镜像 5374 MiB，以 6654 MiB 作为容量预算参考；共享池内不先虚构固定 System 分区 |
| Data | 同一容器的可写 Data 卷，使用其余共享空间；不再设 16/32 GiB 的开发上限 |
| Preboot / 其他辅助卷 | 按 iOS 16 启动任务实际需要建立角色与挂载关系，按需占用共享池 |
| SystemOS / AppOS | 保持 Cryptex 映像的独立身份；它们是镜像输入，不能直接当作两块真实 iPhone 固定 GPT 分区。持续存储位置与挂载入口需按 iOS 16 合约确认 |
| Apple 专属固件/SEP/NAND 区域 | 不复制到高通分区表，不声称本机实现 Apple 安全硬件 |

Qualcomm 启动链、boot/recovery、modem、persist 等仍承担本机固件职责。当前方案只使用现有 userdata，不在手机外层 GPT 中搬动或回收其他分区。

## 已准备的原版输入

目录：`D:\LOS\HiOS\apfs-inputs-20H392-ready`。三个容器均为 4096 字节块，NXSB 校验通过；卷超级块校验通过，且对应对象 ID 均在容器的文件系统对象列表中。

| 输入 | 原始容器字节数 | 容器 UUID | 卷 UUID / 角色 |
| --- | ---: | --- | --- |
| OS | 5,635,047,424 | 0e25f331-6279-458a-9cfe-f39598da17aa | dadd93f8-dbbf-4b17-a1df-0f11e64172f7 / System (0x1) |
| SystemOS | 2,743,074,816 | 07490cba-83ee-4f50-a415-3de222723455 | 96096b28-b6dc-4bc7-9d3a-2632a0fe53f1 / 0x0 |
| AppOS | 10,485,760 | bed6515c-1504-40fa-80e7-ddc4cba65239 | 9e10dda7-90d9-4677-844a-e984bb1ccd0a / 0x0 |

哈希与源文件对应关系在 `inputs.json`，卷信息在 `volumes.json`，256GB 参考参数在 `storage-reference-256gb.json`。OS DMG 内的 512 字节扇区 GPT 包装已剥除，输出从 APFS NXSB 开始；这避免把其分区表误作 4 KiB UFS 的分区表使用。

## 已构建的主机安装镜像

在 macOS 14.6.1 上克隆原版 OS 容器，通过 `diskutil apfs resizeContainer` 合法扩容，再建立 APFSX Data 和 Preboot。所有设备引用均先核对 `hdiutil info`，只操作 `/Users/albertlee/kebab-build/apfs-20H392/HiOS.userdata.work.raw` 所属虚拟设备；工作镜像已卸载。

| 卷 | UUID | 当前占用 | quota / reserve |
| --- | --- | ---: | --- |
| System | DADD93F8-DBBF-4B17-A1DF-0F11E64172F7 | 5,259,280,384 字节 | 0 / 0 |
| Data | 4C56203C-CFE8-4DFF-BF8E-BB48D852C936 | 20,480 字节 | 0 / 0 |
| Preboot | EF45F98E-9F6C-43DB-B7C6-F418CA77429B | 20,480 字节 | 0 / 0 |

容器共享空闲空间为 235,612,561,408 字节，之后随使用变化。Data、Preboot 目前为空；卷角色已经建立，但 iOS 所需内容、挂载关系、System/Data 配对与 Cryptex 装配尚未完成，不能称为完整 iOS 安装。

`diskutil verifyVolume` 对容器和三个卷检查均通过。System 只读读取的 launchd、dyld、launchd.plist 哈希记在 `host-layout.json`。容器 features/ro-compatible/incompatible 为 `0/0/2`；System 为 `18/0/8`（与原始 OS 卷相同），Data、Preboot 为 `2/0/8`。新卷由 storagekitd 2236.141.1 建立，手机 APFS 2142 已解析扩容后的容器并挂载 System；Data/Preboot 仍须分别实测。

Android sparse 工件 `HiOS.userdata.sparse.img` 为 5,385,490,472 字节，逻辑展开为完整 241,008,750,592 字节。337 个 chunk 仅使用 RAW 和零 FILL，**没有 DONT_CARE**；主机洞显式写零。导出后逐项比较源文件所有已分配字节，并检查空洞范围；Windows 再次验证 sparse 结构、总块数、卷身份、boot 的 FAT 文件及 owner 测量值。

- userdata 工件 SHA256：`d0537ffeaab0d25c99358a716d167d9880d3d8752c052e4b0a78e5a9adedd7bc`
- boot-0187-ro.img SHA256：`4890903bb8e0715c6f982954253a68ebd58977c0aa082b7131616e74c78b7a2b`
- 清单：`D:\LOS\HiOS\apfs-inputs-20H392-ready\install-0187.json`
- 安装步骤：`D:\LOS\HiOS\apfs-inputs-20H392-ready\INSTALL-0187.md`

fastboot 实测 userdata 为 `0x381D3DB000`，最大下载块为 805,306,368 字节。安装仅写 userdata 和 boot_b，不改外层 GPT。**用户明确确认后，userdata 的22个分块均 Sending/Writing OKAY，部署已完成。后续 0188–0190 只更新 boot_b。** `deployment-0187.json` 是实际设备观测，`install-0187.json` 是部署前输入清单。

## 实施顺序

1. 主机扩容、共享池、辅助卷角色、feature flags 和文件读取已验证；手机 APFS 2142 容器解析与 System 只读挂载通过，辅助卷/持久写入尚未通过。
2. 用现有原版卷 UUID 准备内核和 ADT：0187 的 `ufsmedia_root=2` 先启动 UFS provider，再保留标准 `/chosen/root-matching`，让它选择 APFS **卷**，而非 0186 诊断使用的整块 disk0。
3. 精确布局、镜像清单和可回读工件已准备，用户最终批准并完成重建 userdata；不重复覆盖已有 APFS 容器。
4. 第一轮保持 `ufsrw=0 rootrw=0`，以 APFS 根卷挂载、卷身份和已知文件读取为验收；随后再做 Data 写入、同步、重启持久化和完整 launchd 启动。

若旧驱动不接受主机扩容后的格式，保留原版镜像，先调整构建/格式化工具或存储后端；不通过改几个长度字段冒充合法 APFS 扩容。

## 当前真机基线

0187 起：`IOBlockStorageDriver → IOMedia → AppleAPFSContainerScheme → AppleAPFSMedia → AppleAPFSContainer → System`；选中 `disk1s1 major1/minor2`，原 UUID 保留，检查点 xid260，58840027 个4KiB块。VFS_ROOT、devfs 和后续 exec 均已执行。0188 补齐供体原版 decmpfs 解码器；0189 通过加载器装入原版 2760 条静态信任记录，真实 launchd/dyld 进入 libignition。

当前根封印/命名快照认证仍有 error80/81，触发原有开发继续路径；不声称 Apple SSV 认证通过。新 Data/Preboot 尚为空、无卷组配对，Cryptex 和共享缓存未装配，完整用户态与持久化仍在后续阶段。

0190 通过既有 `INST CHECK` 完整读取两个压缩文件，大小和 SHA256 与主机原版一致：launchd 541600 字节、`12370e5ee35bf28ae479b6c4b4455f4f4693ab803e37c5eb3032537c18cac61c`；dyld 922912 字节、`5806834564fe6462ae981d0915d612b1eb2f6f168822b5d278bee56254933757`。`INST?` 确认 root ro、active0、entries0/0；仅启用读取诊断，没有安装流写入。launchd.plist 的 CHECK 被既有路径白名单拒绝，不计作手机哈希通过。验收清单为 `device-evidence-0190.json`。

2026-10-29 收尾状态：boot_b 为0190，手机已通过正常用户态串口 FASTBOOT 返回引导器，`fastboot devices` 再次确认 c073a488；没有后台刷写命令。

本方案的验收范围仍是 HiOS 开发启动；Apple 原厂密钥、Secure Enclave 绑定、设备激活和真实电源管理不能由相同卷名或相似布局推导为可用。
