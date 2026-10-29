"""Refresh the concrete checkpoint without erasing earlier session evidence."""
from pathlib import Path
import argparse,datetime,hashlib,json
ap=argparse.ArgumentParser();ap.add_argument('version');ap.add_argument('--complete',action='store_true');a=ap.parse_args()
r=Path(__file__).parent;h=r.parent;images=h/'p1-run/prelink-fix';v=a.version
m=json.loads((r/('manifest-'+v+'.json')).read_text());reg=json.loads((images/(v+'-regression.json')).read_text())
assert reg['file_hashes_match'] and (reg['panic_fastboot_verified'] or reg.get('normal_sync_fastboot_verified'))
fwlog=int(v)>=223;count=176 if fwlog else 175
native=hashlib.sha256((images/('kernel-'+v+'.kebab')).read_bytes()).hexdigest();boot=hashlib.sha256((images/('boot-'+v+'-ro.img')).read_bytes()).hexdigest()
now=datetime.datetime.now().astimezone().strftime('%Y-%m-%d %H:%M:%S %Z')
state='窗口已结束；在途构建/捕获均收尾，续跑自动化已暂停。' if a.complete else '窗口进行中；截止2026-10-29 09:22:25香港时间，在途任务见RUN.md。'
text=f'''# 标准化开发检查点（2026-10-29）

更新：{now}。最新已验证{v}。{state}

这轮已修复旧SPMI并发争用，完成SPMI/PMIC和GENI/TCM的主要分层，补齐devfs发布时间、真实服务诊断和复位策略边界；同时证实APFS Preboot通用持久写。完整iOS仍停在Image4 nonce服务，未到桌面。

## 驱动现在重写了哪些

仍是9个硬件/平台功能域，另计Surface兼容层和AES辅助组件。当前manifest列明{m['implementation_files']}个主要实现文件、{m['class_count']}个类、11个真实registry角色；拆文件和加类不等于新增硬件能力。

| 功能域 | 本轮完成并验证 | 尚未完成 |
| --- | --- | --- |
| SPMI/PMIC | 共享普通事务mutex、bus/客户端分离、scoped nubs及3个类型化服务；持普通锁panic仍可无锁复位 | 通用动态枚举、HID及完整电源消费者 |
| GENI/触摸 | GENI/bitbang传输与TCM协议分离，完整事务锁，真实Bus/Touch服务 | 控制器identify/采样仍未通；完整触摸事件和HID |
| UFS/块存储 | 既有descriptor/完成/RO边界保持，原版10绑定/22槽重审；Preboot真实持久写/重启全SHA | 正常控制器provider/IRQ/SMMU，以及Data内容保护 |
| USB/恢复 | IMEM/PON/PSCI策略和私有状态移出DWC3；正常同步复位及锁内panic FASTBOOT通过 | 普通传输/协议、轮询线程、断连重连的服务生命周期 |
| 按键 | 采样/去抖保持，devfs就绪后发布，24字节记录/真实VCHR资格通过 | 用户消费、HID按下/释放整链 |
| PlatformExpert | 资源/personality及provider范围核对，固定20H392原版ABI来源 | PMGR开发stub仍非真实板级电源管理 |
| CPU | 单核拓扑/SGI/定时器基线保持 | SMP、跨核TLB/cache和IRQ路由 |
| RPMh/VRM | RSC与SPMI回读分层，白名单/状态机和类型化状态保持 | 消费者投票、真实调压/时钟、睡眠唤醒/IOPM |
| 显示 | 既有factory/引用/关闭合约与自测持续回归 | 原版用户ABI/selector74、DPU直扫、真实vsync/120Hz |
| Surface另计 | 连接/参数/共享对象清理的原生资格保持 | 真实iOS框架、跨进程映射及backboardd |

四个带固定供体地址的实现已加KEBAB编译范围保护；板内正文相同，其他板预处理不产生这些代码/绑定。
'''
if fwlog:text+='\nFWLOG满环自覆盖故障已修复：打印前复制，normal/panic各有固定缓冲；不分配、不等待驱动锁，保持调用者IRQ状态。主机24种精确内容/边界/IRQ、2种重入及panic打断normal案例通过；设备普通/panic命令与恢复另留证据。两份64KiB固定数据缓冲另有状态字段，当前只资格化cpus=1。\n'
text+=f'''
## iOS主线的实际进展

- B1/B3：System APFS只读根持续工作。Preboot真实独占创建65559字节文件、APFS同步/UFS cache flush通过；重启只读完整SHA一致，checkpoint261。System launchd/dyld完整SHA不变。Data被原版APFS拒绝未加密卷，仍未挂载/配对。
- B2：PID1具体等待为`security.mac.img4.ignition_blob`。官方SoftwareAES移植经20H392重审，CBC128/192/256、descriptor/raw KAT真机通过；错误预期答案拒绝注册。原版Image4需要专用nonce selector2212，后端未实现；AES默认关闭。详见IMAGE4-BLOCKER-20261029.md。
- 固定SCM查询实测SMCCC1.1、SCM64可用、ICE invalidate/set availability1、QSEE version availability0。无密钥操作，不能据此认定2212后端可用。
- B4/B5/B6/B7：原版静态信任缓存已加载；动态信任、真实Preboot/Cryptex、DSC运行时映射和完整launchd任务未通过。离线映射计划通过不是用户态已通过。
- B8/B9/B10：先做SMMU再A650/Metal，随后DPU直扫/真实vsync/120Hz/HID，最后backboardd和SpringBoard。CPU/GOP画面仍是调试出口。

## 构建与验证证据

共同补丁{count}个，主机补丁4个。0217时代的4205、后来的4208仅是列明扩展名范围；干净目录构建发现遗漏的脚本/exports/xcconfig后，已扩至5192个源目录文件核对。0222真正从重放的源码完成clean全量构建和设备回归，保留完整旧工作树备份。主机Python检查器保留真实语法/tab/ASCII检查，修复collections模块遮蔽，空文件和负向用例通过。后续使用`start-full-build.py VERSION --prepared-source`，跳过builder的Git patch循环；Git只在Ubuntu操作。

原版供体代码、storage 10绑定/22槽、AES181槽/162继承与逐类ABI均核对。当前根卷、Preboot全SHA、两文件全SHA、11 registry角色、原诊断/keys、panic FASTBOOT通过；0220正常sync FASTBOOT和0221持锁panic也分别实测。完整SDK/KDK/Xcode不是由5192这个计数覆盖，外部输入的范围和哈希另见BUILD-BASELINE-20261029.md。

最新native SHA256：`{native}`。
最新boot SHA256：`{boot}`。

不整体重刷userdata，不重复执行216写模式。Mac旧raw仍为checkpoint260；设备已为261，不能用旧raw覆盖。实际proof路径固定且O_EXCL，SHA256为`3f2f47b49455f2d5cce7d745829440ccf0e8f1beda6c5120641c25213f78c957`。

补丁、说明和测试已镜像到Ubuntu仓库工作区；本轮尚未commit/push，GitHub页面尚不反映这些修改。既有未提交内容保留。
'''
soak_path=images/(v+'-soak.json')
if soak_path.exists():
    soak=json.loads(soak_path.read_text())
    if soak.get('complete'):
        assert len(soak['samples'])==4 and soak['samples'][-1]['elapsed_seconds']>=1080
        text+='\n最终常规配置通过18分钟、4个只读检查点：PID1等待地址/CPU时间不变，PON及两份系统文件全SHA一致。观察后再次完整转储65536字节FWLOG，正常同步返回fastboot；活动槽b，无本轮在途构建/刷机/串口捕获。统一检查17 PASS、0 FAIL、4个可选IPSW SKIP。\n'
(h/'STANDARDIZATION-RESULT-20261029.md').write_text(text,newline='\n')
marker='## 十八、标准化最新检查点（自动更新）'
handoff=(h/'HANDOFF.md').read_text().split(marker)[0].rstrip()
handoff+='\n\n'+marker+f'''\n\n{now}，已验证{v}，{state}
详见`STANDARDIZATION-RESULT-20261029.md`、`BUILD-BASELINE-20261029.md`及`IMAGE4-BLOCKER-20261029.md`。
9硬件域/{m['implementation_files']}主要实现/{m['class_count']}类/11实际registry角色；不能当成已完成9个整机驱动。
SPMI/GENI分层、keys发布和USB复位策略抽取通过；Preboot持久写/冷启动SHA通过，Data未过内容保护，Image4仍需2212。{count}共同补丁+4主机补丁；5192全源目录输入重放核对与干净构建已补。
当前验证回归结束设备为fastboot；后续若继续开机/测试，实时状态以RUN.md为准。无整体userdata重写。native={native}，boot={boot}。
'''
(h/'HANDOFF.md').write_text(handoff,newline='\n')
marker='## 最新窗口检查点（自动更新）';p=h/'driver-standardization/STATUS.md';s=p.read_text().split(marker)[0].rstrip()
p.write_text(s+'\n\n'+marker+'\n\n'+f'已验证{v}；详见`../STANDARDIZATION-RESULT-20261029.md`。本节覆盖前文历史的当前状态。SPMI/GENI分层、keys devfs、USB复位分离通过；Preboot持久写已过，Data内容保护和Image4专用key仍缺。{m["implementation_files"]}主要实现/{m["class_count"]}类/11 registry角色；9硬件域计数不变。\n',newline='\n')
p=h/'ROADMAP.md';s=p.read_text();import re
s=re.sub(r'当前已验证\d{4}；',f'当前已验证{v}；',s,count=1)
s=s.replace('后续仅更新 boot_b','不整体重刷userdata；常规刷测更新 boot_b')
if 'STANDARDIZATION-RESULT-20261029.md' not in s:s+='\n本轮最新驱动状态和实测范围见`STANDARDIZATION-RESULT-20261029.md`，Image4具体阻塞见`IMAGE4-BLOCKER-20261029.md`。\n'
p.write_text(s,newline='\n')
guide=r/'A3-GUIDE-REVIEW.md';s=guide.read_text()
s=re.sub(r'运行证据截至\d{4}[^。]*。',f'运行证据截至{v}。',s,count=1)
s=s.replace('DWC3、命令分派、安装和复位仍同文件','DWC3、命令分派和安装仍同文件，复位策略已独立')
marker='## 最新核对（自动更新）';s=s.split(marker)[0].rstrip()
s+=f'''\n\n{marker}

已验证{v}，9硬件域、{m['implementation_files']}主要实现文件、{m['class_count']}类、11实际registry角色。此前172补丁/4205扩展名清单属于历史阶段；最新为{count}共同补丁+4 host补丁、5192源目录文件完整重放和clean构建。统一verify-all现17 PASS/0 FAIL/4可选IPSW SKIP。

USB复位策略/私有状态已分离。FWLOG满环自覆盖及普通TX环溢出已由0197修复，24+2+1主机案例、真机完整65536字节、后续PON、持普通SPMI锁的panic FWLOG/FASTBOOT通过。普通线程/协议/断连生命周期仍待服务化，不能只凭这些改动声称USB标准化全部完成。

Preboot持久化、专用nonce选择器2212、Data内容保护和后续UI的真实边界以`../STANDARDIZATION-RESULT-20261029.md`和`../IMAGE4-BLOCKER-20261029.md`为准。当前没有完整launchd、Data配对、Cryptex/运行时DSC、GPU或桌面验收。
'''
guide.write_text(s,newline='\n')
p=h/'driver-standardization/STATUS.md';s=p.read_text()
note='最新状态见 `../STANDARDIZATION-RESULT-20261029.md`；下文早期批次保留为历史，文末有最新检查点。'
if note not in s:s=s.replace('# 驱动标准化执行记录\n','# 驱动标准化执行记录\n\n'+note+'\n',1)
s=s.replace('全窗口尚未再次写userdata，所有根卷回归保持只读与launchd/dyld完整SHA相同。','截至0204尚未再次写userdata；后续受控Preboot写入见最新检查点。System根卷始终只读，launchd/dyld完整SHA相同。')
p.write_text(s,newline='\n')
print('Updated result, HANDOFF, STATUS and ROADMAP at verified version',v)
