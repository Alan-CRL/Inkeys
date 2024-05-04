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

#ifndef INK_STROKE_MODELER_INTERNAL_PREDICTION_STROKE_END_PREDICTOR_H_
#define INK_STROKE_MODELER_INTERNAL_PREDICTION_STROKE_END_PREDICTOR_H_

#include <optional>
#include <vector>

#include "ink_stroke_modeler/internal/internal_types.h"
#include "ink_stroke_modeler/internal/prediction/input_predictor.h"
#include "ink_stroke_modeler/params.h"
#include "ink_stroke_modeler/types.h"

namespace ink {
namespace stroke_model {

// This class constructs a prediction using the same PositionModeler class as
// the SpringBasedModeler, fixing the anchor position and allowing the stroke to
// "catch up". The way the prediction is constructed is very similar to how the
// SpringBasedModeler models the end of a stroke.
class StrokeEndPredictor : public InputPredictor {
 public:
  explicit StrokeEndPredictor(
      const PositionModelerParams &position_modeler_params,
      const SamplingParams &sampling_params)
      : position_modeler_params_(position_modeler_params),
        sampling_params_(sampling_params) {}

  void Reset() override { last_position_ = std::nullopt; }
  void Update(Vec2 position, Time time) override;
  void ConstructPrediction(const TipState &last_state,
                           std::vector<TipState> &prediction) const override;
  std::unique_ptr<InputPredictor> MakeCopy() const override {
    return std::make_unique<StrokeEndPredictor>(*this);
  }

 private:
  PositionModelerParams position_modeler_params_;
  SamplingParams sampling_params_;

  std::optional<Vec2> last_position_;
};

}  // namespace stroke_model
}  // namespace ink

#endif  // INK_STROKE_MODELER_INTERNAL_PREDICTION_STROKE_END_PREDICTOR_H_
