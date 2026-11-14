"""Offline Touch progress-history policy study. No native/product execution."""
from pathlib import Path
import re,json,math,hashlib,statistics,collections
ROOT=Path(__file__).resolve().parent
LOG=Path.cwd()/'Build/Logs/pan-device-feedback-2026-11-14/console.snapshot.log'
EPS=2**-23;FREQ=10_000_000;TICK=1/FREQ

def error(a,b):return EPS*(math.hypot(a[1],a[2])+math.hypot(b[1],b[2]))
def dist(a,b):return math.hypot(b[1]-a[1],b[2]-a[2])
def trim(h,now):
 while len(h)>1 and now-h[1][0]>.050:h.pop(0)
 if len(h)==64:h.pop(0)
def independent(h):
 out=[]
 for p in reversed(h):
  if not out or out[-1][0]-p[0]+1e-9>=.002:out.append(p)
 return out[::-1]
def history(samples,progress=False,rev=.25):
 raw=[];motion=[];direction=(0.,0.);anchor=None;resets=0;duplicates=0;retirements=0
 for p in samples:
  cleared=False;reversed_=False
  if raw:
   prev=raw[-1]
   if p[0]<=prev[0]:continue
   dx=p[1]-prev[1];dy=p[2]-prev[2];d=math.hypot(dx,dy)
   if p[0]-prev[0]>=.040:
    raw=[];motion=[];direction=(0.,0.);anchor=None;resets+=1;cleared=True
   elif d:
    along=dx*direction[0]+dy*direction[1]
    if direction!=(0.,0.) and along<0:
     if anchor is None:anchor=prev
     rx=p[1]-anchor[1];ry=p[2]-anchor[2]
     if rx*direction[0]+ry*direction[1]<=-rev:
      raw=[anchor];motion=[anchor];dd=math.hypot(rx,ry);direction=(rx/dd,ry/dd);anchor=None;resets+=1;reversed_=True
    else:
     anchor=None
     if d>=rev or direction==(0.,0.):direction=(dx/d,dy/d)
  else:prev=None
  trim(raw,p[0]);raw.append(p)
  if not motion:motion=[p]
  elif dist(motion[-1],p)>error(motion[-1],p):
   if not reversed_ and p[0]-motion[-1][0]>=.070:
    # Resume from the previous actual raw snapshot; never rewrite its time.
    motion=[prev] if prev is not None else []
    retirements+=1
   trim(motion,p[0]);motion.append(p)
  else:duplicates+=1
 return (motion if progress else raw),direction,{'rawCount':len(raw),'motionCount':len(motion),'duplicateRecords':duplicates,'gapOrReverseResets':resets,'staleMotionRetirements':retirements}
def estimate(samples,up,progress=False,rev=.25,cap=2400.,minimum=10.):
 h,direction,meta=history(samples,progress,rev);e=independent(h)
 out=meta|{'anchorCount':len(e),'progress':progress,'selected':0.,'distance':0.,'durationMs':0.,'raw':0.,'reason':'insufficient'}
 if not e:return out
 age=max(0.,up-e[-1][0]);out|={'ageMs':age*1000,'spanMs':(e[-1][0]-e[0][0])*1000,'lastMotionTime':e[-1][0]}
 if len(e)<3 or e[-1][0]-e[0][0]+1e-9<.020:return out
 mt=sum(p[0] for p in e)/len(e);mx=sum(p[1] for p in e)/len(e);my=sum(p[2] for p in e)/len(e);den=sum((p[0]-mt)**2 for p in e)
 if den<=0:return out
 vx=sum((p[0]-mt)*(p[1]-mx)for p in e)/den;vy=sum((p[0]-mt)*(p[2]-my)for p in e)/den;raw=math.hypot(vx,vy);out['raw']=raw;out['rawVector']=[vx,vy]
 if raw<=0 or (direction!=(0.,0.)and vx*direction[0]+vy*direction[1]<=0):out['reason']='direction-or-zero';return out
 ux=vx/raw;uy=vy/raw
 tail=None
 for p in reversed(h[:-1]):
  dt=h[-1][0]-p[0];dx=h[-1][1]-p[1];dy=h[-1][2]-p[2]
  if dt+1e-9<.002 or math.hypot(dx,dy)<rev:continue
  if dx*vx+dy*vy>0:tail=math.hypot(dx,dy)/dt
  break
 positive=sum((b[1]-a[1])*ux+(b[2]-a[2])*uy>error(a,b)for a,b in zip(e,e[1:]))
 out|={'positiveIntervals':positive,'tail':tail}
 if positive<2:out['reason']='unsupported-motion';return out
 def upper(a,b):
  dt=b[0]-a[0]-TICK
  if dt<=0:return 0.
  projection=(b[1]-a[1])*ux+(b[2]-a[2])*uy
  return (max(0.,projection)+rev+error(a,b))/dt
 A,B,C=e[-3:];support=min(upper(A,B),upper(B,C));baseline=min(raw,tail if tail is not None else raw,cap);bounded=min(baseline,support);quality=bounded+1e-9<baseline;gate=max(0.,min(1.,(.070-age)/.040));selected=bounded*gate
 out|={'support':support,'baseline':baseline,'selected':selected,'qualityLimited':quality,'ageGate':gate,'boostSuppressed':quality or (progress and age>.030),'reason':'ready'if selected>minimum else 'stale'if age>=.070 else 'slow','supportTimesMs':[1000*p[0]for p in (A,B,C)]}
 if selected>minimum:
  tau=.30;c=200. if cap==2400. else 1000.;t=tau*math.log1p(selected/(c*tau));d=tau*selected-c*tau*t
  out|={'distance':d,'durationMs':t*1000}
 return out

def parse():
 inputs=collections.defaultdict(list)
 for no,line in enumerate(LOG.read_text(encoding='utf-8-sig',errors='replace').splitlines(),1):
  if '[CanvasPan]'not in line or ' input ordinal='not in line:continue
  f=dict(re.findall(r'([\w-]+)=([^ ]+)',line))
  if f.get('class')!='1':continue
  x,y=map(float,f['pos-px'].strip('()').split(','));inputs[int(f['key'])].append({'line':no,'ordinal':int(f['ordinal']),'phase':int(f['phase']),'point':(int(f['sample-qpc'])/FREQ,x*.5*.19444445,y*.5*.19791666)})
 return inputs

def run():
 inputs=parse();observed=json.loads((ROOT/'device-feedback-touch-2026-11-14.json').read_text(encoding='utf-8'));real=[]
 for row in observed['touchReleases']:
  key=row['key'];events=inputs.get(key,[]);pts=[v['point']for v in events if v['phase']in(0,1)and v['line']<row['line']];upEvents=[v for v in events if v['phase']in(2,3)and v['line']<row['line']]
  if not pts:continue
  up=upEvents[-1]['point'][0]if upEvents else pts[-1][0]+row['ageMs']/1000
  origin=pts[0][0];pts=[(t-origin,x,y)for t,x,y in pts];up-=origin
  gaps=[(a['ordinal'],b['ordinal'])for a,b in zip(events,events[1:])if b['ordinal']>a['ordinal']+1]
  old=estimate(pts,up);new=estimate(pts,up,True)
  real.append({'key':key,'releaseLine':row['line'],'downLogged':any(v['phase']==0 for v in events),'upLogged':bool(upEvents),'inputRecords':len(events),'ordinalGaps':gaps,'loggedReason':row['reason'],'loggedSelected':row['selectedMmPerSec'],'loggedDistance':row['targetMm'],'oldOffline':old,'candidate':new})
 synth=[]
 def add(name,pts,up,**kw):synth.append({'name':name,'old':estimate(pts,up,**kw),'candidate':estimate(pts,up,True,**kw)})
 for hz in [30,60,120,240,480,960]:
  for speed in [20,100,1000,2500]:
   pts=[(round(i*FREQ/hz)/FREQ,speed*i/hz,0.)for i in range(round(.2*hz)+1)];add(f'steady-{hz}-{speed}',pts,pts[-1][0]+.003)
 for stop in [.0,.025,.030,.040,.070,.100]:
  pts=[(i*.008,1000*i*.008,0.)for i in range(5)]
  t,x,y=pts[-1];pts +=[(t+j*.008,x,y)for j in range(1,math.floor(stop/.008)+1)];add(f'hold-{stop*1000:.0f}ms',pts,t+stop+.001)
 for base in [0.,300.]:
  for hold in [.0,.0021,.010,.070,.100]:
   pts=[(i*.01,base*i*.01,0.)for i in range(11)];t,x,y=pts[-1];pts.append((t+.005,x+100,0.))
   if hold:pts.extend((t+.005+j*.0021,x+100,0.)for j in range(1,math.ceil(hold/.0021)+1))
   add(f'jump-base{base}-hold{hold}',pts,pts[-1][0]+.001)
 for duration in [.019,.025,.034,.041,.060,.075]:
  pts=[(i*.008,1000*i*.008,0.)for i in range(10)];t,x,y=pts[-1];pts.extend((t+j*.008,x,y)for j in range(1,math.floor(duration/.008)+1));pts.append((t+duration,x+25,0.));add(f'catchup-{duration*1000:.0f}ms',pts,pts[-1][0]+.005)
 # Counterfactual plateau-end changes and long-stop restart evidence.
 for delta in [.01,25.]:
  pts=[(i*.008,1000*i*.008,0.)for i in range(10)];t,x,y=pts[-1]
  pts.extend((t+j*.008,x,y)for j in range(1,8))
  add(f'plateau60-C{delta}',pts+[(t+.060,x+delta,y)],t+.065)
 for mode in ['ramp','decel','reverse','short-reverse']:
  pts=[]
  for i in range(49):
   t=i/240
   x=(300*t+3500*t*t)if mode=='ramp'else (1000*min(t,.16)+100*max(0,t-.16))if mode=='decel'else (1000*min(t,.16)-500*max(0,t-.16))if mode=='reverse'else (1000*min(t,.195)-500*max(0,t-.195))
   pts.append((t,x,0.))
  add(mode,pts,.203)
 for hz in [30,60,120,240,480,960]:
  for speed in [60,1000,15000]:
   pts=[(round(i*FREQ/hz)/FREQ,speed*i/hz,0.)for i in range(round(.2*hz)+1)]
   add(f'fallback-steady-{hz}-{speed}',pts,pts[-1][0]+.003,rev=1.,cap=12000.,minimum=30.)
 for stop in [.025,.040,.070,.100]:
  pts=[(i*.008,1000*i*.008,0.)for i in range(5)];t,x,y=pts[-1]
  pts.extend((t+j*.008,x,y)for j in range(1,math.floor(stop/.008)+1))
  add(f'fallback-hold-{stop*1000:.0f}',pts,t+stop+.001,rev=1.,cap=12000.,minimum=30.)
 quant=[]
 for hz in [30,60,120,240,480,960]:
  for speed in [10,15,20,30,60,100,300]:
   for q in [.05,.1,.15,.25]:
    for phase in [0,.25,.5,.75]:
     pts=[(round(i*FREQ/hz)/FREQ,round((speed*i/hz+phase*q)/q)*q,0.)for i in range(round(.5*hz)+1)];old=estimate(pts,pts[-1][0]+.003);new=estimate(pts,pts[-1][0]+.003,True)
     quant.append({'hz':hz,'speed':speed,'quantum':q,'phase':phase,'old':old['selected'],'new':new['selected'],'newReason':new['reason']})
 summary={'realCases':len(real),'oldZero':sum(r['oldOffline']['distance']==0 for r in real),'newZero':sum(r['candidate']['distance']==0 for r in real),'oldShort3mm':sum(0<r['oldOffline']['distance']<3 for r in real),'newShort3mm':sum(0<r['candidate']['distance']<3 for r in real),'newNonzeroFromOldZero':[r['key']for r in real if r['oldOffline']['distance']==0<r['candidate']['distance']],'newZeroFromOldNonzero':[r['key']for r in real if r['candidate']['distance']==0<r['oldOffline']['distance']],'quantCases':len(quant),'quantNewZeroFromOldAboveMin':[r for r in quant if r['old']>10 and r['new']<=10]}
 summary['note']='120 of 121 releases had usable per-key points; multi-contact cancellation is not reproduced by isolated contact replay. Missing/down/Up/gap metadata retained.'
 summary['actualGe15QuantNewZero']=sum(r['speed']>=15 and r['old']>10 and r['new']<=10 for r in quant)
 summary['partialOrMissingBoundaryKeys']=[r['key'] for r in real if not r['downLogged'] or not r['upLogged'] or r['ordinalGaps']]
 # Offline assertions define what this study checked, not native test success.
 byname={r['name']:r for r in synth}
 assert abs(byname['hold-25ms']['candidate']['selected']-1000)<.01
 assert abs(byname['hold-40ms']['candidate']['selected']-725)<.01
 assert byname['hold-70ms']['candidate']['selected']==0
 assert byname['hold-100ms']['candidate']['selected']==0
 assert all(r['candidate']['selected']==0 for r in synth if r['name'].startswith('jump-base0.0'))
 assert all(r['candidate']['selected']<=325.1 for r in synth if r['name'].startswith('jump-base300.0'))
 assert byname['short-reverse']['candidate']['selected']==0
 assert byname['reverse']['candidate']['rawVector'][0]<0
 assert summary['actualGe15QuantNewZero']==0
 for r in synth:
  lim=12000 if r['name'].startswith('fallback-') else 2400
  assert math.isfinite(r['candidate']['selected']) and 0<=r['candidate']['selected']<=lim+1e-6,r['name']
 summary['offlineAssertions']='passed; hold25/40/70/100, stationary jump, moving jump bound, reversal, quantized speeds>=15, finite output caps'
 result={'type':'offline Python approximation; not native/product execution','snapshotSha256':hashlib.sha256(LOG.read_bytes()).hexdigest(),'scriptSha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'policy':'Touch progress-only actual-QPC evidence, duplicates retained in raw observations; 30-70ms age from last progress; >=70ms motion gap retires old progress on new motion','summary':summary,'real':real,'synthetic':synth,'quantized':quant}
 (ROOT/'touch-terminal-policy-2026-11-14.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 print(json.dumps(summary,ensure_ascii=False))
 for r in real:
  if r['key']in(5,74,90,108,107,109):print(json.dumps({'key':r['key'],'downLogged':r['downLogged'],'upLogged':r['upLogged'],'old':r['oldOffline'],'new':r['candidate']},ensure_ascii=False))
 print(json.dumps({'holdCases':[(r['name'],r['candidate']['selected'],r['candidate']['distance']) for r in synth if r['name'].startswith('hold-')]},ensure_ascii=False))
if __name__=='__main__':run()
