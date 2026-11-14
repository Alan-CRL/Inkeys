from pathlib import Path
import json,re,statistics,hashlib
from collections import Counter,defaultdict
ROOT=Path.cwd()
TASK=ROOT/'.trellis/tasks/archive/2026-11/11-13-whiteboard-pan-scale-device'
LOG=ROOT/'Build/Logs/pan-touch-feedback-2026-11-14/console.snapshot.log'
FREQ=10_000_000
OV=json.loads((LOG.parent/'overview.json').read_text(encoding='utf-8'))
REASONS={r['quality']['line']:r['reason'] for r in OV['releases'] if 'quality' in r}
lines=LOG.read_text(encoding='utf-8-sig',errors='replace').splitlines()
def fields(s):return dict(re.findall(r'([\w-]+)=([^ ]+)',s))
def pct(x):
    if not x:return None
    x=sorted(x)
    return {'count':len(x),'min':x[0],'median':statistics.median(x),'p90':x[round((len(x)-1)*.9)],'max':x[-1]}
inputs=[];bykey=defaultdict(list);rtsups=[];pending={};coarse=[];pointer=Counter();touchpos=[];mouseups=[]
for n,s in enumerate(lines,1):
    if '[CanvasPan]' in s and 'input ordinal=' in s:
        m=re.search(r'input ordinal=(\d+) epoch=(\d+) key=(\d+) class=(\d+) context=(\d+) cursor=(\d+) phase=(\d+).*sample-qpc=(\d+).*consume-qpc=(\d+)',s)
        if m:
            d=dict(zip(['ordinal','epoch','key','class','context','cursor','phase','qpc','consumeQpc'],map(int,m.groups())),line=n)
            inputs.append(d);bykey[d['key']].append(d)
    if '[CURSOR_TRACE]' in s and 'rts-input event=Up ' in s:
        f=fields(s);ident=(int(f['tcid']),int(f['cid']))
        if f.get('reason')=='arrival':pending[ident]={'line':n,'tick':int(f['tick']),'thread':int(f['thread'])}
        elif f.get('reason')=='published':
            a=pending.pop(ident,None);u={'line':n,'context':ident[0],'cursor':ident[1],'tick':int(f['tick']),'device':int(f['device']),'arrival':a}
            rtsups.append(u)
            if a:coarse.append({'publishedLine':n,'context':ident[0],'cursor':ident[1],'device':u['device'],'arrivalLine':a['line'],'arrivalTick':a['tick'],'publishedTick':u['tick'],'coarseMs':u['tick']-a['tick']})
    if '[CURSOR_TRACE]' in s and ' pointer message=' in s:
        f=fields(s);pointer[(f.get('type'),f.get('message'))]+=1
    if '[CURSOR_TRACE]' in s and 'touch-position via=pointer' in s:touchpos.append(n)
    if '[CURSOR_TRACE]' in s and ' mouse event=' in s and 'message=0x0202' in s:
        f=fields(s);mouseups.append({'line':n,'tick':int(f['tick']),'source':f.get('source'),'x':f.get('x'),'y':f.get('y')})
# Match producer Up logs with actual input Up by raw context/cursor and local output ordering.
ups=[x for x in inputs if x['phase']==2]
upToProducer={}
for r in rtsups:
    candidates=[x for x in ups if x['context']==r['context'] and x['cursor']==r['cursor'] and abs(x['line']-r['line'])<=35]
    if candidates:
        x=min(candidates,key=lambda x:abs(x['line']-r['line']))
        if x['key'] not in upToProducer or abs(r['line']-x['line'])<abs(upToProducer[x['key']]['line']-x['line']):upToProducer[x['key']]=r
releases=[]
for n,s in enumerate(lines,1):
    if '[CanvasPan]' not in s or ' release-quality ' not in s:continue
    f=fields(s)
    if 'key' not in f or 'source' not in f:continue
    klass=int(f['source'])>>32
    if klass not in [1,3,4]:continue
    key=int(f['key']);evs=bykey[key];terminal=[v for v in evs if v['phase']==2 and v['line']<=n]
    if not terminal:continue
    u=terminal[-1];raw=int(f['raw-last-qpc']);motion=int(f['motion-last-qpc']);row={'key':key,'class':klass,'qualityLine':n,'releaseReason':REASONS.get(n),'upLine':u['line'],'upQpc':u['qpc'],'rawLastQpc':raw,'motionLastQpc':motion,'consumeQpc':u['consumeQpc'],'motionToRawMs':(raw-motion)/10000,'rawToUpMs':(u['qpc']-raw)/10000,'motionToUpMs':(u['qpc']-motion)/10000,'upToConsumeMs':(u['consumeQpc']-u['qpc'])/10000,'motionInputLine':None,'rawInputLine':None}
    row['motionInputLine']=next((e['line'] for e in evs if e['qpc']==motion),None)
    row['rawInputLine']=next((e['line'] for e in evs if e['qpc']==raw),None)
    repeats=[e for e in evs if motion<e['qpc']<=raw and e['phase']==1];row['interveningMoveRecords']=len(repeats)
    prod=upToProducer.get(key)
    if prod:
        row['rtsUpPublishedLine']=prod['line'];row['rtsUpPublishedTick']=prod['tick'];row['rtsUpArrivalLine']=prod['arrival']['line'] if prod['arrival'] else None;row['rtsUpArrivalToPublishedCoarseMs']=prod['tick']-prod['arrival']['tick'] if prod['arrival'] else None
        nearby=[m for m in mouseups if prod['line']-20<=m['line']<=prod['line']+60 and m['source'] in ['4','8']]
        row['nearbyPromotedMouseUps']=nearby
    releases.append(row)
summary={}
for klass in [1,3,4]:
    rs=[r for r in releases if r['class']==klass]
    summary[str(klass)]={'releaseWithQualityAndInputUp':len(rs),'motionToRawMs':pct([r['motionToRawMs'] for r in rs]),'rawToUpMs':pct([r['rawToUpMs'] for r in rs]),'motionToUpMs':pct([r['motionToUpMs'] for r in rs]),'upToConsumeMs':pct([r['upToConsumeMs'] for r in rs]),'withRepeatedTrailingMoves':sum(r['interveningMoveRecords']>0 for r in rs),'motionAge45to55ms':sum(45<=r['motionToUpMs']<=55 for r in rs)}
coarseStats={str(c):pct([x['coarseMs'] for x in coarse if x['device']==c]) for c in sorted(set(x['device'] for x in coarse))}
readySummary={}
for klass in [1,3]:
    rs=[r for r in releases if r['class']==klass and r['releaseReason']=='ready']
    readySummary[str(klass)]={'count':len(rs),'motionToRawMs':pct([r['motionToRawMs'] for r in rs]),'rawToUpMs':pct([r['rawToUpMs'] for r in rs]),'motionToUpMs':pct([r['motionToUpMs'] for r in rs]),'upToConsumeMs':pct([r['upToConsumeMs'] for r in rs]),'trailingRepeatedMoves':sum(r['interveningMoveRecords']>0 for r in rs)}

result={'evidenceType':'read-only production-log timing decomposition; no hardware physical-lift timestamp','snapshotSha256':hashlib.sha256(LOG.read_bytes()).hexdigest(),'qpcFrequency':FREQ,'summary':summary,'readySummary':readySummary,'rtsArrivalToPublishedCoarseMsByDevice':coarseStats,'pointerMessageCounts':[{'type':k[0],'message':k[1],'count':v} for k,v in pointer.items()],'touchPositionPointerRecords':len(touchpos),'releases':releases,'rtsUpCoarsePairs':coarse,'key26':next((r for r in releases if r['key']==26),None)}
(TASK/'research/touch-lift-timing-2026-11-14.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k not in ['releases','rtsUpCoarsePairs']},ensure_ascii=False))
