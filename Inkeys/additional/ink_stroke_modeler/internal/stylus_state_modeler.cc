// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ink_stroke_modeler/internal/stylus_state_modeler.h"

#include <cmath>
#include <deque>
#include <limits>
#include <optional>

#include "ink_stroke_modeler/internal/internal_types.h"
#include "ink_stroke_modeler/internal/utils.h"
#include "ink_stroke_modeler/params.h"
#include "ink_stroke_modeler/types.h"

namespace ink::stroke_model {

void StylusStateModeler::Update(Vec2 position, Time time,
                                const StylusState& state) {
  // Possibly NaN should be prohibited in ValidateInput, but due to current
  // consumers, that can't be tightened for these values currently.
  if (state.pressure < 0 || std::isnan(state.pressure)) {
    state_.received_unknown_pressure = true;
  }
  if (state.tilt < 0 || std::isnan(state.tilt)) {
    state_.received_unknown_tilt = true;
  }
  if (state.orientation < 0 || std::isnan(state.orientation)) {
    state_.received_unknown_orientation = true;
  }

  if (!params_.use_stroke_normal_projection &&
      state_.received_unknown_pressure && state_.received_unknown_tilt &&
      state_.received_unknown_orientation) {
    // We've stopped tracking all fields, so there's no need to keep updating.
    state_.raw_input_and_stylus_states.clear();
    return;
  }

  Vec2 velocity = {0, 0};
  Vec2 acceleration = {0, 0};
  if (!state_.raw_input_and_stylus_states.empty() &&
      time != state_.raw_input_and_stylus_states.back().time) {
    velocity = (position - state_.raw_input_and_stylus_states.back().position) /
               (time - state_.raw_input_and_stylus_states.back().time).Value();
    acceleration =
        (velocity - state_.raw_input_and_stylus_states.back().velocity) /
        (time - state_.raw_input_and_stylus_states.back().time).Value();
  }

  state_.raw_input_and_stylus_states.push_back({
      .position = position,
      .velocity = velocity,
      .acceleration = acceleration,
      .time = time,
      .pressure = state.pressure,
      .tilt = state.tilt,
      .orientation = state.orientation,
  });
}

void StylusStateModeler::Reset(const StylusStateModelerParams& params) {
  state_.raw_input_and_stylus_states.clear();
  state_.projection =
      RawInputProjection{.segment_index = 0, .ratio_along_segment = 0};
  state_.received_unknown_pressure = false;
  state_.received_unknown_tilt = false;
  state_.received_unknown_orientation = false;
  save_active_ = false;
  params_ = params;
}

namespace {

RawInputProjection ProjectAlongStrokeNormal(
    Vec2 position, Vec2 acceleration, Time time, Vec2 stroke_normal,
    const std::deque<Result>& raw_input_polyline,
    const RawInputProjection& previous_projection) {
  // We track the best candidate separately for the left and right sides of the
  // stroke, in case the closest projection is not in the right direction.
  std::optional<RawInputProjection> best_left_projection;
  std::optional<RawInputProjection> best_right_projection;
  float best_distance_left = std::numeric_limits<float>::infinity();
  float best_distance_right = std::numeric_limits<float>::infinity();

  // Update `best_projection` and `best_distance` if needed.
  auto maybe_update_projection =
      [](RawInputProjection candidate, float distance,
         std::optional<RawInputProjection>& best_projection,
         float& best_distance) {
        if (distance < best_distance) {
          best_projection = candidate;
          best_distance = distance;
        }
      };
  for (int i = previous_projection.segment_index;
       i < static_cast<int>(raw_input_polyline.size()) - 1; ++i) {
    const Vec2 segment_start = raw_input_polyline[i].position;
    const Vec2 segment_end = raw_input_polyline[i + 1].position;

    // Find the intersection of the stroke normal with the polyline segment.
    std::optional<float> segment_ratio = ProjectToSegmentAlongNormal(
        segment_start, segment_end, position, stroke_normal);
    if (!segment_ratio.has_value()) continue;

    // Ignore a projection that would backtrack.
    if (i == previous_projection.segment_index &&
        *segment_ratio <= previous_projection.ratio_along_segment) {
      continue;
    }

    Vec2 projection = Interp(segment_start, segment_end, *segment_ratio);
    float distance = Distance(position, projection);

    // We update either the best left or the right projection, depending which
    // side of the stroke it lies on -- recall that the stroke normal always
    // points to the left.
    RawInputProjection candidate{.segment_index = static_cast<int>(i),
                                 .ratio_along_segment = *segment_ratio};
    float dot_product = Vec2::DotProduct(projection - position, stroke_normal);
    if (dot_product == 0) {
      // This is a direct intersection, so it's the best candidate.
      return candidate;
    } else if (dot_product < 0) {
      maybe_update_projection(candidate, distance, best_right_projection,
                              best_distance_right);
    } else {
      maybe_update_projection(candidate, distance, best_left_projection,
                              best_distance_left);
    }
  }

  if (best_left_projection.has_value() && best_right_projection.has_value()) {
    // We have candidate projections on both sides of the stroke, so we want to
    // choose the one on the "outside" of the turn. The acceleration will always
    // point to the "inside" of the curve, so we can compare it to the stroke
    // normal (which always points left) to determine whether to use the left or
    // right candidate.
    return Vec2::DotProduct(stroke_normal, acceleration) > 0
               ? *best_right_projection
               : *best_left_projection;
  }

  // We have at most one projection.
  if (best_right_projection.has_value()) {
    return *best_right_projection;
  }
  return best_left_projection.value_or(previous_projection);
}

RawInputProjection ProjectToClosestPoint(
    Vec2 position, const std::deque<Result>& raw_input_polyline,
    const RawInputProjection& previous_projection) {
  std::optional<RawInputProjection> best_projection;
  float min_distance = std::numeric_limits<float>::infinity();
  for (decltype(raw_input_polyline.size()) i = 0;
       i < raw_input_polyline.size() - 1; ++i) {
    const Vec2 segment_start = raw_input_polyline[i].position;
    const Vec2 segment_end = raw_input_polyline[i + 1].position;
    float segment_ratio =
        NearestPointOnSegment(segment_start, segment_end, position);
    // Ignore a projection that would backtrack.
    if (i == previous_projection.segment_index &&
        segment_ratio < previous_projection.ratio_along_segment) {
      continue;
    }
    float distance =
        Distance(position, Interp(segment_start, segment_end, segment_ratio));
    if (distance <= min_distance) {
      best_projection =
          RawInputProjection{.segment_index = static_cast<int>(i),
                             .ratio_along_segment = segment_ratio};
      min_distance = distance;
    }
  }
  return best_projection.value_or(previous_projection);
}

}  // namespace

Result StylusStateModeler::Project(const TipState& tip,
                                   const std::optional<Vec2>& stroke_normal) {
  const std::deque<Result>& states = state_.raw_input_and_stylus_states;
  RawInputProjection& projection = state_.projection;
  if (states.empty()) {
    return {};
  }

  if (params_.use_stroke_normal_projection && stroke_normal.has_value()) {
    projection =
        ProjectAlongStrokeNormal(tip.position, tip.acceleration, tip.time,
                                 *stroke_normal, states, projection);
  } else {
    projection = ProjectToClosestPoint(tip.position, states, projection);
  }

  // Discard earlier segments. The projection is not allowed to backtrack, so
  // we don't need to keep these for the entire duration of the stroke.
  while (state_.projection.segment_index > 0) {
    state_.projection.segment_index--;
    state_.raw_input_and_stylus_states.pop_front();
  }

  Result projected_result =
      states.size() > 1
          ? InterpResult(states[0], states[1], projection.ratio_along_segment)
          : states.front();

  // Correct the time and strip missing fields before returning.
  projected_result.time = tip.time;
  if (state_.received_unknown_pressure) {
    projected_result.pressure = -1;
  }
  if (state_.received_unknown_tilt) {
    projected_result.tilt = -1;
  }
  if (state_.received_unknown_orientation) {
    projected_result.orientation = -1;
  }

  return projected_result;
}

void StylusStateModeler::Save() {
  saved_state_ = state_;
  save_active_ = true;
}

void StylusStateModeler::Restore() {
  if (save_active_) {
    state_ = saved_state_;
  }
}

}  // namespace ink::stroke_model
