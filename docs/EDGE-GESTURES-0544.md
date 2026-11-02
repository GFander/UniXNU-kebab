# 0544 真实触点的边缘起始标记

更新：2026-11-02T17:01:57+08:00。0544已刷入boot_b并重启；原密钥/加密Data/安全与原watchdog基础回归304.447秒通过。用户确认：边缘返回、回桌面及多任务三项均恢复。

0541全部布局和滑动已获用户确认。0542补齐触摸完整坐标范围、0543补齐真实事件屏幕关联后，用户两轮复验返回/回桌面/多任务仍全部失败；这些结果保持，不将前两轮写成完整修复。

## 固定原版证据

已核对20H392原版UIKit `__UIEventHIDTouchEdgeTypeForHIDEvent`：读取触点事件掩码0xb0007，其中FromEdgeTip位0x800映射edgeType=1。原版 `_UISEEdgeTypeFailGestureFeature` 在首次采样未标为边缘起始时进入失败状态；UIKit继续根据运动轨迹/方向等条件识别，单有边缘坐标不能代替该标记。参考hackios固定快照也有此处理，但其用户态注入路径未采用。

## 原生实现

保留原版HID解析和所有现有触点，只为真实触点根据首次落点补充原生FromEdgeTip来源标记。基于D22的375×812逻辑点，侧边/顶边取16点、底边取34点。首次中央落点以后滑到边缘仍不改判；按ID保留起点分类直到真实抬起，取消清理状态。子事件与父事件聚合掩码保留原位，仅增加对应来源位；原版UIKit负责返回/回桌面/多任务识别。

这属于基于当前TCM坐标报告的软件分类，不声称实现Apple触控芯片的接触形态或压力检测。没有合成触点、Home按键或完成手势，也没有替换原版识别器/失败返回。内建屏幕关联修正继续保留。

## 可重跑的主机检查

使用完整回放的XNU源码、Python 3与Clang；输出写到仓库外或忽略的本地缓存：

```sh
python3 scripts/tests/run-touch-full-canvas.py --source /path/to/replayed-xnu --output /tmp/touch-full-canvas.json
python3 scripts/tests/run-touch-edge-origin.py --source /path/to/replayed-xnu --output /tmp/touch-edge.json
```

0543的屏幕关联测试使用其当时的准确源码版本，旧收据保持不变。上述0544测试执行生产函数并使用明确的IOKit/原版事件API替身，不访问手机，也不替代下述原生ABI与实机人工验收。原版UIKit离线指令验证需要合法取得的20H392私有输入，公开仓库仅发布限定结果和代码哈希。

## 分层验证

- 生产分类器和包装函数524,519项主机检查通过，包括完整归一轴、严格边界、10个ID、跨帧移动/抬起/取消、中央起手、异常ID/坐标、原事件及选项透传。8个编译成功的行为错误变异均在运行中被拒绝。
- 原版UIKit ARM64边缘解码和首次采样失败/识别门槛执行通过；IOHID getter与ObjC状态setter为明确API替身，原始判断指令未替换。这是离线原版逻辑验证。
- 原版children accessor已独立核对；通过原方法获取子事件，不读取私有对象槽位。原驱动219槽、事件37槽及最终原生子类219槽配对保留。
- 399共同+4主机补丁、5290源码文件完整回放与实际原生构建配对。内核SHA256：`f7ccc57bd3be1bd76017b9d1712f58df561489e0815f8bce5ae71d17b7841ae2`。
- 实机已观察到真实子事件增加0x800：True。0544已刷入boot_b并重启；原密钥/加密Data/安全与原watchdog基础回归304.447秒通过。用户确认：边缘返回、回桌面及多任务三项均恢复。

原Root/nonce、bank/systembag、已有密钥与加密Data、MACF/ACL/Sandbox和原watchdog保留；本轮无新的手工持久化偏好修改。有限窗口与人工交互不等于长时应用稳定性、GPU/DPU/120Hz、网络、完整SEP或完整12GiB验收。发布内容为配对源码、可重跑测试和限定收据；原始供体、可刷镜像、密钥及私有Data保留在本地。

见[限定收据](qualification/0544/edge-qualified.json)、[前轮屏幕关联及人工失败](EDGE-GESTURES-0543.md)、[坐标修正及人工失败](EDGE-GESTURES-0542.md)。
