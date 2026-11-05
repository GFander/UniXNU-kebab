## 0602已刷入，安全回归后一次同页CP写入标记/原2D绘制

正式0477仅三文件，456+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a。每pass只在原0591命令前加4个已验证CP_MEM_WRITE字，写到对应目标同页、像素区外0x90010080/0x90014080，值a650c001/a650c002；原128字2D命令逐字未变。每pass132字，WPTR159/291；CAP38 schema2/135728B，完整BO验证新增两个明确控制位置，其它像素/guard要求不变，控制成功绝不算像素。原超时/Fault/quarantine/无retry保持，没有SMC/PAS/TRUST/IRQ/电源/SMMU改动。1364主机断言、旧代码编译后红、ASan/UBSan、8运行变异、299坏decoder输入、7实际SDK单元、完整native/54exports/5456清单、真实控制构造机器码、原ABI/HID/cache/封包门禁均通过。初次接收器暂存遇旧words已存在而拒绝，保留旧words再继续；elapsed有效值误当坏fixture及来源CR0历史版本误指601均已保留修正，无设备writer重试。

设备c073a488/b Sending/Writing/reboot均OKAY；native 994662ac7139addc53e22fc155824ff40e694893cd010da8b2b3ae476776644c，boot 0ce91cd4e338954af75df265cbebb460dbf8f7c8ee73686b99d56e17a0f8af9c。capture PID 45488，collector 15588，finalizer 84372，唯一顺序post 47320。恢复核对实际进程，勿重复apply/build/flash/worker。原300秒安全且capture实际退出后，LOWDMA/NC/CPU_RAM→完整GMU/HFI/VM/GX/OOB/CP/原后缀SN11→一次GPURENDER/三份CAP38。若失败保留BO及隔离，分析同页CP控制是否可见，依证据继续。没有重复PAS13SUP或PASRANGE；ZAP认证NOT_RUN/UNKNOWN。0602人工尚NOT_RUN，最新0601显示/点击/返回全正常，亮度暂缓。

检查点native-dpu-0602/0602-flashed-render-witness-pending-checkpoint.json SHA-256 466d27d5d2dd65ade22008a5f453ac27cfc9217301b97c604e6db258c5f4a437。入口normal-baseline-qualified.json、post-sequential-output.log、live-gpu-render/result.json、pass1-raw-bo.private.bin。原0591只有第一目标真正提交并全65536B A5，第二pass未执行；原包/日志/失败完整保留。硬目标仍A650_FIRST_RENDER=PASS。

