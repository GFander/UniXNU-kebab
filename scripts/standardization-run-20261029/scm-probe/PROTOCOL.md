# 固定SCM能力调查

这是默认关闭的BSD启动诊断，不是新IOKit驱动或密钥后端。只在`kebab_scm_probe=1`、非panic、非IRQ、可抢占且中断开启的普通启动上下文执行一次。无用户命令入口，无任意SMC函数号/地址接口。

先调用ARM SMCCC_VERSION `0x80000000`。随后只执行SIP INFO_IS_CALL_AVAIL：fast64 `0xc2000601`，失败则fast32 `0x82000601`；arginfo=1，x2为查询目标。自查询目标`0x02000601`必须返回status0/value1才继续，没有legacy或强制成功兜底。[Linux v6.12 convention detection](https://github.com/torvalds/linux/blob/v6.12/drivers/firmware/qcom/qcom_scm.c)、[ARM SMCCC编号与寄存器约定](https://github.com/torvalds/linux/blob/v6.12/include/linux/arm-smccc.h)。

固定查询目标为ICE invalidate `0x02001003`、ICE set `0x02001004`、QSEE version `0x32000603`。这些目标本身不执行。返回值只表明接口可查询，不能证明密钥存在、Apple selector2212映射成立、QSEE应用可用或Data内容保护已完成。[Linux v6.12 service/command constants](https://github.com/torvalds/linux/blob/v6.12/drivers/firmware/qcom/qcom_scm.h)、[QSEE owner/service/version定义](https://github.com/torvalds/linux/blob/v6.12/drivers/firmware/qcom/qcom_scm.c)。

SMC输入/输出显式占用x0–x7，声明x8–x17、条件码和内存clobber。status1时以x0=1、保存的x6 cookie恢复，其他参数重新装载，最多32次；busy/不支持/其他错误不重试，不进入等待队列。次数限制不能保证一个不返回的固件调用会超时。[Linux v6.12 Qualcomm A6约定](https://github.com/torvalds/linux/blob/v6.12/drivers/firmware/qcom/qcom_scm-smc.c)。实现独立编写，没有复制GPL实现。

验证：真实BSD目标TU和全量链接；15个生产逻辑主机测试在ASAN/UBSAN下覆盖无权限上下文、关闭门控、SMC32回退、错误符号扩展、恢复cookie、所有参数重新装载、32次上限与失败停止。真机必须另验完整固定查询日志、根卷/Preboot SHA、原诊断及panic FASTBOOT。编译/主机mock不代替固件实测。
