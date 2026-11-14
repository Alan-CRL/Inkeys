"""Hypothesis scenarios against the CURRENT Draw3 estimator/inertia (exact port).

python -I run_current.py [N_TRIALS]

Every row = Monte-Carlo over N random phase/noise/jitter draws.
ratio columns are measured / true-finger-speed-at-lift (1.00 = perfect).
glide ratio = simulated glide distance / (v_true^2 / (2*6000)) i.e. what the same model
would do with a perfect velocity.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # python -I drops the script dir
import math
import random

import navport as N
import streams as S
import drivers as D
from navport import QPC_FREQ

NTR = int(sys.argv[1]) if len(sys.argv) > 1 else 400


def pct(x):
    return f'{100 * x:5.1f}%'


def mc(profile_fn, dev, n=NTR, fps=120.0, stall_p=0.0, seed=7, two=None):
    """two=(stagger_ms, offset_ms) -> two-finger run (second finger down at offset, lifts stagger later)."""
    rng = random.Random(seed)
    rows = []
    for _ in range(n):
        origin = 1000.0 + rng.random()
        prof = profile_fn(rng)
        ev, vt, lift = S.make_events(prof, dev, rng, t_origin_s=origin)
        evs = [ev]
        if two:
            stagger, off = two
            prof_b = profile_fn(rng, extra=stagger / 1000.0)
            evb, _, _ = S.make_events(prof_b, dev, rng, t_origin_s=origin + off / 1000.0)
            evs.append(evb)
        start = min(e[0].q for e in evs) / QPC_FREQ - rng.random() / fps
        r = D.run_current(evs, S.frame_clock(start, fps, rng, stall_p=stall_p))
        r['v_true'] = vt
        rows.append(r)
    return rows


def summarize(rows, label, v_true_zero=False):
    n = len(rows)
    vt = rows[0]['v_true']
    rel = [r['release_speed'] for r in rows]
    peak = [r['peak_direct'] for r in rows]
    fe = [r['first_emit'] for r in rows if r['first_emit'] is not None]
    gl = [abs(r['glide']) for r in rows]
    lost = sum(1 for r in rows if r['release_speed'] < 5.0) / n
    if v_true_zero or vt <= 0:
        q = D.quantiles(rel)
        qg = D.quantiles(gl)
        print(f'| {label:<46} | rel v med/p95/p99/max = {q[0]:7.0f} {q[2]:7.0f} {q[3]:7.0f} {q[4]:7.0f} | '
              f'glide DIP med/p99/max = {qg[0]:6.0f} {qg[3]:6.0f} {qg[4]:6.0f} | P(rel>300)={pct(sum(1 for v in rel if v > 300) / n)} |')
        return
    ideal = vt * vt / (2 * N.DECEL)
    qr = D.quantiles([v / vt for v in rel])
    qp = D.quantiles([v / vt for v in peak])
    qg = D.quantiles([g / ideal for g in gl])
    p15 = sum(1 for v in rel if v > 1.5 * vt) / n
    p2 = sum(1 for v in rel if v > 2.0 * vt) / n
    p07 = sum(1 for v in rel if 5.0 <= v < 0.7 * vt) / n
    fe_q = D.quantiles([v / vt for v in fe]) if fe else [float('nan')] * 5
    print(f'| {label:<46} | rel/true med {qr[0]:5.2f} p95 {qr[2]:5.2f} p99 {qr[3]:5.2f} max {qr[4]:5.2f} '
          f'| P>1.5x {pct(p15)} P>2x {pct(p2)} P<0.7x {pct(p07)} lost {pct(lost)} '
          f'| peak/true max {qp[4]:5.2f} | 1st-emit/true med {fe_q[0]:5.2f} p99 {fe_q[3]:5.2f} max {fe_q[4]:6.2f} '
          f'| glide/ideal med {qg[0]:5.2f} p99 {qg[3]:5.2f} max {qg[4]:6.2f} |')


def head(t):
    print('\n### ' + t)


# ------------------------------------------------------------------------------------------------
head('A. Baseline: steady swipe (40 ms ramp, 250 ms total) -- rate x speed, jitter 0.7 ms, 1 ms quant, 120 fps loop')
for rate in (60, 120, 240):
    for v in (1500, 3000, 6000, 12000):
        dev = S.Dev(rate=rate)
        summarize(mc(lambda rng, extra=0.0, v=v: S.profile_steady(v, total=0.25 + extra), dev),
                  f'steady {v} DIP/s @ {rate} Hz')

head('A2. Typical flick: velocity plateau at lift (flick_sin, 120 ms) and accelerating flick (flick_lin 80 ms)')
for rate in (60, 120, 240):
    dev = S.Dev(rate=rate)
    summarize(mc(lambda rng, extra=0.0: S.profile_flick_sin(4000, 0.12 + extra), dev), f'flick_sin 4000 @ {rate} Hz')
    summarize(mc(lambda rng, extra=0.0: S.profile_flick_lin(4000, 0.08 + extra), dev), f'flick_lin 4000 (accel at lift) @ {rate} Hz')

head('B. Timestamp quantization (arrival QPC rounded up to q ms), flick_sin 4000 / flick_lin 4000, 240 Hz and 120 Hz')
for rate in (240, 120):
    for q in (0.0, 1.0, 2.0, 3.0):
        dev = S.Dev(rate=rate, quant_ms=q, jitter_ms=0.3)
        summarize(mc(lambda rng, extra=0.0: S.profile_flick_sin(4000, 0.12 + extra), dev), f'flick_sin q={q:.0f}ms @ {rate} Hz')
        summarize(mc(lambda rng, extra=0.0: S.profile_flick_lin(4000, 0.05 + extra), dev), f'SHORT flick_lin 50ms q={q:.0f}ms @ {rate} Hz')

head('C. Bursty delivery (hold 2-4 reports then deliver back-to-back 80 us apart), flick_sin 4000')
for rate in (120, 240):
    for bp in (0.0, 0.05, 0.15, 0.30):
        dev = S.Dev(rate=rate, burst_p=bp)
        summarize(mc(lambda rng, extra=0.0: S.profile_flick_sin(4000, 0.12 + extra), dev), f'burst_p={bp:.2f} @ {rate} Hz')

head('D. Tiny first-Move dt (Down callback delayed behind first Move: dt=0.15 ms) -- short flicks of 24-60 ms')
for dur in (0.024, 0.036, 0.048, 0.060, 0.090):
    dev = S.Dev(rate=120, tiny_first_dt=True)
    summarize(mc(lambda rng, extra=0.0, d=dur: S.profile_flick_lin(4000, d + extra), dev), f'tiny-first-dt, flick_lin {int(dur * 1000)} ms @ 120 Hz')
for dur in (0.024, 0.036, 0.048, 0.060, 0.090):
    dev = S.Dev(rate=120, tiny_first_dt=False)
    summarize(mc(lambda rng, extra=0.0, d=dur: S.profile_flick_lin(4000, d + extra), dev), f'normal first-dt, flick_lin {int(dur * 1000)} ms @ 120 Hz')

head('E. Delayed Up (panel lift-off latency): Up arrives N ms after last Move, flick_sin 4000 @120 Hz')
for lat in (0, 20, 40, 60, 80, 95, 105, 140):
    dev = S.Dev(rate=120, lift_lat_ms=lat)
    summarize(mc(lambda rng, extra=0.0: S.profile_flick_sin(4000, 0.12 + extra), dev), f'Up delayed {lat} ms')

head('F. Stop-and-lift: cruise 3000 DIP/s, decelerate over 100 ms, rest N ms, lift (true release speed = 0)')
for rest in (0, 20, 40, 60, 80, 95):
    dev = S.Dev(rate=120)
    summarize(mc(lambda rng, extra=0.0, r=rest: S.profile_stop_lift(3000, rest=r / 1000.0 + extra), dev, n=NTR),
              f'rest {rest} ms before lift', v_true_zero=True)

head('G. Two fingers, second finger lifts `stagger` ms after the first (steady 3000 DIP/s, 120 Hz; 2nd Down +5 ms)')
for stagger in (0, 8, 16, 25, 40, 70):
    dev = S.Dev(rate=120)
    summarize(mc(lambda rng, extra=0.0: S.profile_steady(3000, total=0.25 + extra), dev, two=(stagger, 5.0)),
              f'2-finger stagger {stagger} ms')

head('H. Mouse: WM_MOUSEMOVE service interval (coalescing) x device rate; steady 3000 and flick_sin 4000')
for rate in (125, 500, 1000):
    for co in (0.0, 8.0, 16.0):
        dev = S.Dev(rate=rate, sigma_pos=0.0, base_lat_ms=0.5, jitter_ms=0.3, quant_ms=0.0, coalesce_ms=co)
        summarize(mc(lambda rng, extra=0.0: S.profile_flick_sin(4000, 0.12 + extra), dev), f'mouse {rate} Hz coalesce {co:.0f} ms flick')

head('I. Frame stalls during inertia (stall_p=2%/frame, 20-50 ms): max single-frame displacement vs 120 fps ideal')
for v in (3000, 6000, 12000):
    dev = S.Dev(rate=120)
    rows = mc(lambda rng, extra=0.0, v=v: S.profile_steady(v, total=0.25 + extra), dev, stall_p=0.02)
    steps = [r['max_inertia_step'] for r in rows]
    q = D.quantiles(steps)
    nominal = v / 120.0
    print(f'| steady {v:5d} | max per-frame inertia step: med {q[0]:6.1f} p99 {q[3]:6.1f} max {q[4]:6.1f} DIP '
          f'(nominal 1 frame = {nominal:5.1f} DIP, 50 ms cap = {v * 0.05:6.1f} DIP) |')
