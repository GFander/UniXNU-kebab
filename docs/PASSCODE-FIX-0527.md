# 0527：修复 factory 无 PIN 状态的错误密码请求

日期：2026-11-02。实机已越过 Enter Passcode，进入原版设置引导的“选取 Wi-Fi 网络”页面。本次修复无 PIN keybag 的状态识别，不是完整的设置密码、口令验证或 Apple SEP 实现。

## 原因与改动

原版 MobileKeyBag 的普通查询使用 selector 17，扩展查询使用 selector 35；固定 20H392 的两个包装函数进入相同的原版状态传输及 DER 解码路径。SpringBoard 使用扩展查询，而此前 AppleKeyStore 用户客户端没有开放或实现 35，返回 `0xe00002c1`。查询失败后，原版 SpringBoard 回退为“有密码”。

0405 补丁仅修改 `iokit/Kernel/arm/kebab_aks_userclient_impl.h`：将 35 加入只读状态操作，并复用原有经过认证的 factory 状态读取函数。输出仍是原有的 `ss=4 / sls=3`，参数、容量、版本、策略、可用类和后端错误检查保持。没有重建 keybag、修改硬件 Root、改写 Data 或强置 SpringBoard 缓存，也没有伪造验证成功。

密钥操作、锁定/解锁操作及管理接口的权限不变。未知的 SEP、口令尝试次数、延迟或代际等字段保持缺省，不填写虚构值。

## 实机前后对照

| 项目 | 0526 | 0527 |
| --- | --- | --- |
| 已认证 factory bank | 无 PIN，`ss=4 / sls=3` | 原状态保留 |
| AKS DeviceLocked / FirstUnlock | `0 / 1` | `0 / 1` |
| selector 35 | 权限拒绝 | SpringBoard 实际调用返回成功 |
| 原版 SpringBoard PasscodeSet / FirstUnlock 缓存 | `1 / 1` | `0 / 1` |
| 实际界面 | 字母数字 Enter Passcode | 设置引导：选取 Wi-Fi 网络 |

修复后日志记录了 SpringBoard PID 27 的实际 selector 35 成功调用。晚期只读快照在 SpringBoard PID 391 中观察到 `PasscodeSet=0 / FirstUnlock=1`，其原版初始 check-in 完成状态为 1。快照核对固定供体 UUID、代码指纹、ISA、字段偏移和两次读取，未注入用户态 getter 或暂停线程；属于 best-effort 观察，不代表所有并发状态均已证明。

用户确认能够进入引导界面；随后实际 GOP 截图经查看确认是中文“选取 Wi-Fi 网络”，含“连接 Mac 或 PC”选项。截图窗口前显示状态为 on，没有在冻结元数据前观察到 display off。

## 验证

- 实际生产状态函数和完整用户客户端分派：1,163 项主机检查、5 个可编译且因运行行为错误被拒绝的变异版本；覆盖权限、错误参数、直接/OOL 缓冲区、短写、未准备好、错误 bank 策略及后端失败。IOKit 与密钥后端为明确列出的模型，不冒称实机安全证明。
- 固定原版 ARM64 DER 解码器和相关原版解析代码重新执行，正确解析现有 `ss=4 / sls=3` 状态；原版包装函数的 selector 与共同目标另有独立核对。没有更改历史收据。
- 相关原生编译单元及完整构建通过；384 个共同补丁加 4 个 Mac 主机补丁、5,287 个源码文件与实际编译输入逐项一致。
- 只刷已确认设备的 `boot_b`，Sending、Writing、Reboot 均 OKAY。
- 原硬件密钥、既有 bank/systembag、加密 Data、MACF/ACL/Sandbox、defaults、RTC/NVRAM 和原版 watchdog 基础回归通过，窗口 304.414 秒。日常 development-fast 保持，没有额外全盘内容扫描。
- 当前 GOP：1080×2400、10,368,000 字节，独立复核保存的传输帧 CRC、连续内容、完整图像 CRC、日志分流及 PNG 全通道。重试 0、解码错误 0、传输期间日志丢弃计数增量 0；不冒称整次开机日志从未丢失。

0527 内核 SHA-256：`51c39c759eaad13ef9af6d039f99d46a1e34c9db1a3429edb3c9ea8352884c31`，19,048,496 字节。

当前 GOP SHA-256：`a6ea636a6d929d8e1f71c11f3e244e6c823a520f18a7a4902976d0712c4fbf9e`。

## 仍未完成

Wi-Fi 联网、设备激活、设置/验证新密码及解锁后桌面仍未验收。红色 UIKit 状态条回退仍可见；旧 0523 约 2,012 秒出现的 SpringBoard watchdog 超时没有被本轮 304 秒窗口排除。本轮不关闭 watchdog 或延长超时。

当前手机运行 0527，串口和构建器均已释放。原始截图、传输记录、供体二进制及任何私有 Data 保留在工作台，不进入公开提交。源补丁、可重跑主机测试及经过审阅的验收元数据在 Ubuntu 独立任务分支中整理，尚未提交或推送。
