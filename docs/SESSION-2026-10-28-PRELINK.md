# 2026-10-28 prelink 修复记录

## 已验证的根因与修复

0172 的 `__PRELINK_INFO` 为 `0xfffffe00070a4000 + 0x31000`，而组件最高地址为
`0xfffffe000adf4000`。非 fileset 的 arm_vm_init 把 `vm_kernel_slid_top` 设置为
`vm_prelink_einfo`。OSKext 滑动组件段表时调用 ml_static_slide，后者对这个上界之外
的地址返回 0，因此文件段表虽然正确，运行时的段 vmaddr 却被清零。

NativePrelink 补丁 `0002-prelink-info-after-components.patch` 把 INFO 放到原内核和新增
组件段的最大末尾。新地址为 `0xfffffe000adf4000`，末尾为 `0xfffffe000ae25000`。
旧 emitter 重生成 0172 与原镜像逐字节一致；仅改布局的候选只有四个字节不同
（INFO 段和 section 的 VA）。26 项工具测试通过，加载器检查通过。

仅修改布局后，真机进入 corecrypto 注册路径，停止在 prng_random.c:401。
0173 曾按“备用 PRNG 在先、供体在后”的假设加入接管逻辑，仍在 :429 失败。
该方案已撤回。0174 仅编译了诊断版本，未刷入。

实际顺序是 kernel_bootstrap_thread 的 PE_init_iokit 在 EARLY_BOOT 之前执行。
corecrypto 已注册 PRNG，随后 negroni_prng_startup 才尝试注册备用实例。
`0164-kebab-prng-prelink-fallback.patch` 在备用启动函数检查 prng_ready，已有提供者时
直接返回。register_and_init_prng 本身及原有断言完全不变。
测试脚本从实际源码提取启动函数，验证独立内核仍注册、已有提供者时不生成种子也不注册。

0175 全量构建通过，内核 SHA256：
`ce8b026c6dafbecfdbb389c4d4d27b76c8dc1f1004b9a14c430114a45e2ab70a`。
真机越过 PRNG panic，停止于 AppleImage4 的 `failed to get mix-n-match policy: 2`。

## AppleImage4 的开发启动输入

供体回调读取 `/chosen/mix-n-match-prevention-status`，要求 uint32，取反得到 AMNM。
0176 仅增加此属性为 0；真机越过该检查，下一次 panic 是
`failed to read boot nonce hash: secure boot level = 0: 2`。

BNCH 读取路径为 `/chosen/manifest-properties/BNCH`，允许最多 48 字节。
0177 使用 0175 同一内核，额外生成 32 字节开发 nonce，并提供其 SHA384 哈希为 BNCH。
nonce 存在 `/chosen/boot-nonce`，两者经回读测试确认匹配。
这是自定义 UEFI 开发环境声明，不代表 Apple Secure Boot 或硬件认证。
生成器新增显式 `--development-boot` 参数；默认输出不包含这些开发属性。
默认/开发两种路径的回归测试通过。

0177 最初没有成功刷写回执；用户指出设备仍在 bootloader 后重新执行，取得
Sending/Writing/Rebooting 三项 OKAY。真机越过 BNCH，停止于 nonce 管理器无条件
读取前级清单哈希（ENOENT=2），与更早的 AMNM 可容忍读取属于不同路径。

0178 增加 owner image-set JSON，记录实际固件前缀、内核、基础 ADT、配置的 SHA256
及开发 nonce；其 SHA384 作为 boot-manifest-hash，另附明确的 owner domain。
这是自定义开发启动集的测量，未建立 Apple 认证链，也未测量或验证根卷。
nonce 管理器检查通过，COM7 捕获到下一次 data abort。

反汇编证实 AMFI 旧的提前初始化目标 0xfffffff0056fb000（滑动后 0xfffffe0008933000）
是一条 B 到 0xfffffe000894fe28 的跳板；后者经 GOT 0xfffffe0009eed6b0 跳入原内核的
`__ZN11OSMetaClassD2Ev`（0xfffffe0006952228）。错误调用把 Mach-O 地址当成 this，
使 x8 读到段名 `__TEXT_E`，在 0xfffffe000695224c 访问非法地址。
AMFI 正常 kmod start 为 0xfffffe0008943798，原额外调用必须删除。
0179 已删除该 metadata 提名并重新计算 owner manifest，尚未刷入。

0178 使用 kebabkdp=1 提前启动 USB，但 panic 后进入 KDP 等待；FWLOG 无响应，
FASTBOOT 写入超时，设备未复位。0165 新增 kebabusb_early=1：提前启动普通控制台而
不启用 KDP。12 种参数/驱动状态组合测试通过，0180 正在全量编译。
APFS 尚未验证可挂载，不能宣称修复完成。

## 文件与复现

Windows 实验目录：`D:\LOS\HiOS\p1-run\prelink-fix`。
最新候选：`boot-0177.img`，SHA256
`ddb189662dce75694f6d1ff881e215314c1989ef721d8dbe9c6e802ed370f152`。
回退基线仍为 `p1-run/boot-KP.img`。本轮只刷 boot_b，未刷 userdata。

Ubuntu 的 `/home/albert/kebab-dev-ios16` 保存了工具补丁、XNU 0164、两项回归脚本和
`patches/experimental/0172-runtime-base.patch`。后者保存进入本轮时 s4/x 的未暂存
实验修改及 KebabPrelinkBindings.cpp，需在 142 个旧补丁之后应用；不能忽略它而只用 HEAD
重建实验内核。s4/x 也已同步最终的 PRNG 修复。

macOS 实际构建源码：`~/kebab-build/dxb/xnu`；构建日志 `~/kebab-build/build-0175-full.log`。
每次构建前清除了 xnu.obj 和 xnu.sym，构建后校验哈希变化。

验证入口：

```sh
cd /home/albert/unixnu-src/kits/NativePrelink
python3 -m unittest test_emit_prelink test_native_prelink
cd /home/albert/kebab-dev-ios16
python3 scripts/test-development-adt.py
# 在有 clang 的构建机上，针对已应用 0164 的实际源码：
python3 scripts/test-prng-startup.py /path/to/xnu/osfmk/prng/prng_random.c
```

## 恢复调试后的 0180/0181

LOS 干净重装曾清空 iOS 根卷；随后用户完成 LOS 工作并明确要求继续、重刷测试固件。
本轮仅把 0180-ro 写到 boot_b 并切换 b 槽，UFS 写入关闭，LOS a 槽及 F2FS userdata 保留。
0180 普通早期 USB 成功捕获 `amfi interface is stale: 0`。内置 AMFI 的 TrustCache
版本确实为 0，而供体服务的接口尚未注册。

反向调用链恢复了真正的无参数共享初始化函数：原址 `0xfffffff005703c54`，滑动后
`0xfffffe000893bc54`。它由供体服务 start 调用，以 IOLock/CAS 保证只执行一次，
在 `0xfffffe000893be30` 调用接口初始化助手 `0xfffffe00089383f0`；助手在
`0xfffffe0008938470` 调用导入的 amfi_interface_register。共享初始化函数再于
`0xfffffe000893bfa4` 调用 mac_policy_register，提供真实策略回调。
该函数不读取输入参数；与此前误用的析构入口完全不同。

供体 AMFI 表 `0xfffffe0009eee9a0` 的 +0x118 是 TrustCache.version，值为 3，
与本内核启用 CoreEntitlements acceleration 后的布局一致。
0166 只让内置备用提供者在已有 AMFI 时返回，保持所有原有重复注册和版本检查。
0181 使用真实共享初始化地址作为早期提名，已全量构建、打包、回读和加载器检查通过。
145 个补丁及实验快照在独立的干净 XNU 克隆中重放通过，三组提供者/USB 启动测试通过。
0181-ro 当前尚未刷入；等待设备回到 fastboot，仍未验证 APFS 可挂载。

## 0182/0183：AMFI 顺序与 panic FASTBOOT

0181 已实际刷入，panic 为 AMFI 重复注册。EARLY_BOOT 备用提供者先于
mac_policy_initmach 的真实组件注册，单纯检查已有接口仍不能解决顺序。
0166 已改为在 mac_policy_initmach 之后、trust cache 初始化之前显式注册备用提供者。
0182 全量编译通过，未刷入；0183 同时并入用户要求的 panic FASTBOOT 修复。

0167 在底层 halt_all_cpus 为没有平台驱动的早期 panic 接管 USB，防止进入空循环。
panic FASTBOOT 不再退化为普通重启：先用启动期预映射的 SPMI 和 IMEM 写入并验证
引导器原因，再直接 SMC SYSTEM_RESET；不调用 VFS、分配内存或查找 IOService。
普通重启仍保留安装同步；标记不匹配不复位，复位返回时恢复旧 IMEM 值。

test-panic-fastboot.py 提取真实函数并模拟硬件，覆盖正反路径和低层 halt 接管；
新版通过，旧版断言失败。0183 尚在构建，未进行真机验证。

0183 后续已全量构建、刷入并真机验证。AMFI 真实策略加载成功，备用提供者保留
TrustCache version 3，启动越过 lockdown。新 panic 为 AppleARMCPU 误匹配高通 CPU。
panic USB 命令循环持续工作，FWLOG? all 成功取回 17,083 字节完整启动日志；
FASTBOOT 验证 PON 0x04 与 IMEM 0x77665500 后直接 SMC 复位，设备自动进入
fastboot（c073a488）。没有按键操作，日志为 0183-panic-fastboot.log。
0183 内核 SHA256 72e1f4f2d4f8c73d2f1da7b76fcff0824f7a845b3173fde24ce854b06c0ccd59，
boot SHA256 c7410dfba6e907fbe3ab1dbbea1985319b1472cd2776072f76c52505429980f8。

## 0184：原生 CPU 服务匹配

FWLOG 证实 AppleARMCPU 对高通 CPU 做 Apple 设备树校验；原生 KebabCPU 虽已链接，
personality 却位于 NEGRONI 条件块，且错误匹配根 IOPlatformExpertDevice。
0168 将其放回 KEBAB 条件块，匹配 CPU 的 IOPlatformDevice/cpu，score 10000 高于
供体 AppleARMCPU 的 100。被 cpus=1 排除的 CPU 仍保留 DT 节点，但不注册匹配服务。
真实 MPIDR 不变，没有伪造 Apple 中断或 IPI 属性，也未修改供体机器码。

三个实际编译的配置目录字符串（KEBAB/NEGRONI/default）测试通过；补丁已应用到
实际构建源码、独立重放克隆及 Ubuntu s4/x，0184 正在全量构建。

0184 后续已全量编译、刷入并验证：原生 CPU 0 initCPU 与 SGI1 成功，1/1 CPU 就绪，
平台专家注册后进入 BSD 初始化。新 panic 为 AMFI 后台线程缺少 ApplePMGRNub。
完整 17,916 字节 FWLOG 已保存，panic FASTBOOT 再次成功，手机自动回到引导器。
当前活动 b / boot-0184-ro.img / fastboot，userdata 和 LOS a 槽未修改。
内核 SHA256 f76e10af0a6a862b984e830ebdcb836924b94a674ce1390990f3b2e4bbe5e8a1，
boot SHA256 62e9053810f0eb7f6a65c0ff8a1f45ecd59da38797307ead26f73cec291ecc41。
PMGR 调用链与下一步证据记在 HANDOFF.md 第十一节，尚未加入任何 PMGR 兼容占位服务。

## 0185–0187：本轮最终状态（覆盖前文待办）

0185 已发布按本供体审计的原版 provider-less ApplePMGRNub，AMFI 完成，原版 APFS 2142.140.9.704.1 启动并注册 sysctl。AON 读 0xffffffff / 写无操作，这是开发兼容路径，不是实际 PMIC 电源实现。生命周期测试及 donor ABI 审计通过；0169 补丁已保存。

0186 的 0170 修复 device-type=Generic 匹配、真实设备 start、媒体子树校验和 dev_t 身份。真机证实 IOBlockStorageDriver → IOMedia → disk0 major1/minor0，媒体大小 241008750592。仍在 Android userdata 上测试，APFS mountroot 失败2、最终 errno19，尚无有效 APFS 根挂载证据。0185/0186 均成功 panic FWLOG 和串口 FASTBOOT，无按键返回。

0171 / 0187 已全量构建并准备，但未刷入。ufsmedia_root=2 先启动 UFS 再保留标准卷 UUID 匹配，缺少字典时拒绝。实际代码三种路径及失败测试通过。ADT System UUID DADD93F8-DBBF-4B17-A1DF-0F11E64172F7，ufsrw/rootrw 均0；独立 FAT、固件边界、配置、root-matching、owner 哈希回读通过。

0187 kernel SHA256 a506ca6637cfc4f92d5670f63d682364ef73f2055e91eb4110ae2ebe46a6540a；boot SHA256 4890903bb8e0715c6f982954253a68ebd58977c0aa082b7131616e74c78b7a2b。

用户选择专用 HiOS 并参考 256GB iOS 设备容量。主机以合法 diskutil 扩容原版 System 容器到整个 userdata 241008750592，添加无 quota 的 Data/Preboot。共享空闲235612561408。三卷/容器 fsck 及关键原版文件只读哈希通过；flags 与 UUID 已记录。Data/Preboot 空白、Cryptex 待装配、iOS16 驱动兼容性待手机验证。

Android sparse 已生成并独立校验：5385490472 字节，337 个 RAW/零 FILL chunk，无 DONT_CARE，展开为完整 userdata；SHA256 d0537ffeaab0d25c99358a716d167d9880d3d8752c052e4b0a78e5a9adedd7bc。工件、host-layout、install-0187.json、INSTALL-0187.md 位于 apfs-inputs-20H392-ready。导出真实空洞/损坏/截断测试通过。

完整证据、补丁、主机状态及下一步在 HANDOFF.md 第十二节，容量依据在 STORAGE-PLAN.md，官方指南与 Sheng 对照、后续开发顺序在 ROADMAP.md。当前设备 c073a488 / b / boot-0186-ro / fastboot；**尚未写 userdata，实际重建等待最终明确批准。**

## 2026-10-29：0187–0190 已部署并完成APFS只读验收（覆盖上一节状态）

用户已最终确认覆盖userdata。22/22 sparse分块Sending/Writing均OKAY，整个241008750592字节分区已部署为System/Data/Preboot共享APFS；外层GPT不变。bootloader USB在大包后曾掉线，用户重启bootloader后恢复，没有重复刷userdata。0187根卷按原版UUID选择disk1s1并挂载成功，xid260、58840027个4KiB块，越过VFS_ROOT/devfs。

0187在launchd压缩读取时报未注册decmpfs type4/errno45。0188增加原版AppleFSCompressionTypeZlib作为prelink root，真机加载成功并越过该错误。之后AMFI拒绝launchd/dyld；0189补入原版20H392 StaticTrustCache（version2/2760条），BuildManifest摘要验证通过、模块字节不变，无自签条目。现有DarwinBoot标准sidecar/handoff装入AMFI runtime，早期FWLOG明确显示外部模块0加载成功。

0189/0190真实PID1已执行原版dyld/libignition，输出Darwin Ignition Sequence。当前最后输出是ignition参数，并未完成mount-phase-1或SpringBoard。共享缓存ENOENT、Data/Preboot空白、Cryptex未装配是已知缺项；下一步定位真实等待点后装配，不能把缺共享缓存直接当作唯一阻塞原因。

0190仅比0189增加kebab_install=1以使用既有只读INST CHECK。launchd541600字节、dyld922912字节，SHA256分别12370e5ee35bf28ae479b6c4b4455f4f4693ab803e37c5eb3032537c18cac61c和5806834564fe6462ae981d0915d612b1eb2f6f168822b5d278bee56254933757，与主机原版完整文件一致。INST? rootro/active0/entries0，未做安装写入。launchd.plist检查被既有路径白名单拒绝，不计作通过。

原版static bundle SHA256367e051548f9f0407fa9fe9d40007fa4e7c5ff27c45f5a7a30cedf344e24d98e；0190 boot SHA256ddf5d24b6e4e3857839163d0acc5ea907a58d38d47163196f3630a07205e3bba。prelink188/189/190内容一致，原生内核仍0187；188之后未改XNU源码或供体机器码。

可复核证据：0187-flash-userdata.log、0187/0188-ro-boot.log、0189-ro-fwlog.log、0189/0190-ro-boot.log、0190-check-launchd.log、0190-check-dyld.log、0190-inst-status.log。verify-apfs-device-evidence.py检查实际刷写、根挂载、两文件哈希、真实早期PID1及只读状态全部通过，生成apfs-inputs-20H392-ready/device-evidence-0190.json。

0187/0188 panic FASTBOOT、0189/0190正常用户态FASTBOOT均自动通过。最终c073a488 / b / boot-0190-ro / fastboot，无后台任务。根封印error80/81仍走已有开发继续路径，原有kebab_cs=15策略保留；没有声称SSV认证、完整用户态或Data持久化。详细更新在HANDOFF第十三节、ROADMAP和STORAGE-PLAN。
