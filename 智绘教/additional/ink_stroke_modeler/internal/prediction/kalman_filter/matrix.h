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

#ifndef INK_STROKE_MODELER_INTERNAL_PREDICTION_KALMAN_FILTER_MATRIX_H_
#define INK_STROKE_MODELER_INTERNAL_PREDICTION_KALMAN_FILTER_MATRIX_H_

#include <array>
#include <cstddef>
#include <ostream>

namespace ink {
namespace stroke_model {

// These classes provide the matrix arithmetic needed for the Kalman filter.
//
// This is intentionally limited to just the required functions, so some
// common matrix arithmetic operations aren't present (e.g. inversion), and
// some operators' symmetric counterparts are missing (e.g. Vec4 * double is
// defined, but double * Vec4 is not).

// A double-precision vector in 4-dimensional space.
class Vec4 {
 public:
  constexpr Vec4() : Vec4(0, 0, 0, 0) {}
  constexpr Vec4(double x, double y, double z, double w)
      : array_({x, y, z, w}) {}

  double& operator[](size_t i) { return array_[i]; }
  double operator[](size_t i) const { return array_[i]; }

 private:
  std::array<double, 4> array_;
};

// A double-precision 4x4 matrix.
class Matrix4 {
 public:
  // Constructs an identity matrix.
  constexpr Matrix4()
      : Matrix4(1, 0, 0, 0,  //
                0, 1, 0, 0,  //
                0, 0, 1, 0,  //
                0, 0, 0, 1) {}

  // Constructs a matrix with the given values, in row-major order.
  constexpr Matrix4(double m00, double m01, double m02, double m03,  //
                    double m10, double m11, double m12, double m13,  //
                    double m20, double m21, double m22, double m23,  //
                    double m30, double m31, double m32, double m33)
      : array_{{{m00, m01, m02, m03},
                {m10, m11, m12, m13},
                {m20, m21, m22, m23},
                {m30, m31, m32, m33}}} {}

  // Constructs a matrix s.t. all values are zero.
  static constexpr Matrix4 Zero() {
    return {0, 0, 0, 0,  //
            0, 0, 0, 0,  //
            0, 0, 0, 0,  //
            0, 0, 0, 0};
  }

  // Returns a copy of the matrix with its rows and columns swapped, i.e.
  // original.At(i, j) == transposed.At(j, i).
  Matrix4 Transpose() const {
    Matrix4 result;
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) {
        result.At(i, j) = At(j, i);
      }
    }
    return result;
  }

  double& At(size_t row, size_t column) { return array_[row][column]; }
  double At(size_t row, size_t column) const { return array_[row][column]; }

 private:
  std::array<Vec4, 4> array_;
};

// Computes the dot product of two vectors. Given vectors a and b, this is
// equivalent to the matrix product:
// [a₀ a₁ a₂ a₃]⎡b₀⎤
//              ⎢b₁⎥
//              ⎢b₂⎥
//              ⎣b₃⎦
double DotProduct(const Vec4& lhs, const Vec4& rhs);

// Computes the outer product of two vectors. Given vectors a and b, this is
// equivalent to the matrix product:
// ⎡a₀⎤[b₀ b₁ b₂ b₃]
// ⎢a₁⎥
// ⎢a₂⎥
// ⎣a₃⎦
Matrix4 OuterProduct(const Vec4& lhs, const Vec4& rhs);

bool operator==(const Vec4& lhs, const Vec4& rhs);
bool operator!=(const Vec4& lhs, const Vec4& rhs);
Vec4 operator+(const Vec4& lhs, const Vec4& rhs);
Vec4 operator*(const Vec4& v, double k);
Vec4 operator/(const Vec4& v, double k);

bool operator==(const Matrix4& lhs, const Matrix4& rhs);
bool operator!=(const Matrix4& lhs, const Matrix4& rhs);
Matrix4 operator*(const Matrix4& lhs, const Matrix4& rhs);
Matrix4 operator+(const Matrix4& lhs, const Matrix4& rhs);
Matrix4 operator-(const Matrix4& lhs, const Matrix4& rhs);

Matrix4 operator*(const Matrix4& m, double k);
Vec4 operator*(const Matrix4& m, const Vec4& v);
Vec4 operator*(const Vec4& v, const Matrix4& m);

std::ostream& operator<<(std::ostream& stream, const Vec4& v);
std::ostream& operator<<(std::ostream& stream, const Matrix4& m);

// ============================================================================
//                       Inline function implementations
// ============================================================================

inline double DotProduct(const Vec4& lhs, const Vec4& rhs) {
  double result = 0;
  for (int i = 0; i < 4; ++i) result += lhs[i] * rhs[i];
  return result;
}

inline Matrix4 OuterProduct(const Vec4& lhs, const Vec4& rhs) {
  Matrix4 result = Matrix4::Zero();
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      result.At(i, j) = lhs[i] * rhs[j];
    }
  }
  return result;
}

inline bool operator==(const Vec4& lhs, const Vec4& rhs) {
  for (int i = 0; i < 4; ++i) {
    if (lhs[i] != rhs[i]) return false;
  }
  return true;
}
inline bool operator!=(const Vec4& lhs, const Vec4& rhs) {
  return !(lhs == rhs);
}

inline Vec4 operator+(const Vec4& lhs, const Vec4& rhs) {
  Vec4 result;
  for (int i = 0; i < 4; ++i) result[i] = lhs[i] + rhs[i];
  return result;
}

inline Vec4 operator*(const Vec4& v, double k) {
  Vec4 result;
  for (int i = 0; i < 4; ++i) result[i] = v[i] * k;
  return result;
}

inline Vec4 operator/(const Vec4& v, double k) {
  Vec4 result;
  for (int i = 0; i < 4; ++i) result[i] = v[i] / k;
  return result;
}

inline bool operator==(const Matrix4& lhs, const Matrix4& rhs) {
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      if (lhs.At(i, j) != rhs.At(i, j)) return false;
    }
  }
  return true;
}

inline bool operator!=(const Matrix4& lhs, const Matrix4& rhs) {
  return !(lhs == rhs);
}

inline Matrix4 operator*(const Matrix4& lhs, const Matrix4& rhs) {
  Matrix4 result = Matrix4::Zero();
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      for (int k = 0; k < 4; ++k) {
        result.At(i, j) += lhs.At(i, k) * rhs.At(k, j);
      }
    }
  }
  return result;
}

inline Matrix4 operator+(const Matrix4& lhs, const Matrix4& rhs) {
  Matrix4 result;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      result.At(i, j) = lhs.At(i, j) + rhs.At(i, j);
    }
  }
  return result;
}

inline Matrix4 operator-(const Matrix4& lhs, const Matrix4& rhs) {
  Matrix4 result;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      result.At(i, j) = lhs.At(i, j) - rhs.At(i, j);
    }
  }
  return result;
}

inline Matrix4 operator*(const Matrix4& m, double k) {
  Matrix4 result;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      result.At(i, j) = m.At(i, j) * k;
    }
  }
  return result;
}

inline Vec4 operator*(const Matrix4& m, const Vec4& v) {
  Vec4 result;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      result[i] += v[j] * m.At(i, j);
    }
  }
  return result;
}

inline Vec4 operator*(const Vec4& v, const Matrix4& m) {
  Vec4 result;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      result[i] += v[j] * m.At(j, i);
    }
  }
  return result;
}

inline std::ostream& operator<<(std::ostream& stream, const Vec4& v) {
  stream << "(" << v[0];
  for (int i = 1; i < 4; ++i) stream << ", " << v[i];
  return stream << ")";
}

inline std::ostream& operator<<(std::ostream& stream, const Matrix4& m) {
  for (int i = 0; i < 4; ++i) {
    stream << '\n' << m.At(i, 0);
    for (int j = 1; j < 4; ++j) stream << '\t' << m.At(i, j);
  }
  return stream;
}

}  // namespace stroke_model
}  // namespace ink

#endif  // INK_STROKE_MODELER_INTERNAL_PREDICTION_KALMAN_FILTER_MATRIX_H_
