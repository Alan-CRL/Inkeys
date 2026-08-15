#ifndef INK_STROKE_MODELER_INTERNAL_VALIDATION_H_
#define INK_STROKE_MODELER_INTERNAL_VALIDATION_H_

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"

template <typename T>
absl::Status ValidateIsFiniteNumber(T value, absl::string_view label) {
  // std::isnan(integer value) fails to compile with Lexan C++20
  // (b/329239835), so only call std::isnan for floating point values.
  if constexpr (std::is_floating_point_v<T>) {
    if (std::isnan(value)) {
      return absl::InvalidArgumentError(absl::Substitute("$0 is NaN", label));
    }
    if (std::isinf(value)) {
      return absl::InvalidArgumentError(
          absl::Substitute("$0 is infinite", label));
    }
  }
  return absl::OkStatus();
}

template <typename T>
absl::Status ValidateGreaterThanZero(T value, absl::string_view label) {
  if constexpr (std::is_floating_point_v<T>) {
    if (absl::Status status = ValidateIsFiniteNumber(value, label);
        !status.ok()) {
      return status;
    }
  }
  if (value <= 0) {
    return absl::InvalidArgumentError(absl::Substitute(
        "$0 must be greater than zero. Actual value: $1", label, value));
  }
  return absl::OkStatus();
}

template <typename T>
absl::Status ValidateGreaterThanOrEqualToZero(T value,
                                              absl::string_view label) {
  if (absl::Status status = ValidateIsFiniteNumber(value, label);
      !status.ok()) {
    return status;
  }
  if (value < 0) {
    return absl::InvalidArgumentError(absl::Substitute(
        "$0 must be greater than or equal to zero. Actual value: $1", label,
        value));
  }
  return absl::OkStatus();
}

#endif  // INK_STROKE_MODELER_INTERNAL_VALIDATION_H_
