#ifndef INK_STROKE_MODELER_INTERNAL_LOOP_CONTRACTION_MITIGATION_MODELER_H_
#define INK_STROKE_MODELER_INTERNAL_LOOP_CONTRACTION_MITIGATION_MODELER_H_

#include <deque>

#include "ink_stroke_modeler/params.h"
#include "ink_stroke_modeler/types.h"

namespace ink {
namespace stroke_model {

class LoopContractionMitigationModeler {
 public:
  void Reset(
      const PositionModelerParams::LoopContractionMitigationParameters& params);

  // Updates the model with the position and time from the raw inputs, and
  // returns the interpolation value to be used when applying the loop
  // contraction mitigation based on the LoopContractionMitigationParameters.
  float Update(Vec2 velocity, Time time);

  // Returns the interpolation value based on the current set of available
  // speeds and the LoopContractionMitigationParameters.
  float GetInterpolationValue();

  // Saves the current state of the modeler. See comment on
  // StrokeModeler::Save() for more details.
  void Save();

  // Restores the saved state of the modeler. See comment on
  // StrokeModeler::Restore() for more details.
  void Restore();

 private:
  struct SpeedSample {
    float speed;
    Time time;
  };
  std::deque<SpeedSample> speed_samples_;

  // Use a deque + bool instead of optional<deque> for performance. A
  // std::deque, which has a non-trivial destructor that would deallocate its
  // capacity. This setup avoids extra calls to the destructor that would be
  // triggered by each call to std::optional::reset().
  std::deque<SpeedSample> saved_speed_samples_;
  bool save_active_ = false;

  PositionModelerParams::LoopContractionMitigationParameters params_;
};

}  // namespace stroke_model
}  // namespace ink

#endif  // INK_STROKE_MODELER_INTERNAL_LOOP_CONTRACTION_MITIGATION_MODELER_H_
