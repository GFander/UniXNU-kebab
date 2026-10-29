from pathlib import Path
r=Path(__file__).parent.parent
p=r/'ROADMAP.md';s=p.read_text()
line=s.splitlines()[2]
assert line.startswith('更新：2026-10-29')
s=s.replace(line,'更新：2026-10-29（香港时间）。当前已验证0205，0206为诊断候选；七小时持续开发截止09:22:25。驱动实施、当前镜像及后台任务以 standardization-run-20261029/RUN.md 为准。目标是 OnePlus 8T / SM8250、16 KiB 页、iPhone X iOS 16.7.16（20H392）。',1)
start=s.index('| 优先级 | 工作包 | 具体交付与完成条件 |')
end=s.index('### 最近三项任务',start)
s=s[:start]+'''执行顺序采用用户的 `STANDARDIZATION-PLAN.md`，GPU/SMMU在直扫和桌面之前。CPU/GOP路径保留为GPU读回验证出口。

| 阶段 | 工作包 | 具体交付与完成条件 |
| --- | --- | --- |
| A1 已通过 | SPMI/PMIC/RSC分层 | 总线与客户端拆分；真实provider/nub/personality；普通事务序列化，持锁panic FASTBOOT保持 |
| A2 已通过约定范围 | GENI/TCM分层及正常身份 | 保留FIFO/bitbang/TCM；整命令/整帧事务锁；GENI/Touch实际匹配/类型化状态；实际触摸采样和HID另验 |
| A3/A4 已形成工件 | 全驱动回审、新驱动规范 | 10篇指南逐项差距、manifest/DRV?、逐Apple类ABI记录；后续每项能力继续按硬件和用户态证据验收 |
| B1 | APFS Data读写 | 保持System只读；独立Data卷的写入/同步/重启后读取；不重复覆盖整个userdata |
| B2 当前 | ignition等待点 | 0204/0205已定位PID1的sysctl/AppleImage4 gate；0206追踪具体OID，修复真实交接后观察下一步骤 |
| B3 | Data/Preboot装配 | 卷身份、角色、目录、System/Data配对与实际挂载正确 |
| B4 | 动态信任缓存 | 按目标格式/签名输入加载，记录来源、条目与实际验证行为 |
| B5 | Cryptex/SystemOS/AppOS | 原版映像、manifest、trust cache、挂载路径和身份对应 |
| B6 | 共享缓存 | 正确文件/子缓存、UUID、映射/slide与实际dyld使用 |
| B7 | 完整启动任务 | launchd持续推进，mount-phase-1及所需任务通过 |
| B8 | GPU/Metal | 第0步SMMU，分别落实A650/GMU固件、a6xx寄存器、GPU/DPU stream/context；提交/完成/读回/CA合成 |
| B9 | backboardd与显示输入 | DPU直扫GPU缓冲、扫描中所有权、真实vsync与120Hz、HID完整事件链 |
| B10 | SpringBoard | FrontBoard/RunningBoard/SpringBoardServices到真实锁屏/桌面，并验证交互 |

B1/B2可独立推进，B3依赖Data写能力；B4→B5→B6→B7之后推进B8→B9→B10。SMMU是GPU/直扫共同依赖。真实事件/缓冲生命周期是验收内容，服务名称与成功返回不能替代它。

'''+s[end:]
start=s.index('1. 固化 0187–0190',s.index('### 最近三项任务'))
end=s.index('用户已选择手机专用于 HiOS',start)
s=s[:start]+'''1. 根据0206的PID1 sysctl进入/返回追踪，确认AppleImage4 gate等待的OID和待交接数据；保留0205两次一致阻塞栈。
2. 在既有APFS共享容器中单独验证Data读写/同步/持久化，明确System只读；原版输入和恢复工件保留。
3. 按B3–B7装配卷、Cryptex与共享缓存，逐步跑完真实启动任务，再进入SMMU/GPU和直扫。

'''+s[end:]
start=s.index('Windows：`D:\\LOS\\HiOS\\p1-run\\prelink-fix`。')
end=s.index('AMFI 的开发启动集哈希',start)
s=s[:start]+'''Windows：`D:\\LOS\\HiOS\\p1-run\\prelink-fix`。0205为已验证常规只读镜像，0206为PID1专用sysctl追踪候选。`0205-pid1-stack-a/b.log`及对应symbols.json保存实际等待链；各版regression.json保存根卷/诊断/哈希/panic恢复，0201/0202另有transport-regression.json核对CLK/SE/PING及正常FASTBOOT。部署记录仍为`apfs-inputs-20H392-ready/deployment-0187.json`，本窗口没有重复覆盖userdata。

源码：Ubuntu `/home/albert/kebab-dev-ios16`；实验内核 `/home/albert/s4/x`；Mac实际 `/Users/albertlee/kebab-build/dxb/xnu`。0173/0174为首批标准化；0175–0184新增总线分层、服务/诊断、devfs和阻塞观测。正式补丁按文件名顺序重放；本轮manifest-0204.json已核对列出的路径在实际两树与独立重放树一致。正式ADT生成器已加入SPMI/GENI资源节点及保留原启动安全输入的回归检查。

'''+s[end:]
start=s.index('独立设备/平台功能域共9类',s.index('## 驱动规范审计'))
s=s[:start]+'''独立设备/平台功能域仍为9类，另计Surface及PMGR开发入口。主要实现现为21个C/C++文件、18个类；原0190审计的13文件/10类保留为历史。新增文件来自职责拆分，不代表新增8个硬件驱动。0204 DRV?确认11个registry角色实际匹配。

0173/0174的显示/Surface/存储合约保持；A1/A2总线、正常服务和原协议已逐批回归，0203修复按键节点早于devfs发布的问题。实际触摸/HID、正常USB服务生命周期、真实电源、私有图形用户ABI、SMMU/GPU/直扫仍有明确待验收项。

10篇指南回审见 [A3-GUIDE-REVIEW.md](standardization-run-20261029/A3-GUIDE-REVIEW.md)，执行记录见 [STATUS.md](driver-standardization/STATUS.md)，实时续跑见 [RUN.md](standardization-run-20261029/RUN.md)。`standardization-run-20261029/abi/`逐类记录了已核实的存储/PMGR事实与尚未恢复的图形布局，未把原生factory自测等同于原版框架或backboardd资格。
'''
p.write_text(s,newline='\n')
