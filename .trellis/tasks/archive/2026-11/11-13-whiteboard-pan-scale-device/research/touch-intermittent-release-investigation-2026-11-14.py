from pathlib import Path
import re, json, hashlib, math

TASK = Path(__file__).resolve().parent
ATTACHMENT = Path('C:/Users/alan-/.codex/attachments/c5d97310-dfa6-440a-a4d7-dca0cb31e3c8/已粘贴的文本.txt')
raw=ATTACHMENT.read_bytes();lines=raw.decode('utf-8-sig').splitlines()
patterns={
 'touch_navigation_class1':r'\[CanvasPan\].*\bclass=1\b',
 'touch_begin':r'\btouch-begin\b',
 'touch_end':r'\btouch-end\b',
 'touch_position':r'\btouch-position\b',
 'touch_count_nonzero':r'\btouchCount=[1-9]\d*',
 'touch_suppressed_true':r'\b(?:suppressed|touchSuppressed)=1\b',
 'rts_known_touch':r'\brts-input\b.*\bdeviceKnown=1 device=0\b',
 'rts_unknown_device0_placeholder':r'\brts-input\b.*\bdeviceKnown=0 device=0\b',
 'rts_touch_batch':r'\[CanvasPan\].*\brts-batch\b',
 'touch_pan_reject':r'\[CanvasPan\].*\bpan-reject device=0\b',
 'touch_mouse_source_type4':r'\bmouse-source\b.*\bdevice=4\b',
}
counts={k:{'count':len(found:=[i for i,s in enumerate(lines,1) if re.search(p,s)]),'lines':found} for k,p in patterns.items()}
cache=[]
for i,s in enumerate(lines,1):
 m=re.search(r'mouse-filter event=(\d+) touchKnown=(\d+) touchX=(-?\d+) touchY=(-?\d+) touchTick=(\d+) touchId=(\d+) primary=(\d+) via=(\S+)',s)
 if m:cache.append({'line':i,'event':int(m[1]),'known':int(m[2]),'x':int(m[3]),'y':int(m[4]),'tick':int(m[5]),'id':int(m[6]),'primary':int(m[7]),'via':m[8]})
# This is a faithful scalar specialization of current Touch AddSample timing/window rules.
# Synthetic arrivals only, NOT actual Touch input or production C++ execution.
def tracker(samples,cap):
 history=[];timing=False;resets=[]
 for i,(q,x) in enumerate(samples):
  if history:
   pq,px=history[-1];dt=(q-pq)/10_000_000;dist=abs(x-px)
   err=(2**-23)*(abs(x)+abs(px));allow=cap*(dt+1/10_000_000)+err
   if dt<.002 and dist>allow:
    resets.append({'index':i,'qpc':q,'dtMs':dt*1000,'distance':dist,'allowedDistance':allow})
    history=[];timing=True
   elif dt>=.040:history=[];timing=False
  while len(history)>1 and (q-history[1][0])/10_000_000>.050:history.pop(0)
  if len(history)==64:history.pop(0)
  history.append((q,x))
  if len(history)>=3 and (history[-1][0]-history[0][0])/10_000_000+1e-9>=.020:timing=False
 span=(history[-1][0]-history[0][0])/10_000_000
 reason='compressed-time' if timing else 'insufficient-samples' if len(history)<3 else 'insufficient-span' if span+1e-9<.020 else 'ready'
 return {'reason':reason,'remainingSamples':len(history),'spanMs':span*1000,'resetCount':len(resets),'resets':resets,'overallObservedSpeed':(samples[-1][1]-samples[0][1])/((samples[-1][0]-samples[0][0])/10_000_000)}
synthetic={}
for name,cap,step,unit in [('TouchPhysical',2400,1,'mm'),('TouchFallback',12000,5,'system-DIP')]:
 burst=[(1_000_000+(i//2)*83_330+(i%2)*1000,step*i) for i in range(20)]
 uniform=[(1_000_000+round(i*(burst[-1][0]-burst[0][0])/19),step*i) for i in range(20)]
 synthetic[name]={'unit':unit,'frequency':10_000_000,'cap':cap,'burstArrival':tracker(burst,cap),'uniformArrivalSameEndpointsAndDuration':tracker(uniform,cap),'samplesBurst':burst,'samplesUniform':uniform,'upAgeMs':2,'isActualUserTouchTrace':False,'productionCppExecuted':False}
known=[c for c in cache if c['known']]
first_unknown=next((c for c in cache if not c['known']),None)
result={'attachment':str(ATTACHMENT),'sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw),'lines':len(lines),'counts':counts,'cachedTouchKnownCount':len(known),'cachedTouchUnique':sorted(set((c['x'],c['y'],c['tick'],c['id'],c['primary'],c['via']) for c in known)),'firstKnown':known[0] if known else None,'lastKnown':known[-1] if known else None,'firstUnknown':first_unknown,'syntheticConditionalRisk':synthetic,'caveat':'No actual Touch lifecycle is visible. User says observed Touch problem occurs with pen far away, fingers only. Synthetic arithmetic demonstrates conditional guard behavior only; it cannot identify their actual Touch failure.'}
(TASK/'touch-intermittent-release-investigation-2026-11-14.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k not in ('syntheticConditionalRisk','counts')},ensure_ascii=False))
print(json.dumps(counts,ensure_ascii=False))
for k,v in synthetic.items():print(k,{kind:{a:b for a,b in val.items() if a!='resets'} for kind,val in v.items() if kind in ('burstArrival','uniformArrivalSameEndpointsAndDuration')})
