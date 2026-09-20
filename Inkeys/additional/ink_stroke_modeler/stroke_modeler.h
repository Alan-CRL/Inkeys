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

#ifndef INK_STROKE_MODELER_STROKE_MODELER_H_
#define INK_STROKE_MODELER_STROKE_MODELER_H_

#include <memory>
#include <optional>
#include <vector>

#include "absl/status/status.h"
#include "ink_stroke_modeler/internal/internal_types.h"
#include "ink_stroke_modeler/internal/loop_contraction_mitigation_modeler.h"
#include "ink_stroke_modeler/internal/position_modeler.h"
#include "ink_stroke_modeler/internal/prediction/input_predictor.h"
#include "ink_stroke_modeler/internal/stylus_state_modeler.h"
#include "ink_stroke_modeler/internal/wobble_smoother.h"
#include "ink_stroke_modeler/params.h"
#include "ink_stroke_modeler/types.h"

namespace ink {
namespace stroke_model {

// This class models a stroke from a raw input stream. The modeling is performed
// in several stages, which are delegated to component classes:
// - Wobble Smoothing: Dampens high-frequency noise from quantization error.
// - Position Modeling: Models the pen tip as a mass, connected by a spring, to
//   a moving anchor.
// - Stylus State Modeling: Constructs stylus states for modeled positions by
//   interpolating over the raw input.
//
// Additionally, this class provides prediction of the modeled stroke.
//
// StrokeModeler is completely unit-agnostic. That is, it doesn't matter what
// units or coordinate-system the input is given in; the output will be given in
// the same coordinate-system and units.
class StrokeModeler {
 public:
  // Clears any in-progress stroke, and initializes (or re-initializes) the
  // model with the given parameters. Returns an error if the parameters are
  // invalid.
  absl::Status Reset(const StrokeModelParams& stroke_model_params);

  // Clears any in-progress stroke, keeping the same model parameters.
  // Returns an error if the model has not yet been initialized via
  // Reset(StrokeModelParams).
  absl::Status Reset();

  // Updates the model with a raw input, and appends newly generated Results
  // to the results vector. Any previously generated Result values remain valid.
  // (This does not require that any previous results returned remain in the
  // results vector, the vector is appended to without examining the existing
  // contents.)
  //
  // The function fills an out parameter instead of returning by value to allow
  // the caller to reuse allocations. Update is expected to be called 10s to
  // 100s of times over the course of a stroke, producing a relatively small
  // result each time.
  //
  // Returns an error if the the model has not yet been initialized (via Reset)
  // or if the input stream is malformed (e.g decreasing time, Up event before
  // Down event). In that case, results will be unmodified after the call.
  //
  // If this does not return an error, results will contain at least one Result,
  // and potentially more than one if the inputs are slower than the minimum
  // output rate.
  absl::Status Update(const Input& input, std::vector<Result>& results);

  // Models the given input prediction without changing the internal model
  // state, and then clears and fills the results parameter with the new
  // predicted Results. Any previously generated prediction Results are no
  // longer valid.
  //
  // Returns an error if the the model has not yet been initialized (via Reset),
  // if there is no stroke in progress, or if prediction has been disabled . In
  // that case, results will be empty after the call.
  //
  // The output is limited to results where the predictor has sufficient
  // confidence.
  absl::Status Predict(std::vector<Result>& results) const;

  // Saves the current modeler state.
  //
  // Subsequent updates can be undone by calling Restore(), until a call to
  // Reset() clears the stroke or a call to Save() sets a new saved state.
  void Save();

  // Restores the saved state of the modeler.
  //
  // Discards the portion of input after the last call to Save(). This does not
  // clear or modify the saved state. Does nothing if Save() has not been called
  // for this stroke.
  void Restore();

 private:
  void ResetInternal();

  absl::Status ProcessDownEvent(const Input& input,
                                std::vector<Result>& results);
  absl::Status ProcessMoveEvent(const Input& input,
                                std::vector<Result>& results);
  absl::Status ProcessUpEvent(const Input& input, std::vector<Result>& results);

  std::unique_ptr<InputPredictor> predictor_;

  std::optional<StrokeModelParams> stroke_model_params_;

  WobbleSmoother wobble_smoother_;
  PositionModeler position_modeler_;
  StylusStateModeler stylus_state_modeler_;
  LoopContractionMitigationModeler loop_contraction_mitigation_modeler_;

  // This buffer is used as optimization to avoid re-allocating the vector in
  // the predictor but doesn't hold state between calls, so can be mutable.
  mutable std::vector<TipState> tip_state_buffer_;

  struct InputAndCorrectedPosition {
    Input input;
    Vec2 corrected_position{0};
  };
  std::optional<InputAndCorrectedPosition> last_input_;

  std::unique_ptr<InputPredictor> saved_predictor_;
  std::optional<InputAndCorrectedPosition> saved_last_input_;
  bool save_active_ = false;
};

}  // namespace stroke_model
}  // namespace ink

#endif  // INK_STROKE_MODELER_STROKE_MODELER_H_
