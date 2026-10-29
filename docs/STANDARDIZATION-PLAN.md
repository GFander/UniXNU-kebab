# 标准化执行计划


---

## 阶段 A：驱动标准化（先做）

### A1 SPMI/PMIC 总线分层

**现状**：`kebab_spmi.c` 1907 行，一个文件里同时装了
PMIC arbiter 访问、PON 块、按键采样、VRM/TCS 四条互不相干的职责。

```
arbiter 访问      core/chnls/obsrvr/cnfg 的映射与读写原语
PON              restart reason 的读与受门控的写
keys             GPIO6/GPIO7 采样与配置
VRM/TCS          rail 使能、TCS 状态机
```

**目标形态**（按 UniXNU 的三层与 personality 规则）：

```
KebabSPMIBus        arbiter provider：映射、通道、APID→PPID 解析
  ├ KebabPon        PON 子设备：restart reason
  ├ KebabPmicKeys   PMIC 按键子设备：GPIO 采样
  └ KebabVrm        VRM/TCS 子设备：rail 与 TCS
```

**改造顺序（三步，每步都能独立构建验证）**

```
A1a  抽出总线层：新建 kebab_spmi_bus.c，把映射与读写原语搬过去，
     kebab_spmi.c 改为调用它。行为零变化，日志零变化。
A1b  把 PON / keys / VRM 改成总线层的客户端，仍用现有入口函数，
     只是内部不再直接碰寄存器。
A1c  加入 IOKit 身份：KebabSPMIBus 作为 IOService 注册，
     子设备通过 personality 匹配（IOClass / IOProviderClass / IONameMatch）。
```

**保留项（不许动）**

```
panic FASTBOOT 路径      不引入 IOKit、不加锁、不分配
boot-arg 门控            现有 ufsrw / ufsmedia 之类的语义不变
日志格式                 便于和上一版对比
```

**验收**

```
构建通过，内核哈希变化
上机：panic FASTBOOT 仍可用；PON 回读值不变；按键采样值不变
日志里原有行一条不少（新增行必须能解释）
```

### A2 GENI 总线分层

```
现状   GENI/I2C 代码在 kebab_touch.c 里，和触摸协议混在一起
目标   KebabGeniBus（QUPv3 SE 传输）+ KebabTouch（协议层）
保留   bitbang 诊断路径
验收   触摸采样值不变；总线时序日志与改造前一致
```

### A3 用 UniXNU 指南回审全部驱动

按以下文档逐条对照，产出差距清单与改进项：

```
https://unixnu.com/zh/porting/drivers          适配顺序与接入步骤
https://unixnu.com/zh/drivers/overview         三层驱动、personality、设备树匹配
https://unixnu.com/zh/drivers/xnu-drivers      内置 XNU 驱动规范
https://unixnu.com/zh/drivers/device-tree-matching
https://unixnu.com/zh/porting/platform         平台与 DMA
https://unixnu.com/zh/porting/ufs              存储
https://unixnu.com/zh/porting/display          显示与亮度
https://unixnu.com/zh/porting/input            触控与按键
https://unixnu.com/zh/porting/gpu              GPU 与 Metal
https://unixnu.com/zh/porting/power            DSP、音频与电源
```


### A4 新增驱动一律按 UniXNU 方法

流程固定为：查组件目录 → 选层次（UEFI / 内置 XNU / kext）→ 按匹配规则确定
provider 与资源 → 按对应实现步骤接入 → 构建 → 按子系统适配检查验证 → 记录。

---

## 阶段 B：主线

```
B1   APFS 读写能力，替代 HFS+ 根卷
B2   定位 ignition 的等待点
B3   Data / Preboot 卷装配
B4   动态信任缓存
B5   Cryptex / SystemOS
B6   共享缓存映射
B7   引导任务跑完
B8   GPU / Metal（含 SMMU）
B9   backboardd + 直扫 + 真 vsync + 120Hz + 输入
B10  SpringBoard：锁屏与桌面
```

### 调整理由：显示等 GPU，然后直接做直扫

原计划里显示分两步（先 CPU 拷贝，再 GPU 合成，再直扫）。现在改为：

```
GPU 驱动先修好
  ↓
显示一次到位：DPU 直接扫描 GPU 合成的缓冲 + 硬件 vsync + 120 Hz
不经过"GPU 渲染到扫描缓冲再拷贝"那一层
```

这样省掉一次中间改造，但也带来两个必须先解决的问题，见下。

### 补全说明

**B8 GPU/Metal**：已定基于 UniXNU（Adreno740 的结构 + AdrenoMetal 前端）。
A650 属 a6xx，需要替换寄存器层、固件名、GMU 参数与 SMMU stream ID。
**SMMU 适配作为 B8 的第 0 步**，因为：

```
GPU 的 BO 池满之后要用分散页，靠 SMMU 拼成连续 IOVA
直扫时 DPU 要读 GPU 分配的缓冲，那些缓冲不是物理连续的
显示 context bank 需要 stage-1 窗口
```

也就是说，SMMU 现在同时被 GPU 和直扫依赖，不再是可选项。

**B10 SpringBoard**：B9 通了之后才可能。需要 FrontBoard、RunningBoard、
SpringBoardServices 起来，然后才是主界面。

### 两个必须先解决的问题

**一、GPU 的验证需要一条呈现通路**

GPU 渲染出来的画面要么显示出来、要么读回内存比对。在做直扫之前，需要一条
能看到结果的路径。做法：

```
KebabFramebuffer 的 CPU 拷贝路径暂时保留，作为 GPU 的验证出口
GPU 渲染 → 读回 → 用现有路径显示 → 肉眼确认
等直扫打通后，再把这条路径降级为回退
```

**这不是保留"GPU 拷贝"那一层**，只是保留一个调试出口。

**二、直扫要同时满足三个条件**

```
一  GPU 能合成（CoreAnimation 能渲染）
二  显示 context bank 有 stage-1 窗口，或用物理连续缓冲
三  vsync 与缓冲交换的生命周期正确（换缓冲不能撕屏）
```

第三条最容易出问题：DPU 正在扫描的缓冲不能在换帧时被释放或改写。

### 依赖关系（不可颠倒的部分）

```
B1  独立，但 B3 依赖它（Data 卷要在同一个 APFS 容器里）
B2  独立，最好在 B3 之前做，它可能直接指向 B3
B4  依赖 B3（动态缓存位置在 Preboot/Data）
B5  依赖 B4（cryptex 的签名链）
B6  依赖 B5（部分缓存文件在 cryptex 里）
B7  依赖 B1-B6 全部
B8  B7 之后；SMMU 是它的第 0 步
B9  依赖 B8（GPU 先能合成）
B10 依赖 B9
```

---

## 横切工件（贯穿全程）

```
驱动清单 manifest + DRV? 命令      每次启动一眼看出哪层失败
abi/<class>.json 审计记录          每接一个 Apple 私有类锁一份
启动步骤可观测                     ignition、launchd、kext start 的步骤号
```

---

## 每个批次的固定动作

```
一  改代码（单一目的，不混功能）
二  全量重建：rm -rf dxb/build/xnu.obj dxb/build/xnu.sym
三  核对内核哈希确实变了
四  生成候选镜像，校验跨度与同余
五  刷机，读日志，比对旧行为
六  记录：改了什么、验证了什么、有没有回退
```

任一步失败就回退到上一批的镜像，不带着问题往下走。