#include "ink_stroke_modeler/internal/internal_types.h"

#include <string>

#include "absl/strings/str_format.h"

namespace ink {
namespace stroke_model {

std::string ToFormattedString(const TipState &tip_state) {
  return absl::StrFormat("<TipState: pos: %v, vel: %v, acc: %v, time: %v>",
                         tip_state.position, tip_state.velocity,
                         tip_state.acceleration, tip_state.time);
}

std::string ToFormattedString(const StylusState &stylus_state) {
  return absl::StrFormat(
      "<StylusState: pressure: %v, tilt: %v, orientation: %v>",
      stylus_state.pressure, stylus_state.tilt, stylus_state.orientation);
}

}  // namespace stroke_model
}  // namespace ink
