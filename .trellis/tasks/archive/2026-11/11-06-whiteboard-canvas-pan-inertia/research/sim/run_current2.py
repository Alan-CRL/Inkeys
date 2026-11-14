"""Deeper Monte-Carlo on the strongest spike candidates found by run_current.py.

python -I run_current2.py [N_TRIALS]
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # python -I drops the script dir
import math
import random

import navport as N
import streams as S
import drivers as D
from navport import QPC_FREQ

NTR = int(sys.argv[1]) if len(sys.argv) > 1 else 1500


def frac(rows, pred):
    return 100.0 * sum(1 for r in rows if pred(r)) / len(rows)


def two_finger(rate, v=3000.0, stagger_range=(0.0, 60.0), n=NTR, seed=11, fps=120.0):
    rng = random.Random(seed)
    rows = []
    dev = S.Dev(rate=rate)
    for _ in range(n):
        stagger = rng.uniform(*stagger_range)
        origin = 1000.0 + rng.random()
        ea, vt, _ = S.make_events(S.profile_steady(v, total=0.25), dev, rng, origin)
        eb, _, _ = S.make_events(S.profile_steady(v, total=0.25 + stagger / 1000.0), dev, rng, origin + 0.005)
        start = ea[0].q / QPC_FREQ - rng.random() / fps
        r = D.run_current([ea, eb], S.frame_clock(start, fps, rng))
        r['stagger'] = stagger
        r['v_true'] = vt
        rows.append(r)
    return rows


print('### G2. Two-finger flick, 2nd finger lifts U(0,60) ms after 1st; steady 3000 DIP/s; N=%d per rate' % NTR)
print('| rate | rel/true med p90 p99 max | P(rel>1.5x) | P(rel>2x) | P(rel>3x) | glide/ideal p90 p99 max | P(glide>4x ideal) |')
for rate in (60, 120, 240):
    rows = two_finger(rate)
    vt = 3000.0
    ideal = vt * vt / (2 * N.DECEL)
    rr = [r['release_speed'] / vt for r in rows]
    gg = [abs(r['glide']) / ideal for r in rows]
    q = D.quantiles(rr, (0.5, 0.9, 0.99, 1.0))
    qg = D.quantiles(gg, (0.9, 0.99, 1.0))
    print(f'| {rate:3d} Hz | {q[0]:.2f} {q[1]:.2f} {q[2]:.2f} {q[3]:.2f} | {frac(rows, lambda r: r["release_speed"] > 1.5 * vt):4.1f}% '
          f'| {frac(rows, lambda r: r["release_speed"] > 2 * vt):4.1f}% | {frac(rows, lambda r: r["release_speed"] > 3 * vt):4.1f}% '
          f'| {qg[0]:.2f} {qg[1]:.2f} {qg[2]:.2f} | {frac(rows, lambda r: abs(r["glide"]) > 4 * ideal):4.1f}% |')

print('\n### G2b. by stagger bucket @120 Hz (release/true; glide/ideal)')
rows = two_finger(120, n=4000)
vt = 3000.0
ideal = vt * vt / (2 * N.DECEL)
print('| stagger ms | N | rel/true med p90 p99 max | P(rel>2x) | glide/ideal p99 max |')
for lo, hi in ((0, 5), (5, 10), (10, 15), (15, 20), (20, 30), (30, 45), (45, 60)):
    b = [r for r in rows if lo <= r['stagger'] < hi]
    rr = [r['release_speed'] / vt for r in b]
    gg = [abs(r['glide']) / ideal for r in b]
    q = D.quantiles(rr, (0.5, 0.9, 0.99, 1.0))
    qg = D.quantiles(gg, (0.99, 1.0))
    print(f'| {lo:2d}-{hi:2d} | {len(b):4d} | {q[0]:.2f} {q[1]:.2f} {q[2]:.2f} {q[3]:.2f} | {frac(b, lambda r: r["release_speed"] > 2 * vt):4.1f}% | {qg[0]:.2f} {qg[1]:.2f} |')

# ----------------------------------------------------------------------------------------------
print('\n### M1. Mouse already moving at button press (3000 DIP/s, no ramp), thread stalled so Down+first Move dispatch back-to-back (0.15 ms)')
print('| drag duration | first-emit/true med p99 max | release/true med p99 max | P(rel>2x) | P(rel>=clamp 24000) | glide DIP p99 max |')
for dur in (0.016, 0.024, 0.036, 0.060, 0.100):
    rng = random.Random(5)
    dev = S.Dev(rate=1000, sigma_pos=0.0, base_lat_ms=0.3, jitter_ms=0.2, quant_ms=0.0, coalesce_ms=8.0, tiny_first_dt=True)
    rows = []
    for _ in range(NTR):
        ev, vt, _ = S.make_events(S.profile_steady(3000.0, ramp=0.001, total=dur), dev, rng, 1000.0 + rng.random())
        r = D.run_current([ev], S.frame_clock(ev[0].q / QPC_FREQ - rng.random() / 120, 120.0, rng))
        rows.append(r)
    fe = [r['first_emit'] / 3000.0 for r in rows if r['first_emit'] is not None]
    rl = [r['release_speed'] / 3000.0 for r in rows]
    gl = [abs(r['glide']) for r in rows]
    qf = D.quantiles(fe, (0.5, 0.99, 1.0))
    qr = D.quantiles(rl, (0.5, 0.99, 1.0))
    qg = D.quantiles(gl, (0.99, 1.0))
    print(f'| {int(dur * 1000):3d} ms | {qf[0]:.2f} {qf[1]:.2f} {qf[2]:.2f} | {qr[0]:.2f} {qr[1]:.2f} {qr[2]:.2f} '
          f'| {frac(rows, lambda r: r["release_speed"] > 6000):4.1f}% | {frac(rows, lambda r: r["release_speed"] >= 23999):4.1f}% | {qg[0]:.0f} {qg[1]:.0f} |')

# ----------------------------------------------------------------------------------------------
print('\n### F2. Drag-and-place: cruise v, decelerate to rest over T_d, rest R ms, lift.  True release speed = 0.  Phantom inertia from the current model')
print('| cruise | T_d | rest | release v med / p99 | phantom glide DIP med / p99 | P(glide > 50 DIP) |')
for v in (1500, 3000, 6000):
    for td in (60, 100, 200):
        for rest in (0, 50, 95):
            rng = random.Random(9)
            dev = S.Dev(rate=120)
            rows = []
            for _ in range(300):
                ev, vt, _ = S.make_events(S.profile_stop_lift(v, decel=td / 1000.0, rest=rest / 1000.0), dev, rng, 1000.0 + rng.random())
                rows.append(D.run_current([ev], S.frame_clock(ev[0].q / QPC_FREQ - rng.random() / 120, 120.0, rng)))
            qr = D.quantiles([r['release_speed'] for r in rows], (0.5, 0.99))
            qg = D.quantiles([abs(r['glide']) for r in rows], (0.5, 0.99))
            print(f'| {v:5d} | {td:3d} | {rest:3d} | {qr[0]:6.0f} / {qr[1]:6.0f} | {qg[0]:6.0f} / {qg[1]:6.0f} | {frac(rows, lambda r: abs(r["glide"]) > 50):5.1f}% |')

# ----------------------------------------------------------------------------------------------
print('\n### Q. Distance is quadratic in velocity: glide = v^2 / (2*6000)')
print('| v DIP/s | glide DIP | glide in 1080p screen widths(1920) | 4K@150% (2560 DIP) widths | time to stop s |')
for v in (1500, 3000, 4500, 6000, 9000, 12000, 18000, 24000):
    g = v * v / (2 * N.DECEL)
    print(f'| {v:6d} | {g:8.0f} | {g / 1920:5.2f} | {g / 2560:5.2f} | {v / N.DECEL:4.2f} |')
print('error amplification: v_est/v_true = 1.5 -> distance x2.25 ; 2 -> x4 ; 2.77 -> x7.7 ; 3 -> x9')

print('\n### P. Pen hover brake: 12000 DIP/s^2 is not an immediate stop')
print('| speed at hover | stop time ms | extra glide DIP |')
for v in (1500, 3000, 6000, 12000):
    print(f'| {v:5d} | {v / N.PEN_DECEL * 1000:6.0f} | {v * v / (2 * N.PEN_DECEL):6.0f} |')
