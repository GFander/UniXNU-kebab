"""Check production body/log/register preservation across the transport split."""
from pathlib import Path
import re
import runpy
from collections import Counter

r=Path(__file__).parent
module=runpy.run_path(str(r/'extract-geni.py'))
old=module['original'];touch=(r/'after/bsd/dev/kebab_touch.c').read_text()
bus=(r/'after/bsd/dev/kebab_geni.c').read_text()
def function(s,name):
    at=s.index('\n'+name+'(');start=s.rfind('\n',0,at)+1
    end=s.index('\n}\n',at)+3
    return s[start:end]
def norm(s):
    s=re.sub(r'\bkg\.', 'kt.',s)
    for old,new in module['public'].items():s=re.sub(r'\b'+new+r'\b',old,s)
    return re.sub(r'\s+','',s).removeprefix('static')
for name in module['names']:
    renamed=module['public'].get(name,name)
    assert norm(function(old,name))==norm(function(bus,renamed)),name
tcm=['kt_transform','ktq_push','ktq_pop','kt_queue_selftest','kt_emit','kt_print_event',
     'kebab_touch_open','kebab_touch_close','kebab_touch_read','kt_dev_register',
     'kt_tcm_read_header','kt_tcm_command','kt_le16','kt_mode_name','kt_status_name',
     'kt_boot_status_name','kt_identify','kt_boot_info','kt_app_info','kt_set_report_config',
     'kt_touch_report','kt_poll_once']
for name in tcm:assert norm(function(old,name))==norm(function(touch,name)),name
quoted=r'"(?:\\.|[^"\\])*"'
logs=lambda source:Counter(re.findall(quoted,source))
# Compare only log strings, not headers or newly added comments.
old_logs=Counter(x for x in re.findall(quoted,old) if x.startswith('"KEBAB TOUCH:'))
new_logs=Counter(x for x in re.findall(quoted,touch+bus) if x.startswith('"KEBAB TOUCH:'))
assert old_logs==new_logs,(old_logs-new_logs,new_logs-old_logs)
assert 'ml_io_map(' not in touch and 'kt_rd(' not in touch and 'kt_se_wr(' not in touch
assert 'kebab_tcm_' not in bus
for name in ['M_GENI_CMD_CANCEL_REQ','M_GENI_CMD_ABORT_REQ','KT_BB_STRETCH','KT_SER_CLK_CFG',
             'KT_SCL_COUNTERS','KT_TLMM_SIZE','KT_PIN_SDA','KT_PIN_SCL','KT_PIN_RESET','KT_PIN_IRQ']:
    pattern=r'^#define '+name+r'\b[^\n]*(?:\\\n[^\n]*)?'
    assert re.search(pattern,old,re.M)[0]==re.search(pattern,bus,re.M)[0],name
files=(r/'after/bsd/conf/files').read_text()
assert sum(x.startswith('bsd/dev/kebab_geni.c') for x in files.splitlines())==1
print('33 hardware/transport bodies, 22 client bodies, all phase logs/register/timing constants, single build input PASS')
