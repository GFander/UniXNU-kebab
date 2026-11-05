## 0609 A650_FIRST_RENDER=PASS — 2026-11-05

Actual609 PAS INIT/MEM_SETUP/AUTH returned0/0, independently qualified by3 identicalCAP45 and frozen memory/17ranges/136pairs/260pages/CP parents. One legal CP_SET_SECURE_MODE(0) completed with its dedicatedGPU fence3; HYP protection and raw secure-mode register remain UNKNOWN. Original605424-word drawing unchanged: two32x32 RGBA8 targets in disjoint views of one64KiBBO, pitch256. Hardware RPTR/shadow248→460, Fence3→4, freshCCU timestamps; every pixel/channel and every padding/guard byte matched. Draw35668us. Three identicalCAP38 f98548e9ce9ccf784c96086e8e810a1f5d1dba1af19bb970f483da135949e7ed. Independent scalar verifier and corrupt-pixel/guard rejection passed.605A5FAIL and608prePAS16 refusal preserved.

Original boot security/encryptedData/Root/nonce/bank/systembag/watchdog304.384s PASS; prePAS ownerGUI/tap/home PASS separately archived. Afterrender readonly observation335.275s: originalwatchdog enabled, polls750→1050/beats75→105, no panic/timeout. This extended observer does not rerun old crypto probes. Postrender manualGUI NOT_RUN_OWNER_AWAY: owner explicitly unable to verify; assumed normal is not a manual PASS. Brightness deferred; CPU display path preserved. No new risky hardware operations while owner away.

Private complete checkpoint `native-dpu-0609/0609-first-render-qualified.json`; public digest/measurements [qualification/0609/first-render-qualified.json](qualification/0609/first-render-qualified.json). Native447edd1d920b26d92aba154e830d0f87f560b612f738b345eec37572e6ab22e5; boot06e6b6fc1cb878dd2777152b0c02b12937e2bb9d92cfe0207a63d93d32efde95; sourcebase d5c9c62db752f225a288b88959d3754edc94138a plus463+4/5462.609 allwrite-once commands consumed: NEVER resend auth/exit/render or reclaim frozen PAS memory. All jobs terminal, serial released.

SubsequentIRQ/Metal work reached a source/host/cross-compile checkpoint: officialNativeAdrenoClient10304855… exactv2 layout/bounds/error tests underASan/UBSan PASS; productionclient and layouttest compilearm64-ios16.7/sdk17.5 objects. This is not20H392 privateABI qualification or hardwareMetal. CurrentofficialAdrenoMetal e82788dd… provider explicitlyiOS17; cannot copy its privatevtable to20H392. GPU SPI300/INTID332 andA6xx completionbit20 audited; IRQdelivery stillNOT_RUN/mask0. Full nextentry/boundaries: [GPU-METAL-0609-CHECKPOINT.md](GPU-METAL-0609-CHECKPOINT.md). A650_METAL_FIRST_EXECUTION=NOT_RUN. Next: source/host A650-only IRQ and authorized per-clientNativeAdreno v2 BO/VM/submit/wait, then qualifiednewimage with recovery/freshsafety and independentMetal test; no selector tunnel/mailbox/security bypass, no PAS13support-query route.

## 0609 actual PAS authentication qualified; secure exit/render pending

0609 original safety304.384s and owner GUI/tap/home PASS are separately archived. One ZAPAUTH command completed: INIT/MEM_SETUP/AUTH raw status/value all0/0; INIT used3 interrupted/resume SMC entries under the existing bound, not3 authentication retries. Three identical CAP45 SHA256 61f725563cc223e5182c2809b7ce85701a67c9afe4f243cad9fff08ce94aecae. Independent VM/OOB/CP/Fence/IB and17ranges/136pairs/260pages/CAP43 qualification PASS. Metadata/image remain frozen and retained; HYP protection and actual secure-mode register remain UNKNOWN. No PAS13 support query or TRUST write. Native447edd1d920b26d92aba154e830d0f87f560b612f738b345eec37572e6ab22e5; boot06e6b6fc1cb878dd2777152b0c02b12937e2bb9d92cfe0207a63d93d32efde95; Git base d5c9c62db752f225a288b88959d3754edc94138a plus463+4 ordered patches/5462 sources.

Private checkpoint: native-dpu-0609/0609-actual-PAS-auth-qualified-checkpoint.json. Authentication must never be resent on this boot. Original postauth GPU/PAS-INFO/DMA/power/thermal/SN11 suffix PASS; fresh readonly system/watchdog observer in progress. It does not repeat original crypto probes. Next: after observer terminal PASS, once legal secure exit/CAP46 then unchanged605 two-image workload/CAP38/rawBO/strict pixels. FirstRender NOT_RUN on609;605 A5 FAIL preserved. Brightness deferred. Current owner recovery availability and609 manualGUI already confirmed; do not ask these same questions again without a new reason.

## 0609已受控刷入boot_b；原300s回归与认证前链路待完成

用户本轮明确“现在可以配合恢复”，当前恢复配合已归档。已保存旧包刷新UTC，重跑全部原有门禁，只对设备c073a488的boot_b刷入609。native447edd1d920b26d92aba154e830d0f87f560b612f738b345eec37572e6ab22e5；实际boot和报告PID见native-dpu-0609/0609-flashed-preauth-pending-checkpoint.json。PID仅报告值，必须核对真实存活/终止状态，串口唯一所有者不变。

现阶段仅确认刷入，不确认安全/GUI/GPU功能。原300s安全回归、LOWDMA/NC/RAM/ZAPSTAGE（VM发布前）和原GMU/HFI前缀仍需新609实证；未排队ZAPAUTH/退出/绘制。实际PAS、CP、secure-exit与FirstRender本启动均NOT_RUN，旧608前置16拒绝及3CAP45、旧606CP、旧605A5像素失败继续保留。人工609GUI另行请求，未回复保持NOT_RUN。准备完成后按已封存受控入口一次认证→独立VM/OOB/CP/CAP43/CAP45→原postauth回归→一次退出/CAP46→原605两图/CAP38逐像素。无新支持查询、TRUST写、重试或低区回收。亮度暂缓。

## 0609恢复推进：用户当前已确认可以配合实体恢复

用户新消息“现在可以配合恢复”已单独归档native-dpu-0609/owner-recovery-available-0609.json；仅解除当前恢复配合阻塞，不等于GUI、认证或绘制验收。已复核609不可变ready checkpoint c4ec5ef9…aa4e93及受控入口6a6ee556…5e46c全部绑定文件和当前镜像，均匹配。手机仍608，609实际PAS仍NOT_RUN。现在保留旧包刷新UTC并重跑完整门禁，然后仅boot_b/c073a488受控609启动、原300s安全、预VM staging/GMUHFI前缀和一次认证。608早期拒绝与全部历史失败继续保留。后续不重复询问同一已确认的恢复条件。

## 0609后续实机因当前实体恢复配合回复缺失而阻塞

相同恢复条件已跨连续3个goal turn未解决：完整native/封包/离线门禁完成后首次等待；随后补齐并验证受控执行入口，仍等待；本轮再次实查无609 flash、无当前owner-recovery receipt、无auth/sequence目录，无后台设备操作者。已能独立完成的本阶段源码、host、native、封包与执行入口全部就绪。手机仍0608，实际PAS NOT_RUN/UNKNOWN，FirstRender仍未达成；不会因自动goal唤醒而重复测试、重发608入口或假定当前人在设备旁。

开发指南§15.2的原要求是每次刷写前确认“可恢复路径”。首次可能不返回的SMC恢复依赖实体按键；先前确认过去数小时后重新核实当前配合，是对恢复路径时效性的安全判断，不是重新索取已有开发授权。当前问题已发送，不重复发问。用户回复当前可配合后，按609已封存检查点刷新UTC及完整门禁，继续原300s→staging/GMUHFI→一次实际认证→独立资格→安全退出→原605绘制。阻塞审计gpu-zap-0609/recovery-block-audit.json；目标未完成，未标记PASS。

## 0609实机执行入口补齐，仍未刷入或调用PAS

native与镜像保持既有447edd1d…ab22e5资格，手机仍0608。新增native-dpu-0609/post-baseline-auth-exit-render-once.py：先核对实际609刷写镜像/Current/300s安全/当前恢复配合/预VM staging和GMU-HFI前缀，再单次认证→只读缓存VM/OOB/CP→独立CAP43→认证资格→原postauth回归→单次退出→原605绘制→3份严格CAP38。不会为取得CP命令收据而重复初始化VM/OOB/CP；没有PAS13支持查询。

主机控制流通过23个坏门禁拒绝、10个失败位置逐项阻断后续writer、一次入口重入拒绝；注入的主机执行即使完成也只能HOST_MODEL_ONLY，不能物理PASS。任一步失败后只尝试一次已核实不可变getter集合34/35/36/37/38/43/44/45/46，拒绝初始化/认证/RESET/SHUTDOWN或新测量命令；故障采集自身出错也不覆盖首个失败。严格代码/plan哈希见gpu-zap-0609/controlled-execution-entry-qualified.json。这些是运行脚本改动，没有改变已构建native或boot内容。

另已备好flash_regression_once_0609.py及保持每次旧镜像/retime receipt的refresh_utc_preserving_history.py，均未运行设备动作。首次609设备操作仍缺当前恢复配合回复，没有创建owner-recovery-available-0609.json，没有排队刷写/PAS/退出/绘制。回复后不要重跑0608认证入口；按新609源/镜像门禁重新启动、安全回归、staging和GMU/HFI，再运行受控入口。原FirstRender目标与各项安全要求不变。

## 0609完整native/封包/全量离线刷写前门禁PASS；等待当前实体恢复配合确认

手机仍0608，没有刷609，没有新的PAS或绘制排队。0609正式0484、463+4/5462、仅1个已有私有header，Git基d5c9c62db752f225a288b88959d3754edc94138a加精确补丁与全源清单。native447edd1d920b26d92aba154e830d0f87f560b612f738b345eec37572e6ab22e5，HIDfc55436750875b2fb156b4dbbed5702616e6de0d4c74779ade8544cf7094f014。Mac builder77123已确认结束并配对，全5462源前后相等、54exports/7实际编译、原ABI/HID207/touch/cache/NC/RAM/CR0/资源/CPUstage/协议46/原424绘制字/newauth/newexit/锁作用域机器码11类证明通过。旧脚本部分stdout仍含历史版本计数标签；以相应JSON和绑定哈希为准，实际回放463+4/5462。

锁缺陷的原生产cold-check wrapper+NativeCheck依赖红测完全复现0608 result16/phase0/transaction0/SMC0/begin0；移出外层VRM后同一路径绿测，29认证运行变异、16退出运行变异、两组各2非法低地址访问变异和ASan/UBSan均通过。489/423/512坏证据拒绝；明确所有主机SMC/GPU/像素fixture为synthetic。原快照每次读取仍检查原cold owner，没有改SCM等待/重试、MMIO、电源或安全门禁。

实际prelink/payload/100663296B boot封包、原Root/nonce/bank/Data/ABI/watchdog、原静态和运行owner测量、全部ADT/文件回读及完整离线flash-preflight均通过。严格不可变检查点native-dpu-0609/0609-native-package-pending-recovery-checkpoint.json，source/runtime入口gpu-zap-0609/runtime_0609_provenance.py、source-scope.json，actual-source private-header范围已固定。UTC已刷新做离线验证，用户返回后很可能超过300s；必须保存当前包/retime receipt、重新只刷新UTC及依赖owner测量，再跑完整门禁，绝不绕过时间窗。

0608实际有限资格仍是安全304.475s、CPUstage和GMU/HFI前缀，CP本启动NOT_RUN，旧606CP与0590FirstExecution单独保留。唯一ZAPAUTH被前置嵌套VRM锁16拦住，3份CAP45及原始wire全存，INIT/MEM/AUTH全部attempted/returned/attempts0，无HYP所有权调用，不能叫认证失败或成功。该一次入口已消耗，不重发。0608人工GUI仍未收到回复，NOT_RUN；609尚未启动，GUI同为NOT_RUN。608有限资格sha779cd9a3b2f9b0091c88b7b3478b16b1dfacd3e653d9ee88ae7dad9363b00966。

用户早先明确可以实体恢复，但已过去数小时；依据开发指南§15.2，首次实际PAS前已异步询问当前是否仍可配合，尚未回复。未创建609 owner-recovery-available文件，未排任何设备命令。回复后先按现有boot_b/唯一串口/UTC门禁启动609并跑原300s、LOWDMA/NC/RAM/ZAPSTAGE→原GMU/HFI前缀，再一次ZAPAUTH（本版内部原VM/OOB/CP）。只在实际认证独立PASS后做postauth原回归、一次ZAPEXIT及3CAP46，最后原605两图424字不变绘制、3CAP38schema6/RPTR248/460/Fence3/4/完整BO逐像素。任何不确定PAS返回不自动重试、SHUTDOWN、释放或CPU访问冻结区域。主目标FirstRender尚未达成，PAS13支持查询路线关闭，亮度暂缓。

## 0608真实认证前拒绝已封存；0609单header修复锁作用域

手机0608。原安全304.475s、LOWDMA/NC/RAM/ZAPSTAGE和原GMU/HFI前缀PASS；608本次未发布GPU VM，CP/IB/Fence本启动NOT_RUN，历史0590FirstExecution/0606CP回归继续独立保留。人工608GUI尚未回复，NOT_RUN。唯一ZAPAUTH命令得到3份一致CAP45：result16/phase0/transaction_started0，INIT/MEM/AUTH全部attempted/returned/attempts0，metadata/image_frozen0；没有进入实际PAS调用，更不是认证失败或成功。严格资格native-dpu-0608/0608-safety-staging-GMUHFI-prePAS-refusal-qualified.json sha779cd9a3b2f9b0091c88b7b3478b16b1dfacd3e653d9ee88ae7dad9363b00966。0608一次入口已消耗，不重发、不清空或覆盖失败。

已定位源调用链：kebab_rpmh_gpu_zap_prevm持VRM globalbusy→kebab_a650_zap_prevm_locked→KFSnapshot→KFR32/KFCheck→KFNativeCheck→kebab_rpmh_gpu_cold_check再次获取同锁，立即16。旧主机snapshot peer直接返回work，没有这条实际检查路径；补入真实cold-check wrapper和NativeCheck后，改前完整复现实机16/phase0/SMC0/begin0并exit42。此前commentary把错误猜为SCM争用过早，已明确纠正；未按此猜测加入等待或重试。

0609隔离candidate只改既有KebabNativeA650ZapAuth.h：规范wrapper仍固定父资源并检查10范围/45对/260页，保持PAS namespace排他；等外层VRM释放后，再原样调用原KFSnapshot两次，完整5CB Fault/所有配置比较后才记quiet_samples2。初始和SCM token内fresh复核都如此。原每次读取的cold-owner检查仍运行，没有关闭门禁、改MMIO、SCM重试或图像命令。主机同一路径修后result0/phase11/fakeSMC3/begin1，明确synthetic；7实际SDK单元通过，生产Mac仍608，正式0484/463+4/5462/native/package尚待。

恢复入口gpu-zap-0609/0609-development-checkpoint.json、source-scope.json、fix_prevm_snapshot_scope.py、native-host-inputs/auth-model.cpp；red/green原日志在Ubuntu /home/albert/gpu-zap-0609。继续原生auth/exit host全套和变异→正式构建→封包门禁→新受控boot→原300s安全→一次认证资格→独立安全退出→原605两图逐像素。当前没有后台或排队认证、退出、绘制命令；FirstRender仍未通过。不得把下一版从608继承的有限资格扩大成CP实机PASS。亮度暂缓，PAS13支持查询路线关闭。

## 0608已实际刷入boot_b，原300秒与认证前链路运行中

设备c073a488，boot_b；native15fba3848da269e995594201a147b0bcc876868e460c33696af9a27838844290，实际boot3153d8cc9f073110b4c19f92ac610ef40f7c9563038b3fde03bab49c59f4d056。严格UTC300s及全部源/ABI/原owner/ADT/Root/Data/watchdog门禁通过后刷入，capture77076，baseline观察88284/51136；恢复时必须核对真实进程，不能仅信PID。首次完整预检缺608AKS ABI报告已保留并补跑，第一次实际刷写调度在任何设备命令前被短暂conda-hook Python进程挡住；等待该进程退出并使用无profile shell后按原唯一进程门禁通过，未放宽。

当前原300s安全回归尚待完成。新608显示/点击/返回手势已请求人工验收，未收到回复前保持NOT_RUN。自动队列仅LOWDMA/NC→RAM→ZAPSTAGE（VM发布前）→原GMU/HFI前缀，没有排队PAS调用。实际INIT/MEM/AUTH、退出与绘制均NOT_RUN，PAS13支持查询不再运行；0605像素A5失败继续保留。

实际检查点native-dpu-0608/0608-flashed-preauth-pending-checkpoint.json；新受控入口exercise-zap-lifecycle-once.py、qualify-actual-zap-auth.py，必须先完成并核对所需真实父链。未因封包或刷入声称认证或FirstRender通过。首次实际所有权调用只允许一次；失败保留现场、保护/冻结/隔离状态及所有原始日志，不自动重试、释放、SHUTDOWN或写TRUST。亮度继续暂缓。

## 0608正式native与封包完成，手机仍0606；实际认证未执行

源父0607、运行和实机父0606。正式0483，462+4/5462，13路径/2私有header；完整回放、逆向恢复、7实际SDK单元及54export通过。Mac builder63014已结束，native/HID已配对，全源前后相等。native15fba3848da269e995594201a147b0bcc876868e460c33696af9a27838844290。原ABI/HID207/touch/cache/NC/RAM/CR0/资源/CPUstage/协议max46、新auth和secure-exit实际机器码均通过。

0608原生退出body主机ASan/UBSan、16实际编译运行变异和2非法低地址访问变异通过。新增完整SMMU提交前门禁：5个CB故障先红，修复后全部在触及Ring前拒绝，并与认证完成时全配置比较；提交后复核保留。旧变异自身空指针、最初审计把机器码地址排序当执行顺序及末尾guard寄存器分配差异均为测试/审计错误，历史结果保留，没有为通过它们改硬件行为。Core12149/18变异、原424绘制字节等价和native独立解释通过。CAP45/46 decoder分别489/423拒绝，CAP38 schema6/起点36与真实退出父绑定512拒绝；全部主机fixture明确synthetic。

实际608prelink/payload/boot封包和原始安全/Root/Data/原ABI/完整静态及运行owner/全部ADT检查通过，尚未刷写。严格独立检查点native-dpu-0608/0608-native-package-pending-device-checkpoint.json；source与runtime入口gpu-zap-0608/runtime_0608_provenance.py。第一次草案flasher误留606 native绑定，已在离线阶段保留并从最新608已登记的原始flasher重派生，移植实际修正过的606精确两属性归一化，仍保留完整树等价和两层测量。不是绕过门禁。

用户已明确回复可以在需要时配合实体按键恢复（call_9684a569471b40cba71a232f75e26e7e），无需重复询问同一许可。后续仍执行UTC300s、boot_b/c073a488及唯一串口门禁；新608人工GUI保持NOT_RUN。0606原304.367s安全、CPUstage和GPUCP/GUI全部PASS；0590 FirstExecution PASS、0605原图A5失败保留。PAS13支持查询路线关闭，实际INIT/MEM/AUTH仍NOT_RUN/UNKNOWN，HYP和TRUST寄存器安全位UNKNOWN，FirstRender尚未通过。

下一步：受控608启动→原300s安全/人工GUI→LOWDMA/NC/RAM和ZAPSTAGE（GPU VM发布前）→原GMU/HFI前缀→一次ZAPAUTH（同worker/token内部复用原VM/OOB/CP）→3份CAP45和独立CAP43/VM/OOB/CP及原安全回归。仅明确认证PASS后一次ZAPEXIT→3份CAP46硬件独立Fence3/globalFence2/RPTRShadow36/guard/Fault→原605两图424字不变的GPURENDER→CAP38schema6/RPTR248/460、Fence3/4、完整BO和逐像素检查。不重复可能改变HYP所有权的调用，不CPU访问冻结内存，不释放/重试/写TRUST。607禁止封包刷写，避免仅为添加退出入口重启重复认证。亮度继续暂缓。

## 0608隔离开发中：首次认证前备齐同启动secure-exit与原绘制入口

手机仍0606且实际PAS NOT_RUN/UNKNOWN，FirstRender仍0605 FAIL。Mac/Ubuntu正式源仍0607（0482/461+4/5460），builder49759已结束，native b80ad78b70a6e79061709eec3e2ddd90b8681e2d6d5424f9f6e3ed462903ba25；607所有原ABI/HID/cache/NC/RAM/CR0/资源/原424words/CPUstage/协议max45、新auth实际机器码及CAP45 decoder489坏输入均已通过。NC/RAM/Stage机器码审计因新增frozen守卫引起寄存器变化的旧拒绝已保存，并增加实际frozen全局加载/拒绝证明；未改驱动迁就审计。

607不封包/不刷写：它只有认证入口，先上机后为添加secure-exit代码而重启会重复改变PAS/HYP所有权。608以607为SOURCE父、606为RUNTIME/PHYSICAL父，不能虚构607实机PASS。新隔离candidate13文件/2私有header，尚无正式0483/native/镜像。保留607全部源/构建/失败证据，不重新apply/build/pair707。

608最小secure-exit9字：CP_SET_SECURE_MODE(0)+原7字KCPFenceWords(sequence3)，仅重定向完成地址到既有NC FenceBO+500，CPU只写不同sentinel和保护区，GPU写3。原全局Fence2保留，RPTR/Shadow27→36；必须实际Fence3、RPTR/Shadow36、idle、无Fault和保护区正确，原200ms/有限2000poll/一次提交/触后失败隔离。无SMC/TRUST/新电源/MMIO地址/新BO。原0605两张图的424命令字预期保持逐字节不变，只将Ring起点36与CAP38 schema6明确区分；完整字节等价和native body host尚待验证。实际安全寄存器位仍UNKNOWN，不将packet完成扩大成像素成功。

Core host12149断言、ASan/UBSan、18个实际编译运行变异通过，含独立RPTR滞后/Shadow滞后/Fence滞后/时钟不推进有限poll。早期测试把允许idle位当错误、遗漏RPTR单独滞后样本导致变异逃逸，均保留后补测；首次SDK缺显式kern路径已修正，第二次7实际SDK单元通过。生产Mac树未变。精确检查点gpu-zap-0608/0608-development-checkpoint.json；设计入口candidate-note.md、source-scope.json、KebabA650SecureExitCore.h、candidate/iokit/Kernel/arm/KebabNativeA650SecureExit.h。继续实际native body和schema6/46的host/decoder→正式源/构建→完整封包和恢复门禁，再一次启动完成独立认证资格、安全模式退出及原负载FirstRender。首次真实所有权变更前确认人工恢复可用，绝不自动重复认证。亮度暂缓。

## 0607完整native构建完成，尚未封包或上机

正式0482，461+4/5460，9文件/2新私有header，Git d5c9c62db752f225a288b88959d3754edc94138a。三方PAS13差异审计和四份原DT核对完成；preVM INIT/MEM/image-only复制→冻结→同一SCM token/线程复用原VM/GX/OOB/CP→AUTH→复核。旧generic slot1排他保留并增加永久frozen拒绝；没有新电源参数、TRUST、secure-exit、绘制或支持查询。HYP保护与GPU安全模式仍UNKNOWN。

纯协议2307断言/22运行变异；实际native body主机124908断言、ASan/UBSan、25运行变异、2个非法CPU访问变异被mprotect以SIGSEGV拦截，覆盖全部5CB有效Fault位的认证前后100种注入。一次变异揭示实际DSB执行计数测试缺口，补测后全通过，早期失败原样保留。CAP45 12KiB正常协议/CRC/6变异通过；native body host evidence10520B、protocol448B、SMMU六快照offset728，始终明确SMC/HW peers为模拟。两次草案及实际应用后7个SDK单元、461+4完整回放、0482正反向、54真实exports及5460全源前后一致通过。

Mac builder49759已实际结束并配对。native19537504B SHA-256 b80ad78b70a6e79061709eec3e2ddd90b8681e2d6d5424f9f6e3ed462903ba25；HID309608B SHA-256 fc55436750875b2fb156b4dbbed5702616e6de0d4c74779ade8544cf7094f014。原20H392 identity/classB/FairPlay ABI和通用GPU linked检查已通过。一个本地主机审核因尚未取回export收据而拒绝，取回真实收据后通过，未改驱动。准确检查点native-dpu-0607/0607-native-build-pending-package-checkpoint.json。不得重新apply/build/pair；Mac与Ubuntu当前源为0607，手机仍0606。

剩余：CAP45独立decoder/坏输入与原始父样本绑定、新auth实际机器码资格、原HID/cache/NC/RAM/CR0/资源/协议/ADT及runtime来源、prelink/封包/UTC/完整刷前门禁。实际认证NOT_RUN/UNKNOWN，FirstRender仍0605 FAIL。未封包、未刷写、未执行任何PAS。首次实际认证前确认人工按键恢复可用，不自动重试。成功后独立PASRANGE/CAP43提供认证后真实Fence/RPTR/idle证据；CAP45同时保存完整SCM返回与6份原始SMMU。之后执行原安全回归及新人工GUI，secure-exit和0605原绘制另行资格。保持长期目标A650_FIRST_RENDER=PASS，亮度暂缓。

## 0607开发中：PAS13三方源码审计及preVM单token生命周期主机验证

实机仍0606，严格基线0ac87e110f5e7f08a031d746dad37ad546e92aaa007f6458852c26c5390d5def、native7336f504bd08ed7bc92d3e942c42181c34eded1280b93180fd73da9aa29cd607；304.367秒安全/人工GUI/CPU staging/原GPUCP限定通过。实际ZAP认证NOT_RUN/UNKNOWN，FirstRender仍0605 FAIL（A5像素）。没有新的PAS或绘制实机命令。

新增固定alioth f188378及其2026-10-31证据、Linux2019原补丁与固定v6.12、SM8250 vendor 7c2b1aed差异矩阵，四份原DT PAS13节点逐一核对。alioth没有PAS_IS_SUPPORTED前置，查询路线继续关闭；其secure0是wait返回值，不是直接安全寄存器测量，FSR400不能单独当有效SMMU Fault。源码32位PIL约束不冒充实测aperture。vendor SCM带宽包装与Linux SM8250无可选clock/ICC配置的差异明列，禁止猜投票或移植板级常量。

此前postCP全流程host候选保留但不用于native：MEM_SETUP后的image复制与现有preVM门禁冲突。新候选在同一普通线程、同一既有SCM token内：preVM原namespace门禁→INIT→MEM_SETUP→image-only复制/补零/读回→冻结low memory并仅释放临时namespace串行化→复用原VM/GX/OOB/CP→实际AUTH→原CP/Fault复核→SCM end。禁止INIT后CPU触碰metadata；不能重跑旧Stage、复用共享桥、重试AUTH、写TRUST或削弱slot1门禁。必须先完成native owner/锁顺序/原初始化复用及linked机器码资格，当前尚无正式0607补丁/构建/镜像/刷写授权门禁结果。

主机新状态机2307断言、ASan/UBSan和22个实际编译运行变异通过；旧postCP方案接收preVM输入实际运行红42。只证明synthetic callbacks协议，不证明硬件认证/电源/所有权/渲染。源码与收据在本地gpu-zap-0607，公共副本docs/qualification/0607-source及docs/GPU-PAS13-LIFECYCLE-0607.md。继续native开发、原构建门禁和受控单次实机资格；最终目标A650_FIRST_RENDER=PASS，随后IRQ/20H392 Metal。亮度暂缓，0607人工尚NOT_RUN。

## 0606限定验收通过：原安全/GUI、精确ZAP CPU staging和原GPUCP

原安全304.367秒、人工显示/点击/返回全部正常、完整GPUCP及原后缀/SN11回归通过。只执行一次ZAPSTAGE；三份256B CAP44一致 SHA-256 c1800102f350ac1786e9b9f4670b144559b98297b3ef4f31d2c6f61e4ebf0760。实际260页检查、1,064,960字节复制及逐字节完整读回、两次DSB、元数据16KiB和image1MiB完整补零/hash全部通过，233769us。两份全区域SHA分别4263302f407618c9a2a0fed8377b94144b6e08d3aa1522c60325dcfeb41cdba6 / 9b51c355b327a21056ef63a350f6e7cb3d4132cc25a8676a1c96fba96046aeef。旧generic PAS13 namespace slot1门禁原样保持；测试发生于GPU VM发布前，没有PAS INIT/MEM/AUTH、secure-exit或TRUST写入。实际认证仍NOT_RUN/UNKNOWN，PAS13支持查询路线继续关闭。606没有重复绘制，FirstRender仍605 FAIL。

严格收据native-dpu-0606/0606-staging-and-GPUCP-physical-qualified.json SHA-256 0ac87e110f5e7f08a031d746dad37ad546e92aaa007f6458852c26c5390d5def，90个raw样本hash重核；native 7336f504bd08ed7bc92d3e942c42181c34eded1280b93180fd73da9aa29cd607，boot 08a18902ab801abfaa2726d5c39d7701218cac7c5038d18270b0141bd2226414，460+4/5458、Git d5c9c62db752f225a288b88959d3754edc94138a。人工独立收据1f9e5ae0b22604b0cbaf6f6d7608a855d54949fbb49687d1614b622121b1e9e1。新ZAP属性完整static/runtime owner测量与原排除表保持；历史换行/fixture/刷前完整树比较拒绝及UTC过期记录均保留后修复，原全树其它内容一致性断言仍在，17坏归一化输入拒绝。亮度暂缓。

下一0607尚未实现：Separate canonical postCP secure-memory/SCM lifecycle lease; preserve old generic slot1 exclusion. Before any PAS call prove exact owners, fresh idle/fault/interval/NC checks and source-backed power/ABI. Once INIT is attempted do not touch metadata; image loading after successful MEM_SETUP must follow pinned MDT lifecycle and retain all memory on uncertainty. No support-query repetition or TRUST fallback.。606的CPU staging不授予PAS地址aperture资格或安全内存lease，也不证明认证通过。固定平台CP init→ZAP load→合法secure-exit来源见605/A650-mandatory-secure-exit-source-qualified.json。继续完成必要SCM所有权和生命周期，并以实际结果推进FirstRender；GPU IRQ/20H392 Metal后续验收。当前全部观察者结束，恢复先核对真实进程；已完成606的apply/build/flash/worker不得重跑。

## 0606已刷入：精确ZAP输入隔离CPU staging，等待原安全回归

正式0481为7文件（2个新私有header），460+4/5458，Git d5c9c62db752f225a288b88959d3754edc94138a。605硬件18项绘制寄存器全部与提交值相同、Fence3/RPTR239/CP控制及CCU完成，但像素仍全A5，第二pass未提交；严格收据ddfaefa170653b7cd80406052553d6d9cf633077b6b0166942a9ed669757279d，原失败隔离保持。固定vendor/Linux源码明确CP init后ZAP装载及合法secure-exit为平台启动步骤，本次按用户源码例外审查必要前置，不重复IS_SUPPORTED或把false解释为认证失败。

606在GPU VM发布前，沿原样的KF_OP_PAS13_SEPARATE_IMAGE/slot1门禁，把当前SecureDT中的精确6860B元数据与1676B load单次snapshot/hash校验，复制到已验收的独立Normal-NC metadata16KiB/image1MiB，逐字节校验完整内容/补零、260页实际翻译和两份全区域SHA。原输入及分配保持；2秒协作预算、一次claim、失败证据、CAP44冻结256B收据。没有PAS/SMC、secure-exit、TRUST、IRQ、电源或SMMU写入；实际认证NOT_RUN/UNKNOWN。新属性均纳入完整static和seed-wipe runtime ADT测量，原排除表不变。Host1068799断言、ASan/UBSan、13运行变异、6协议变异、179坏decoder输入、4坏ADT输入、7实际SDK单元、完整native/54exports/5458源清单、新worker/gate/GPR字节写读/DSB/AT-PAR/digest/CAP44机器码及原ABI/HID/cache/封包门禁通过；这些不代替实际staging。

设备c073a488/boot_b Sending/Writing/reboot均OKAY。native 7336f504bd08ed7bc92d3e942c42181c34eded1280b93180fd73da9aa29cd607，boot 08a18902ab801abfaa2726d5c39d7701218cac7c5038d18270b0141bd2226414。capture 87760，collector 75744，finalizer 70340，唯一post 90308。恢复先核对实际进程，勿重复apply/build/flash/worker。原300秒安全后LOWDMA/NC/RAM→一次ZAPSTAGE/三CAP44→完整GPUCP/后缀/SN11；606无绘制重试，FirstRender保持605 FAIL。人工606尚NOT_RUN，605全部正常收据5904868e41f5fc603e10b3dd5b37271559ae910a8947ce7ce9ae9f505785b93c；亮度暂缓。

检查点native-dpu-0606/0606-flashed-exact-ZAP-staging-pending-checkpoint.json SHA-256 3de59acb73137e8f5c1a5a886b0858144daa05768a10364c6573555f4adb8d9e。下一入口normal-baseline-qualified.json、live-ZAP-stage/result.json、post-sequential-output.log、live-gpu-cp/result.json。实际staging通过后继续独立postCP所有权/SCM生命周期及经认证的secure-exit，先完成源码/host/native门禁，旧generic slot1排他不放宽。硬目标仍A650_FIRST_RENDER=PASS，GPU IRQ和20H392 Metal/Turnip后续推进。

## 0605同页CP写入诊断完成：FirstRender=FAIL

原安全304.476秒和原GPU/CP/后缀回归通过。只有一次GPURENDER，三份CAP38一致SHA-256 8558766d5dadfed7961da071ea41ff800c33805bb0add722a28c6c2cd72aa479。第一目标同页CP控制可见=True；实际图像摘要及逐像素状态：[{"pass_index": 0, "submitted": true, "captured": true, "BO_sha256": "c698b0cead6b55a34759a3a06a19e24b21a81629626f984175c353d5d2b6cc39", "same_page_CP_control": true, "observed_control_word": "0xa650c001", "image_bytes": 4096, "image_sha256": "f600eca824e84a43f0691b267bd620e462c50da165c5b80e17aecb7a924f1fa8", "all_pixels_A5": true, "terminal": {"wptr": 239, "rptr": 239, "shadow": 239, "fence": 3, "marker": 2790263821, "status": 1, "rbbm_irq": 0, "irq_mask": 0, "cp_fault": 0, "cp_irq": 0, "cp_protect": 0, "ahb_fence": 0, "gfsr": 0, "fsr": 1024, "sqe": 1, "timestamps": [1111458326, 1111458331, 1111458432]}, "diagnostic_registers": ["0xf03080", "0x100010", "0x1f001f", "0xf03080", "0x0", "0x30", "0x90010100", "0x0", "0x4", "0xff", "0xff", "0x0", "0x0", "0xf180", "0x4100000", "0x18000000", "0x10", "0x2"], "diagnostic_fresh": true, "first_mismatch": 256}, {"pass_index": 1, "submitted": false, "captured": false, "same_page_CP_control": null}]。CP写入标记不是绘制像素，不扩大为RB/CCU执行成功。原Fault/超时/隔离保留，不重试writer、不清现场。本次0605人工状态见独立收据（未收到回复则NOT_RUN），亮度暂缓。

严格记录native-dpu-0605/0605-render-witness-physical-qualified.json SHA-256 ddfaefa170653b7cd80406052553d6d9cf633077b6b0166942a9ed669757279d，90个raw样本hash重核；native c91c1055aa524728f9bd0c7de9af426c437cf3041184082da83f67e093a402bd，boot c7fe690514f93fa4def00ca886fdb45dc45741966170c8fd35df01cf9c44af59，459+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a。所有原始CAP38、wire/log、BO和截取像素均保存，0591历史不改。PAS13重复支持查询路线已关闭，本轮未查询，ZAP实际认证NOT_RUN/UNKNOWN。

下一根据本轮实际结果推进：Actual18 fixed CP_REG_TO_MEM measurements preserved. Continue from observed register/address/format/pitch state and raw BO; preserve existing target pixel/guard validation. Review remaining mandatory platform prerequisites only with concrete source evidence. Keep CP control separate, preserve fault isolation; no repeated PAS query or unsupported ZAP inference.。605已包含source-backed固定18项寄存器采样，保留604初始RB ECO与RB/GRAS命令模式；若仍失败，不重复相同测试，继续核对剩余2D restore/模式/cache与原始BO，不得无证据转回ZAP或盲写TRUST。

## 0604与0605人工验收独立补充

0604人工GUI补充：用户明确确认SpringBoard显示、点击打开应用、返回主屏手势全部正常，亮度暂缓。独立收据native-dpu-0604/0604-manual-GUI-owner-confirmed.json SHA-256 145079d5cf4ba9b89f369c97eca9c00918856b15472f1244ccde2ecd45ca4ea0。与安全回归及GPU像素验收分别归档；旧严格记录中的等待字段保持历史原样。

0605人工GUI补充：用户明确确认SpringBoard显示、点击打开应用、返回主屏手势全部正常，亮度暂缓。独立收据native-dpu-0605/0605-manual-GUI-owner-confirmed.json SHA-256 5904868e41f5fc603e10b3dd5b37271559ae910a8947ce7ce9ae9f505785b93c。与安全回归及GPU像素验收分别归档；旧严格记录中的等待字段保持历史原样。

## 0605已刷入：固定18项A650绘制寄存器采样，等待安全回归

正式0480仅4个Render header，459+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a。原6042D命令与CP控制完整保留，在末次CCU clean/WFI后新增18个标准CP_REG_TO_MEM到已归属Normal-NC Fence BO的0x300/0x400空闲区，以及CP_WAIT_MEM_WRITES；原硬件Fence、逐像素/guard、Fault/超时/隔离/无retry保留。212字/pass、WPTR239/451、CAP38 schema5/136512B。主机1634断言、旧604编译红、ASan/UBSan、22编译运行变异、505坏decoder输入、7实际SDK单元、完整native/54exports/5456源清单、实际ARM64构造424命令字及18个volatile NC诊断复制、原ABI/HID/cache/封包门禁均通过。换行回放拒绝和主机fixture编译失败已保留后修复；不改硬件失败记录。

设备c073a488的boot_b Sending/Writing/reboot均OKAY。native c91c1055aa524728f9bd0c7de9af426c437cf3041184082da83f67e093a402bd，boot c7fe690514f93fa4def00ca886fdb45dc45741966170c8fd35df01cf9c44af59。capture 88424，collector 77500，finalizer 86620，唯一顺序post 87288。恢复先核对实际进程，勿重复apply/build/flash/writer。原300秒安全及capture退出后LOWDMA/NC/RAM→完整GPUCP/原后缀/SN11→一次绘制/三CAP38。人工605尚NOT_RUN，亮度暂缓。604安全304.429秒、CP控制/Fence3/RPTR166通过，但像素全A5，第二pass未提交；失败隔离和原始证据保持。604人工尚未收到回复。

检查点native-dpu-0605/0605-flashed-fixed-register-diagnostics-render-pending-checkpoint.json SHA-256 d98733e7d889358fbf19da78270182e9affd2b4c2f0512c14b56d76a6373f1de。PAS13重复查询路线关闭，ZAP实际认证NOT_RUN/UNKNOWN，GPU IRQ/Metal未验收。硬目标仍A650_FIRST_RENDER=PASS。下一从live-gpu-render/result.json实际寄存器及原始BO定位；若失败继续最小源码核实修复，不以诊断值或CP标记替代像素。

## 0604同页CP写入诊断完成：FirstRender=FAIL

原安全304.429秒和原GPU/CP/后缀回归通过。只有一次GPURENDER，三份CAP38一致SHA-256 2521cd95cb2b1df988c8a7481d54e25826eac39df404a54604b2891a4dbeef2d。第一目标同页CP控制可见=True；实际图像摘要及逐像素状态：[{"pass_index": 0, "submitted": true, "captured": true, "BO_sha256": "c698b0cead6b55a34759a3a06a19e24b21a81629626f984175c353d5d2b6cc39", "same_page_CP_control": true, "observed_control_word": "0xa650c001", "image_bytes": 4096, "image_sha256": "f600eca824e84a43f0691b267bd620e462c50da165c5b80e17aecb7a924f1fa8", "all_pixels_A5": true, "terminal": {"wptr": 166, "rptr": 166, "shadow": 166, "fence": 3, "marker": 2790263821, "status": 1, "rbbm_irq": 0, "irq_mask": 0, "cp_fault": 0, "cp_irq": 0, "cp_protect": 0, "ahb_fence": 0, "gfsr": 0, "fsr": 1024, "sqe": 1, "timestamps": [1105686741, 1105686746, 1105686841]}, "first_mismatch": 256}, {"pass_index": 1, "submitted": false, "captured": false, "same_page_CP_control": null}]。CP写入标记不是绘制像素，不扩大为RB/CCU执行成功。原Fault/超时/隔离保留，不重试writer、不清现场。人工602状态见独立收据（若未存在则NOT_RUN），亮度暂缓。

严格记录native-dpu-0604/0604-render-witness-physical-qualified.json SHA-256 86f8a1342bd7899ede807892170baadcf64bc8a3eccf4a8ee6291b45958b72f5，90个raw样本hash重核；native 4ba45bdca829c7ea4a731a5792a52932d237490cbd7f4f7b6a6b45b962974639，boot d5cb9d54351cea9f222c5bc7e50775f8669cbc2f2158ce8fcb45e1744fa0bb43，458+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a。所有原始CAP38、wire/log、BO和截取像素均保存，0591历史不改。PAS13重复支持查询路线已关闭，本轮未查询，ZAP实际认证NOT_RUN/UNKNOWN。

下一根据本轮实际结果推进：A650 RB/GRAS modes submitted; if pixels still fail, inspect actual2D register state with fixed source-backed CP_REG_TO_MEM to owned fence scratch, preserve existing target pixel/guard validation. Review remaining mandatory platform prerequisites only with concrete source evidence. Keep CP control separate, preserve fault isolation; no repeated PAS query or unsupported ZAP inference.。604已包含source-backed初始RB ECO与RB/GRAS命令模式写；若仍失败，不重复相同修复，继续核对剩余2D restore/模式/cache与原始BO，不得无证据转回ZAP或盲写TRUST。

## 0604已刷入：A650 RB/GRAS命令模式恢复，绘制等待安全回归

正式0479仅RenderCore/Binding两个header，458+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a。固定Mesa63350a的A6XX初始context restore分别写RB_MODE_CNTL=0x10（8811/有效6:4）和GRAS_MODE_CNTL=2（8110/有效1:0）；只新增这一组4字命令模式。保留603初始ECO、602真实同页CP控制、原始0591 2D/逐像素/guard、Fault/超时/quarantine/无retry。139字/pass、WPTR166/305；CAP38 schema4/135784B。1378主机断言、旧603编译红、ASan/UBSan、15运行变异、323坏decoder输入（含两快照全部5CB故障位拒绝）、7实际SDK单元、完整native/54exports/5456清单、实际vector加载保存ECO+模式机器码、原ABI/HID/cache/封包门禁均通过。另复核591/602/603失败快照全部5CB和global前后相同且有效Fault位0，原始FSR0x400保留；成功路径原有VM decoder已覆盖全部5CB，不放宽门禁。没有SMC/PAS/TRUST/IRQ/电源/SMMU改动。

设备c073a488/b Sending/Writing/reboot均OKAY。native 4ba45bdca829c7ea4a731a5792a52932d237490cbd7f4f7b6a6b45b962974639，boot d5cb9d54351cea9f222c5bc7e50775f8669cbc2f2158ce8fcb45e1744fa0bb43。capture PID 84764，collector 59776，finalizer 71312，唯一顺序post 92116。恢复先核对实际进程，勿重复apply/build/flash/worker。原300秒安全且capture实际退出后LOWDMA/NC/CPU_RAM→完整GMU/HFI/VM/GX/OOB/CP/原后缀SN11→一次GPURENDER/三CAP38。人工604尚NOT_RUN，最新603显示/点击/返回全部正常已独立收据54824c8adba437a00d98966fa4995676ada3283f07c8a5d75ab320d0193f7522，亮度暂缓。

检查点native-dpu-0604/0604-flashed-RB-GRAS-modes-render-pending-checkpoint.json SHA-256 354535ea27ce2c2d423ec9edcfa8505ec5a85e453088c23edec9707a7ce5bcdd。入口normal-baseline-qualified.json、post-sequential-output.log、live-gpu-render/result.json。603严格记录05f9cfd8bb3145b17f977237a32f54e37636940242f58afa64e34a669a140f69：ECO命令已提交，CP控制a650c001可见/Fence3/RPTR162/CCU完成，但像素仍A5，第二pass未提交，隔离保留。PAS13查询路线关闭，ZAP实际认证NOT_RUN/UNKNOWN。FirstRender硬目标尚未达到，不能以CP控制或GUI代替像素。

0603人工GUI补充：用户明确确认显示/点击打开应用/返回主屏全部正常，亮度暂缓。独立收据native-dpu-0603/0603-manual-GUI-owner-confirmed.json SHA-256 54824c8adba437a00d98966fa4995676ada3283f07c8a5d75ab320d0193f7522。严格记录中的等待字段保持历史原样，实际像素FAIL及隔离状态不变，不将人工GUI或CP标记当GPU绘制。

## 0603同页CP写入诊断完成：FirstRender=FAIL

原安全304.434秒和原GPU/CP/后缀回归通过。只有一次GPURENDER，三份CAP38一致SHA-256 d9832262dac8b4626ce1ffbbf4d1163b8b1b65343a599a007ddcfa9831f3c557。第一目标同页CP控制可见=True；实际图像摘要及逐像素状态：[{"pass_index": 0, "submitted": true, "captured": true, "BO_sha256": "c698b0cead6b55a34759a3a06a19e24b21a81629626f984175c353d5d2b6cc39", "same_page_CP_control": true, "observed_control_word": "0xa650c001", "image_bytes": 4096, "image_sha256": "f600eca824e84a43f0691b267bd620e462c50da165c5b80e17aecb7a924f1fa8", "all_pixels_A5": true, "terminal": {"wptr": 162, "rptr": 162, "shadow": 162, "fence": 3, "marker": 2790263821, "status": 1, "rbbm_irq": 0, "irq_mask": 0, "cp_fault": 0, "cp_irq": 0, "cp_protect": 0, "ahb_fence": 0, "gfsr": 0, "fsr": 1024, "sqe": 1, "timestamps": [1157873376, 1157873381, 1157873484]}, "first_mismatch": 256}, {"pass_index": 1, "submitted": false, "captured": false, "same_page_CP_control": null}]。CP写入标记不是绘制像素，不扩大为RB/CCU执行成功。原Fault/超时/隔离保留，不重试writer、不清现场。人工602状态见独立收据（若未存在则NOT_RUN），亮度暂缓。

严格记录native-dpu-0603/0603-render-witness-physical-qualified.json SHA-256 05f9cfd8bb3145b17f977237a32f54e37636940242f58afa64e34a669a140f69，90个raw样本hash重核；native 7f828fc6de9413dceb0bb596e31265c78692592106bd140ba62a93ddd0dcdcdd，boot 534f56058e776e4797cd9de3d25728dd7681fdf07d25d4d9ff92db36058e03fa，457+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a。所有原始CAP38、wire/log、BO和截取像素均保存，0591历史不改。PAS13重复支持查询路线已关闭，本轮未查询，ZAP实际认证NOT_RUN/UNKNOWN。

下一根据本轮实际结果推进：Initial A650 RB ECO submitted; if pixels still fail, inspect remaining2D restore/mode/cache state and raw BO. Keep CP control separate, preserve fault isolation; no repeated PAS query or unsupported ZAP inference.。603已包含source-backed初始RB ECO写；若仍失败，不重复相同修复，继续核对剩余2D restore/模式/cache与原始BO，不得无证据转回ZAP或盲写TRUST。

## 0603已刷入：A650初始RB ECO补齐，绘制等待安全回归

正式0478仅RenderCore/Binding两个header，457+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a。依据固定Mesa63350a A650初始restore，在602真实同页CP标记后新增WFI/TYPE4 RB_DBG_ECO_CNTL=0x04100000共3字；正常/blit相等只跳过模式切换，原始初始写不能省。原2D与CP控制、逐像素/guard、Fault/超时/quarantine/无retry保持。135字/pass，WPTR162/297，CAP38 schema3/135752B。1370主机断言、旧602编译红、ASan/UBSan、11运行变异、305坏decoder输入、7实际SDK单元、完整native/54exports/源清单、实际vector加载并保存WFI/8e04/04100000机器码、原ABI/HID/cache/封包门禁通过。首次派生工具残留parent601/common456，已在源码apply前拒绝，原工具保留后修成parent602/common457；驱动未为迁就工具修改。602旧输入清单的3个接收器/words/调用门禁适配以独立收据绑定，不改写历史输入。

设备c073a488/b Sending/Writing/reboot均OKAY。native 7f828fc6de9413dceb0bb596e31265c78692592106bd140ba62a93ddd0dcdcdd，boot 534f56058e776e4797cd9de3d25728dd7681fdf07d25d4d9ff92db36058e03fa。capture PID 59908，collector 75704，finalizer 50140，唯一顺序post 91040。恢复核对实际进程，勿重复apply/build/flash/worker。原300秒安全且capture实际退出后LOWDMA/NC/CPU_RAM→完整GMU/HFI/VM/GX/OOB/CP/原后缀SN11→一次GPURENDER/三CAP38；原调用门禁已严格核对CAP34/35各3及36/37各3，不重复602旧VM计数错误。人工603尚NOT_RUN，最新602显示/点击/返回全部正常，亮度暂缓。

检查点native-dpu-0603/0603-flashed-initial-RB-ECO-render-pending-checkpoint.json SHA-256 8cc8aa499e95311d5df5c17605f56a87a55026fa3826d26f314972386891197e。入口normal-baseline-qualified.json、post-sequential-output.log、live-gpu-render/result.json。602严格记录d57503fb051bdf26ac94289e054f0ab9b64d4b8ffd76b6288cc198673d84d1c8：同目标页CP控制a650c001可见，Fence3/RPTR159/CCU完成，像素仍A5，第二pass未提交，隔离保留。没有PAS13SUP/PASRANGE/认证/secure exit/IRQ/Metal新资格；PAS13查询路线关闭、ZAP实际认证NOT_RUN/UNKNOWN。硬目标仍A650_FIRST_RENDER=PASS，失败继续按真实结果聚焦，不能以标记当像素。

## 0602同页CP写入诊断完成：FirstRender=FAIL

原安全304.405秒和原GPU/CP/后缀回归通过。只有一次GPURENDER，三份CAP38一致SHA-256 0c3d1c9809a53c166f05f6ffe5c35e0163b46f0b4806b784280d47aecd9ba539。第一目标同页CP控制可见=True；实际图像摘要及逐像素状态：[{"pass_index": 0, "submitted": true, "captured": true, "BO_sha256": "c698b0cead6b55a34759a3a06a19e24b21a81629626f984175c353d5d2b6cc39", "same_page_CP_control": true, "observed_control_word": "0xa650c001", "image_bytes": 4096, "image_sha256": "f600eca824e84a43f0691b267bd620e462c50da165c5b80e17aecb7a924f1fa8", "all_pixels_A5": true, "terminal": {"wptr": 159, "rptr": 159, "shadow": 159, "fence": 3, "marker": 2790263821, "status": 1, "rbbm_irq": 0, "irq_mask": 0, "cp_fault": 0, "cp_irq": 0, "cp_protect": 0, "ahb_fence": 0, "gfsr": 0, "fsr": 1024, "sqe": 1, "timestamps": [3968711036, 3968711041, 3968711150]}, "first_mismatch": 256}, {"pass_index": 1, "submitted": false, "captured": false, "same_page_CP_control": null}]。CP写入标记不是绘制像素，不扩大为RB/CCU执行成功。原Fault/超时/隔离保留，不重试writer、不清现场。人工602状态见独立收据（若未存在则NOT_RUN），亮度暂缓。

严格记录native-dpu-0602/0602-render-witness-physical-qualified.json SHA-256 d57503fb051bdf26ac94289e054f0ab9b64d4b8ffd76b6288cc198673d84d1c8，90个raw样本hash重核；native 994662ac7139addc53e22fc155824ff40e694893cd010da8b2b3ae476776644c，boot 0ce91cd4e338954af75df265cbebb460dbf8f7c8ee73686b99d56e17a0f8af9c，456+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a。所有原始CAP38、wire/log、BO和截取像素均保存，0591历史不改。PAS13重复支持查询路线已关闭，本轮未查询，ZAP实际认证NOT_RUN/UNKNOWN。

下一根据同页CP/像素结果聚焦2D状态。固定Mesa A650源码明确初始restore写RB_DBG_ECO_CNTL=0x04100000；normal与blit相等只跳过切换而不省初始写，当前遗漏已审计但根因未证实。可聚焦该单一初始化改动再完整安全实机验证；不得无证据转回ZAP前置或盲写TRUST。

## 0602已刷入，安全回归后一次同页CP写入标记/原2D绘制

正式0477仅三文件，456+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a。每pass只在原0591命令前加4个已验证CP_MEM_WRITE字，写到对应目标同页、像素区外0x90010080/0x90014080，值a650c001/a650c002；原128字2D命令逐字未变。每pass132字，WPTR159/291；CAP38 schema2/135728B，完整BO验证新增两个明确控制位置，其它像素/guard要求不变，控制成功绝不算像素。原超时/Fault/quarantine/无retry保持，没有SMC/PAS/TRUST/IRQ/电源/SMMU改动。1364主机断言、旧代码编译后红、ASan/UBSan、8运行变异、299坏decoder输入、7实际SDK单元、完整native/54exports/5456清单、真实控制构造机器码、原ABI/HID/cache/封包门禁均通过。初次接收器暂存遇旧words已存在而拒绝，保留旧words再继续；elapsed有效值误当坏fixture及来源CR0历史版本误指601均已保留修正，无设备writer重试。

设备c073a488/b Sending/Writing/reboot均OKAY；native 994662ac7139addc53e22fc155824ff40e694893cd010da8b2b3ae476776644c，boot 0ce91cd4e338954af75df265cbebb460dbf8f7c8ee73686b99d56e17a0f8af9c。capture PID 45488，collector 15588，finalizer 84372，唯一顺序post 47320。恢复核对实际进程，勿重复apply/build/flash/worker。原300秒安全且capture实际退出后，LOWDMA/NC/CPU_RAM→完整GMU/HFI/VM/GX/OOB/CP/原后缀SN11→一次GPURENDER/三份CAP38。若失败保留BO及隔离，分析同页CP控制是否可见，依证据继续。没有重复PAS13SUP或PASRANGE；ZAP认证NOT_RUN/UNKNOWN。0602人工尚NOT_RUN，最新0601显示/点击/返回全正常，亮度暂缓。

检查点native-dpu-0602/0602-flashed-render-witness-pending-checkpoint.json SHA-256 466d27d5d2dd65ade22008a5f453ac27cfc9217301b97c604e6db258c5f4a437。入口normal-baseline-qualified.json、post-sequential-output.log、live-gpu-render/result.json、pass1-raw-bo.private.bin。原0591只有第一目标真正提交并全65536B A5，第二pass未执行；原包/日志/失败完整保留。硬目标仍A650_FIRST_RENDER=PASS。

## 0601原安全、人工GUI、GPU/CP及只读资源资格通过；主线回到First Render

用户明确确认SpringBoard显示、点击打开应用、返回主屏全部正常；亮度继续暂缓。人工独立收据native-dpu-0601/0601-manual-GUI-owner-confirmed.json SHA-256 d1627da4447f4e201320735ce0b577f8e0651ef24bf23013758de64e8e063f8b。原安全独立normal-baseline-qualified.json为304.438秒PASS，Root/nonce、原bank/systembag/加密Data/MACF/ACL/Sandbox/watchdog保留。

LOWDMA831、Normal-NC/CPU_RAM、GMU/HFI/四初始化消息、GPUVM/GX/OOB/CP及后缀/SN11全部回归通过；CP Fence1/2、RPTR16/27、真实IB marker0xa6500c0d。实际CR0=0x260406，0x220406变体仍HOST_ONLY。新PASRANGE仅一次，17范围/136对/260页/双次CP静止、flags1023/result0/phase3、3027us；三份568B CAP43同SHA-256 5f31340696c97002e8ac138730c9b0b444a9e954dbf9cc8b3d3619e1748a6196。独立资源收据0601-readonly-resources-qualified.json，公开15个私有PA已遮去，408B私有范围摘要不能从公开字段独立重算；此记录不是全局alias排除或认证lease。

PAS13支持查询仍false，按用户明确指示结束重复查询路线；ZAP实际认证NOT_RUN/UNKNOWN，返回0不能解释为认证必然失败。没有INIT/MEM/AUTH/SHUTDOWN、secure exit或绘制。FirstRender仍0591真实像素FAIL，GPU IRQ/Metal未验收。下一直接核对0591 PM4/地址/格式/Pitch/CCU写回/CPU同步/SMMU/原始BO，设计聚焦的可区分原因硬件绘制；只有明确安全故障或平台源码证明必需ZAP前置时再调查，保留全部原门禁。

严格汇总native-dpu-0601/0601-original-regression-resources-qualified-render-pending.json SHA-256 6a78fb60fc13df2d339bf29fe2fbf5dae0eee7c2bb896d3d368c991f573652a0，93个raw样本hash重核，serial已释放且无设备进程。native b67481ed190b22ad5eb1fb5821d66d2b0e83e1af10e1b476019518707ec24b8e，boot 6d3dfff3930815d9849bf58b691ef8912e20a8e1d350f22fe264e688deac8d29，455+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a，未commit/push。0591/0599及全部历史失败保留，勿重发此boot任何writer。

## 0601已刷入，原安全回归及只读PAS资源观察等待中

正式0476，455+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a。九文件新增正常PASRANGE/CAP43，canonical completed-CP owner、8个GMU池（含scratch）/7个GPU池/2个低PA共17范围136对，260页AT/PAR重查、双VM/CP静止快照、私有408B范围摘要；公开568B遮去15个私有PA。仅观察，不是全局alias排除、secure lease或认证权限，原PAS namespace slot1门禁保持。6114主机断言、ASan/UBSan、14编译运行变异、协议6变异、7实际SDK TU、完整native/54exports/源清单、LC_FUNCTION_STARTS真实机器码边界和487坏decoder输入、原ABI/HID/cache/封包及刷新后门禁通过。旧审计把未命名后续函数误算入getter，现以真实函数边界95指令核实；未改驱动以迁就审计。

device c073a488/b Sending/Writing/reboot均OKAY。native b67481ed190b22ad5eb1fb5821d66d2b0e83e1af10e1b476019518707ec24b8e，boot 6d3dfff3930815d9849bf58b691ef8912e20a8e1d350f22fe264e688deac8d29。capture PID 78324，collector 79428，finalizer 91408，唯一顺序post PID 60612。恢复须核对实际进程，勿重复apply/build/flash/writer。原300秒安全且capture实际退出后LOWDMA→NC→CPU_RAM→原GMU/HFI/VM/GX/OOB/CP/后缀/SN11→一次PAS13SUP→只读PASRANGE/三CAP43，前置失败不继续依赖。原CPU显示、安全、加密Data和watchdog均保留，0601人工GUI尚NOT_RUN，最新0600用户全部正常，亮度暂缓。

检查点native-dpu-0601/0601-flashed-baseline-pending-checkpoint.json SHA-256 fe62c5e941a2e8db9f40d3bf99119fe74bdae4ec472383ea188826c4fa046d2a。入口normal-baseline-qualified.json、post-sequential-output.log、live-PAS13-resources/result.json。0600真实support0不可反转，0591像素FAIL保持；没有新认证/secure exit/绘制/IRQ/Metal资格。下一须根据真实资源结果继续PAS独立owner/HYP生命周期与完整Mesa restore核实。

0600人工GUI补充：用户确认显示/点击打开应用/返回主屏全部正常，亮度暂缓。独立收据native-dpu-0600/0600-manual-GUI-owner-confirmed.json SHA-256 30dfc0d614ef266561eb93c98ef4bac4d0b7072f4364f1a11652160cb3f6e884。原严格记录中人工等待字段为历史状态，PAS13查询false、未认证/未绘制保持不变。

## 0600原安全/CPU_RAM/GMU及CP回归通过，postCP PAS13查询仍false

原安全304.374秒、LOWDMA831、Normal-NC260页、独立CPU图案写读/清零写读和独立SHA通过。GMU首次映射、固件/HFI/四初始化消息、独立GPUVM/GX/OOB及CP Fence1/2、RPTR16/27、IB marker0xa6500c0d回归通过。原后缀/INFO/DMA/IOVA/平台/温度/SN11完成、串口释放。0600实际CR0为0x260406，因此新冷策略对0x220406的修复仍只有host/编译/机器码证据，未把此次回归冒称599该具体硬件变体已复验；完整同boot CR0未变。

实际CP及后缀后仅一次PAS13SUP，status0/value0/type0/data0/cookie0，attempt1/两guard0。三份88B CAP41同SHA-256 37c5992a71888b145d07a070104d027caf20e155d98319061efd27a594e7ee2f，与598上电前查询同为false，单纯GX/CP条件未改变结果。overall accepted=false，original_regression_qualified/query_transport_qualified=true。没有INIT/MEM/AUTH/SHUTDOWN、secure exit、绘制、IRQ交付或Metal。FirstRender仍591真实像素FAIL，不能由CP/CPU_RAM代替。人工600尚等待回复，最新599已人工全部正常，亮度暂缓。

严格记录native-dpu-0600/0600-original-regression-CPU-RAM-qualified-PAS13-support-false.json SHA-256 68389fdbd683cceabc2964b6a627e6c24066ee5ad69038c8c1720b08d9a7330a。native 80369fb18ccb3061f26e5fd7dbaa0b0d8c521987622167bcecf4385c2b443b94，boot 00ca500537ab752268972b2ac87c40982346822136737bfdf8859e88fe7b53ef，454+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a，90个raw样本hash重核。599四读/零写失败和全部工具失败日志保留；无writer重试。

下一0601审查PAS13专用静止资源/内存范围/别名/生命周期契约。真实SM8250 vendor和固定Linux A650 loader不调用IS_SUPPORTED作为装载门控，但这不是本机认证可行证明，也没有实现新override或认证caller；不得将false反转、无条件继续、放宽原PAS namespace slot1门禁、猜TRUST或借外机carveout。任何后续新scope须有完整实际owner证明及独立source/host/native/实机门禁。Mesa完整restore仍要独立核对，不能把591全部归ZAP。

## 0600已刷入，源核实的inactive MemAttr冷策略修复等待实机

正式0475仅两个header，454+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a。Arm IHI0062D.c9.6.3/9-197指出MemAttr[19:16]在MTCFG20=0时inactive且reset UNKNOWN。保持固定policy、所有active字段；仅冷策略比较允许该inactive字段差异，发布前新增完整CR0一致性检查，运行期与父快照完整CR0精确相等，原Unowned完整比较保持，不写全局CR0。实际599原始fixture主机复现4read/0write/result6，旧代码两个desired测试红；修复后16种inactive值、28其它位拒绝、active MemAttr和同boot变化拒绝，首次映射5950断言、VM6573031断言/原677cases，ASan/UBSan及6编译运行变异通过。断言数不是独立case数。实际5SDK TU、454+4/5456回放/逆回、fullnative/54exports/7records、真实MTCFG选择和完整raw比较机器码、四decoder/56active坏字段/3同boot变化、原ABI/HID/cache/封包/刷前门禁通过。测试输出路径碰撞、审计分支prologue假设和封包parent版本初次拒绝均保留后修正，未改硬件失败记录或重试writer。

device c073a488/b一次Sending/Writing/reboot通过。native 80369fb18ccb3061f26e5fd7dbaa0b0d8c521987622167bcecf4385c2b443b94，boot 00ca500537ab752268972b2ac87c40982346822136737bfdf8859e88fe7b53ef。capture PID 73496、collector 88480、finalizer 91232，唯一post exec77249。恢复核对实际进程，勿重复apply/build/flash/writer。原300秒安全及capture实际退出后LOWDMA→NC→CPU_RAM→原GPU/CP/后缀/SN11→一次postCP PAS13SUP；前置失败不继续依赖写入，原独立后缀仍完成。

检查点native-dpu-0600/0600-flashed-baseline-pending-checkpoint.json SHA-256 03747236e13eca0d55fdce924dd5737012e33708e0920065ce63f3aec295004f。入口normal-baseline-qualified.json、post-sequential-output.log、live-gpu-first/result.json、live-gpu-cp/result.json、live-PAS13-supported/result.json。0600人工尚NOT_RUN；用户599显示/点击/返回全部正常已另存独立收据。0599真实CPU_RAM通过且GMUMAP拒绝、0598 support0、0591像素FAIL全部保留。当前无ZAP认证/secure exit/IRQ/Metal资格，Mesa完整restore仍独立待查，亮度暂缓。

0599人工GUI补充：用户明确回复显示/点击应用/返回主屏全部正常，亮度暂缓。独立收据native-dpu-0599/0599-manual-GUI-owner-confirmed.json SHA-256 8e5b2101057e90d5d97ea0c5e5b1dec64ab723cf86e452b3d307c9739fca7895。原始严格记录中人工等待字段保持历史原样，GMUMAP失败不变。

## 0599低PA真实CPU读写通过，GMUMAP在CR0冷策略处拒绝

原安全304.477秒、LOWDMA/Normal-NC、独立CPU图案写读及清零写读通过。image1MiB/metadata16KiB全部260页，四个word计数均266240、4次DSB、196761us，独立pattern/zero SHA一致。三份280B CAP42相同，SHA-256 f9c8638352d900fbb5c0ae0e56e5398269b806883d747df67814bb190a084849。真实RAM可达性不是PAS认证或GPU像素证明。

随后原GMU首次映射CAP25 result6/primary6即KG_OWNERSHIP，4reads、0writes、0touched、0published/attempted、cleanup0。之前CAP23实际CR0=0x220406，旧policy=0x260406，xor0x40000。固定源码KFSnapshot在前三个ID和CR0共4读后按整字比较拒绝；原CAP25仅导出work而未导出partial before，不能伪造失败原始值。未重试writer，GMU固件/CP和依赖postCP PAS13查询均NOT_RUN。独立原PAS INFO/DMA/IOVA/平台/温度/SN11后缀一次完成并释放串口。0598支持查询false及0591像素FAIL保持。

Arm IHI0062D.c 9.6.3/9-197说明MemAttr[19:16] reset UNKNOWN，只有MTCFG20=1时覆盖属性；两值MTCFG均0。0600拟只在MTCFG0冷策略比较中允许inactive MemAttr差异，原所有权/活跃字段仍严格、同一boot完整CR0稳定比较不放宽、不写CR0。先真实host重现0599四读拒绝再修复。规范：https://documentation-service.arm.com/static/5f900d34f86e16515cdc08fb 。Mesa完整restore仍独立待核，不能把0591全归ZAP。

严格记录native-dpu-0599/0599-CPU-RAM-qualified-GMUMAP-CR0-refused.json SHA-256 7e69ed8d04e6bb1059062a96cfb4a71be3103125d06b0d63472df9fab4aa1956，native f2b6ea0daf777b3c8cf0c51eb81cf36103f800a2d54cf9dc1ee99f2d194ef5fc，boot 7285aa74fb5a9b3644fdc171694152d98b7b7f9199ec8d9b64e1c305eabd96d3，453+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a，54个raw样本hash重核。overall accepted=false，原安全/后缀及CPU_RAM=true，GPU回归false。人工GUI等待用户回答，亮度暂缓。全部失败现场保留，不再使用599 pending进程作为现状；当前无serial owner。下一gpu-zap-0600。

## 0599 已刷入，原安全及 CPU RAM 验证等待中

正式0474、453+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a。五文件新增 LOWDMARW/CAP42，仅访问已测量并逐页核实的独立低PA Normal-NC image1MiB/metadata16KiB。图案写读、清零写读及独立SHA校验，260页/每阶段266240字/4次DSB，2秒协作预算，失败不重试不释放。原namespace slot1未发布门禁保持，无SMC/固件/电源/MMIO/绘制更改。主机真实RAM2137653断言、ASan/UBSan、14编译运行变异、协议6变异，5实际SDK TU、完整native/54exports、源码清单不变、机器码STR/LDR/DSB/AT/PAR/SHA/CAP42及原ABI/HID/封包/刷前门禁均通过。审计脚本唯一性和mangled符号名错误保留后修正，未重构驱动。

device c073a488/b，一次Sending/Writing/reboot通过。native f2b6ea0daf777b3c8cf0c51eb81cf36103f800a2d54cf9dc1ee99f2d194ef5fc，boot 7285aa74fb5a9b3644fdc171694152d98b7b7f9199ec8d9b64e1c305eabd96d3。capture PID 69816，collector 80384，finalizer 19892。唯一顺序post exec10800；恢复需核对实际进程，勿重复apply/build/flash/writer。300秒原安全与capture实际退出后，LOWDMA→NC→CPU_RAM→原GPU/CP/后缀/SN11→一次PAS13SUP（在CP之后）。失败保留且依赖不执行。

检查点 native-dpu-0599/0599-flashed-baseline-pending-checkpoint.json SHA-256 b1a3a728913e94113376e74ec860bfa10e595a1ee9160cae19fd1dc19da0066e。入口 normal-baseline-qualified.json、post-sequential-output.log、live-owned-CPU-RAM/result.json、live-gpu-cp/result.json、live-PAS13-supported/result.json。0599人工GUI尚NOT_RUN，最新0598人工全部正常。0598真实support0不可反转；FirstRender仍0591像素FAIL，ZAP认证/secure exit/IRQ/Metal未验收。亮度暂缓。

## 0598原安全及GPU回归通过，真实PAS13支持查询返回0

0598原安全回归304.486秒通过，用户确认显示/点击/返回主屏全部正常，亮度暂缓。LOWDMA与Normal-NC映射复验通过，原Root/nonce/bank/systembag/加密Data/安全策略/watchdog保持。标准SMC64 PAS_IS_SUPPORTED13仅一次，实际返回status0/value0/type0/data0/cookie0，SMC尝试1、两层guard0。三份88B CAP41相同，SHA-256 37c5992a71888b145d07a070104d027caf20e155d98319061efd27a594e7ee2f。该结果是明确的支持查询false，不得改称PASS、假定ZAP已加载或无条件继续认证。查询在低DMA/NC之后、GPU上电/VM/CP之前。普通DRV复查和之后既有GMU/HFI、VM/OOB/CP/Fence1/2/IB marker0xa6500c0d及原后缀/SN11均通过，串口释放。

严格记录native-dpu-0598/0598-original-regression-qualified-PAS13-support-false.json SHA-256 0c24bddd7e67f14f518fe251d7a0d968abe3775cb4c2824863de511768510ad5。overall accepted=false，original_regression_qualified/query_transport_qualified=true。native 40a3ffbf7ff1f7338b6ffe4a93ddc6c789f0fb4263275aa9e75334bd03a3dcaa，boot 1934162a955aefe501cde376252fc41eb9df62d36084e5e3304752d9664cfc14，452+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a，87个实际raw样本hash重核。0598五个实际SDK单元、54exports/七编译记录和真实RO查询tuple/thread_call/SCM/完整64bit store/CAP41无SMC等均已核验；host includes和mutant unused-helper首次编译错误、旧UTC拒绝及所有历史失败保留。

原始IS_SUPPORTED语义已与固定Linux v6.12和本机vendor源码核对：res.a1=0解释为false，不能反转。Linux Adreno ZAP加载路径和vendor PIL加载路径并不直接调用该支持查询作为加载门控，这只能作为继续审查依据，不能证明本机认证可行。下一先核对GX/CP条件下同一固定查询的差异，并在GPUVM发布前独立验证已归属低PA的CPU读写可达性。ml_nofault_copy要求pmap_valid_address且走物理copy，不适合此unmanaged低PA，不得放宽它的全局门禁或制造WB alias。任何认证仍需独立内存/资源/范围/PAS生命周期资格，保留现有namespace门禁，不能盲写TRUST或借用alioth carveout。

本次没有低PA数据load/store，没有INIT/MEM/AUTH/SHUTDOWN、secure exit或绘制。FirstRender仍0591真实像素FAIL，GPU IRQ及20H392 Metal未验收。继续入口gpu-zap-0599（尚未创建候选），从当前598真实源树推进；当前设备598所有pins保留且GPUVM已发布，勿在此启动绕过排他检查执行认证。

## 0597原安全/人工界面、独立Normal-NC映射和GPU执行链回归通过

0597原安全回归304.456秒通过，用户确认显示、点击和返回主屏全部正常，亮度暂缓。LOWDMA与LOWDMANC各一次。三份CAP20测量一致；三份CAP40一致，SHA-256 107430602a0b17021cdda9b20491b4eb26923c035d838efd9905813e02681a3e。已归属低32 image1MiB和metadata16KiB的全部260个4KiB区间实际AT/PAR物理地址及Normal-NC0x44通过，flags15/result0。所有descriptor/map长期保留；此次没有低PA数据load/store，没有PAS调用、固件、MMIO或电源写。原CAP20测量仍831，只描述先前测量冻结结果。

本次GMU/HFI、VM/BO、GX/OOB及CP_ME_INIT/Fence1/2/IB写回0xa6500c0d回归通过，rptr16/27；原后缀/SN11完成，串口释放。GPU IRQ仍masked，绘制本次未执行，FirstRender仍0591原始像素FAIL。当前597已发布GPUVM slot1，不能在本启动绕过原namespace gate做PAS认证；后续新启动须在GPUVM发布前取得独立PAS资源和生命周期资格。

严格记录native-dpu-0597/0597-owned-Normal-NC-and-GPU-regression-qualified.json SHA-256 7982d2b76dc82f67e4aeae6ef6bf0e03587eb4754496a375b70514e8a4cce172。native a5b6bd4473fc849f382dccff8ae3a4e9489320c7f8929c84191bcf987f3be888，boot 9622fdffbeac209546ffe5fd1c05590a35e2527f0a2db5c9e6e2c86c13b56ce2，451+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a。84个原始样本hash重核，完整wire/log/三份CAP20/CAP40和所有历史失败保留。主机protocol样本曾改错CRC字段偏移，native审计先误以为直接调用，decoder先把合法PAR flags当坏输入，初次刷前UTC过旧；均在设备动作前严格拒绝并保存，分别修正测试/审计或UTC后完整重验，没有改硬件门禁或重试writer。

下一0598可接入固定标准SMC64 PAS_IS_SUPPORTED(13)专用thread_call查询和只读冻结结果。既有INFO可用不等于PAS13支持，更不等于ZAP已认证。随后仍需独占SCM/PAS状态、source-backed带宽/时钟条件、固定ZAP元数据与独立NC生命周期、INIT/MEM/加载/AUTH以及合法secure exit；不能盲写TRUST、使用QSEE桥页或假定认证完成。低层任务继续，Metal/Turnip尚未实机执行。

## 0597已刷入，等待原安全及独立Normal-NC映射验收

0597正式0472修改5文件。新增正常LOWDMANC仅映射已测量EFI-owned image1MiB和metadata16KiB，沿用现有PAS13 namespace排他检查，GPU slot1必须未发布；保留所有成功/部分失败descriptor与map，不cleanup/重试。每4KiB实际AT/PAR核对物理地址与Normal-NC0x44，共260页；完整DAIF保存/恢复，无低PA数据load/store、SMC、固件或电源寄存器写。CAP40仅复制冻结160B，正常协议允许40且41拒绝。原CAP20/完整runtime测量和已通过硬件链路不变。

7848主机检查、ASan/UBSan、13编译后行为变异、实际provider体、正常协议6变异、451+4/5456回放及完整逆回、4实际SDK TU、完整native/54exports和原ABI/HID/cache/封包/刷前门禁通过。实际machine code确认outline USB worker、内联的既有namespace CAS/slot1/busy检查及正常end边、AT和PAR属性比较。原失败测试/审计/旧UTC包均保留，未绕过门禁。

设备c073a488/b一次Sending/Writing/reboot全OKAY，已进XNU。native a5b6bd4473fc849f382dccff8ae3a4e9489320c7f8929c84191bcf987f3be888，boot 9622fdffbeac209546ffe5fd1c05590a35e2527f0a2db5c9e6e2c86c13b56ce2，Git d5c9c62db752f225a288b88959d3754edc94138a。capture PID 50684，collector 12220，finalizer 70148。顺序lowDMA/NC等待PID 89220，GPU等待PID 72252。需核对真实进程，勿重复apply/build/flash或writer。原300秒安全与capture真实退出后LOWDMA一次/三CAP20，再LOWDMANC一次/三CAP40；GPU writer需两者accepted且串口释放。

Windows检查点native-dpu-0597/0597-flashed-baseline-pending-checkpoint.json SHA-256 86e378d2b7e834e5a8acd4656a8f6d1d2f21fd6cccec0f07caabfbadbaf32a33。入口normal-baseline-qualified.json、post-owned-lowdma-output.log、live-measured-lowdma/result.json、live-owned-lowdma-NC/result.json及post-gpu-regression-output.log。人工597 GUI尚NOT_RUN；596人工正常；亮度暂缓。当前NC/PAS/绘制/IRQ/Metal仍未验收，FirstRender仍0591原始像素FAIL。下一PAS13只读支持查询和认证生命周期须独立证明，不能以INFO可用或NC映射当作认证成功。

## 0596原安全/人工界面、完整lowDMA测量及既有GPU执行链通过

0596原安全回归304.461秒通过，用户报告显示、点击和返回主屏全部正常，亮度暂缓。LOWDMA仅一次，三份CAP20一致，flags831/result0/state3；实际完整当前树hash=64577367da594b6d2774f9934a92fc57dc24d2b2f5d9775e4a91f9c6fd21fe5c，与owner显式绑定运行期hash一致。原完整静态hash ae6be6c475c02cea1e645b3875abcd0666063ce3826c30fc4b85dba4c2c66fda、原64B熵输入和内核清零路径及原三exclusion保持。只证明EFI低32所有权与当前树软件测量，尚无NC映射或PAS资格。

本次GMU固件/HFI初始化、VM/BO、GX/OOB、CP_ME_INIT、Fence1/2及IB真实内存写回0xa6500c0d全部回归通过，rptr16/27，GPU IRQ仍masked。原PAS后缀只有既有INFO查询，不是standard IS_SUPPORTED/auth。SN11、原有DMA/平台/IRQ/热状态后缀均完成；串口释放。FirstRender保持0591原始像素FAIL，本次未重试未修正的绘制。

严格记录 native-dpu-0596/0596-lowDMA-and-GPU-regression-qualified.json SHA-256 ab10cd6ac8b84e25afe482a2183fb520a441dc0329be80ebe905fdfbdc8dfa71，native 4280e9bf4f649dd63b8ef15ce5ebc5a9ea0739ec7c91d5948c38e08ad554f4b3，boot ab3ef6f00c71a0220e308efe6f7d64868c47668ae5467f112e91830c5fe8a03c，450+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a。81个raw样本hash与本目录实际文件重核，所有原wire/日志/错误保留。主机crypto验收曾残留449计数，后续脚本在串口前停止；只修成实际450并对相同原始安全日志完整重验，之后唯一LOWDMA成功。未重刷或重发writer。

下一0597拟实现独立受控的已归属image1MiB/metadata16KiB Normal-NC映射和真实翻译属性逐页检查，无WB别名、不读写未知内存、不做PAS/电源寄存器写。XNU pmap_cache_attributes对unmanaged低PA返回默认IO，不能用它冒充实际VA属性；应以源核实的AT/PAR翻译和MAIR0x44为依据。随后再推进PAS13独占owner、认证ZAP和secure exit，保留既有namespace门禁。GPU IRQ和20H392 Metal仍未验收。

## 0596已刷入，等待原安全与完整运行期ADT测量

0596正式0471只修改lowDMA consumer C/header/core三文件。原完整静态hash和64B熵输入保留；owner manifest新增从唯一原版种子清零严格推导的完整运行期hash，consumer仍核对完整当前tree，要求唯一64零字节seed，原三exclusion不变。成功才设置新flag512，receiver严格要求831。3670主机检查、ASan/UBSan、19编译后行为变异、5封包坏契约拒绝、450+4/5456完整回放及逆回、4实际SDK单元、完整native/54exports与原ABI/HID/cache/封包门禁通过。初轮坏测试漏还原TrustCache导致变异存活，修正测试后重跑；初轮封包漏--hid-roots被拒，原产物保留后用正确参数重做。未跳过门禁。

设备c073a488/b Sending/Writing/reboot全OKAY，已进入XNU。native 4280e9bf4f649dd63b8ef15ce5ebc5a9ea0739ec7c91d5948c38e08ad554f4b3，boot ab3ef6f00c71a0220e308efe6f7d64868c47668ae5467f112e91830c5fe8a03c，Git d5c9c62db752f225a288b88959d3754edc94138a。capture PID 51984，collector 61012，finalizer 71204，LOWDMA等待PID 87252，GPU等待PID 91072。恢复需核对实际进程，勿重复apply/build/flash或worker。原安全300秒及capture真正退出后LOWDMA仅一次，再三CAP20；只有严格flags831和owner运行期hash通过、串口释放后才能原GPU链路回归。无NC/PAS/ZAP/绘制/IRQ/Metal资格。本次人工GUI尚NOT_RUN，595用户正常，亮度暂缓。

Windows检查点 native-dpu-0596/0596-flashed-baseline-pending-checkpoint.json SHA-256 2ed68eeb4c71306976ca2c99dd2f3a920f9867adc0ac62101b47f1fb09b98cd2。入口normal-baseline-qualified.json、post-lowdma-output.log、post-gpu-regression-output.log、live-measured-lowdma/result.json。FirstRender仍0591像素FAIL。下一安全开发是PAS13独占owner和NC低PA适配，不可用CP/Fence通过替代真正像素。

## 0595 CAP39诊断通过，完整测量失败根因为原版熵种子清零

0595原安全回归304.401秒通过。LOWDMA仅一次，仍结果12；三份CAP20一致，三份273068B CAP39原始tree完全相同，CRC/逐字节比较通过。独立按字节保留header/padding解析、恢复原有TrustCache producer span后，host SHA256=64577367da594b6d2774f9934a92fc57dc24d2b2f5d9775e4a91f9c6fd21fe5c，与native实际值完全一致。静态expected=ae6be6c475c02cea1e645b3875abcd0666063ce3826c30fc4b85dba4c2c66fda。差异只有 /chosen/random-seed 和 /chosen/memory-map/TrustCache。

原版pexpert/gen/pe_gen.c PE_get_random_seed读取后把DT random-seed清零。本corpus为64字节，单独按该已确认操作处理静态树，就精确复现实机hash。原始seed和内核清零路径必须保留，不能为了测量一致而改动熵输入。596拟在同一完整owner manifest保留原静态ADT hash，增加从原静态树严格推导的内核运行期hash；新receiver/consumer需明确识别该契约，核对64零字节并对其余完整当前tree严格hash，仍只用原三exclusion。正在0596候选中开发，尚未构建或实机验收。

诊断通过不等于lowDMA测量通过，整体accepted=false。GPU链路、NC/PAS/ZAP、绘制、IRQ、Metal未执行，FirstRender仍0591 FAIL。串口已释放。原始tree只留private，不提交。用户已回复595显示、点击打开应用和返回主屏手势正常，亮度继续暂缓。

严格记录 native-dpu-0595/0595-original-safety-qualified-ADT-diagnostic-measurement-failed.json SHA-256 fd70c34eb9c016e181011ecf8507b60924ac260dd7827c9135a60c2019e496ee。native cf63426a153a65796e332e270966f283a4ae0f393ef3f9949da0a014e2fc684c，boot 864c937a9e6959235de9239ffdb12e11438691f32ed3c43973ddede8b6f85633，449+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a。完整wire、三份CAP20/CAP39和独立分析全部保留。Root曾漏594协议入口max38，595已补且实际machine code和正常OPEN/GET/ACK源码测试均验证。下一工作目录gpu-zap-0596。

## 0594 原安全通过，CAP39 被正常协议入口上限拒绝

0594原安全回归304.474秒通过，低DMA一次调用仍结果12，三份CAP20一致。CAP39首次OPEN被 kc_execute 的 cmd.a>38 检查返回22，未进入已实现provider，未取得原始tree。没有重发LOWDMA或触发GPU/NC/PAS。串口已释放。人工594 GUI尚NOT_RUN，最新人工593正常。

Root补provider时遗漏正常协议入口，是软件接线错误。下一595仅把协议合法上限从38扩至已实现的39，并在实际生产kc_execute的OPEN/GET/ACK全路径重验，40仍须在provider前拒绝。已有主机ASan/UBSan和6编译后行为突变通过，下一走正式回放/构建/封包/受控boot_b。原测量检查、三exclusion与594失败cache代码不变。

严格记录 native-dpu-0594/0594-original-safety-qualified-CAP39-protocol-rejected.json SHA-256 dd635082af7481032c2e7e93fe1fa0df42391cae40ad841232f42a7ac21c3f69。native 54c3f93b27bf641a9e6c98cb4dc421e03094a99eaa700bb428b34a63d27c09b4，boot a7c225c16fac200b0d119ad09a388944abc5c1c6f845a262b07de0775c05f5a9，448+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a。诊断之后使用gpu-zap-0594/analyze_actual_ADT.py；当前根因仍未有原始tree证据，不放宽测量。所有原日志、raw CAP20和协议错误保留，FirstRender仍0591 FAIL。

## 0594 已刷入，原安全与 CAP39 诊断等待中

0594 正式0469修改consumer C/header和Framebuffer三文件，增加失败时正常worker保留最多2MiB原始当前ADT、CAP39只复制cache。原测量比较、三项exclusion、失败终态、无NC/PAS/低PA访问保持。3560检查、ASan/UBSan、15编译后行为突变、448+4/5456全量回放/逆回、4实际SDK TU、完整native、54 exports、原版ABI/HID/cache和完整刷前门禁通过。第一次exports工具引用593未资格目录而失败，使用实际native-dpu-0593-lf父收据修正工具路径后通过，原文件与构建保留，未重复构建。

设备c073a488/b的一次受控boot_b Sending/Writing/reboot全OKAY，已进入XNU。native 54c3f93b27bf641a9e6c98cb4dc421e03094a99eaa700bb428b34a63d27c09b4，boot a7c225c16fac200b0d119ad09a388944abc5c1c6f845a262b07de0775c05f5a9，Git d5c9c62db752f225a288b88959d3754edc94138a。capture PID 11492、collector 55952、finalizer 43984。唯一post exec15655在原300秒安全与capture真实结束前不取得串口；之后一次LOWDMA、三次CAP20失败原始记录和三次CAP39原始tree。预期保留测量FAIL12，只取得诊断，未启动GPU链路。

Windows检查点 native-dpu-0594/0594-flashed-baseline-pending-checkpoint.json SHA-256 966588a043acbdf3745d63aeb2df279efb21ec2002b99f3c687c581b153e3b68。恢复核对真实进程及post-diagnostic-output.log、normal-baseline-qualified.json、live-lowdma-ADT-diagnostic/result.json，勿重复writer/build/apply/flash。诊断完整后运行 gpu-prereq-20261103/gpu-zap-0594/analyze_actual_ADT.py，独立按字节保留header/padding，仅还原已确认的四种producer payload span及原三exclusion，比较静态hash、native计算hash和host独立hash与逐属性差异。树原始字节仅保留private，不提交。根因仍未知，不放宽检查。

最新人工GUI为593 PASS，594尚未人工验收，亮度暂缓。593原安全304.404s通过但lowDMA完整hash FAIL12，GPU链路被依赖门禁拦截，所有历史失败保留。First Render仍0591 FAIL，NC/PAS/ZAP、IRQ投递及Metal均未验收。

## 0593 原安全与人工界面通过，真实完整 ADT 测量不匹配

0593 原版安全回归304.404秒通过，Root/nonce、既有bank/systembag、加密Data、MACF/ACL/Sandbox、watchdog及原8GiB/owned0保持。用户报告本次593 SpringBoard显示、点击打开应用和返回主屏手势正常，亮度暂缓。

LOWDMA唯一一次真实调用已越过SHA256/SHA384已知向量自检、当前SecureDT物理跨度及策略hash检查。最终state3/result12即MEASUREMENT_MISMATCH，flags0x11f。expected完整ADT hash为 ae6be6c475c02cea1e645b3875abcd0666063ce3826c30fc4b85dba4c2c66fda，实际归一化计算值为 64577367da594b6d2774f9934a92fc57dc24d2b2f5d9775e4a91f9c6fd21fe5c。三份后续只读CAP20与首次失败原始字节相同，SHA-256 96293eee4a9087d2845cb40b2a2b22a6af788733bda2f82983ebdd3360f9786f。失败永久缓存，没有重发worker。

GPIO/GPU链路回归在lowDMA依赖门禁前被拦截，未发送本轮GMU/VM/OOB/CP writer。NC、PAS13、ZAP、secure exit、绘制、IRQ投递、Metal均未执行。最新绘制仍0591 FAIL。串口已释放。

下一步0594拟增加有限、只读、不可变的当前ADT失败诊断，以独立恢复四个已确认producer payload spans并逐字段比较，区分启动树变化、归一化实现及流式SHA问题。根因尚未确认，完整测量检查和原三项exclusion保持严格。

严格记录 native-dpu-0593/0593-original-safety-qualified-lowDMA-hash-failed.json SHA-256 b3bf64ae477254c68010e9fdda1b84b80d6ca0347a089dd2b36b59e9cd249954。native ff3ec44f1ec443e0b8bd7a98452f7f99d38a1e0c54f779ea52c341cc54c5f10d，boot bd62250e039d3d9151804cd08dcaefc74ca2dc63f75d87e712f827bb79bb0e7a，447+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a。该记录取代pending状态，保留592结果14与593未资格CRLF输入/构建、原脚本/日志。勿在593重复LOWDMA。

## 0593 已刷入，等待原安全与真实 lowDMA 测量

0593 正式0468仅修改 lowDMA consumer 一文件，用实际 SHA256/SHA384 abc 已知向量自检作为 readiness，保留失败14、终态缓存、范围/当前树/owner检查。旧代码缺注册表的失败先由主机复现。3505检查、ASan/UBSan、12编译成功后的行为突变、447+4/5456全量回放/完整逆回、4实际SDK TU、完整native、54 exports、实际SHA调用路径/常量、原版ABI/HID/cache与完整封包/刷前门禁通过。

一次受控 boot_b 刷写已完成，设备c073a488/b，Sending/Writing/reboot全OKAY，已进入XNU。native ff3ec44f1ec443e0b8bd7a98452f7f99d38a1e0c54f779ea52c341cc54c5f10d、boot bd62250e039d3d9151804cd08dcaefc74ca2dc63f75d87e712f827bb79bb0e7a，Git d5c9c62db752f225a288b88959d3754edc94138a。capture PID 65612，collector 88200，finalizer 89356。lowDMA等待 exec34500，原始GPU链路回归等待 exec83164。只能在原300秒安全通过并确认capture真实终止后一次 LOWDMA，再测量CAP20三次；GPU回归还必须等待lowDMA accepted且串口释放。绘制、NC/PAS/ZAP、IRQ、Metal未执行。

Windows检查点 native-dpu-0593/0593-flashed-baseline-pending-checkpoint.json SHA-256 8808816abc3ca2cf361ef7d2fac6607538ce865781ed7c090bac1ab0dd3bfb71。从真实进程和日志恢复，勿重做 apply/build/package/flash 或重发writer。入口为post-lowdma-output.log、post-gpu-regression-output.log、normal-baseline-qualified.json及live-measured-lowdma/result.json。实际Mac使用native-dpu-0593-lf与build-0593-lf-full.log，Ubuntu stage为gpu-0593-lf。

首轮CRLF候选被canonical回放拒绝，Mac首轮完整构建产物另存 unqualified-CRLF-build，未封包刷写；统一LF后重新主机测试/回放/构建。生成的注册脚本曾残留592 native hash和guard base路径，门禁真实拒绝后只修对应元数据并补齐，不重复已完成的profile操作。592 result14、旧脚本/日志和所有未资格输入均保留。人工GUI最新为592 PASS，593尚未人工验收，亮度继续暂缓。

## 0592 原版安全与人工界面通过，低 DMA 测量真实失败

0592 原版安全回归 304.489 秒通过，Root/nonce、既有 bank/systembag、加密 Data、MACF/ACL/Sandbox、watchdog 和原8GiB/owned0保持。用户对本次0592的 SpringBoard 显示、点击打开应用、返回主屏手势报告正常，亮度继续暂缓。GPU链路本次尚未运行，不能继承旧硬件记录为0592新资格。

一次且仅一次 LOWDMA 已执行。native worker0/snapshot0/state3/result14即 CRYPTO_NOT_READY，flags0x1f。low32 raw PA 0x97e61000、2113536字节，image PA 0x97f00000、metadata PA 0x98000000，EFI116记录、managed banks0。三份后续只读不可变CAP20与首份失败逐字相同，SHA-256 698575ca81c3a6957621d89ef973d4e0521f39328e4577745b43eb14eb9dc804。未重发 LOWDMA，未做 NC/PAS/ZAP/secure exit/GPU绘制。此次测量失败永久缓存。串口已释放。

源码核实 SHA256_Init/Update/Final 使用 built-in ccsha256_ltc_di，SHA384 包装调用既有 MIT BearSSL 实现，不依赖 g_crypto_funcs。当前 consumer 却把该注册表字段当成前置要求，因而返回14。下一版0593拟用实际SHA256/SHA384已知向量自检替换该不相干依赖，保留真正自检失败为14以及一次性终态、完整测量/范围/隔离检查，再跑主机、正式构建和受控回归。

主机还修正过采集顺序与原crypto验收445/446计数。raw-before-worker等待脚本在串口使用前取消，真实唯一LOWDMA由v2发送；原300秒日志完成后因旧计数中止的验收失败另存，修正后同一原日志完整重验通过。全部旧脚本/日志/封包保留。

严格记录 native-dpu-0592/0592-original-safety-qualified-lowDMA-failed.json SHA-256 61453178eda23945a8850fb8f3d293f88eae5c7317deabc6310adbfa9db4ec9d，当前native 91d4ace690385fde17cd241b696ad629db60152deaeaf5f14fb23eb4aa82fde7、boot 849381d83c3266dabf1f11bbde6dc2743332fd29d274151fc27d979482bf8b33。下一工作目录 gpu-prereq-20261103/gpu-zap-0593 应从0592实际源树继续，避免重复 LOWDMA 或旧 apply/build。First Render 仍以0591 FAIL为最新绘制结果。此前pending检查点与旧等待顺序属于历史。

## 0592 已刷入并进入 XNU，原版安全回归进行中

用户已回到设备旁并明确可协助测试，当前恢复条件允许继续受控实机步骤。用户报告 0591 的 SpringBoard 显示、点击打开应用和返回主屏手势全部正常，亮度仍暂缓。该人工结果仅属 0591。

0592 prelink、完整 FAT/ADT 测量、原版 ABI/HID/cache/owner watchdog 与原刷前门禁均通过。仅给 0592 增加精确新 firmware-base 和两项 typed ADT 变换，原全局固件白名单保持。实际一次 boot_b Sending/Writing 及 reboot 均 OKAY，设备 c073a488/b。新 DarwinBoot 已进入 XNU，USB 日志恢复，原300秒安全回归进行中，尚未验收低内存 grant。

native 91d4ace690385fde17cd241b696ad629db60152deaeaf5f14fb23eb4aa82fde7，实际 boot 849381d83c3266dabf1f11bbde6dc2743332fd29d274151fc27d979482bf8b33。Git d5c9c62db752f225a288b88959d3754edc94138a，0467/446+4/5456。capture PID 85380，collector 11836，finalizer 85456。唯一 lowDMA post 的 exec session 为46871，等待原 safety 通过并确认原 capture 真实退出后，先三次 CAP20 raw-grant、一次 LOWDMA、再三次 measured CAP20，154坏记录拒绝测试已通过。不要开启第二串口或重发 worker。物理 NC/PAS/ZAP/FirstRender/Metal 仍未验收。

检查点 native-dpu-0592/0592-flashed-baseline-pending-checkpoint.json SHA-256 6688ecd1b10b97b4d0c5ba2a71f5c3f14cd33dcf5159d999500ada08d58b4c83。恢复先核对当前进程及最新日志，不凭历史 PID 推断。完整新封包、旧时间包和原主机失败均保留。下一入口是 post-lowdma-output.log、normal-baseline-qualified.json 和 live-measured-lowdma/result.json。原0591像素 FAIL不变。

## 0592 主机构建完成，首次新 EFI 实机验证待具备恢复条件

手机最后确认运行 0591。该次原版安全回归 304.406 秒及 CP First Execution 已通过，First Render 为 FAIL。首次绘制 Fence3、RPTR155 和三次 CCU 时间戳已完成，但 65536 字节 BO 全部仍为 0xa5。第二次绘制未提交，namespace 已隔离，原始 CAP38、失败收据和全部历史证据保留。

0592 正式 0467 仅修改 kebab_low_dma.c、kebab_low_dma.h 和 kebab_usb.c，新增正常 worker 命令 LOWDMA，保持 CAP20 仅复制不可变快照。复用 sealed current-SecureDT 度量实现，源树其余 5453 路径保持 0591。446 共同加 4 主机补丁、5456 源文件全量回放、三文件完整逆回、四个实际 SDK 编译单元 overlay/applied、3472 主机检查、十项运行突变及 ASan/UBSan 通过。Mac 正式 make install 完成，54 个实际导出头和真实链接调用通过。

Git HEAD d5c9c62db752f225a288b88959d3754edc94138a，分支 gpu/adreno650-20261103。工作树补丁及逐文件清单也是源码身份的一部分，本轮未提交或推送。0592 native 为 19501056 字节，SHA-256 91d4ace690385fde17cd241b696ad629db60152deaeaf5f14fb23eb4aa82fde7，配对 HID 为 fc55436750875b2fb156b4dbbed5702616e6de0d4c74779ade8544cf7094f014。实际构建日志、编译/链接与源码配对收据存于 Windows native-dpu-0592。

已离线核验独立低 DMA EFI 基础镜像。只替换 DarwinBoot，原 93 个其他可执行组件和镜像尾部保持。候选基础镜像 SHA-256 5e76d0053d8d8b08e6fdf6adf94652791ae584ea2b4b8797b750ee2fa2696caa，8MiB prefix 为 f2933aa0173184a1cdbc9af6187416678b9a212e0e141217b08554660ba67bd2。它尚未完成正式内核封包、注册、完整 owner ADT、firmware prefix 与 flasher 门禁。全局固件白名单保持原值。0592 未刷写，低 PA 映射、PAS13、ZAP 认证和 GPU secure-mode exit 均未执行。

用户明确表示现在无法操作手机。新 DarwinBoot 的所有权检查可能在 XNU USB/watchdog 启动前拒绝继续，自动返回 fastboot 的恢复路径尚未验证。因此首次新 EFI 实机步骤暂缓，恢复条件满足前保留 0591，不重试已隔离 GPU。此限制与主 AGENT 的恢复路径安全要求一致。

源审确认 0591 缺少 A650 authenticated ZAP 和后续 secure-mode exit，Linux/vendor/固定 alioth 均提供前置依据。这是必须解决的缺失阶段，尚未证明它是无像素的唯一原因。后续还需审计 pinned Mesa 完整静态上下文恢复。禁止借用未归属低内存、盲写 TRUST 寄存器或跳过认证。

20H392 原始 dyld cache UUID 8f970452c9e33e88b114feb1864bc9e0 的只读元数据审计得到 _MTLDevice 实例大小 528 字节，固定官方 AdrenoMetal 7e0d3d2ebaecd736784de0baa09254b7f63bc150 要求 536 字节。直接接入会被尺寸检查拒绝，需精确 ABI 适配。NativeAdreno v1/v2 BO 数量和结构差异也需独立处理。GPU IRQ 投递和 Metal 执行仍未验收。人工 UI、触摸和手势保持 NOT_RUN_OWNER_AWAY，亮度继续暂缓。

恢复入口为 Windows gpu-prereq-20261103/gpu-zap-0592/record_host_checkpoint.py 和 native-dpu-0592/0592-host-build-hardware-deferred-checkpoint.json，后者 SHA-256 877693e11c0f2280d67e347e23fac1cfd56d7cccb86cb5474d330c9e63cd37f3。检查点记录所有证据哈希与剩余门禁，记录脚本采用独占新建语义，不应重复运行覆盖收据。源码已位于 Ubuntu /home/albert/s4/x 与 Mac dxb/xnu，正式构建/配对已结束，无需重做 apply 或 build。

下一阶段先完成封包和完整 owner 测量门禁，再在具备实体 fastboot 恢复条件时验证真正 EFI 低 DMA 所有权交接与原版安全回归。之后实现 GPU namespace 发布前的 bounded PAS13/SCM 所有权、认证 ZAP、secure-mode exit，再验证完整绘制状态和两张离屏图。每阶段使用独立新收据和故障隔离，保留已通过 CP 与原安全链路。

主机工具曾修正 CAP34/35 样本计数、0592 链接审计的 GPU-only 图遍历和逆回收据旧说明文字。原失败脚本/收据均保留，新低 DMA 边用实际直接调用检查，逆回 v2 重新执行三文件恢复。0591 的图像失败保持严格 FAIL。

## 0590 实机通过 A650 First Execution

0590 的顶层 HWCG 读回为 0x8aa8aa82，SP 原始读值为 0→0。CP 真实完成初始化和固定 IB。Fence 为 1、2，RPTR 为 16、27，GPU 将独立 scratch marker 改为 0xa6500c0d。CAP37 schema2 的三份原始捕获经独立重解码通过，实际 148 次写入、64 次读取、2 次完成轮询，耗时 67739 µs。原 304.477 秒安全回归、GMU/HFI、VM/OOB/GX 和 SN11 均通过。0588 和 0589 的失败证据保留。

Git d5c9c62db752f225a288b88959d3754edc94138a（gpu/adreno650-20261103）与补丁 0465、444+4/5452 项清单共同标识构建。内核 8d303d05707be1e099890b94cedd8b7f19d5979467c171f2ba72c578191cb839，实刷镜像 087fcf48a6e6b7ca3bdd8c81ce84d82ffa75046e6e9cb550c67d8bb36ba78855。严格资格收据 SHA-256 500f9bafc9596578b2da2d39fc3c8fd7fa7cd32f747d680b6f2d8ae75c7999a6，实机检查点 SHA-256 977ab748e77d088ff8b74729517ade6e009e64b049ae5c13d5d882e72409af1b，保存在 Windows native-dpu-0590。实机后续进程已经退出，串口已经释放。

GPU IRQ 投递、First Render 和 Metal 仍为 NOT_RUN。下一步审计固定 Mesa A6xx 矩形绘制、线性 RGBA8 布局、CCU 写回和 GPU IRQ 路由，验证两次独立输出的逐像素及行尾检查。人工 UI、触摸和手势保持 NOT_RUN，亮度暂缓。

# 当前状态：0587原安全基线、GPU BO/CB1和有限OOB/GX上电通过

2026-11-03：最新实机0587严格汇总accepted=true，原Root/nonce、既有bank/systembag、加密Data、原MACF/ACL/Sandbox/watchdog及304.356秒回归、全部原采集尾部通过。原GMU固件/VERSION、四条初始化HFI消息ACK、资源/SN11只读观察保持。新增独立27页BO和CB1/ASID2准备通过；GPU OOB只请求bit30、只W1C自己的ACK bit31，成功后保留请求和全部私有引用。

CAP36三份不可变3424B事件一致，均严格重解码并绑定同次启动的VM参考。实际一次操作64读/2写/2屏障/1轮询，3.197毫秒，result0/held1；SPTPRAC bits6/7从0xc0到0，GX_GDSCR从0x00222001到0xf8222800，GX_CFG从0x00088400到0x00070400。IRQ上部mask保持0xffff0007，SMMU六条NS路由、五个普通context配置不变。上述证明有限上电和时钟开启指示，不证明GPU频率、目标带宽、CP或像素。三份采集是同次publication的复制，不是三次硬件执行或续租。

原生完整构建为441共同+4Mac主机补丁/5444源，0462仅6个现有文件加7个新文件；六个原有文件可完整逆回0586。四个实际SDK编译单元、48个真实export、完整native/HID/构建后源清单配对通过。native SHA256 fe6d1680d9a652eed3c1a7a25b70efeb4b4c583f9b411b7c8472ddd64d2a5ce8，19,481,216B；原HID对象不变。主机模型、实际构建、实机记录分别封存。

CP/SQE、ringbuffer/fence、First Execution、First Render及Metal尚未验收；完整HFI目标资源效果仍部分完成。下一步在已通过的OOB/VM私有所有权上接SQE与最小A650命令，保持CPU显示、安全、Data与watchdog。GUI本轮NOT_RUN_OWNER_AWAY，亮度滑块暂缓。0586长时启动在刷前曾报告panic状态，现有串口窗口不含原因；0587仅本次304秒窗口已通过，不能称长期稳定。

见[0587限定资格](qualification/0587/oob-gx-qualified-GPU-PARTIAL.json)、[源码回放](qualification/0587/public-all-source-replay-0587.json)、[同代A650源码审计](A650-PEER-AUDIT-20261103.md)和[可恢复开发检查点](GPU-CONTINUATION.md)。以下旧记录保持历史，不替代当前验收范围。

# 历史0583：有限GMU固件、VERSION队列DMA与SN11观察通过

截至2026-11-03：实机0583限定资格accepted=true；原SCM/Root/nonce、既有bank/systembag、加密Data、安全与watchdog304.415秒及全部原后续回归通过。43页/3个WIMG6页、首次NS、完整32KiB TCM、IRQ初始化/arm/readback后，RSC睡眠交接、真实GMU固件释放/FW_INIT100/HFI_CTRL ACK与一次VERSION20010008应答均成功，观察到有限GMU队列DMA；SN11三次独立只读观察成功。完整HFI初始化、带宽投票、GX/CP/GPU绘制/Metal仍未资格，人工GUI仍NOT_RUN_OWNER_AWAY。

0583为437共同+4Mac主机补丁/5417源，source/runtime parent0582仅接受原基线、backing、FirstNS、Cold/IRQ与suffix，不继承0582失败的RSC/FW成功。正式0458共5路径（3修改/2新增）：sleep末尾BUSY0 poll改为单次raw诊断；preprogram BUSY0及真正wake REQ2→fresh ACKbit1→BUSY0保持严格。另加CAP31固定96B SN11只读CommandDB观察，无新USB控制命令或带宽vote。实际完整native SHA256 `4a27045936807e2477af28e12f691628a6979d3e0d8ed8dcd1dc0e5ae9aab0a0`，19,424,008字节；即时source/HID配对、4TU overlay/applied、37实际exports、42原符号+15FW/HFI+3SN11符号、19原调用/156B policy及437+4/5417公开逐文件回放通过。源码/软件模型、native证据与本次实机结果分别记录。

新boot先通过原304.415秒回归及43页/3个WC页、首次NS306读/20写/TLB1。GMUCOLD一次加载并核对完整32KiB TCM：8192word写/8192word读，CRC f5508f4a；SYSRESET写1/回读1属于释放前Cold记录，不是后来SYSRESET0后的fresh读，也不是独立物理reset ACK。GMUIRQ一次完成Core47读/9写、实际483读/9写、valid11与初始MASK/readback。CAP27是FW启动前的immutable arm快照；其中dispatch0/handler_observed0不表示固件启动后没有IRQ，也不证明已观察物理中断触发。

GMUFW一次调用内的资源准备成功：CAP28 result0/state3、273读/21写/21屏障、poll2/delay0、elapsed734us，STATUS bit16/REQ0/ACKraw1（bit1clear）完成睡眠交接，BUSYraw1只作诊断。真正FW唤醒随后观察ACKraw3/BUSY0；睡眠BUSY1没有冒充wake成功。六hidden raw仍仅为diagnostic-notACK，未猜测其未知字段或RW/RO属性。

CAP29固件本体真实phase5/result0，5077读/20写/30屏障、poll9/500us delay、elapsed3347us，FW_INIT result0x100与HFI_CTRL ACK1均成功。记录中的missing_owners=2/retained_bandwidth1表示受控保留原带宽而无新vote；bandwidth_qualified0/bw_commands0，不能把配置中的目标数字当成已授带宽或现场继承floor。所有真实私有Root/Cold/namespace/IRQ pointer与fresh gate保持；没有借CAP缓存状态取得硬件权限。

随后GMUHFI一次真实VERSION请求得到匹配ACK：returned_version0x20010008，39次共享读/6次共享写、4次寄存器读/2次寄存器写、poll1、elapsed570us，packet/doorbell/ACK消费与version_verified均为1，dma_observed1。该证据证明有限GMU固件和CPU共享队列通信/DMA已发生；hfi_initialized0，尚未发送完整PERF/BW/CORE_FW_START等初始化，也未取得GX/CP、GPU绘制、GPU工作负载DMA或Metal资格。旧aggregate字段GMU=false和physical_DMA=false保留其完整GPU里程碑含义；新增GMU_firmware_running、GMU_shared_queue_DMA_observed/limited_shared_queue_DMA_qualified为true，不能用旧bool否认本次GMU成功，也不能用新bool泛化为完整GPU。

CAP28/29/30各三份相同hash只是各自一次publication的私有缓存复制，不是三次live写入或三次续租。原安全suffix完成后，CAP31是三次独立OPEN（session61/62/63），每次双遍volatile RAM派生记录并核对同session重复GET；三份96B一致。实际SN11 address0x500a4、slave5、resource version0x1000、aux8B，raw aux0=530000/aux1=196624、两priority均0。该address是RPMh command地址，不是CPU MMIO指针；未解读aux单位/width/VCD、未推断当前floor、未发command或授予BW所有权。

原CAP20 expectedABSENT、PAS INFO/RSC只读查询、IOVA/pool/显示IRQ/VSYNC/温度等suffix全部通过，post92313终止0，capture81908/collector81416/finalizer81764真实结束、串口释放。41548B固件原属性仍纳入完整owner ADT测量，无新exclude；0487 prefix、原组件/20H392 ABI/HID219/cache、Root/bank/加密Data/MACF/ACL/Sandbox/watchdog保持。生成器、注册、打包与本次flash各一次，无writer retry；sealed运行工具和历史receipt不改。

0577实际编译失败、0578 Cold75读0写拒绝、0579 IRQ初始MASK0拒绝、0581 hidden echo4写后隔离与0582睡眠BUSY超时全部保留。0583此次资格只覆盖上述有限实际GMU流程；下一阶段仍需真实完整HFI/GX、CP/ring/fence与正确像素，之后才谈Metal入口。人工GUI owner-away未运行，不反复请求；亮度滑块和新增安装仍暂缓，无新Git commit/push。

见[0583限定资格](qualification/0583/limited-gmu-fw-version-sn11-qualified.json)、[437+4/5417源码回放](qualification/0583/public-all-source-replay-0583.json)及[连续进度](CONTINUOUS-6-7-8.md)。

# 历史0582：原基线、TCM与IRQ通过，RSC睡眠末尾BUSY超时隔离（原文保留）

截至2026-11-03：实机0582原SCM/Root/nonce、既有bank/systembag、加密Data、安全与watchdog304.455秒及全部原后续回归通过；43页/3个WIMG6页、首次NS、SYSRESET1下完整32KiB TCM及IRQ初始化/arm/readback通过。RSC完成全部21次写入并观察到睡眠STATUS bit16、REQ0及ACK bit1清除，随后因BUSY raw1在睡眠末尾等待BUSY0超时隔离；FW本体0读0写、HFI跳过。整轮accepted=false，无FW/HFI/设备DMA/GX/GPU/Metal资格，人工GUI仍NOT_RUN_OWNER_AWAY。

0582为436共同+4Mac主机补丁/5415源，source/runtime parent0581部分资格；正式0457只修改资源Core/Native两文件、无新增生产文件，六hidden TCS写后保留barrier和一次raw诊断，不再把未知echo当ACK。实际完整native SHA256 `17f7328036a4361b0b505ffa5335a83d733a4ecf9616785caa64dae8865f846a`，19,407,432字节；四TU overlay/applied、176个显式未变clone、37exports、57符号/19原调用/156B policy及新hidden diagnostic通过。新资源链Core315、Native5场景及另两场景FW/HFI组合均为软件模型，旧全套未重跑；source30 body/transport历史与资源依赖替换分开，不把主机结果称为实机FW成功。

同次GMUCOLD与GMUIRQ各一次成功：Cold11688读/8196写、完整8192word写/8192word读、CRC f5508f4a，SYSRESET写1/回读1，elapsed21.254ms。IRQ Core47读/9写、实际MMIO483读/9写、valid11，Arm阶段真实HFI/AO STATUS0，MASKffff0007/AO12、enable30000、priority8080a0a0、pending/active0、epoch1/gen1。dispatch0/handler_observed0表示未观察物理触发；不把arm/readback称为实际触发，也不把reset请求回读称为独立物理reset ACK。

GMUFW只尝试一次。CAP28三份168B缓存一致：result110/state4/quarantined1，705读/21写/21屏障、75poll/72delay，elapsed10.807ms；STATUS0 raw01010006（bit16已观察）、REQ0、ACK raw1（bit1已清除）、BUSY raw1。最终failed_region0/offset0x404/operation6，是睡眠末尾BUSY0 poll超时，不是581的第4写echo超时。21写/seqRAM/PDC及睡眠STATUS/REQ/ACK证据保留，但资源READY与FW资格不因此成立；失败未重试、回滚或释放pins。

六个hidden诊断raw依次为offset38=0、34=82204514、40=0、3c=82204510、48=80000000、44=82204520；只读一次后在canonical返回锁外打印，明确not-ACK，不推断未知位或RW/RO类型。独立primary源审确认vendor/mainline的stop路径以STATUS bit16/REQ0为睡眠交接，未要求该处BUSY0；真正wake路径REQ2→ACK2之后仍要求BUSY0。下一最小候选只将睡眠末尾BUSY读取保留为诊断，preprogram BUSY0、wake BUSY0及ACK bit1清除门不放宽，仍须独立新源码/native/实机验收。

CAP29三份248B失败缓存仍为result110/phase0/action2、FW本体0读/0写，raw与581相同不代表复用了旧执行；未运行记录中的默认0不是live寄存器观测。SYSRESET0、FW_INIT、HFI_CTRL均未尝试，依赖GMUHFI/CAP30未运行，没有VERSION ACK或HFI资格。每CAP三份相同CRC都是同一immutable publication的复制，不是三次live写入或成功。

原CAP20 expectedABSENT、PAS INFO/RSC只读查询、IOVA/pool/显示IRQ/VSYNC/温度等suffix全部完成；post80862以exit1重抛资源失败，capture79824/collector80860/finalizer75500均结束、串口释放。41548B属性仍纳入完整owner ADT测量，无新exclude，0487 prefix及原组件/20H392 ABI/HID219/cache、Root/bank/加密Data/MACF/ACL/Sandbox/watchdog保持；216输出生成器、注册和打包各一次，未重跑或UTC retime。

当前完整限定基线仍为0580（reset-held TCM与IRQ arm/readback，不含FW）。0577编译失败、0578 Cold75读0写、0579 IRQ MASK0零写拒绝、0581 hidden echo4写后隔离与本次睡眠BUSY失败均保留。后续CP/ring/fence/正确像素和Metal未资格；亮度滑块与新增安装继续暂缓，Git尚未新commit/push。

见[0582部分资格](qualification/0582/substrate-cold-irq-qualified-RSC-sleep-BUSY-failed.json)、[436+4/5415源码回放](qualification/0582/public-all-source-replay-0582.json)及[连续进度](CONTINUOUS-6-7-8.md)。

# 历史0581：原基线、TCM与IRQ通过，新增RSC回读超时隔离（原文保留）

截至2026-11-03：实机0581原SCM/Root/nonce、既有bank/systembag、加密Data、安全与watchdog304.453秒及全部原后续回归通过；43页/3个WIMG6页、首次NS、SYSRESET1下完整32KiB TCM及IRQ初始化/arm/readback通过。新增RSC在第4次写后的offset0x34全字回读等待超时，result110并隔离；FW本体0读0写、未解除reset，HFI跳过。整轮accepted=false，无FW/HFI/设备DMA/GX/GPU/Metal资格；人工GUI仍NOT_RUN_OWNER_AWAY，亮度滑块和新增安装继续暂缓。

0581为435共同+4Mac主机补丁/5415源，source/runtime parent0580；正式0456共25处变化（7个现有文件、18个新增文件），仅授予新boot完整私有生产链之后的有限FW_INIT/HFI_CTRL与单次FW_VERSION候选。实际完整native SHA256 `6b5eafbcd33496f36c270f2690559089d9f6098ba742644bf829a4cf622269fc`，19,407,280字节；四TU bound/applied、最终closed ledger、37真实exports、原42加15个新符号/私有调用图与CAP无直接FW/HFI操作通过。新source30软件模型与原580协议/body继承分开，未把旧27整依赖pins重标为新执行；这些源码/host/native证据不证明硬件启动成功。

同次GMUCOLD与GMUIRQ各一次成功：完整TCM8192word写/8192word读、CRC f5508f4a、SYSRESET写1/回读1；Cold11688读/8196写，elapsed24.859ms。IRQ Core47读/9写、实际MMIO483读/9写、valid11，真实Arm HFI/AO STATUS0，MASKffff0007/AO12、GIC enable30000、priority8080a0a0、epoch1/gen1、pending/active0。dispatch0/handler_observed0，未观察物理触发；不把arm/readback称为实际触发，也不把reset请求回读称为独立物理reset ACK。

GMUFW只尝试一次，其资源阶段CAP28三份168B缓存一致：result110/state4/quarantined1、4106读/4写/4屏障/polls0，failed_region0/offset0x34/operation4，last_read0x82204514、last_write0，reset_readback1，elapsed10.175ms。该失败发生在named-RSC的hidden CMD0_ADDR全字echo等待；当前实际证据不支持把它判为只读寄存器、映射基址错误或解读8220字段。固定vendor路径对该hidden寄存器采用raw write+barrier，未建立wholeword echo合同；下一最小修正仍须独立源码、native和实机验收。

CAP29三份248B失败缓存一致：result110/phase0/action2、primary_error110、FW本体0读/0写。资源失败在真正FW入口之前返回，没有SYSRESET0、FW_INIT或HFI_CTRL操作；该未运行record里的默认0不构成live寄存器观测。依赖GMUHFI/CAP30跳过，没有VERSION ACK或HFI消息资格。CAP三份CRC相同是同一次immutable publication的复制，不是三次live成功。原始两份raw哈希保留在脱敏收据，失败未重试、回滚、复位或解除pins。

原CAP20 expectedABSENT、PAS INFO/RSC只读查询、IOVA/pool/显示IRQ/VSYNC/温度等后续回归仍全部完成；post51338以exit1重抛新增阶段失败，未生成全链成功标记。所有本轮捕获/观察者结束、串口释放。原41548B固件属性继续纳入fullADT owner测量，无新exclude，0487 prefix及原组件/20H392 ABI/HID219/cache、Root/bank/加密Data/MACF/ACL/Sandbox/watchdog保持；232输出生成器、注册/打包各一次，无UTC retime。

当前完整限定基线保留0580（reset-held TCM和IRQ arm/readback，不含FW）；0576限定通过、0577完整构建失败未刷、0578 Cold75读0写、0579 IRQ MASK0零写拒绝与本次RSC4写后隔离均保持。未完成FW/HFI、CP/ring/fence/正确离屏像素及Metal；尚未新commit/push。

见[0581部分资格](qualification/0581/substrate-cold-irq-qualified-RSC-readback-failed.json)、[435+4/5415源码回放](qualification/0581/public-all-source-replay-0581.json)及[连续进度](CONTINUOUS-6-7-8.md)。

# 历史0580：reset-held TCM与IRQ初始化、arm/readback限定通过（原文保留）

截至2026-11-03：实机0580原SCM/Root/nonce、既有bank/systembag、加密Data、安全与watchdog304.399秒及全部原后续回归通过；43页/3个WIMG6页、首次NS映射、SYSRESET1下完整32KiB TCM写读，以及真实IRQ已知MASK初始化、arm/readback均通过。IRQ dispatch0，未观察物理触发；FW仍reset-held，没有FW/HFI消息、设备DMA/GX/GPU/Metal资格。整轮限定范围accepted=true，人工GUI仍NOT_RUN_OWNER_AWAY，亮度滑块和新增安装继续暂缓。

0580为434共同+4Mac主机补丁/5397源，source/runtime parent0579部分资格；正式0455仅改IRQ Core/Native两文件，无新增生产文件。只继承579原基线、首次NS和TCM成功，不继承其IRQ失败。实际完整native SHA256 `41383aeee41da9abe5864a77bdafe50d8378e5b669b03327695e7e6319e78027`，19,384,928字节；两TU actual overlay/applied、5397postbuild、29真实exports、42linked符号/19组BL，以及实际initial-mask ORR与Arm分支通过。新reviewed1协议normal/ASan各6106检查、10个编译后运行失败变异，真实执行新Core47读/9写；producer和接收器独立通过，不以host测试替代实机。

实机GMUCOLD一次成功：result0/phase3(RESET_HELD_TCM)，11688读/8196写、addedMask5（AXI和GMU新增请求，CXO原请求1继承），8192个32位word写入及8192个word读回，覆盖完整32768B含holes；TCM CRC/readCRC均f5508f4a，elapsed21.189ms。SYSRESET写1/回读1、root/source资格与child pin保持，不额外宣称独立物理reset ACK。

GMUIRQ一次成功：result0/phase2、installed1/armed1/touched1，Core47读/9写、实际MMIO483读/9写、valid_mask11。已知初始MASK有限set/readback后，HFI MASK=ffff0007、AO MASK=12；Arm阶段真实读到HFI/AO STATUS均0。GIC enable=30000、priority=8080a0a0、两route=200、pending/active0，epoch1/gen1。dispatches0/handler_observed0仅表示未观察物理中断触发，不能把安装、arm或readback称为实际触发；没有强制setpending。原579 MASK0与未导出初始STATUS的失败记录保持。

同次启动root200配置/总线、43页/8pools360448B/3个CPU shared页WIMG6、首次NS306读20写/TLB1及原CAP20 expectedABSENT、PAS INFO/RSC/IOVA/pool/显示IRQ/VSYNC/温度等全部通过，post真实exit0。所有捕获/观察者已结束、串口释放。当前41548B属性仍纳入完整owner ADT测量，无新exclude，原0487 prefix、硬件Root/原bank/加密Data、MACF/ACL/Sandbox与watchdog保持。生成器197输出、注册和打包各一次，未重跑或UTC retime。

下一阶段仍需分别验证GPU-local RSC/PDC、保留BW边界、真正FW release/FW_INIT/HFI_CTRL与VERSION消息；这些不等于完整HFI、GX、设备DMA、GPU或Metal。0576限定通过、0577完整构建失败、0578 Cold75读0写拒绝、0579 IRQ15读0写拒绝均保留。尚未新commit/push。

见[0580限定资格](qualification/0580/reset-held-tcm-irq-qualified.json)、[434+4/5397源码回放](qualification/0580/public-all-source-replay-0580.json)及[连续进度](CONTINUOUS-6-7-8.md)。

# 历史0579：真实reset-held TCM写读通过，IRQ初始MASK零写拒绝（原文保留）

截至2026-11-03：实机0579原SCM/Root/nonce、既有bank/systembag、加密Data、安全与watchdog304.39秒及全部原后续回归通过；43页/3个WIMG6页、首次NS映射和SYSRESET1下完整32KiB TCM写读通过。IRQ在首次硬件写入前因初始HFI/AO MASK均0而返回result13，handler已安装但未armed、未观察实际触发；初始HFI/AO STATUS未导出，不能据默认0推断实读0。整轮accepted=false，FW仍未释放，无HFI消息、设备DMA/GPU/Metal资格；亮度滑块和新增安装继续暂缓。

0579为433共同+4Mac主机补丁/5397源，source/runtime parent578；正式0454只改BootOwner一处held CMD比较，使用原canonical invariant mask，fresh ROOT_ON/DIRTY/UPDATE严格门和所有其他生产文件保持。578原Cold75读/0写失败保留；host真实生产RootHold模型产生cached CMD0x12而fresh CMD2并复现旧失败，是源码/模型推断，不是旧设备private ledger导出。新host normal/ASan各34场景2079071检查及4个已编译运行失败变异通过。实际完整native SHA256 `13b1941441dc87746cc4544a95e233a4114490ee369aa774eb4f9cadb34deff0`，19,384,768字节；1TU actual overlay/applied、5397postbuild、29exports、42symbols/19组BL与原ABI/cache/touch219/fullowner保护通过。协议CAP1..27、13个生产pins和原248/168B ABI不变，578真27测试明确继承，不称重跑。

实机GMUCOLD一次成功：CAP26三份248B缓存一致，result0/phase3(RESET_HELD_TCM)，11688读/8196写、addedMask5（新增AXI与GMU请求，CXO原请求1继承）；SYSRESET写1并回读1，8192个32位word写入及8192个word读回，覆盖完整32768B含holes，CRC均f5508f4a，载入payload18404B，elapsed15.178ms。Root/source资格1、child pinned/root_children1、owner_dead0/quarantine0。这里证明reset寄存器请求/回读与TCM内容一致，不额外宣称独立物理reset ACK；FW保持SYSRESET1，未deassert、未FW_INIT或运行HFI。

GMUIRQ随后只尝试一次：result13/phase0，Core15读/0写（Native MMIO累计108读/0写），installed1/armed0/touched0、dispatch/handler_observed0。GICCTL0x12、原priority word0x80808080、目标source disabled/no pending/no active/level等初始正面检查通过；实际HFI MASK=0、AO MASK=0，与旧“起始全masked”前置假设不符，所以在任一硬件写入前拒绝。**handler安装不等于armed，也不等于物理IRQ触发；579未导出初始HFI/AO STATUS，不能把收据中的默认status0称为实际硬件status0。** 没有强制setpending、清错冒充完成、重试或解除reset。

同次启动原Root/Data/watchdog304.39秒、root200配置/总线、43页/3WC、首次NS306读20写/TLB1、CAP20 expectedABSENT、PAS INFO/RSC/IOVA/pool/显示IRQ/VSYNC/温度等原后续回归通过；这些分项通过保留，整轮因IRQ失败accepted=false，post以exit1结束。所有捕获/观察者已结束，串口释放，人工GUI仍NOT_RUN_OWNER_AWAY。原41548B测量属性/fullADT owner/0487 prefix、旧密钥/bank/加密Data和原MACF/ACL/Sandbox/watchdog保持，未重复579生成器/注册/打包。

下一步是独立0580有限known-mask初始化/readback、fresh ResetHeld门和原始状态缓存候选；最终source manifest尚待Root固定，不能预记成功。真实IRQ arm、实际trigger、FW_INIT/HFI消息和DMA/GPU/Metal仍分别需要实际资格。576完整限定通过、577fullmake失败未刷、578零写Cold失败与本次IRQ拒绝均保留。

见[0579部分资格](qualification/0579/substrate-cold-qualified-IRQ-initial-mask-failed.json)、[433+4/5397源码回放](qualification/0579/public-all-source-replay-0579.json)及[连续进度](CONTINUOUS-6-7-8.md)。只更新脱敏元数据与文档，尚未新commit/push。

# 历史0578：原基线与首次NS通过，Cold75读0写拒绝（原文保留）

截至2026-11-03：实机0578原SCM/Root/nonce、既有bank/systembag、加密Data、安全与watchdog304.373秒通过，43页固件内存、3个HFI/log页WIMG6及首次普通NS映射和原后续回归通过。GMUCOLD在RootPrepare预检75读/0写返回result11/phase0；TCM未装载，依赖IRQ未运行，整轮accepted=false。最近完整限定通过的0576与0577实际构建失败历史保留，尚无固件执行、HFI消息、设备DMA/GPU/Metal资格；亮度滑块和新增安装继续暂缓。

0578为432共同+4Mac主机补丁/5397源，正式0453仅修NativeBoot的四个KERNEL kern头路径；source parent为实际fullmake失败且未刷的0577，runtime/package parent仍为完整限定通过的0576。576→578的运行源码合并为26处/17新文件，不能把它当作正式0453的一文件变化。实际完整native SHA256 `c7fd94e0b3bf4a9c74b47bcffb4643383873a020866bbaf2b38417a14305186d`，19,384,768字节；六TU实际overlay/applied、立即native/HID/5397postbuild配对、29实际exports、42linked符号及原156B first policy通过。原compiler/MACF/ACL/ClassB/20H392 ABI、触屏219槽、cache、原组件与fullowner保持。

同次启动GMUSTAGE真实43页/8pools/360448B持有，currentSecureDT唯一41548B固件属性精确校验，3个CPU shared页pmap WIMG6；属性继续纳入完整owner ADT测量，无新exclude，0487 prefix保留。GMUMAP真实306读/20写/TLB1，CB0/ASID1/VMID0、SID5 translation和SID4FAULT/no RAM、私有epoch1/gen1保持；FSR raw400是Format2、definedFault0，ASIDPNE写0读0。启用普通NS映射不等于设备DMA已执行。

GMUCOLD只尝试一次，source_qualified1，但RootPrepare预检返回result11/phase0、75读/0写、root_qualified0/root_children0/owner_dead0/quarantine0；没有新增clock请求、SYSRESET写入、TCM写读或FW启动。三份248B缓存收据一致，未把拒绝当成功、未重试/恢复/复位；依赖GMUIRQ command/CAP27均跳过，不能宣称IRQ armed或实际IRQ触发。75读路径与RootPrepare快照/fresh gates仍在源码诊断，拒绝根因尚未确定，579尚未固定。

失败后原CAP20 expectedABSENT、PAS INFO/RSC/IOVA/pool/显示IRQ/VSYNC/温度后续回归仍完成，真实VSYNC软件0、温度16+9有效；post以exit1结束，没有写完整post成功标记。所有本次捕获与观察者结束，串口释放。原硬件Root/既有bank/加密Data/安全/watchdog保持；本次没有TCM、FW_INIT/HFI、设备DMA、GPU或Metal资格，人工GUI未复验。下一步先定位零写预检拒绝，再对新候选独立native和实机验收；不会按host模型结果开放执行。

见[0578部分资格](qualification/0578/substrate-first-qualified-cold-preflight-failed.json)、[432+4/5397源码回放](qualification/0578/public-all-source-replay-0578.json)及[连续进度](CONTINUOUS-6-7-8.md)。源码/host、完整native和实机结果分开记录，尚未新commit/push。

## 历史0577：实际fullmake失败，未形成合格native或刷机

0577源为431共同+4主机补丁/5397项，26处/17新文件。overlay预检的额外kern引用路径掩盖了NativeBoot四个裸quoted kern头的缺失，实际完整构建失败；无合格native、未打包或刷机。0578仅按KERNEL使用显式kern路径修正并重新完整构建，不将577失败改成成功。原577失败与576完整限定通过记录保留。

# 历史0576：首次NS映射与固件WC内存实机完整限定通过

截至2026-11-03：0576原SCM/Root/nonce、既有bank/systembag、加密Data、安全与watchdog304.368秒通过；真实43页固件内存、3个HFI/log页NormalNC属性和首次普通NS SMMU映射全部通过。GMU仍停在producer quiet，未装载TCM或运行固件，尚无设备DMA/GPU/Metal资格；亮度滑块和新增安装继续暂缓。

0576为430共同+4Mac主机补丁/5380源，0451仅修改Core和重新审查的Binding，无新生产文件。完整native SHA256 `729a72afe948432892e6d46913ba934e859189f04441782634bbc7253dc2607b`。真实两TU、立即native/HID/postbuild配对、20实际exports、27linked与156B新policy、原compiler保护/ClassB/20H392基类ABI/触屏219槽/cache指令/原组件/fullowner全部通过。

本次currentSecureDT中41548B固件精确SHA经私有snapshot验证；43个4K页预备、8pools/360448B持有、RAM载入22888B、HFI CPU结构完成，3个CPU16K shared页真实pmap WIMG6。属性由575包保留并readonly校验，仍纳入原完整ADT测量，无新exclude/无Data输入、0487 prefix不变。TCM18404B仍待装入，异步HFI未运行。

GMUMAP一次真实success：306读/20写/1次TLBpoll，CB0/ASID1/VMID0/M1、SID5 mask400 translation，SID4FAULT/no RAM；其他SMR invalid/普通CB1..4 M0，protectedCB5..7未读写。私有epoch1/gen1与发布pins保留，仅ATTACHED_QUIET，不自动allow_run。实机SCTLR00c00027，ASIDPNE主动写0并严格验证；FSR raw400保留为Format2(AArch64)，definedFault0。575的17写隔离失败原记录保留，新故障位语义由架构/厂商定义验证。启用映射不等于设备已使用这些页完成DMA。

原CAP20 expectedABSENT、PAS INFO/RSC/IOVA/pool/IRQ/温度回归全部通过，2532页、真实VSYNC软件0、温度16+9路有效。下一步真实五clock冷启动、SYSRESET1和完整32KiB TCM写读，再真实GMU/HFI IRQ、GPU-local RSC/PDC、FW_INIT与HFI消息、CP/BO/fence/像素及20H392 Metal。IRQ arm/readback与实际中断触发分别验收，HFI CTRL ACK与消息通信分别验收，CPU显示、原安全/存储保持；Git尚未新提交/推送。

见[0576限定收据](qualification/0576/gpu-first-qualified.json)、[完整源码回放](qualification/0576/public-all-source-replay-0576.json)与[连续进度](CONTINUOUS-6-7-8.md)。

# 历史0544：真实边缘触点来源，用户确认返回、回桌面和多任务三项边缘手势均恢复

2026-11-02。0541全部布局和滑动已获用户确认；0542触摸坐标、0543内建屏幕关联两轮单独修正后，三项边缘手势均人工失败。进一步核对固定20H392原版UIKit，确认初始触点必须携带FromEdgeTip0x800来源，只有边缘坐标不足以进入边缘识别。

0544在原生物理触屏路径按真实首次落点分类并保留到真实抬起，中央起手不改判；原HID解析、触点生命周期和原UIKit手势识别器保留。根据当前TCM坐标软件分类，不声称Apple触控芯片形态/压力检测；没有合成触点、Home键或完成手势。仅刷boot_b，原Root/bank/systembag、已有密钥和加密Data/安全及原watchdog基础回归304.447秒通过。用户确认返回、回桌面和多任务三项边缘手势均恢复。

399共同+4主机补丁、5290源码文件与实际完整原生构建配对匹配。生产分类/透传524,519主机检查和8运行失败变异、原版UIKit判断指令离线执行、原219/37槽及最终219槽/children accessor ABI均通过。真实子事件增加0x800观察：True。原显示布局、硬件密钥、加密Data、MACF/ACL/Sandbox和原watchdog保留，无新手工持久化偏好修改。

见[0544修正、验证与边界](EDGE-GESTURES-0544.md)及[限定收据](qualification/0544/edge-qualified.json)。有限窗口不替代长时GUI稳定性；网络、GPU/DPU/120Hz、完整SEP、AMP及完整12GiB仍未验收，蓝牙继续延期。本次发布包含配对源码、可重跑测试及限定收据。

# 历史0543：事件关联修正后手势仍全部失败

2026-11-02。用户已确认0541全部布局和滑动正常；0542修正触摸坐标盲区后，返回/回桌面/多任务人工复验仍全部失败。固定20H392的backboardd单独读取事件0xb0019字段区分直接触屏和触控板路径，不能用服务注册表DisplayIntegrated代替。

0543保留原版IOHIDEventDriver解析与分发，仅给本机真实内建触屏的digitizer事件补充该屏幕关联；未合成触点或Home按键。仅刷boot_b并重启，原Root/bank/systembag、已有密钥和加密Data/安全及原watchdog基础回归304.505秒通过。用户复验手势仍失败，继续追踪。原布局/显示、密钥/Data、MACF/ACL/Sandbox和原watchdog保留，无新手工持久化偏好修改。

398+4/5289源码与完整原生构建配对一致。驱动219槽/事件37槽、最终子类219槽、限定原类虚表调用及原版ARM64字段访问执行已核对；生产包装函数210组透传与提供者/原失败保留检查及7个运行失败变异通过。实际事件0→1观察：True。基础回归不替代长时GUI稳定性；GPU/DPU/120Hz、网络、完整SEP、AMP与完整12GiB仍未验收，蓝牙继续延期。

见[0543修正与边界](EDGE-GESTURES-0543.md)和[限定收据](qualification/0543/edge-qualified.json)。源码尚未提交/推送，既有dirty工作保留。

# 历史0542：坐标盲区修正后手势仍全部失败

2026-11-02。用户确认0541全部布局和滑动正常；当前新问题是返回、回桌面、多任务等边缘手势失效。已核对触摸转换仍使用旧(22,18)居中偏移，无法到达逻辑屏幕四边；0418改为原传感器端点映射完整1125×2436逻辑范围。原HID描述符/驱动、显示布局、既有安全与加密Data均保持。

0542已仅刷入boot_b并重启；原密钥/加密Data/安全与原watchdog基础回归304.445秒通过。返回/回桌面/多任务手势的人工GUI复验仍全部失败，不能以基础回归或HID计数替代。

397+4补丁、5288源码文件与完整原生构建配对匹配。生产转换/HID编码5,268,910项主机检查、4个编译后的运行失败变异及193项规范TCM检查通过；没有新的手工持久化偏好修改。详见[0542修正与边界](EDGE-GESTURES-0542.md)和[限定收据](qualification/0542/edge-qualified.json)。蓝牙、GPU/DPU/120Hz、AMP和完整12GiB继续延期。

# 历史0541：逻辑画布统一

2026-11-02。参照两个 hackios 仓库并核对原版20H392的实际canvas选择，修正provider发布物理尺寸导致的逻辑画布不一致。0541实际CA渲染缓冲已从1080×2400中间合成再复制，变为直接1125×2436；真实GOP仍为1080×2400、stride4320。原版屏幕类型缓存4→3，没有覆盖用户态类型选择或系统偏好。

真实保留Surface的锁屏像素中，红色rdar回退条已消失，底部两个按钮左右对称。原始wire、连续图像与PNG逐像素复核通过；这张图来自Surface，不宣称抓取时屏幕点亮。一次自动GOP抓取命中熄屏得到黑帧，失败边界保留。

396+4补丁、5288文件与完整原生构建输入一致；原Root/bank/systembag、加密Data、安全/defaults/RTC/NVRAM及原watchdog基础回归304.467秒通过。0539匹配原版CREATE2类型授权规则的改动仍保留，真实横滑/应用滚动尚待用户返回复验，控制中心和键盘边缘也不能记为全部通过。

见[0541修正及边界](LAYOUT-SCALING-0541.md)、[限定范围收据](qualification/0541/layout-qualified.json)、[真实锁屏Surface](qualification/0541/lockscreen.png)。没有新手工Data偏好修改，原始加密Data与回退备份保留。蓝牙、GPU/DPU/120Hz和完整12GiB边界不变。

# 历史0535/0536：离线初始化后已显示桌面，滑动异常现场

2026-11-02。0535执行一次性受测量约束的文件初始化；0536复用同一封存原生0535内核，移除一次性计划和私人DataArk路径后冷启动，已实际捕获SpringBoard图标桌面。用户确认能进入桌面，但横向滑动及应用内滑动会闪退，不能记为稳定可用桌面。

原版mobileactivationd实际选择显式guest development分支并自行报告Activated；没有预写Activated、创建签名激活记录、连接Apple服务器或宣称Apple官方激活/SEP。原版迁移状态保持success/20H392；全部直接受影响文件和元数据在主机备份，手机保留旧inode，未格式化/替换Data或重建既有密钥。

0535/0536基础加密/旧密钥、defaults/RTC/NVRAM、MACF及原watchdog均通过超过300秒窗口。391+4/5,288源码清单精确匹配。该回归不证明GUI稳定。实际SIGABRT经0537只读诊断确认原版异常为NSInternalInconsistencyException / Failed to create work interval；继续追踪权限与原生创建返回值，不以无条件成功或关闭监控处理。

见[完整阶段报告](OFFLINE-SETUP-0536.md)、[分项收据](qualification/0536/offline-setup-qualified.json)、[实际桌面截图](qualification/0536/springboard-desktop.png)。A设置UI、B原服务本地状态、C稳定交互、D重复重启、E加密回归分别记录。真实网络、官方Apple激活、口令/SEP、GPU/DPU/120Hz和完整12GiB尚未实现；蓝牙继续延期。

# 历史0527状态：无PIN密码请求修复，实机进入设置引导

更新时间：2026-11-02。已通过真实扩展keybag只读查询修复错误密码请求。SpringBoard原版缓存由0526的PasscodeSet=1变为0，FirstUnlock=1；用户及实际GOP确认原版设置引导进入“选取Wi-Fi网络”页面。没有强置用户态状态、伪造验证成功、重建bank或清空Data。

0405仅允许原版只读selector35复用已认证factory状态helper。现有DER状态字节、bank、硬件Root、加密Data和其他操作权限保持。1,163项实际生产分派/状态函数主机检查及5个运行失败变异通过；原版ARM64解码器重新执行通过。完整原生构建384个共同补丁加4个主机补丁，5,287个源码文件逐项一致。正常密钥/Data/安全/defaults/RTC/NVRAM及原watchdog基础回归304.414秒通过。

当前1080×2400实机GOP截图经独立CRC、完整内容、日志分流和PNG逐通道复核并实际查看。界面为设置引导，不是可用桌面；未声称联网、激活、新密码设置/验证、完整SEP或长时间稳定性通过。红色UIKit回退条仍可见，旧0523约2012秒的SpringBoard180秒watchdog问题仍未排除。蓝牙、原生GPU/DPU及完整12GiB仍延期。

见[0527修复与边界](PASSCODE-FIX-0527.md)、[验收元数据](qualification/0527/0527-factory-no-pin-qualified.json)、[原版状态观察](qualification/0527/0527-original-state-observations.log)。此前的[0526只读定位](PASSCODE-READONLY-0526.md)保持为修复前证据。以下0485资料仅作历史基线。

# 历史0485基线（原文保留）

更新时间：2026-11-01。完整功能总览见[仓库首页](../README.md)，资料入口见[文档索引](README.md)。本页取代此前逐轮追加的“最新状态”；旧页原文保存在[历史归档](history/STATUS-before-0460.txt)。

## 已验收与未验收

| 范围 | 当前结论 | 证据 |
| --- | --- | --- |
| 正常启动 | 最近正常运行0485，原版check-in成功完成标志和首份帧复制边界已实测，原版 iOS 用户态运行；原硬件密钥、MKB/systembag、C/D 加密 Data、RTC/NVRAM/defaults 与 watchdog 回归通过 | [0485收据](qualification/0485/0485-shell-frame-qualified.json)、[原始日志](qualification/0485/0485-media-boot.log) |
| 共同源码 | 343个共同补丁 + 4 个 Mac 主机补丁，全部 5,281 个源码文件与实际编译输入一致；固定 Apple OSS 基提交 `1b191cb58250d0705d8a51287127505aa4bc0789` | [公共回放](qualification/0485/public-all-source-replay-0485.json)、[源码清单](qualification/0485/all-mac-source-0485.json) |
| CPU / 内存 | 8 核与 8 GiB−2 MiB（8,587,837,440 字节，约 7.998 GiB）连续窗口已实测；非完整 12 GiB、AMP 或功耗验收 | [平台历史验收](PLATFORM-BASELINE-20261029.md) |
| 触摸 | 0459 人工五点、横竖滑动、双指分离/保持与依次抬起通过原版 HID；新增 2,030 份报告，零队列丢失 | [实际记录](TOUCH-PHYSICAL-20261031.md)、[收据](qualification/0459-touch/0459-touch-live-qualified.json) |
| 音量键 | 真实音量上/下的按下与释放已进入原版 HID；不等于 GUI 消费 | [0429/0430](HID-INPUT-AND-DATA-IDENTITY-20261031.md) |
| 显示 | GOP 控制台可见；原版显示枚举、显示区域及缩放修复已有实测 | [枚举](GOP-DISPLAY-ENUMERATION-20261031.md)、[区域](DISPLAY-AREA-20261031.md)、[缩放](DISPLAY-SCALE-20261031.md) |
| 桌面 | **尚未显示可用 SpringBoard**；原版check-in成功完成标志已置位，桌面像素尚未进入本后端，不把进程存活算作桌面完成 | [UI 等待](FIXED-UI-WAITS-20261031.md)、[迁移现场](CORELOCATION-WAIT-20261031.md) |
| 图形 / 120 Hz | A650/Metal、完整合成呈现、DPU/DSI 生命周期与真实面板 120 Hz 尚未验收 | 120 Hz 为用户提供的硬件规格/目标，不能用软件定时器证明 |
| 蓝牙 | 控制器无线功能不可用；资源、固件与内部 UART 前置证据已归档，按用户安排延期 | [后续入口](BLUETOOTH-DEFERRED-20261031.md) |

0485原生内核：18,967,016字节，SHA-256
`638a4491c6ebc77a9a0301782fe0f8c289f0f8cc53cd2b5009b4c47664095fae`。
0360/0361 只改 NativeAES 和既有 IOSurface OOL 传输；原 nonce KDF、A/B/C/D 与 keychain 代码不变。不是完整 FairPlay、AKS、迁移或桌面完成。

## 当前直接阻塞

0485 的固定原版状态读取先得到 `state=0/checkin-completed=0`，封存回归后的独立快照得到 `state=2/checkin-completed=1`。精确20H392代码核对证明nil-error完成回调才signal该原子字节；同时原版迁移偏好仍为success=true/buildVersion=20H392。这里证明原版初始成功完成状态，不保证后台断连/重登的完整生命周期。

晚期帧计数为SwapBegin序号485、排队2、present调用1。首份/最后处理记录id176、mask0x80000007，但本后端未选择到非零Surface ID、实际复制0字节。原版null-surface路径也设置掩码位，所以不能把它当作已有显示内容。当前证据把边界限定到有效Surface/桌面图层提交进入CPU复制之前；尚未确定上游只形成此类记录的唯一原因，不能据此宣布GPU或DPU故障。

原版UIKit与CA服务线程仍在运行；watchdog318.586秒、旧A/B/C/D与六类keychain、硬件Root/nonce/bank/systembag/defaults/RTC/NVRAM回归通过。完整密钥/FairPlay、可见桌面、GPU/Metal及真实120Hz未验收，蓝牙延期。详见[源码、实机与边界](SHELL-FIRST-FRAME-20261101.md)。

## 必须保留的边界

- 持久bank策略为factory无口令C/D，B与keychain AK/AKU从现有Root/bank用途隔离派生；锁屏口令、多用户、完整keychain/Apple SEP尚未完成；A/B缓存均为内部加密句柄，factory无PIN；非全RAM擦除证明。systembag envelope 承载本后端认证 bank，不冒充 Apple SEP 格式。
- 原版 watchdog 客户端、心跳与截止时间保持；其后端为软件观察器，未声称独立硬件 WDT。0479聚合心跳收据覆盖到321.176秒；这是有限窗口。
- Sandbox 执行代码保留；owner 策略中的硬件类条件做了精确板级映射，不能称策略数据未修改或具有 Apple 签名认证。
- development-fast 是正常开发策略；完整 System/SystemOS 内容检查保留 0341 证据，最近独立原版只读 Data fsck 为 0416。本次没有重新执行全盘扫描或 fsck。
- 0459 真实触摸只验到原版 HID 接收；GUI 消费、屏幕像素对齐、超过两指及物理采样率未验收。
- 部分旧阶段文档仍描述当时的 HFS+、单核或触摸未采样状态，作为历史证据保留，当前结论以本页和对应更新的验收收据为准。
