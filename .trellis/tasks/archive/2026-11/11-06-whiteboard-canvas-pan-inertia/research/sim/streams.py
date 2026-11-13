"""Synthetic touch/mouse stream generator for the canvas-pan inertia study.

Models the path  finger -> digitizer report -> RTS/WM_MOUSEMOVE callback arrival QPC
-> single-slot mailbox (ContactSnapshot, latest wins) -> render-loop consumption.

Why arrival time matters: Draw3.RealtimeStylus.cpp:1029/1389/1434 stamps every snapshot with
QueryQpc() *inside the RTS callback* (arrival time), never with a digitizer/HID timestamp, and
Draw3.WindowControl.cpp:484/502/1818 does the same for mouse.  So every jitter/burst/quantization
of the delivery path is injected 1:1 into the velocity estimator's time axis.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # python -I drops the script dir

import math
import random

from navport import QPC_FREQ


# ----------------------------------------------------------------------------------------------
# True finger trajectories (1-D along +x).  Each returns (pos(t), vel(t), lift_t, v_true_release)
# ----------------------------------------------------------------------------------------------
def profile_steady(v, ramp=0.040, total=0.250):
    def pos(t):
        t = min(t, total)
        return v * t * t / (2 * ramp) if t <= ramp else v * (t - ramp / 2)

    def vel(t):
        return v * min(1.0, t / ramp)
    return pos, vel, total, v


def profile_flick_lin(v_rel, dur):
    """Constant acceleration, finger leaves while still accelerating (worst case for lag)."""
    def pos(t):
        t = min(t, dur)
        return v_rel * t * t / (2 * dur)

    def vel(t):
        return v_rel * min(1.0, t / dur)
    return pos, vel, dur, v_rel


def profile_flick_sin(v_rel, dur):
    """Fast attack, velocity plateau at lift (typical swipe)."""
    def pos(t):
        t = min(t, dur)
        return v_rel * (2 * dur / math.pi) * (1 - math.cos(math.pi * t / (2 * dur)))

    def vel(t):
        t = min(t, dur)
        return v_rel * math.sin(math.pi * t / (2 * dur))
    return pos, vel, dur, v_rel


def profile_stop_lift(v, ramp=0.040, cruise=0.120, decel=0.100, rest=0.050):
    t1 = ramp
    t2 = ramp + cruise
    t3 = t2 + decel
    lift = t3 + rest

    def pos(t):
        t = min(t, lift)
        if t <= t1:
            return v * t * t / (2 * ramp)
        p1 = v * ramp / 2
        if t <= t2:
            return p1 + v * (t - t1)
        p2 = p1 + v * cruise
        if t <= t3:
            u = t - t2
            return p2 + v * u - v * u * u / (2 * decel)
        return p2 + v * decel / 2

    def vel(t):
        if t <= t1:
            return v * t / ramp
        if t <= t2:
            return v
        if t <= t3:
            return v * (1 - (t - t2) / decel)
        return 0.0
    return pos, vel, lift, 0.0


# ----------------------------------------------------------------------------------------------
# Delivery model
# ----------------------------------------------------------------------------------------------
class Dev:
    def __init__(self, name='touch120', rate=120.0, sigma_pos=0.15, base_lat_ms=2.0,
                 jitter_ms=0.7, burst_p=0.0, burst_len=(2, 4), quant_ms=1.0,
                 lift_lat_ms=0.0, tiny_first_dt=False, coalesce_ms=0.0):
        self.name = name
        self.rate = rate
        self.sigma_pos = sigma_pos
        self.base_lat_ms = base_lat_ms
        self.jitter_ms = jitter_ms
        self.burst_p = burst_p
        self.burst_len = burst_len
        self.quant_ms = quant_ms
        self.lift_lat_ms = lift_lat_ms
        self.tiny_first_dt = tiny_first_dt
        self.coalesce_ms = coalesce_ms

    def clone(self, **kw):
        d = Dev.__new__(Dev)
        d.__dict__.update(self.__dict__)
        d.__dict__.update(kw)
        return d


class Ev:
    __slots__ = ('kind', 'q', 'x', 'y', 'true_t', 'seq')

    def __init__(self, kind, q, x, y, true_t):
        self.kind = kind
        self.q = q
        self.x = x
        self.y = y
        self.true_t = true_t
        self.seq = 0


def make_events(profile, dev, rng, t_origin_s=0.0):
    """Returns (events, v_true_release, true_lift_t).  Event times are QPC ticks, origin shifted
    by t_origin_s so that QPC values look like a real machine (large, not starting at 0)."""
    pos, vel, lift_t, v_true = profile
    period = 1.0 / dev.rate
    reports = []  # (kind, true_t, x)
    last_true_x = pos(0.0)
    reports.append(('down', 0.0, pos(0.0) + rng.gauss(0, dev.sigma_pos)))
    k = 1
    while k * period < lift_t - 1e-9:
        t = k * period
        x = pos(t)
        if abs(x - last_true_x) >= 0.03:      # panels do not report a stationary finger
            reports.append(('move', t, x + rng.gauss(0, dev.sigma_pos)))
            last_true_x = x
        k += 1
    # last move exactly at lift if the finger was still moving
    if abs(pos(lift_t) - last_true_x) >= 0.03:
        reports.append(('move', lift_t, pos(lift_t) + rng.gauss(0, dev.sigma_pos)))
    up_t = lift_t + dev.lift_lat_ms / 1000.0
    reports.append(('up', up_t, reports[-1][2]))

    if dev.coalesce_ms > 0:
        reports = _coalesce(reports, dev, rng)

    # --- arrival times
    arr = []
    for (kind, t, x) in reports:
        a = t + dev.base_lat_ms / 1000.0 + abs(rng.gauss(0, dev.jitter_ms / 1000.0))
        arr.append(a)
    # bursts: hold a run of events and deliver them back-to-back (80 us apart)
    i = 1
    while i < len(arr) - 1:
        if dev.burst_p > 0 and rng.random() < dev.burst_p:
            L = rng.randint(*dev.burst_len)
            j = min(len(arr) - 1, i + L - 1)
            release = arr[j]
            for n, idx in enumerate(range(i, j + 1)):
                arr[idx] = release + n * 0.00008
            i = j + 1
        else:
            i += 1
    if dev.tiny_first_dt and len(arr) > 2:
        # Down callback delayed so the first Move arrives right behind it.
        arr[0] = arr[1] - 0.00015
    # monotonic + quantization (timer tick granularity of the delivering thread)
    q = dev.quant_ms / 1000.0
    last = -1.0
    out = []
    for (kind, t, x), a in zip(reports, arr):
        a = max(a, last + 0.00002)
        if q > 0:
            a = math.ceil(a / q - 1e-9) * q
        a = max(a, last)
        last = a
        out.append(Ev(kind, int(round((t_origin_s + a) * QPC_FREQ)), x, 0.0, t))
    # sequence numbers (ContactSnapshot::sequence increments per publish)
    for n, e in enumerate(out):
        e.seq = n + 1
    return out, v_true, lift_t


def _coalesce(reports, dev, rng):
    """Message-loop coalescing for WM_MOUSEMOVE: a busy window thread dispatches only the latest
    position once per service interval (down/up are never merged)."""
    P = dev.coalesce_ms / 1000.0
    out = [reports[0]]
    bucket_end = reports[0][1] + P * rng.random()
    cur = None
    for (kind, t, x) in reports[1:-1]:
        while t > bucket_end:
            if cur is not None:
                out.append(('move', bucket_end, cur))
                cur = None
            bucket_end += P
        cur = x
    if cur is not None:
        out.append(('move', bucket_end, cur))
    out.append(reports[-1])
    return out


# ----------------------------------------------------------------------------------------------
# Render-loop frame clock (Draw3.DrawingController.cpp:18572-18582 WaitForFrameDeadline)
# ----------------------------------------------------------------------------------------------
def frame_clock(start_s, fps, rng, jitter_ms=0.3, stall_p=0.0, stall_ms=(20.0, 50.0)):
    dt = 1.0 / fps
    t = start_s
    while True:
        yield int(round(t * QPC_FREQ))
        t += dt + abs(rng.gauss(0, jitter_ms / 1000.0)) * 0.0 + rng.gauss(0, jitter_ms / 1000.0)
        if stall_p > 0 and rng.random() < stall_p:
            t += rng.uniform(*stall_ms) / 1000.0
