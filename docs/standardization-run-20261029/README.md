# 七小时标准化窗口的证据与脚本

本目录保存各批次的不可变before/after、故障测试、构建及设备证据。最新结果以根目录`STANDARDIZATION-RESULT-20261029.md`和本目录RUN.md为准；历史失败候选不属于当前可刷基线。0223的完整FWLOG转储失败，0224补齐普通发送环排空后通过；0225为同native、移除测试参数的常规配置。

正式补丁位于Ubuntu仓库`patches/xnu-8796-kebab`，主机准备位于`patches/hosts/macos-dxb-13.5`。阶段目录中的旧过渡补丁、prepare/gate/fix/pivot脚本只用于重现对应过渡，不能在最终源树盲目重跑。部分脚本固化了本项目的Windows/Mac/Ubuntu路径；这不是跨主机一键构建入口。

仓库`bash scripts/verify-all.sh`严格应用共同和主机补丁，再从刚重放的生产代码运行8项故障测试；加上既有检查共17 PASS，4个可选IPSW检查SKIP。它们不需要手机，不访问网络。历史阶段测试与fixture另留，不应全部对当前源码盲跑。

review mirror把说明/证据JSON放在docs、脚本/fixture放在scripts；原始本机目录保持阶段间相对路径。纯分析辅助若依赖相邻证据JSON，需从docs取对应文件；真正接入verify-all的测试与fixture保持可运行。

最终交叉证据由`verify-final-evidence.py`核对镜像/config、源码清单与实际Mac、逐类ABI、正常同步复位、锁内panic FWLOG/FASTBOOT、真实64KiB满环及完整补丁重放。稳定性观察是4个只读采样点，不等于完整系统或UI测试。

不重复写216的Preboot proof，不整体重刷userdata；旧Mac raw checkpoint260不能覆盖实际设备261。Git只在Ubuntu进行；prepared-source构建跳过Mac builder内置Git patch循环。完整旧Mac源树保留，当前实际树来自干净重放。
