# 0386：原版 Sandbox 与本机 watchdog 契约

本目录是实机验收元数据；设备序列号和本机路径已脱敏。运行日志SHA保留在收据中，原始日志和手机数据由设备所有者保管，不包含在源码发布中。

先读 [完整报告](../../SANDBOX-WATCHDOG-20261031.md)。原生内核0383复用于0384/0385/0386；owner策略数据改21字节，原版代码不变；这不等于Apple认证的未修改策略。原版watchdog仍监控实际心跳。

## 公共源码回放

准备Apple OSS的XNU仓库本地克隆，然后在设备仓库执行：

```sh
python3 scripts/qualification/replay-xnu.py --repo . --xnu-source /path/to/xnu --manifest docs/qualification/0386/all-mac-source-0383.json --receipt /tmp/kebab-replay.json
```

脚本从固定基线1b191cb58250d0705d8a51287127505aa4bc0789在临时目录回放268个板级补丁和4个Mac构建补丁，逐一比对5239个实际编译输入。可用`--cache`指定临时目录。该检查不需要IPSW、SDK、固件、设备或密钥；不证明运行时功能。

## 主机测试快照

`scripts/tests/`中的三个C++文件是对应构建实际生产函数连同显式IOKit/VFS/后端替身生成的固定测试输入。可用clang++17和ASAN/UBSAN编译执行；线程队列测试还需`-pthread`。分别覆盖watchdog30、Cryptex真实队列264、只读库可见性46项。源函数变化后须从新源重新生成并校验，不能拿旧快照为新实现背书。

策略变异检查另覆盖407项实际prelink候选条件，输入含设备所有者自行准备的原版组件，因此本仓库只发布结果，不分发对应闭源候选二进制。原版Sandbox代码SHA和只读策略适配窗口在适配脚本中固定，任何不匹配拒绝执行。

## 构建边界

本次实机使用owner工作台完成KDK构建、原版组件prelink、owner策略适配、payload测量和boot_b刷写。`board.toml`仍保存早期standalone构建入口，未把它当成本次已验收的端到端prelink配方；不能仅凭公共源码回放通过就宣称其bootimg已实机验证。此处明确区分源码、构建与运行行为。

IPSW/DMG、固件和boot镜像、原版kext二进制、硬件根密钥/opaque记录、APFS数据、口令及令牌均未发布。下一步继续真实A/B文件密钥、原版AKS接口和daemon功能，再审核并实现GPU/显示/输入，最终进入可用SpringBoard。
