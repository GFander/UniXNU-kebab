# 0543 真实触屏事件的内建屏幕关联

更新：2026-11-02T16:50:40+08:00。0543已刷入boot_b并重启；原密钥/加密Data/安全与原watchdog基础回归304.505秒通过。用户复验：三项边缘手势仍失败或部分失败，详见人工反馈收据。

用户确认0541全部布局与滑动正常。0542修正了触摸居中裁切盲区，真实底部起点已达到raw y=2399→logical y=2435，但用户明确复验三项手势仍全部失败。该失败结果保留，不能将坐标修正记作完整手势修复。

## 本轮修正

固定20H392的backboardd在`_determineServiceForEvent:sender:fromTouchPad:`中单独读取事件字段0xb0019；仅设置服务注册表DisplayIntegrated不能替代该事件字段。字段为0时，原版代码将digitizer送入触控板路径。0543通过继承原版IOHIDEventDriver，给本机真实内建触屏的digitizer事件补充屏幕关联；原版报告解析、事件对象、时间戳、坐标、ID、触摸掩码、子事件与原版分发过程保留。

匹配限定Digitizer/TouchScreen usage，启动时再次检查原IOHIDInterface的提供者必须为KebabHIDDevice且真实role=2。其他设备和按键不能进入该路径。没有合成触点、识别结果或Home按键，也没有改写Apple代码或二进制虚表。

## 证据与边界

- 原供体IOHIDEventService/Driver对象大小200/264字节；编译探针逐槽核对原驱动219槽和事件对象37槽。最终原生子类219槽、214个继承目标及两个限定到原类虚表的启动/分发调用再次核对。
- 原版ARM64 getter/setter在合成事件上的执行通过；字段0xb0019只改变digitizer数据第10字节的0x08位，清除后所有字节恢复。没有替换原版指令；这属于离线ABI验证。
- 生产包装函数在明确IOKit/原服务替身下通过提供者限制、原启动失败保留、210组事件/选项透传和null透传检查；7个编译成功的运行错误变异均被拒绝。
- 398共同+4主机补丁、5289源码文件完整回放与实际原生构建输入匹配。内核SHA256：`374f3624b908cfe2e8d2ee860f1fd7221e1c8b0b59f8ea8042cf4bbc00d8e5e8`。仅4个内核文件变化（其中1个为新增声明头），显示与坐标映射代码不变。
- 实机原版译码器启动：已观察到start=1；真实事件标记0→1：已观察到。0543已刷入boot_b并重启；原密钥/加密Data/安全与原watchdog基础回归304.505秒通过。用户复验：三项边缘手势仍失败或部分失败，详见人工反馈收据。

原Root/nonce、bank/systembag、既有密钥和加密Data、MACF/ACL/Sandbox及原watchdog保留；无新的手工持久化偏好修改。基础回归不替代长时GUI稳定性、真实GPU/DPU/120Hz、Wi-Fi、完整SEP或12GiB验收。源码在Ubuntu隔离工作树，保留既有dirty，未提交/推送。

见[限定收据](qualification/0543/edge-qualified.json)；0542坐标修正和人工失败见[上一阶段](EDGE-GESTURES-0542.md)。
