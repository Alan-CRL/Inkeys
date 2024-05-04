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

#ifndef INK_STROKE_MODELER_INTERNAL_WOBBLE_SMOOTHER_H_
#define INK_STROKE_MODELER_INTERNAL_WOBBLE_SMOOTHER_H_

#include <deque>

#include "ink_stroke_modeler/params.h"
#include "ink_stroke_modeler/types.h"

namespace ink {
namespace stroke_model {

// This class smooths "wobble" in input positions from high-frequency noise. It
// does so by maintaining a moving average of the positions, and interpolating
// between the given input and the moving average based on how quickly it's
// moving. When moving at a speed above the ceiling in the WobbleSmootherParams,
// the result will be the unmodified input; when moving at a speed below the
// floor, the result will be the moving average.
class WobbleSmoother {
 public:
  void Reset(const WobbleSmootherParams &params, Vec2 position, Time time);

  // Updates the average position and speed, and returns the smoothed position.
  Vec2 Update(Vec2 position, Time time);

  // Saves the current state of the wobble smoother. See comment on
  // StrokeModeler::Save() for more details.
  void Save();

  // Restores the saved state of the wobble smoother. See comment on
  // StrokeModeler::Restore() for more details.
  void Restore();

 private:
  struct Sample {
    Vec2 position{0, 0};
    Vec2 weighted_position{0, 0};
    float distance = 0;
    Duration duration{0};
    Time time{0};
  };

  struct State {
    std::deque<Sample> samples;
    Vec2 weighted_position_sum{0, 0};
    float distance_sum = 0;
    float duration_sum = 0;
  };

  State state_;

  // Use a State + bool instead of optional<State> for performance. State
  // contains a std::deque, which has a non-trivial destructor that would
  // deallocate its capacity. This setup avoids extra calls to the destructor
  // that would be triggered by each call to std::optional::reset().
  State saved_state_;
  bool save_active_ = false;

  WobbleSmootherParams params_;
};

}  // namespace stroke_model
}  // namespace ink

#endif  // INK_STROKE_MODELER_INTERNAL_WOBBLE_SMOOTHER_H_
