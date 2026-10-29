# 驱动标准化执行记录

最新状态见 `../STANDARDIZATION-RESULT-20261029.md`；下文早期批次保留为历史，文末有最新检查点。

依据 `D:/LOS/HiOS/DRIVER-AUDIT-20261029.md`。2026-10-29。原审计针对0190，保留为历史基线；本文件记录实施结果。

## 实施边界

以保持已有硬件路径和目标20H392 ABI为前提，逐包构建和真机回归。当前完成审计实施顺序的第1包和第2包的请求/生命周期部分。总线provider重建、HID、正常USB服务化与真实电源能力仍未完成，不能称为全部驱动已经标准化。

所有本轮设备写入仅为boot_b；userdata/APFS/GPT未再次覆盖，UFS仍`ufsrw=0 rootrw=0`。原生内核仍为XNU8796.141.3/16KiB/单核测试。IOStorageFamily、APFS、AMFI、FSCompression供体代码保持原样；没有替换成最新版macOS接口。

## 0173 / 0191：Surface、显示、UserClient

- 修正KEBAB personality，发布真正的IOCoreSurfaceRoot，IOResources/IOBSD匹配；NEGRONI匹配保持。
- AppleCLCD的init、start失败回滚、stop/free成对管理锁、Surface、映射、workloop和timer。停止通过公开`IOWorkLoop::runAction`进入工作循环，取消/移除timer，结束SwapWait等待。不能直接从子类调用protected closeGate/openGate。
- 平衡物理Surface创建者的table user和retained引用，保留仍被客户端引用的Surface。
- 两个UserClient使用共同参数合约入口，保留原selector和合法的附加scalar、可变/部分structure约定；未知调用仍返回Unsupported。每连接的调用/关闭锁保证关闭与在途调用互斥，显示通知权利和Surface引用按关闭路径清理。
- 真实类未改名。selector74仍是项目兼容布局，未宣称已恢复原版私有ABI。

验证：真实catalogue三种板型编译、生产参数检查器、9类显示启动失败注入、timer并发停止、Surface存活用户和重复关闭测试均通过；真实KDK编译参数检查3个TU、XNU全量构建通过。

0191已刷入真机，Surface注册、APFS根卷和PID1/libignition保持。launchd与dyld完整读取哈希不变。主动PANIC!后串口FASTBOOT验证PON/IMEM回读，fastboot实际返回`c073a488`。日志在`../p1-run/prelink-fix/0191-*`。

## 0174 / 0192：块存储合约与真机原生客户端自测

- 明确C后端拥有UFS控制器；IOKit桥仅拥有其64KiB staging buffer。保留现有platform root provider和启动期常驻singleton，不伪造一个新控制器服务。
- 几何检查要求块大小可被staging chunk完整容纳；在传输前检查整个descriptor长度，保留较长descriptor。避免分块写入一部分后才发现descriptor不足。
- 明确init状态；在线状态由start/stop管理。stop等待在途传输锁并将设备离线，后续读取、同步和idle hint返回NoDevice。媒体状态报告也使用同一锁。
- 首次发布使用独立mutex，24个并发调用只发布一次；不能持有C后端锁进行IOKit匹配，防止geometry回调嵌套。
- 保持同步轮询执行：被接受的请求恰好完成一次，解锁后回调，错误时带上已完成的整块字节数；立即拒绝不回调；NULL completion保留原同步返回约定。ETIMEDOUT明确映射Timeout。原来已有的后端锁和解锁后回调得到保留。
- 保持只读、固定DMA/cache维护、无IRQ/SMMU/SMP重写。Idle成功仅表示接受提示，不代表硬件进入低功耗；unsupported功能未伪装成实现。

`tests/test-blockstorage-contracts.py`执行真实方法：ASan/UBSan、部分初始化清理、越界/溢出/短descriptor、第二块失败/部分完成、超时、callback重入、stop与在途请求并发、RO拒绝全部通过。`test-blockstorage-publication.py`执行实际发布函数，禁用/失败回滚/24并发调用通过。

0192全量构建后启用一次`kebab_driver_test=1`，在正常init线程测试：

- IOCoreSurfaceRoot真实factory/open、Limits、坏输出、close及关闭后拒绝：PASS。
- AppleCLCD真实factory/open、DisplaySize、坏输出、close及关闭后拒绝：PASS。
- 真实IOBufferMemoryDescriptor读取NXSB；completion恰好一次且可重入媒体查询；short/bounds/RO均在传输前拒绝：PASS。
- 随后继续执行真实PID1/libignition；launchd、dyld完整读取SHA256与原版相同；正常FASTBOOT自动返回引导器。

这个自测验证内核里的工厂和连接合约，**不等于用户进程IOServiceOpen、原版IOSurface.framework或backboardd的完整ABI验收**。自测默认关闭；0193移除该boot-arg，内核/prelink内容与0192相同。

## 固定ABI与交付物

`audit-storage-abi.py`核对固定供体SHA、原版IOStorageFamily可执行段SHA、10个硬绑定及编译后桥的22个storage虚函数槽位，并输出`abi/storage-0192.json`和`abi/storage-0193.json`。这不是完整私有图形/HID ABI或运行时class size证明。

- 0173补丁：`0173-kebab-iokit-driver-lifecycle.patch`。
- 0174补丁：`phase2/0174-kebab-blockstorage-contracts.patch`。
- 原生0191：`b9961acdc9fe297bafb4b8db91cdd83ff056fbf2419cd15d40b748ef1d11a5a6`。
- 原生0192/0193：`754293bead19aa20f28800abee39429862d5252c4ae0fa4e3cc63ca98a9afdae`。
- boot0191：`6e898ce3bf69eedbebae91af0709ed04e7d322d2c6dd37118f17d201d85fa3e0`。
- boot0192自测：`7180acd748f5c2ed2e8115002c09a372e70ff873a19164532c2659cb2df666bc`。
- boot0193常规：`cb8c775e1007912305942352e00fa1f17a00995e80d826e8cbd073509c6d27a0`。

构建使用Mac真实源树的`dxb/build.sh`，清理obj/sym后全量构建，没有使用会fetch/reset的build-local。源文件与补丁已应用Ubuntu实际树、Mac实际树与独立重放树；尚未提交/推送Git。

## 0193常规镜像最终回归

0193已确认Sending/Writing boot_b均OKAY后重启。配置不包含`kebab_driver_test`，启动日志也没有自测调用；Surface注册、APFS UUID根挂载及真实PID1/libignition与0192相同。两文件SHA256再次一致。随后主动`PANIC!`，串口FASTBOOT在panic路径跳过文件系统同步，PON读回0x04、IMEM读回0x77665500 MATCH并直接PSCI复位；`0193-final-fastboot.txt`确认`c073a488 fastboot`，无按键介入。

当前手机：boot_b为0193-ro，活动b，停在fastboot。没有后台构建、捕获或刷机任务。当前无新自发panic；为恢复回归注入的panic已明确标记，不能算成启动故障。完整用户态最后仍停在ignition参数，不把此次驱动改造当作iOS界面推进。

`verify-result.py`检查9个改动/新增文件及其余10个主要实现文件在Ubuntu/Mac/独立重放树中的一致性、镜像身份、真机自测、两文件哈希与正常/panic恢复证据，生成`evidence-0193.json`。未修改的10个主要实现与原审计快照SHA256一致。

## 后续实施顺序与不能跳过的验收

1. **总线层**：从现有SPMI/GENI代码形成明确资源provider和子设备边界。当前按键线程与控制台可能访问同一observer channel；应设计普通事务序列化，同时给panic恢复保留有界、无IOKit依赖的路径。不能直接给所有SPMI入口套sleep mutex，panic可能中断持锁线程。TCM协议、坐标和按键去抖核心保持，先验证总线错误与取消，再接事件服务。
2. **HID**：当前NO_KERNEL_HID、prelink无IOHID组件，且backboardd尚未运行。必须先从固定20H392供体恢复需要的类/事件/虚表和依赖，再加入适配服务。公开主线IOHID只能参考架构，不能据此猜私有vtable。完整触摸按下/移动/抬起/取消、按键去抖及真实backboardd接收是最终验收；现有devfs队列保留。
3. **正常USB服务化**：DWC3传输核心、普通命令分派、安装协议和复位策略分层；由正常服务拥有轮询线程及断连重连状态。不得只注册一个IOService名称便声称重构完成。早期/KDP/panic路径继续只依赖预映射资源与C核心，普通服务gate不得进入panic。
4. **电源能力**：RPMh白名单/TCS状态机、SPMI/PON与消费者分别建模。只读查询、门控写入、真实调压/时钟、面板电源、整机睡眠逐项列硬件回读/错误恢复证据后才接IOPM。现有PMGR provider-less原版nub、FB内存置黑不构成真实电源驱动。
5. **私有图形ABI和模块化**：固定selector74、端口/映射/跨进程Surface的原版调用资格，待完整图形用户态可运行后实测。再考虑独立kext及依赖打包；不为形式统一移动CPU/PE等常驻对象或早期C代码。

上述3–5包涉及当前尚无的功能和硬件验收，仍属于待开发项。现阶段不修改这些尚未充分验证的运行路径。

## 七小时窗口后续：0175–0184 / 0196–0205（进行中）

A1 SPMI分层、A2 GENI/TCM分层、正常查询服务、DRV?和devfs发布时间修复已实施并逐批全量构建/刷测。源代码与实际设备状态、13文件到21文件/10类到18类的计数、10篇官方指南逐项回审、尚未完成的HID/电源/图形资格详见 `../standardization-run-20261029/A3-GUIDE-REVIEW.md`；实时状态见同目录RUN.md。

0175先修复旧基线即可复现的observer争用。0196在正常SPMI锁内主动panic仍可无锁回fastboot，24次PON正确；之后再做零语义抽取。0199三个PMIC/RSC服务类型化查询PASS；0202 GENI/Touch实际匹配及只读状态PASS，完整事务锁覆盖TCM命令/帧与普通诊断。触摸identify/decode/node均为0，尚无采样/HID资格。0203将按键节点移到devfs就绪后，实际字符设备lookup和24字节记录通过，采样/去抖体保持。

0204的manifest包含9个功能域、21主要实现文件、18类、11真实registry角色；清单路径在两台实际树和独立重放树一致。逐类ABI记录保存于本轮abi目录，仍明确图形用户ABI和selector74未通过。当前原版PMGR指令/大小/native init及storage绑定/槽位核验通过。截至0204尚未再次写userdata；后续受控Preboot写入见最新检查点。System根卷始终只读，launchd/dyld完整SHA相同。

0205加入安全有界的等待线程内核栈，已确认PID1阻塞于sysctl/AppleImage4的gate，当前进入B2的具体OID诊断。仍没有完整launchd、Data持久化、Cryptex/DSC、GPU、backboardd或SpringBoard验收。上述0193“当前状态”保留为历史，此节与RUN.md覆盖它。

## 最新窗口检查点（自动更新）

已验证0225；详见`../STANDARDIZATION-RESULT-20261029.md`。本节覆盖前文历史的当前状态。SPMI/GENI分层、keys devfs、USB复位分离通过；Preboot持久写已过，Data内容保护和Image4专用key仍缺。25主要实现/19类/11 registry角色；9硬件域计数不变。
