# 开发、验证与发布

当前状态见 [STATUS.md](STATUS.md)，功能总览见[首页](../README.md)。适配方法参考 [UniXNU 移植指南](https://unixnu.com/zh/porting/)，文件职责参考[官方设备仓库说明](https://unixnu.com/zh/porting/device-repository)。本仓库现有布局与新版模板的差距见 [REPOSITORY-LAYOUT.md](REPOSITORY-LAYOUT.md)。

## 实际构建路线

当前验收路线使用 Apple OSS XNU 8796.141.3，16 KiB 页，板级 `KEBAB`，DEVELOPMENT ARM64；共同补丁按 `patches/xnu-8796-kebab/*.patch` 文件名排序，再应用四个 `patches/hosts/macos-dxb-13.5/*.patch` 主机补丁。0468源码（0469复用）的5,275 项源码清单不覆盖外部 Xcode、SDK、KDK、供体二进制和私有设备输入。

Mac 全量构建使用固定主机环境与 prepared-source 流程；prelink 使用固定 20H392 kernelcache、原版组件身份与各次 ABI 资格。Windows 端载荷打包、ADT 变换、策略数据适配、回读及刷机门控另有独立步骤。历史环境与命令保留在 [BUILD-BASELINE-20261029.md](BUILD-BASELINE-20261029.md) 和 [FIRMWARE.md](FIRMWARE.md)，不能直接把旧版本号或旧卷号用于当前设备。

根目录 `board.toml` 仍是历史入口，并未描述完整的当前 prelink/打包流程。公开仓库尚缺从全新环境到当前可刷镜像的一条统一且已上机复验的配方；不得把文档整理或源码回放写成已解决这个问题。

## 三层验证分开记录

1. **源码/主机**：补丁顺序、逐文件清单、生产代码测试与明确列出的模型/替身。行为变异测试须实际编译并因行为错误失败，不能把编译失败当作拒绝成功。
2. **实际构建**：相关编译单元、完整原生构建、最终文件存在/哈希、原版组件文字段与绑定/虚表 ABI、prelink 和镜像回读。不能只检查“Build Done”字样。
3. **实机**：准确版本/原生哈希/启动参数、实际日志、命令完成标记、原版服务行为和回归；将时间窗口、失败和未验证部分一起保存。注册服务、进程存活和日志变少都不能替代功能验收。

当前公开源码回放命令见[首页](../README.md)。它固定 Apple OSS 基提交并比较所有列明源文件，不接触设备。触摸记录校验器只复核封存的人工采样，不生成新事件。大型镜像只在存储或内容变更所需的验收流程中做完整校验；正常 development-fast 启动不能因此被称为完整内容复验。

## 设备与安全不变量

- 测试固件只写已确认目标设备的 `boot_b`。刷写前核对设备/槽位、全部门控和待刷文件，Sending/Writing 均 OKAY 后才启动捕获并重启。
- userdata 已专用于 HiOS，不能套用旧文档“仍保留 LOS 数据”的假设。格式化、重新分区、替换整个 userdata、重建已有密钥都需要对应操作的明确授权；日常修复不做这些动作。
- 保留硬件 Root/nonce、既有 bank/systembag、C/D 文件与原始密文证明、MACF/ACL/Sandbox、原版 watchdog。未知 AKS 操作返回真实错误，不提供软件密钥回退或无条件成功。
- watchdog 授权使用精确 owner 策略映射，保留真实允许/拒绝语义。不能通过停服务、屏蔽日志、改超时或禁用监控表面上消除失败。
- 串口只允许一个捕获者；查询超时后检查原捕获/设备状态，不能把超时当作进程终止，也不能自动叠加查询。panic 后复位，不从 KDP 强行恢复执行。
- 同一 Mac 构建树只运行一个构建，确认旧构建已结束后才改源码。普通工具输出、进程锁文件和旧 PID 本身不能证明进程仍活着。

## 文件与提交

- `patches/xnu-8796-kebab/` 保持已验收基线的有序板级补丁；主机适配放 `patches/hosts/`。改动通用组件或驱动拆仓必须另做真实构建和实机迁移，不能只移动目录就声称标准化完成。
- 验收资料放 `docs/qualification/<版本或里程碑>/`，阶段解释放 `docs/`，测试和可复用校验器放 `scripts/tests/` / `scripts/qualification/`。旧记录放 `docs/history/`，不改写原始日志和哈希绑定样本。
- 每个功能提交说明实际问题、最终行为、测试和未完成项；README/STATUS 指向最新验收，禁止继续向首页叠加互相矛盾的“最新进度”。
- 仅在 Ubuntu 检出中操作本项目 Git；作者为 `GFander <gfander@users.noreply.github.com>`，提交使用英文 conventional commit。推送前获取 `origin/ios16` 并确认基线；无新上游提交则无需 rebase，有新提交时先安全整合，永不强推。
- 显式列出暂存路径，保留既有不相关工作树修改；不要 `git add -A`，不批量提交工作台的未跟踪文件。
- 发布前审查新增文件、敏感数据、链接、补丁顺序/哈希和与改动相称的检查。推送后从 GitHub 再次确认 `ios16` 的提交哈希。
- 发布源码、工具和经审阅的证据；不发布 Apple 闭源输入、IPSW、可刷镜像、设备密钥/keyblob 或私人 Data。源码许可与第三方许可分别保留。
