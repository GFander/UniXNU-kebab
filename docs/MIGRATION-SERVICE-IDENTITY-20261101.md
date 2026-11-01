# 0476：确认原版等待服务及 FairPlay daemon 退出现场

2026-11-01。继[0475端口路径](MIGRATION-IPC-PORT-STATE-20261101.md)，0356只增加
原生服务标签观察。5份标签均未截断/替换，确认两个服务名：
`com.apple.locationd.synchronous`和`com.apple.fairplayd.versioned`。
**服务身份已查明，退出原因、迁移和桌面登记仍未修复。**

## 实机新证据

- wrapper的CoreLocation迁移等待，及locationd自身同步请求，转交目的都由locationd63
  持有，原标签确为com.apple.locationd.synchronous。两采样队列6/16、8/16。
- locationd另一条SEND1203等待的目标由launchd1持有，队列6/6，原标签确为
  com.apple.fairplayd.versioned；不是凭接收PID猜测它就是launchd bootstrap本身。
- 原版job声明fairplayd.H2、/usr/sbin/fairplayd.H2、mobile用户/组、KeepAlive，
  包含上述versioned服务。其首次被记录的退出为signal0/status0，栈为原__exit、
  dyld LibSystemHelpers::exit、dyld start；随后观察到约10秒间隔的反复启动。
  首次退出观察器有预算限制，不能据此断言每次退出原因相同或从未短暂check-in。
- locationd的BLS isAlwaysOnEnabled仍指向SpringBoard；SpringBoard仍等待backboardd，
  backboardd等待迁移。上述非原子快照，不是完整事务或唯一依赖环证明。

[FairPlay元数据与观察](qualification/0476/0476-fairplay-observations.json)只包含
原输入哈希、job事实、退出栈和启动时间；闭源二进制、完整反汇编、密钥、端口guard
/context、消息内容和私有Data不发布。实际从只读原System挂载取样。

## 实现和分层验证

只有active且ip_service_port置位、splabel非NULL时，才按原IPC服务标签解释。
在既有port锁内复制launchd-name/flags；CONFIG_SERVICE_PORT_INFO开启时复制domain
和最多63个字符。明确trunc/changed，未知/空名保留标识；锁外打印独立短行。
不改变配置开关、原消息/服务行为、权限或返回值，也不读取label guard/context。

| 层次 | 本轮实际范围 |
| --- | --- |
| 主机生产体 | CONFIG1为253检查，CONFIG0为245检查；20个实际编译行为变异拒绝 |
| 模型边界 | IPC/task/并发是明确模型；覆盖非服务/死端口无效label、逐跳引用、null/0/1/62/63/64/255长度、字符替换及224B行预算 |
| 原生 | 335 common+4 host、5,279源文件精确回放/postbuild相同；TU0错误、29未屏蔽警告；原IPC调用/新输出、HID207、宏/ACL及打包回读保持 |
| 实机 | 29完整IPC观察、14条STATE、3条transit路径、5份完整服务标签；全部固定命令实际完成 |
| 加密保留 | 原A先UNWRAP/旧负载与密文保持、锁定已有缓存/直接I/O零传输拒绝；旧B/C/D及原securityd6–11旧记录恢复；Root/nonce/bank/systembag、RTC/NVRAM/defaults保持 |
| 监控 | 原watchdog至324.949秒，无panic/查询超时；有限窗口，不称长期稳定 |

相较0475只改观察器头，crypto/security源码未变。原生0476为18,980,848字节：

```text
be4c67b34abaafa6555862c3577e24236e71a315ede57bb10770d62fc0aafdb8
```

[验收](qualification/0476/0476-IPC-qualified.json)、
[加密回归](qualification/0476/0476-A-qualified.json)、
[可重跑校验器](../scripts/tests/run-ipc-service-name.py)。
无新触摸、GPU呈现、物理120Hz、全盘扫描或Data fsck。

## 晚期观察与下一步

对仍运行的0475另做独立MIG/UI查询，距原startup note墙钟1039秒，同一wrapper
线程仍在CoreLocation、同一SpringBoard线程仍在check-in、相关端口对象不变。
原封存启动日志未改；未独立采kernel uptime/bootUUID，不能把墙钟间隔称该测量。
见[晚期观察](qualification/0475/0475-late-waits-qualified.json)。

继续定位原fairplayd.H2实际初始化/系统调用返回及服务登记生命周期。静态可见的
启动检查不足以证明退出原因；保留原安全检查，不替换原daemon或伪造回复，
不跳迁移、不删库/重建key/降保护类。原密钥/数据库/迁移/桌面目标继续进行，
暂不扩展GPU/显示/输入，蓝牙延期。factory无PIN不等价Apple SEP/口令。
