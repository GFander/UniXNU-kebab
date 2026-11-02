# 0541 布局与缩放修正

2026-11-02。本轮参照 b1ackm4x 的 kernel/userland 布局方案，使用原版 20H392 实际调用路径验证，未执行其服务注入或自动解锁代码。

## 根因与改动

物理 GOP 为 1080×2400、stride 4320；D22/iPhone X 的逻辑显示为 1125×2436、3 倍缩放，即 375×812 点。这两个尺寸有不同职责。

此前 selector 8 报告 1125×2436，但 provider 的 canvas-width/canvas-height 发布了 1080×2400。原版 IOMobileFramebufferGetCanvasSizes 把 canvas 属性放入候选列表，原版 CA 的 set_physical_and_canvas_sizes 选择最后一个候选。因此原来会出现 1080×2400 assembly 再复制到 1125×2436 目标的路径。

0417 补丁仅将 canvas 属性改为 fLogicalW/fLogicalH，保持与 selector 8 一致；真实 GOP 的宽高、内存长度、stride、分配与已有完整画布映射代码保持不变。没有修改 UIKit、SpringBoard、系统偏好或加密 Data 内容来实现此调整。

参考：[MiniIOMobileFramebuffer.cpp](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/020124fde2c5708ef63f03fffd996a19b5b7c8dc/xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp)、[显示文档](https://github.com/b1ackm4x/hackios-xnu-kernel/blob/020124fde2c5708ef63f03fffd996a19b5b7c8dc/docs/LEMONADE-DISPLAY.md)、[独立 userland 固定版本](https://github.com/b1ackm4x/hackios-xnu-userland/tree/3278f7f01796e74ecd634c0d348d2b255a37658f)。参考设备是 OnePlus 9/SM8350、主要显示 ABI 为 iOS16.5，不能将其硬件代码或用户态地址直接搬到 kebab/20H392。

## 实机证据

- 0541 boot_b Sending/Writing OKAY；native SHA-256 `4a430f25d2df434f263bdbc38825e79d655adf488c6c856f900bdba645dc21a8`。
- 396 common + 4 host patches / 5288 文件与实际原生构建源码精确一致；MACF、ACL、加密功能及原版服务保留。
- 原版 backboardd 现在直接创建 1125×2436 BGRA 渲染缓冲；原先 1080×2400 assembly 不再出现。
- 原版 SpringBoardHome 初始化的屏幕类型缓存从 0540 的 4 变为 0541 的 3。未覆盖该缓存或原版类型选择函数。
- 原版 Root/bank/systembag、A/B/C/D 现有 factory 范围、加密 Data/defaults/RTC/NVRAM/watchdog 基础回归通过，watchdog 窗口 304.467 秒。这不是口令/完整 SEP 或全系统 GUI 稳定性验收。
- 真实 Surface 2：1125×2436、stride 4608、11,225,088 字节，元数据 stable=1，原始 SHA-256 `1a7bc51e2b80d08778cf3027835507b8e2271b0a20cd13fa663b06c6c2a670a2`。图像及原始 wire 独立 CRC、连续数据、PNG 像素校验通过，零重试，传输窗口日志丢弃增量 0。
- [锁屏像素](qualification/0541/lockscreen.png) 中红色 rdar 布局诊断条已消失，时钟、右上状态栏和底部两个按钮均在画面内，底部按钮左右对称。图像来自保留的真实 Surface；抓取时面板已经熄屏，不声称该图证明了面板此时点亮。

## 保留的失败及限制

- 自动 GOP 抓取命中原版 backboardd 在 99 秒主动熄屏的窗口，得到完整黑帧。wire/CRC/PNG 验证通过，但不作为 UI 布局验收。之后原版系统在 116 秒自行点亮并提交真实新帧。
- 泛用接收工具的 layer0 请求返回 EOPNOTSUPP/95，已保留失败；随后采用已支持的真实 Surface2 路径取图，没有把不支持改成成功。
- 0540 的只读 UIScreen 观测器在主屏幕数组路径返回 3，直接 cache 实际为 0；不能把它解释为已证明某个主屏幕对象 ISA 错误。该工具对原版屏幕数组的识别仍待补齐；本次几何修正的证据来自原版 CA 路径、实际缓冲、原版屏幕类型缓存和真实像素。
- 当前尚未人工复验横向滑动、应用内滚动、控制中心居中和完整密码键盘边缘。用户不在设备旁，未重复要求操作。0539 的原版 work interval 策略修正已保留；不把基础回归通过当作其滑动问题已实机解决。
- 现有原始 Data、硬件密钥和回退备份没有重建或清理。本轮没有新的手工持久化偏好修改。

目前手机运行 0541，串口采集已经结束并释放。Ubuntu Git HEAD 仍为 `5f8dd62fafc3fff9f00b1c91c5f1c4c37a2c6453`，新补丁在隔离工作树中，尚未提交或推送。
