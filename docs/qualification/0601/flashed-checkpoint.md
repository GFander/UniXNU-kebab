## 0601已刷入，原安全回归及只读PAS资源观察等待中

正式0476，455+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a。九文件新增正常PASRANGE/CAP43，canonical completed-CP owner、8个GMU池（含scratch）/7个GPU池/2个低PA共17范围136对，260页AT/PAR重查、双VM/CP静止快照、私有408B范围摘要；公开568B遮去15个私有PA。仅观察，不是全局alias排除、secure lease或认证权限，原PAS namespace slot1门禁保持。6114主机断言、ASan/UBSan、14编译运行变异、协议6变异、7实际SDK TU、完整native/54exports/源清单、LC_FUNCTION_STARTS真实机器码边界和487坏decoder输入、原ABI/HID/cache/封包及刷新后门禁通过。旧审计把未命名后续函数误算入getter，现以真实函数边界95指令核实；未改驱动以迁就审计。

device c073a488/b Sending/Writing/reboot均OKAY。native b67481ed190b22ad5eb1fb5821d66d2b0e83e1af10e1b476019518707ec24b8e，boot 6d3dfff3930815d9849bf58b691ef8912e20a8e1d350f22fe264e688deac8d29。capture PID 78324，collector 79428，finalizer 91408，唯一顺序post PID 60612。恢复须核对实际进程，勿重复apply/build/flash/writer。原300秒安全且capture实际退出后LOWDMA→NC→CPU_RAM→原GMU/HFI/VM/GX/OOB/CP/后缀/SN11→一次PAS13SUP→只读PASRANGE/三CAP43，前置失败不继续依赖。原CPU显示、安全、加密Data和watchdog均保留，0601人工GUI尚NOT_RUN，最新0600用户全部正常，亮度暂缓。

检查点native-dpu-0601/0601-flashed-baseline-pending-checkpoint.json SHA-256 fe62c5e941a2e8db9f40d3bf99119fe74bdae4ec472383ea188826c4fa046d2a。入口normal-baseline-qualified.json、post-sequential-output.log、live-PAS13-resources/result.json。0600真实support0不可反转，0591像素FAIL保持；没有新认证/secure exit/绘制/IRQ/Metal资格。下一须根据真实资源结果继续PAS独立owner/HYP生命周期与完整Mesa restore核实。

