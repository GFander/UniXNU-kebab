# A3：按 UniXNU 指南回审驱动

2026-10-29。目标仍是 SM8250 / OnePlus 8T、XNU8796.141.3、16 KiB、20H392。运行证据截至0225。本文是本轮回审，0190原审计保留为历史。

## 采用的规则及适用范围

以下10个官方页面已通过网页工具读取。脚本直接下载遭遇403，`guides/sources.json`仅保存该失败，不能当作离线文档副本。指南和Sheng的较新SoC、iOS版本、4 KiB例子用于结构参考；本机地址、页大小、固件、供体类布局和原版调用必须另行核实。

| 指南 | 本轮采用的检查 | 本机结论和下一步 |
| --- | --- | --- |
| [驱动移植步骤](https://unixnu.com/zh/porting/drivers) | 先找组件和依赖，分清固件、内置XNU和家族适配；把构建、匹配、硬件和用户态分别验收 | 新增SPMI/GENI身份均为内置XNU。保留启动核心，不在正常服务出现前调用IOKit。每批有独立补丁、native hash、打包和设备证据 |
| [驱动总览](https://unixnu.com/zh/drivers/overview) | 传输/硬件、协议/策略、上层家族分层；匹配的是实际provider | SPMI/PON/keys/RPMh已拆分。GENI与TCM已拆分；HID、真实电源家族仍缺，不能用C++类名补足 |
| [内置XNU驱动](https://unixnu.com/zh/drivers/xnu-drivers) | 编译输入、类构造、personality、注册顺序、错误和生命周期必须一致 | TU和全量构建检查并用。SPMI三个子服务、GENI/Touch均已在真机匹配及类型化调用。永久C核心和可停止正常查询服务分别记录 |
| [设备树匹配](https://unixnu.com/zh/drivers/device-tree-matching) | 资源来自实际板级描述；IOClass/IOProviderClass/IONameMatch限定范围，子设备不借用全局名字匹配 | 新ADT节点用本项目root绝对64位资源对，C++验证长度和值后才发布scoped nub。IOBSD依赖阻止早于C映射。该schema是KEBAB明确约定，不宣称通用Apple/FDT reg解析器 |
| [平台与DMA](https://unixnu.com/zh/porting/platform) | CPU/IRQ、地址空间、DMA所有权、cache维护、页粒度明确 | cpus=1和现有连续物理DMA保持。没有SMMU资格证明，也没有把IODMACommand名称当作可用IOVA。GPU/DPU前必须先做SMMU |
| [UFS](https://unixnu.com/zh/porting/ufs) | 从控制器/介质到BSD根卷逐层确认，区分后端能力和文件系统装配 | 原C后端+原版IOStorageFamily+IOMedia+APFS根、文件hash已通过。请求边界、完成一次、部分完成、离线/超时有测试；controller provider、IRQ、SMMU和Data持久写仍待开发 |
| [显示](https://unixnu.com/zh/porting/display) | framebuffer资源、缓冲生命周期、呈现/完成/通知与目标框架ABI分别验收 | 生命周期与原生factory查询已通过；selector74仍未恢复。当前CPU/GOP拷贝和timer不是DPU直扫/硬件vsync。保留它作为GPU验证出口 |
| [输入](https://unixnu.com/zh/porting/input) | 总线、协议/坐标、事件家族分离；验证完整接触序列和释放/取消 | S3908@4b和PMIC原始按键已确认。GENI时钟/SE/恢复已测，但未加电ping仅证明失败恢复。没有实际触摸报告或backboardd接收证据；HID供体ABI与整链是明确缺口 |
| [GPU](https://unixnu.com/zh/porting/gpu) | 内存映射、固件/GMU、提交/完成和前端各有硬件证据 | AdrenoMetal/A740结构不能替代A650。先落实a6xx寄存器/固件/GMU及GPU和DPU的SMMU stream/context，验证BO映射、读回，再接Metal/CA |
| [电源/DSP](https://unixnu.com/zh/porting/power) | 电源、时钟、调压、音频/DSP与系统PM分开，消费者依赖真实后端 | RPMh命令白名单与状态机保留；PON、PMIC GPIO和RSC各有界面。当前KebabVrm是策略/回读身份，不代表IOPM。provider-less ApplePMGRNub与0xffffffff AON也不能当完整PMGR |

## 9个功能域逐项结论

文件拆分和新增子服务没有增加硬件功能域计数。仍为9个硬件/平台域，另计Surface兼容层及PMGR开发入口。类数、实现文件数、代码行数另行由manifest列出。

| 域 | 当前证据 | 尚缺的标准化/功能合约 | 下一次验收 |
| --- | --- | --- | --- |
| PlatformExpert | 标准平台匹配，实际provider可由DRV?读取；SPMI/GENI板级资源有显式契约 | 显示仍由平台专家直接创建；PMGR开发入口夹在平台服务中 | 把资源来源和开发兼容能力记录清楚；不为形式热卸载常驻平台对象 |
| CPU | IOCPU对象、拓扑/SGI检查、单核启动和DRV? | 多核启动、核间TLB/缓存及中断路由没有本轮证据 | B阶段稳定后逐核扩展，避免与APFS写入混测 |
| UFS/块存储 | descriptor/完成/RO自测；每版两文件SHA和APFS根一致；10绑定/22槽；0216 Preboot实际写/同步，0217–0218重启全SHA一致 | C启动核心持有控制器，桥仍挂平台root；轮询而非异步控制器，DMA依赖固件；Data内容保护未实现 | 保持已验证的Preboot持久化基线；Data先落实加密/内容保护再验收写入 |
| USB | CDC可重开，普通诊断恢复；正常及panic FASTBOOT；panic中DRV?明确拒绝 | DWC3、命令分派和安装仍同文件，复位策略已独立；正常线程和断连状态缺独立服务生命周期 | 再做正常传输/策略分离。早期、KDP、panic继续使用预映射C核心，不挂IOKit锁 |
| SPMI/PMIC | S0修复原有observer并发；24次PON回读；A1a/b零语义抽取；A1c服务实际匹配 | 常驻C映射不随服务stop卸载；尚无通用外设动态枚举/热插拔承诺 | 保持窄接口；资源冲突和不支持事务明确失败；不扩大PON写权限 |
| RPMh/VRM | C编码/白名单/TCS核心保留，RSC与SPMI readback明确分离；typed RSC状态通过 | 真正消费者投票聚合、调压、时钟、睡眠/唤醒及IOPM未完成 | 每路rail使能/回读/失败恢复分别记录，不能由AMFI通过推断电源完整 |
| 触摸 | GENI/I2C/bitbang抽出；TCM/坐标/队列体保持；0201/0202硬件阶段日志与0200一致；0202真实类型化查询PASS | C轮询仍是启动常驻。供体HID家族未接入，实际采样尚未完成 | 通电后identify/app info/报告程序/多点完整序列，然后目标HID/backboardd |
| 按键 | PMIC原始04/01/01/10、去抖核心、队列自测；普通事务序列化；实际KebabPmicKeys；0203延后发布devfs且VFS字符设备资格检查PASS | 没有HID事件/停止时释放事件；真实按键事件用户进程读取仍待验收 | 接供体HID消费者/键盘约定，验证完整按下/释放与错误处理 |
| 显示 | 清理/等待/引用合约和原生factory验证通过；DRV?实际AppleCLCD | 真实用户IOServiceOpen、原版私有ABI、DPU直扫、真实vsync/120Hz未过 | 先补私有ABI证据；B8的SMMU/GPU完成后再推进DPU缓冲交换 |
| Surface另计 | 已有真实root匹配、原生连接/参数/关闭测试、共享对象引用清理 | 用户跨进程端口和映射、原版IOSurface.framework及backboardd完整调用资格不足 | 记录每个selector大小/结构来源，未知布局不伪造为已验证 |

## A4新增驱动的必留记录

每个新组件在补丁说明中记录：目标硬件/组件来源、所在层、provider和资源schema、实例/映射/线程/锁的拥有者、失败与stop边界、普通/早期/panic调用资格、私有ABI事实、构建输入与实际设备验收。

SPMI：内置XNU；IOPlatformDevice→KebabSPMIBus→scoped KebabSPMIDevice→三个客户端。父服务核验四组资源、EE和子设备specifier；正常查询使用C总线mutex，panic只走原有无锁PON core。服务stop只排空查询和清理nubs，持久映射归启动核心。

GENI：内置XNU；IOPlatformDevice→KebabGeniBus→KebabI2CDevice@4b→KebabTouch。C核心拥有SE13/wrapper/TLMM和bitbang，TCM保留协议。完整命令/完整帧和诊断使用共同正常事务锁。新服务只提供有序状态查询，不开放任意MMIO或用户ABI。stop排空查询，C轮询/映射仍按内核启动期常驻约定保留。0202已验收；proto3/mapped1/bb0/identified0/decode0/node0明确记录了功能边界。

`manifest-0217.json`在24个主要实现文件中核对19个类，保持9个功能域计数；11个registry角色实际匹配。清单列出的源码在Ubuntu实际树、Mac实际树与独立补丁重放树SHA一致，native符号和personality同时核对。`abi/`已保存7份逐类记录，图形私有布局/用户ABI未通过的部分明确保留；原版PMGR指令/类大小、IOAES的181槽/162继承及存储10绑定/22槽在当前prelink重新检查。AES单列辅助组件，不增加已完成硬件域数。

完整重放另外发现并修复0167/0170的旧上下文，补存0173-z存储编译输入和0193已运行的RAM日志/介质发布/prelink生命周期胶合。172个共同补丁严格重放成功；verify-all为8 PASS、0 FAIL、4 SKIP（未指定IPSW）。10个Mac实际准备差异单独存为`patches/hosts/macos-dxb-13.5`，不混入通用BSP。共同补丁加主机补丁与Mac实际4205个列明源码/头文件/配置输入匹配，另有两个不参与编译的空Data旧文件。0218还给四个带供体固定地址的实现加了KEBAB编译保护，板内正文逐字节不变，其他板预处理无代码/绑定，真机全回归通过。

## 对真正启动iOS的影响

本轮A阶段减少了总线争用、服务匹配和生命周期的不确定性，但没有跨越用户态启动关卡。最后已证实的用户态仍是APFS System上的真实PID1进入libignition。Data仍空且未配对；Preboot已挂载并保存一个受控测试文件，真实引导内容仍未装配。SystemOS/AppOS/Cryptex、共享缓存和完整launchd任务没有通过，也没有Metal、backboardd、SpringBoard桌面的证据。

0204进程快照确认仅有PID0和PID1；launchd只有1个等待线程，两次CPU时间与wait地址不变。0205无暂停阻塞栈两次一致，已定位到`sysctl → AppleImage4 → lck_mtx_gate_wait`。0206 PID1专用sysctl追踪确认`security.mac.img4.ignition_blob`，oldlen132，原版handler=0xfffffe000891c330，没有leave。0207恢复defaults平台nub的匹配资格后仍等待。原版AppleImage4::start会等待IOAESAccelerator，再finalize nonce并打开gate；缺失共享缓存不是该等待的直接证据。

按A4查找并固定官方SoftwareAES 4a45326da5c2cd6fe6813d79ee43db20805628af。其catalog OS ABI仅声明21H461；本机重新核对20H392的181槽/162继承地址、构造/析构、原版代码hash，0208还实测了288/136/136的AES/crypto/IOService类大小。0209定位raw输入被读为零；0210修正KEBAB静态UPL范围后，真实CBC128/192/256、descriptor及raw接口KAT均通过。0211错误预期答案测试拒绝注册；AES现必须显式boot-arg启用，默认关闭。0213捕获原版Image4请求selector2212/bits128/op0/bytes16，返回unsupported并panic，串口FASTBOOT恢复通过。通用软件CBC通过，不代表苹果UID/GID/派生key已实现。

0214准确选择Data dev1/3并核对同backing后，被原版APFS以“unencrypted data volume is not allowed”拒绝，无Data文件写入。没有伪装FastSim或修改原版返回值。0215改验允许未加密的Preboot dev1/4；0216以ufsrw1/rootrw0独占创建65559字节文件，完整写读SHA、VFS_SYNC和UFS cache flush通过。0217–0218在ufsrw0/rootrw0冷启动后重读同SHA，checkpoint实际261。文件路径固定，O_EXCL拒绝重复覆盖，安装器原白名单保持。

按用户手册推进：B2落实真实密钥/nonce后端；B1保留Preboot通用持久化证据，Data另过内容保护资格；B3装配卷/路径/身份；B4/B5补真实信任与Cryptex；B6验证缓存布局/映射；B7跑完任务。B8先SMMU再A650；保留CPU framebuffer作读回呈现。B9再验DPU直扫、扫描中缓冲所有权、真实vsync/120Hz及HID输入，B10才是锁屏/桌面。

不能按文件数、C++类数或指南阶段数给出桌面完成百分比。下一步的可衡量成果应是具体的进程等待、卷读写、任务与帧/事件证据。

## 最新核对（自动更新）

已验证0225，9硬件域、25主要实现文件、19类、11实际registry角色。此前172补丁/4205扩展名清单属于历史阶段；最新为176共同补丁+4 host补丁、5192源目录文件完整重放和clean构建。统一verify-all现17 PASS/0 FAIL/4可选IPSW SKIP。

USB复位策略/私有状态已分离。FWLOG满环自覆盖及普通TX环溢出已由0197修复，24+2+1主机案例、真机完整65536字节、后续PON、持普通SPMI锁的panic FWLOG/FASTBOOT通过。普通线程/协议/断连生命周期仍待服务化，不能只凭这些改动声称USB标准化全部完成。

Preboot持久化、专用nonce选择器2212、Data内容保护和后续UI的真实边界以`../STANDARDIZATION-RESULT-20261029.md`和`../IMAGE4-BLOCKER-20261029.md`为准。当前没有完整launchd、Data配对、Cryptex/运行时DSC、GPU或桌面验收。
