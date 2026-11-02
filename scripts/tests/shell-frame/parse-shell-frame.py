"""Report exact observer results; do not equate completed swap with visible pixels."""
from pathlib import Path
import hashlib,json,re,sys
r=Path(__file__).resolve().parent;name=sys.argv[1] if len(sys.argv)>1 else '0485-media-boot.log';log=r/name;raw=log.read_bytes();s=raw.decode();shell=[];frames=[];firstlast=[]
for m in re.finditer(r'\[\s*([\d.]+)\] KEBAB SHELL: original20H392 result=(\d+) state=(\d+) checkin-completed=(\d+);',s):
 t,result,state,completed=m.groups();shell.append(dict(seconds=float(t),result=int(result),state=int(state),completed=int(completed)))
for m in re.finditer(r'\[\s*([\d.]+)\] KEBAB FRAME: queued=(\d+) presented=(\d+) begun=(\d+) completed=(\d+) pending=(\d+) power-off=(\d+) have-frame=(\d+);',s):
 values=m.groups();frames.append(dict(zip(('seconds','queued','presented','begun','completed','pending','power_off','have_frame'),[float(values[0])]+list(map(int,values[1:])))))
for m in re.finditer(r'\[\s*([\d.]+)\] KEBAB FRAME: (first|last) id=(\d+) mask=0x([0-9a-f]+) selected=(\d+) found=(\d+) missing=(\d+) self=(\d+) rejected=(\d+) copied-bytes=(\d+)',s):
 t,kind,identity,mask,selected,found,missing,self_surface,rejected,copied=m.groups();firstlast.append(dict(seconds=float(t),kind=kind,id=int(identity),mask=int(mask,16),selected=int(selected),found=int(found),missing=int(missing),self_surface=int(self_surface),rejected=int(rejected),copied_bytes=int(copied)))
q=dict(input=name,actual_log_sha256=hashlib.sha256(raw).hexdigest(),shell_snapshots=shell,frame_snapshots=frames,first_last_frames=firstlast,original_success_completion_observed=any(q['result']==0 and q['completed']==1 for q in shell),cpu_pixel_copy_observed=any(q['copied_bytes']>0 for q in firstlast),panel_scanout_or_visible_springboard_proven=False,best_effort_no_suspension_or_atomicity_claim=True)
out=r/(Path(name).stem+'-shell-frame-observations.json');out.write_text(json.dumps(q,indent=2)+'\n',newline='\n');print(json.dumps(q,indent=2))
