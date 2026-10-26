# EP0 探针上机测试步骤（P0-1）

目标只有一个：读到 `KEBAB EP0 PROBE:` 的最后一个字和 32 项历史，判断 EP0 状态机
停在哪一步。探针在 `0091-kebab-usb-cdc-acm.patch` 里，**本测试不改内核**；主机侧
解码器是 `scripts/decode-ep0-probe.py`，先在本机跑通再上机：

```sh
python3 scripts/decode-ep0-probe.py --selftest
```

## 1. 刷什么

内核必须来自 `ios16` 分支且包含补丁 0091（写作时 tip 为 0102）。

构建产物在 `build/xnu.obj/DEVELOPMENT_ARM64_KEBAB/kernel.development.kebab`
（`~/kebab-build/build-local.sh`）。先确认二进制里有格式串：

```sh
strings build/xnu.obj/DEVELOPMENT_ARM64_KEBAB/kernel.development.kebab \
    | grep -c 'KEBAB EP0 PROBE:'
# 正常是 2（头行一条、历史行一条），至少要有 1
```

启动参数在**载荷**的 ADT 里，不在内核文件里。只改参数时不需要重编内核，但必须
重新生成 `payload.img`；`pack-payload.py` 只替换 FAT32 里的内核文件，会保留旧参数。

1. 在 `board.toml` 的 `[payload].kernel_args` 末尾加一项：

   ```toml
   "kebabpanic=1",
   ```

   原有的 `debug=0x14e`、`serial=3`、`kebabusb=1`、`cpus=1` 等全部保留。
   **不要**加 `kebabkdp=1`：KDP 和这条测试共用同一条 bulk 流；也不要加 `0x200`。
2. 按常规流程重新生成 `payload.img`，大小必须正好是 67,108,864 字节。
3. 拼进 boot 镜像并刷 `boot_b`：

   ```sh
   IMG=XNU-ep0-$(date +%m%d).img
   python3 scripts/repack-firmware.py --template signed.img \
       --payload payload.img --out "$IMG"
   fastboot devices
   fastboot flash boot_b "$IMG"
   fastboot reboot
   ```

4. 回 Android：`fastboot flash boot_a <lineage boot.img>`（见 README）；测试期间
   不要动 `boot_a`。

## 2. 抓日志

固件里的 panic 钩子只在**同一次 bulk OUT 传输**里匹配到完整的 `PANIC!`（6 字节）
才触发。用终端逐键敲是 6 次 1 字节传输，**不会触发**。必须用一次
`write(b"PANIC!")`，附录脚本就是这么做的。

### Windows

1. 设备管理器确认端口名，例如 `COM7`；重新枚举后编号可能变。
2. 装一次 pyserial，然后运行附录脚本：

   ```powershell
   python -m pip install pyserial
   python ep0_capture.py COM7 ep0-panic.log
   ```

3. 看到 `KEBAB USB: configured` 后按回车，脚本发送 `PANIC!`。panic 文本之后应出现
   9 行 `KEBAB EP0 PROBE:`（1 行头 + 8 行历史）。

### macOS

1. 确认端口名，然后运行同一个脚本：

   ```sh
   ls /dev/tty.usbmodem*
   python3 ep0_capture.py /dev/tty.usbmodemXXXX ep0-panic.log
   ```

2. 重新枚举后名字通常会变；脚本重开失败时会打印异常，记下新名字，用
   `--no-panic` 只读抓一次。
3. 也可以 `cat /dev/tty.usbmodemXXXX | tee ep0.log` 抓日志，但发送 panic 仍需单独
   一次 6 字节写：`printf 'PANIC!' > /dev/tty.usbmodemXXXX`（不要手敲）。

两个主机都一样：日志按原始字节保存，不要用会改写行尾的终端日志功能；解码器能
容忍 CRLF 和内核噪声。

## 3. 操作顺序

1. **开端口**：设备启动到 `KEBAB USB: configured` 后打开串口开始记录。错过基线
   那一行没关系，panic 报告里带完整历史。
2. **看基线**：日志里应出现一行 `KEBAB EP0 PROBE: progress 0x... seq ...`。只有
   一行、没有历史，这是正常的基线。
3. **强制 panic**：发一次 `PANIC!`。期望看到
   `panic(cpu 0 caller ...): kebab usb console test`，随后约 8.9 KB 的 panic
   报告，最后是 9 行探针报告。等到 60 秒没有新输出。
4. **解码**：

   ```sh
   python3 scripts/decode-ep0-probe.py ep0-panic.log   # Windows: python ...
   ```

   先看 `Verdict` 里的 `most likely failure`、`capture` 和
   `after the last probe line`，再看历史表。
5. **关端口**：停止抓取，关闭串口，等 3 秒。
6. **重开端口**：同一个端口名再打开，观察 30～60 秒。三种结果：
   - 打不开 / 端口消失：记录错误；这是“panic 后主机无法重新配置端口”的直接证据。
   - 打得开但没有新文本：主机重新打开了设备，设备没有收到新的 SETUP。
   - 出现 `KEBAB USB: bus reset, rebuilding EP0`、`connected`、`setup ...`、
     `configured`：主机重新枚举成功；把这段文本留在日志里。
7. **收尾**：长按电源重启；需要回 Android 时刷 `boot_a`。重连段写在
   `<日志名>.reopen`，脚本结束时打印这 30 秒里各类行的条数。

> 探针报告是 panic 时刻的快照，每次 panic 只发一次（`probe_sent` 是局部变量），
> 关/开端口**不会**再生成一份报告。重连是否成功只能看报告之后的普通日志行；解码器
> 的 `after the last probe line` 会把 `bus reset` / `connected` / `configured` /
> `setup` / `disconnect` 的条数列出来。

## 4. 判读

解码器的 `most likely failure` 直接给下面某一项：

| 失败码 | 最后阶段 | 含义 | 下一步 |
| --- | --- | --- | --- |
| `none` | 7 SETUP re-armed | 最后一次控制传输完整结束，EP0 空闲 | panic 前 EP0 是好的；重连失败看报告之后的尾部日志与主机侧 |
| `no data` | 头行 | 只有基线，没有历史 | 触发一次 panic 再抓 |
| `no transitions` | 历史全 0 | 有历史行但 32 个槽都是 0 | 重抓；确认不是从全零窗口开始 |
| `setup stalled` | 1 SETUP received | SETUP 后没有提交数据/状态，通常是 unsupported 请求被 stall | 对照 `unsupported request` 与 `last setup line`；补请求或确认 stall 是预期的 |
| `status readiness missing` | 1 | 无数据请求，等 `XferNotReady(status=2)` 没等到 | 查控制器事件、`ud.state == EP0_WAIT_STATUS`、type==3/status==2 解码 |
| `data submit rejected` | 2 | 数据阶段 TRB 被 DEPCMD 拒绝 | 查 `ud_start()` 返回值、TRB 地址高低位、端点/资源、复位后是否重建 |
| `data stage incomplete` | 2 | 数据阶段提交了但没完成 | `SET_LINE_CODING` 查 64 字节 OUT 容量与主机是否真发了 7 字节；IN 查主机是否在读 |
| `data submit error` | 2 | 数据阶段提交返回 0/2 以外的状态 | 查 DEPCMD 状态与端点状态；当前代码还不会写这些码 |
| `status not submitted` | 3 | 数据完成，状态阶段从未提交 | 查 `ud_status_stage()`、`XferNotReady(status=2)`、`STATUS3` |
| `status wait out of order` | 4 | 状态就绪事件来时状态机不在等它 | 查 completion 丢失、状态被重置、事件属于上一笔传输 |
| `status submit rejected` | 5 | 状态 TRB 被 DEPCMD 拒绝 | 查 `STATUS2`/`STATUS3` 与方向 |
| `status stage incomplete` | 5 | 状态阶段提交了但没完成 | 查 TRB 类型/方向、completion 是否被别处吃掉 |
| `setup not rearmed` | 6 | 状态完成但没有重新挂 SETUP | 查 `ud_start_setup()` 及其返回值 |
| `bus reset without rebuild` | 16 | 总线复位后没有 phase 17 | 查 `ud_ep0_rebuild()` 的 DEPCMD 序列；看 `EP0 rebuild failed` |
| `rebuilt, no setup` | 17 | 重建完成，后续没有 SETUP | 主机没重新枚举；按第 3 节第 6 步重开/拔插，换主机或直连再试 |
| `unknown phase` | 其他 | 解码器不认识这个 phase | 从 0091 的 note 点补表 |

其他输出这样读：

- `capture: complete` 表示 32 个槽都收到；`truncated` / `partial` 表示日志被截断。
  `last word` 仍可判最后一步，但历史不完整，重抓一次。
- `sequence counter: wrapped ...`：8 位序号回绕，环仍然有效，不是失败。
- `status stage submitted: no in the last transfer; yes in the window`：最后一笔
  传输没走到状态阶段，但窗口里更早的传输走到过；以 `last transfer` 为准。
- `anomalies`：`history ends before the header word` 是截断；`history does not
  start at the expected ring slot` 是丢了开头。

## 5. 与 EP0-PROBE.md 的差异（以代码为准）

1. **序号只有 8 位**：头行和字里都是 `seq & 0xff`，绝对迁移次数读不出来；历史行
   的 `[NN]` 是完整计数器 mod 32 的环下标。解码器只能判断 32 项窗口内是否回绕。
2. **历史是每行 4 个字、固定 8 行**：不是一行一个迁移；未写过的槽是 0，解码器会
   丢掉。真实格式串是 `KEBAB EP0 PROBE: [%02u]0x%08x ...`。
3. **当前代码只会写 status 0/2/5**：`2` 是提交失败，`5` 是 `XferNotReady(status=2)`
   顺序错误；规范里的 1/3/4/16+n 还没有记录点，解码器仍能显示它们。
4. **基线只有头行**：`ud_configure()` 只打一行 `progress ...`，没有历史；只有
   panic 报告带 32 项。
5. **重建失败不写 phase 17**：`ud_ep0_rebuild()` 在第一个 DEPCMD 失败时提前
   `return`，所以最后停在 16 可能表示重建失败；看 `KEBAB USB: EP0 rebuild failed`。
6. **panic 报告是一次性的**：`kebab_usb_panic_spin()` 里的 `probe_sent` 是局部
   变量，每次 panic 只发一次；关/开端口不会重发。重连看报告之后的普通日志。
7. **phase 16/17 的 request 是 0**：最后字会显示 `non-request event`；解码器的
   `Sequence` 用最后一次 SETUP 的请求来解释，不要被最后字的 request 误导。
8. **`PANIC!` 必须一次写 6 字节**：`ud_bulk_out_done()` 只在单次传输的
   `received >= 6` 时匹配；逐键输入无效。

## 6. 每次记录

| 日期 | 主机 | 构建（commit） | 启动参数 | 日志 | 失败码 | 重连现象 | 结论/下一步 |
| --- | --- | --- | --- | --- | --- | --- | --- |
|  |  |  |  |  |  |  |  |

## 附录：ep0_capture.py

脚本独占打开端口并原样记录；按回车后一次写入 `PANIC!`，等 60 秒；再关闭、重开
同一端口抓 30 秒，并打印 `bus reset` / `connected` / `setup` / `configured` 的
条数。`--no-panic` 只读，用于重枚举后端口改名的情况。

```python
#!/usr/bin/env python3
# ep0_capture.py PORT LOG [--no-panic]
import sys, threading, time
import serial

port, log_path = sys.argv[1], sys.argv[2]
send_panic = "--no-panic" not in sys.argv[3:]
stop = threading.Event()
seen = {"reset": 0, "connected": 0, "setup": 0, "configured": 0}

def pump(handle, out):
    while not stop.is_set():
        try:
            data = handle.read(4096)
        except serial.SerialException:
            return
        if not data:
            continue
        if stop.is_set():
            return
        try:
            out.write(data); out.flush()
            sys.stdout.buffer.write(data); sys.stdout.buffer.flush()
        except (ValueError, OSError):
            return
        seen["reset"] += data.count(b"KEBAB USB: bus reset")
        seen["connected"] += data.count(b"KEBAB USB: connected")
        seen["setup"] += data.count(b"KEBAB USB: setup ")
        seen["configured"] += data.count(b"KEBAB USB: configured")

def open_port():
    return serial.Serial(port, 115200, timeout=0.2)

handle = open_port()
log = open(log_path, "wb")
threading.Thread(target=pump, args=(handle, log), daemon=True).start()
print("opened %s -> %s" % (port, log_path))

if not send_panic:
    print("read-only; press Enter to stop")
    input()
    stop.set(); handle.close(); log.close()
    raise SystemExit(0)

input("press Enter when 'KEBAB USB: configured' is visible ...")
handle.write(b"PANIC!"); handle.flush()       # one 6-byte bulk OUT transfer
print("PANIC! sent; waiting 60 s for the panic and probe reports")
time.sleep(60)
input("press Enter to close the port ...")
stop.set(); handle.close(); log.close()
print("closed; reopening in 3 s")
time.sleep(3)

try:
    handle = open_port()
except Exception as exc:
    print("reopen failed: %s" % exc)
    raise SystemExit(0)

before = dict(seen)
stop.clear()
log = open(log_path + ".reopen", "wb")
threading.Thread(target=pump, args=(handle, log), daemon=True).start()
print("reopened; capturing 30 s")
time.sleep(30)
stop.set(); handle.close(); log.close()
print("after reopen: bus reset %d, connected %d, setup %d, configured %d" % (
    seen["reset"] - before["reset"], seen["connected"] - before["connected"],
    seen["setup"] - before["setup"], seen["configured"] - before["configured"]))
print("done")
```

## 已知的解析缺口

合并后的驱动会打印两种 EP0 行：

```
KEBAB EP0 PROBE: progress 0x... seq ... req ... phase ... status ...
KEBAB EP0 STATE: resets=.. disconnects=.. fences=.. events=.. acked=..
                 discarded=.. cursor=.. takeover=.. tx_complete=..
                 line=xxxxxxxxxxxxxx controls=..
```

`scripts/decode-ep0-probe.py` 目前只解析第一种。第二种是重写后加入的计数器快照，
手工读即可，含义：

| 字段 | 判读 |
| --- | --- |
| `resets` | 主机重新打开端口会触发总线复位；重连后应递增 |
| `disconnects` | 断连事件计数 |
| `fences` | 用资源索引取消传输并等确认的次数；异常增长说明有传输没收尾 |
| `events` / `acked` | 控制器写入的事件数与已应答字节；长期不相等说明事件环在泄漏 |
| `discarded` | 事件数超出范围被丢弃的次数；应为 0 |
| `cursor` | 软件事件游标位置 |
| `takeover` | panic 接管是否发生过 |
| `tx_complete` | bulk IN 完成次数 |
| `line` | 保存的 line coding，七个字节 |
| `controls` | 控制线状态（DTR/RTS）|

重连测试的判据：`resets` 增加、`discarded` 保持 0、`events/acked` 同步增长、
`line` 与主机设置的终端参数一致。
