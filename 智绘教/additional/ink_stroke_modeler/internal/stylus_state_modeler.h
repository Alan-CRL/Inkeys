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

#ifndef INK_STROKE_MODELER_INTERNAL_STYLUS_STATE_MODELER_H_
#define INK_STROKE_MODELER_INTERNAL_STYLUS_STATE_MODELER_H_

#include <deque>
#include <ostream>

#include "ink_stroke_modeler/internal/internal_types.h"
#include "ink_stroke_modeler/params.h"
#include "ink_stroke_modeler/types.h"

namespace ink {
namespace stroke_model {

// This class is used to model the state of the stylus for a given position,
// based on the state of the stylus at the original input points.
//
// The stylus is modeled by storing the last max_input_samples positions and
// states received via Update(); when queried, it treats the stored positions as
// a polyline, and finds the closest segment. The returned stylus state is a
// linear interpolation between the states associated with the endpoints of the
// segment, correcting angles to account for the "wraparound" that occurs at 0
// and 2π. The value used for interpolation is based on how far along the
// segment the closest point lies.
//
// If Update() is called with a state in which a field (i.e. pressure, tilt, or
// orientation) has a negative value (indicating no information), then the
// results of Query() will be -1 for that field until Reset() is called. This is
// tracked independently for each field; e.g., if you pass in tilt = -1, then
// pressure and orientation will continue to be interpolated normally.
class StylusStateModeler {
 public:
  // Adds a position and state pair to the model. During stroke modeling, these
  // values will be taken from the raw input.
  void Update(Vec2 position, const StylusState &state);

  // Clear the model and reset.
  void Reset(const StylusStateModelerParams &params);

  // Query the model for the state at the given position. During stroke
  // modeling, the position will be taken from the modeled input.
  //
  // If no Update() calls have been received since the last Reset(), this will
  // return {.pressure = -1, .tilt = -1, .orientation = -1}.
  StylusState Query(Vec2 position) const;

  // Saves the current state of the stylus state modeler. See comment on
  // StrokeModeler::Save() for more details.
  void Save();

  // Restores the saved state of the stylus state modeler. See comment on
  // StrokeModeler::Restore() for more details.
  void Restore();

 private:
  struct PositionAndStylusState {
    Vec2 position{0};
    StylusState state;

    PositionAndStylusState(Vec2 position_in, const StylusState &state_in)
        : position(position_in), state(state_in) {}
  };

  struct ModelerState {
    bool received_unknown_pressure = false;
    bool received_unknown_tilt = false;
    bool received_unknown_orientation = false;

    std::deque<PositionAndStylusState> positions_and_stylus_states;
  };

  ModelerState state_;

  // Use a ModelerState + bool instead of optional<ModelerState> for
  // performance. ModelerState contains a std::deque, which has a non-trivial
  // destructor that would deallocate its capacity. This setup avoids extra
  // calls to the destructor that would be triggered by each call to
  // std::optional::reset().
  ModelerState saved_state_;
  bool save_active_ = false;

  StylusStateModelerParams params_;
};

}  // namespace stroke_model
}  // namespace ink

#endif  // INK_STROKE_MODELER_INTERNAL_STYLUS_STATE_MODELER_H_
