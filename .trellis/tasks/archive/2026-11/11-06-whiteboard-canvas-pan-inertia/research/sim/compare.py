"""Side-by-side: CURRENT estimator vs candidate replacement estimators on identical synthetic streams.

python -I compare.py [N_TRIALS]
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # python -I drops the script dir
import math
import random

import navport as N
import streams as S
import drivers as D
import proposed as P
from navport import QPC_FREQ

SKIP = False
NTR = int(sys.argv[1]) if len(sys.argv) > 1 else 400
MODELS = (sys.argv[2].split(',') if len(sys.argv) > 2 else ['cur', 'lsq1w50', 'lsq2', 'impulse'])  # e.g. cur,lsq2,lsq2b6,avg21,lsq2+ring
ONLY = set(sys.argv[3].split(',')) if len(sys.argv) > 3 else None  # section numbers to run, e.g. 2,4,6


def trial_streams(profile_fn, dev, rng, two=None, fps=120.0, stall_p=0.0):
    origin = 1000.0 + rng.random()
    prof = profile_fn(rng)
    ev, vt, _ = S.make_events(prof, dev, rng, origin)
    evs = [ev]
    if two is not None:
        stagger = rng.uniform(*two) if isinstance(two, tuple) else two
        evb, _, _ = S.make_events(profile_fn(rng, extra=stagger / 1000.0), dev, rng, origin + 0.005)
        evs.append(evb)
    start = min(e[0].q for e in evs) / QPC_FREQ - rng.random() / fps
    fseed = rng.randrange(1 << 30)
    return evs, vt, start, fseed


def run_all(profile_fn, dev, n=NTR, two=None, fps=120.0, stall_p=0.0, seed=21):
    if SKIP:
        return {m: [(0, 0, 0, None, 0)] for m in MODELS}, 1.0
    rng = random.Random(seed)
    out = {m: [] for m in MODELS}
    vt = None
    for _ in range(n):
        evs, vt, start, fseed = trial_streams(profile_fn, dev, rng, two, fps, stall_p)
        mk = lambda: S.frame_clock(start, fps, random.Random(fseed), stall_p=stall_p)
        r = D.run_current(evs, mk())
        out['cur'].append((r['release_speed'], abs(r['glide']), r['peak_direct'], r['first_emit'], r['max_inertia_step']))
        for m in MODELS[1:]:
            q = P.run_proposed(evs, mk(), kind=m.replace('+ring', ''), all_events=m.endswith('+ring'))
            out[m].append((q['release_speed'], q['glide'], q['peak_est'], q['first_emit'], q['max_step']))
    return out, vt


def row(label, out, vt, cols=MODELS):
    if SKIP:
        return
    for m in cols:
        rel = [x[0] for x in out[m]]
        gl = [x[1] for x in out[m]]
        pk = [x[2] for x in out[m]]
        n = len(rel)
        if vt > 0:
            ideal = (vt * vt / (2 * N.DECEL)) if m == 'cur' else P.glide_distance(min(vt, P.V_MAX))
            r = [v / vt for v in rel]
            q = D.quantiles(r, (0.5, 0.9, 0.99, 1.0))
            g = D.quantiles([x / ideal for x in gl], (0.5, 0.99, 1.0))
            p15 = 100.0 * sum(1 for v in r if v > 1.5) / n
            p07 = 100.0 * sum(1 for v in r if v < 0.7) / n
            p0 = 100.0 * sum(1 for v in rel if v < 5.0) / n
            pkmax = max(pk) / vt
            print(f'| {label:<34} | {m:<8} | rel/true {q[0]:5.2f} {q[1]:5.2f} {q[2]:5.2f} {q[3]:5.2f} '
                  f'| >1.5x {p15:5.1f}% <0.7x {p07:5.1f}% none {p0:5.1f}% | glide/ideal {g[0]:5.2f} {g[1]:5.2f} {g[2]:6.2f} | peak/true max {pkmax:5.2f} |')
        else:
            q = D.quantiles(rel, (0.5, 0.9, 0.99, 1.0))
            g = D.quantiles(gl, (0.5, 0.99, 1.0))
            p300 = 100.0 * sum(1 for v in rel if v > 300) / n
            print(f'| {label:<34} | {m:<8} | rel v {q[0]:6.0f} {q[1]:6.0f} {q[2]:6.0f} {q[3]:6.0f} '
                  f'| P(rel>300) {p300:5.1f}% | glide DIP {g[0]:6.0f} {g[1]:6.0f} {g[2]:6.0f} |')


SKIP = False


def head(t):
    global SKIP
    SKIP = ONLY is not None and t.split('.')[0] not in ONLY
    if SKIP:
        return
    print('\n### ' + t)
    print('(rel/true quantiles: med p90 p99 max; glide/ideal: med p99 max; ideal = same model with perfect velocity)')


head('1. Steady swipe 3000 DIP/s')
for rate in (60, 120, 240):
    o, vt = run_all(lambda rng, extra=0.0: S.profile_steady(3000, total=0.25 + extra), S.Dev(rate=rate))
    row(f'steady 3000 @{rate}Hz', o, vt)

head('2. Flick with plateau at lift (flick_sin 4000, 120 ms)')
for rate in (60, 120, 240):
    o, vt = run_all(lambda rng, extra=0.0: S.profile_flick_sin(4000, 0.12 + extra), S.Dev(rate=rate))
    row(f'flick_sin 4000 @{rate}Hz', o, vt)

head('3. Accelerating flick (flick_lin 4000: 80 ms, SHORT 50 ms) @120 Hz')
o, vt = run_all(lambda rng, extra=0.0: S.profile_flick_lin(4000, 0.08 + extra), S.Dev(rate=120))
row('flick_lin 80ms', o, vt)
o, vt = run_all(lambda rng, extra=0.0: S.profile_flick_lin(4000, 0.05 + extra), S.Dev(rate=120, quant_ms=2.0))
row('flick_lin 50ms q=2ms', o, vt)

head('4. Delivery pathologies, flick_sin 4000')
o, vt = run_all(lambda rng, extra=0.0: S.profile_flick_sin(4000, 0.12 + extra), S.Dev(rate=120, burst_p=0.30))
row('burst_p=0.30 @120Hz', o, vt)
o, vt = run_all(lambda rng, extra=0.0: S.profile_flick_sin(4000, 0.12 + extra), S.Dev(rate=240, quant_ms=3.0, jitter_ms=0.3))
row('quant 3ms @240Hz', o, vt)
o, vt = run_all(lambda rng, extra=0.0: S.profile_flick_sin(4000, 0.12 + extra), S.Dev(rate=120), stall_p=0.05)
row('loop stalls 5%/frame (20-50ms)', o, vt)
for dur in (0.036, 0.060, 0.090):
    o, vt = run_all(lambda rng, extra=0.0, d=dur: S.profile_flick_lin(4000, d + extra), S.Dev(rate=120, tiny_first_dt=True))
    row(f'tiny first dt, flick_lin {int(dur * 1000)}ms', o, vt)

head('5. Delayed Up (finger was moving at lift): policy trade-off')
for lat in (20, 50, 95):
    o, vt = run_all(lambda rng, extra=0.0: S.profile_flick_sin(4000, 0.12 + extra), S.Dev(rate=120, lift_lat_ms=lat))
    row(f'Up delayed {lat} ms', o, vt)

head('6. Drag-and-place / stop-and-lift (true release speed = 0)')
for v, td, rest in ((3000, 100, 0), (3000, 100, 50), (3000, 100, 95), (6000, 100, 50), (1500, 100, 50)):
    o, vt = run_all(lambda rng, extra=0.0, v=v, td=td, rest=rest: S.profile_stop_lift(v, decel=td / 1000.0, rest=rest / 1000.0 + extra), S.Dev(rate=120))
    row(f'v{v} decel{td}ms rest{rest}ms', o, vt)

head('7. Two-finger flick, 2nd finger lifts U(0,60) ms after the 1st, steady 3000')
for rate in (60, 120, 240):
    o, vt = run_all(lambda rng, extra=0.0: S.profile_steady(3000, total=0.25 + extra), S.Dev(rate=rate), two=(0.0, 60.0), n=NTR * 2)
    row(f'2-finger U(0,60)ms @{rate}Hz', o, vt)

head('8. Mouse 1000 Hz, WM_MOUSEMOVE service interval 8/16 ms, flick_sin 4000')
for co in (0.0, 8.0, 16.0):
    dev = S.Dev(rate=1000, sigma_pos=0.0, base_lat_ms=0.5, jitter_ms=0.3, quant_ms=0.0, coalesce_ms=co)
    o, vt = run_all(lambda rng, extra=0.0: S.profile_flick_sin(4000, 0.12 + extra), dev)
    row(f'mouse coalesce {co:.0f}ms', o, vt)

head('9. Mouse already moving at press, Down+first Move back-to-back (0.15 ms); drag 16/24/36/60 ms @3000')
for dur in (0.016, 0.024, 0.036, 0.060):
    dev = S.Dev(rate=1000, sigma_pos=0.0, base_lat_ms=0.3, jitter_ms=0.2, quant_ms=0.0, coalesce_ms=8.0, tiny_first_dt=True)
    o, vt = run_all(lambda rng, extra=0.0, d=dur: S.profile_steady(3000.0, ramp=0.001, total=d + extra), dev)
    row(f'mouse press-moving drag {int(dur * 1000)}ms', o, vt)

head('10. Render-loop rate = StrokeTimingProfile target_fps 30/60/240 (the nav loop only samples once per frame)')
for fps in (30.0, 60.0, 240.0):
    o, vt = run_all(lambda rng, extra=0.0: S.profile_steady(3000, total=0.25 + extra), S.Dev(rate=120), fps=fps)
    row(f'steady 3000, loop {int(fps)} fps', o, vt)
    o, vt = run_all(lambda rng, extra=0.0: S.profile_flick_sin(4000, 0.12 + extra), S.Dev(rate=120), fps=fps)
    row(f'flick_sin 4000, loop {int(fps)} fps', o, vt)
    o, vt = run_all(lambda rng, extra=0.0: S.profile_steady(3000, total=0.25 + extra), S.Dev(rate=120), two=(0.0, 60.0), fps=fps, n=NTR * 2)
    row(f'2-finger U(0,60), loop {int(fps)} fps', o, vt)
