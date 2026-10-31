# 自制驱动盘点与 IOKit 标准化审计

2026-10-29，针对当前已验证的 boot0190：原生内核0187、prelink0188内容。此次仅审计、保存源码快照和更新文档；没有修改运行代码、重编译或刷机。

后续执行更新：0173/0174已实施并在0191–0193真机回归，详见[标准化执行记录](DRIVER-STANDARDIZATION-20261029.md)。下文保留0190审计时的发现；Surface匹配、显示清理和UserClient/UFS合约已有修复，HID/USB服务化/真实电源等仍待开发。

结论：**可以按 Apple IOKit 的架构与接口习惯标准化。当前已有标准父类和部分事件机制，主要缺口是设备发布、正常运行的生命周期、总线分层、HID 家族接入及私有 ABI 资格确认。** 早期启动、GIC/定时器和 panic 恢复保持可独立运行的底层路径。

## 数量口径与证据

- 按独立设备/平台功能域：**9类**，见下表。UFS控制器和IOKit块设备桥归为同一存储功能域，触摸/按键各自的协议或去抖辅助文件不重复计数。
- 另有 **1类 IOSurface 兼容实现**，以及内嵌在 PlatformExpert 中的 **PMGR开发兼容入口**。后者分配的是原版 ApplePMGRNub，不是自制的完整电源驱动。
- 以上主要具名实现共 **13个 C/C++ 文件，14,533物理行**，包含注释和空行，不含头文件、公用架构文件内的补丁及下述辅助模块；这个行数不代表原创代码量或完成度。
- C++ 源码定义 **10个 OSObject/IOService/IOUserClient 类**，它们不等于10个独立硬件驱动。
- Apple供体的 APFS、IOStorageFamily、AMFI、ApplePMGR、corecrypto、AppleFSCompressionTypeZlib 等不计入自制驱动。Apple OSS HFS+、BearSSL等密码库移植、安装器/卷写入器、AMFI/签名/共享缓存辅助和主机探针也不计入。
- GICv3、架构定时器、早期帧缓冲串口等另属于架构/启动适配，见 `pexpert/arm/pe_fiq.c`、`pe_serial.c`、`osfmk/arm64/kebab_early.c`；不按每个函数拆成一个设备驱动。这些公共文件没有纳入13文件行数。

源码取自实际构建机 `/Users/albertlee/kebab-build/dxb/xnu`，13个主要文件均与 Ubuntu `/home/albert/s4/x` SHA256一致；同时核对编译清单、当前内核符号和原生 personality 表，避免把仓库里的探针当成已装入的驱动。

机器清单：[inventory.json](/D:/LOS/HiOS/audit-drivers-20261029/inventory.json)，检查脚本：[inventory-drivers.py](/D:/LOS/HiOS/audit-drivers-20261029/inventory-drivers.py)。逐文件哈希、类继承和内核符号证据均在清单中。

- 剥CTF后的原生0187 SHA256：`ce03a20bca0acc80f578fe77f8de179012670143b80521b2698996e0232c267a`
- 原版供体 kernelcache SHA256：`2ec297bac95bcba01046fd0148e97e7e5885c05a6c18827f39f534ba375c1221`
- 编译参数确认 `KEBAB`、`ARM64_BOARD_CONFIG_KEBAB` 和 `NO_KERNEL_HID`；当前prelink闭包没有IOHID组件。

## 逐项盘点

| 功能域 | 实现与物理行 | 当前形态/已知验收 | 标准化目标 |
| --- | --- | --- | --- |
| 1 平台专家 | KebabPlatformExpert.cpp，300 | IODTPlatformExpert子类，标准personality；直接创建显示，夹带PMGR开发兼容和复位入口 | 保留父类；把设备依赖、兼容入口和复位职责明确分层，正常设备由provider/nub发布 |
| 2 CPU | KebabCPU.cpp，284 | IOCPU子类；拓扑/MPIDR检查、processor_register、SGI启动；当前只实测cpus=1 | 保持Mach/IOCPU合约；明确常驻生命周期，再逐核验证SMP，不要求CPU对象像热插拔外设卸载 |
| 3 UFS/块存储 | kebab_ufs.c + KebabBlockStorageDevice.cpp，1690 | C轮询/SCSI/DMA后端 + IOBlockStorageDevice桥；原版IOStorageFamily发布IOMedia；APFS根和两文件哈希已通过 | 保留已验证后端；明确控制器拥有权、错误恢复、请求/完成语义和释放路径，再逐步做provider资源、队列与DMA抽象 |
| 4 USB DWC3 | kebab_usb.c，3071 | 自制CDC-ACM、日志/KDP和恢复通道；内核线程轮询；panic/正常FASTBOOT均已实测 | 正常运行部分可封装IOService/事件源；早期和panic保留独立、无分配的C传输核心 |
| 5 SPMI/PMIC | kebab_spmi.c，1907 | PMIC arbiter访问、PON、GPIO状态和电源命令；PON回读/复位链已实测 | 总线provider、PMIC child/nub和窄权限接口；解析明确资源，统一总线访问序列化 |
| 6 RPMh/VRM/TCS | kebab_rpmh_vrm.c，674 | 资源白名单、编码、TCS所有权/超时/回读状态机，经SPMI模块整合；不是完整调压/时钟/休眠驱动 | 保留可测核心；电源资源服务与消费者分离，能力实测后接入IOPM，不以成功返回代替真实状态 |
| 7 触摸 | kebab_touch.c + kebab_touch_tcm.c，3467 | **Synaptics S3908/TouchComm**，GENI/I2C及诊断bitbang；devfs事件队列。0190日志只证明映射/队列检查，硬件启动仍需TOUCHINIT | 总线/协议/HID三层；接入与供体ABI相容的IOHID事件链，验证多点按下/移动/抬起、坐标、取消及错误 |
| 8 按键 | kebab_keys.c + kebab_keys_core.c，830 | PM8150电源/音量状态、去抖和/dev/kebab_keys队列；默认需kebab_keys=1，0190未启用 | 独立事件服务，映射到HID consumer/keyboard合约；保留已测试去抖，处理设备停止时的释放事件 |
| 9 显示 | KebabFramebuffer.cpp，941 | 本地IOMobileFramebuffer/AppleCLCD/UserClient兼容；复用GOP扫描缓冲，CPU拷贝、定时器模拟vsync | 明确硬件后端与目标iOS ABI层，补资源清理和参数校验；实际backboardd验证，再推进DPU/真实vsync |
| 另计：Surface兼容 | KebabSurface.cpp，1369 | 自制Surface对象、共享内存/端口及IOCoreSurfaceRoot/UserClient；**当前缺少KEBAB personality** | 先修服务发布，随后核对原版IOSurface客户端、引用/端口/映射、配额和每个selector的行为 |
| 另计：PMGR兼容入口 | 已包含于PlatformExpert行数 | 创建供体原版provider-less ApplePMGRNub；AON读0xffffffff、写无操作 | 固定供体身份、隔离ABI适配；需要真实电源功能时实现高通电源后端，不能把现状称为完整PMGR |

触摸源码已经明确本机为S3908@0x4B。厂商DT里另有互斥的S6SY761配置，不能因此把当前选中的硬件写成S6SY761。

## 核实的具体缺口

### A. IOCoreSurfaceRoot的匹配项被编译掉

[KernelConfigTables.cpp](/D:/LOS/HiOS/audit-drivers-20261029/sources/config/KernelConfigTables.cpp:51) 中，该项仍在 `ARM64_BOARD_CONFIG_NEGRONI` 条件块下。KEBAB块只有PlatformExpert和CPU。对实际0187原生内核解析 `_gIOKernelConfigTables`，得到的也只有这两项加IOPanicPlatform，**没有IOCoreSurfaceRoot**。

当前源树搜索未发现另一个显式分配/注册该root服务的路径；prelink闭包也没有替代它的Surface/HID家族。框架要匹配的服务可能因此永远不出现，即便Surface实现已经链接进内核。应优先修正板级personality并实际观察注册/客户端打开。此项属于后续图形链的确定缺口，**尚无证据证明它就是当前libignition最后无新日志的原因**。

### B. 触摸/按键未进入HID家族

当前是自制devfs记录接口，并不把事件交给backboardd的HID链；没有IOHID组件的事实与代码注释、编译选项一致。按Apple习惯包装一个C++类也不会自动补出这条链。应保留GENI/TCM、PMIC采样和去抖，增加总线provider及适配目标iOS16家族的事件服务。

Apple OSS [IOHIDEventService](https://github.com/apple-oss-distributions/IOHIDFamily/blob/main/IOHIDFamily/IOHIDEventService.cpp) 可参考事件发布、工作循环和停止资源的组织；该开源主线仅供架构参考，不能直接当作本供体的私有类大小或vtable依据。

### C. 显示启动失败的资源清理不完整

[AppleCLCD::start](/D:/LOS/HiOS/audit-drivers-20261029/sources/iokit/Kernel/arm/KebabFramebuffer.cpp:231) 分配lock、clients数组、Surface、workloop和timer；失败分支只释放部分资源，timer创建/加入失败时直接返回。该类没有对应的 `stop/free` override，调用方随后release对象不能释放其私有成员。

这是具体的部分初始化清理缺口；不应扩大成“所有没有stop的内建类都错误”。CPU/PlatformExpert等常驻对象可以保留常驻合约。图形事件源则需要取消、移除、释放的对应路径，且应结束等待者、清理客户端引用。

### D. 私有显示ABI仍有未核实的布局

[SupportedFrameInfo结构](/D:/LOS/HiOS/audit-drivers-20261029/sources/iokit/Kernel/arm/KebabFramebuffer.cpp:132) 明确记载selector74布局尚未从原版恢复，当前采用项目自定义形状。显示类名和selector号相似，不证明原版框架接受所有字段。需要按固定20H392的真实调用确认输入/输出大小、字段和完成语义；把ABI事实、推测和设备观测分别记录。

### E. UserClient还没有统一的接口检查层

两个UserClient目前采用switch和逐分支校验，有长度检查、分配上限、锁和Surface引用管理，不能说“完全没有检查”。但没有统一的IOExternalMethodDispatch合约表，打开策略、跨进程Surface/端口权限、异步通知持有与关闭策略也应明确。标准化时可以把固定计数、可变结构上限及状态校验集中，并保留合法跨进程共享。

对照同版 [XNU8796 IOUserClient::externalMethod](https://github.com/apple-oss-distributions/xnu/blob/xnu-8796.141.3/iokit/Kernel/IOUserClient.cpp#L6420) 的dispatch校验机制；这项审计没有证明某条现有路径可被利用，也没有要求为所有用户客户端统一加root权限。

### F. 底层运行合约仍偏向启动阶段

UFS依赖固件留下的时钟、PHY和可用的物理DMA映射，使用有锁的串行轮询；桥的doAsyncReadWrite在调用线程完成实际传输后回调。这在当前只读启动中已被验证，不应当作异步控制器、SMMU驱动或完整错误恢复已经实现。

正常设备可逐步按provider资源、请求队列、超时/取消和已核实的DMA映射组织。先保留16KiB连续缓冲与cache维护的已验证路径，不能为了套用IODMACommand接口就假定高通SMMU映射已完成。USB、SPMI、触摸中的固定资源也宜收敛为板级描述，但panic路径不依赖临时ADT解析或动态查找。

### G. 标准化需要把来源和板型注释整理准确

具名文件仍有来自参考板的旧注释，例如early console标题写OnePlus10Pro，GIC注释混用SM8450/kebab，而实际目标是SM8250/OnePlus8T。整理这些描述和板级资源清单有价值；不能凭注释推断实测地址本身错误。密码库与Apple OSS移植应保留原来源，不计作自制硬件驱动。

## 采用的Apple规范及适用边界

参考Apple [IOKit基类](https://developer.apple.com/library/archive/documentation/DeviceDrivers/Conceptual/IOKitFundamentals/BaseClasses/BaseClasses.html) 的provider/matching、引用计数和成对生命周期；[事件处理](https://developer.apple.com/library/archive/documentation/DeviceDrivers/Conceptual/IOKitFundamentals/HandlingEvents/HandlingEvents.html) 的工作循环/事件源组织；以及上面的同版XNU UserClient实现。

可执行的项目规范如下：

1. 硬件后端类保留Kebab/Qualcomm名称；框架依赖的兼容类名和selector按目标ABI保留。每个文件明确是原生控制器、总线/子设备、家族适配、兼容或诊断。
2. 正常驱动通过可审计的provider/nub/personality建立依赖。init保持最小状态；start获取资源并逐步完成初始化；所有中间失败都可逆序回滚。明确常驻或可停止的生命周期。
3. 同一硬件的状态和请求使用一致的序列化方式。可以先把已验证的轮询挂到timer/工作线程，不把“立刻改硬件中断”作为标准化的前置条件；真正中断接入需核对GIC provider和ADT描述。
4. I/O接口明确成功/失败、完成一次、超时、取消、部分完成和设备离线的含义。unsupported返回有定义的错误，能力不能由属性名或无操作的成功返回推导。
5. 正常电源路径在实际硬件支持后对接IOPM；先列能力矩阵，区分显示内存置黑、面板电源和整机休眠，区分RPMh投票、真实调压与PMGR兼容。
6. 公共家族API沿用标准接口；iOS私有头、类大小、vtable和符号绑定以固定供体审计。`KebabPrelinkBindings.cpp` 的当前硬编码绑定集中保留并生成ABI清单，不直接替换为最新版macOS头文件。
7. 早期打印、GIC/定时器、panic USB/FASTBOOT保留可独立执行的C路径。Apple的[ARM启动实现](https://github.com/apple-oss-distributions/xnu/blob/xnu-8796.141.3/osfmk/arm/arm_init.c)本来也使用C；使用C不是不标准的证据。
8. 内建驱动同样可以符合IOKit模型。独立kext的Info.plist、依赖与prelink打包属于后续模块化，不要求为了形式统一把现有常驻代码立即移出内核。当前也没有可直接替代早期路径的DriverKit运行环境。

IOMobileFramebuffer、IOSurface、ApplePMGR的完整目标私有实现不在这些公开范例里。参照开源代码能规范组织方式，不能替代供体ABI恢复和真机验收。

## 建议实施顺序与验收

| 顺序 | 改造包 | 验收 |
| --- | --- | --- |
| 1 | 修正KEBAB Surface personality；补显示部分初始化回滚、timer停止/释放；集中UserClient参数合约 | Surface root实际注册并可打开；错误路径无遗留timer/映射；0190根挂载、两文件哈希、串口恢复不回退 |
| 2 | 保持UFS已验证后端，梳理provider、锁、请求完成和错误/只读合约；固定IOStorage供体ABI清单 | APFS根卷与原版文件哈希一致；超时/边界/并发/只读拒绝行为明确；不同时改DMA、IRQ与SMP |
| 3 | SPMI/GENI资源与子设备分层；保留TCM/按键核心，增加匹配目标供体的HID事件服务 | 实际事件经IOHID到backboardd；验证触摸完整序列、按键去抖、坐标及错误取消 |
| 4 | 正常USB封装为独立服务；诊断、安装协议、复位策略与DWC3传输分层 | 正常断连/重连可恢复；早期和panic FASTBOOT保持无按键可用；不把IOKit锁带入panic |
| 5 | RPMh/SPMI与消费者的电源合约、时钟/调压/休眠能力逐项实现；再考虑完整kext模块化 | 每项能力都有真实硬件状态、错误和恢复证据，不能由AMFI不panic或服务名称认定完成 |

本轮未实施这些运行代码变更。手机及0190基线保留；此次新增的是盘点、二进制核验和标准化方案。
