# Image4 当前阻塞及下一步

当前真正的PID1已从APFS System执行dyld/libignition，但停在`security.mac.img4.ignition_blob`。默认关闭SoftwareAES时，原版AppleImage4等待AES服务；显式启用且通用KAT通过后，原版代码请求selector **2212**、128bit、encrypt、16bytes，收到明确unsupported后panic。0213已实测这一请求和panic串口FASTBOOT恢复，不能把等待归因于DSC缺失。

## 2212不是未初始化的raw-key字段

0221供体离线核对见`standardization-run-20261029/nonce-metadata-0221.json`，prelink SHA256为`89fde10d1749dc39353a74c43b06d52c20dc864338dff0d0c4d2df3a91cd0583`。

原版helper `0xfffffe00089273e0`把40字节key结构清零，设置bits128，再在`0xfffffe0008927424`读取nonce-domain描述的`+0x38`，写入key的`+0x24` selector。调用时IV为零，输入为nonce记录的16字节；NativeAES头中的selector偏移36与原始指令一致。不能忽略selector，把结构里全零的bytes当作可用raw key。

Image4的11个固定nonce-domain描述都明确使用2212：test、trust cache、PDI、cryptex、DDI、ephemeral cryptex及5个cryptex1域。该事实确认这是原版nonce选择器；**没有证明它等同哪一个UID/GID根密钥或具体KDF**。不能仅凭数字给它起一个硬件key名字。

原版IOAES的`createSpecialKeys`确有描述表、基密钥操作和派生结果表的流程；当前raw-only NativeAES没有完整实现这些私有状态。直接调用原版`start/createSpecialKeys`不能替代实现和验证所需的基密钥能力。

## 板级接口目前知道什么

0219仅查询固定SCM接口可用性，SMCCC为1.1，SCM64自查询成功，ICE invalidate/set返回可用，QSEE version返回不可用。没有执行这些目标操作，没有传递密钥、共享内存或修改RPMB。探针默认关闭。

ICE是另一类接口；这些availability结果不能证明能派生2212，也不能证明QSEE的所有其他接口都不可用。编号和调用约定依据[Linux v6.12 SCM定义](https://github.com/torvalds/linux/blob/v6.12/drivers/firmware/qcom/qcom_scm.h)及[具体调用端](https://github.com/torvalds/linux/blob/v6.12/drivers/firmware/qcom/qcom_scm.c)，不是从别的SoC猜测。

## 后续验收顺序

1. 离线追溯固定20H392供体的AES key-description来源、2212到基选择器/派生输入的关系；记录可证明的字段和调用点，不用新版本供体地址替代。
2. 确定KEBAB实际能提供的设备密钥/派生后端及其安全存储、生命周期、错误和复位稳定性。需要原版消费者兼容的含义和操作合约；不能用全零或随意常量伪装设备密钥。
3. 除CBC公开向量外，增加专用选择器正反例、失败不注册、重启稳定性、nonce/BNCH一致性验证，再观察原版Image4是否真正打开gate、ignition OID是否返回。
4. 继续原版Data内容保护/卷配对、真实Preboot/Cryptex与DSC/launchd任务。Preboot测试文件持久化不是Data keybag或完整安全启动的证明。

截至当前仍没有完整launchd、mount-phase-1、Metal、backboardd或SpringBoard运行证据。当前稳定基线保持System/Preboot只读、SoftwareAES关闭，保留真实失败而不伪造安全返回值。
