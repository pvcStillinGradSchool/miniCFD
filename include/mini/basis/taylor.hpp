//  Copyright 2023 PEI Weicheng
#ifndef MINI_BASIS_TAYLOR_HPP_
#define MINI_BASIS_TAYLOR_HPP_

#include <concepts>

#include <cassert>
#include <cmath>

#include <iostream>
#include <type_traits>

#include "mini/algebra/eigen.hpp"

namespace mini {
namespace basis {

/**
 * @brief The basis, formed by monomials, of the space spanned by polynomials.
 * 
 * @tparam S the type of coordinates
 * @tparam kDimensions the dimension of underlying space
 * @tparam kDegrees the maximum degree of its members
 */
template <std::floating_point S, int kDimensions, int kDegrees>
class Taylor;

template <std::floating_point S, int kDegrees>
class Taylor<S, 1, kDegrees> {
 public:
  // the maximum degree of members in this basis
  static constexpr int P = kDegrees;

  // the number of terms in this basis
  static constexpr int N = P + 1;

  using Scalar = S;
  using Vector = algebra::Vector<Scalar, N>;

  /**
   * @brief Get the values of all basis functions at an arbitrary point.
   * 
   * @param x the coordinate of the query point
   * @return Vector the values
   */
  static Vector GetValues(Scalar x) {
    Vector vec;
    Scalar x_power = 1;
    vec[0] = x_power;
    for (int k = 1; k < N; ++k) {
      vec[k] = (x_power *= x);
    }
    assert(std::abs(x_power - std::pow(x, P)) < 1e-14);
    return vec;
  }

  /**
   * @brief Get the k-th order derivatives of all basis functions at an arbitrary point.
   * 
   * @param x the coordinate of the query point
   * @param k the order of the derivatives to be taken
   * @return Vector the derivatives
   */
  static Vector GetDerivatives(int k, Scalar x) {
    assert(0 <= k && k <= P);
    Vector vec;
    vec.setZero();  // For all j < k, there is vec[j] = 0.
    auto factorial_j = std::tgamma(Scalar(k + 1));  // factorial(j == k)
    auto factorial_j_minus_k = Scalar(1);  // factorial(j - k == 0)
    // j * (j - 1) * ... * (j - k + 1) =
    vec[k] = factorial_j / factorial_j_minus_k;
    auto x_power = Scalar(1);
    for (int j = k + 1; j < N; ++j) {
      auto j_minus_k = j - k;
      factorial_j_minus_k *= j_minus_k;
      factorial_j *= j;
      x_power *= x;
      vec[j] = x_power * factorial_j / factorial_j_minus_k;
    }
    assert(std::abs(x_power - std::pow(x, P - k)) < 1e-14);
    assert(factorial_j == std::tgamma(P + 1));
    assert(factorial_j_minus_k == std::tgamma(P - k + 1));
    return vec;
  }
};

namespace {

template <int D>
constexpr int GetN(int P);

template <>
constexpr int GetN<1>(int P) {
  return 1 + P;
}

template <int D>
constexpr int GetN(int P) {
  return (GetN<D - 1>(P) * (D + P)) / D;
}

}

template <std::floating_point S, int kDimensions, int kDegrees>
class Taylor {
 public:
  static constexpr int P = kDegrees;
  static constexpr int D = kDimensions;
  static constexpr int N = GetN<D>(P);  // the number of components
  using Scalar = S;
  using MatNx1 = algebra::Matrix<Scalar, N, 1>;
  using Coord = algebra::Matrix<Scalar, D, 1>;

  static MatNx1 GetValue(const Coord &coord) {
    return _GetValue(coord);
  }

 private:
  static MatNx1 _GetValue(const Coord &xy) requires(kDegrees == 0) {
    return { 1 };
  }

  static MatNx1 _GetValue(const Coord &xy)
      requires(kDimensions == 2 && kDegrees == 1) {
    return { 1, xy[0], xy[1] };
  }

  static MatNx1 _GetValue(const Coord &xy)
      requires(kDimensions == 2 && kDegrees == 2) {
    auto x = xy[0], y = xy[1];
    return { 1, x, y, x * x, x * y, y * y };
  }

  static MatNx1 _GetValue(const Coord &xy)
      requires(kDimensions == 2 && kDegrees == 3) {
    auto x = xy[0], y = xy[1];
    auto x_x = x * x, x_y = x * y, y_y = y * y;
    return { 1, x, y, x_x, x_y, y_y,
        x_x * x, x_x * y, x * y_y, y * y_y };
  }

  static MatNx1 _GetValue(const Coord &xyz)
      requires(kDimensions == 3 && kDegrees == 1) {
    auto x = xyz[0], y = xyz[1], z = xyz[2];
    return { 1, x, y, z };
  }

  static MatNx1 _GetValue(const Coord &xyz)
      requires(kDimensions == 3 && kDegrees == 2) {
    auto x = xyz[0], y = xyz[1], z = xyz[2];
    return { 1, x, y, z, x * x, x * y, x * z, y * y, y * z, z * z };
  }

  static MatNx1 _GetValue(const Coord &xyz)
      requires(kDimensions == 3 && kDegrees == 3) {
    auto x = xyz[0], y = xyz[1], z = xyz[2];
    auto xx{x * x}, xy{x * y}, xz{x * z}, yy{y * y}, yz{y * z}, zz{z * z};
    return { 1, x, y, z, xx, xy, xz, yy, yz, zz,
        x * xx, x * xy, x * xz, x * yy, x * yz, x * zz,
        y * yy, y * yz, y * zz, z * zz };
  }

  static constexpr int X{1}, Y{2}, Z{3};
  static constexpr int XX{4}, XY{5}, XZ{6}, YY{7}, YZ{8}, ZZ{9};
  static constexpr int XXX{10}, XXY{11}, XXZ{12}, XYY{13}, XYZ{14}, XZZ{15};
  static constexpr int YYY{16}, YYZ{17}, YZZ{18}, ZZZ{19};

 public:
  template <typename MatKxN>
  static MatKxN GetPdvValue(const Coord &xyz, const MatKxN &coeff)
      requires(kDegrees == 0) {
    MatKxN res; res.setZero();
    return res;
  }

  template <int K>
  static auto GetGradValue(const Coord &xyz,
      const algebra::Matrix<Scalar, K, N> &coeff) requires(kDegrees == 0) {
    algebra::Matrix<Scalar, K, 3> res; res.setZero();
    return res;
  }

  template <typename MatKxN>
  static MatKxN GetPdvValue(const Coord &xyz, const MatKxN &coeff)
      requires(kDegrees == 1) {
    MatKxN res = coeff; res.col(0).setZero();
    return res;
  }

  template <int K>
  static auto GetGradValue(const Coord &xyz,
      const algebra::Matrix<Scalar, K, N> &coeff) requires(kDegrees == 1) {
    algebra::Matrix<Scalar, K, 3> res;
    // pdv_x
    res.col(0) = coeff.col(X);
    // pdv_y
    res.col(1) = coeff.col(Y);
    // pdv_z
    res.col(2) = coeff.col(Z);
    return res;
  }

  template <typename MatKxN>
  static MatKxN GetPdvValue(const Coord &xyz, const MatKxN &coeff)
      requires(kDegrees == 2) {
    auto x = xyz[0], y = xyz[1], z = xyz[2];
    MatKxN res = coeff; res.col(0).setZero();
    // pdv_x
    assert(res.col(X) == coeff.col(X));
    res.col(X) += coeff.col(XX) * (2 * x);
    res.col(X) += coeff.col(XY) * y;
    res.col(X) += coeff.col(XZ) * z;
    // pdv_y
    assert(res.col(Y) == coeff.col(Y));
    res.col(Y) += coeff.col(XY) * x;
    res.col(Y) += coeff.col(YY) * (2 * y);
    res.col(Y) += coeff.col(YZ) * z;
    // pdv_z
    assert(res.col(Z) == coeff.col(Z));
    res.col(Z) += coeff.col(XZ) * x;
    res.col(Z) += coeff.col(YZ) * y;
    res.col(Z) += coeff.col(ZZ) * (2 * z);
    // pdv_xx
    res.col(XX) += coeff.col(XX);
    assert(res.col(XX) == coeff.col(XX) * 2);
    // pdv_xy
    assert(res.col(XY) == coeff.col(XY));
    // pdv_xz
    assert(res.col(XZ) == coeff.col(XZ));
    // pdv_yy
    res.col(YY) += coeff.col(YY);
    assert(res.col(YY) == coeff.col(YY) * 2);
    // pdv_yz
    assert(res.col(YZ) == coeff.col(YZ));
    // pdv_zz
    res.col(ZZ) += coeff.col(ZZ);
    assert(res.col(ZZ) == coeff.col(ZZ) * 2);
    return res;
  }

  template <int K>
  static auto GetGradValue(const Coord &xyz,
      const algebra::Matrix<Scalar, K, N> &coeff) requires(kDegrees == 2) {
    auto x = xyz[0], y = xyz[1], z = xyz[2];
    algebra::Matrix<Scalar, K, 3> res;
    // pdv_x
    res.col(0) = coeff.col(X);
    res.col(0) += coeff.col(XX) * (2 * x);
    res.col(0) += coeff.col(XY) * y;
    res.col(0) += coeff.col(XZ) * z;
    // pdv_y
    res.col(1) = coeff.col(Y);
    res.col(1) += coeff.col(XY) * x;
    res.col(1) += coeff.col(YY) * (2 * y);
    res.col(1) += coeff.col(YZ) * z;
    // pdv_z
    res.col(2) = coeff.col(Z);
    res.col(2) += coeff.col(XZ) * x;
    res.col(2) += coeff.col(YZ) * y;
    res.col(2) += coeff.col(ZZ) * (2 * z);
    return res;
  }

  template <typename MatKxN>
  static MatKxN GetPdvValue(const Coord &xyz, const MatKxN &coeff)
      requires(kDegrees == 3) {
    auto x = xyz[0], y = xyz[1], z = xyz[2];
    auto xx{x * x}, xy{x * y}, xz{x * z}, yy{y * y}, yz{y * z}, zz{z * z};
    MatKxN res = coeff; res.col(0).setZero();
    // pdv_x
    assert(res.col(X) == coeff.col(X));
    res.col(X) += coeff.col(XX) * (2 * x);
    res.col(X) += coeff.col(XY) * y;
    res.col(X) += coeff.col(XZ) * z;
    res.col(X) += coeff.col(XXX) * (3 * xx);
    res.col(X) += coeff.col(XXY) * (2 * xy);
    res.col(X) += coeff.col(XXZ) * (2 * xz);
    res.col(X) += coeff.col(XYY) * yy;
    res.col(X) += coeff.col(XYZ) * yz;
    res.col(X) += coeff.col(XZZ) * zz;
    // pdv_y
    assert(res.col(Y) == coeff.col(Y));
    res.col(Y) += coeff.col(XY) * x;
    res.col(Y) += coeff.col(YY) * (2 * y);
    res.col(Y) += coeff.col(YZ) * z;
    res.col(Y) += coeff.col(XXY) * xx;
    res.col(Y) += coeff.col(XYZ) * xz;
    res.col(Y) += coeff.col(XYY) * (2 * xy);
    res.col(Y) += coeff.col(YYY) * (3 * yy);
    res.col(Y) += coeff.col(YYZ) * (2 * yz);
    res.col(Y) += coeff.col(YZZ) * zz;
    // pdv_z
    assert(res.col(Z) == coeff.col(Z));
    res.col(Z) += coeff.col(XZ) * x;
    res.col(Z) += coeff.col(YZ) * y;
    res.col(Z) += coeff.col(ZZ) * (2 * z);
    res.col(Z) += coeff.col(XXZ) * xx;
    res.col(Z) += coeff.col(XYZ) * xy;
    res.col(Z) += coeff.col(XZZ) * (2 * xz);
    res.col(Z) += coeff.col(YYZ) * yy;
    res.col(Z) += coeff.col(YZZ) * (2 * yz);
    res.col(Z) += coeff.col(ZZZ) * (3 * zz);
    // pdv_xx
    res.col(XX) += coeff.col(XXY) * y;
    res.col(XX) += coeff.col(XXZ) * z;
    res.col(XX) += coeff.col(XXX) * x * 3;
    res.col(XX) += res.col(XX);
    assert(1e-10 > (res.col(XX) - 2 * (coeff.col(XXX) * x * 3
        + coeff.col(XX) + coeff.col(XXY) * y + coeff.col(XXZ) * z)).norm());
    // pdv_xy
    res.col(XY) += coeff.col(XXY) * x * 2;
    res.col(XY) += coeff.col(XYY) * y * 2;
    res.col(XY) += coeff.col(XYZ) * z;
    assert(1e-10 > (res.col(XY) - (coeff.col(XY) + coeff.col(XYZ) * z
        + 2 * (coeff.col(XXY) * x + coeff.col(XYY) * y))).norm());
    // pdv_xz
    res.col(XZ) += coeff.col(XXZ) * x * 2;
    res.col(XZ) += coeff.col(XZZ) * z * 2;
    res.col(XZ) += coeff.col(XYZ) * y;
    assert(1e-10 > (res.col(XZ) - (coeff.col(XZ) + coeff.col(XYZ) * y
        + 2 * (coeff.col(XXZ) * x + coeff.col(XZZ) * z))).norm());
    // pdv_yy
    res.col(YY) += coeff.col(XYY) * x;
    res.col(YY) += coeff.col(YYZ) * z;
    res.col(YY) += coeff.col(YYY) * y * 3;
    res.col(YY) += res.col(YY);
    assert(1e-10 > (res.col(YY) - 2 * (coeff.col(YYY) * y * 3
        + coeff.col(YY) + coeff.col(XYY) * x + coeff.col(YYZ) * z)).norm());
    // pdv_yz
    res.col(YZ) += coeff.col(XYZ) * x;
    res.col(YZ) += coeff.col(YYZ) * y * 2;
    res.col(YZ) += coeff.col(YZZ) * z * 2;
    assert(1e-10 > (res.col(YZ) - (coeff.col(YZ) + coeff.col(XYZ) * x
        + 2 * (coeff.col(YYZ) * y + coeff.col(YZZ) * z))).norm());
    // pdv_zz
    res.col(ZZ) += coeff.col(XZZ) * x;
    res.col(ZZ) += coeff.col(YZZ) * y;
    res.col(ZZ) += coeff.col(ZZZ) * z * 3;
    res.col(ZZ) += res.col(ZZ);
    assert(1e-10 > (res.col(ZZ) - 2 * (coeff.col(ZZZ) * z * 3
        + coeff.col(ZZ) + coeff.col(XZZ) * x + coeff.col(YZZ) * y)).norm());
    // pdv_xxx
    res.col(XXX) *= 6;
    assert(res.col(XXX) == coeff.col(XXX) * 6);
    // pdv_xxy
    res.col(XXY) += res.col(XXY);
    assert(res.col(XXY) == coeff.col(XXY) * 2);
    // pdv_xxz
    res.col(XXZ) += res.col(XXZ);
    assert(res.col(XXZ) == coeff.col(XXZ) * 2);
    // pdv_xyy
    res.col(XYY) += res.col(XYY);
    assert(res.col(XYY) == coeff.col(XYY) * 2);
    // pdv_xyz
    assert(res.col(XYZ) == coeff.col(XYZ));
    // pdv_xzz
    res.col(XZZ) += res.col(XZZ);
    assert(res.col(XZZ) == coeff.col(XZZ) * 2);
    // pdv_yyy
    res.col(YYY) *= 6;
    assert(res.col(YYY) == coeff.col(YYY) * 6);
    // pdv_yyz
    res.col(YYZ) += res.col(YYZ);
    assert(res.col(YYZ) == coeff.col(YYZ) * 2);
    // pdv_yzz
    res.col(YZZ) += res.col(YZZ);
    assert(res.col(YZZ) == coeff.col(YZZ) * 2);
    // pdv_zzz
    res.col(ZZZ) *= 6;
    assert(res.col(ZZZ) == coeff.col(ZZZ) * 6);
    return res;
  }

  template <int K>
  static auto GetGradValue(const Coord &xyz,
      const algebra::Matrix<Scalar, K, N> &coeff) requires(kDegrees == 3) {
    auto x = xyz[0], y = xyz[1], z = xyz[2];
    auto xx{x * x}, xy{x * y}, xz{x * z}, yy{y * y}, yz{y * z}, zz{z * z};
    algebra::Matrix<Scalar, K, 3> res;
    // pdv_x
    res.col(0) = coeff.col(X);
    res.col(0) += coeff.col(XX) * (2 * x);
    res.col(0) += coeff.col(XY) * y;
    res.col(0) += coeff.col(XZ) * z;
    res.col(0) += coeff.col(XXX) * (3 * xx);
    res.col(0) += coeff.col(XXY) * (2 * xy);
    res.col(0) += coeff.col(XXZ) * (2 * xz);
    res.col(0) += coeff.col(XYY) * yy;
    res.col(0) += coeff.col(XYZ) * yz;
    res.col(0) += coeff.col(XZZ) * zz;
    // pdv_y
    res.col(1) = coeff.col(Y);
    res.col(1) += coeff.col(XY) * x;
    res.col(1) += coeff.col(YY) * (2 * y);
    res.col(1) += coeff.col(YZ) * z;
    res.col(1) += coeff.col(XXY) * xx;
    res.col(1) += coeff.col(XYZ) * xz;
    res.col(1) += coeff.col(XYY) * (2 * xy);
    res.col(1) += coeff.col(YYY) * (3 * yy);
    res.col(1) += coeff.col(YYZ) * (2 * yz);
    res.col(1) += coeff.col(YZZ) * zz;
    // pdv_z
    res.col(2) = coeff.col(Z);
    res.col(2) += coeff.col(XZ) * x;
    res.col(2) += coeff.col(YZ) * y;
    res.col(2) += coeff.col(ZZ) * (2 * z);
    res.col(2) += coeff.col(XXZ) * xx;
    res.col(2) += coeff.col(XYZ) * xy;
    res.col(2) += coeff.col(XZZ) * (2 * xz);
    res.col(2) += coeff.col(YYZ) * yy;
    res.col(2) += coeff.col(YZZ) * (2 * yz);
    res.col(2) += coeff.col(ZZZ) * (3 * zz);
    return res;
  }
};

}  // namespace basis
}  // namespace mini

#endif  // MINI_BASIS_TAYLOR_HPP_
