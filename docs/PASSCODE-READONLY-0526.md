# 0526：Enter Passcode 只读追踪

日期：2026-11-02。范围：原版 20H392 的 SpringBoard → SpringBoardFoundation / MobileKeyBag → AppleKeyStore。没有修改密码验证结果、现有 keybag、保护等级、硬件 Root 或加密 Data。

## 当前结论

已发现可复现的状态接口缺口，不能把此密码界面理解为已经存在用户设置的密码：

1. 实机普通状态查询成功。后端认证的 factory no-PIN bank 对应 `ss=4 / sls=3`；selector 7 的实际返回值为 `0x4`，即 `DeviceLocked=0 / FirstUnlock=1`。
2. 原版 `SBFMobileKeyBag` 在初始化和状态更新时请求 `ExtendedDeviceLockState`，MobileKeyBag 因此使用 **selector 35（0x23）**。
3. 实机 selector 35 反复返回 `0xe00002c1`，即 `kIOReturnNotPrivileged`。当前用户客户端白名单没有 35，分派实现也没有 35；只放宽白名单仍会落到 Unsupported，不能作为完整修复。
4. 原版 `MKBGetDeviceLockStateInfo` 的失败路径不能提供正常状态字典。原版 `SBFMobileKeyBagState.lockState` 对缺失值取得 0；`_queue_setHasPasscodeIfNecessary:` 将 `lockState != 3` 记为有密码。这是原版失败回退逻辑，不是本轮注入的结果。
5. 0526 对已初始化 SpringBoard 单例的只读双读，实际得到 **`PasscodeSet=1 / FirstUnlock=1`**。因此 factory no-PIN 后端状态与 SpringBoard 认知确实不一致。

当前证据支持“扩展状态查询被拒绝，导致上层按有密码处理”这一定位。尚未修改接口并做前后对照，不能宣称密码问题已修复，或证明所有密码请求均来自同一个原因。selector 35 的现有日志没有调用者 PID；SpringBoard 请求链由固定原版代码和其实际缓存结果结合定位，不能冒称已经记录到每一次完整跨层调用。

## UI 类型

0523 的封存实机 GOP 及用户照片显示字母数字密码框和 QWERTY 键盘，不是固定六位数字键盘。没有读取输入内容、尝试密码或伪造验证成功。0526 已通过实际上滑及后续当前 GOP 截图，确认同样是字母数字 QWERTY 界面，两侧键位完整、输入框为空。枚举类型及具体 UI 控制器尚未读回，不能用截图外观冒充原版类型枚举。

用户提供的论坛参考图是“重新启动后需要密码”的 iPad 界面，只作为 first-unlock / keystore 的排查线索；没有帖子、版本或修复代码可作直接因果证明。

## 已完成的验证及边界

- 0525：原生构建、382 个共同补丁加 4 个主机补丁、5,287 个源文件逐项一致；基础回归 304.436 秒通过。
- 0526：只修正只读代码指纹读取的 4 字节对齐问题，保留密码接口原返回值；383 加 4 个补丁、5,287 个源文件逐项一致，基础回归 304.462 秒通过。
- 0526 内核 SHA-256：`979140703a7f53838b66ccccdd2305c148dbb9f6db8f0481bfc0ebcad4b59af7`。
- 实机 SpringBoard PID 27，在启动约 123.305 秒读取到原版缓存 `PasscodeSet=1 / FirstUnlock=1`。单例指纹、ISA、字段偏移和两次读取均通过；未暂停线程，仍属于 best-effort 快照。
- 原硬件密钥、既有 bank/systembag、加密 Data、MACF / ACL / Sandbox、时钟与原 watchdog 均保留。基础回归不等于长时间稳定性验收。
- 旧 0523 在运行约 2,012 秒时发生 SpringBoard 180 秒未报到的 userspace watchdog panic。其原因尚未证明与密码请求相同，没有关闭 watchdog 或延长超时。

## 后续修复边界

应先核对 selector 35 的原版只读参数、DER 解码和访问权限，再按现有已认证 bank 返回真实状态；仅包含后端能够证明的字段。未知的 SEP、密码、延迟或失败计数不能填成成功值。

修复后应复读同一组原版缓存和状态，确认 no-PIN 被识别为无密码，同时验证加密 Data 的既有读取、锁定策略、拒绝路径和 watchdog。不得用清空 Data、重建 bank、强置 `PasscodeSet=0`、注入解锁通知或自动验证密码代替接口修复。本轮按用户要求停留在只读定位，没有实施这些认证语义变化。

私有证据位于 `boot123-20261029/ca-surface-0526/`、原版代码分析位于 `boot123-20261029/identity-0511/passcode-*.private.*`。完整用户截图、原版二进制、keyblob 和私有 Data 不进入公开提交。
