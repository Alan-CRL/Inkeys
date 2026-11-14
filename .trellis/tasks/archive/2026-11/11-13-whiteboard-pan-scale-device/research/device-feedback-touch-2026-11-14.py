from pathlib import Path
import json,re,math,statistics,collections,bisect
ROOT=Path.cwd(); OUT=Path(__file__).resolve().parent
LOG=ROOT/'Build/Logs/pan-device-feedback-2026-11-14/console.snapshot.log'
lines=LOG.read_text(encoding='utf-8-sig',errors='replace').splitlines()
over=json.loads((LOG.parent/'overview.json').read_text(encoding='utf-8'))
def fields(s): return {k:v for k,v in re.findall(r'([\w-]+)=([^ ]+)',s)}
def vec(s): return tuple(map(float,s.strip('()').split(',')))
rows=[]; inputs=collections.defaultdict(list);steps=[];stops=[]
for no,s in enumerate(lines,1):
 m=re.search(r'\[CanvasPan\] seq=(\d+) qpc=(\d+) tid=(\d+) (.*)',s)
 if not m:continue
 body=m[4];r={'line':no,'seq':int(m[1]),'qpc':int(m[2]),'kind':body.split(' ')[0],'f':fields(body)};rows.append(r)
 if r['kind']=='input': inputs[int(r['f']['key'])].append(r)
 if r['kind']=='pan-step': steps.append(r)
 if r['kind']=='navigation-stop':stops.append(r)
 if r['kind']=='render' and steps and steps[-1]['line']>max([steps[-2]['line']] if len(steps)>1 else [0]):
  if 'render' not in steps[-1]:steps[-1]['render']=r
stepLines=[r['line'] for r in steps];rowLines=[r['line'] for r in rows]
penFirst=min(r['line'] for r in rows if r['kind']=='input' and r['f'].get('class')=='3' and r['f'].get('phase')=='0')
results=[]
for original in over['releases']:
 f=fields(original['text']);q=fields(original.get('quality',{}).get('text',''));p=fields(original.get('profile',{}).get('text',''))
 source=q.get('source',p.get('source'));cls=int(source)>>32 if source else original.get('terminal',{}).get('class')
 if cls!=1:continue
 key=int(q['key']) if 'key' in q else original.get('terminal',{}).get('key')
 info={'line':original['line'],'key':key,'keyConfidence':'quality' if 'key'in q else 'nearest-terminal-only','terminalAssociationMismatch':key!=original.get('terminal',{}).get('key'),'release':f,'quality':q,'profile':p,'beforeFirstPen':original['line']<penFirst}
 ctx=fields(over.get('contexts',{}).get(str(key),{}).get('text',''));info['context']=ctx
 startI=bisect.bisect_left(stepLines,info['line'])-1;start=steps[startI] if startI>=0 else None
 info['startStep']=start
 nextDown=next((r for r in rows[bisect.bisect_right(rowLines,info['line']):] if r['kind']=='input' and r['f'].get('phase')=='0'),None)
 info['nextDown']={a:nextDown[a] for a in ('line','qpc','f')} if nextDown else None
 coast=[];end=None;endKind=None;traceStops=[]
 if start and start['f'].get('phase')=='1':
  for r in rows[bisect.bisect_right(rowLines,info['line']):]:
   if nextDown and r['line']>=nextDown['line']:end=nextDown;endKind='new-down';break
   if r['kind']=='navigation-stop':
    traceStops.append(r);end=r;endKind=r['f'].get('reason')
    if r['f'].get('reason')=='fling-complete-or-cancel':
     later=next((s for s in steps if s['line']>r['line']),None)
     if later and later['f']['phase']=='0' and (not nextDown or later['line']<nextDown['line']):coast.append(later);end=later
    break
   if r['kind']=='pan-step':
    if int(r['f']['held'])>0:end=r;endKind='held-next-contact';break
    coast.append(r)
    if r['f']['phase']=='0':end=r;endKind='idle';break
  if end is None:endKind='log-end'
 else:endKind='not-coasting-at-release'
 info['coastStepCount']=len(coast);info['coastNonzeroStepCount']=sum(math.hypot(*vec(r['f']['delta-px']))>0 for r in coast)
 info['coastFirstLine']=coast[0]['line'] if coast else None;info['coastLastLine']=coast[-1]['line'] if coast else None
 info['endKind']=endKind;info['end']=end;info['stopRecords']=traceStops
 info['pixelDeltaSum']=[sum(vec(r['f']['delta-px'])[a] for r in coast) for a in (0,1)]
 renders=[s['render'] for s in coast if 'render'in s]
 info['coastRenderCount']=len(renders);info['coastPresentOkCount']=sum(r['f'].get('present-ok')=='1' for r in renders)
 info['firstCoastRender']=renders[0] if renders else None;info['lastCoastRender']=renders[-1] if renders else None
 if start and 'render'in start and renders:info['viewportPixelsChange']=[b-a for a,b in zip(vec(start['render']['f']['viewport']),vec(renders[-1]['f']['viewport']))]

 info['coastSeqGaps']=[[a['seq'],b['seq']] for a,b in zip(rows[bisect.bisect_left(rowLines,info['line']):bisect.bisect_right(rowLines,end['line'])] if end else [],rows[bisect.bisect_left(rowLines,info['line'])+1:bisect.bisect_right(rowLines,end['line'])] if end else []) if b['seq']!=a['seq']+1]
 if 'motion-per-dip' in ctx:
  mx,my=vec(ctx['motion-per-dip']);dx,dy=vec(ctx['dip-per-pixel']);scale=(mx*dx,my*dy)
  info['observedCoastMm']=sum(math.hypot(vec(r['f']['delta-px'])[0]*scale[0],vec(r['f']['delta-px'])[1]*scale[1]) for r in coast)
  if p.get('target-distance') and float(p['target-distance'])>0:info['observedToTargetRatio']=info['observedCoastMm']/float(p['target-distance'])
 if start and end:
  def frame(r):return int(r.get('render',{}).get('f',{}).get('frame-qpc',r['qpc']))
  info['elapsedToEndMs']=(frame(end)-frame(start))/10000
 ownInputs=inputs.get(key,[])
 terminal=[r for r in ownInputs if r['f'].get('phase')in('2','3') and r['line']<info['line']]
 info['actualTerminal']=terminal[-1] if terminal else None
 if q.get('last-qpc'):
  qs=[int(q[n]) for n in ('support-first-qpc','support-last-qpc','last-qpc')]
  abc=[next((r for r in ownInputs if int(r['f']['sample-qpc'])==value and r['f']['phase'] in ('0','1')),None) for value in qs]
  info['anchorsABC']=abc
  if all(abc):
   mx,my=vec(ctx['motion-per-dip']);dx,dy=vec(ctx['dip-per-pixel']);scale=(mx*dx,my*dy)
   unitvec=vec(f['raw']);mag=math.hypot(unitvec[0]*mx,unitvec[1]*my);u=(unitvec[0]*mx/mag,unitvec[1]*my/mag) if mag else (0,0)
   iv=[]
   for a,b in zip(abc,abc[1:]):
    pa=vec(a['f']['pos-px']);pb=vec(b['f']['pos-px']);delta=((pb[0]-pa[0])*scale[0],(pb[1]-pa[1])*scale[1]);dt=(int(b['f']['sample-qpc'])-int(a['f']['sample-qpc']))/1e7
    err=(2**-23)*(math.hypot(pa[0]*scale[0],pa[1]*scale[1])+math.hypot(pb[0]*scale[0],pb[1]*scale[1]));proj=delta[0]*u[0]+delta[1]*u[1]
    iv.append({'dtMs':dt*1000,'deltaMm':delta,'projectedMm':proj,'samePosition':pa==pb,'upperReconstructed':(max(0,proj)+.25+err)/(dt-1e-7)})
   info['intervalsABBC']=iv
 moves=[r for r in ownInputs if r['f']['phase']=='1' and r['line']<info['line']]
 if moves:
  last=moves[-1];same=[]
  for r in reversed(moves):
   if r['f']['pos-px']!=last['f']['pos-px']:break
   same.append(r)
  info['lastIdenticalMoveCount']=len(same);info['lastIdenticalSpanMs']=(int(last['f']['sample-qpc'])-int(same[-1]['f']['sample-qpc']))/10000
 results.append(info)
summary={'touchReleases':len(results),'reasonCounts':dict(collections.Counter(r['release']['reason'] for r in results)),'endKinds':dict(collections.Counter(r['endKind'] for r in results)),'readyWithoutObservedNonzeroCoast':[{'key':r['key'],'line':r['line'],'endKind':r['endKind']} for r in results if r['release']['reason']=='ready' and not r['coastNonzeroStepCount']],'shorterThan3mm':sum(0<float(r['profile'].get('target-distance',0))<3 for r in results),'shortWithZeroInterval':sum(0<float(r['profile'].get('target-distance',0))<3 and any(i['samePosition'] for i in r.get('intervalsABBC',[])) for r in results),'beforeAfter':{}}
for label,before in [('before',True),('after',False)]:
 group=[r for r in results if r['beforeFirstPen']==before and r['release']['reason']=='ready'];dist=[float(r['profile']['target-distance']) for r in group if 'target-distance'in r['profile']]
 summary['beforeAfter'][label]={'n':len(group),'profileObserved':len(dist),'below3mm':sum(d<3 for d in dist),'medianMm':statistics.median(dist),'supportLimited':sum(r['quality'].get('support-limited')=='1'for r in group)}
tailFirst=min(r['line'] for r in rows if r['kind']=='input' and r['f'].get('class')=='4' and r['f'].get('phase')=='0')
summary['lastTouchReleaseLine']=max(r['line']for r in results);summary['firstTailDownLine']=tailFirst;summary['touchReleasesAfterFirstTail']=sum(r['line']>tailFirst for r in results)
summary['readyWithoutPresentOk']=[{'key':r['key'],'line':r['line']}for r in results if r['release']['reason']=='ready' and not r['coastPresentOkCount']]
summary['keyAssociationCorrections']=[{'key':r['key'],'line':r['line']} for r in results if r['terminalAssociationMismatch']]
# Reuse prior offline study functions read-only, without running its output routine.
study=OUT/'overspeed-quality-design-2026-11-14.py';namespace={'__file__':str(study),'__name__':'read_only_quality_functions'}
exec(compile(study.read_text(encoding='utf-8'),str(study),'exec'),namespace)
opposite={}
for key in (171,172):
 points=[]
 for r in inputs[key]:
  if r['f']['phase']not in ('0','1') or int(r['f']['sample-qpc'])>111695896311:continue
  x,y=vec(r['f']['pos-px']);points.append((int(r['f']['sample-qpc'])/1e7,x*.5*.19444445,y*.5*.19791666))
 opposite[str(key)]=namespace['estimate'](points,2400,.25)
summary['twoFinger171172BeforeUp172Offline']=opposite
result={'snapshot':over['snapshot'],'firstPenLine':penFirst,'summary':summary,'touchReleases':results,'caveat':'Actual logged deltas are rounded; diagnostic sequence gaps mean sums are lower observations, not proof of lost product input. Quality key overrides nearest-terminal association.'}
# Persist per-gesture evidence only; the immutable 28MB source log is already retained by main.
def compactEvent(r):
 if r is None:return None
 f=r.get('f',{})
 out={'line':r['line'],'qpc':r['qpc']}
 for key in ('key','class','phase','held','samples','sample-qpc','frame-qpc','consume-qpc','delay-ms','pos-px','viewport','present-ok','reason'):
  if key in f:out[key]=f[key]
 return out
commonContext=results[0]['context']
compacted=[]
for r in results:
 c={k:v for k,v in r.items() if k not in ('startStep','nextDown','end','stopRecords','actualTerminal','firstCoastRender','lastCoastRender','context','anchorsABC','coastSeqGaps')}
 c['startStep']=compactEvent(r['startStep']);c['end']=compactEvent(r['end'])
 c['actualTerminal']=compactEvent(r['actualTerminal'])
 c['nextDown']=compactEvent(r['nextDown']) if r['nextDown'] else None
 c['stopRecords']=[compactEvent(e)for e in r['stopRecords']]
 c['coastDiagnosticSequenceGapCount']=len(r['coastSeqGaps'])
 c['firstCoastRender']=compactEvent(r['firstCoastRender']);c['lastCoastRender']=compactEvent(r['lastCoastRender'])
 if 'anchorsABC'in r:c['anchorsABC']=[compactEvent(e)for e in r['anchorsABC']]
 compacted.append(c)
result['commonTouchContext']=commonContext
brief=[]
for r in results:
 mx,my=vec(r['context']['motion-per-dip']);raw=vec(r['release']['raw']);selected=vec(r['release']['selected']);q=r['quality'];pf=r['profile']
 brief.append({'key':r['key'],'line':r['line'],'keyConfidence':r['keyConfidence'],'beforeFirstPen':r['beforeFirstPen'],'reason':r['release']['reason'],'rawMmPerSec':math.hypot(raw[0]*mx,raw[1]*my),'selectedMmPerSec':math.hypot(selected[0]*mx,selected[1]*my),'targetMm':float(pf['target-distance'])if 'target-distance'in pf else None,'durationSeconds':float(pf['duration-s'])if 'duration-s'in pf else None,'ageMs':float(r['release']['age-ms']),'samples':int(r['release']['samples']),'spanMs':float(r['release']['span-ms']),'qualityUpperMmPerSec':float(q['support-upper'])if 'support-upper'in q else None,'supportLimited':q.get('support-limited'),'boostSuppressed':q.get('boost-suppressed'),'tailLimitDipPerSec':float(r['release']['tail-limit']),'tailLimited':r['release']['tail-limited'],'coastMm':r.get('observedCoastMm'),'coastSteps':r['coastStepCount'],'presentOk':r['coastPresentOkCount'],'endKind':r['endKind'],'endLine':r['end']['line']if r['end']else None,'endElapsedMs':r.get('elapsedToEndMs'),'lastRepeatMoveCount':r.get('lastIdenticalMoveCount'),'lastRepeatSpanMs':r.get('lastIdenticalSpanMs'),'diagnosticSequenceGaps':len(r['coastSeqGaps'])})
result['touchReleases']=brief
result['keyCases']={str(r['key']):r for r in compacted if r['key']in(5,64,74,90,108,120,171)}
result['summary']['verifiedNextTouchDownWhereInputLineMissing']=[{'oldKey':85,'newKey':86,'rtsPublishedLine':39968,'registerLine':39970,'acceptedLine':39974},{'oldKey':92,'newKey':93,'rtsPublishedLine':41486,'registerLine':41488,'acceptedLine':41492},{'oldKey':128,'newKey':129,'rtsPublishedLine':50335,'registerLine':50337,'acceptedLine':50341}]
for key,r in result['summary']['twoFinger171172BeforeUp172Offline'].items():
 result['summary']['twoFinger171172BeforeUp172Offline'][key]={k:v for k,v in r.items()if k in('ready','rawCount','anchors','spanMs','rawVector','raw','latestTail','selectedWithA')}

(OUT/'device-feedback-touch-2026-11-14.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(summary,ensure_ascii=False,indent=2))
for r in results:
 if r['key'] in [5,64,74,90,108,120,171]:print(json.dumps({k:r.get(k) for k in ['key','line','keyConfidence','coastNonzeroStepCount','endKind','elapsedToEndMs','observedCoastMm','observedToTargetRatio','intervalsABBC','lastIdenticalMoveCount','lastIdenticalSpanMs']},ensure_ascii=False))
