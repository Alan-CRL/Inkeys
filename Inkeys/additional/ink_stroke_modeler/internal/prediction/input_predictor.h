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

#ifndef INK_STROKE_MODELER_INTERNAL_PREDICTION_INPUT_PREDICTOR_H_
#define INK_STROKE_MODELER_INTERNAL_PREDICTION_INPUT_PREDICTOR_H_

#include <memory>
#include <vector>

#include "ink_stroke_modeler/internal/internal_types.h"
#include "ink_stroke_modeler/types.h"

namespace ink {
namespace stroke_model {

// Interface for input predictors that generate points based on past input.
class InputPredictor {
 public:
  virtual ~InputPredictor() {}

  // Resets the predictor's internal model.
  virtual void Reset() = 0;

  // Updates the predictor's internal model with the given input.
  virtual void Update(Vec2 position, Time time) = 0;

  // Constructs a prediction into the output parameter based on the given
  // last_state, based on the predictor's internal model. The result may be
  // empty if the predictor has not yet accumulated enough data, via Update(),
  // to construct a reasonable prediction.
  //
  // Subclasses are expected to maintain the following invariants:
  // - The prediction parameter is expected to be cleared by this function.
  // - The given state must not appear in the prediction.
  // - The time delta between each state in the prediction, and between the
  //   given state and the first predicted state, must conform to
  //   SamplingParams::min_output_rate.
  virtual void ConstructPrediction(const TipState& last_state,
                                   std::vector<TipState>& prediction) const = 0;

  // Returns a copy of the predictor, including any dynamic state.
  virtual std::unique_ptr<InputPredictor> MakeCopy() const = 0;
};

}  // namespace stroke_model
}  // namespace ink

#endif  // INK_STROKE_MODELER_INTERNAL_PREDICTION_INPUT_PREDICTOR_H_
