# 0609 First Render 后的 GPU IRQ / 20H392 Metal 检查点

2026-11-05。0609 已有实际 PAS INIT/MEM_SETUP/AUTH、安全退出及原605两张离屏图 PASS。这个目录只包含后续主机/编译与源码依据，不是 Metal 实机结果。设备仍保留0609，不为本检查点刷写、重新认证或发布用户态 GPU 能力。

## 已执行的客户端契约资格

官方 UniXNU/Adreno740 固定 `1030485546ef7ebb9abbfb7c17fbca93cca40971`。原样保存 MIT LICENSE、NativeAdrenoABI.h、NativeAdrenoClient.h/.cpp。新 test-client-contract.cpp 使用真实客户端生产代码和显式 HOST callback model，不连接 IOKit，不提交 GPU。

- Mac Apple clang15：`-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined` 编译并实际运行通过。
- 先验证 ABI v1、未知 flags、31位物理地址、超 BO 上限、非4KiB大小、129/0条 IB 被拒绝；无效请求不进入 callback。再验证 v2 的128条 IB、3104字节完整提交、传输超时原样返回并清空输出 Fence、关闭后拒绝调用。
- 生产客户端和契约测试均以 `arm64-apple-ios16.7` 编译为对象；LC_BUILD_VERSION 平台2/minos16.7/sdk17.5。IOKit 类型头来自 macOS14.5 SDK，通过显式 framework 路径引入。这证明交叉编译和目标布局，**不是20H392私有运行时 ABI 或插件资格**；未链接/加载 iOS 插件。
- v2 Params24、DeviceInfo64、Submit3104、buffers偏移32、上限128。旧 A650 v1 Submit416/上限16 与此不同；相同 Transport callback 签名不能消除此差异。当前 v2 还含方法17/18，不能仅凭版本号把不同固定提交的功能集合视为相同。

## 适配边界

| 层 | 当前证据 | 下一实现入口 |
|---|---|---|
| A650 2D / 同步 | 0609 两图全像素、真实 Fence/RPTR/颜色缓存时间戳 PASS | 保留该底座，任何客户端化改动必须重跑原负载 |
| GPU IRQ | 仍 NOT_RUN；本次仍 irq_mask=0 | SPI300/INTID332 的有界单源实验，先核实 GIC 所有权/目标CPU/触发方式/EOI 与 MMIO 序列 |
| BO/VM | 当前私有受控诊断所有权与缓存资格 PASS | 16KiB CPU 分配对应4KiB GPU页；实现每客户端句柄、隔离、引用和失败保留，不能把原诊断 BO 直接交给任意客户端 |
| NativeAdreno UserClient | 0609 尚未发布 | 固定 v2 全结构/大小/能力查询，正常 MACF/ACL/Sandbox 接入；禁止 selector 扫描、tunnel、mailbox 或 NativeSandboxGrant 绕过 |
| Metal 前端 | 20H392 原符号/镜像清单在 metal-abi-0581-audit/audit.json；仅静态 pin | 先实现独立测试进程的真实 BO/submit/wait，再桥接 MTLDevice/Queue/Buffer；保持 CPU 显示路径 |
| 官方当前 provider | AdrenoMetal `e82788dd367753802ec2e00476ab27addfb46834`；OriginalAcceleratorABI 明确 iOS17；IOGPU opaque extent736/slots168–214 | 单独核实20H392，不能搬用其 iOS17 vtable/地址；当前 prelink 未装 IOGPUFamily/AGX |
| Turnip / AIR | A650 编译器支持属于上游来源；未完成当前用户态后端连接 | 固定 Mesa、Darwin backend、metal2vulkan 各SHA与接口；先 blit/fillBuffer 避免把 AIR 编译当首个执行前置，再真实 Shader |

## GPU IRQ 的固定源码依据和风险

本地已固定 vendor `7c2b1aedb5573d140684a9ecc918ce22af5f0817` 的 kona-gpu.dtsi：GPU `reg=0x03d00000/0x40000`，`<0 300 IRQ_TYPE_LEVEL_HIGH>`；GMU/HFI 为304/305，不能混用。A650 GPU GIC INTID 为300+32=332。此处是来源审计，不声称已注册或收到中断。

已固定 alioth-ring `70771a71795d7e1a78afdb82badd7943adb0a3a0` 的 NativeAdrenoIrq.cpp：状态/清除/掩码 DWORD 索引0x201/0x37/0x38，CP状态/保护0x823/0x824；寄存器窗口内实际字节偏移需乘4。A6xx enable0x03d0e242，完成位 CP_CACHE_FLUSH_TS=bit20；这不是直接启用整掩码的授权。通用 decoder 的 kKnown 和 kCpErrors=0x3fff7 带 A7xx 项，不能不加代际边界复制为 A650 错误策略。

下一轮应先编写 A650-only 中断状态分类/仅自身已确认位 W1C 的 host 故障测试，绑定既有332路由和唯一父级所有者；使用一次已审命令与硬件 Fence 独立对照，保留原有 polling deadline 和 fault 隔离。未知中断或清除失败时停止并隔离，不做自动 reset/replay。现有用户暂离，首次新 IRQ 原生镜像/实机动作待恢复配合条件重新满足。

`A650_METAL_FIRST_EXECUTION=NOT_RUN`，`GPU_IRQ_DELIVERY=NOT_RUN`。未修改 systembag、Data、Root/nonce、原安全策略或设备镜像。本检查点后的人工 GUI 检查 NOT_RUN，不能用用户“假设正常继续”替代实测。
