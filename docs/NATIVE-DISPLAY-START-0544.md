# 原生DPU/DSI开发起点：0544稳定基线

2026-11-02。用户确认第4项基本GUI交互正常；因当前无法使用App Store安装新增应用，明确要求暂跳第5项新增应用安装验收，继续第6项。第5项保留“基础稳定性与安装前置通过，新增应用安装/启动未验证”，不标记全完成。

## 本阶段实际完成

新增离线工具 `scripts/qualification/read-kebab-display-resources.py`，严格检查FDT块边界、节点/属性、寄存器cell几何、reg-names数量及中断父域。保留总线ranges原值，地址标为bus_address，不擅自认证物理MMIO映射。仅输出白名单资源与面板选择，不输出整段bootargs、序列号或私有属性。

`scripts/tests/test-kebab-display-resources.py`通过592项合成边界、截断、重复属性、cell/寄存器尺寸、中断父域及隐私检查。另对本地Mu-Silicium参考kebab.dtb完成实际解析，得到8个显示资源节点；SHA256为 `1ec75ba9dbbf094c70d0f5ea9b4592980f62434da1972d95ba948452f4bf677f`。这份DTB来自参考固件，不是当前手机运行态面板身份或寄存器读回证据。

## 已核对的参考资源

| 资源 | 总线地址 / 长度 | 限定含义 |
| --- | --- | --- |
| DISP_CC cc_base | 0x0AF00000 / 0x20000 | 整体显示时钟窗口 |
| MDP mdp_phys | 0x0AE00000 / 0x84208 | 不自动赋予电源、映射或非secure访问所有权 |
| DSI0控制器 | 0x0AE94000 / 0x400 | IRQ父域为MDSS，cells为4,0 |
| DSI1控制器 | 0x0AE96000 / 0x400 | IRQ父域为MDSS，cells为5,0 |
| DSI0 PHY / PLL | 0x0AE94400 / 0x7C0；0x0AE94900 / 0x260 | 不复位或重配 |
| DSI1 PHY / PLL | 0x0AE96400 / 0x7C0；0x0AE96900 / 0x260 | 不能据存在便假设双路活动 |
| PLL gdsc_base | 0x0AF03000 / 0x8 | 不是DISP_CC整体基址 |

MDSS上级为GIC，interrupt cells为0,83,4；DSI的4/5是其子域编号，不能直接使用为GIC号。保留源定义，不在本阶段编造IRQ路由。

仓库 `ShengDpuObservation` 明确绑定SM8550/DPU9；本机SM8250的布局须独立恢复。Linux v6.12的[SM8250 DPU6 catalog](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/msm/disp/dpu1/catalog/dpu_6_0_sm8250.h)为后续寄存器语义交叉参考，不能搬用SM8550布局。

旧PANEL-DSI.md中的AMB655X序列、DSC及TE资料是已有参考输入；其中DISP_CC整窗口描述由上表纠正。参考DTB选AMB655X不等于本机实际面板已再次核实。现有内核使用运行期GOP交接，不能把旧文档固定0x9C000000当作当前可靠地址。

## 下一实施关口

1. 从本机现有固件交接/面板证据确认实际资源、供电和时钟保持条件，固定SM8250寄存器语义与非secure映射范围。
2. 添加有界只读原生状态采集，观察实际CTL/SSPP、DSC、INTF/TE和扫描缓冲；电源关闭、映射或资源所有权不明时保留失败，不盲读MMIO。
3. 实际TE/完成计数及IRQ路由验证后再接硬件完成通知；不要把当前IOTimer回调当VSYNC。
4. 保留CPU产生原版画面，逐步完成单管线扫描接管、亮度控制和恢复。之后才做60/120Hz切换及完整冷初始化。

本阶段没有新内核构建、刷机、MMIO读写、面板reset、时钟/PLL/DSC重配或VSYNC/120Hz验收。当前实际版本仍0544；工作目录native-dpu-0545仅是下一候选的准备目录。原密钥、加密Data、安全与watchdog边界保持。

## 重跑

```sh
python3 scripts/tests/test-kebab-display-resources.py
python3 scripts/qualification/read-kebab-display-resources.py --dtb /path/to/reference-kebab.dtb --output /tmp/new-reference-resources.json
```

输出使用新路径，拒绝覆盖既有收据；工具不访问设备或网络。参考DTB本身不随本次改动发布。
