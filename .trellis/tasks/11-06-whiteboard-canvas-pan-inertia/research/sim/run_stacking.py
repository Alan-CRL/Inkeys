"""Stacking (fling boost) study: current Draw3 behaviour vs Chromium-style boost on the proposed model.

python -I run_stacking.py

Timeline (all times from the release of swipe #1):
   swipe #1  : steady v1 for 250 ms (40 ms ramp), released at t=0, glide starts
   swipe #2  : pointer goes DOWN at t=gap (glide may still be running), steady v2 for 250 ms, released
Outputs: velocity used to start glide #2, glide #2 distance, total displacement of the viewport content.
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


def mirror(events):
    for e in events:
        e.x = -e.x
    return events


def make_pair(v1, v2, gap_ms, rng, rate=120.0, sign2=+1):
    dev = S.Dev(rate=rate)
    origin = 1000.0
    e1, _, lift1 = S.make_events(S.profile_steady(v1, total=0.25), dev, rng, origin)
    # swipe #1 releases at its Up arrival; swipe #2 starts gap_ms after that
    up1 = (e1[-1].q) / QPC_FREQ
    e2, _, _ = S.make_events(S.profile_steady(abs(v2), total=0.25), dev, rng, up1 + gap_ms / 1000.0)
    if sign2 < 0:
        mirror(e2)
    return e1, e2


# ---------------------------------------------------------------------------------------------
def current_two_finger_catch(v1, v2, gap_ms, seed=1, sign2=+1):
    """Existing behaviour when the second gesture is a (two-finger) Pan that catches the glide:
    BeginCanvasPan(inherit=true) -> first positional Move cuts the residual -> release = new direct only."""
    rng = random.Random(seed)
    e1, e2 = make_pair(v1, v2, gap_ms, rng, sign2=sign2)
    f0 = e1[0].q / QPC_FREQ - 0.002
    frames = S.frame_clock(f0, 120.0, random.Random(5))
    r1 = D.run_current([e1], frames, until_q=e2[0].q - 1)
    m = r1['m']
    residual = m.speed()
    # continue the same frame clock until e2's Down is dequeued
    frames2 = S.frame_clock(e2[0].q / QPC_FREQ - 0.001, 120.0, random.Random(6))
    r2 = D.run_current([e2], frames2, motion=m, inherit=True)
    v_start = r2['release_speed'] * (1 if sign2 > 0 else -1)
    return dict(v1_release=r1['release_speed'], residual_at_catch=residual, v2_measured=abs(r2['selected_speed']),
                v2_start=v_start, release_src=r2['release_src'],
                glide1_before_catch=r1['glide'], pan1=r1['pan_dist'], pan2=r2['pan_dist'], glide2=r2['glide'],
                total=r1['pan_dist'] + r1['glide'] + r2['pan_dist'] + r2['glide'])


def current_single_finger_candidate(v1, gap_ms):
    """Existing behaviour when a SINGLE finger lands during glide: PanCandidate -> no pan, no velocity.
    Glide continues at 6000, then after 180 ms the candidate times out and brakes at 12000."""
    m = N.Motion()
    m.velocity = (v1, 0.0)
    m.inertia_active = True
    d = 0.0
    t = 0.0
    dt = 1 / 240
    while t < gap_ms / 1000.0 and m.inertia_active:     # glide until the finger lands
        d += N.step_inertia(m, dt, False)[0]
        t += dt
    d_before = d
    t = 0.0
    while t < 0.180 and m.inertia_active:               # candidate window: ordinary decel
        d += N.step_inertia(m, dt, False)[0]
        t += dt
    while m.inertia_active:                             # InertiaBrakeRequested -> 12000
        d += N.step_inertia(m, dt, True)[0]
    return d_before, d - d_before


# ---------------------------------------------------------------------------------------------
class SeqState:
    def __init__(self, add_residual=False, boost=True):
        self.boost = P.Booster(add_residual=add_residual)
        self.use_boost = boost


def proposed_sequence(v1, v2, gap_ms, boost=True, add_residual=False, seed=1, sign2=+1, kind='lsq2'):
    rng = random.Random(seed)
    e1, e2 = make_pair(v1, v2, gap_ms, rng, sign2=sign2)
    bo = P.Booster(add_residual=add_residual)
    dt_frame = 1 / 120.0
    t_frame = e1[0].q / QPC_FREQ - 0.002
    contacts = [dict(ev=e1), dict(ev=e2)]
    glide_v = 0.0                    # signed
    glide_total = [0.0, 0.0]
    pan_total = [0.0, 0.0]
    phase = 0
    tr = P.Tracker(kind)
    idx = [0, 0]
    last_pos = None
    started_v = [0.0, 0.0]
    residual_at_catch = 0.0
    last_seq = 0
    down_done = False
    while True:
        T = int(round(t_frame * QPC_FREQ))
        t_s = T / QPC_FREQ
        ev = contacts[phase]['ev'] if phase < 2 else []
        # glide advance (between swipes / after the last)
        if abs(glide_v) > 0:
            d, sp = P.inertia_step(abs(glide_v), dt_frame)
            sgn = 1 if glide_v > 0 else -1
            gi = 0 if (phase == 0 or (phase == 1 and not down_done)) else 1
            glide_total[gi] += sgn * d
            glide_v = sgn * sp
        # event processing for current phase
        while phase < 2 and idx[phase] < len(ev) and ev[idx[phase]].q <= T:
            e = ev[idx[phase]]
            idx[phase] += 1
            if e.kind == 'down':
                tr.reset()
                tr.add(e.q / QPC_FREQ, e.x)
                last_pos = e.x
                if phase == 1:
                    residual_at_catch = glide_v
                    bo.on_grab(e.q / QPC_FREQ, glide_v, abs(glide_v) > 0)
                    glide_v = 0.0            # content now follows the pointer
                down_done = True
            elif e.kind == 'move':
                tr.add(e.q / QPC_FREQ, e.x)
                dx = e.x - last_pos
                pan_total[phase] += dx
                last_pos = e.x
                if phase == 1:
                    bo.on_move(e.q / QPC_FREQ, tr.estimate(), dx)
            else:
                pan_total[phase] += e.x - last_pos
                v = tr.estimate()
                age = e.q / QPC_FREQ - (tr.last_t() or -1e9)
                v = (v or 0.0) * P.stop_gate(age)
                v = math.copysign(min(abs(v), P.V_MAX), v)
                if phase == 1 and boost:
                    v = bo.on_release(e.q / QPC_FREQ, v)
                elif phase == 0:
                    bo.on_fling_start(v)
                    started_v[0] = v
                else:
                    bo.on_fling_start(v)
                if phase == 1:
                    started_v[1] = v
                glide_v = v if abs(v) >= P.STOP_SPEED else 0.0
                if phase == 0:
                    phase = 1
                    down_done = False
                    idx[1] = 0
                else:
                    phase = 2
                break   # `ev` belongs to the old phase: re-bind on the next loop pass
        if phase == 2 and abs(glide_v) == 0:
            break
        t_frame += dt_frame
        if t_frame > e1[0].q / QPC_FREQ + 12.0:
            break
    return dict(v1_start=started_v[0], residual_at_catch=residual_at_catch, v2_start=started_v[1],
                glide1=glide_total[0], pan1=pan_total[0], pan2=pan_total[1], glide2=glide_total[1],
                total=pan_total[0] + pan_total[1] + glide_total[0] + glide_total[1])


# ---------------------------------------------------------------------------------------------
if __name__ == '__main__':
    print('### Current Draw3, SAME direction, v1=v2=4000 DIP/s (two-finger catch path = BeginCanvasPan(inherit=true))')
    print('| gap after release | residual at catch | measured v2 | v2 used for glide | release_src | total DIP (pan1+glide1+pan2+glide2) | glide2 DIP | "stack" = glide2 vs single-swipe glide (1333) |')
    for gap in (80, 150, 300, 500):
        r = current_two_finger_catch(4000, 4000, gap)
        print(f'| {gap:4d} ms | {r["residual_at_catch"]:7.0f} | {r["v2_measured"]:7.0f} | {r["v2_start"]:7.0f} | {r["release_src"]:<9} | {r["total"]:7.0f} | {r["glide2"]:7.0f} | {r["glide2"] / 1333.0:4.2f}x |')

    print('\n### Current Draw3, SINGLE finger lands during glide (PanCandidate path): swipe ignored, glide continues then 12000 brake')
    print('| gap | glide before landing DIP | extra old glide after landing DIP | contribution of the new swipe |')
    for gap in (80, 150, 300, 500):
        a, b = current_single_finger_candidate(4000, gap)
        print(f'| {gap:4d} ms | {a:6.0f} | {b:6.0f} | 0 (no velocity sample is ever taken: contact disposition=PanCandidate) |')

    print('\n### Proposed (LSQ2 + exp/friction glide tau=0.30 c=1000 + Chromium FlingBooster), SAME direction v1=v2=4000')
    print('| gap | residual at catch | v1 start | v2 start (no boost) | glide2 no-boost | v2 start (boost: +prev start) | glide2 boosted | total no-boost | total boosted | v2 start (boost residual) | total boosted-residual |')
    for gap in (80, 150, 300, 500):
        nb = proposed_sequence(4000, 4000, gap, boost=False)
        bo = proposed_sequence(4000, 4000, gap, boost=True)
        br = proposed_sequence(4000, 4000, gap, boost=True, add_residual=True)
        print(f'| {gap:4d} ms | {bo["residual_at_catch"]:6.0f} | {bo["v1_start"]:6.0f} | {nb["v2_start"]:6.0f} | {nb["glide2"]:6.0f} | {bo["v2_start"]:6.0f} | {bo["glide2"]:6.0f} '
              f'| {nb["total"]:6.0f} | {bo["total"]:6.0f} | {br["v2_start"]:6.0f} | {br["total"]:6.0f} |')

    print('\n### Proposed, OPPOSITE direction (swipe #2 mirrored): booster must reset, v2 independent')
    for gap in (80, 150, 300):
        bo = proposed_sequence(4000, 4000, gap, boost=True, sign2=-1)
        print(f'| gap {gap:4d} ms | residual {bo["residual_at_catch"]:6.0f} | v2 start {bo["v2_start"]:7.0f} | glide2 {bo["glide2"]:7.0f} |')

    print('\n### Proposed, third swipe chain / cap: v=4000 repeated flicks (each 150 ms after the previous release)')
    # simple analytic chain using the booster rules (start velocity += 4000 each time, cap 18000)
    v = 0.0
    bo = P.Booster()
    for i in range(1, 8):
        v_new = 4000.0
        if i == 1:
            v = v_new
            bo.on_fling_start(v)
            print(f'| flick {i} | start {v:6.0f} | glide {P.glide_distance(v):6.0f} DIP |')
            continue
        res = P.glide_distance  # placeholder to keep flake quiet
        # residual 150 ms after release for this curve:
        A = v + P.FRICTION * P.TAU_S
        resid = A * math.exp(-0.150 / P.TAU_S) - P.FRICTION * P.TAU_S
        bo.on_grab(0.0, resid, True)
        bo.on_move(0.01, 4000.0, 10.0)
        v = bo.on_release(0.02, v_new)
        print(f'| flick {i} | residual {resid:6.0f} | start {v:6.0f} | glide {P.glide_distance(v):6.0f} DIP |')
