from pathlib import Path
import json,re,math,bisect,statistics,collections
ROOT=Path.cwd();OUT=Path(__file__).resolve().parent
LOG=ROOT/'Build/Logs/pan-touch-feedback-2026-11-14/console.snapshot.log'
lines=LOG.read_text(encoding='utf-8-sig',errors='replace').splitlines();overview=json.loads((LOG.parent/'overview.json').read_text(encoding='utf-8'))
def fields(s):return dict(re.findall(r'([\w-]+)=([^ ]+)',s))
def vec(s):return tuple(map(float,s.strip('()').split(',')))
rows=[];steps=[];inputs=collections.defaultdict(list)
for no,s in enumerate(lines,1):
 m=re.search(r'\[CanvasPan\] seq=(\d+) qpc=(\d+) tid=(\d+) (.*)',s)
 if not m:continue
 r={'line':no,'seq':int(m[1]),'qpc':int(m[2]),'kind':m[4].split(' ')[0],'f':fields(m[4])};rows.append(r)
 if r['kind']=='input':inputs[int(r['f']['key'])].append(r)
 elif r['kind']=='pan-step':steps.append(r)
 elif r['kind']=='render' and steps and 'render'not in steps[-1]:steps[-1]['render']=r
rowLines=[r['line']for r in rows];stepLines=[r['line']for r in steps]
def frame(r):return int(r.get('render',{}).get('f',{}).get('frame-qpc',r['qpc']))
def compact(r):
 if not r:return None
 return {'line':r['line'],'qpc':r['qpc']}|{k:v for k,v in r['f'].items()if k in('key','class','phase','held','samples','sample-qpc','frame-qpc','pos-px','reason','delta-px','v-content-dip','present-ok','viewport')}
results=[]
for original in overview['releases']:
 if original.get('class')!=1:continue
 key=original['key'];rel=fields(original['release']);q=fields(original.get('quality',{}).get('text',''));pf=fields(original.get('profile',{}).get('text',''));ctx=fields(overview['contexts'].get(str(key),{}).get('text',''))
 mx,my=vec(ctx.get('motion-per-dip','(.19444445,.19791666)'));dx,dy=vec(ctx.get('dip-per-pixel','(.5,.5)'));scale=(mx*dx,my*dy)
 normDip=lambda s:math.hypot(vec(s)[0]*mx,vec(s)[1]*my)
 normPx=lambda s:math.hypot(vec(s)[0]*scale[0],vec(s)[1]*scale[1])
 r={'key':key,'line':original['line'],'reason':rel['reason'],'rawMmPerSec':normDip(rel['raw']),'selectedMmPerSec':normDip(rel['selected']),'boostedMmPerSec':normDip(rel['boosted']),'motionAgeMs':float(rel['age-ms']),'ageGate':max(0,min(1,(70-float(rel['age-ms']))/40)),'supportLimited':q.get('support-limited'),'ageLimited':q.get('age-limited'),'boostSuppressed':q.get('boost-suppressed'),'supportUpper':float(q['support-upper'])if 'support-upper'in q else None,'tailLimited':rel['tail-limited'],'tailLimitDip':float(rel['tail-limit']),'rawPoints':int(q.get('raw-points',rel['samples'])),'motionPoints':int(q['motion-points'])if 'motion-points'in q else None,'anchors':int(q['anchors'])if 'anchors'in q else None,'targetMm':float(pf['target-distance'])if 'target-distance'in pf else None,'durationSeconds':float(pf['duration-s'])if 'duration-s'in pf else None}
 r['selectedRawRatio']=r['selectedMmPerSec']/r['rawMmPerSec']if r['rawMmPerSec']else None
 own=[x for x in inputs[key]if x['line']<r['line']];terminal=next((x for x in reversed(own)if x['f']['phase']in('2','3')),None);r['terminal']=compact(terminal)
 startIndex=bisect.bisect_left(stepLines,r['line'])-1;start=steps[startIndex]if startIndex>=0 else None;r['releaseStep']=compact(start);releaseFrameQpc=frame(start) if start and start['f'].get('phase')=='1' else int(terminal['f']['frame-qpc']) if terminal else int(re.search(r'qpc=(\d+)',original['release'])[1]);r['releaseStepMissing']=not(start and start['f'].get('phase')=='1');r['releaseFrameQpc']=releaseFrameQpc
 rawLast=int(q['raw-last-qpc'])if 'raw-last-qpc'in q else None;motionLast=int(q['motion-last-qpc'])if 'motion-last-qpc'in q else None
 r['rawLastQpc']=rawLast;r['motionLastQpc']=motionLast;r['rawVsMotionLagMs']=(rawLast-motionLast)/10000 if rawLast and motionLast else None
 unique=[]
 for a in own:
  if a['f']['phase']not in('0','1'):continue
  if not unique or a['f']['pos-px']!=unique[-1]['f']['pos-px']:unique.append(a)
 if len(unique)>1:
  a,b=unique[-2:];dt=(int(b['f']['sample-qpc'])-int(a['f']['sample-qpc']))/1e7
  pa,pb=vec(a['f']['pos-px']),vec(b['f']['pos-px']);dist=math.hypot((pb[0]-pa[0])*scale[0],(pb[1]-pa[1])*scale[1]);r['lastProgressIntervalMs']=dt*1000;r['lastProgressObservedSpeedMmPerSec']=dist/dt if dt>0 else None;r['lastProgressPair']=[compact(a),compact(b)]
 beginLine=overview['contexts'].get(str(key),{}).get('line',own[0]['line']if own else 0)
 held=[a for a in steps[:startIndex]if a['line']>=beginLine and a['f'].get('phase')=='2' and normPx(a['f']['delta-px'])>0]
 r['lastNonzeroHeldStep']=compact(held[-1])if held else None
 if held:
  h=held[-1];idx=bisect.bisect_left(stepLines,h['line']);prev=steps[idx-1]if idx else None;dt=(frame(h)-frame(prev))/1e7 if prev else None
  r['lastHeldFrameObservedSpeedMmPerSec']=normPx(h['f']['delta-px'])/dt if dt and dt>0 else None
  r['lastHeldToReleaseMs']=(releaseFrameQpc-frame(h))/10000
 tailrows=rows[bisect.bisect_right(rowLines,r['line']):];nextDown=next((a for a in tailrows if a['kind']=='input'and a['f'].get('phase')=='0'),None)
 coast=[];end=None;stopRecords=[];kind='not-coasting-at-release'
 if r['reason']=='ready':
  kind='end-of-log'
  for a in tailrows:
   if nextDown and a['line']>=nextDown['line']:end=a;kind='next-down';break
   if a['kind']=='navigation-stop':
    end=a;kind=a['f'].get('reason');stopRecords.append(a)
    if kind=='fling-complete-or-cancel':
     nxt=next((z for z in steps if z['line']>a['line']),None)
     if nxt and nxt['f']['phase']=='0'and(not nextDown or nxt['line']<nextDown['line']):coast.append(nxt);end=nxt
    break
   if a['kind']=='pan-step':
    if int(a['f']['held'])>0:end=a;kind='next-held-contact';break
    coast.append(a)
    if a['f']['phase']=='0':end=a;kind='idle';break
 r['coastSteps']=len(coast);r['nonzeroCoastSteps']=sum(normPx(a['f']['delta-px'])>0 for a in coast);r['coastMm']=sum(normPx(a['f']['delta-px'])for a in coast)
 r['coastPresentOk']=sum(a.get('render',{}).get('f',{}).get('present-ok')=='1'for a in coast);r['endKind']=kind;r['end']=compact(end);r['stopRecords']=[compact(z)for z in stopRecords];r['nextDown']=compact(nextDown)
 r['elapsedToEndMs']=(frame(end)-releaseFrameQpc)/10000 if end else None;r['coastToTargetRatio']=r['coastMm']/r['targetMm']if r['targetMm']else None
 r['firstCoastFrames']=[{'line':a['line'],'elapsedMs':(frame(a)-releaseFrameQpc)/10000,'deltaMm':normPx(a['f']['delta-px']),'speedMmPerSec':normDip(a['f']['v-content-dip']),'presentOk':a.get('render',{}).get('f',{}).get('present-ok')}for a in coast[:4]] if start else []
 r['preUpLastMoves']=[compact(a)for a in own if a['f']['phase']in('0','1')][-10:]
 groupEnd=startIndex
 if groupEnd>=0 and int(steps[groupEnd]['f']['held'])==0:groupEnd-=1
 groupBegin=groupEnd
 while groupBegin>=0 and int(steps[groupBegin]['f']['held'])>0:groupBegin-=1
 group=steps[groupBegin+1:groupEnd+1];lo=steps[groupBegin]['line']if groupBegin>=0 else 0
 groupEvents=[z for z in rows[bisect.bisect_right(rowLines,lo):bisect.bisect_left(rowLines,r['line'])]if z['kind']=='input'and z['f'].get('class')=='1']
 r['groupMaxHeld']=max([int(z['f']['held'])for z in group]or[0]);r['groupHeldTravelMm']=sum(normPx(z['f']['delta-px'])for z in group)
 r['groupFirstHeldLine']=group[0]['line']if group else None;r['groupLastHeldLine']=group[-1]['line']if group else None
 r['groupKeys']=sorted(set(int(z['f']['key'])for z in groupEvents));r['groupDownUpEvents']=[compact(z)for z in groupEvents if z['f']['phase']in('0','2','3')]
 groupDown=[int(z['f']['sample-qpc'])for z in groupEvents if z['f']['phase']=='0']
 r['groupDownToFinalUpMs']=(int(terminal['f']['sample-qpc'])-min(groupDown))/10000 if terminal and groupDown else None
 results.append(r)
ready=[r for r in results if r['reason']=='ready']
summary={'touchReleases':len(results),'reasons':dict(collections.Counter(r['reason']for r in results)),'readyEndKinds':dict(collections.Counter(r['endKind']for r in ready)),'readyWithoutNonzeroCoast':[r['key']for r in ready if not r['nonzeroCoastSteps']],'readyWithoutPresentOk':[r['key']for r in ready if not r['coastPresentOk']],'readyAgeLimitedCounts':dict(collections.Counter(r['ageLimited']for r in ready)),'readySupportLimitedCounts':dict(collections.Counter(r['supportLimited']for r in ready)),'readyMedianMotionAgeMs':statistics.median(r['motionAgeMs']for r in ready),'readyMedianSelectedRawRatio':statistics.median(r['selectedRawRatio']for r in ready),'readyMedianActualCoastMm':statistics.median(r['coastMm']for r in ready),'readyMedianCoastUntilNextEventMs':statistics.median(r['elapsedToEndMs']for r in ready if r['elapsedToEndMs']is not None)}
result={'snapshot':overview['snapshot'],'summary':summary,'releases':results,'caveat':'Readonly log analysis, not native execution or optical/physical motion test. Diagnostic loss and rounded deltas bound exact accumulated distances.'}
# Keep a small per-gesture ledger and only the key-case timelines; raw log remains immutable in Build.
caseKeys={5,9,26,30,31,46,48,64,68,174,187}
result['cases']={str(z['key']):z for z in results if z['key']in caseKeys}
large={'preUpLastMoves','lastProgressPair','groupDownUpEvents','releaseStep','lastNonzeroHeldStep','firstCoastFrames','terminal','nextDown','end','stopRecords'}
result['releases']=[{k:v for k,v in z.items()if k not in large}for z in results]
summary['groupMaxHeldByReason']={why:dict(collections.Counter(z['groupMaxHeld']for z in results if z['reason']==why))for why in set(z['reason']for z in results)}
summary['firstCoastWithin25msRetainsSelectedSpeed']=[{'key':z['key'],'releaseLine':z['line'],'selectedMmPerSec':z['selectedMmPerSec'],'firstFrames':z['firstCoastFrames']}for z in results if z['key']in(5,9,26,30)]
summary['verifiedMissingInputDowns']=[{'oldKey':14,'newKey':15,'registerLine':18306,'acceptedLine':18311},{'oldKey':23,'newKey':24,'registerLine':20954,'acceptedLine':20959},{'oldKey':29,'newKey':30,'registerLine':22489,'acceptedLine':22494},{'oldKey':151,'newKey':153,'registerLine':40018,'acceptedLine':40025},{'oldKey':172,'newKey':174,'registerLine':42783,'acceptedLine':42785}]
(OUT/'touch-handoff-feedback-2026-11-14.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(summary,ensure_ascii=False,indent=2))
for r in sorted(ready,key=lambda a:a['coastMm'])[:14]:print({k:r[k]for k in('key','line','rawMmPerSec','selectedMmPerSec','selectedRawRatio','motionAgeMs','targetMm','coastMm','elapsedToEndMs','endKind')})
