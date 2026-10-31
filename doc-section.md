
## 六十一、触摸自动上电与芯片识别

> 目标：让触摸驱动自己在 bring-up 时点亮它自己的两路电源，并在复位之后真正读出
> 芯片的身份，而不是继续假设。本节记录：启动参数、顺序与顺序的理由、上电前后的
> 真机输出、芯片回的身份字节，以及它到底在 application 还是 bootloader 模式。

### 1. 启动参数与门限

| 参数 | 默认 | 作用 |
| --- | --- | --- |
| `kebab_touch_power` | `0` | 让 `kebab_touch_start()` 在**任何 I2C 传输之前**给 `ldoa13` 与 `ldoc1` 上电 |

* 它**不是**替代 `kebab_vrm_write`，而是在它之上再加一道门：`kebab_vrm_write=0`
  时 `kebab_spmi_vrm_boot_power()` 直接打印拒绝并返回失败，没有人值守的启动
  永远不会给任何一颗 LDO 发命令。
* 两颗 LDO 仍然各只有一次机会（`vrm.ldoa13_done` / `vrm.ldoc1_done`）。开机时
  已经上过电的那一颗，控制台再 `VRMON` 会被拒；反过来，开机时读到 `EN=0x80`
  而跳过的轨道**不**占用这次机会（`done` 保持 `false`），只是把 `ldoa13_ok`
  置真，好让第二步 `ldoc1` 的前置条件成立。
* 传输、复位、识别仍然要 `kebab_touch_boot=1`；只给 `kebab_touch_power=1` 时
  驱动只上电、然后停在"hardware untouched"，方便先用 `PWR?` 验证电源。

### 2. 顺序，以及为什么是这个顺序

```
kebab_touch_start()
  ├─ 3 s 控制台窗口（原有，给主机打开 CDC-ACM 的时间）
  ├─ kebab_spmi_vrm_boot_power()          ← 本补丁新增
  │    ├─ 用 PWR? 的同一条 SPMI observer 路径读 EN
  │    ├─ EN == 0x80 → 跳过这一颗
  │    ├─ 编码并发送 ldoa13 的 ENABLE，等 TCS 完成，回读 EN
  │    ├─ 再对 ldoc1 做同样的一步
  │    └─ 任一步失败 → 返回 -1，通道按模块既有规则关闭
  ├─ kt_sleep_ms(15)                       ← 第二颗之后 10–20 ms
  ├─ TOUCHCLK（使能 SE13 的 S5 分支时钟）   ← 本次实测补上的必要一步
  ├─ 引脚 mux、复位脉冲（原有）
  ├─ kt_sleep_ms(20)                       ← 复位释放之后再等一次
  └─ se_init → identify → boot info
```

为什么必须先电后复位：两颗 LDO 的 ENABLE 是 RPMh 的 VREG 寄存器，每次重启都回到
`0x00`；而没有供电的 SE13 目标**不会 NACK**，它让传输以 `GENI_STATUS` 的
`state 0x21` + `ABORT` 结束。所以上电必须在第一个字节之前，而不是在失败之后。

为什么时钟要单独补：0116 当时就量到"第一次写 GENI 寄存器之后控制台不再回来"，
那一次和这一次都停在同一行 `se_init: force default, output ctrl`。控制台上
`TOUCHCLK` → `TOUCHSE` 一直是能跑通的，差别就是时钟。把这一步放进启动路径之后，
`se_init` 一次通过，识别命令才真正发到 `0x4b`。这一条不是"顺手加的"：没有它，
自动上电只是把电加上去，永远走不到芯片。

### 3. 真机输出

镜像：0132 + `kebab_touch=1 kebab_touch_boot=1 kebab_touch_power=1
kebab_vrm_write=1 kebab_pon_write=1`，`boot_b`。

**上电之前**（`PWR?`，与 0130 记录一致）：两颗都是 `enable 0x00`；同一条一字节
写在 `0x4b` 上以 `state 0x21` 结束——这是"上电前"的基线。

**上电（本次实测，原文）**：

```text
KEBAB TOUCH: console window over, starting MMIO
KEBAB VRM: === boot power: touch rails, one ENABLE each ===
KEBAB VRM: encode ldoa13 ENABLE -> OK msgid 0x00010108 addr 0x00040c04 data 0x00000001
KEBAB VRM: whitelist: WRITE_ACK to the ENABLE register of ldoa13 only, slot 0x1
KEBAB VRM: send ldoa13 -> OK (0)
KEBAB VRM: phase COMPLETE, CMD_STATUS 0x00010101 (ISSUED 1, COMPL 1)
KEBAB VRM: boot power: ldoa13 COMPL, phase COMPLETE, CMD_STATUS 0x00010101 (ISSUED 1, COMPL 1), EN 0x00 -> 0x80
KEBAB VRM: encode ldoc1 ENABLE -> OK msgid 0x00010108 addr 0x00042f04 data 0x00000001
KEBAB VRM: whitelist: WRITE_ACK to the ENABLE register of ldoc1 only, slot 0x4
KEBAB VRM: send ldoc1 -> OK (0)
KEBAB VRM: phase COMPLETE, CMD_STATUS 0x00010101 (ISSUED 1, COMPL 1)
KEBAB VRM: boot power: ldoc1 COMPL, phase COMPLETE, CMD_STATUS 0x00010101 (ISSUED 1, COMPL 1), EN 0x00 -> 0x80
KEBAB VRM: boot power: ldoa13 and ldoc1 both read 0x80
KEBAB TOUCH: both rails up, waited 15 ms before the reset
```

**时钟、复位、SE 初始化**：

```text
KEBAB TOUCH: TOUCHCLK: 0x52008 0x063c83c0 -> 0x0e3c83c0
KEBAB TOUCH: TOUCHCLK: 0x52008 now 0x0e3c83c0 (s5 1), halt 0x00004220
KEBAB TOUCH: reset pulse (mode 1), ctl 0x00000200 io 0x00000003
KEBAB TOUCH: reset pulse done, waited 20 ms
KEBAB TOUCH: se_init: clear IRQs
KEBAB TOUCH: se_init: force default, output ctrl
KEBAB TOUCH: se_init: packing + byte granularity
KEBAB TOUCH: se_init: clock select + SCL counters
KEBAB TOUCH: TCM identify begin
```

上电之前这一段是停不住的：写 `GENI_OUTPUT_CTRL` 之后控制台就没了。现在它继续走
到识别。

**身份读取（原文）**：

```text
KEBAB TOUCH: TCM command 0x02, 0 payload byte(s)
KEBAB TOUCH: command 0x02: trailing byte 0x00, vendor padding is 0x5a
KEBAB TOUCH: identify: message 0x10, 24 payload byte(s)
KEBAB TOUCH: identify payload[0] a5 03 01 01 73 33 39 30 38 2d 31 35 2e 30 2e 30
KEBAB TOUCH: identify payload[16] 00 00 00 00 b9 ba 3c 00
KEBAB TOUCH: identify: part "s3908-15.0.0...." at payload offset 4
KEBAB TOUCH: identify: version 0x01 mode 0x01 (application), the two bytes in front of the part number
KEBAB TOUCH: identify: 2 byte(s) in front of those are left uninterpreted
KEBAB TOUCH: identify prefix[0] a5 03
KEBAB TOUCH: identify: build 0x003cbab9
KEBAB TOUCH: boot info: message 0x0e, 0 payload byte(s)
KEBAB TOUCH: boot info: code 0x0e (not implemented), 0 payload byte(s): this firmware does not report boot status
KEBAB TOUCH: identity: APPLICATION MODE (mode 0x01), the touch firmware is running
```

### 4. 身份字节与判定

identify 的 24 字节 payload：

| 偏移 | 字节 | 含义 |
| --- | --- | --- |
| 0–1 | `a5 03` | 固件在结构前多带的两个字节，**不解释**，原样打印 |
| 2 | `01` | version |
| 3 | `01` | **mode = MODE_APPLICATION** |
| 4–19 | `73 33 39 30 38 2d 31 35 2e 30 2e 30 00 00 00 00` | `"s3908-15.0.0"`，NUL 补齐 |
| 20–23 | `b9 ba 3c 00` | build（packrat）`0x003cbab9` |

判定方法刻意不是"按下标取"：part number 是这份 payload 里唯一位置自明的字段
（可打印 ASCII），而厂商 `syna_tcm_identification` 里 version/mode 正好在它前面
两个字节。所以先找 part 串，再取它前面两字节，并把更前面的字节原样打印成
"未解释前缀"。这样固件多带两个字节时，part 名不会被整体错位，模式也不会被读成
`0xa5`。

**结论：application mode。** 旁证是 `GET_BOOT_INFO`（0x10）回 `0x0e`
`not implemented`——boot info 是 bootloader 才会答的命令，application 固件不实现
它。也就是说这颗芯片**不需要下载固件**，它已经在跑触摸固件（15.0.0）。

反过来说，如果 mode 不是 `0x01`（`0x02` host download、`0x0b`/`0x0c`
bootloader、`0x0e` production test），驱动会打印
`touch unavailable: ... needs a firmware download, which this driver does not
implement` 并 `return -1`：不建线程、不建设备节点、不假装已经能出数据。
0120 里那一步"发 `CMD_RUN_APPLICATION_FIRMWARE` 再重新 identify"已经删掉，因为
那是一个本驱动从未验证过的模式切换。

### 5. 失败路径

* 读 EN 失败、编码/白名单拒绝、TCS 没有 COMPLETE、回读不是 `0x80`：每一种都
  打印是哪一颗、哪一步、`phase` 与 `CMD_STATUS`，然后
  `touch unavailable: a rail did not come up, so no transfer is attempted; the
  boot continues without touch`，`kebab_touch_start()` 返回 `-1`。
* `kebab_touch_start()` 由 `void` 改为 `int`，`bsd_init()` 仍然忽略返回值：
  这条路径不 panic、不重试、不循环。失败的命令本身已经把通道关掉，重试正是
  那道一次性门限要防的事。
* 识别阶段失败同样只影响触摸：没有 TCM 应答，或 mode 不是 application，都
  只打印 verdict 并返回，启动继续。

### 6. 边界

* **没有加 HID**：事件路径一行没动，`/dev/kebab_touch` 的记录格式与队列不变。
* 这颗固件的报文尾部不是厂商的 `0x5a` padding，`GET_APP_INFO`（0x20）与
  `SET_TOUCH_REPORT_CONFIG`（0x26）会因此错位：本次输出里
  `command 0x20: payload 23040 too large` 与 `report config answered 0x0d` 就是
  这个原因。识别与模式判定不受影响。既然没有解析出报文的布局，驱动就不启动
  轮询线程：这一次先看到的是相反的情况——线程起来了，每 200 ms 打一条
  `report padding 0xa5, not 0x5a`，把这唯一的可读通道刷掉——所以最终版本把
  "有没有解码器"作为起线程的条件。事件路径本身保持原样。
* `launchd` 在启动末尾仍然会 panic（`unexpected SIGKILL of launchd (CS_KILLED)`），
  那是 AMFI/签名那条线的问题，发生在触摸驱动返回之后，与本补丁无关。
