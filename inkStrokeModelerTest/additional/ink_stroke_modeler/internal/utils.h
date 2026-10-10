/*
 * Copyright 2022 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef INK_STROKE_MODELER_INTERNAL_UTILS_H_
#define INK_STROKE_MODELER_INTERNAL_UTILS_H_

#include <algorithm>
#include <optional>

#include "ink_stroke_modeler/internal/internal_types.h"
#include "ink_stroke_modeler/numbers.h"
#include "ink_stroke_modeler/types.h"

namespace ink {
namespace stroke_model {

// General utility functions for use within the stroke model.

// Clamps the given value to the range [0, 1].
inline float Clamp01(float value) { return std::clamp(value, 0.f, 1.f); }

// Returns the ratio of the difference from `start` to `value` and the
// difference from `start` to `end`, clamped to the range [0, 1]. If
// `start` == `end`, returns 1 if `value` > `start`, 0 otherwise.
inline float Normalize01(float start, float end, float value) {
  if (start == end) {
    return value > start ? 1 : 0;
  }
  return Clamp01((value - start) / (end - start));
}

// Linearly interpolates between `start` and `end`, clamping the interpolation
// value to the range [0, 1].
template <typename ValueType>
inline ValueType Interp(ValueType start, ValueType end, float interp_amount) {
  return start + (end - start) * Clamp01(interp_amount);
}

// Linearly rescales `value` relative to `a` and `b`, such that `a` maps to 0
// and `b` maps to 1. If `a` == `b` this function will return 0 for any `value`.
//
// Note that this function doesn't clamp the result to the range [0, 1].
inline float InverseLerp(float a, float b, float value) {
  if (b - a == 0.f) {
    return 0.f;
  }
  return (value - a) / (b - a);
}

// Linearly interpolates from `start` to `end`, traveling around the shorter
// path (e.g. interpolating from π/4 to 7π/4 is equivalent to interpolating from
// π/4 to 0, then 2π to 7π/4). The returned angle will be normalized to the
// interval [0, 2π). All angles are measured in radians.
inline float InterpAngle(float start, float end, float interp_amount) {
  auto normalize_angle = [](float angle) {
    while (angle < 0) angle += 2 * kPi;
    while (angle > 2 * kPi) angle -= 2 * kPi;
    return angle;
  };

  start = normalize_angle(start);
  end = normalize_angle(end);
  float delta = end - start;
  if (delta < -kPi) {
    end += 2 * kPi;
  } else if (delta > kPi) {
    end -= 2 * kPi;
  }
  return normalize_angle(Interp(start, end, interp_amount));
}

// Linearly interpolates all fields of `Result` using the `Interp` function,
// with the exception of `orientation` which uses `InterpAngle`.
//
// If `pressure`, `tilt`, or `orientation` are not present on either `start` or
// `end` (as indicated by a value < 0), then the output will also have an unset
// value for that field, indicated by a -1.
Result InterpResult(const Result& start, const Result& end,
                    float interp_amount);

// Returns the distance between two points.
inline float Distance(Vec2 start, Vec2 end) {
  return (end - start).Magnitude();
}

// Returns the point on the line segment from `segment_start` to `segment_end`
// that is closest to `point`, represented as the ratio of the length along the
// segment.
inline float NearestPointOnSegment(Vec2 segment_start, Vec2 segment_end,
                                   Vec2 point) {
  if (segment_start == segment_end) return 0;

  Vec2 segment_vector = segment_end - segment_start;
  Vec2 projection_vector = point - segment_start;
  return Clamp01(Vec2::DotProduct(projection_vector, segment_vector) /
                 Vec2::DotProduct(segment_vector, segment_vector));
}

// Returns a vector approximating the normal direction of the stroke based on
// the velocity and acceleration of `tip_state`. The returned vector's magnitude
// will be an arbitrary value in the range (0, 2], and it will point to the
// left-hand side of the stroke (assuming a right-handed coordinate system).
//
// If velocity and magnitude are both zero, then we cannot compute the normal
// direction, and this return `std::nullopt`.
std::optional<Vec2> GetStrokeNormal(const TipState& tip_state, Time prev_time);

// Projects the given `position` to the segment defined by `segment_start` and
// `segment_end` along the given `stroke_normal`. If the projection is not
// possible, this returns `std::nullopt`.
std::optional<float> ProjectToSegmentAlongNormal(Vec2 segment_start,
                                                 Vec2 segment_end,
                                                 Vec2 position,
                                                 Vec2 stroke_normal);

}  // namespace stroke_model
}  // namespace ink

#endif  // INK_STROKE_MODELER_INTERNAL_UTILS_H_
