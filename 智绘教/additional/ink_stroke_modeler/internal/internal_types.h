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

#ifndef INK_STROKE_MODELER_INTERNAL_INTERNAL_TYPES_H_
#define INK_STROKE_MODELER_INTERNAL_INTERNAL_TYPES_H_

#include <string>

#include "ink_stroke_modeler/types.h"

namespace ink {
namespace stroke_model {

// This struct contains the position, velocity, and acceleration of the modeled
// pen tip at the indicated time.
struct TipState {
  Vec2 position{0};
  Vec2 velocity{0};
  Vec2 acceleration{0};
  Time time{0};
};

std::string ToFormattedString(const TipState& tip_state);

template <typename Sink>
void AbslStringify(Sink& sink, const TipState& tip_state) {
  sink.Append(ToFormattedString(tip_state));
}

// This struct contains information about the state of the stylus. See the
// corresponding fields on the Input struct for more info.
struct StylusState {
  float pressure = -1;
  float tilt = -1;
  float orientation = -1;
};

bool operator==(const StylusState& lhs, const StylusState& rhs);
std::string ToFormattedString(const StylusState& stylus_state);

template <typename Sink>
void AbslStringify(Sink& sink, const StylusState& stylus_state) {
  sink.Append(ToFormattedString(stylus_state));
}

////////////////////////////////////////////////////////////////////////////////
// Inline function definitions
////////////////////////////////////////////////////////////////////////////////

inline bool operator==(const StylusState& lhs, const StylusState& rhs) {
  return lhs.pressure == rhs.pressure && lhs.tilt == rhs.tilt &&
         lhs.orientation == rhs.orientation;
}

}  // namespace stroke_model
}  // namespace ink

#endif  // INK_STROKE_MODELER_INTERNAL_INTERNAL_TYPES_H_
