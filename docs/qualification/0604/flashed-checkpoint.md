## 0604已刷入：A650 RB/GRAS命令模式恢复，绘制等待安全回归

正式0479仅RenderCore/Binding两个header，458+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a。固定Mesa63350a的A6XX初始context restore分别写RB_MODE_CNTL=0x10（8811/有效6:4）和GRAS_MODE_CNTL=2（8110/有效1:0）；只新增这一组4字命令模式。保留603初始ECO、602真实同页CP控制、原始0591 2D/逐像素/guard、Fault/超时/quarantine/无retry。139字/pass、WPTR166/305；CAP38 schema4/135784B。1378主机断言、旧603编译红、ASan/UBSan、15运行变异、323坏decoder输入（含两快照全部5CB故障位拒绝）、7实际SDK单元、完整native/54exports/5456清单、实际vector加载保存ECO+模式机器码、原ABI/HID/cache/封包门禁均通过。另复核591/602/603失败快照全部5CB和global前后相同且有效Fault位0，原始FSR0x400保留；成功路径原有VM decoder已覆盖全部5CB，不放宽门禁。没有SMC/PAS/TRUST/IRQ/电源/SMMU改动。

设备c073a488/b Sending/Writing/reboot均OKAY。native 4ba45bdca829c7ea4a731a5792a52932d237490cbd7f4f7b6a6b45b962974639，boot d5cb9d54351cea9f222c5bc7e50775f8669cbc2f2158ce8fcb45e1744fa0bb43。capture PID 84764，collector 59776，finalizer 71312，唯一顺序post 92116。恢复先核对实际进程，勿重复apply/build/flash/worker。原300秒安全且capture实际退出后LOWDMA/NC/CPU_RAM→完整GMU/HFI/VM/GX/OOB/CP/原后缀SN11→一次GPURENDER/三CAP38。人工604尚NOT_RUN，最新603显示/点击/返回全部正常已独立收据54824c8adba437a00d98966fa4995676ada3283f07c8a5d75ab320d0193f7522，亮度暂缓。

检查点native-dpu-0604/0604-flashed-RB-GRAS-modes-render-pending-checkpoint.json SHA-256 354535ea27ce2c2d423ec9edcfa8505ec5a85e453088c23edec9707a7ce5bcdd。入口normal-baseline-qualified.json、post-sequential-output.log、live-gpu-render/result.json。603严格记录05f9cfd8bb3145b17f977237a32f54e37636940242f58afa64e34a669a140f69：ECO命令已提交，CP控制a650c001可见/Fence3/RPTR162/CCU完成，但像素仍A5，第二pass未提交，隔离保留。PAS13查询路线关闭，ZAP实际认证NOT_RUN/UNKNOWN。FirstRender硬目标尚未达到，不能以CP控制或GUI代替像素。

