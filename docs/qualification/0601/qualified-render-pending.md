## 0601原安全、人工GUI、GPU/CP及只读资源资格通过；主线回到First Render

用户明确确认SpringBoard显示、点击打开应用、返回主屏全部正常；亮度继续暂缓。人工独立收据native-dpu-0601/0601-manual-GUI-owner-confirmed.json SHA-256 d1627da4447f4e201320735ce0b577f8e0651ef24bf23013758de64e8e063f8b。原安全独立normal-baseline-qualified.json为304.438秒PASS，Root/nonce、原bank/systembag/加密Data/MACF/ACL/Sandbox/watchdog保留。

LOWDMA831、Normal-NC/CPU_RAM、GMU/HFI/四初始化消息、GPUVM/GX/OOB/CP及后缀/SN11全部回归通过；CP Fence1/2、RPTR16/27、真实IB marker0xa6500c0d。实际CR0=0x260406，0x220406变体仍HOST_ONLY。新PASRANGE仅一次，17范围/136对/260页/双次CP静止、flags1023/result0/phase3、3027us；三份568B CAP43同SHA-256 5f31340696c97002e8ac138730c9b0b444a9e954dbf9cc8b3d3619e1748a6196。独立资源收据0601-readonly-resources-qualified.json，公开15个私有PA已遮去，408B私有范围摘要不能从公开字段独立重算；此记录不是全局alias排除或认证lease。

PAS13支持查询仍false，按用户明确指示结束重复查询路线；ZAP实际认证NOT_RUN/UNKNOWN，返回0不能解释为认证必然失败。没有INIT/MEM/AUTH/SHUTDOWN、secure exit或绘制。FirstRender仍0591真实像素FAIL，GPU IRQ/Metal未验收。下一直接核对0591 PM4/地址/格式/Pitch/CCU写回/CPU同步/SMMU/原始BO，设计聚焦的可区分原因硬件绘制；只有明确安全故障或平台源码证明必需ZAP前置时再调查，保留全部原门禁。

严格汇总native-dpu-0601/0601-original-regression-resources-qualified-render-pending.json SHA-256 6a78fb60fc13df2d339bf29fe2fbf5dae0eee7c2bb896d3d368c991f573652a0，93个raw样本hash重核，serial已释放且无设备进程。native b67481ed190b22ad5eb1fb5821d66d2b0e83e1af10e1b476019518707ec24b8e，boot 6d3dfff3930815d9849bf58b691ef8912e20a8e1d350f22fe264e688deac8d29，455+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a，未commit/push。0591/0599及全部历史失败保留，勿重发此boot任何writer。

