## 0603已刷入：A650初始RB ECO补齐，绘制等待安全回归

正式0478仅RenderCore/Binding两个header，457+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a。依据固定Mesa63350a A650初始restore，在602真实同页CP标记后新增WFI/TYPE4 RB_DBG_ECO_CNTL=0x04100000共3字；正常/blit相等只跳过模式切换，原始初始写不能省。原2D与CP控制、逐像素/guard、Fault/超时/quarantine/无retry保持。135字/pass，WPTR162/297，CAP38 schema3/135752B。1370主机断言、旧602编译红、ASan/UBSan、11运行变异、305坏decoder输入、7实际SDK单元、完整native/54exports/源清单、实际vector加载并保存WFI/8e04/04100000机器码、原ABI/HID/cache/封包门禁通过。首次派生工具残留parent601/common456，已在源码apply前拒绝，原工具保留后修成parent602/common457；驱动未为迁就工具修改。602旧输入清单的3个接收器/words/调用门禁适配以独立收据绑定，不改写历史输入。

设备c073a488/b Sending/Writing/reboot均OKAY。native 7f828fc6de9413dceb0bb596e31265c78692592106bd140ba62a93ddd0dcdcdd，boot 534f56058e776e4797cd9de3d25728dd7681fdf07d25d4d9ff92db36058e03fa。capture PID 59908，collector 75704，finalizer 50140，唯一顺序post 91040。恢复核对实际进程，勿重复apply/build/flash/worker。原300秒安全且capture实际退出后LOWDMA/NC/CPU_RAM→完整GMU/HFI/VM/GX/OOB/CP/原后缀SN11→一次GPURENDER/三CAP38；原调用门禁已严格核对CAP34/35各3及36/37各3，不重复602旧VM计数错误。人工603尚NOT_RUN，最新602显示/点击/返回全部正常，亮度暂缓。

检查点native-dpu-0603/0603-flashed-initial-RB-ECO-render-pending-checkpoint.json SHA-256 8cc8aa499e95311d5df5c17605f56a87a55026fa3826d26f314972386891197e。入口normal-baseline-qualified.json、post-sequential-output.log、live-gpu-render/result.json。602严格记录d57503fb051bdf26ac94289e054f0ab9b64d4b8ffd76b6288cc198673d84d1c8：同目标页CP控制a650c001可见，Fence3/RPTR159/CCU完成，像素仍A5，第二pass未提交，隔离保留。没有PAS13SUP/PASRANGE/认证/secure exit/IRQ/Metal新资格；PAS13查询路线关闭、ZAP实际认证NOT_RUN/UNKNOWN。硬目标仍A650_FIRST_RENDER=PASS，失败继续按真实结果聚焦，不能以标记当像素。

