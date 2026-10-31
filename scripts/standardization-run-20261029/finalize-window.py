"""Close the authorized window only after its deadline and confirmed heartbeat pause."""
from pathlib import Path
import datetime,subprocess,sys,tomllib
r=Path(__file__).parent
deadline=datetime.datetime(2026,10,29,1,22,25,tzinfo=datetime.timezone.utc)
assert datetime.datetime.now(datetime.timezone.utc)>=deadline,'The authorized seven-hour window has not ended'
cfg=tomllib.loads(Path('C:/Users/Albert/.codex/automations/hios/automation.toml').read_text())
assert cfg['status']=='PAUSED','Confirm heartbeat pause through the app tool first'
subprocess.run([sys.executable,str(r/'update-result-docs.py'),'0225','--complete'],check=True)
p=r/'RUN.md';s=p.read_text();marker='## 当前批次\n\n';assert s.count(marker)==1
note='''09:22 收尾：七小时窗口已结束，hios续跑已由app工具暂停且TOML确认PAUSED。稳定版0225，native=e7cb391447d5a6aaf2feb45a3e5d48191996ca775f179a40adecc38f09b1655b，boot=4a9601ee935d4530bac165e16322faba3705a27546995b377f15d18b142080cf。18分钟4点只读观察PASS；随后225首次完整65536字节FWLOG PASS、PON正常、normal sync FASTBOOT PASS，最终c073a488 fastboot/active b。无本轮在途构建、刷机或COM捕获。

最终17 PASS/0 FAIL/4可选IPSW SKIP；176共同+4 host补丁、5192全源目录输入重放一致；51源码条目/7ABI/原版storage与AES审计和独立设备证据交叉一致。所有结果见STANDARDIZATION-RESULT-20261029.md、BUILD-BASELINE-20261029.md、IMAGE4-BLOCKER-20261029.md及evidence-0225.json。源代码/记录已镜像Ubuntu仓库，未commit/push；此前未提交内容保留。

计划真实边界：A1/A2结构与回归已过，USB普通服务/线程、HID/真实电源等A3缺口仍在；B1 Preboot通用持久化通过，B2专用nonce selector2212后端未实现，Data内容保护/Cryptex/DSC/完整launchd和图形/UI未通过。不要把计数或Preboot proof当完整iOS启动。不要再运行旧过渡生成器、216写模式或用checkpoint260的raw覆盖设备261。

'''
p.write_text(s.replace(marker,marker+note,1),newline='\n')
print('Window closed after seven hours; heartbeat paused; stable 0225 and final hardware state recorded')
