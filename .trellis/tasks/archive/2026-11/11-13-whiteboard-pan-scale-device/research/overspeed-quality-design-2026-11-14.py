"""Offline quality-rule candidate evaluation. Not a native product test."""
from pathlib import Path
import json,math,hashlib
ROOT=Path(__file__).resolve().parent
MIN_DT=.002
CAP=1800.
EPS=2**-23

def evidence(samples):
    chosen=[]
    for p in reversed(samples):
        if not chosen or chosen[-1][0]-p[0]+1e-9>=MIN_DT:chosen.append(p)
    return list(reversed(chosen))

def history(samples,reversal=.15):
    h=[];direction=(0.,0.);anchor=None;resets=0
    for p in samples:
        if h:
            prev=h[-1]
            if p[0]<=prev[0]:continue
            dx=p[1]-prev[1];dy=p[2]-prev[2];dist=math.hypot(dx,dy)
            if p[0]-prev[0]>=.040:
                h=[];direction=(0.,0.);anchor=None;resets+=1
            elif dist:
                along=dx*direction[0]+dy*direction[1]
                if direction!=(0.,0.) and along<0:
                    if anchor is None:anchor=prev
                    rx=p[1]-anchor[1];ry=p[2]-anchor[2]
                    if rx*direction[0]+ry*direction[1]<=-reversal:
                        h=[anchor];dd=math.hypot(rx,ry);direction=(rx/dd,ry/dd);anchor=None;resets+=1
                else:
                    anchor=None
                    if dist>=reversal or direction==(0.,0.):direction=(dx/dist,dy/dist)
        while len(h)>1 and p[0]-h[1][0]>.050:h.pop(0)
        if len(h)==64:h.pop(0)
        h.append(p)
    return h,direction,resets

def estimate(samples,cap=CAP,reversal=.15):
    h,direction,resets=history(samples,reversal)
    e=evidence(h)
    out={'rawCount':len(h),'anchors':len(e),'spanMs':1000*(e[-1][0]-e[0][0]) if e else 0,'reversalsOrGaps':resets}
    if len(e)<3 or e[-1][0]-e[0][0]+1e-9<.020:return out|{'ready':False,'selected':0}
    n=len(e);mt=sum(p[0] for p in e)/n;mx=sum(p[1] for p in e)/n;my=sum(p[2] for p in e)/n
    den=sum((p[0]-mt)**2 for p in e)
    vx=sum((p[0]-mt)*(p[1]-mx) for p in e)/den;vy=sum((p[0]-mt)*(p[2]-my) for p in e)/den
    raw=math.hypot(vx,vy)
    if not raw or vx*direction[0]+vy*direction[1]<=0:return out|{'ready':False,'raw':raw,'selected':0}
    ux=vx/raw;uy=vy/raw
    tail=None
    for old in reversed(h[:-1]):
        dt=h[-1][0]-old[0];dx=h[-1][1]-old[1];dy=h[-1][2]-old[2]
        if dt+1e-9<MIN_DT or math.hypot(dx,dy)<reversal:continue
        if dx*vx+dy*vy>0:tail=math.hypot(dx,dy)/dt
        break
    A,B,C=e[-3:]
    def projected(a,b):return ((b[1]-a[1])*ux+(b[2]-a[2])*uy)/(b[0]-a[0])
    prev=projected(A,B);last=projected(B,C)
    # Upper allowance is coordinate float + one QPC tick, not an empirical speed bound.
    coordinate_error=EPS*(math.hypot(A[1],A[2])+math.hypot(B[1],B[2]))
    last_error=EPS*(math.hypot(B[1],B[2])+math.hypot(C[1],C[2]))
    previous_dt=B[0]-A[0];last_dt=C[0]-B[0]
    if min(previous_dt,last_dt)<=1e-7:
        return out|{'ready':False,'raw':raw,'selected':0,'qualityReason':'support-time-resolution'}
    support_upper=(max(0.,prev)*previous_dt+coordinate_error)/(previous_dt-1e-7)
    last_upper=(max(0.,last)*last_dt+last_error)/(last_dt-1e-7)
    regular=min(raw,tail if tail is not None else raw,cap)
    prefix=min(regular,support_upper)
    strict=min(regular,support_upper,last_upper)
    pair_upper=min((max(0.,prev)*previous_dt+reversal+coordinate_error)/(previous_dt-1e-7),
                   (max(0.,last)*last_dt+reversal+last_error)/(last_dt-1e-7))
    selected=min(regular,pair_upper)
    positive_intervals=0
    for left,right in zip(e,e[1:]):
        delta=(right[1]-left[1])*ux+(right[2]-left[2])*uy
        err=EPS*(math.hypot(left[1],left[2])+math.hypot(right[1],right[2]))
        positive_intervals+=delta>err
    upper_ab=(max(0.,prev)*previous_dt+reversal+coordinate_error)/(previous_dt-1e-7)
    upper_bc=(max(0.,last)*last_dt+reversal+last_error)/(last_dt-1e-7)
    zero_limiter=(upper_ab<=upper_bc and abs(prev*previous_dt)<=coordinate_error) or (upper_bc<=upper_ab and abs(last*last_dt)<=last_error)
    quality_limited=selected+1e-9<regular
    selected_a=selected if positive_intervals>=2 else 0.
    selected_ab=0. if (quality_limited and zero_limiter) else selected_a
    return out|{'positiveIntervals':positive_intervals,'zeroLimitingInterval':bool(zero_limiter),'selectedWithA':selected_a,'selectedWithAB':selected_ab}|{'ready':True,'raw':raw,'rawVector':[vx,vy],'latestTail':tail,
                'previousIndependent':prev,'lastIndependent':last,
                'supportUpperWithoutSpatialBudget':support_upper,'lastFloatUpper':last_upper,
                'prefixCandidateSelected':prefix,'strictFloatPairSelected':strict,
                'supportPairUpperWithSpatialBudget':pair_upper,'spatialPairSelected':selected,
                'oldSelected':regular,'selected':selected,
                'qualityLimited':selected+1e-9<regular,'wouldPermitBoost':not(selected+1e-9<regular)}


def line(speed,hz=240,duration=.1,mode='steady',last_delta=0):
    n=round(duration*hz)
    points=[]
    for i in range(n+1):
        t=round(i*1e7/hz)/1e7
        if mode=='steady':x=speed*i/hz
        elif mode=='ramp':x=300*t+0.5*(speed-300)/duration*t*t
        elif mode=='step':x=300*min(t,duration-.02)+speed*max(0,t-(duration-.02))
        elif mode=='last_step':x=300*min(t,duration-1/hz)+speed*max(0,t-(duration-1/hz))
        elif mode=='decel':x=speed*min(t,duration-.02)+300*max(0,t-(duration-.02))
        points.append((t,x,0.))
    t,x,y=points[-1];points[-1]=(t,x+last_delta,y)
    return points

def run():
    cases=[]
    for hz in [30,60,120,240,480,960]:
        for v in [20,300,1700,1800,1801,2500,5000,12000]:
            r=estimate(line(v,hz));cases.append({'kind':'steady','hz':hz,'speed':v,'result':r})
            assert r['ready'] and r['selected']>0,(hz,v,r)
            assert abs(r['selected']-min(v,CAP))<max(.3,v*1e-4),(hz,v,r)
        for delta in [1,3,10,30,100,300]:
            r=estimate(line(300,hz,last_delta=delta));cases.append({'kind':'terminal-spike','hz':hz,'delta':delta,'result':r})
            assert r['selected']<=375.1,(hz,delta,r)
        for mode in ['ramp','step','last_step','decel']:
            r=estimate(line(2500,hz,mode=mode));cases.append({'kind':mode,'hz':hz,'result':r})
    for x in [42.,102.]:
        samples=[(0.,0.,0.),(.01,3.,0.),(.02,6.,0.),(.03,9.,0.),(.0401,x,0.)]
        r=estimate(samples);cases.append({'kind':'review-terminal-example','lastMm':x,'result':r});assert r['selected']<315.1
    real=[]
    old=json.loads((ROOT/'pen-release-trace-2026-11-14.json').read_text(encoding='utf-8'))
    for k,events in old['events_by_key'].items():
        if k not in ['76','77','78','79','80']:continue
        points=[(p['qpc']/1e7,p['x_px']*.5*old['motion_per_dip'][0],p['y_px']*.5*old['motion_per_dip'][1]) for p in events if p['phase'] in [0,1]]
        origin=points[0][0];points=[(t-origin,x,y) for t,x,y in points]
        r=estimate(points);real.append({'attachment':'first','key':k,'result':r});assert r['ready'] and r['selected']>15
    new=json.loads((ROOT/'pen-intermittent-release-investigation-2026-11-14.json').read_text(encoding='utf-8'))
    for k,g in new['gestures'].items():
        points=[(p['qpc']/1e7,p['xm'],p['ym']) for p in g['events'] if p['phase'] in [0,1]]
        origin=points[0][0];points=[(t-origin,x,y) for t,x,y in points]
        r=estimate(points);real.append({'attachment':'second','key':k,'result':r});assert r['ready'] and r['selected']>15
    plateau=estimate([(0.,0.,0.),(.01,3.,0.),(.02,6.,0.),(.03,9.,0.),(.04,102.,0.),(.05,102.,0.)])
    quantized=[]
    for hz in [120,240,480,960]:
        for speed in [10,15,20,30,60,100,300]:
            for quantum in [.05,.1,.15,.25]:
                for phase in [0,.25,.5,.75]:
                    pts=line(speed,hz)
                    pts=[(t,round((x+phase*quantum)/quantum)*quantum,y) for t,x,y in pts]
                    r=estimate(pts)
                    if not r['ready']:continue
                    strict=r['strictFloatPairSelected']
                    quantized.append({'hz':hz,'speed':speed,'quantum':quantum,'phase':phase,'raw':r['raw'],'oldSelected':r['oldSelected'],'selectedWithA':r['selectedWithA'],'selectedWithAB':r['selectedWithAB'],'positiveIntervals':r['positiveIntervals'],'zeroLimiter':r['zeroLimitingInterval'],'strictFloatSelected':strict,'spatialSelected':r['spatialPairSelected']})
    assert plateau['selected']<15.1
    assert all(r['spatialSelected']>=15 for r in quantized if r['oldSelected']>=15)
    plateau_probe=[]
    for period in [.0021,.01]:
        for base in [0.,300.]:
            points=[(i*period,base*i*period,0.) for i in range(25)]
            t,x,y=points[-1]
            points[-1]=(t,x+100,0.)
            plateau_probe.append({'kind':'singlejump','periodMs':period*1000,'base':base,'result':estimate(points)})
            points.append((t+period,x+100,0.))
            plateau_probe.append({'kind':'jumpthenhold','periodMs':period*1000,'base':base,'result':estimate(points)})
    result={'chosenVariant':'withA','selectedField':'selectedWithA','legacySelectedField':'selected (dual spatial upper bounds without A)','notChosenVariant':'withAB','type':'offline Python rule study, not native execution','candidate':'raw50ms LSQ with both adjacent independent A-B and B-C projection upper bounds; existing reversalDistance spatial noise budget plus QPC/float allowance; preserve old latest-tail and output cap','cases':cases,'plateau':plateau,'plateauProbes':plateau_probe,'quantizedCases':quantized,'realTraceTailResults':real,'scriptSha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    (ROOT/'overspeed-quality-design-2026-11-14.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'syntheticCases':len(cases),'plateau':plateau,'quantizedCount':len(quantized),'newAFalseBelow15':sum(r['oldSelected']>=15 and r['selectedWithA']<15 for r in quantized),'newABFalseBelow15':sum(r['oldSelected']>=15 and r['selectedWithAB']<15 for r in quantized),'ABFalseExamples':[r for r in quantized if r['oldSelected']>=15 and r['selectedWithAB']<15][:5],'strictFloatZeroAbove15':sum(r['strictFloatSelected']<15 and r['speed']>=20 for r in quantized),'spatialZeroAbove15':sum(r['spatialSelected']<15 and r['speed']>=20 for r in quantized),'newSpatialFailures':sum(r['oldSelected']>=15 and r['spatialSelected']<15 for r in quantized),'real':real,'rampStepDecel':[(c['kind'],c['hz'],round(c['result'].get('raw',0),2),round(c['result']['selected'],2),c['result'].get('qualityLimited')) for c in cases if c['kind'] in ['ramp','step','last_step','decel']]},ensure_ascii=False))
if __name__=='__main__':run()
