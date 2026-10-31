# 原版 HID 音量键与 Data 运行时设备身份（0430）

0429 实机按音量上、下，各产生真实按下和松开；按键位序列为0、2、0、4、0，原版 HID 报告均返回0，队列无丢失。原版 IOHIDDevice、IOHIDInterface 和 IOHIDEventDriver 已组成真实匹配链。0430 完整内核重启后再次成功初始化。该验收覆盖音量键到原版内核 HID 路径，不声称背板服务或界面已消费事件；电源键、触摸、显示呈现、GPU与可用SpringBoard仍未验收。

0317 从固定20H392供体核对原版 IOHIDDevice 的208字节布局、207个虚表槽和57个绑定；最终内核207个槽再次校验。公共 Apple IOHIDFamily-1915.140.3 仅用于接口语义参考，不声称与供体版本相同。无虚拟键盘、Home注入、虚假Apple设备身份、二进制虚表修补或未实现接口返回成功。

0428 首次接入时揭示RTC与NVRAM硬编码设备号1/3：实际Data仍名disk1s2，但dev_t变为1/4，因而冷读拒绝。0318使用已固定Data UUID、同一底层存储、当前挂载/private/var和只读System身份校验，保留root0600、classD、双记录/双银行完整性检查。0429真实1/4场景通过，0430再次通过；没有格式化、生成新Root或密钥包、回退软件密钥或跳过认证。

0429/0430均保留硬件Root/nonce、原版MKB、C/D加密Data、RTC/NVRAM、defaults和原版watchdog，心跳超过180秒。297common+4host补丁独立回放到5259个完全相同源码文件。1288个报告/策略用例、生命周期/并发停止测试、253个运行时设备号用例和9个行为变异通过ASAN/UBSAN；主机上的硬件、IOKit、VFS和原版解析器均为明确替身，实机证据单列。

每日development-fast继续启用，不重复全量System/SystemOS校验。最近独立只读Data fsck仍为0416，完整System/SystemOS内容证明仍为0341，本轮没有新增对应证明。蓝牙资料已归档，继续延期到可用SpringBoard之后。

下一步验证S3908真实固件/供电/报告及触摸事件；解决原版CADisplay枚举和呈现，再推进A650/Metal、真实显示时序与可用SpringBoard。保留未完成的真实密钥/daemon问题，不以掩盖崩溃代替功能。
