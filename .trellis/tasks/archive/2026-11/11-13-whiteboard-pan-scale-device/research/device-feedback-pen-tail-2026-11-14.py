from pathlib import Path
import json,re,hashlib,math,statistics
from collections import Counter
ROOT=Path(__file__).resolve().parents[4]
SNAP=ROOT/'Build/Logs/pan-device-feedback-2026-11-14/console.snapshot.log'
LINES=SNAP.read_text(encoding='utf-8-sig').splitlines()
input_re=re.compile(r'input ordinal=(\d+) epoch=(\d+) key=(\d+) class=(\d+) context=(\d+) cursor=(\d+) phase=(\d+) pos-px=\(([-\d.]+),([-\d.]+)\) sample-qpc=(\d+) frame-qpc=(\d+) consume-qpc=(\d+) delay-ms=([-\d.]+) action=([^ ]+)')
step_re=re.compile(r'pan-step phase=(\d+) held=(\d+) samples=(\d+) scrolling=(\d+) delta-px=\(([-\d.]+),([-\d.]+)\)')
bykey={};steps=[];contexts={};admissions={};releases={};quality={};profiles={};last_release=None;allinputs=[]
for num,line in enumerate(LINES,1):
 m=input_re.search(line)
 if m:
  vals=m.groups();e=dict(zip(['ordinal','epoch','key','class','context','cursor','phase'],map(int,vals[:7])),x=float(vals[7]),y=float(vals[8]),qpc=int(vals[9]),frameQpc=int(vals[10]),consumeQpc=int(vals[11]),delayMs=float(vals[12]),action=vals[13],line=num)
  allinputs.append(e);bykey.setdefault(e['key'],[]).append(e)
 m=step_re.search(line)
 if m:steps.append(dict(zip(['phase','held','samples','scrolling'],map(int,m.groups()[:4])),dx=float(m[5]),dy=float(m[6]),line=num))
 if 'pan-context key=' in line:
  k=int(re.search(r'key=(\d+)',line)[1]);contexts[k]={'line':num,'text':line}
 if 'pan-admission key=' in line:
  k=int(re.search(r'key=(\d+)',line)[1]);admissions[k]={'line':num,'text':line}
 if 'release-quality key=' in line:
  k=int(re.search(r'key=(\d+)',line)[1]);quality[k]={'line':num,'text':line}
 if 'release released=1' in line:
  prior=next(e for e in reversed(allinputs) if e['phase'] in (2,3));k=prior['key'];last_release=k;releases[k]={'line':num,'text':line}
 if 'release-profile source=' in line and last_release is not None:profiles[last_release]={'line':num,'text':line}

def model(es, mx=.19444445*.5,my=.19791666*.5):
 # Same detector branch structure, double offline arithmetic from logged .001px positions;
 # not native float execution. Suppression/gap input conditions are reported separately.
 states={};down=es[0];start=(down['x']*mx,down['y']*my);last=start;scroll=False;sx=sy=0.;first_cross=None
 for e in es[1:]:
  if e['phase']!=1:
   states[e['frameQpc']]={'dx':sx/mx,'dy':sy/my,'latestQpc':e['qpc']};continue
  pos=(e['x']*mx,e['y']*my);dx=pos[0]-last[0];dy=pos[1]-last[1]
  ddx=pos[0]-start[0];ddy=pos[1]-start[1];dist=math.hypot(ddx,ddy)
  if (not scroll and dist>1.51) or (scroll and (abs(dx)>.02 or abs(dy)>.02)):
   last=pos
   if not scroll:
    ratio=max(0.,dist-1.5)/dist;dx=ddx*ratio;dy=ddy*ratio;first_cross=e['line']
   if dx!=0 or dy!=0:scroll=True;sx+=dx;sy+=dy
  states[e['frameQpc']]={'dx':sx/mx,'dy':sy/my,'latestQpc':e['qpc']}
 return {'dx':sx/mx,'dy':sy/my,'firstSlopCrossLine':first_cross,'scrolling':scroll,'states':states}

renders=[]
for num,line in enumerate(LINES,1):
 m=re.search(r'render frame=(\d+) frame-qpc=(\d+) sample-qpc=(\d+).*page=(\d+) viewport=\(([-\d.]+),([-\d.]+)\)',line)
 if m:renders.append({'line':num,'frame':int(m[1]),'qpc':int(m[2]),'sampleQpc':int(m[3]),'page':int(m[4]),'x':float(m[5]),'y':float(m[6])})
registers={}
for num,line in enumerate(LINES,1):
 m=re.search(r'pan-register key=(\d+) class=(\d+).*down=\(([-\d.]+),([-\d.]+)\) qpc=(\d+)',line)
 if m:registers[int(m[1])]={'line':num,'x':float(m[3]),'y':float(m[4]),'qpc':int(m[5])}
rows=[]
for key,es in bykey.items():
 if es[0]['class']!=4:continue
 down=next((e for e in es if e['phase']==0),None);up=next((e for e in reversed(es) if e['phase'] in (2,3)),None)
 down_observed=down is not None
 if down is None and key in registers:
  d=registers[key];down=dict(es[0],line=d['line'],x=d['x'],y=d['y'],qpc=d['qpc'],phase=0,ordinal=es[0]['ordinal']-1)
 if not down or not up:continue
 model_es=es if down_observed else [down]+es
 rel=releases[key];seg=LINES[down['line']-1:rel['line']];ss=[s for s in steps if down['line']<=s['line']<=rel['line']]
 pred=model(model_es);ox=sum(s['dx'] for s in ss);oy=sum(s['dy'] for s in ss);moves=[e for e in es if e['phase']==1]
 gaps=[{'beforeOrdinal':a['ordinal'],'afterOrdinal':b['ordinal'],'beforeLine':a['line'],'afterLine':b['line']} for a,b in zip(es,es[1:]) if b['ordinal']!=a['ordinal']+1]
 other=[e for e in allinputs if down['line']<=e['line']<=rel['line'] and e['key']!=key]
 pre_render=next((r for r in reversed(renders) if r['line']<(pred['firstSlopCrossLine'] or down['line'])),None)
 next_down=min([e['line'] for e in allinputs if e['phase']==0 and e['line']>up['line']]+[len(LINES)+1])
 post_render=next((r for r in renders if rel['line']<r['line']<next_down),None)
 if post_render is None:post_render=next((r for r in reversed(renders) if down['line']<r['line']<rel['line']),None)
 viewport_delta=[pre_render['x']-post_render['x'],pre_render['y']-post_render['y']] if pre_render and post_render and pre_render['page']==post_render['page'] else None
 viewport_error=[viewport_delta[0]-pred['dx'],viewport_delta[1]-pred['dy']] if viewport_delta else None
 seq=[]
 for nn in range(down['line'],rel['line']+1):
  m=re.search(r'^\[CanvasPan\] seq=(\d+)',LINES[nn-1])
  if m:seq.append((nn,int(m[1])))
 seq_gaps=[{'beforeLine':a[0],'beforeSeq':a[1],'afterLine':b[0],'afterSeq':b[1]} for a,b in zip(seq,seq[1:]) if b[1]!=a[1]+1]
 frame_checks=[];model_frames=sorted(pred['states'])
 if pre_render:
  for rr in renders:
   if not(down['line']<=rr['line']<=post_render['line']):continue
   candidates=[fq for fq in model_frames if fq<=rr['qpc']]
   if not candidates:continue
   state=pred['states'][candidates[-1]]
   # Skip renders whose newest consumed sample is absent from printed input evidence.
   if rr['sampleQpc']!=state['latestQpc']:continue
   error=[pre_render['x']-rr['x']-state['dx'],pre_render['y']-rr['y']-state['dy']]
   frame_checks.append({'renderLine':rr['line'],'frameQpc':rr['qpc'],'sampleQpc':rr['sampleQpc'],'errorPixels':error})
 pred.pop('states')
 row={'key':key,'renderFrameCheckCount':len(frame_checks),'renderFrameMaxErrorPixels':max((max(abs(v) for v in rr['errorPixels']) for rr in frame_checks),default=None),'renderFrameErrorsOverPointOne':[rr for rr in frame_checks if max(abs(v) for v in rr['errorPixels'])>.1],'exactDownInputLogged':down_observed,'viewportStart':pre_render,'viewportEnd':post_render,'viewportDeltaPixels':viewport_delta,'viewportErrorPixels':viewport_error,'canvasSequenceGaps':seq_gaps,'downLine':down['line'],'upLine':up['line'],'release':rel,'context':contexts.get(key),'admission':admissions.get(key),'quality':quality.get(key),'profile':profiles.get(key),'inputCount':len(es),'moveCount':len(moves),'ordinalGaps':gaps,'classes':sorted(set(e['class'] for e in es)),'contexts':sorted(set(e['context'] for e in es)),'cursors':sorted(set(e['cursor'] for e in es)),'epochs':sorted(set(e['epoch'] for e in es)),'phaseCounts':dict(Counter(str(e['phase']) for e in es)),'actions':dict(Counter(e['action'] for e in es)),'durationMs':(up['qpc']-down['qpc'])/1e4,'lastMoveAgeMs':(up['qpc']-moves[-1]['qpc'])/1e4 if moves else None,'panStepCount':len(ss),'scrollingFrames':sum(bool(s['scrolling']) for s in ss),'nonzeroFrames':sum(bool(s['dx'] or s['dy']) for s in ss),'maxHeld':max((s['held'] for s in ss),default=0),'upHeld':ss[-1]['held'] if ss else None,'sumDeltaPixels':[ox,oy],'predictedDeltaPixels':pred,'deltaErrorPixels':[ox-pred['dx'],oy-pred['dy']],'frameRoundingBoundPixels':.005*len(ss)+.03,'maxMotionDistanceMm':max(math.hypot((e['x']-down['x'])*.19444445*.5,(e['y']-down['y'])*.19791666*.5) for e in model_es),'otherContactInputs':len(other),'stops':[{'line':down['line']+i,'text':l} for i,l in enumerate(seg) if 'stop trigger=' in l or 'navigation-stop' in l or 'viewport-clamp' in l],'suppressedFrames':[{'line':down['line']+i,'text':l} for i,l in enumerate(seg) if 'system trigger=' in l and ('suppressed=1' in l or 'touchSuppressed=1' in l or 'selection=0' in l)]}
 rows.append(row)

tail_start=min(r['downLine'] for r in rows);rts_downs=[{'line':n,'text':l} for n,l in enumerate(LINES,1) if 'rts-input event=Down reason=published' in l and 'cid=22 ' in l]
# Source pen-sample QPC directly preceding each published RTS Down can match exact nav Down QPC.
extra=[]
for rd in rts_downs:
 sample=next((l for l in reversed(LINES[max(0,rd['line']-6):rd['line']-1]) if 'pen-sample contact=1 inverted=1' in l),None)
 q=int(re.search(r'qpc=(\d+)',sample)[1]) if sample else None
 if q is not None and not any(q==next((e['qpc'] for e in bykey[r['key']] if e['phase']==0),registers.get(r['key'],{}).get('qpc')) for r in rows):
  nearby=[{'line':n,'text':l} for n,l in enumerate(LINES[rd['line']:rd['line']+100],rd['line']+1) if 'system trigger=' in l][:3]
  extra.append({'down':rd,'penSample':sample,'qpc':q,'nearbySystemState':nearby})
config=ROOT/'Build/ARM64/Debug/PanOverspeedValidation/Inkeys/Config/main.json';saved=json.loads(config.read_bytes().decode('utf-8-sig'))['Drawing']['Whiteboard']['PanInertia']
down_delays=[e['delayMs'] for es in bykey.values() for e in es if e['class']==4 and e['phase']==0]
latency={}
for cls in (3,4):
 age=[];interval=[]
 for key,es in bykey.items():
  if es[0]['class']!=cls:continue
  for nn in range(es[0]['line'],es[-1]['line']+1):
   text=LINES[nn-1]
   if '[CanvasPan]' not in text or ' render frame=' not in text:continue
   m=re.search(r'sample-to-submit-ms=([-\d.]+)',text)
   if m and float(m[1])>=0:age.append(float(m[1]))
   m=re.search(r'submit-interval-ms=([-\d.]+)',text)
   if m and float(m[1])>=0:interval.append(float(m[1]))
 latency[str(cls)]={'sampleToSubmitCount':len(age),'sampleToSubmitMedianMs':statistics.median(age) if age else None,'sampleToSubmitP95Ms':sorted(age)[int(len(age)*.95)] if age else None,'sampleToSubmitMaxMs':max(age) if age else None,'submitIntervalMedianMs':statistics.median(interval) if interval else None}
result={'downConsumeDelayMedianMs':statistics.median(down_delays),'downConsumeDelayMaxMs':max(down_delays),'softwareSubmitMetricsByClass':latency,'evidence':'log parsing plus offline detector geometry calculation, not native execution or hardware measurement','snapshot':str(SNAP),'snapshotSha256':hashlib.sha256(SNAP.read_bytes()).hexdigest(),'snapshotLineCount':len(LINES),'configurationAtAnalysis':{'path':str(config),'sha256':hashlib.sha256(config.read_bytes()).hexdigest(),'PanInertia':saved},'summary':{'tailGestures':len(rows),'disabledReleases':sum('reason=disabled ' in r['release']['text'] for r in rows),'acceptedAdmissions':sum(r['admission'] is not None and 'accepted=1 ' in r['admission']['text'] for r in rows),'allInputEvents':sum(r['inputCount'] for r in rows),'gesturesWithOrdinalGaps':sum(bool(r['ordinalGaps']) for r in rows),'withNonzeroHeldMovement':sum(r['nonzeroFrames']>0 for r in rows),'withOtherContactInputs':sum(r['otherContactInputs']>0 for r in rows),'withStopsOrClamp':sum(bool(r['stops']) for r in rows),'withSuppressedOrNonSelectionState':sum(bool(r['suppressedFrames']) for r in rows),'activeNonInvertedSamples':sum('pen-sample contact=1 inverted=0 ' in l for r in rows for l in LINES[r['downLine']-1:r['release']['line']]),'pressureRecordsInSnapshot':sum('pressure' in l.lower() for l in LINES),'renderFramesChecked':sum(r['renderFrameCheckCount'] for r in rows),'renderFramesOverPointOneError':sum(len(r['renderFrameErrorsOverPointOne']) for r in rows),'renderFrameMaxErrorPixels':max(r['renderFrameMaxErrorPixels'] for r in rows if r['renderFrameMaxErrorPixels'] is not None),'allUpHeldZero':all(r['upHeld']==0 for r in rows),'allSingleHeld':all(r['maxHeld']==1 for r in rows),'maxTotalErrorPixels':max(max(abs(v) for v in r['deltaErrorPixels']) for r in rows),'matchedWithinAccumulatedPrintRounding':sum(max(abs(v) for v in r['deltaErrorPixels'])<=r['frameRoundingBoundPixels'] for r in rows),'upAgeMedianMs':statistics.median(r['lastMoveAgeMs'] for r in rows),'durationMedianMs':statistics.median(r['durationMs'] for r in rows),'qualityValidReleaseCount':sum('support-valid=1' in r['quality']['text'] for r in rows),'rawTailDownCount':len(rts_downs),'extraNonNavigationTailDowns':len(extra),'viewportMatchedWithinPointOnePixel':sum(r['viewportErrorPixels'] is not None and max(abs(v) for v in r['viewportErrorPixels'])<.1 for r in rows),'maxExactDownViewportErrorPixels':max(max(abs(v) for v in r['viewportErrorPixels']) for r in rows if r['exactDownInputLogged'] and r['viewportErrorPixels'] is not None),'maxViewportErrorPixels':max(max(abs(v) for v in r['viewportErrorPixels']) for r in rows if r['viewportErrorPixels'] is not None)},'extraNonNavigationTailDowns':extra,'gestures':rows}
OUT=Path(__file__).with_suffix('.json');OUT.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(result['summary'],ensure_ascii=False))
for r in rows:
 if max(abs(v) for v in r['deltaErrorPixels'])>r['frameRoundingBoundPixels']:print('LOG_DELTA_GAP',r['key'],r['deltaErrorPixels'],'seqgapcount',len(r['canvasSequenceGaps']),'viewportErr',r['viewportErrorPixels'])
