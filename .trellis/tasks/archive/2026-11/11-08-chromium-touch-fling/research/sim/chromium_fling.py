#!/usr/bin/env python3
"""Chromium formula oracle (standard library only): python -I chromium_fling.py.

Audited against the local Chromium main snapshots on 2026-11-08.
This independent double-precision oracle is not a bit-for-bit float emulator.
No project imports, application execution, network, or third-party packages.
"""
import json
import math


def time_delta(a, b):
    """Chromium TimeDelta subtraction uses integer microseconds."""
    return (round(a * 1000000) - round(b * 1000000)) / 1000000


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def lsq2(samples):
    """Samples newest first, (seconds, x, y); return derivative at newest.

    Equivalent to VelocityTracker LSQ2 / WEIGHTING_NONE / RESTRICTION_NONE:
    columns 1,t,t^2 -> modified Gram-Schmidt QR -> triangular solve.
    """
    if not samples:
        return (0.0, 0.0)
    newest = samples[0][0]
    selected = [s for s in samples[:20] if time_delta(newest, s[0]) <= 0.100]
    n = min(2, len(selected) - 1) + 1
    if n < 2:
        return (0.0, 0.0)
    ts = [time_delta(s[0], newest) for s in selected]
    a = [[t ** j for t in ts] for j in range(n)]
    q, r = [], [[0.0] * n for _ in range(n)]
    for j in range(n):
        col = a[j][:]
        for qi in q:
            projection = dot(qi, col)
            col = [x - projection * y for x, y in zip(col, qi)]
        norm = math.sqrt(dot(col, col))
        if norm < 1e-6:
            return (0.0, 0.0)
        q.append([x / norm for x in col])
        for i in range(j, n):
            r[j][i] = dot(q[j], a[i])
    velocities = []
    for axis in (1, 2):
        ys = [s[axis] for s in selected]
        b = [0.0] * n
        for i in reversed(range(n)):
            b[i] = (dot(q[i], ys) - sum(r[i][j] * b[j]
                    for j in range(i + 1, n))) / r[i][i]
        velocities.append(b[1])
    return tuple(velocities)


class Tracker:
    """One multipointer movement ring, including missing-id backtrack stop."""
    def __init__(self):
        self.clear()

    def clear(self):
        self.movements = []
        self.last_time = None
        self.ids = set()

    def clear_pointer(self, pointer_id):
        self.ids.discard(pointer_id)
        if self.movements:
            self.movements[-1][1].pop(pointer_id, None)

    def add(self, time, positions, action="move", pointer_id=None):
        if action == "down":
            self.clear()
        elif action == "pointer_down":
            self.clear_pointer(pointer_id)
        elif action in ("up", "pointer_up"):
            if self.last_time is not None and time_delta(time, self.last_time) >= .080:
                self.movements = []
            return
        elif action != "move":
            return
        ids = set(positions)
        if (ids & self.ids and self.last_time is not None
                and time_delta(time, self.last_time) >= .040):
            self.movements = []
        self.last_time, self.ids = time, ids
        self.movements.append((time, dict(positions)))
        self.movements = self.movements[-20:]

    def velocity(self, pointer_id):
        selected = []
        if not self.movements:
            return (0., 0.)
        newest = self.movements[-1][0]
        for time, positions in reversed(self.movements):
            if pointer_id not in positions or time_delta(newest, time) > .100:
                break
            selected.append((time, *positions[pointer_id]))
        return lsq2(selected)


class Bezier:
    """CubicBezier::Solve: 11-sample seed, 4 Newton iterations, bisection."""
    def __init__(self, p1x, p1y, p2x=.55, p2y=1.):
        self.cx, self.cy = 3 * p1x, 3 * p1y
        self.bx = 3 * (p2x - p1x) - self.cx
        self.by = 3 * (p2y - p1y) - self.cy
        self.ax, self.ay = 1 - self.cx - self.bx, 1 - self.cy - self.by
        self.start_gradient = (p1y / p1x if p1x > 0 else
            p2y / p2x if p1y == 0 and p2x > 0 else
            1. if p1y == 0 and p2y == 0 else 0.)
        self.end_gradient = ((p2y - 1) / (p2x - 1) if p2x < 1 else
            (p1y - 1) / (p1x - 1) if p2y == 1 and p1x < 1 else
            1. if p2y == 1 and p1y == 1 else 0.)
        self.spline = [self.x(i / 10) for i in range(11)]

    def x(self, t):
        return ((self.ax * t + self.bx) * t + self.cx) * t

    def y(self, t):
        return ((self.ay * t + self.by) * t + self.cy) * t

    def dx(self, t):
        return (3 * self.ax * t + 2 * self.bx) * t + self.cx

    def solve(self, x, epsilon=1e-7):
        if x < 0:
            return self.start_gradient * x
        if x > 1:
            return 1 + self.end_gradient * (x - 1)
        t = x
        lo, hi = 0., 1.
        for i in range(1, 11):
            if x <= self.spline[i]:
                hi, lo = i / 10, (i - 1) / 10
                t = lo + (hi - lo) * (x - self.spline[i - 1]) / (
                    self.spline[i] - self.spline[i - 1])
                break
        error = 0.
        for _ in range(4):
            error = self.x(t) - x
            if abs(error) < min(1e-7, epsilon):
                return self.y(t)
            derivative = self.dx(t)
            if abs(derivative) < 1e-7:
                break
            t -= error / derivative
        if abs(error) < epsilon:
            return self.y(t)
        while lo < hi:
            value = self.x(t)
            if abs(value - x) < epsilon:
                return self.y(t)
            if x > value:
                lo = t
            else:
                hi = t
            next_t = (lo + hi) * .5
            if next_t == t:  # finite-precision termination
                break
            t = next_t
        return self.y(t)


class PhysicsCurve:
    def __init__(self, velocity, viewport=(1000, 800), boost=1., ppi=(96., 96.)):
        self.duration = min(2., max(map(abs, velocity)) / 2300.)
        self.distance = []
        for v, inches, size in zip(velocity, ppi, viewport):
            a = inches * 2.7559e-5  # DIP/ms^2 in this task
            d = math.copysign((v / 1000.) ** 2 / (2 * a), v)
            bound = math.floor(size * boost * 3.)
            self.distance.append(max(-bound, min(bound, d)))
        axis = 1 if abs(velocity[1]) == max(map(abs, velocity)) else 0
        slope = abs(velocity[axis] * self.duration / self.distance[axis])
        self.p1 = (.2, .2 * slope) if .2 * slope < 1 else (1 / slope, 1.)
        self.bezier = Bezier(*self.p1)
        self.previous_time, self.previous_offset = 0., (0., 0.)

    def advance(self, elapsed):
        if elapsed < 0:
            return True, (0., 0.), (0., 0.)
        active = elapsed / self.duration < 1.
        if not active:
            return False, tuple(self.distance), (0., 0.)
        progress = self.bezier.solve(elapsed / self.duration)
        offset = tuple(d * progress for d in self.distance)
        dt = elapsed - self.previous_time
        velocity = tuple((x - y) / dt for x, y in zip(offset, self.previous_offset))
        self.previous_time, self.previous_offset = elapsed, offset
        return True, offset, velocity


class Booster:
    """All eight ShouldBoostFling rejection checks, including source/modifiers."""
    def __init__(self):
        self.reset()

    def reset(self):
        self.previous, self.current = (0., 0.), (0., 0.)
        self.cutoff, self.scroll_time = None, None
        self.source, self.modifiers = None, 0

    def should_boost(self, time, velocity, source=1, modifiers=0):
        checks = [self.previous != (0., 0.), source == self.source,
                  modifiers == self.modifiers, self.cutoff is not None,
                  self.cutoff is not None and time_delta(time, self.cutoff) <= 0,
                  dot(self.previous, velocity) > 0,
                  dot(self.current, self.current) >= 350 ** 2,
                  dot(velocity, velocity) >= 350 ** 2]
        return all(checks), checks

    def start(self, time, velocity, source=1, modifiers=0):
        if self.should_boost(time, velocity, source, modifiers)[0]:
            velocity = tuple(x + y for x, y in zip(velocity, self.previous))
        self.reset()
        self.previous = self.current = velocity
        self.source, self.modifiers = source, modifiers
        return velocity

    def observe(self, kind, time, delta=(0., 0.), source=1,
                momentum=False, prevent_boosting=False):
        if self.previous == (0., 0.):
            return
        if ((self.cutoff is not None and time_delta(time, self.cutoff) > 0)
                or source != self.source):
            self.reset()
            return
        if kind == "begin":
            self.cutoff = time + .050
        elif kind == "update":
            if momentum or self.cutoff is None:
                return
            if dot(self.previous, delta) <= 0:
                self.reset()
                return
            if self.scroll_time is not None:
                dt = time_delta(time, self.scroll_time)
                if dt >= .001 and dot(delta, delta) / dt ** 2 < 150 ** 2:
                    self.reset()
                    return
            self.scroll_time, self.cutoff = time, time + .050
        elif kind == "end":
            self.scroll_time = None
        elif kind == "cancel":
            if prevent_boosting:
                self.reset()
                return
            self.scroll_time, self.cutoff = None, time + .050


def legacy(elapsed, velocity):
    alpha, beta, gamma = -5707.62, 172., 3.7
    position = lambda t: alpha * math.exp(-gamma * t) - beta * t - alpha
    speed = lambda t: -alpha * gamma * math.exp(-gamma * t) - beta
    time_at_speed = lambda v: -math.log((v + beta) / (-alpha * gamma)) / gamma
    maximum = min(max(map(abs, velocity)), speed(0.))
    time_offset = time_at_speed(maximum)
    duration = time_at_speed(0.)
    t = min(duration, elapsed + time_offset)
    scalar = position(t) - position(time_offset)
    return {"duration": duration - time_offset,
            "offset": [v / maximum * scalar for v in velocity],
            "velocity": [v / maximum * speed(t) if t < duration else 0.
                         for v in velocity]}


def main():
    out = {"precision": "independent Python double formula reference"}
    xs = [0., 30., 60., 90., 120., 90., 60., 30., 0.]
    samples, rows = [], []
    for i, x in enumerate(xs):
        samples.insert(0, (i * .010, x, 0.))
        rows.append({"time": i * .010, "x": x, "velocity": lsq2(samples)})
    out["lsq_reversal"] = rows
    tracker_rows = {}
    for gap in (.039999, .040000):
        tr = Tracker()
        tr.add(1., {1: (0., 0.)}, "down")
        tr.add(1.01, {1: (30., 0.)})
        tr.add(1.01 + gap, {1: (60., 0.)})
        tracker_rows["move_gap_" + str(gap)] = tr.velocity(1)
    for age in (.079999, .080000):
        tr = Tracker()
        tr.add(1., {1: (0., 0.)}, "down")
        tr.add(1.01, {1: (30., 0.)})
        tr.add(1.01 + age, {1: (30., 0.)}, "up")
        tracker_rows["up_age_" + str(age)] = tr.velocity(1)
    tr = Tracker()
    tr.add(1., {1: (0., 0.)}, "down")
    tr.add(1.01, {1: (30., 0.)})
    tr.add(1.02, {1: (60., 0.), 2: (100., 0.)}, "pointer_down", 2)
    tracker_rows["pointer_down_keeps_first"] = tr.velocity(1)
    tracker_rows["pointer_down_new_has_one"] = tr.velocity(2)
    tr.add(1.03, {1: (90., 0.), 2: (130., 0.)})
    tracker_rows["two_pointer_move"] = [tr.velocity(1), tr.velocity(2)]
    tr.add(1.04, {1: (120., 0.)})
    tr.add(1.05, {1: (150., 0.), 2: (190., 0.)})
    tracker_rows["missing_id_stops_backtrack"] = tr.velocity(2)
    out["tracker_boundaries"] = tracker_rows
    out["lsq_two_samples"] = lsq2([(.010, 30., -15.), (0., 0., 0.)])
    out["lsq_quadratic"] = lsq2([(t, 1000 * t + 5000 * t * t, -500 * t)
                                    for t in (.08, .06, .04, .02, 0.)])
    out["bezier"] = {str(x): Bezier(.2, 1.).solve(x)
                     for x in (-.1, 0., .1, .25, .5, .75, 1., 1.1)}
    curves = []
    for velocity, viewport, boost in [((3000., -1500.), (1000, 800), 1.),
                                     ((10000., 5000.), (1000, 800), 1.),
                                     ((10000., 5000.), (1000, 800), 2.),
                                     ((1000., 0.), (1000, 800), 1.)]:
        curve = PhysicsCurve(velocity, viewport, boost)
        row = {"initial_velocity": velocity, "viewport": viewport, "boost": boost,
               "duration": curve.duration, "distance": curve.distance,
               "p1": curve.p1, "samples": []}
        for fraction in (.1, .25, .5, .75, 1.):
            active, offset, speed = curve.advance(curve.duration * fraction)
            row["samples"].append({"fraction": fraction, "active": active,
                                   "offset": offset, "velocity": speed})
        curves.append(row)
    out["physics"] = curves
    out["legacy"] = {str(t): legacy(t, (3000., -1500.)) for t in (.1, .5, 2.)}
    booster = Booster()
    booster.start(1., (1000., 0.))
    booster.current = (400., 0.)  # deliberately use decayed current velocity
    booster.observe("cancel", 1.01)
    out["boost_at_cutoff"] = booster.should_boost(1.06, (500., 0.))
    out["boost_after_cutoff"] = booster.should_boost(1.060001, (500., 0.))
    out["boost_accumulates_start_not_current"] = booster.start(1.04, (500., 0.))
    cases = []
    for failed in range(8):
        b = Booster()
        b.start(1., (1000., 0.))
        b.current = (400., 0.)
        b.cutoff = 1.1
        time, v, source, modifiers = 1.05, (500., 0.), 1, 0
        if failed == 0: b.previous = (0., 0.)
        elif failed == 1: source = 2
        elif failed == 2: modifiers = 1
        elif failed == 3: b.cutoff = None
        elif failed == 4: time = 1.100001
        elif failed == 5: v = (-500., 0.)
        elif failed == 6: b.current = (349., 0.)
        elif failed == 7: v = (349., 0.)
        cases.append({"rejection_index": failed, "result": b.should_boost(
            time, v, source, modifiers)})
    out["booster_eight_rejections"] = cases
    for amount in (1.49, 1.5):
        b = Booster()
        b.start(1., (1000., 0.))
        b.observe("cancel", 1.01)
        b.observe("update", 1.02, (10., 0.))
        b.observe("update", 1.03, (amount, 0.))
        out["scroll_speed_" + str(amount / .01)] = b.previous
    out["resolved_semantics"] = [
        "First Provider scroll: radially subtract 15 per pointer if count < 3, then average.",
        "Curve returns endpoint on inactive; controller discards inactive delta (no endpoint dispatch)."]
    print(json.dumps(out, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
