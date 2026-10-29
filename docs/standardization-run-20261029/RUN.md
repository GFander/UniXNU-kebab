# 七小时连续开发窗口

- 用户授权：依据STANDARDIZATION-PLAN.md持续推进7小时，手机可自由刷测。
- 开始：2026-10-29 02:22:25 Asia/Hong_Kong。
- 截止：2026-10-29 09:22:25 Asia/Hong_Kong；届时结束新增改动，完成在途安全收尾并汇报。
- 续跑自动化：`hios`，当前线程heartbeat，每30分钟检查，限定14次；截止收尾时暂停。
- 起点：0193-ro / native0192 / APFS只读，手机c073a488在fastboot。
- 回退：D:/LOS/HiOS/p1-run/prelink-fix/boot-0193-ro.img。
- 数据：已部署APFS的userdata不重复覆盖；boot_b为常规刷测目标。阶段B写入仅在有明确验证目标、保留可恢复输入后单独开展。

## 当前批次

09:22 收尾：七小时窗口已结束，hios续跑已由app工具暂停且TOML确认PAUSED。稳定版0225，native=e7cb391447d5a6aaf2feb45a3e5d48191996ca775f179a40adecc38f09b1655b，boot=4a9601ee935d4530bac165e16322faba3705a27546995b377f15d18b142080cf。18分钟4点只读观察PASS；随后225首次完整65536字节FWLOG PASS、PON正常、normal sync FASTBOOT PASS，最终c073a488 fastboot/active b。无本轮在途构建、刷机或COM捕获。

最终17 PASS/0 FAIL/4可选IPSW SKIP；176共同+4 host补丁、5192全源目录输入重放一致；51源码条目/7ABI/原版storage与AES审计和独立设备证据交叉一致。所有结果见STANDARDIZATION-RESULT-20261029.md、BUILD-BASELINE-20261029.md、IMAGE4-BLOCKER-20261029.md及evidence-0225.json。源代码/记录已镜像Ubuntu仓库，未commit/push；此前未提交内容保留。

计划真实边界：A1/A2结构与回归已过，USB普通服务/线程、HID/真实电源等A3缺口仍在；B1 Preboot通用持久化通过，B2专用nonce selector2212后端未实现，Data内容保护/Cryptex/DSC/完整launchd和图形/UI未通过。不要把计数或Preboot proof当完整iOS启动。不要再运行旧过渡生成器、216写模式或用checkpoint260的raw覆盖设备261。

08:58 更新：已验证0225常规配置（native与0224完全相同，去掉driver自测及SPMI lockpanic参数，AES/SCM实验关闭）。0224完整FWLOG测试：25次真实DRV?填环，written92558，完整65536字节且无自身新头污染，后续PON正常；持normal SPMI锁PANIC!后FWLOG?及FASTBOOT也PASS。正式0197最终包含双快照和普通TX分块排空，已保存repo并重放；0223只修快照未排空TX导致full-dump超时，保留negative JSON，不能作合格版本。0225根卷/proof/两文件全SHA/原诊断/keys/11角色及正常sync FASTBOOT PASS。51源码条目、7ABI、AES181/162和镜像/config独立交叉验证PASS，证据evidence-0225.json。

176共同+4 host补丁新目录严格重放，5192源目录文件完全一致（all-source-replay-0224.json）；verify-all-complete.log为17 PASS/0 FAIL/4可选IPSW SKIP。Mac实际源码是fresh tree，旧完整树保留。持久Ubuntu重放树/home/albert/.cache/hios-series-tv4g26dw现也补齐host0002–0004，不能再当只含host0001的旧状态。

当前手机运行0225，正在18分钟只读稳定性观察，Windows session73302（soak-device.py），4个采样计划为0/6/12/18分钟，开始08:51:17左右，预计09:09:30前完成。每次PON、PID1、两份系统文件全SHA，期间不要占用COM7；首点PASS。soak boot记录0225-soak-boot.log，构建/刷机均无在途；观察后再测225首次完整FWLOG，再正常FASTBOOT收尾。截止仍09:22:25，须暂停自动化hios，更新最终状态/文档并结束，不新增功能。

08:20 更新：0222已从新生成的XNU目录clean全量构建并完成真机原诊断/keys、25实现/19类/11角色、System/Preboot RO及proof全SHA、两文件SHA和panic FASTBOOT；manifest/7ABI/AES181-162更新222。第一次fresh构建暴露availability脚本遗漏；扩大至全部源目录文件后补齐Private.exports和Libsyscall.xcconfig，修复LLDB的stdlib collections遮蔽，保留真实检查（8个正负案例通过），不沿用旧禁用检查补丁。175共同+4 host补丁严格重放，5192个源目录文件完全一致；verify-all加入host重放和validator后16 PASS/0 FAIL/4可选IPSW SKIP。原Mac完整树保留在/Users/albertlee/kebab-build/xnu-before-clean-replay-0222。以后prepared源码用start-full-build.py VERSION --prepared-source（CODEQL=1在该builder仅跳过Git patch循环，无CodeQL任务）。所有Git仍仅Ubuntu。

当前新候选0197修复FWLOG满环读时自覆盖，已主机真实复现旧错误。normal/panic两个固定64KiB快照；copy时暂关IRQ并恢复调用者状态，printf前完成复制；同上下文重入拒绝，panic独立槽不等待normal。24种完整内容/边界/IRQ+2重入+panic打断normal测试PASS，真实TU PASS。0223全量构建进行中（prepared-source，PID87955），待打包/上机正常和panic FWLOG及恢复后才保存正式197。当前boot_b222、fastboot，无COM捕获。后续仍不重复整体写userdata/216 proof写模式。新成果文档STANDARDIZATION-RESULT-20261029.md已按已验证222生成，candidate不得计入完成；B2具体证据见IMAGE4-BLOCKER-20261029.md。

07:33 更新：0218四文件KEBAB编译范围保护已全量构建、刷测并通过原有全回归/panic FASTBOOT；七个函数正文之外未改动运行逻辑。0194已正式保存并独立重放。0219固定SCM能力调查（默认关闭，显式probe=1上机）完成：SMCCC raw10001、SCM64自查询status0/value1、ICE invalidate/set可用、QSEE version不可用，均1次返回。无目标key操作。15种生产逻辑ASAN/UBSAN、BSD TU、全量构建、Preboot全SHA、两文件SHA、诊断/keys/11 registry角色和panic FASTBOOT通过；0195正式保存并独立重放。manifest-0219为24主要文件/19类/11角色；7份ABI和AES181/162审计均更新219。

补丁可复现性已修复：0167旧上下文、0170严格重生成、0173-z遗漏storage输入、0193遗漏已运行RAM日志/媒体/prelink生命周期胶合。172/172严格重放，verify-all 8 PASS/0 FAIL/4 IPSW可选SKIP。10个Mac构建准备差异保存host-scoped overlay，非通用BSP；172共同补丁+overlay与实际Mac 4205个列明源码/头/配置一致，另两个0字节不编译Data遗留。实际3585个builder/fakeroot输入已哈希，BUILD-BASELINE-20261029.md说明SDK/KDK与旧board.toml局限。

当前正在0220全量构建，候选0196把7个原复位函数、IMEM/probe/armed状态从USB抽到resident C reboot层。反向重命名核对7个函数体+init块完全一致；actual-function ASAN/UBSAN已覆盖正常sync、panic无VFS/PE、零/负reset返回、PON门控、无IMEM、PON失败、reset2资格、诊断拒绝和early halt，真实TU PASS。需全量/上机后才能正式保存。最后补了header的C++链接guard，C路径预处理不变；为使全量构建输入完全固定，等当前220退出后再做一次clean220构建（不要在仍运行时清obj）。手机现在219、fastboot，无COM占用。下个镜像移除kebab_scm_probe，保留Preboot mode3、ufsrw0/rootrw0/AESoff；先测正常FASTBOOT，再用同native加lockpanic配置测锁内panic恢复。不要重复执行216写入或整体重刷userdata。

06:33 更新：0217只读冷启动验证PASS（Preboot dev1/4与System同backing、两卷RO、ufsrw0）；完整65559字节重读SHA与216相同，APFS checkpoint实际xid261。已抓217 DRV?，11角色有效；全回归/PANIC!→FASTBOOT通过，当前boot_b217、fastboot、无COM捕获。0192最终已保存正式repo并独立重放。manifest-0217.json核对24主要实现文件/19类/11registry角色与列明support路径在Ubuntu/Mac/独立重放一致；7份原ABI记录更新到217，IOAES181槽/162继承匹配也核对217，仍明确2212未实现。

正在做后续B2只读安全固件能力调查的资料准备，尚未新增SCM源文件或刷测。官方catalog没有可直接套用的SM8250密钥后端；不可把Sm8550平台或SoftwareAES当成UID/SEP等价。GitHub connector先404（旧路径）后429（qcom子目录），已后退到原始固定版本源码读取；web已打开torvalds/linux/master/drivers/firmware/qcom/qcom_scm-smc.c和qcom_scm.c，仅核对前不可执行未知调用。计划只固定SMCCC版本/SCM调用可用性等读取，无共享内存、无任意SMC、无密钥/熔丝/RPMB写入。需要先读实际Linux约定，再决定实现。完整verify-all.sh已在Ubuntu启动，日志/home/albert/verify-window-20261029.log（Windows session33944），待读结果；不输出remote URL。ROADMAP已更新217/Preboot持久化/Data内容保护与2212缺口，STATUS/HANDOFF/A3及最终repo镜像同步还需继续。

06:26 更新：0191正式最终版已保存repo并独立重放，内容为Preboot只读探针（不是早先Data候选）。0215实测Preboot UUID EF45... /dev/disk1s3 dev1/4 与System同backing，readonly=1/System-readonly=1，mount syscall=0；全量根卷/hash/keys/扩展诊断/panicFASTBOOT回归PASS。

0216全量构建后创建显式命名的boot-0216-preboot-write.img（不是-ro镜像）：ufsrw=1/rootrw=0/probe=2，driver自测参数移除，AES实验关闭。pack-preboot-write.py核验FAT其他文件、ADT非owner输入、firmware完全相同，owner各项测量正确。19种真实SHA/VFS故障+26种挂载/读写门控/耐久错误路径+7种多卷sync主机测试PASS。真机Preboot RW/System RO，独占新建/private/preboot/.hios-apfs-proof-v1，65559字节跨4K/16K边界，完整写读SHA=3f2f47b49455f2d5cce7d745829440ccf0e8f1beda6c5120641c25213f78c957，APFS VFS_SYNC=0/UFS SYNCHRONIZE CACHE=0。System launchd/dyld外部全SHA保持。INST CHECK对Preboot路径按原白名单拒绝，未放宽安装权限；proof由native完整读取/逐字节比对/SHA及下一次重启验读资格确认。随后真实DRV?与正常FASTBOOT（多卷VFS_SYNC+UFS cache+PON/IMEM）通过，已回fastboot。

当前0217已全量构建（只纠正sync注释，实际代码同216）、打包和确认Sending/Writing OKAY后启动，正在抓0217-ro-boot.log，session16276。ufsrw=0/rootrw=0/probe=3（Preboot只读重启验证），--selftest；不要重复执行216写入，文件O_EXCL设计拒绝覆盖。下一步确认reboot-read同65559字节SHA和readonly双标志，抓DRV?（在regress-device之前！），再跑217完整回归。0192仍候选，待冷重启验证后保存最终apfs-write-proof/0192并独立重放；Mac根目录0192还是早期稿，promote时须覆盖最终再重放。实际源树已是kalloc_data/kfree_data与准确的vfs_iterate“iteration reference”注释。准备脚本prepare-apfs-write-proof.py已可从before不可变快照重建。

06:00 更新：正式补丁已到0190（187–190已独立重放，SoftwareAES许可证另存repo/licenses）。0209分阶段证明raw输入被IOMemoryDescriptor读为全零。0210仅修正KEBAB静态UPL下界，从只覆盖0xfffffe0008000000后的prelink改为同时覆盖0xfffffe0006004000起的native；真实常量input=0xfffffe000602e8f4。4/16KiB主机walker/cap/未映射页拒绝测试PASS，真机CBC128/192/256、descriptor/offset/callback/bounds、raw加密和原地解密全部PASS。随后原版Image4请求selector2212/bits128/op0/bytes16，明确unsupported并panic；串口FASTBOOT两次自动恢复。没有伪造硬件key。0190使kebab_software_aes=1显式启用，默认关闭。0211负向KAT（仅改公开预期答案）拒绝注册，根卷/hash/keys/扩展诊断/panicFASTBOOT全PASS。0213是同一211 native的正向配置，用于捕获2212，不是新源码构建。

0214 Data只读挂载候选：已通过target TU、原版20H392 mount_apfs字段/调用点审计与15种host故障/引用测试；真机准确找到UUID4C56203C...、disk1s2 dev1/3、与System相同backing=10000013e。原版APFS在完成挂载前panic“unencrypted data volume is not allowed”，无Data文件写入。此0191候选尚未正式保存，不能算Data已挂载。

当前正在全量构建0215：改为同容器允许未加密的Preboot（UUID EF45F98E-9F6C-43DB-B7C6-F418CA77429B，/private/preboot）做独立APFS存储资格检查；先只读，后明确测试文件写/同步/重启读取。Data内容保护与Image4派生key是另一个尚未完成的安全后端关卡，不使用FastSim身份或原版APFS/Image4返回值补丁。当前手机boot_b0214，panic，COM捕获已结束，下一步先串口FASTBOOT再刷215。ufsrw/rootrw仍0，userdata没有重复覆盖。Mac 0215 PID3617/build-0215-full.log。

实际source树已由apfs-probe-transition/switch-to-preboot.patch从Data候选转到Preboot探针，含删除旧kebab_data.c/h、加入kebab_apfs_probe.c/h。最终0191应从apfs-probe/before→after生成，不能把旧data-mount/0191当最终补丁；旧候选仅留复现记录。新bootarg kebab_apfs_probe=1，打包215时必须删除旧kebab_data_mount和AES测试参数。新回归通过前不promote0191。Data挂载ABI取自原System/sbin/mount_apfs SHA ccac6fa5b10c74ac9458212044bdc4115831c7a01be04d61d04e86bdec52dc27，匿名uid/gid默认ffffffff；并非直接套21H461。

05:17 更新：0207 根卷/hash/keys/扩展诊断/panic FASTBOOT 回归通过；0185/0186已保存正式repo并应用独立重放。0208 SoftwareAES候选全量构建/打包/181槽与162继承匹配检查通过，真机确认原版AES/crypto/service大小288/136/136、原版metaclass地址正确，但 KAT=2/interface=2，故未注册服务；不能作为AES能力验收。正常FASTBOOT已回引导器。0209正在全量构建，只加公开测试向量的分阶段日志，以定位descriptor与raw-buffer接口之间的失败；不降低KAT、不修改原版Image4。0187/0188仍为候选补丁，未加入正式系列。

05:09 更新（以下旧段落保留为过程记录）：0204–0206 真机回归均通过。0183 加入有界 PROC?/PID1?；0184 只读栈快照定位 launchd 的 sysctl→AppleImage4 gate wait；0185 默认关闭的 PID1 sysctl 跟踪进一步确认 `security.mac.img4.ignition_blob`，oldlen=132，handler=0xfffffe000891c330。此前 OID 正常返回，该 OID 没有 leave。0206 panic FASTBOOT、根卷、两文件全量 hash、keys 和扩展诊断全部通过，boot sha256=ed9154a7c494b102afc4780ce4ce6d43b2ca64427f4dda825da15a1242b8258c。

0207 全量构建、打包、回读通过并已确认 Sending/Writing OKAY 后启动，正在捕获。0186 仅恢复被 platform excludeList 排除的 defaults IOPlatformDevice，让原版 AppleImage4 personality 有机会匹配；DRV? 加入可选 Image4/AES 实例观测。原版 AppleImage4::start 会无限等待 IOAESAccelerator，随后才打开 ignition gate。下一步验证实际匹配与等待，不修改原版 handler 或安全返回。

已按 A4 查询官方组件目录并固定 SoftwareAES 4a45326da5c2cd6fe6813d79ee43db20805628af（UniXNU catalog 33da81310819b86d3ea62cb704f156c56407764a）。上游只声明 iPadOS17.7.11/21H461，因此移植前必须重新核对本机 20H392 IOAES vtable、metaclass、布局与调用 ABI；不能复制上游固定地址。正式补丁及独立重放已到0184；0185/0186验证后需补存。所有版本仍 ufsrw=0/rootrw=0。

当前已验证0203-ro：A1 SPMI分层/3个类型化客户端，A2 GENI/TCM分层/2个IOKit服务，DRV?实际registry状态，keys devfs延后发布均已通过真机。手机boot_b0203、fastboot，无后台捕获。ufsrw=0/rootrw=0，userdata未重复写入。0203 native=e0e036a522979cb97a1896eff73eb2757c64ef02f495aed1e804738471bc16df；boot=47811bc8d18ddcaa27cd87c92746192d2541dd507fa241aded1bed7b21156b1f。

0202 typed GENI PASS：proto3/mapped1/bb0/identified0/decode0/node0；正常CLK/SE/PING与0201逐行一致（仍是未加电ping超时后cancel恢复，未得到触摸采样），正常及panic FASTBOOT通过。0203 keys devfs VFS lookup=0、VCHR/dev_t正确，major21、24-byte records；其余自测/扩展诊断/root/hash/panic回归全PASS。0175–0182均已正式保存并应用独立Mac重放树。A3-GUIDE-REVIEW.md已形成，尚需更新其0202/keys状态；manifest/ABI记录/主文档仍待收尾同步。正式Ubuntu scripts/make-kebab-adt.py已加入两个bus节点；repro/test-generator.py证明旧节点及nonce/BNCH完全保留，新节点与实际刷入overlay一致。Windows根目录旧make-kebab-adt.py保留，不能拿它覆盖正式生成器。

正在做B2只读可观测：bootdiag/0183（BSD进程入口与Mach私有线程快照分文件）已应用Ubuntu/Mac实际树。0204全量构建进行中，Mac PID17947/build-0204-full.log。15命令parser/KDP边界、panic全部普通诊断拒绝、快照proc/task/thread引用/锁/cap32/64主机测试PASS，真实TU检查PASS。不会暂停线程、访问用户内存或写文件系统。尚未打包/上机/正式保存0183。Mac最终0183-kebab-process-observability-final.patch已覆盖为当前稿，源树一致；不要重放无-final初稿。下一步0204构建完后package 0204 0203（可移除selftest），刷机捕获100秒，然后PROC?、PID1?至少两次，保存进程/线程等待与CPU时间，按native符号解析continuation，继续定位真正等待。完整launchd、Data/Preboot、Cryptex/DSC和UI仍未通过。
## 固定规则

每批单一目的；生产代码检查/合适主机测试；Mac删除经验证的obj/sym后全量构建；核对新native hash；prelink与loader/FAT/owner校验；Sending/Writing OKAY后才重启；捕获早于重启；对比根卷、原有日志/诊断、文件hash和FASTBOOT。失败回到上一已验证镜像，不把失败当完成。

Git只经SSH Ubuntu；Mac实际源树用patch；不运行会fetch/reset的build-local。现有实验基线/未提交修改保留。早期/panic路径不引入IOKit、锁或动态分配。

## 进度

- 02:22 读取用户计划、最新交接和执行记录，确认fastboot连接；建立续跑与截止检查。
- 0194是原0193 native/prelink，仅加kebab_keys=1的对照配置；按键初值pon04/gpio6=01/gpio7=01/ident10。SPMI/稳定PON/TCS日志已保存。
- 0195全量构建与零语义抽取检查通过；设备在按键并发下PON读出其他寄存器值，回退0194。0194重复PON也出现10/00、01/01与错误subtype01，确认旧实现就有争用。panic FASTBOOT仍正常。
- PWR?/TOUCH?在普通串口解析分支缺失，基线为0响应；之后单独做诊断可达性修复，不混进抽取。
- GitHub新认证已验证账户GFander，令牌在本机DPAPI凭据及Ubuntu内存缓存中，未写入源码/日志/remote URL；实际响应过期时间2026-12-28 02:38:35香港时间。
- 02:55 S0通过：native0196=6b45cdd43a2392381816a17878ae14c324196df2c32ab7679f244bbad4c54d8b；boot0196=b22b221537e0295afce3e6ba46d0f09662b6c45765c5a325cbade549617f768d。12并发主机测试/旧reset回归均PASS；24/24真实PON读数正确，锁内故障注入panic与FASTBOOT成功，证据0196-regression.json。新函数旧panic core保持原样，normal wrapper/diagnostic/key paths加共同mutex；USB按panic_active选择无锁core或normal wrapper。
- 03:04 A1a重试通过：native0197=e6b3c94a7ddcc23f648f4cf378c849ea5e1f119ba224b6674417712fae73c4c3；移除lockpanic测试参数，keys=1保持。0197-regression.json证明与0196诊断逐行一致、按键04/01/01/10、两文件hash和panicFASTBOOT全通过。0176已正式保存并应用重放树。
- A1b在a1b/目录：新增独立PON、PMIC按键、只读rail客户端与RPMh/RSC适配文件；bus state/VA仅private头可见；正常mutex已移入bus，panic无锁core保持。0177最终补丁须重新运行make-patch并上传（Mac原0177文件仍是初稿，实际树已追加compile-fix1–4）；不要用旧稿重放。
- 03:29 A1b通过：native0198=968b489fe66f4bd6d72535f3d3b93b6abf597c95725823a22b1c42ded67946e5；boot=805901e1dd4f3d06342cc2bff50dc2fddd9aaf214256a23a9dee04ccbbeaa998。0198-regression.json全PASS。0177最终稿已重新生成、上传Mac及正式目录、应用重放树，上条“旧稿”提醒已处理。
- A1c明确边界：KebabSPMIBus匹配root绝对资源node与IOBSD；3个scoped KebabSPMIDevice nub由personality匹配KebabPon/KebabPmicKeys/KebabVrm，RSC标为独立transport的依赖策略，不冒充SPMI事务。正常IOKit服务有自身锁/生命周期，但panic只用C core，C持久映射不由IOKit stop释放。新状态查询是只读；keys仍遵守原boot-arg，未加HID或真实IOPM。
- 03:46 A1c全验收通过：0199真实typed PON04/00、keys04/01/01/10、RSC TCS1=0，3子服务和原生自测均PASS；0178已同步正式repo/重放树。无自发panic。故障注入持有normal bus mutex后PANIC!仍经无锁core自动返回fastboot。


