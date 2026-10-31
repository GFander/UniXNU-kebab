# 2026-10-29 实际构建基线

目标：OnePlus 8T / SM8250、XNU8796.141.3、DEVELOPMENT ARM64 KEBAB、16 KiB，供体iPhone10,3 iOS16.7.16 / 20H392。本文记录经过设备验证的构建方式；不把旧board.toml描述当作当前可运行的一键配方。

## 源码与主机准备

- pristine XNU基点`1b191cb`，共同补丁在`patches/xnu-8796-kebab`按文件名应用。截至0197共176个；包括0173-z遗漏输入补存、0194板级范围保护、0195固定SCM调查、0196复位策略分层和0197 FWLOG快照/发送排空。
- 0167只修正旧上下文，增删的运行代码不变；0170从原GNU patch成功结果重新生成，严格应用输出逐字节相同。
- 0173-z补存此前已编译但缺少记录的storage头、桥、固定绑定与编译入口；0193补存此前已运行的RAM日志、UFS发布及prelink生命周期代码。二者记录现有工作树，不冒充新功能。
- `patches/hosts/macos-dxb-13.5/0001–0004`单独用于这个主机配方。0001包含FPD字段/stub、kas_info、fakeroot MIG/库路径、架构计时器入口；0002补齐availability脚本及3个FPD导出；0003修复LLDB检查器stdlib遮蔽并保留真实检查；0004记录builder的Libsyscall.xcconfig准备。不要混用于其他KERNEL_SDKROOT配方。
- 早期4205/4208只覆盖列明扩展名，漏掉了脚本、exports和xcconfig。0222已扩到5192个源目录文件（除Git、Python缓存、DS_Store和生成的compile_commands.json），严格共同补丁加4个host补丁重放与实际Mac一致。两个空Data遗留已清理；完整旧工作树仍保留。该范围不包含完整SDK/Xcode/KDK身份。

重放证据：`standardization-run-20261029/series-repair/all-source-replay-0222.json`及后续版本记录；实际3585个fakeroot头/defs/库/对象、builder/主机补丁的早期哈希见同目录`build-inputs-0218.json`，后续以最新build-inputs文件为准。该清单同时记录实际clang、Xcode、SDK路径与版本；没有把整个KDK/Xcode镜像列入哈希资格。

## 已测试的全量构建

主机`albertlee@192.168.30.165`，工程`/Users/albertlee/kebab-build`，实际源码`dxb/xnu`。KDK为`KDKs/KDK_13.5_22G74.kdk`。环境：`MACOS_VERSION=13.5 MACHINE_CONFIG=KEBAB KERNEL_CONFIG=DEVELOPMENT`。

从已准备并核对的源码运行`python3 start-full-build.py VERSION --prepared-source`：先验证绝对路径，删除仅有的`dxb/build/xnu.obj`及`xnu.sym`，再以`caffeinate -s bash build.sh`执行完整构建。该选项设置现有builder的CODEQL=1；在本版本脚本中它唯一作用是跳过Git patch循环，不启动CodeQL任务。不要运行会重新抓取/重置源码的`build-local.sh`。单文件检查只用于提前发现ABI/头文件错误，不能替代全量构建。

0222从Ubuntu严格重放后导出的新源码目录完成clean构建；旧树完整保留在`/Users/albertlee/kebab-build/xnu-before-clean-replay-0222`。第一次尝试因availability脚本失败，第二次因LLDB模块遮蔽失败；没有把存在部分内核输出当成功。补齐记录并修复检查器后完整构建及设备回归通过。LLDB检查仍能拒绝真实语法/tab/非ASCII错误，不使用旧主机补丁的停用检查办法。

最终内核必须存在：`dxb/build/xnu.obj/DEVELOPMENT_ARM64_KEBAB/kernel.development.kebab`。build脚本中的部分分支会吞掉make退出码，不能仅凭“Build Done”判断成功；还须校验最终文件、新hash、链接类/固定绑定，以及打包和真机回归。

## prelink与刷测

原供体`kernelcache.fileset.macho` SHA256：`2ec297bac95bcba01046fd0148e97e7e5885c05a6c18827f39f534ba375c1221`。slide固定`-0x1effcdc8000`。AMFI早期入口为供体`0xfffffff005703c54`。原始代码保持，绑定、vtable和类布局按20H392重审，不能套21H461地址。

Windows的`driver-standardization/package-build.py VERSION PREVIOUS --selftest`完成CTF移除、依赖闭包prelink、loader范围、storage 10绑定/22槽、FAT文件、ADT owner测量及只读门控检查。常规镜像保持`ufsrw=0 rootrw=0`；受控Preboot写镜像另名且单独验证，不能交给只读验收器冒充通过。

仅刷`boot_b`；确认Sending/Writing均OKAY后，先开始COM7捕获再重启。启动后验证System dev1/2、Preboot dev1/4与同backing、两卷只读、固定proof全SHA、真实registry、原始按键值、SPMI/PON/TCS、launchd/dyld完整SHA，最后`PANIC!`→串口`FASTBOOT`→fastboot设备重新出现。

## 尚未接入统一构建入口

当前board.toml仍含旧route A、旧页粒度说明及不存在的旧构建脚本名。它不描述本次prelink/20H392流程，暂不能作为可复现主入口。后续需把上述实际输入和主机准备移入显式版本化的构建配方，再用全新目录构建并上机；不能只更新元数据后声称干净重建已完成。

## 存储状态

不整体重刷userdata。真实APFS容器241008750592字节，System/Data/Preboot共享；System UUID DADD93F8-DBBF-4B17-A1DF-0F11E64172F7。受控Preboot写后checkpoint为261。Mac的旧raw基线仍为260，不能用于覆盖当前真机来“同步”。

Preboot唯一测试文件`/private/preboot/.hios-apfs-proof-v1`为65559字节，SHA256 `3f2f47b49455f2d5cce7d745829440ccf0e8f1beda6c5120641c25213f78c957`。不要重复运行写模式；O_EXCL应拒绝覆盖。Data内容保护、Image4派生key、Cryptex/共享缓存和完整launchd仍未通过。
