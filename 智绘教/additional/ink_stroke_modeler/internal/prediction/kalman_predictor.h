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

#ifndef INK_STROKE_MODELER_INTERNAL_PREDICTION_KALMAN_PREDICTOR_H_
#define INK_STROKE_MODELER_INTERNAL_PREDICTION_KALMAN_PREDICTOR_H_

#include <deque>
#include <memory>
#include <optional>
#include <vector>

#include "ink_stroke_modeler/internal/internal_types.h"
#include "ink_stroke_modeler/internal/prediction/input_predictor.h"
#include "ink_stroke_modeler/internal/prediction/kalman_filter/axis_predictor.h"
#include "ink_stroke_modeler/params.h"
#include "ink_stroke_modeler/types.h"

namespace ink {
namespace stroke_model {

// This class constructs a prediction by using a pair of Kalman filters (one
// each for the x- and y-dimension) to model the true state of the tip, assuming
// that the data we receive contains some noise.
// To construct a prediction, we first fetch the estimation of the position,
// velocity, acceleration, and jerk from the Kalman filters. The prediction is
// then constructed in two parts: one cubic spline that connects the last tip
// state to the estimation, constructed from the positions and velocities at the
// endpoints; and one cubic spline that extends into the future, constructed
// from the estimated position, velocity, acceleration, and jerk.
class KalmanPredictor : public InputPredictor {
 public:
  explicit KalmanPredictor(const KalmanPredictorParams &predictor_params,
                           const SamplingParams &sampling_params)
      : predictor_params_(predictor_params),
        sampling_params_(sampling_params),
        x_predictor_(predictor_params_.process_noise,
                     predictor_params_.measurement_noise,
                     predictor_params_.min_stable_iteration),
        y_predictor_(predictor_params_.process_noise,
                     predictor_params_.measurement_noise,
                     predictor_params_.min_stable_iteration) {}

  void Reset() override;
  void Update(Vec2 position, Time time) override;
  void ConstructPrediction(const TipState &last_state,
                           std::vector<TipState> &prediction) const override;
  std::unique_ptr<InputPredictor> MakeCopy() const override {
    return std::make_unique<KalmanPredictor>(*this);
  }

  struct State {
    Vec2 position{0};
    Vec2 velocity{0};
    Vec2 acceleration{0};
    Vec2 jerk{0};
  };

  // Returns the current estimate of the tip's true state, as modeled by the
  // Kalman filters, or std::nullopt if the predictor does not yet have enough
  // data to make a reasonable estimate.
  std::optional<State> GetEstimatedState() const;

 private:
  bool IsStable() const {
    return x_predictor_.Stable() && y_predictor_.Stable();
  }

  static void ConstructCubicConnector(const TipState &last_tip_state,
                                      const State &estimated_state,
                                      const KalmanPredictorParams &params,
                                      Duration sample_dt,
                                      std::vector<TipState> *output);

  static void ConstructCubicPrediction(const State &estimated_state,
                                       const KalmanPredictorParams &params,
                                       Time start_time, Duration sample_dt,
                                       int n_samples,
                                       std::vector<TipState> *output);

  int NumberOfPointsToPredict(const State &estimated_state) const;

  KalmanPredictorParams predictor_params_;
  SamplingParams sampling_params_;

  std::optional<Vec2> last_position_received_;

  std::deque<Time> sample_times_;

  AxisPredictor x_predictor_;
  AxisPredictor y_predictor_;
};

}  // namespace stroke_model
}  // namespace ink
#endif  // INK_STROKE_MODELER_INTERNAL_PREDICTION_KALMAN_PREDICTOR_H_
