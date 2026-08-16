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

#ifndef INK_STROKE_MODELER_INTERNAL_POSITION_MODELER_H_
#define INK_STROKE_MODELER_INTERNAL_POSITION_MODELER_H_

#include <optional>

#include "absl/status/statusor.h"
#include "ink_stroke_modeler/internal/internal_types.h"
#include "ink_stroke_modeler/internal/utils.h"
#include "ink_stroke_modeler/params.h"
#include "ink_stroke_modeler/types.h"

namespace ink {
namespace stroke_model {

// Returns the number of input steps to be interpolated (and therefore the
// number of outputs to be modeled) between two inputs.
absl::StatusOr<int> NumberOfStepsBetweenInputs(
    const TipState& tip_state, const Input& start, const Input& end,
    const SamplingParams& sampling_params,
    const PositionModelerParams& position_modeler_params);

// This class models the movement of the pen tip based on the laws of motion.
// The pen tip is represented as a mass, connected by a spring to a moving
// anchor; as the anchor moves, it drags the pen tip along behind it.
class PositionModeler {
 public:
  void Reset(const TipState& state, PositionModelerParams params) {
    params_ = params;
    state_ = state;
    saved_state_.reset();
  }

  // Given the position of the anchor and the time, updates the model and
  // returns the new state of the pen tip.
  TipState Update(Vec2 anchor_position, Time time);

  const TipState& CurrentState() const { return state_; }
  const PositionModelerParams& Params() const { return params_; }

  // This helper function linearly interpolates between the between the start
  // and end anchor position and time, updating the model at each step and
  // storing the result in the given output iterator.
  //
  // NOTE: Because the expected use case is to repeatedly call this function on
  // a sequence of anchor positions/times, the start position/time is not sent
  // to the model. This prevents us from duplicating those inputs, but it does
  // mean that the first input must be provided on its own, via either Reset()
  // or Update(). This also means that the interpolation values are
  // (1 ... n) / n, as opposed to (0 ... (n - 1)) / (n - 1).
  //
  // Template parameter OutputIt is expected to be an output iterator over
  // TipState.
  template <typename OutputIt>
  void UpdateAlongLinearPath(Vec2 start_anchor_position, Time start_time,
                             Vec2 end_anchor_position, Time end_time,
                             int n_samples, OutputIt output) {
    for (int i = 1; i <= n_samples; ++i) {
      float interp_value = static_cast<float>(i) / n_samples;
      Vec2 position =
          Interp(start_anchor_position, end_anchor_position, interp_value);
      Time time = Interp(start_time, end_time, interp_value);
      *output++ = Update(position, time);
    }
  }

  // This helper function models the end of the stroke, by repeatedly updating
  // with the final anchor position. It attempts to stop at the closest point to
  // the anchor, by checking if it has overshot, and retrying with successively
  // smaller time steps.
  //
  // It halts when any of these three conditions is met:
  // - It has taken more than max_iterations steps (including discarded steps)
  // - The distance between the current state and the anchor is less than
  //   stop_distance
  // - The distance between the previous state and the current state is less
  //   than stop_distance
  //
  // Template parameter OutputIt is expected to be an output iterator over
  // TipState.
  template <typename OutputIt>
  void ModelEndOfStroke(Vec2 anchor_position, Duration delta_time,
                        int max_iterations, float stop_distance,
                        OutputIt output) {
    for (int i = 0; i < max_iterations; ++i) {
      // The call to Update modifies the state, so we store a copy of the
      // previous state so we can retry with a smaller step if necessary.
      const TipState previous_state = state_;
      TipState candidate =
          Update(anchor_position, previous_state.time + delta_time);
      if (Distance(previous_state.position, candidate.position) <
          stop_distance) {
        // We're no longer making any significant progress, which means that
        // we're about as close as we can get without looping around.
        return;
      }

      float closest_t = NearestPointOnSegment(
          previous_state.position, candidate.position, anchor_position);
      if (closest_t < 1) {
        // We're overshot the anchor, retry with a smaller step.
        delta_time *= .5;
        state_ = previous_state;
        continue;
      }
      *output++ = candidate;

      if (Distance(candidate.position, anchor_position) < stop_distance) {
        // We're within tolerance of the anchor.
        return;
      }
    }
  }

  // Saves the current state of the position modeler. See comment on
  // StrokeModeler::Save() for more details.
  void Save() { saved_state_ = state_; }

  // Restores the saved state of the position modeler. See comment on
  // StrokeModeler::Restore() for more details.
  void Restore() {
    if (saved_state_.has_value()) state_ = *saved_state_;
  }

 private:
  PositionModelerParams params_;
  TipState state_;
  std::optional<TipState> saved_state_;
};

}  // namespace stroke_model
}  // namespace ink

#endif  // INK_STROKE_MODELER_INTERNAL_POSITION_MODELER_H_
