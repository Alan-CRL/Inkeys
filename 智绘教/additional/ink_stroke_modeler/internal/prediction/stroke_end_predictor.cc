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

#include "ink_stroke_modeler/internal/prediction/stroke_end_predictor.h"

#include <iterator>
#include <optional>
#include <vector>

#include "ink_stroke_modeler/internal/internal_types.h"
#include "ink_stroke_modeler/internal/position_modeler.h"
#include "ink_stroke_modeler/types.h"

namespace ink {
namespace stroke_model {

void StrokeEndPredictor::Update(Vec2 position, Time time) {
  last_position_ = position;
}

void StrokeEndPredictor::ConstructPrediction(
    const TipState &last_state, std::vector<TipState> &prediction) const {
  prediction.clear();
  if (!last_position_) {
    // We don't yet have enough data to construct a prediction.
    return;
  }

  prediction.reserve(sampling_params_.end_of_stroke_max_iterations);
  PositionModeler modeler;
  modeler.Reset(last_state, position_modeler_params_);
  modeler.ModelEndOfStroke(*last_position_,
                           Duration(1. / sampling_params_.min_output_rate),
                           sampling_params_.end_of_stroke_max_iterations,
                           sampling_params_.end_of_stroke_stopping_distance,
                           std::back_inserter(prediction));
}

}  // namespace stroke_model
}  // namespace ink
