# 最新：0583有限GMU固件、VERSION队列DMA与SN11观察通过

截至2026-11-03：实机0583限定资格accepted=true；原SCM/Root/nonce、既有bank/systembag、加密Data、安全与watchdog304.415秒及全部原后续回归通过。43页/3个WIMG6页、首次NS、完整32KiB TCM、IRQ初始化/arm/readback后，RSC睡眠交接、真实GMU固件释放/FW_INIT100/HFI_CTRL ACK与一次VERSION20010008应答均成功，观察到有限GMU队列DMA；SN11三次独立只读观察成功。完整HFI初始化、带宽投票、GX/CP/GPU绘制/Metal仍未资格，人工GUI仍NOT_RUN_OWNER_AWAY。

0583为437共同+4Mac主机补丁/5417源，source/runtime parent0582仅接受原基线、backing、FirstNS、Cold/IRQ与suffix，不继承0582失败的RSC/FW成功。正式0458共5路径（3修改/2新增）：sleep末尾BUSY0 poll改为单次raw诊断；preprogram BUSY0及真正wake REQ2→fresh ACKbit1→BUSY0保持严格。另加CAP31固定96B SN11只读CommandDB观察，无新USB控制命令或带宽vote。实际完整native SHA256 `4a27045936807e2477af28e12f691628a6979d3e0d8ed8dcd1dc0e5ae9aab0a0`，19,424,008字节；即时source/HID配对、4TU overlay/applied、37实际exports、42原符号+15FW/HFI+3SN11符号、19原调用/156B policy及437+4/5417公开逐文件回放通过。源码/软件模型、native证据与本次实机结果分别记录。

新boot先通过原304.415秒回归及43页/3个WC页、首次NS306读/20写/TLB1。GMUCOLD一次加载并核对完整32KiB TCM：8192word写/8192word读，CRC f5508f4a；SYSRESET写1/回读1属于释放前Cold记录，不是后来SYSRESET0后的fresh读，也不是独立物理reset ACK。GMUIRQ一次完成Core47读/9写、实际483读/9写、valid11与初始MASK/readback。CAP27是FW启动前的immutable arm快照；其中dispatch0/handler_observed0不表示固件启动后没有IRQ，也不证明已观察物理中断触发。

GMUFW一次调用内的资源准备成功：CAP28 result0/state3、273读/21写/21屏障、poll2/delay0、elapsed734us，STATUS bit16/REQ0/ACKraw1（bit1clear）完成睡眠交接，BUSYraw1只作诊断。真正FW唤醒随后观察ACKraw3/BUSY0；睡眠BUSY1没有冒充wake成功。六hidden raw仍仅为diagnostic-notACK，未猜测其未知字段或RW/RO属性。

CAP29固件本体真实phase5/result0，5077读/20写/30屏障、poll9/500us delay、elapsed3347us，FW_INIT result0x100与HFI_CTRL ACK1均成功。记录中的missing_owners=2/retained_bandwidth1表示受控保留原带宽而无新vote；bandwidth_qualified0/bw_commands0，不能把配置中的目标数字当成已授带宽或现场继承floor。所有真实私有Root/Cold/namespace/IRQ pointer与fresh gate保持；没有借CAP缓存状态取得硬件权限。

随后GMUHFI一次真实VERSION请求得到匹配ACK：returned_version0x20010008，39次共享读/6次共享写、4次寄存器读/2次寄存器写、poll1、elapsed570us，packet/doorbell/ACK消费与version_verified均为1，dma_observed1。该证据证明有限GMU固件和CPU共享队列通信/DMA已发生；hfi_initialized0，尚未发送完整PERF/BW/CORE_FW_START等初始化，也未取得GX/CP、GPU绘制、GPU工作负载DMA或Metal资格。旧aggregate字段GMU=false和physical_DMA=false保留其完整GPU里程碑含义；新增GMU_firmware_running、GMU_shared_queue_DMA_observed/limited_shared_queue_DMA_qualified为true，不能用旧bool否认本次GMU成功，也不能用新bool泛化为完整GPU。

CAP28/29/30各三份相同hash只是各自一次publication的私有缓存复制，不是三次live写入或三次续租。原安全suffix完成后，CAP31是三次独立OPEN（session61/62/63），每次双遍volatile RAM派生记录并核对同session重复GET；三份96B一致。实际SN11 address0x500a4、slave5、resource version0x1000、aux8B，raw aux0=530000/aux1=196624、两priority均0。该address是RPMh command地址，不是CPU MMIO指针；未解读aux单位/width/VCD、未推断当前floor、未发command或授予BW所有权。

原CAP20 expectedABSENT、PAS INFO/RSC只读查询、IOVA/pool/显示IRQ/VSYNC/温度等suffix全部通过，post92313终止0，capture81908/collector81416/finalizer81764真实结束、串口释放。41548B固件原属性仍纳入完整owner ADT测量，无新exclude；0487 prefix、原组件/20H392 ABI/HID219/cache、Root/bank/加密Data/MACF/ACL/Sandbox/watchdog保持。生成器、注册、打包与本次flash各一次，无writer retry；sealed运行工具和历史receipt不改。

0577实际编译失败、0578 Cold75读0写拒绝、0579 IRQ初始MASK0拒绝、0581 hidden echo4写后隔离与0582睡眠BUSY超时全部保留。0583此次资格只覆盖上述有限实际GMU流程；下一阶段仍需真实完整HFI/GX、CP/ring/fence与正确像素，之后才谈Metal入口。人工GUI owner-away未运行，不反复请求；亮度滑块和新增安装仍暂缓，无新Git commit/push。

见[0583限定资格](qualification/0583/limited-gmu-fw-version-sn11-qualified.json)、[437+4/5417源码回放](qualification/0583/public-all-source-replay-0583.json)及[当前状态](STATUS.md)。

# 历史0582：原基线、TCM与IRQ通过，RSC睡眠末尾BUSY超时隔离（原文保留）

截至2026-11-03：实机0582原SCM/Root/nonce、既有bank/systembag、加密Data、安全与watchdog304.455秒及全部原后续回归通过；43页/3个WIMG6页、首次NS、SYSRESET1下完整32KiB TCM及IRQ初始化/arm/readback通过。RSC完成全部21次写入并观察到睡眠STATUS bit16、REQ0及ACK bit1清除，随后因BUSY raw1在睡眠末尾等待BUSY0超时隔离；FW本体0读0写、HFI跳过。整轮accepted=false，无FW/HFI/设备DMA/GX/GPU/Metal资格，人工GUI仍NOT_RUN_OWNER_AWAY。

0582为436共同+4Mac主机补丁/5415源，source/runtime parent0581部分资格；正式0457只修改资源Core/Native两文件、无新增生产文件，六hidden TCS写后保留barrier和一次raw诊断，不再把未知echo当ACK。实际完整native SHA256 `17f7328036a4361b0b505ffa5335a83d733a4ecf9616785caa64dae8865f846a`，19,407,432字节；四TU overlay/applied、176个显式未变clone、37exports、57符号/19原调用/156B policy及新hidden diagnostic通过。新资源链Core315、Native5场景及另两场景FW/HFI组合均为软件模型，旧全套未重跑；source30 body/transport历史与资源依赖替换分开，不把主机结果称为实机FW成功。

同次GMUCOLD与GMUIRQ各一次成功：Cold11688读/8196写、完整8192word写/8192word读、CRC f5508f4a，SYSRESET写1/回读1，elapsed21.254ms。IRQ Core47读/9写、实际MMIO483读/9写、valid11，Arm阶段真实HFI/AO STATUS0，MASKffff0007/AO12、enable30000、priority8080a0a0、pending/active0、epoch1/gen1。dispatch0/handler_observed0表示未观察物理触发；不把arm/readback称为实际触发，也不把reset请求回读称为独立物理reset ACK。

GMUFW只尝试一次。CAP28三份168B缓存一致：result110/state4/quarantined1，705读/21写/21屏障、75poll/72delay，elapsed10.807ms；STATUS0 raw01010006（bit16已观察）、REQ0、ACK raw1（bit1已清除）、BUSY raw1。最终failed_region0/offset0x404/operation6，是睡眠末尾BUSY0 poll超时，不是581的第4写echo超时。21写/seqRAM/PDC及睡眠STATUS/REQ/ACK证据保留，但资源READY与FW资格不因此成立；失败未重试、回滚或释放pins。

六个hidden诊断raw依次为offset38=0、34=82204514、40=0、3c=82204510、48=80000000、44=82204520；只读一次后在canonical返回锁外打印，明确not-ACK，不推断未知位或RW/RO类型。独立primary源审确认vendor/mainline的stop路径以STATUS bit16/REQ0为睡眠交接，未要求该处BUSY0；真正wake路径REQ2→ACK2之后仍要求BUSY0。下一最小候选只将睡眠末尾BUSY读取保留为诊断，preprogram BUSY0、wake BUSY0及ACK bit1清除门不放宽，仍须独立新源码/native/实机验收。

CAP29三份248B失败缓存仍为result110/phase0/action2、FW本体0读/0写，raw与581相同不代表复用了旧执行；未运行记录中的默认0不是live寄存器观测。SYSRESET0、FW_INIT、HFI_CTRL均未尝试，依赖GMUHFI/CAP30未运行，没有VERSION ACK或HFI资格。每CAP三份相同CRC都是同一immutable publication的复制，不是三次live写入或成功。

原CAP20 expectedABSENT、PAS INFO/RSC只读查询、IOVA/pool/显示IRQ/VSYNC/温度等suffix全部完成；post80862以exit1重抛资源失败，capture79824/collector80860/finalizer75500均结束、串口释放。41548B属性仍纳入完整owner ADT测量，无新exclude，0487 prefix及原组件/20H392 ABI/HID219/cache、Root/bank/加密Data/MACF/ACL/Sandbox/watchdog保持；216输出生成器、注册和打包各一次，未重跑或UTC retime。

当前完整限定基线仍为0580（reset-held TCM与IRQ arm/readback，不含FW）。0577编译失败、0578 Cold75读0写、0579 IRQ MASK0零写拒绝、0581 hidden echo4写后隔离与本次睡眠BUSY失败均保留。后续CP/ring/fence/正确像素和Metal未资格；亮度滑块与新增安装继续暂缓，Git尚未新commit/push。

见[0582部分资格](qualification/0582/substrate-cold-irq-qualified-RSC-sleep-BUSY-failed.json)、[436+4/5415源码回放](qualification/0582/public-all-source-replay-0582.json)及[当前状态](STATUS.md)。

# 历史0581：原基线、TCM与IRQ通过，新增RSC回读超时隔离（原文保留）

截至2026-11-03：实机0581原SCM/Root/nonce、既有bank/systembag、加密Data、安全与watchdog304.453秒及全部原后续回归通过；43页/3个WIMG6页、首次NS、SYSRESET1下完整32KiB TCM及IRQ初始化/arm/readback通过。新增RSC在第4次写后的offset0x34全字回读等待超时，result110并隔离；FW本体0读0写、未解除reset，HFI跳过。整轮accepted=false，无FW/HFI/设备DMA/GX/GPU/Metal资格；人工GUI仍NOT_RUN_OWNER_AWAY，亮度滑块和新增安装继续暂缓。

0581为435共同+4Mac主机补丁/5415源，source/runtime parent0580；正式0456共25处变化（7个现有文件、18个新增文件），仅授予新boot完整私有生产链之后的有限FW_INIT/HFI_CTRL与单次FW_VERSION候选。实际完整native SHA256 `6b5eafbcd33496f36c270f2690559089d9f6098ba742644bf829a4cf622269fc`，19,407,280字节；四TU bound/applied、最终closed ledger、37真实exports、原42加15个新符号/私有调用图与CAP无直接FW/HFI操作通过。新source30软件模型与原580协议/body继承分开，未把旧27整依赖pins重标为新执行；这些源码/host/native证据不证明硬件启动成功。

同次GMUCOLD与GMUIRQ各一次成功：完整TCM8192word写/8192word读、CRC f5508f4a、SYSRESET写1/回读1；Cold11688读/8196写，elapsed24.859ms。IRQ Core47读/9写、实际MMIO483读/9写、valid11，真实Arm HFI/AO STATUS0，MASKffff0007/AO12、GIC enable30000、priority8080a0a0、epoch1/gen1、pending/active0。dispatch0/handler_observed0，未观察物理触发；不把arm/readback称为实际触发，也不把reset请求回读称为独立物理reset ACK。

GMUFW只尝试一次，其资源阶段CAP28三份168B缓存一致：result110/state4/quarantined1、4106读/4写/4屏障/polls0，failed_region0/offset0x34/operation4，last_read0x82204514、last_write0，reset_readback1，elapsed10.175ms。该失败发生在named-RSC的hidden CMD0_ADDR全字echo等待；当前实际证据不支持把它判为只读寄存器、映射基址错误或解读8220字段。固定vendor路径对该hidden寄存器采用raw write+barrier，未建立wholeword echo合同；下一最小修正仍须独立源码、native和实机验收。

CAP29三份248B失败缓存一致：result110/phase0/action2、primary_error110、FW本体0读/0写。资源失败在真正FW入口之前返回，没有SYSRESET0、FW_INIT或HFI_CTRL操作；该未运行record里的默认0不构成live寄存器观测。依赖GMUHFI/CAP30跳过，没有VERSION ACK或HFI消息资格。CAP三份CRC相同是同一次immutable publication的复制，不是三次live成功。原始两份raw哈希保留在脱敏收据，失败未重试、回滚、复位或解除pins。

原CAP20 expectedABSENT、PAS INFO/RSC只读查询、IOVA/pool/显示IRQ/VSYNC/温度等后续回归仍全部完成；post51338以exit1重抛新增阶段失败，未生成全链成功标记。所有本轮捕获/观察者结束、串口释放。原41548B固件属性继续纳入fullADT owner测量，无新exclude，0487 prefix及原组件/20H392 ABI/HID219/cache、Root/bank/加密Data/MACF/ACL/Sandbox/watchdog保持；232输出生成器、注册/打包各一次，无UTC retime。

当前完整限定基线保留0580（reset-held TCM和IRQ arm/readback，不含FW）；0576限定通过、0577完整构建失败未刷、0578 Cold75读0写、0579 IRQ MASK0零写拒绝与本次RSC4写后隔离均保持。未完成FW/HFI、CP/ring/fence/正确离屏像素及Metal；尚未新commit/push。

见[0581部分资格](qualification/0581/substrate-cold-irq-qualified-RSC-readback-failed.json)、[435+4/5415源码回放](qualification/0581/public-all-source-replay-0581.json)及[当前状态](STATUS.md)。

# 历史0580：reset-held TCM与IRQ初始化、arm/readback限定通过（原文保留）

截至2026-11-03：实机0580原SCM/Root/nonce、既有bank/systembag、加密Data、安全与watchdog304.399秒及全部原后续回归通过；43页/3个WIMG6页、首次NS映射、SYSRESET1下完整32KiB TCM写读，以及真实IRQ已知MASK初始化、arm/readback均通过。IRQ dispatch0，未观察物理触发；FW仍reset-held，没有FW/HFI消息、设备DMA/GX/GPU/Metal资格。整轮限定范围accepted=true，人工GUI仍NOT_RUN_OWNER_AWAY，亮度滑块和新增安装继续暂缓。

0580为434共同+4Mac主机补丁/5397源，source/runtime parent0579部分资格；正式0455仅改IRQ Core/Native两文件，无新增生产文件。只继承579原基线、首次NS和TCM成功，不继承其IRQ失败。实际完整native SHA256 `41383aeee41da9abe5864a77bdafe50d8378e5b669b03327695e7e6319e78027`，19,384,928字节；两TU actual overlay/applied、5397postbuild、29真实exports、42linked符号/19组BL，以及实际initial-mask ORR与Arm分支通过。新reviewed1协议normal/ASan各6106检查、10个编译后运行失败变异，真实执行新Core47读/9写；producer和接收器独立通过，不以host测试替代实机。

实机GMUCOLD一次成功：result0/phase3(RESET_HELD_TCM)，11688读/8196写、addedMask5（AXI和GMU新增请求，CXO原请求1继承），8192个32位word写入及8192个word读回，覆盖完整32768B含holes；TCM CRC/readCRC均f5508f4a，elapsed21.189ms。SYSRESET写1/回读1、root/source资格与child pin保持，不额外宣称独立物理reset ACK。

GMUIRQ一次成功：result0/phase2、installed1/armed1/touched1，Core47读/9写、实际MMIO483读/9写、valid_mask11。已知初始MASK有限set/readback后，HFI MASK=ffff0007、AO MASK=12；Arm阶段真实读到HFI/AO STATUS均0。GIC enable=30000、priority=8080a0a0、两route=200、pending/active0，epoch1/gen1。dispatches0/handler_observed0仅表示未观察物理中断触发，不能把安装、arm或readback称为实际触发；没有强制setpending。原579 MASK0与未导出初始STATUS的失败记录保持。

同次启动root200配置/总线、43页/8pools360448B/3个CPU shared页WIMG6、首次NS306读20写/TLB1及原CAP20 expectedABSENT、PAS INFO/RSC/IOVA/pool/显示IRQ/VSYNC/温度等全部通过，post真实exit0。所有捕获/观察者已结束、串口释放。当前41548B属性仍纳入完整owner ADT测量，无新exclude，原0487 prefix、硬件Root/原bank/加密Data、MACF/ACL/Sandbox与watchdog保持。生成器197输出、注册和打包各一次，未重跑或UTC retime。

下一阶段仍需分别验证GPU-local RSC/PDC、保留BW边界、真正FW release/FW_INIT/HFI_CTRL与VERSION消息；这些不等于完整HFI、GX、设备DMA、GPU或Metal。0576限定通过、0577完整构建失败、0578 Cold75读0写拒绝、0579 IRQ15读0写拒绝均保留。尚未新commit/push。

见[0580限定资格](qualification/0580/reset-held-tcm-irq-qualified.json)、[434+4/5397源码回放](qualification/0580/public-all-source-replay-0580.json)及[当前状态](STATUS.md)。

# 历史0579：真实reset-held TCM写读通过，IRQ初始MASK零写拒绝（原文保留）

截至2026-11-03：实机0579原SCM/Root/nonce、既有bank/systembag、加密Data、安全与watchdog304.39秒及全部原后续回归通过；43页/3个WIMG6页、首次NS映射和SYSRESET1下完整32KiB TCM写读通过。IRQ在首次硬件写入前因初始HFI/AO MASK均0而返回result13，handler已安装但未armed、未观察实际触发；初始HFI/AO STATUS未导出，不能据默认0推断实读0。整轮accepted=false，FW仍未释放，无HFI消息、设备DMA/GPU/Metal资格；亮度滑块和新增安装继续暂缓。

0579为433共同+4Mac主机补丁/5397源，source/runtime parent578；正式0454只改BootOwner一处held CMD比较，使用原canonical invariant mask，fresh ROOT_ON/DIRTY/UPDATE严格门和所有其他生产文件保持。578原Cold75读/0写失败保留；host真实生产RootHold模型产生cached CMD0x12而fresh CMD2并复现旧失败，是源码/模型推断，不是旧设备private ledger导出。新host normal/ASan各34场景2079071检查及4个已编译运行失败变异通过。实际完整native SHA256 `13b1941441dc87746cc4544a95e233a4114490ee369aa774eb4f9cadb34deff0`，19,384,768字节；1TU actual overlay/applied、5397postbuild、29exports、42symbols/19组BL与原ABI/cache/touch219/fullowner保护通过。协议CAP1..27、13个生产pins和原248/168B ABI不变，578真27测试明确继承，不称重跑。

实机GMUCOLD一次成功：CAP26三份248B缓存一致，result0/phase3(RESET_HELD_TCM)，11688读/8196写、addedMask5（新增AXI与GMU请求，CXO原请求1继承）；SYSRESET写1并回读1，8192个32位word写入及8192个word读回，覆盖完整32768B含holes，CRC均f5508f4a，载入payload18404B，elapsed15.178ms。Root/source资格1、child pinned/root_children1、owner_dead0/quarantine0。这里证明reset寄存器请求/回读与TCM内容一致，不额外宣称独立物理reset ACK；FW保持SYSRESET1，未deassert、未FW_INIT或运行HFI。

GMUIRQ随后只尝试一次：result13/phase0，Core15读/0写（Native MMIO累计108读/0写），installed1/armed0/touched0、dispatch/handler_observed0。GICCTL0x12、原priority word0x80808080、目标source disabled/no pending/no active/level等初始正面检查通过；实际HFI MASK=0、AO MASK=0，与旧“起始全masked”前置假设不符，所以在任一硬件写入前拒绝。**handler安装不等于armed，也不等于物理IRQ触发；579未导出初始HFI/AO STATUS，不能把收据中的默认status0称为实际硬件status0。** 没有强制setpending、清错冒充完成、重试或解除reset。

同次启动原Root/Data/watchdog304.39秒、root200配置/总线、43页/3WC、首次NS306读20写/TLB1、CAP20 expectedABSENT、PAS INFO/RSC/IOVA/pool/显示IRQ/VSYNC/温度等原后续回归通过；这些分项通过保留，整轮因IRQ失败accepted=false，post以exit1结束。所有捕获/观察者已结束，串口释放，人工GUI仍NOT_RUN_OWNER_AWAY。原41548B测量属性/fullADT owner/0487 prefix、旧密钥/bank/加密Data和原MACF/ACL/Sandbox/watchdog保持，未重复579生成器/注册/打包。

下一步是独立0580有限known-mask初始化/readback、fresh ResetHeld门和原始状态缓存候选；最终source manifest尚待Root固定，不能预记成功。真实IRQ arm、实际trigger、FW_INIT/HFI消息和DMA/GPU/Metal仍分别需要实际资格。576完整限定通过、577fullmake失败未刷、578零写Cold失败与本次IRQ拒绝均保留。

见[0579部分资格](qualification/0579/substrate-cold-qualified-IRQ-initial-mask-failed.json)、[433+4/5397源码回放](qualification/0579/public-all-source-replay-0579.json)及[当前状态](STATUS.md)。只更新脱敏元数据与文档，尚未新commit/push。

# 历史0578：原基线与首次NS通过，Cold75读0写拒绝（原文保留）

截至2026-11-03：实机0578原SCM/Root/nonce、既有bank/systembag、加密Data、安全与watchdog304.373秒通过，43页固件内存、3个HFI/log页WIMG6及首次普通NS映射和原后续回归通过。GMUCOLD在RootPrepare预检75读/0写返回result11/phase0；TCM未装载，依赖IRQ未运行，整轮accepted=false。最近完整限定通过的0576与0577实际构建失败历史保留，尚无固件执行、HFI消息、设备DMA/GPU/Metal资格；亮度滑块和新增安装继续暂缓。

0578为432共同+4Mac主机补丁/5397源，正式0453仅修NativeBoot的四个KERNEL kern头路径；source parent为实际fullmake失败且未刷的0577，runtime/package parent仍为完整限定通过的0576。576→578的运行源码合并为26处/17新文件，不能把它当作正式0453的一文件变化。实际完整native SHA256 `c7fd94e0b3bf4a9c74b47bcffb4643383873a020866bbaf2b38417a14305186d`，19,384,768字节；六TU实际overlay/applied、立即native/HID/5397postbuild配对、29实际exports、42linked符号及原156B first policy通过。原compiler/MACF/ACL/ClassB/20H392 ABI、触屏219槽、cache、原组件与fullowner保持。

同次启动GMUSTAGE真实43页/8pools/360448B持有，currentSecureDT唯一41548B固件属性精确校验，3个CPU shared页pmap WIMG6；属性继续纳入完整owner ADT测量，无新exclude，0487 prefix保留。GMUMAP真实306读/20写/TLB1，CB0/ASID1/VMID0、SID5 translation和SID4FAULT/no RAM、私有epoch1/gen1保持；FSR raw400是Format2、definedFault0，ASIDPNE写0读0。启用普通NS映射不等于设备DMA已执行。

GMUCOLD只尝试一次，source_qualified1，但RootPrepare预检返回result11/phase0、75读/0写、root_qualified0/root_children0/owner_dead0/quarantine0；没有新增clock请求、SYSRESET写入、TCM写读或FW启动。三份248B缓存收据一致，未把拒绝当成功、未重试/恢复/复位；依赖GMUIRQ command/CAP27均跳过，不能宣称IRQ armed或实际IRQ触发。75读路径与RootPrepare快照/fresh gates仍在源码诊断，拒绝根因尚未确定，579尚未固定。

失败后原CAP20 expectedABSENT、PAS INFO/RSC/IOVA/pool/显示IRQ/VSYNC/温度后续回归仍完成，真实VSYNC软件0、温度16+9有效；post以exit1结束，没有写完整post成功标记。所有本次捕获与观察者结束，串口释放。原硬件Root/既有bank/加密Data/安全/watchdog保持；本次没有TCM、FW_INIT/HFI、设备DMA、GPU或Metal资格，人工GUI未复验。下一步先定位零写预检拒绝，再对新候选独立native和实机验收；不会按host模型结果开放执行。

见[0578部分资格](qualification/0578/substrate-first-qualified-cold-preflight-failed.json)、[432+4/5397源码回放](qualification/0578/public-all-source-replay-0578.json)及[当前状态](STATUS.md)。源码/host、完整native和实机结果分开记录，尚未新commit/push。

## 历史0577：实际fullmake失败，未形成合格native或刷机

0577源为431共同+4主机补丁/5397项，26处/17新文件。overlay预检的额外kern引用路径掩盖了NativeBoot四个裸quoted kern头的缺失，实际完整构建失败；无合格native、未打包或刷机。0578仅按KERNEL使用显式kern路径修正并重新完整构建，不将577失败改成成功。原577失败与576完整限定通过记录保留。

# 历史0576：首次NS映射与固件WC内存实机完整限定通过

截至2026-11-03：0576原SCM/Root/nonce、既有bank/systembag、加密Data、安全与watchdog304.368秒通过；真实43页固件内存、3个HFI/log页NormalNC属性和首次普通NS SMMU映射全部通过。GMU仍停在producer quiet，未装载TCM或运行固件，尚无设备DMA/GPU/Metal资格；亮度滑块和新增安装继续暂缓。

0576为430共同+4Mac主机补丁/5380源，0451仅修改Core和重新审查的Binding，无新生产文件。完整native SHA256 `729a72afe948432892e6d46913ba934e859189f04441782634bbc7253dc2607b`。真实两TU、立即native/HID/postbuild配对、20实际exports、27linked与156B新policy、原compiler保护/ClassB/20H392基类ABI/触屏219槽/cache指令/原组件/fullowner全部通过。

本次currentSecureDT中41548B固件精确SHA经私有snapshot验证；43个4K页预备、8pools/360448B持有、RAM载入22888B、HFI CPU结构完成，3个CPU16K shared页真实pmap WIMG6。属性由575包保留并readonly校验，仍纳入原完整ADT测量，无新exclude/无Data输入、0487 prefix不变。TCM18404B仍待装入，异步HFI未运行。

GMUMAP一次真实success：306读/20写/1次TLBpoll，CB0/ASID1/VMID0/M1、SID5 mask400 translation，SID4FAULT/no RAM；其他SMR invalid/普通CB1..4 M0，protectedCB5..7未读写。私有epoch1/gen1与发布pins保留，仅ATTACHED_QUIET，不自动allow_run。实机SCTLR00c00027，ASIDPNE主动写0并严格验证；FSR raw400保留为Format2(AArch64)，definedFault0。575的17写隔离失败原记录保留，新故障位语义由架构/厂商定义验证。启用映射不等于设备已使用这些页完成DMA。

原CAP20 expectedABSENT、PAS INFO/RSC/IOVA/pool/IRQ/温度回归全部通过，2532页、真实VSYNC软件0、温度16+9路有效。下一步真实五clock冷启动、SYSRESET1和完整32KiB TCM写读，再真实GMU/HFI IRQ、GPU-local RSC/PDC、FW_INIT与HFI消息、CP/BO/fence/像素及20H392 Metal。IRQ arm/readback与实际中断触发分别验收，HFI CTRL ACK与消息通信分别验收，CPU显示、原安全/存储保持；Git尚未新提交/推送。

以下保留历史限定结论。

# 历史0575：原基线与固件WC内存通过；首次SMMU读回隔离，修正中

截至2026-11-03：0575原SCM/Root/nonce、既有bank/systembag、加密Data、安全与watchdog304.426秒通过；真实43页固件内存准备和3个HFI/log页的NormalNC属性通过。首次SMMU映射在读回校验后隔离，保留引用和失败记录，尚未运行固件或DMA；最近全项限定回归通过的是0574。亮度滑块和新增安装继续暂缓。

0575为429共同+4Mac主机补丁/5380源，0450/28处变化/21个新文件，完整native SHA256 `e0c13a818a67666b8a666fddc923757a4a09a2aa0370b0381148648595b6ca92`。六TU、立即native/HID/postbuild配对、20实际exports、27linked符号/精确policy、原compiler保护/ClassB/20H392基类ABI/触屏219槽/cache指令/原组件与fullowner guard通过。新41548B固件属性纳入原完整ADT测量，无新exclude、仍0487固件prefix。

实机GMUSTAGE一次成功：currentSecureDT原固件精确SHA私有snapshot，43个4K映射页预备、8pools/360448B保留、RAM载入22888B，HFI CPU结构准备完成；3个CPU16K HFI/log页pmap属性均为WIMG6(NormalNC)。TCM载入18404B仍待做；异步HFI协议、CM3执行和实际DMA未资格。

GMUMAP一次真实失败：295读/17写/1次TLBpoll，result12/state6 quarantine、primary IO3/cleanup OWNERSHIP6，pins保留，本boot无重试/恢复/复位。请求SCTLR00c01026而回读00c00026，差异为ASIDPNE提示位；随后FSR400表示AArch64 Format，被全字非零判定误作故障。下一版显式不请求该提示（仍验证写0读0），fault判定使用架构定义位并保留raw，不ACK或清零掩盖失败。此处修复尚未上机。

同次启动原CAP20/PAS INFO/RSC/IOVA/pool/IRQ/温度回归已补完，2532页/真实VSYNC软件0、温度16+9路有效。RSC只读查询成功不解除私有namespace隔离。继续有限首次映射修复，再五clock冷启动/SYSRESET/TCM、真实GPU-local RSC/IRQ资源、FW_INIT/HFI、CP/BO/fence/像素及20H392 Metal；不以本轮Stage通过代替固件或GPU可用。原安全、输入、CPU显示与存储保持，Git未新提交/推送。

以下保留历史限定结论。

# 历史0574：真实GMU root/总线/NS观测通过，继续首次映射与固件

截至2026-11-03：0574原SCM/Root/nonce、既有bank/systembag、加密Data、安全与watchdog304.474秒回归通过；CX/MX、GPUCC CX/AHB持有、GMU root200MHz配置ACK、SMMU总线时钟持有与一次普通NS观测通过。首次SMMU映射、设备DMA、GMU固件执行与GPU/Metal仍未验收；亮度滑块和新增安装继续暂缓。

0574为428共同+4Mac主机补丁/5359源，与完整native精确配对，SHA256 `76beba2b9304f767b9c5e93dfbe92a1094b74dec480516c0f2ee6908138a4cd3`。GMUROOT实际578读/5写/215屏障/10微秒/4次poll，PLL600MHz/div300MHz/root200MHz配置取得ACK；未测量真实频率，两条GMU/GX分支保持关闭，未复位或运行CM3。GPUBUS实际258读/1写/44屏障/3次poll，ACKmask7、refs4/children2，未隔离；只新增HLOS1 SMMU gate写入，其余两clock已有请求且真实ACK。CAP均复制各自唯一执行事件，不能作为当前lease。

GPUSMMU只读一次：95读/0写，未分配pool/table、未做TLB操作或DMA。实际ID0=4c017e06、ID1=20000005、ID2=5511：5个普通NS context bank、6个SMR、PA/IPA36位、VA48位。六个SMR均invalid，CB0..4均M0/FSR0，CBAR/CBA2R0；其余未初始化寄存器原样保留为诊断，不假设完整raw稳定。受保护CB5..7未读。后续首次配置须初始化普通CB和route，不能假定已有translation。

同次启动PAS INFO、长期池、4K页表、IRQ/VSYNC和温度三CRC通过，25路配置温度通道均有效；热节流/恢复未资格。旧0487固件下CAP20仍准确返回ABSENT，无低地址grant、NC/PAS或设备DMA资格。571/572的真实CC失败与隔离记录保留。

继续measured ADT固件输入、真实43页backing及HFI/log缓存属性、首次SMMU namespace，然后正常200MHz五clock冷启动、SYSRESET/TCM、FW_INIT/HFI、CP/ring/BO/fence及真实离屏像素，随后20H392 Metal/Turnip/IOSurface。CPU显示、输入、原安全与存储基线保留；固件与密钥不进入公共仓库。原生DPU冷初始化/恢复、PCIe、120Hz及完整热管理仍未全项完成，非GPU必要的长期问题可明确延期。Git分支gpu/adreno650-20261103，尚未新提交或推送。

以下保留历史限定结论。

# 历史0573：真实GPUCC CX/AHB持有通过，继续GMU与专属SMMU

截至2026-11-03：0573原SCM/Root/nonce、既有bank/systembag、加密Data、安全与watchdog304.443秒回归通过；真实CX/MX floor5投票及GPUCC CX上电ACK、retention、AHB持有通过，父引用2且未隔离。原生低DMA只读记录准确报告旧固件未提供区域。active SMMU/DMA、GMU/GPU/Metal仍未验收；亮度滑块和新增安装继续暂缓。

0573为427共同+4Mac主机补丁/5341源，与完整native精确配对，SHA256 `ab897a97e5ebfb0f3c217a1db33bda9d52dbca721794c632a00a7e065d5f9dbe`。标准SCM双锁/session保留原Root调用体与失败语义，PAS worker未接线。GPUCC仅按厂商和主线明确的软件字段比较GDSCR控制，未分类位完整保留；native fresh RMW只合并拥有的collapse0/retention11与AHB enable0，实际1540 powerACK确认。CAP19三份280B CRC相同，refs2/held1/pin1/dead0/quarantine0；78读/2写/13微秒delay/1次poll。CAP仅复制缓存，不重复投票；这不代表GPU/GMU执行。

同次启动PAS INFO、长期池、4K页表、IRQ/VSYNC和温度三CRC通过，25路配置通道此次均有效，未资格热节流/恢复。CAP20的256B原生precopy raw消费者在旧0487固件下返回ABSENT；编译期83cb expected identity不是现场firmware measurement，没有低地址grant、NC/PAS或设备DMA资格。571/572两次真实CC失败与隔离记录保留，不改成通过。

继续GMU200MHz root、GPU SMMU bus lease和专属只读NS观测，再首次route/active namespace、固件/HFI、CP/ring/BO/fence和真实离屏像素，随后20H392 Metal/Turnip/IOSurface。原生DPU scanout/冷初始化/恢复、PCIe、120Hz及完整热管理仍未全项完成；非GPU必要的长期问题可按用户要求明确延期。CPU显示、输入、原安全与存储基线保留。GPU分支为gpu/adreno650-20261103，Git尚未新提交/推送。

以下保留历史限定结论。

# 历史0568：实际CX/MX投票和持有实机通过，继续GPUCC与专属SMMU

截至2026-11-03：0568原Root/nonce、既有bank/systembag、加密Data、安全与watchdog304.467秒回归通过；原生MX→CX两条固定floor5投票取得真实WRITE_ACK，保留一个driver引用，完整通道恢复及IRQ/VSYNC/温度回归通过。GPUCC、active SMMU/DMA、GMU/GPU/Metal仍未验收。亮度滑块和第5项新增安装继续暂缓。

0568为423共同+4主机补丁/5327源，与完整native精确配对，SHA256 `45a52f1d600947630fdb4035d9cbac7b2c4d0eb77210afb1957a0a615948e878`。固定GPUPOWER在正常worker只Acquire一次，实际MX30010 DATA5再CX30000 DATA5；复用原RPMh global owner和独立GPU账本。两条fresh ISSUED+COMPL、RSC idle和完整恢复均通过，refs1/ack3/uncertain0/lease3；三次CAP18完整11100B CRC相同，重复采集仅复制缓存，不重复投票。TCS0、BCM残留和其它foreign word保留，IRQ_ENABLE不写。这里只覆盖CX/MX fixed floor5，不是GPUCC/GFX/BCM/GPU运行或整项电源管理完成。

同次启动PAS INFO、长期DMA池、4K页表、真实IRQ/VSYNC与温度三CRC复验通过（配置25路，前两次16+9有效，第三次15+9；一个硬件VALID=0样本原样排除，未伪造温度）。PAS接口availability不证明PAS13支持、高PA接受或固件认证；长期池也不是设备DMA。原OTA DTB、A650 GMU/HFI/SQE差异与专属GPU SMMU已离线审计；所有新的hardware claim须独立native/实机证据。当前GPU分支gpu/adreno650-20261103，HEAD仍d5c9c62，尚未新commit/push。

下一步是实际持CX/MX引用下的GPUCC/CX domain和GMU时钟接管、合法firmware staging/PAS与GPU专属SMMU，再GMU/HFI、CP/ring/BO/fence/真实离屏像素，随后20H392 Metal/Turnip/IOSurface。DPU原生scanout/冷初始化/恢复、完整热节流、PCIe及120Hz仍未全项验收；按用户要求，久拖且不妨碍GPU正常工作的事项可明确延期，不把6/7/8虚报完成。

以下保留旧阶段原文，仅代表当时状态。

# 历史0565：长期DMA池与原基线实机通过

截至2026-11-03：0565原Root/nonce、既有bank/systembag、加密Data、安全与watchdog304.394秒回归通过；原生长期DMA池准备、nG页表完整传输与独立走表、IRQ/VSYNC/温度复验通过。实际SMMU绑定和设备DMA仍未实现，GPU/GMU/Metal未验收。亮度滑块按用户要求暂缓，第5项新增安装继续暂跳。

0565为420共同+4主机补丁/5312文件与完整原生构建精确配对，native SHA256 `0978a6e01d0cfeb4e783833d87456be62901398bb1502e7ebfbf6f0de55c227d`。只刷c073a488的boot_b，原密钥/加密Data/原版MACF/ACL/Sandbox/watchdog保持。单次IOVAPREP长期持有独立64KiB表与64KiB数据；三次source14完整65728字节CRC和独立表图验证通过：固定0x70000000 IOVA、16个4KiB non-global/RO/AF/SH/NC/XN叶、真实PA高于8GiB。重复采集的是immutable receipt，不是重新扫描live pool，不是设备DMA。source10schema3三次各2532叶独立复核通过；实际raw OSPtr释放修正及complete失败隔离已绑定native，旧unique_ptr主机peer不能证明旧版本实际释放。

单Mac builder与单串口约束保持，0565回归与后续传输均已结束且COM7释放。当前开发分支`gpu/adreno650-20261103`，Git HEAD仍d5c9c62，未新commit/push。0566仅下一只读GPU GCC/CommandDB候选，不能当已上机结果。A650的实际固件、原始OTA DTB与GPU独立SMMU已经离线核对；GMU/HFI/CP/fence/离屏像素及20H392 Metal/Turnip需要分别取得实机证据。

下一依赖是GPU时钟/电源/带宽真实投票和持有、GPU专属SMMU ownership/cache/TLB/故障恢复，再GMU/HFI、CP/ring/fence、离屏GPU像素，随后原版iOS Metal/IOSurface。显示CB5不能推及GPU；新表nG/unusedASID/M=0也不能排除旧global TLB别名。DPU原生扫描提交、冷初始化/恢复、完整thermal策略、PCIe及120Hz仍未全项验收；对久拖但不影响GPU正常工作的事项，按用户最新要求明确延期并保留未验状态，不因延期伪称6/7/8全部完成。

以下保留各旧阶段原文；其中“当前/测试中/最新”等仅指当时现场，不覆盖本节。

## 历史过程原文

# 原生显示、SMMU与时钟：0563基线通过，系统亮度按用户指令暂缓

2026-11-03。用户要求暂缓亮度滑块修复，先核对GPU之前尚未完成的6/7/8；第5项新增安装仍因App Store激活前置暂跳。实机0563，0564仅封存未刷。

| 路线 | 已取得的原生与实机证据 | 继续开发的内容 |
| --- | --- | --- |
| 6 DPU/DSI | 原生GIC SPI115/MDP PP0完成中断，约60Hz真实事件驱动原版VSYNC；0552人工确认画面、滑动、触摸和三种边缘手势正常。0555原生读取真实DSI2.4与继承配置 | 0558至0561面板DCS读写/恢复通过；0560系统滑块人工失败，现按用户要求暂缓。原框架亮度库未成功加载，具体原因尚未确定；原子scanout完成、冷初始化/恢复与120Hz仍未验收 |
| 7 SMMU/DMA/PCIe | 非安全显示SID唯一匹配CB5；实际SMMU支持4K表、36bit PA；0554/0555真实高于8GiB的64K连续表池中7张4K表/2532映射逐项复核通过；0556两遍审计74个NS可见CB，选择空闲ASID6 | 临时表发布前已清零/释放，没有任何CB引用，不能拿返回PA启用DMA。仍需长期池、实际绑定/缓存/TLB/fence与DMA设备证明；PCIe未接入 |
| 8 时钟/电源/热 | 25路有效TSENS温度；实际DSI时钟与ARC cx/mx/mmcx metadata；0556真实MDP同父divider2→3→2，UPDATE应答和原配置恢复通过，硬件帧/IRQ持续且无软件VBL回退 | 标称300/200MHz来自厂商PLL计划，未独立测频；电压/PLL未改。ARC codes不是实时电压，也不能直接作投票索引；完整电源域/热策略仍待实现 |

0556为最近完整通过的本轮硬件里程碑：411共同+4主机/5306源文件与完整原生构建配对。原Root/nonce、既有bank/systembag、已有密钥/加密Data、MACF/ACL/Sandbox及原watchdog304.473秒回归保留。时钟测试只执行一次，CFG100403→100405→100403、6次寄存器写、IRQ19387/19395/19397、software delta0；source12三份CRC回执一致，之后source9/温度三次复验通过，串口均关闭。见[限定收据](qualification/0556/native-clock-asid-qualified.json)。

0557第一次面板读因短包返回长度处理错误而失败，原件保留；0558修正后真实DCS亮度399→349→399通过，0559/0560/0561再次通过。0560背光子服务注册后，用户复验滑块仍回中间/没有变化，系统亮度未验收。

0561原版backboardd只读采样在原cache/可执行UUID和正常线程条件下运行：IOMFB亮度库初始化once已结束但句柄和9函数均0，主显示context背光ready/control为0；发布背光服务后仍相同。原始20H392指令另确认BuiltIn能力允许内建显示直接关联，不能把ContainerID猜测当根因。0562继续从原dyld已保存的线程错误中读取具体原因；这些诊断不计作亮度功能完成。

最近完整回归0561为416+4/5311，原密钥/加密Data/安全与watchdog304.485秒保留；真实面板读写/恢复、IRQ/VBL和温度各三份CRC通过。0562为417+4/5311，完整原生/回放/ABI已过且刷boot_b，正在实机回归。新增18+25生产观测函数主机模型分别检查UUID、缺失状态、地址/长度边界和读取竞争；不能替代真实错误文本。

参考本地LineageOS HEAD7c2b1aedb5573d140684a9ecc918ce22af5f0817的干净选取文件；不修改其既有KernelSU/供电等dirty，不把厂商GPL实现体复制进MIT板级模块。保留此前真实失败与错误修正：0547白名单遗漏、0548温度偏移、0553低于4GiB表池分配失败；没有把失败改写为成功。

本轮源码与记录尚未提交或推送；GitHub最后发布仍是0544。6/7/8均未标为全项完成，继续开发，不存在需owner处理的当前阻塞。


## 最新核对：GPU前剩余工作

0563正常回归304.571秒及面板399→349→399/IRQ/VBL/温度复验通过。原生DPU扫描提交、长期active IOMMU与真实DMA、完整clock/reset/power/thermal生命周期仍未完成。参考本地kona-gpu.dtsi的KGSL/GMU IOMMU资源，显示CB5结果不能代替GPU上下文验证；PCIe可随后续无线并行，不视为Adreno本体启动必经步骤。优先active SMMU/DMA，再补最小clock/power闭环与原生DPU提交。亮度滑块暂缓、120Hz未验收。

0564仅完整构建封存，未打包/刷入。前文0561/0562“测试中”为当时历史记录，不代表当前还在运行这些捕获；现所有串口观察者已退出。6/7/8仍未全项完成。
