# HiOS / UniXNU-kebab 开发路线

更新：2026-10-29（香港时间）。当前已验证0225；七小时持续开发截止09:22:25。驱动实施、当前镜像及后台任务以 standardization-run-20261029/RUN.md 为准。目标是 OnePlus 8T / SM8250、16 KiB 页、iPhone X iOS 16.7.16（20H392）。

当前位于 UniXNU 指南“阶段 6：存储与根卷”向用户态交接的位置。**0187 起已从有效 APFS System 卷只读挂载根文件系统；0188 补齐原版 decmpfs 解码器；0189 装入原版静态信任缓存，真实 PID 1 的 dyld/libignition 已执行并输出 Darwin Ignition Sequence。** 当前最后输出停在 ignition 参数，完整 launchd 启动任务、mount-phase-1 和 SpringBoard 尚未通过。历史 HFS+ 有 mount-phase-1 记录，不能计作当前 APFS 路线已通过。

本文取代旧路线图中“APFS 不在关键路径、以 HFS+ 为终点”的结论。本轮以原版 APFS 和 prelink 为主线，HFS+ 只保留为历史回归基线。

0216/0217已在真实Preboot卷完成文件写入、APFS同步、UFS缓存刷新和只读重启后的完整SHA验证（65559字节，checkpoint从260到261），System两文件完整SHA不变。Data仍未挂载：0214准确找到Data后，被原版APFS的未加密Data检查拒绝。PID1的具体等待为`security.mac.img4.ignition_blob`；原版Image4需要尚未实现的派生key selector2212。不能把通用APFS持久化当作Data保护或完整启动链已完成。

## 目标怎样验收

| 目标 | 必须看到的结果 | 当前位置 |
| --- | --- | --- |
| 内核启动 | XNU、平台专家、CPU/定时器和 BSD 初始化 | 已实测，当前固定 cpus=1 |
| iOS 用户态启动 | APFS 根卷挂载，真实 /sbin/launchd 作为 PID 1，dyld 与启动任务持续运行 | 根卷挂载和真实 PID 1 早期 dyld/libignition 已通过；完整启动任务未通过 |
| 真实 iOS 界面 | backboardd 和 SpringBoard 存活，真实锁屏/桌面上屏 | 未验证 |
| 可交互系统 | 触摸和按键经 HID 到达界面，显示交换和 Data 写入可持续工作 | 部件有既有实现，当前整链未验证 |
| 可日用系统 | 稳定重启、持久 Data、电源/温控、网络、音频等通过实测 | 后续阶段 |

不能把指南阶段数换算成完成百分比。剩余工作包含尚未运行的私有 ABI 和用户态路径，修掉当前 panic 不代表下一轮就能出桌面；现阶段不承诺日历工期。

## 对照 UniXNU 与 Sheng

已阅读官方[移植入口](https://unixnu.com/zh/porting/)、[启动验收](https://unixnu.com/zh/porting/bring-up)、[prelink 原理](https://unixnu.com/zh/concepts/prelink)、[UFS](https://unixnu.com/zh/porting/ufs)、[显示](https://unixnu.com/zh/porting/display)和 [GPU 指南](https://unixnu.com/zh/porting/gpu)。

Sheng 参考固定为 `UniXNU/UniXNU-sheng@08cde20edc3254ed6f7df8d0f5384fb887786e8f`，本轮核对了 README、board.toml、sheng_defconfig、porting.toml、PMGR 实现及其观测检查脚本。

| 内容 | Sheng 的参考价值 | kebab 的边界 |
| --- | --- | --- |
| prelink | 源码 XNU + 指定 kernelcache 的原版 kext，依赖闭包、指针重绑定、稀疏索引和早期安全初始化 | 保留此方案；每个私有地址、结构和 vtable 按自己的供体重新审计 |
| OS/硬件 | 当前配置为 SM8550 + iPadOS 17.7.11（21H461） | 我们是 SM8250 + iOS 16.7.16；不复制其地址、固件、分区边界或页表设置 |
| 存储 | 原生控制器后端发布 IOMedia；OS/SystemOS 窗口、根卷身份与 Data 分开处理 | 复用分层和验收方法，保留本机 UFS 接管代码及实际 GPT |
| 平台服务 | 明确 provider、实例大小、原版 nub、注册顺序与观测点 | PMGR 兼容不等于真实时钟/休眠/电源管理；每项能力单独验证 |
| 图形 | MobileFramebuffer/IOSurface、GOP 呈现与 GPU 后端分层 | 先验证 CPU/GOP 呈现；A740 的后端不能当作 A650 已可用 |
| 工程记录 | porting.toml 把源码、主机检查与设备观测分开 | 逐项记录最后成功检查点和镜像身份，避免“编译通过=设备完成” |

参考源码：[board.toml](https://github.com/UniXNU/UniXNU-sheng/blob/08cde20edc3254ed6f7df8d0f5384fb887786e8f/board.toml)、[sheng_defconfig](https://github.com/UniXNU/UniXNU-sheng/blob/08cde20edc3254ed6f7df8d0f5384fb887786e8f/configs/sheng_defconfig)、[porting.toml](https://github.com/UniXNU/UniXNU-sheng/blob/08cde20edc3254ed6f7df8d0f5384fb887786e8f/porting.toml)、[ShengPlatformPower.cpp](https://github.com/UniXNU/UniXNU-sheng/blob/08cde20edc3254ed6f7df8d0f5384fb887786e8f/package/ShengPlatformPower.cpp)。

## 当前能力与缺口

| 子系统 | 证据与状态 | 下一项验收 |
| --- | --- | --- |
| 固件/加载器 | ABL → UEFI → UniXNU → DarwinBoot → XNU 已在真机运行 | 固定可复现的打包输入；固件 USB 与内核 CDC 分别记录 |
| CPU/中断 | 0184 起原生 KebabCPU：1/1 CPUs initialized，SGI1 verified | 根卷稳定后再逐核启用 SMP，不与 APFS 故障混测 |
| 诊断/恢复 | panic 后 FWLOG成功；0192正常及0193主动panic后串口FASTBOOT均实际返回引导器 | 保留失败现场与可靠回退通道 |
| prelink | 修复映射上界、16 KiB 布局和 vtable；供体代码哈希保持不变 | 固定供体、工具版本、补丁顺序和导入绑定表 |
| 密码学/安全启动服务 | 原版AMFI策略与静态缓存已装入；Image4模块/MAC初始化通过，IO服务仍等待AES。官方SoftwareAES经20H392 ABI重审后，真机CBC128/192/256及descriptor/raw接口通过 | selector2212未实现；软件CBC不提供苹果派生key。先确认板级安全固件/密钥接口，再落实nonce与Data内容保护；AES实验默认关闭 |
| PMGR | 0185 发布原版 provider-less 开发 nub，AMFI 完成 | 当前读值 0xffffffff、写无操作；不是持久 AON/真实 PMIC 实现 |
| UFS/IOKit | 原版10绑定/22槽位保持；0216真实Preboot VFS写与UFS同步、0217冷启动读取通过 | 控制器provider、IRQ/SMMU/SMP仍待；Data写入必须另过内容保护资格 |
| APFS | 原版APFS2142 System只读根；Preboot dev1/4读写/重启SHA通过，xid261；正常reset已补齐多卷同步 | Data加密挂载、封印和完整用户态未通过 |
| 根卷内容 | System/Data/Preboot共享241008750592字节，System UUID不变；Preboot现仅有固定探针文件 | Data仍空且未配对；SystemOS/AppOS及真实Preboot引导内容未装配 |
| 用户态 | 真实PID1进入libignition；0204–0206确定sysctl→Image4 gate及ignition_blob OID；0213定位selector2212请求 | 接通真正密钥/nonce能力后再观察下一步骤；随后Cryptex/Data/DSC与mount-phase-1 |
| 显示 | GOP/内核画面已见；0191修复Surface注册及生命周期；0192真实内核factory打开Surface/AppleCLCD并查询、拒绝坏输出、关闭通过 | 仍需用户进程及原版iOS16框架验证surface、swap、通知与未恢复的selector74布局 |
| 触摸/按键 | 当前触摸实现选用 Synaptics S3908/TouchComm + GENI；按键经PM8150/SPMI，均为devfs队列；当前日志仅确认触摸映射 | 补入目标ABI相容的HID家族，实测事件经IOHID到达backboardd |
| GPU/网络/音频 | 当前启动链没有端到端证据 | GPU/SMMU按B8在直扫和桌面之前；网络/音频另按硬件依赖验收，不能套用Sheng状态 |

### APFS 的独立检查点

1. **依赖初始化**：corecrypto、Image4、AMFI 可用，且没有早期重复注册。0185 已通过。
2. **文件系统注册**：apfs_module_start 与 sysctl 注册。0185 已通过。
3. **真实块接口**：匹配 Generic IOBlockStorageDevice，观察 IOMedia 子服务、BSD 名称和 dev_t；根设备使用同一媒体身份。0186 已通过。
4. **有效介质**：0187 真机识别合法检查点和原版 System UUID，只读根挂载通过；0190 完整读取 launchd/dyld 并与主机 SHA256 一致。此项只读链路通过，尚不包含整卷逐字节校验。根封印和命名快照认证尚未完成，当前触发原有开发继续路径。
5. **完整根卷启动**：0189 已执行真实 PID 1 的早期 dyld/libignition；SystemOS/Cryptex、Preboot、Data 与完整启动任务尚未通过。
6. **通用持久写**：0215 Preboot只读挂载，0216独占新建固定文件并全SHA写读/同步，0217 UFS只读重启后再全SHA通过。Data role的未加密卷被原版APFS拒绝；这一步不证明Data加密/内容保护、配对或密钥持久化。

0186 的 `unable to root ...: 2` 和最终 errno 19 是失败现象，不足以单独判定 APFS 哪层实现有错。必须先提供有效 APFS 介质，再区分卷选择、IOMedia 映射、容器解析和实际块读取。不能继续把 Android 的 userdata 当作已安装的 iOS 卷测试。

已核对本地原始 IPSW 的 BuildManifest：iOS 16.7.16 / 20H392，支持 iPhone10,3 和 iPhone10,6；`OS=087-86698-044.dmg`、`Cryptex1,SystemOS=087-86593-047.dmg`、`Cryptex1,AppOS=087-86715-047.dmg`，并有对应 trust cache/root hash。DMG 文件大小不等于解压后卷容量，部署前必须转换、解析并计算实际窗口。

## 开发顺序与完成条件

执行顺序采用用户的 `STANDARDIZATION-PLAN.md`，GPU/SMMU在直扫和桌面之前。CPU/GOP路径保留为GPU读回验证出口。

| 阶段 | 工作包 | 具体交付与完成条件 |
| --- | --- | --- |
| A1 已通过 | SPMI/PMIC/RSC分层 | 总线与客户端拆分；真实provider/nub/personality；普通事务序列化，持锁panic FASTBOOT保持 |
| A2 已通过约定范围 | GENI/TCM分层及正常身份 | 保留FIFO/bitbang/TCM；整命令/整帧事务锁；GENI/Touch实际匹配/类型化状态；实际触摸采样和HID另验 |
| A3/A4 已形成工件 | 全驱动回审、新驱动规范 | 10篇指南逐项差距、manifest/DRV?、逐Apple类ABI记录；后续每项能力继续按硬件和用户态证据验收 |
| B1 部分通过 | APFS持久写与Data保护 | Preboot真实持久写已通过；System保持只读。Data需先解决未加密拒绝、内容保护与密钥，再做独立写/同步/重启读取 |
| B2 当前 | Image4 nonce/密钥后端 | ignition_blob OID及selector2212已定位，软件CBC已验收；确认板级SCM/QSEE等可用接口和真实密钥语义，接通nonce能力后观察下一步 |
| B3 | Data/Preboot装配 | Preboot实际只读/RW挂载已过；System/Data配对、初始目录、keybag及Preboot引导输入仍待 |
| B4 | 动态信任缓存 | 按目标格式/签名输入加载，记录来源、条目与实际验证行为 |
| B5 | Cryptex/SystemOS/AppOS | 原版映像、manifest、trust cache、挂载路径和身份对应 |
| B6 | 共享缓存 | 正确文件/子缓存、UUID、映射/slide与实际dyld使用 |
| B7 | 完整启动任务 | launchd持续推进，mount-phase-1及所需任务通过 |
| B8 | GPU/Metal | 第0步SMMU，分别落实A650/GMU固件、a6xx寄存器、GPU/DPU stream/context；提交/完成/读回/CA合成 |
| B9 | backboardd与显示输入 | DPU直扫GPU缓冲、扫描中所有权、真实vsync与120Hz、HID完整事件链 |
| B10 | SpringBoard | FrontBoard/RunningBoard/SpringBoardServices到真实锁屏/桌面，并验证交互 |

B1通用写能力与B2诊断可独立推进；Data保护和Image4现在共同依赖实际密钥后端，不能用软件CBC自测成功代替。B3依赖Data资格；B4→B5→B6→B7之后推进B8→B9→B10。SMMU是GPU/直扫共同依赖，也需按所选其他DMA控制器的真实需求前置。真实事件/缓冲生命周期是验收内容，服务名称与成功返回不能替代它。

### 最近三项任务

1. 基于已确认的Image4 selector2212和Data未加密拒绝，先做板级安全固件接口的只读能力调查，再设计实际密钥/nonce/内容保护适配；默认关闭不完整的AES实验。
2. 保留Preboot持久写的65559字节/SHA/xid261证据；在加密Data能正确挂载后，单独重复同等级持久化资格，验证System只读及原版文件不变。
3. 按B3–B7装配卷、真实信任/manifest、Cryptex与共享缓存；随后按B8 SMMU/GPU、B9直扫/HID、B10桌面逐级验收。

用户已选择手机专用于 HiOS，并要求参考真实 256GB iOS 设备。采用 System/Data 共享 APFS 容器，Data 使用余下共享空间；本机 userdata 实际为 241,008,750,592 字节，不冒充完整 256GB。原始恢复输入给出 5374 MiB 系统容器及 256GB 档 1280 MiB padding；6654 MiB 仅为预算参考，不能当作真机固定分区。详细布局见 `STORAGE-PLAN.md`。**用户最终确认后已覆盖 userdata，22 个分块全部 OKAY；未修改手机外层 GPT。不整体重刷userdata；常规刷测更新 boot_b。**

三个APFS原版输入已在电脑解码，System UUID为`dadd93f8-dbbf-4b17-a1df-0f11e64172f7`。共享容器已部署；Data仍为空，Preboot仅新增受控探针文件，Cryptex未装配。本窗口没有重新整体刷写userdata。Preboot真实挂载/持久写已验，Data保护、封印及完整启动链各自保留待验收项。

## 本轮证据位置

Windows：`D:\LOS\HiOS\p1-run\prelink-fix`。0217为最新已验证只读镜像；0216-preboot-write仅为已完成的一次固定文件写入测试，O_EXCL禁止重放覆盖。0205栈与0206 sysctl日志保存等待链；0210/0213保存AES资格与2212失败；0214保存Data策略拒绝；0215–0217保存Preboot挂载、同步、xid261与完整SHA。各regression.json保存其余回归，0216-write-session.json区分native proof哈希与安装器白名单拒绝。整体部署仍引用deployment-0187.json，不重复整体写userdata。

源码：Ubuntu `/home/albert/kebab-dev-ios16`；实验内核 `/home/albert/s4/x`；Mac实际 `/Users/albertlee/kebab-build/dxb/xnu`。0173/0174为首批标准化；0175–0184新增总线分层、服务/诊断、devfs和阻塞观测。正式补丁按文件名顺序重放；本轮manifest-0204.json已核对列出的路径在实际两树与独立重放树一致。正式ADT生成器已加入SPMI/GENI资源节点及保留原启动安全输入的回归检查。

AMFI 的开发启动集哈希只测量输入，不代表 Apple 签名认证或对整块根卷的验证。当前 PMGR 兼容、APFS 注册、只读挂载和完整用户态分别记录，不能相互替代。

## 驱动规范审计（2026-10-29）

独立设备/平台功能域仍为9类，另计Surface、PMGR开发入口、SoftwareAES与APFS探针。manifest-0217列24个主要实现文件、19个类及11个实际registry角色；原13文件/10类与0204的21文件/18类保留为历史。源码一致性只覆盖清单列明路径，已在Ubuntu、Mac实际树与独立重放树逐文件核对。新增文件和类数不等于新增硬件驱动。

0173/0174的显示/Surface/存储合约保持；A1/A2总线、正常服务和原协议已逐批回归，0203修复按键节点早于devfs发布的问题。实际触摸/HID、正常USB服务生命周期、真实电源、私有图形用户ABI、SMMU/GPU/直扫仍有明确待验收项。

10篇指南回审见 [A3-GUIDE-REVIEW.md](standardization-run-20261029/A3-GUIDE-REVIEW.md)，执行记录见 [STATUS.md](driver-standardization/STATUS.md)，实时续跑见 [RUN.md](standardization-run-20261029/RUN.md)。`standardization-run-20261029/abi/`逐类记录了已核实的存储/PMGR事实与尚未恢复的图形布局，未把原生factory自测等同于原版框架或backboardd资格。

本轮最新驱动状态和实测范围见`STANDARDIZATION-RESULT-20261029.md`，Image4具体阻塞见`IMAGE4-BLOCKER-20261029.md`。
