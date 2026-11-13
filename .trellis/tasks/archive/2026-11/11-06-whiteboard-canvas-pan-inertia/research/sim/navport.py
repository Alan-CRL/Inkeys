"""Faithful Python port of Draw3.CanvasNavigation.cpp pan-velocity/inertia logic.

Source of truth:
  Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp  (lines noted per function)
  Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cppm (constants, lines 14-24)

Only the motion math is ported (no gesture FSM, no tile planning).  Velocities are
rounded through float32 where the C++ uses `float`; sample sums are double like C++.
Run with:  python -I navport.py   (self-test only)
"""
import math
import struct

QPC_FREQ = 10_000_000  # typical Windows 10/11 x64 QueryPerformanceFrequency (10 MHz)

# --- constants (cppm L14-L24) ------------------------------------------------
GESTURE_WINDOW_S = 0.180
RELEASE_HORIZON_S = 0.100
SAMPLE_CAPACITY = 24
MAX_SPEED = 24000.0
DECEL = 6000.0
PEN_DECEL = 12000.0
STOP_SPEED = 5.0


def f32(x):
    try:
        return struct.unpack('<f', struct.pack('<f', x))[0]
    except OverflowError:
        return math.copysign(float('inf'), x)


def clamp_velocity(vx, vy):  # cpp L26-L33
    if not (math.isfinite(vx) and math.isfinite(vy)):
        return (0.0, 0.0)
    speed = f32(math.hypot(vx, vy))
    if speed <= MAX_SPEED or speed <= 0.0:
        return (vx, vy)
    scale = f32(MAX_SPEED / speed)
    return (f32(vx * scale), f32(vy * scale))


class Motion:
    """CanvasPanMotionState (cppm L153-L171)."""

    def __init__(self):
        self.velocity = (0.0, 0.0)
        self.direct = (0.0, 0.0)
        self.candidate = (0.0, 0.0)
        self.selected = (0.0, 0.0)
        self.cand_src = 'none'        # none | residual | topology
        self.release_src = 'none'     # none | new-direct | residual | topology
        self.sx = 0.0
        self.sy = 0.0
        self.last_update_qpc = 0
        self.last_vel_sample_qpc = 0
        self.samples = []             # list of (qpc, x, y), len <= 24
        self.has_new_move = False
        self.inertia_active = False

    def speed(self):
        return math.hypot(*self.velocity)


def reset_velocity_samples(m, qpc):  # cpp L35-L44
    m.sx = 0.0
    m.sy = 0.0
    m.last_update_qpc = qpc
    m.last_vel_sample_qpc = 0
    m.samples = [(qpc, 0.0, 0.0)] if qpc > 0 else []


def append_velocity_sample(m, qpc, freq):  # cpp L46-L70
    if qpc <= 0 or freq <= 0:
        return
    if m.samples and qpc <= m.samples[-1][0]:
        return
    if len(m.samples) == SAMPLE_CAPACITY:
        m.samples.pop(0)
    m.samples.append((qpc, m.sx, m.sy))
    horizon = int(RELEASE_HORIZON_S * freq)
    while len(m.samples) > 1 and qpc - m.samples[0][0] > horizon:
        m.samples.pop(0)


def estimate_velocity(m, freq):  # cpp L72-L102  (LSQ degree-1 over all kept samples)
    n = len(m.samples)
    if n < 2 or freq <= 0:
        return (0.0, 0.0)
    newest = m.samples[-1][0]
    st = stt = sx = sy = stx = sty = 0.0
    for (q, x, y) in m.samples:
        t = (q - newest) / freq
        st += t
        stt += t * t
        sx += x
        sy += y
        stx += t * x
        sty += t * y
    den = n * stt - st * st
    if not math.isfinite(den) or den <= 1.0e-12:
        return (0.0, 0.0)
    return clamp_velocity(f32((n * stx - st * sx) / den), f32((n * sty - st * sy) / den))


def begin_pan(m, inherit, qpc):  # cpp L335-L350
    m.candidate = clamp_velocity(*m.velocity) if inherit else (0.0, 0.0)
    m.cand_src = 'residual' if inherit else 'none'
    m.release_src = 'none'
    m.inertia_active = False
    m.velocity = (0.0, 0.0)
    m.direct = (0.0, 0.0)
    m.selected = (0.0, 0.0)
    m.has_new_move = False
    reset_velocity_samples(m, qpc)


def reset_pan_velocity_samples(m, qpc):  # cpp L352-L368
    if m.has_new_move:
        m.candidate = clamp_velocity(*m.direct)
        m.cand_src = 'topology'
    reset_velocity_samples(m, max(qpc, m.last_update_qpc))
    m.velocity = (0.0, 0.0)
    m.direct = (0.0, 0.0)
    m.selected = (0.0, 0.0)
    m.release_src = 'none'
    m.has_new_move = False


def update_pan(m, content_delta, velocity_delta, qpc, freq, update_velocity=True):  # cpp L370-L409
    if qpc > m.last_update_qpc:
        m.last_update_qpc = qpc
    positional = velocity_delta[0] != 0.0 or velocity_delta[1] != 0.0
    if update_velocity and positional and qpc > 0 and freq > 0:
        m.has_new_move = True
        m.candidate = (0.0, 0.0)
        m.cand_src = 'none'
        if not m.samples:
            reset_velocity_samples(m, qpc)
        elif qpc > m.samples[-1][0]:
            m.sx += velocity_delta[0]
            m.sy += velocity_delta[1]
            append_velocity_sample(m, qpc, freq)
            m.direct = estimate_velocity(m, freq)
            m.last_vel_sample_qpc = qpc
    elif update_velocity and positional:
        m.has_new_move = True
        m.candidate = (0.0, 0.0)
        m.cand_src = 'none'
    m.velocity = clamp_velocity(*m.direct)
    return content_delta


def release_age_seconds(release_qpc, last_input_qpc, freq, cancelled):  # cpp L416-L424
    if cancelled or release_qpc <= 0 or last_input_qpc <= 0 or freq <= 0 or release_qpc < last_input_qpc:
        return float('inf')
    return (release_qpc - last_input_qpc) / freq


def end_pan(m, age):  # cpp L426-L459
    if m.has_new_move:
        m.selected = clamp_velocity(*m.direct)
        m.release_src = 'new-direct'
    elif m.cand_src != 'none':
        m.selected = clamp_velocity(*m.candidate)
        m.release_src = 'residual' if m.cand_src == 'residual' else 'topology'
    else:
        m.selected = (0.0, 0.0)
        m.release_src = 'none'
    m.velocity = m.selected
    if (not math.isfinite(age)) or age < 0.0 or age > RELEASE_HORIZON_S:
        m.velocity = (0.0, 0.0)
    m.velocity = clamp_velocity(*m.velocity)
    m.candidate = (0.0, 0.0)
    m.cand_src = 'none'
    m.inertia_active = m.speed() >= STOP_SPEED


def stop_pan(m):  # cpp L503
    m.__init__()


def step_inertia(m, dt, pen_in_range):  # cpp L472-L501
    if (not m.inertia_active) or (not math.isfinite(dt)) or dt <= 0.0:
        return (0.0, 0.0)
    bounded = min(dt, 0.05)
    speed = f32(m.speed())
    if speed < STOP_SPEED:
        stop_pan(m)
        return (0.0, 0.0)
    decel = PEN_DECEL if pen_in_range else DECEL
    seconds = f32(bounded)
    next_speed = max(0.0, f32(speed - decel * seconds))
    traveled = f32((speed + next_speed) * 0.5 * seconds)
    delta = (f32(m.velocity[0] / speed * traveled), f32(m.velocity[1] / speed * traveled))
    if next_speed < STOP_SPEED:
        stop_pan(m)
    else:
        scale = next_speed / speed
        m.velocity = (f32(m.velocity[0] * scale), f32(m.velocity[1] * scale))
    return delta


if __name__ == '__main__':
    # Self-test mirroring inkStrokeModelerTestTests/canvas_navigation_tests.cpp L310-L319:
    # 3200 DIP/s glide with 6000 DIP/s^2 == 853.3 DIP.
    m = Motion()
    m.velocity = (3200.0, 0.0)
    m.inertia_active = True
    d = 0.0
    while m.inertia_active:
        d += step_inertia(m, 0.01, False)[0]
    print('glide(3200) =', round(d, 2), 'expected', round(3200 ** 2 / (2 * DECEL), 2))
    # Self-test mirroring L224-L233: samples at +10/+30/+50 ms (freq 1000) -> 1000 DIP/s
    m = Motion()
    begin_pan(m, False, 4000)
    update_pan(m, (10, 0), (10, 0), 4010, 1000)
    update_pan(m, (20, 0), (20, 0), 4030, 1000)
    update_pan(m, (20, 0), (20, 0), 4050, 1000)
    print('lsq velocity =', m.velocity, 'expected ~1000')
