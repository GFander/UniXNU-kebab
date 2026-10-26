# 面板 DSI 直驱：AMB655X 初始化序列

本文件记录 kebab（OnePlus 8T，SM8250）面板初始化序列的来源、提取结果、保真度验证，
以及内核自己点屏还缺什么。配套产物：

| 文件 | 内容 |
| --- | --- |
| `scripts/extract-panel-init.py` | 抓取、解析、校验、生成下表与头文件 |
| `scripts/panel/kebab-panel-amb655x.json` | 机器可读的完整命令表与时序 |
| `scripts/panel/kebab-panel-amb655x.h` | 可直接粘进驱动的 C 数组与 timing 结构 |
| `docs/PANEL-DSI.md` | 本文件 |

当前移植仍然走引导器帧缓冲（`0x9C000000`，1080×2400），本文只做离线提取，
不改内核、不改补丁、不上真机。

## 一、结论

1. 面板是**三星 AMB655X**，设备树节点 `qcom,mdss_dsi_samsung_amb655x_dsc_cmd`：
   1080×2400、4 lane、DSC、命令模式（`dsi_cmd_mode`）、60/120 Hz。
2. 初始化序列**不在 C 文件里**。厂商内核 `techpack/display` 只有通用
   `dsi_panel.c`，没有 `dsi_panel_kebab.c`；面板数据在设备树 dtsi 的
   `qcom,mdss-dsi-on-command` 字节数组里，由 `dsi_panel_create_cmd_packets()`
   在 probe 时解析。
3. 提取结果：**47 步 / 264 字节 payload / 593 字节原始流**；两条延时
   （第 4 步 11 ms、第 30 步 61 ms）；表校验 CRC32 `0xED2FEC73`（60 Hz）与
   `0x34E4EFC1`（120 Hz）。
4. 与设备自身 DTB 里的编译结果**逐字节相同**（两种模式各 593 字节），
   见 §五；用另一套独立的 awk 解析复算，SHA-256 也相同。
5. 内核要自己点屏，还缺 DSI 控制器/PHY/PLL、DPU + DSC、供电与 TE 的驱动，
   以及命令模式下的帧同步，见 §八。

## 二、面板身份

### 2.1 引导器自己的选择（决定性证据）

Mu-Silicium 仓库里的 `Resources/DTBs/kebab.dtb` 是 kebab 的 dtbo dump，
`/chosen/bootargs` 带着 ABL 运行期写入的 cmdline：

```text
msm_drm.dsi_display0=qcom,mdss_dsi_samsung_amb655x_dsc_cmd:
androidboot.project_name=19805 androidboot.project_codename=kebab
androidboot.hw_version=15 androidboot.serialno=3ed3897b
```

同一棵树里 `techpack/display/msm/dsi/dsi_display.c` 对该参数的说明是：

```c
module_param_string(dsi_display0, dsi_display_primary, MAX_CMDLINE_PARAM_LEN, 0600);
MODULE_PARM_DESC(dsi_display0,
	"msm_drm.dsi_display0=<display node>:<configX> where <display node> is 'primary dsi display node name' ...");
```

即 `<display node>` 就是设备树节点名。ABL 已经把这个节点指给内核，这是
“面板是哪一块”最直接的证据。

注意：这份 DTB 带的是它那台机器的序列号 `3ed3897b`。若要 100% 确认本机，读一次
本机 boot-args 里的同一个字符串即可；若本机不是 amb655x，把脚本的 `--source`
指向对应 dtsi 重新生成，流程不变。

### 2.2 设备树节点属性

| 属性 | 值 |
| --- | --- |
| `qcom,mdss-dsi-panel-name` | `samsung amb655x fhd cmd mode dsc dsi panel` |
| `qcom,mdss-dsi-panel-manufacture` | `SAMSUNG` |
| `qcom,mdss-dsi-panel-version` | `DSC` |
| 类型 / bpp | `dsi_cmd_mode` / 24 |
| 分辨率 / 物理尺寸 | 1080×2400 / 70×151 mm |
| lane | 4（`lane-0..3-state`，`lane_map_0123`，`burst_mode`）|
| reset 序列 | 1/10 ms → 0/1 ms → 1/10 ms |
| TE | `te-check-enable`、`te-using-te-pin`、pin select 1、DCS 命令 1，GPIO 66 |
| reset GPIO / vddd GPIO | 75 / 24 |
| 背光 | `bl_ctrl_dcs`，1..2047 级，默认 360 |
| 供电 | `vddi` 1.8 V、`vddr` 1.3 V、`vci` 3.0 V（`dsi_panel_pwr_supply`）|
| HDR | 峰值 5,400,000 / 平均 2,000,000（驱动内部单位）|

### 2.3 其它候选为什么排除

`kebab.dtsi` / `kebab-oem.dtsi` 的触摸节点把多块面板列在 `panel = <...>` 里，
那是**候选集合**（`oem,project-id` 里还有 20809 等同板项目，共用一份 dtbo），不是
本机实际面板。逐个看参数：

| 节点 | 厂商 | 分辨率 | 刷新率 | 结论 |
| --- | --- | --- | --- | --- |
| `samsung_amb655x` | SAMSUNG | 1080×2400 | 60 / 120 | **采用**：ABL 选择，且是唯一 1080×2400 且 120 Hz 的面板 |
| `samsung_dd305` | SAMSUNG | 1080×2400 | 60 / 90 | 90 Hz，非 8T |
| `samsung_ana6705` | SAMSUNG | 1080×2400 | 60 / 90 | 90 Hz，非 8T |
| `samsung_ana6706` | SAMSUNG | 1080×2376 | 60 / 120 | 分辨率不符 |
| `samsung_oneplus_dsc` | SAMSUNG | 1440×3120 | 60 / 90 | 8 Pro 面板 |
| `boe_nt37800` | BOE | 1080×2400 | 60 / 90 | BOE，90 Hz，时钟 830 MHz |

OnePlus 8T 的规格是 1080×2400 120 Hz，与 amb655x 完全对上。

## 三、序列的来源与编码

### 3.1 源文件

| 项 | 值 |
| --- | --- |
| 仓库 | `OnePlusOSS/android_kernel_oneplus_sm8250` |
| 分支 / commit | `oneplus/SM8250_R_11.0`，`98719830b0de5584f5588bbe9f4c8304d97a64c6` |
| 路径 | `arch/arm64/boot/dts/vendor/qcom/dsi-panel-samsung_amb655x_fhd_dsc.dtsi` |
| 大小 / SHA-256 | 27,742 字节 / `7d7118f2…57fa98` |
| URL | <https://raw.githubusercontent.com/OnePlusOSS/android_kernel_oneplus_sm8250/98719830b0de5584f5588bbe9f4c8304d97a64c6/arch/arm64/boot/dts/vendor/qcom/dsi-panel-samsung_amb655x_fhd_dsc.dtsi> |

选这个仓库的理由：它是 8T（`KB05AA`）的**厂商内核源码**，`kebab-oem.dtsi` 与
`kebab-overlay.dts` 都在这棵树里。LineageOS 的 `android_kernel_oneplus_sm8250`
在 `techpack/display/msm/dsi/` 下只有通用文件，`arch/arm64/boot/dts/vendor/qcom/`
里取 `kebab.dtsi` 返回 404（其 kebab 设备树不在该路径），因此不作为序列来源。

### 3.2 为什么不是 `dsi_panel_kebab.c`

`techpack/display/msm/dsi/` 里只有通用的 `dsi_panel.c` / `dsi_panel.h` /
`dsi_display.c` 等；面板差异全部下沉到 dtsi。`dsi_panel.c` 的
`dsi_panel_create_cmd_packets()` 把 dtsi 里的字节流拆成 `dsi_cmd_desc`：

```c
cmd[i].msg.type = data[0];
cmd[i].last_command = (data[1] == 1);
cmd[i].msg.channel = data[2];
cmd[i].msg.flags |= (data[3] == 1 ? MIPI_DSI_MSG_REQ_ACK : 0);
cmd[i].post_wait_ms = cmd[i].msg.wait_ms = data[4];
cmd[i].msg.tx_len = ((data[5] << 8) | (data[6]));
```

### 3.3 字节流格式（每包里 7 字节头 + payload）

| 偏移 | 含义 |
| --- | --- |
| 0 | MIPI DSI 数据类型：`0x05` DCS short write、`0x15` DCS short write + 1 参数、`0x39` DCS long write、`0x29` generic long write、`0x03/0x13/0x23` generic short write、`0x06` DCS read |
| 1 | last 标志（1 → `MIPI_DSI_MSG_LASTCOMMAND`）|
| 2 | 虚拟通道 |
| 3 | ack 请求（1 → `MIPI_DSI_MSG_REQ_ACK`）|
| 4 | **该包之后**的延时，毫秒 |
| 5:6 | payload 长度，大端 16 位 |
| 7.. | payload；DCS 写时 `payload[0]` 是寄存器，其后是参数 |

这株序列实际只用到 `0x39`、`0x15`、`0x05`、`0x06` 四种类型，没有宏，
所有值都是字面字节；`--provenance` 会把每一步对应到源码行。

### 3.4 延时语义

`dsi_panel_create_cmd_packets()` 同时把该字节写进 `msg.wait_ms` 与
`post_wait_ms`；`dsi_panel_tx_cmd_set()` 在每条命令之后执行：

```c
if (cmds->post_wait_ms)
	usleep_range(cmds->post_wait_ms*1000, ((cmds->post_wait_ms*1000)+10));
```

所以表中第 4 步的 `0x0B` 就是 sleep-out 之后等 11 ms，第 30 步的 `0x3D` 就是
`FC A5 A5` 之后等 61 ms。`on-command` 的 state 是 `dsi_lp_mode`，即这 47 步都在
LP（低功耗）模式下发送。

## 四、提取结果

### 4.1 摘要

| 项 | timing@0（60 Hz）| timing@1（120 Hz）|
| --- | --- | --- |
| 源码起始行 | 103 | 442 |
| 步数 | 47 | 47 |
| payload 字节 | 264 | 264 |
| 原始流字节（含 7 字节头）| 593 | 593 |
| 延时步 | #4 11 ms、#30 61 ms | 同 |
| 表 CRC32 | `0xED2FEC73` | `0x34E4EFC1` |
| 原始流 SHA-256 | `2331287b…8d6f38` | `dc363733…3fa553` |

CRC32 的定义：按顺序把每一步的 `dtype`(1) + `wait_ms`(1) + 长度(2, BE) + payload
拼接后取 CRC32；JSON 里的 `checksum_definition` 也写了这句。

除 `on` 之外，每个 timing 还带 24 组命令（`off`、`switch`、`gamma_change`、
`seed`、`panel`、`register_read`、`level2_key_*`、AOD/HBM 等），全部在 JSON 里。

### 4.2 逐步核对（12 条，行号取自 3.1 的文件）

| # | 行 | 源码（去缩进）| 解码 |
| --- | --- | --- | --- |
| 0 | 105 | `39 01 00 00 00 00 03 F0 5A 5A` | long write `F0` ← `5A 5A`（厂商解锁）|
| 1 | 106–122 | `39 01 00 00 00 00 81 9E` + 后续 16 行 | long write `9E`，`0x81`=129 字节 DSC PPS，payload 跨 17 行 |
| 2 | 123 | `39 01 00 00 00 00 02 9D 01` | long write `9D` ← `01`（使能 DSC）|
| 3 | 124 | `39 01 00 00 00 00 03 F0 A5 A5` | long write `F0` ← `A5 A5`（解锁段结束）|
| 4 | 127 | `05 01 00 00 0B 00 01 11` | short write `11`（sleep out），**11 ms** |
| 5 | 130 | `39 01 00 00 00 00 03 F0 5A 5A` | 再次解锁 |
| 6 | 131 | `39 01 00 00 00 00 02 B0 04` | `B0` ← `04`（页选择）|
| 7 | 132 | `39 01 00 00 00 00 06 D5 24 9E 9E 00 20` | `D5` ← 5 参数（VLIN 限流）|
| 19 | 147 | `39 01 00 00 00 00 05 2A 00 00 04 37` | `2A` ← `0000 0437`，CASET 0..1079 |
| 20 | 148 | `39 01 00 00 00 00 05 2B 00 00 09 5F` | `2B` ← `0000 095F`，PASET 0..2399 |
| 30 | 160 | `39 01 00 00 3D 00 03 FC A5 A5` | `FC` ← `A5 A5`，**61 ms** |
| 41 | 174 / 513 | `39 01 00 00 00 00 02 60 00` / `60 10` | `60` ← `00`（60 Hz）/ `10`（120 Hz）|
| 46 | 181 | `05 01 00 00 00 00 01 29` | short write `29`（display on）|

两条 CASET/PASET 的值可以手算复核：`0x437 = 1079`、`0x95F = 2399`，与面板
1080×2400 的可见区一致。

### 4.3 两种刷新率的差异

逐包比较 60 Hz 与 120 Hz 的 `on-command`：**只有第 41 步不同**——`0x60` 的
参数 `0x00` 与 `0x10`。运行期 60↔120 切换用 `timing-switch-command`（3 步、
29 字节），不需要重发整张表。

### 4.4 独立复算与踩过的坑

独立复算（不经过脚本）在 shell 里做：用 awk 从 dtsi 里抓字节、拼成流、算 SHA-256：

```sh
awk '/qcom,mdss-dsi-on-command = \[/{c++; if(c==1){f=1; next}} f&&/^[ \t]*\];/{exit} f{gsub(/\/\*[^*]*\*\//,""); print}' \
    dsi-panel-samsung_amb655x_fhd_dsc.dtsi | tr -s ' \t' '\n' | grep -E '^[0-9A-Fa-f]{2}$' | tr -d '\n' \
  | xxd -r -p | sha256sum
```

两条命令的输出分别是 `2331287b…`（60 Hz）与 `dc363733…`（120 Hz），与脚本的
`stream_sha256` 相同。

保真度上真正踩到的坑：

1. **注释里的十六进制**。`/* FD Setting */` 里的 `FD` 会被“两个十六进制字符”
   的正则抓成一个字节，导致后面全部错位（第一版就栽在这里，偏移 335 处
   解出 `0xFD` 包）。脚本先把注释替换成等长空格再取字节。
2. **多行 payload**。DSC PPS 一行放不下，长度字段 `0x0081` 只在第 106 行，
   实际 payload 到第 122 行。脚本记录每一步的起始行与结束行。
3. **第二份 `display-timings`**。文件末尾的 `&dsi_samsung_amb655x_dsc_cmd`
   覆盖节点里又有一份 `timing@0/1`，只补 `phy-timings` 与 `display-topology`；
   脚本按 timing 序号合并，后出现的覆盖先出现的。
4. **延时字节不是长度**。`0x3D` 看起来像“61 字节”，其实是 61 ms；长度字段
   在 5:6 字节处。
5. **没有宏**。整张表都是字面字节，`0x81`、`0x0F` 这类长度不需要展开。

## 五、与设备 DTB 的交叉校验

`scripts/extract-panel-init.py --dtb kebab.dtb` 会走一条完全不同的路径：
直接解析 FDT 二进制，找到同一节点下 `timing@N` 的 `qcom,mdss-dsi-on-command`
属性，与解码结果逐字节比较。

```text
cross    : kebab.dtb (912906 bytes, sha256 1ec75ba9dbbf094c...)
           timing@0: fdt 593 bytes vs extracted 593 bytes -> same
           timing@1: fdt 593 bytes vs extracted 593 bytes -> same
           MATCH
```

DTB 里的 `on-command` 是编译后的字节数组（未压缩、未改写），两种模式各 593
字节；SHA-256 分别是 `2331287b…` 与 `dc363733…`，与源码提取结果一致。
也就是说：**厂商源码里的序列和设备固件里的序列是同一份**。

DTB 来源：<https://github.com/Project-Silicium/Mu-Silicium/blob/main/Resources/DTBs/kebab.dtb>
（912,906 字节，`sha256 1ec75ba9dbbf094c70d0f5ea9b4592980f62434da1972d95ba948452f4bf677f`）。

## 六、时序参数

来自 `timing@0` / `timing@1` 与 PHY 覆盖块：

| 参数 | timing@0 | timing@1 |
| --- | --- | --- |
| `panel-framerate` | 60 | 120 |
| `panel-clockrate` | 1,100,000,000 | 1,100,000,000 |
| `panel-width` × `panel-height` | 1080 × 2400 | 1080 × 2400 |
| `h-front-porch` / `h-back-porch` / `h-pulse-width` | 52 / 24 / 24 | 52 / 24 / 24 |
| `h-sync-skew` | 0 | 0 |
| `v-front-porch` / `v-back-porch` / `v-pulse-width` | 1208 / 8 / 4 | 4 / 8 / 4 |
| `mdp-transfer-time-us` | 12000 | 未写（沿用）|
| `panel-phy-timings` | `00 24 0A 0A 26 25 09 0A 06 02 04 00 1E 1A` | 同 |
| `display-topology` / 默认 | `<1 1 1>,<2 2 1>` / index 1 | 同 |
| DSC | 2 encoder、slice 540×30、2 slice/pkt、8 bpc、8 bpp、block prediction 开 | 同 |

其它：4 lane、`burst_mode`、24 bpp、命令模式、TE GPIO 66（pin select 1）、
reset GPIO 75、vddd GPIO 24、reset 1/10 ms → 0/1 ms → 1/10 ms、
`init-delay-us` 1000、背光 DCS 1..2047（默认 360）。

按未压缩时序算：1080+52+24+24=1180 像素/行、2400+1208+8+4=3620 行、60 Hz，
像素时钟约 256 MHz；DSC 打开后 DSI 侧按 540 像素/slice 送，链路时钟仍由
`panel-clockrate` 1.1 GHz 与 4 lane 决定。这里只做记录，不代替 PHY 计算。

## 七、初始化序列在做什么

按顺序分 9 段（行号对应 3.1 的文件，60 Hz 版）：

| 阶段 | 行 | 内容 |
| --- | --- | --- |
| 1. DSC 配置 | 105–124 | 解锁 `F0 5A 5A` → 写 129 字节 DSC PPS（`9E`）→ `9D 01` 使能 DSC → 上锁 |
| 2. Sleep out | 127 | `11`，之后等 11 ms |
| 3. 模拟参数 | 130–141 | VLIN 限流（`B0 04` → `D5 24 9E 9E 00 20`）、OSC 选择（`D1 22`、`D6 11`）|
| 4. TE | 143–145 | `35 00`，TE 打开（配合 TE GPIO 66）|
| 5. 窗口 | 147–148 | CASET `0..1079`、PASET `0..2399` |
| 6. FD / FFC | 150–160 | `D5 8D`、`B0 0A`、`D5 05`；`E4`/`E9` 写 FFC 系数，随后 `FC A5 A5` 并等 **61 ms** |
| 7. 调光 | 163–171 | `B7` 三组（`01`/`13`/`4C`）设置调光，`53 20` 写默认亮度 |
| 8. 刷新率 | 173–175 | `60 00`（60 Hz）或 `60 10`（120 Hz），随后上锁 |
| 9. ACL / 点屏 | 177–181 | `55 00` 关 ACL，`29` display on |

`29` 在表里没有延时；厂商驱动在 `dsi_panel_enable()` 里发完 `on-command`
之后还会写 dimming gamma、MCA 设置，最后才通过 DCS 开背光。

## 八、直驱还缺什么

### 8.1 寄存器窗口（来自设备 DTB）

| 模块 | 基址 | 说明 |
| --- | --- | --- |
| MDP/SDE | `0x0AE00000` | `qcom,sde-kms`，DSC 块在 `0x81000..0x81C00` |
| DSI0 控制器 | `0x0AE94000` | `qcom,dsi-ctrl-hw-v2.4`，`reg-names = dsi_ctrl, disp_cc_base` |
| DSI0 PHY | `0x0AE94400` | `qcom,dsi-phy-v4.1`（7 nm），`dyn_refresh_base` 在 `0x0AE94200` |
| DSI0 PLL | `0x0AE94900` | `qcom,mdss_dsi_pll_7nm_v4_1` |
| DISP_CC | `0x0AF03000` | 显示时钟控制器 |

### 8.2 时钟与供电

* DSI 控制器时钟：`byte_clk`、`byte_clk_rcg`、`byte_intf_clk`、`pixel_clk`、
  `pixel_clk_rcg`、`esc_clk`；PHY 侧还要 DSI PLL 的 byte/pixel 输出。
* MDP 侧：`gcc_iface`、`gcc_bus`、`gcc_nrt_bus`、`iface_clk`、`core_clk`、
  `vsync_clk`、`lut_clk`、`rot_clk`。
* 面板供电：`vddi` 1.8 V、`vddr` 1.3 V、`vci` 3.0 V（PMIC 稳压器，`dsi_panel_pwr_supply`）。
* PHY/控制器模拟供电：`vdda-0p9`（PHY）、`vdda-1p2`（控制器）、`refgen`。
* GDSC/mmcx：MDP 的 `mmcx-supply` 与 `mdss_core_gdsc`（DTB 里的 GDSC 节点）需要先上电，
  否则寄存器访问可能挂死。

### 8.3 DPU 与 DSC

* 面板是**命令模式**，且**只接受 DSC 码流**：第一步就写 PPS 并用 `9D 01`
  使能 DSC（2 encoder、540×30 slice、8 bpp）。host 端不做 DSC 就没有图像，
  不能拿 RGB888 直接往 DSI 里灌。
* 扫描输出需要 MDP/SDE：mixer、SSPP、DSC encoder、INTF 都要配；现有移植里
  没有 DPU 驱动，所以“直接点屏”不是只写 DSI 就够。
* 若先做最小可用路径，可以只用一个 mixer + 一个 DSC encoder + 命令模式
  刷一帧静态图，但时钟、电源、SMMU（或物理连续内存）仍要齐。

### 8.4 TE 与背光

* TE 走 GPIO 66（`qcom,platform-te-gpio`），面板每帧发 TE 后 host 才能更新
  CASET/PASET 或推新帧；没有 TE 就只能盲刷。
* 背光是 `bl_ctrl_dcs`：亮度通过 DCS `0x51` 写（1..2047），厂商驱动走
  `mipi_dsi_dcs_set_display_brightness()`（`bl_high2bit` 时是 Samsung 两字节变体），
  调光曲线由 `B7`/`53` 那组命令控制。内核直驱时要自己实现这条链路，不能再用
  引导器的固定亮度。

### 8.5 与现有帧缓冲路径的关系

当前 `0x9C000000`（1080×2400，stride 4320，32 bpp）是 ABL 配好的扫描缓冲，
面板保持扫描输出。只要**不动 DSI/PHY、不复位面板（GPIO 75）**，这条路径继续
有效，控制台和 `IOMobileFramebuffer` 都不受影响。

反过来，一旦内核去 reset 面板或重配 DSI，ABL 的显示配置就失效；在 DPU/DSI
驱动完整到能自己送帧之前，屏幕会黑，只能重启回引导器。因此直驱应当作为
独立开关（例如 `kebab_dsi=1`）叠加，而不是替换现有帧缓冲路径。

## 九、复现

```sh
# 抓取 + 提取 + 交叉校验 + 生成 JSON/头文件
python3 scripts/extract-panel-init.py --dtb /path/to/kebab.dtb

# 只校验已提交的产物
python3 scripts/extract-panel-init.py --dtb /path/to/kebab.dtb --check

# 逐步对照源码行
python3 scripts/extract-panel-init.py --provenance
```

字节成本：序列源文件 27,742 字节；为确认编码语义另取了
`dsi_panel.c`（187,166）、`dsi_panel.h`（12,096）、`dsi_defs.h`（24,537）、
`dsi_display.c`（314,225），合计 **565,766 字节**。交叉校验用的 DTB
（912,906 字节）是本地已有文件。搜索阶段还拉过一次约 24 MB 的 GitHub trees
API JSON（用于确认 LineageOS 仓库里没有 kebab 面板 C 文件），没有克隆任何
内核树。

## 十、参考

1. 厂商内核（面板 dtsi 与 `dsi_panel.c`）：
   <https://github.com/OnePlusOSS/android_kernel_oneplus_sm8250/tree/oneplus/SM8250_R_11.0>
2. 面板 dtsi（固定 commit）：
   <https://raw.githubusercontent.com/OnePlusOSS/android_kernel_oneplus_sm8250/98719830b0de5584f5588bbe9f4c8304d97a64c6/arch/arm64/boot/dts/vendor/qcom/dsi-panel-samsung_amb655x_fhd_dsc.dtsi>
3. kebab dtbo dump（ABL cmdline 与面板节点）：
   <https://github.com/Project-Silicium/Mu-Silicium/blob/main/Resources/DTBs/kebab.dtb>
4. 编码解析：`techpack/display/msm/dsi/dsi_panel.c` 的
   `dsi_panel_create_cmd_packets()` / `dsi_panel_tx_cmd_set()`
5. 面板选择的 module param：`techpack/display/msm/dsi/dsi_display.c` 的
   `module_param_string(dsi_display0, ...)`
