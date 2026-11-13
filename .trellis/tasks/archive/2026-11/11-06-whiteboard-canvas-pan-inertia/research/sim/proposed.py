"""Candidate replacement model for canvas-pan velocity / inertia / fling-boost (research prototype).

NOT product code.  Pure python so that the numbers in inertia-root-cause.md can be reproduced
and so the C++ unit tests can copy the asserted constants.

Pieces
  * Tracker      : per-pointer sample ring + window rules + estimator variants
                   ('lsq1' (current-like), 'lsq1w50', 'lsq2', 'impulse')
  * stop_gate    : soft release-age gate (replaces the 100 ms binary cliff)
  * Inertia      : closed-form exponential + coulomb friction, frame-rate independent
  * Booster      : Chromium FlingBooster style stacking
  * run_proposed : drives trackers with the same mailbox/frame model as drivers.run_current
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # python -I drops the script dir
import math

from navport import QPC_FREQ

# ---------------------------------------------------------------------------------------------
# Parameters (all candidates for C++ constants)
# ---------------------------------------------------------------------------------------------
HORIZON_S = 0.100          # Android/Compose VelocityTracker horizon
GAP_BREAK_S = 0.040        # Android ASSUME_POINTER_STOPPED_TIME: a >40 ms gap ends the run
MIN_GAP_S = 0.002          # samples closer than this are merged (newer wins)
MIN_SPAN_S = 0.020         # need >= 20 ms of history before any velocity is emitted
MIN_SAMPLES = 3
V_MAX = 12000.0            # DIP/s (3.2 m/s at 96 DIP/in); current code uses 24000
STOP_FULL_S = 0.030        # release age <= 30 ms: full velocity
STOP_ZERO_S = 0.070        # release age >= 70 ms: zero velocity (linear in between)
TAU_S = 0.30               # exponential time constant of the glide
FRICTION = 1000.0          # DIP/s^2 coulomb term -> finite stop time
STOP_SPEED = 20.0          # DIP/s
# fling boost (Chromium ui/events/blink/fling_booster.cc)
BOOST_MIN_FLING = 350.0    # kMinBoostFlingSpeedSquare = 350^2   (px/s)
BOOST_MIN_SCROLL = 150.0   # kMinBoostTouchScrollSpeedSquare = 150^2
BOOST_TIMEOUT_S = 0.050    # kFlingBoostTimeoutDelay
BOOST_MAX = 18000.0        # cap after boosting (1.5 x V_MAX)


def clamp_vec(vx, vy, vmax=V_MAX):
    s = math.hypot(vx, vy)
    if not math.isfinite(s):
        return 0.0, 0.0
    if s > vmax:
        k = vmax / s
        return vx * k, vy * k
    return vx, vy


# ---------------------------------------------------------------------------------------------
# Velocity trackers
# ---------------------------------------------------------------------------------------------
class Tracker:
    def __init__(self, kind='lsq2', min_gap=MIN_GAP_S, horizon=HORIZON_S, gap_break=GAP_BREAK_S,
                 min_span=MIN_SPAN_S, min_samples=MIN_SAMPLES):
        self.kind = kind
        self.s = []                       # (t_seconds, x)
        self.min_gap = min_gap
        self.horizon = horizon
        self.gap_break = gap_break
        self.min_span = min_span
        self.min_samples = min_samples

    def reset(self):
        self.s = []

    def add(self, t, x, gap_allow=None):
        if self.s:
            if t <= self.s[-1][0] + self.min_gap:
                # merge: newer position wins, time moves to the newer stamp if it is later
                self.s[-1] = (max(t, self.s[-1][0]), x)
                return
            if t - self.s[-1][0] > (gap_allow if gap_allow is not None else self.gap_break):
                self.s = []               # pointer was stationary: start a fresh run
        self.s.append((t, x))
        while len(self.s) > 20 or (self.s and t - self.s[0][0] > self.horizon):
            self.s.pop(0)

    def last_t(self):
        return self.s[-1][0] if self.s else None

    def _window(self, gap_allow=None):
        if len(self.s) < 2:
            return []
        g = gap_allow if gap_allow is not None else self.gap_break
        w = [self.s[-1]]
        for i in range(len(self.s) - 2, -1, -1):
            if w[-1][0] - self.s[i][0] > g:
                break
            w.append(self.s[i])
        w.reverse()
        return w

    def estimate(self, gap_allow=None):
        """Returns velocity (DIP/s, signed) at the newest sample, or None if not enough data."""
        w = self._window(gap_allow)
        if len(w) < self.min_samples or w[-1][0] - w[0][0] < self.min_span:
            return None
        k = self.kind
        if k == 'avg21':          # mean of the 100 ms quadratic and the 50 ms linear estimate
            a = _lsq(w, 2)
            tn = w[-1][0]
            w5 = [p for p in w if tn - p[0] <= 0.050]
            b = _lsq(w5, 1) if len(w5) >= self.min_samples and w5[-1][0] - w5[0][0] >= self.min_span * 0.6 else a
            return None if a is None else 0.5 * (a + (b if b is not None else a))
        if k.startswith('lsq2b'):  # quadratic, derivative evaluated `back` ms before the newest sample
            back = float(k[5:]) / 1000.0
            return _lsq(w, 2, back=back)
        if k == 'lsq1w50':
            tn = w[-1][0]
            w = [p for p in w if tn - p[0] <= 0.050]
            if len(w) < self.min_samples or w[-1][0] - w[0][0] < self.min_span * 0.6:
                return None
            k = 'lsq1'
        if k == 'lsq1':
            return _lsq(w, 1)
        if k == 'lsq2':
            return _lsq(w, 2)
        if k == 'impulse':
            return _impulse(w)
        raise ValueError(k)


def _lsq(w, deg, back=0.0):
    tn = w[-1][0]
    ts = [p[0] - tn for p in w]
    xs = [p[1] for p in w]
    n = len(w)
    if deg == 1:
        st = sum(ts); stt = sum(t * t for t in ts); sx = sum(xs); stx = sum(t * x for t, x in zip(ts, xs))
        den = n * stt - st * st
        if den <= 1e-12:
            return None
        return (n * stx - st * sx) / den
    # quadratic: x = a + b t + c t^2 ; normal equations, solve 3x3 (Cramer); b is the velocity at t=0
    s1 = sum(ts); s2 = sum(t ** 2 for t in ts); s3 = sum(t ** 3 for t in ts); s4 = sum(t ** 4 for t in ts)
    sx = sum(xs); stx = sum(t * x for t, x in zip(ts, xs)); st2x = sum(t * t * x for t, x in zip(ts, xs))
    A = [[n, s1, s2], [s1, s2, s3], [s2, s3, s4]]
    B = [sx, stx, st2x]
    det = _det3(A)
    if abs(det) < 1e-18:
        return _lsq(w, 1)
    Ab = [row[:] for row in A]
    for r in range(3):
        Ab[r][1] = B[r]
    b1 = _det3(Ab) / det
    if back == 0.0:
        return b1
    Ac = [row[:] for row in A]
    for r in range(3):
        Ac[r][2] = B[r]
    c2 = _det3(Ac) / det
    return b1 - 2.0 * c2 * back      # d/dt (a + b t + c t^2) at t = -back


def _det3(m):
    return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
            - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
            + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))


def _impulse(w):
    """Android ImpulseVelocityTrackerStrategy::getVelocity / Compose calculateImpulseVelocity."""
    work = 0.0

    def ke2v(wk):
        return math.copysign(math.sqrt(abs(wk)) * math.sqrt(2.0), wk)
    for i in range(len(w) - 1):
        dt = w[i + 1][0] - w[i][0]
        if dt <= 0:
            continue
        vprev = ke2v(work)
        vcur = (w[i + 1][1] - w[i][1]) / dt
        work += (vcur - vprev) * abs(vcur)
        if i == 0:
            work *= 0.5
    return ke2v(work)


def stop_gate(age_s):
    """1 for age<=30 ms, 0 for age>=70 ms, linear between (replaces the 100 ms cliff)."""
    if not math.isfinite(age_s) or age_s < 0:
        return 0.0
    if age_s <= STOP_FULL_S:
        return 1.0
    if age_s >= STOP_ZERO_S:
        return 0.0
    return (STOP_ZERO_S - age_s) / (STOP_ZERO_S - STOP_FULL_S)


# ---------------------------------------------------------------------------------------------
# Inertia (closed form per step -> independent of frame rate / stalls)
# ---------------------------------------------------------------------------------------------
def glide_distance(v0, tau=TAU_S, c=FRICTION):
    """Total distance until rest for exponential+friction: d = v0*tau - c*tau^2*ln(1+v0/(c*tau))."""
    if v0 <= 0:
        return 0.0
    return v0 * tau - c * tau * tau * math.log(1.0 + v0 / (c * tau))


def glide_time(v0, tau=TAU_S, c=FRICTION):
    return tau * math.log(1.0 + v0 / (c * tau)) if v0 > 0 else 0.0


def inertia_step(speed, dt, tau=TAU_S, c=FRICTION, dt_cap=0.100):
    """Advance one frame.  Returns (distance, new_speed).  Exact integral of v' = -v/tau - c."""
    dt = min(max(dt, 0.0), dt_cap)
    if speed < STOP_SPEED or dt <= 0:
        return 0.0, 0.0 if speed < STOP_SPEED else speed
    A = speed + c * tau
    e = math.exp(-dt / tau)
    s1 = A * e - c * tau
    if s1 < STOP_SPEED:
        t_stop = tau * math.log(A / (c * tau + STOP_SPEED)) if A > c * tau + STOP_SPEED else 0.0
        t_stop = min(t_stop, dt)
        return A * tau * (1 - math.exp(-t_stop / tau)) - c * tau * t_stop, 0.0
    return A * tau * (1 - e) - c * tau * dt, s1


# ---------------------------------------------------------------------------------------------
# Fling booster (Chromium semantics)
# ---------------------------------------------------------------------------------------------
class Booster:
    def __init__(self, add_residual=False):
        self.prev_start = 0.0         # velocity used to start the previous fling (already boosted)
        self.armed = False
        self.residual = 0.0           # decayed fling velocity at grab time
        self.cutoff = None            # absolute time (s) after which a release no longer boosts
        self.add_residual = add_residual
        self.last_scroll_t = None

    def on_fling_start(self, v):
        self.prev_start = v

    def on_grab(self, t, residual_v, fling_active):
        """Pointer went down while the glide was still running."""
        self.armed = fling_active and abs(residual_v) >= BOOST_MIN_FLING and abs(self.prev_start) > 0
        self.residual = residual_v
        self.cutoff = t + BOOST_TIMEOUT_S
        self.last_scroll_t = None

    def on_move(self, t, v_inst, dx):
        """Called for every pan sample with the instantaneous velocity (or None) and the delta."""
        if not self.armed:
            return
        if t > self.cutoff:
            self.armed = False
            return
        if dx * self.prev_start <= 0 and abs(dx) > 0:
            self.armed = False        # moving against the old fling
            return
        if v_inst is not None and abs(v_inst) < BOOST_MIN_SCROLL:
            self.armed = False
            return
        self.cutoff = t + BOOST_TIMEOUT_S

    def on_release(self, t, v_new):
        """Returns the velocity to start the new glide with."""
        v = v_new
        if (self.armed and t <= self.cutoff and v_new * self.prev_start > 0
                and abs(v_new) >= BOOST_MIN_FLING and abs(self.residual) >= BOOST_MIN_FLING):
            v = v_new + (self.residual if self.add_residual else self.prev_start)
            v = math.copysign(min(abs(v), BOOST_MAX), v)
        self.armed = False
        if abs(v) > 0:
            self.prev_start = v
        return v


# ---------------------------------------------------------------------------------------------
# Driver: same mailbox + frame model as drivers.run_current, per-pointer trackers, no resets
# ---------------------------------------------------------------------------------------------
class _C:
    def __init__(self, events, kind):
        self.ev = events
        self.i = 0
        self.last_seq = 0
        self.alive = False
        self.tr = Tracker(kind)
        self.snapshot = None
        self.lifted_v = None
        self.lifted_t = None

    def latest(self, T):
        while self.i < len(self.ev) and self.ev[self.i].q <= T:
            self.i += 1
        return self.ev[self.i - 1] if self.i > 0 else None


def run_proposed(contact_events, frames, kind='lsq2', max_frames=2500, merge_hold_s=0.080, all_events=False):
    cs = [_C(e, kind) for e in contact_events]
    begin_q = max(c.ev[0].q for c in cs)
    res = dict(release_speed=0.0, peak_est=0.0, glide=0.0, first_emit=None, released=False,
               release_age_ms=None, max_step=0.0)
    active = False
    released = False
    inertia_v = 0.0
    last_nav = 0
    last_est = 0.0
    n = 0
    for T in frames:
        n += 1
        if n > max_frames:
            break
        t_now = T / QPC_FREQ
        frame_dt = (T - last_nav) / QPC_FREQ if last_nav else 1.0 / 120
        gap_allow = max(GAP_BREAK_S, frame_dt + 0.016)   # a render-loop stall must not look like a stopped finger
        if not active and not released and T >= begin_q:
            for c in cs:
                c.alive = True
                snap = c.ev[0] if c.ev[0].q == begin_q else c.latest(T)
                if c.ev[0].q == begin_q:
                    c.i = max(c.i, 1)
                c.snapshot = snap
                c.last_seq = snap.seq
                c.tr.reset()
                c.tr.add(snap.q / QPC_FREQ, snap.x)   # Down is a regular sample (true time/pos pair)
            active = True
        if active:
            release_q = 0
            for c in cs:
                if not c.alive:
                    continue
                snap = c.latest(T)
                if snap is None or snap.seq == c.last_seq:
                    continue
                if all_events:
                    # producer-side ring: every event since the last frame reaches the tracker, in order
                    for e in c.ev:
                        if e.seq <= c.last_seq or e.q > T or e.kind != 'move':
                            continue
                        c.tr.add(e.q / QPC_FREQ, e.x, gap_allow)
                c.last_seq = snap.seq
                c.snapshot = snap
                if snap.kind == 'move' and all_events:
                    v = c.tr.estimate(gap_allow)
                    if v is not None:
                        if res['first_emit'] is None:
                            res['first_emit'] = abs(v)
                        res['peak_est'] = max(res['peak_est'], abs(v))
                elif snap.kind == 'move':
                    c.tr.add(snap.q / QPC_FREQ, snap.x, gap_allow)
                    v = c.tr.estimate(gap_allow)
                    if v is not None:
                        if res['first_emit'] is None:
                            res['first_emit'] = abs(v)
                        res['peak_est'] = max(res['peak_est'], abs(v))
                else:
                    c.alive = False
                    release_q = max(release_q, snap.q)
                    v = c.tr.estimate(gap_allow)
                    age = (snap.q / QPC_FREQ) - (c.tr.last_t() or -1e9)
                    c.lifted_v = (v if v is not None else 0.0) * stop_gate(age)
                    c.lifted_t = snap.q / QPC_FREQ
            if not any(c.alive for c in cs):
                vs = [c.lifted_v for c in cs if c.lifted_v is not None
                      and release_q / QPC_FREQ - c.lifted_t <= merge_hold_s]
                v = sum(vs) / len(vs) if vs else 0.0
                last_age = min((release_q / QPC_FREQ) - (c.tr.last_t() or -1e9) for c in cs)
                res['release_age_ms'] = last_age * 1000
                v = math.copysign(min(abs(v), V_MAX), v)
                inertia_v = abs(v) if abs(v) >= STOP_SPEED else 0.0
                res['release_speed'] = inertia_v
                res['released'] = True
                active = False
                released = True
        elif released and inertia_v > 0:
            dt = (T - last_nav) / QPC_FREQ
            d, inertia_v = inertia_step(inertia_v, dt)
            res['glide'] += d
            res['max_step'] = max(res['max_step'], d)
        last_nav = T
        if released and inertia_v <= 0:
            break
    return res
