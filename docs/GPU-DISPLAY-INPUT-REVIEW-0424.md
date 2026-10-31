# GPU、显示、输入指南复核：0424

2026-10-31。原指南以0289为基线，0347复核后的启动内容和服务状态也已有变化。此次对照正常0424、对应0423原生内核、293 common +4 host/5252文件一致的源码，以及实际发布payload重新审核。证据索引为boot123-20261029/guide-current-inputs-audit-0424.json。蓝牙已按用户要求延期，不再作为图形阶段前置。

原硬件Root/nonce、原版MKB/systembag、factory C/D加密Data、defaults资源生命周期、MACF/Sandbox和watchdog已有对应实机回归。当前原版launchd已越过早期phase2缺内容问题。A/B、口令、完整SEP和AKS17/35仍未完成；图形不会替代它们，遇到实际启动阻塞再针对性处理。

## 最新实证

1. 0424 SpringBoard在FrontBoardServices的`FBSDisplayMonitor _initWithBookendObserver:transformer:`触发BRK。实际字符串为“failed to find the main CADisplay”。已按匹配UUID的原版dyld缓存符号及调用点核对，不能将其泛称为GPU未启动。
2. framebuffer已经存在：0424 MobileGestaltHelper实际调用selector8和28成功。最新进程快照也包含原backboardd。因此“没有注册显示服务”“backboardd从未执行”均不成立。进程存在不证明其渲染、事件或显示枚举完成。
3. 最新KebabFramebuffer仍为固件GOP映射、CPU拷贝和IOTimerEventSource，提供1125×2436逻辑画布与1080×2400物理裁切。没有原生GPU/硬件vsync/直扫资格。原指南对早期帧地址硬编码的描述已过时；以PE_current_console/boot_args.Video和实际交接范围为准。
4. 原版iOS16 `_kern_SupportedFrameInfo` 在0x1af227c60以selector74调用IOConnectCallMethod：1个scalar输入、1个scalar输出、最多64字节结构输出；scalar输出交给调用者作为条目数。它不是宽高/色域/刷新率的模式结构。当前自制版本填入自定义kebab_frame_info是不正确的ABI，必须撤销该声明；未实现的追踪信息应明确返回unsupported。
5. 当前预链接仍有17个代码组件，没有IOHIDFamily或IOHIDEventDriver。相同供体的元数据闭包增量仍为IOHIDFamily代码及IOHIDEventDriver codeless personalities；不能将新增roots当作真实触控事件验收。
6. 实际ADT中只有kebab-geni13触摸相关节点，没有GPU/DPU/DSI/SMMU资源节点。不能照搬sheng或SM8350的context、ASID、寄存器和双管线假设。

## 参考边界与修订顺序

保留原指南的HID路线、供体版本绑定、最终GPU合成与DPU直扫、CPU拷贝回退和不触碰secure context。撤销“A740接口/编译链代际无关可直接照抄”“Peppy名称即适合S3908”“selector74枚举显示模式”等结论。

官方[显示指南](https://unixnu.com/zh/porting/display)要求先核实GOP几何和保留范围，建立正确像素呈现与完成通知，再启用GPU交接。官方[GPU指南](https://unixnu.com/zh/porting/gpu)要求为其他代际分别实现后端及编译支持。官方[输入指南](https://unixnu.com/zh/porting/input)要求按目标kernelcache恢复HID ABI，并验证一致触点帧、坐标和按键释放。

本次官方链接固定的GopFramebuffer为cf3b42979a9c44219e7bb149543e6fc3388b58ed，公开MobileFramebuffer头明确标注iOS17实验ABI，不能直接替换本机iOS16类。其将selector74作为可选追踪键的处理方式与本机iOS16调用方核对一致，但对象尺寸、槽位和私有状态偏移仍须分别审计。sheng DisplayTimingProbe固定516a780473a2ac59c8b438e132bb60204dda9e22，仅作为读取范围与时序方法参考。

实施顺序：

1. 修正已证实的显示ABI错误，保留每个真实客户端的调用/失败诊断；跟踪CADisplay枚举，恢复原backboardd真正需要的显示资源和接口。
2. 在受控package入口单独启用原版HID预链接闭包，检查重定位、初始化、personality注册和用户客户端，再连接S3908/PMIC真实采样。不能伪报usage或注入合成Home事件代替多点触控。
3. 依据本机DTS、固件与读回补齐显示资源；先只读盘点DPU和非secure SMMU状态，再实现正确的映射、缓冲引用和硬件完成通知。
4. 接A650的真实电源/GMU/HFI、固件、页表、ring/fence和已知像素读回，再连接匹配Metal前端；不把UFS高地址DMA资格外推到GPU。
5. 验证可见可用SpringBoard、输入、恢复和长时运行，再推进直扫/120Hz性能门槛；最后恢复蓝牙真实功能开发。

本审核没有宣称图形、HID或SpringBoard完成。审核完成后按用户最新授权继续实施，不沿用0347“审核后停止”的历史限制。
