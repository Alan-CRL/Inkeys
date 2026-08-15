#include "ink_stroke_modeler/internal/loop_contraction_mitigation_modeler.h"

#include "ink_stroke_modeler/internal/utils.h"
#include "ink_stroke_modeler/params.h"
#include "ink_stroke_modeler/types.h"

namespace ink {
namespace stroke_model {

void LoopContractionMitigationModeler::Reset(
    const PositionModelerParams::LoopContractionMitigationParameters& params) {
  speed_samples_.clear();

  save_active_ = false;

  params_ = params;
}

float LoopContractionMitigationModeler::GetInterpolationValue() {
  if (speed_samples_.empty() || !params_.is_enabled) return 1;

  float sum = 0;
  for (const auto& speed_sample : speed_samples_) {
    sum += speed_sample.speed;
  }
  float average_speed = sum / speed_samples_.size();

  float source_ratio = Clamp01(InverseLerp(
      params_.speed_lower_bound, params_.speed_upper_bound, average_speed));
  return Interp(params_.interpolation_strength_at_speed_lower_bound,
                params_.interpolation_strength_at_speed_upper_bound,
                source_ratio);
}

float LoopContractionMitigationModeler::Update(Vec2 velocity, Time time) {
  if (!params_.is_enabled) return 1;
  // The moving average acts as a low-pass signal filter, removing
  // high-frequency fluctuations in the velocity.
  speed_samples_.push_back({.speed = velocity.Magnitude(), .time = time});
  while (!speed_samples_.empty() &&
         speed_samples_.back().time - speed_samples_.front().time >
             params_.min_speed_sampling_window) {
    speed_samples_.pop_front();
  }
  return GetInterpolationValue();
}

void LoopContractionMitigationModeler::Save() {
  saved_speed_samples_ = speed_samples_;
  save_active_ = true;
}

void LoopContractionMitigationModeler::Restore() {
  if (save_active_) speed_samples_ = saved_speed_samples_;
}

}  // namespace stroke_model
}  // namespace ink
