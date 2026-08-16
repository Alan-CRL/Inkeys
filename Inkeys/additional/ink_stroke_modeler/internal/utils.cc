#include "ink_stroke_modeler/internal/utils.h"

#include <cstdlib>
#include <optional>

#include "ink_stroke_modeler/internal/internal_types.h"
#include "ink_stroke_modeler/types.h"

namespace ink {
namespace stroke_model {

Result InterpResult(const Result& start, const Result& end,
                    float interp_amount) {
  return {
      .position = Interp(start.position, end.position, interp_amount),
      .velocity = Interp(start.velocity, end.velocity, interp_amount),
      .acceleration =
          Interp(start.acceleration, end.acceleration, interp_amount),
      .time = Interp(start.time, end.time, interp_amount),
      .pressure = start.pressure < 0 || end.pressure < 0
                      ? -1
                      : Interp(start.pressure, end.pressure, interp_amount),
      .tilt = start.tilt < 0 || end.tilt < 0
                  ? -1
                  : Interp(start.tilt, end.tilt, interp_amount),
      .orientation =
          start.orientation < 0 || end.orientation < 0
              ? -1
              : InterpAngle(start.orientation, end.orientation, interp_amount),
  };
}

std::optional<Vec2> GetStrokeNormal(const TipState& tip_state, Time prev_time) {
  constexpr float kCosineHalfDegree = 0.99996192;

  auto orthogonal = [](Vec2 v) { return Vec2{-v.y, v.x}; };

  float v_magnitude = tip_state.velocity.Magnitude();
  float a_magnitude = tip_state.acceleration.Magnitude();

  if (v_magnitude == 0 && a_magnitude == 0) {
    // If both the velocity and acceleration are zero, we can't compute the
    // stroke normal.
    return std::nullopt;
  }

  // If either of the velocity or acceleration is zero, the normal direction is
  // orthogonal to whichever is not zero.
  if (v_magnitude == 0) {
    return orthogonal(tip_state.acceleration);
  }
  if (a_magnitude == 0) {
    return orthogonal(tip_state.velocity);
  }

  // If the velocity and acceleration are sufficiently close to pointing the
  // same or opposite direction, skip the calculation below and return the
  // orthogonal vector to the velocity. This avoids potential precision issues
  // without meaningfully sacrificing quality.
  if (std::abs(Vec2::DotProduct(tip_state.velocity, tip_state.acceleration)) >
      kCosineHalfDegree * v_magnitude * a_magnitude) {
    return orthogonal(tip_state.velocity);
  }

  // The direction of the previous segment in the polyline is `velocity`; we
  // don't know the direction of the next segment (because it hasn't happened
  // yet), but `velocity` + `acceleration` * `delta_t` should be a decent
  // approximation. We then normalize the vectors and add them together to get
  // the average stroke direction at this point.
  auto unit_vec = [](Vec2 x) { return x / x.Magnitude(); };
  Duration delta_t = tip_state.time - prev_time;
  Vec2 stroke_dir =
      unit_vec(tip_state.velocity) +
      unit_vec(tip_state.velocity + tip_state.acceleration * delta_t.Value());

  // The vector orthogonal to the stroke direction is the normal vector.
  return orthogonal(stroke_dir);
}

std::optional<float> ProjectToSegmentAlongNormal(Vec2 segment_start,
                                                 Vec2 segment_end,
                                                 Vec2 position,
                                                 Vec2 stroke_normal) {
  auto cross = [](Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; };

  Vec2 v = segment_end - segment_start;
  float det = cross(stroke_normal, v);
  if (det == 0) return std::nullopt;

  Vec2 w = segment_start - position;
  float param = cross(w, stroke_normal) / det;
  if (param < 0 || param > 1) return std::nullopt;
  return param;
}

}  // namespace stroke_model
}  // namespace ink
