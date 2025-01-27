//  Copyright 2021 PEI Weicheng and JIANG Yuyan
#ifndef MINI_INTEGRATOR_FUNCTION_HPP_
#define MINI_INTEGRATOR_FUNCTION_HPP_

#include <concepts>

#include <cmath>

#include <type_traits>
#include <utility>

#include "mini/algebra/eigen.hpp"

namespace mini {
namespace integrator {

/**
 * @brief Integrate a callable object defined in parametric (local) coordinates by an Integrator object.
 * 
 * @tparam LocalToValue the type of the integrand
 * @tparam Integrator the type of the integrator
 * @param local_to_value the integrand which takes local coordinates as arguments
 * @param integrator the Integrator object
 * @return the value of the integral
 */
template <typename LocalToValue, typename Integrator>
auto Quadrature(LocalToValue &&local_to_value, const Integrator &integrator) {
  using Local = typename Integrator::Local;
  using Value = std::invoke_result_t<LocalToValue, Local>;
  Value sum; algebra::SetZero(&sum);
  auto n = integrator.CountPoints();
  for (int i = 0; i < n; ++i) {
    auto f_val = local_to_value(integrator.GetLocal(i));
    f_val *= integrator.GetLocalWeight(i);
    sum += f_val;
  }
  return sum;
}

/**
 * @brief Integrate a callable object defined in physical (global) coordinates by an Integrator object.
 * 
 * @tparam GlobalToValue the type of the integrand
 * @tparam Integrator the type of the integrator
 * @param global_to_value the integrand which takes global coordinates as arguments
 * @param integrator the Integrator object
 * @return the value of the integral
 */
template <typename GlobalToValue, typename Integrator>
auto Integrate(GlobalToValue &&global_to_value, const Integrator &integrator) {
  using Global = typename Integrator::Global;
  using Value = std::invoke_result_t<GlobalToValue, Global>;
  Value sum; algebra::SetZero(&sum);
  auto n = integrator.CountPoints();
  for (int i = 0; i < n; ++i) {
    auto f_val = global_to_value(integrator.GetGlobal(i));
    f_val *= integrator.GetGlobalWeight(i);
    sum += f_val;
  }
  return sum;
}

/**
 * @brief Calculate the inner-product of two functions defined in physical (global) coordinates by an Integrator object.
 * 
 * @tparam Func1 the type of the first function
 * @tparam Func2 the type of the second function
 * @tparam Integrator the type of the integrator
 * @param f1 the first function
 * @param f2 the second function
 * @param integrator the Integrator object
 * @return the value of the innerproduct
 */
template <typename Func1, typename Func2, typename Integrator>
auto Innerprod(Func1 &&f1, Func2 &&f2, const Integrator &integrator) {
  using Global = typename Integrator::Global;
  static_assert(std::regular_invocable<Func1, Global>);
  static_assert(std::regular_invocable<Func2, Global>);
  return Integrate([&f1, &f2](const Global &xyz_global){
    return f1(xyz_global) * f2(xyz_global);
  }, integrator);
}

/**
 * @brief Calculate the \f$L_p\f$-norm of a function by an Integrator object.
 * 
 * If the function returns a matrix, the value of the norm is also a matrix, which is obtained in a coeff-wise way.
 * 
 * @tparam GlobalToValue type of the function
 * @tparam Integrator the type of the integrator
 * @param global_to_value the function
 * @param integrator the Integrator object
 * @return the value of the norm
 */
template <typename GlobalToValue, typename Integrator, int P = 2>
auto Norm(GlobalToValue &&global_to_value, const Integrator &integrator) {
  using Global = typename Integrator::Global;
  using Value = std::invoke_result_t<GlobalToValue, Global>;
  auto integral = Integrate([&global_to_value](Global const &global){
    return std::pow(std::abs(global_to_value(global)), P);
  }, integrator);
  constexpr double one_over_p = 1.0 / P;
  return std::pow(integral, one_over_p);
}

/**
 * @brief Calculate the \f$L_p\f$-distance between two scalar-valued functions by an Integrator object.
 * 
 * @tparam Func1 
 * @tparam Func2 
 * @tparam Integrator 
 * @tparam P 
 * @param func1 
 * @param func2 
 * @param integrator 
 * @return 
 */
template <typename Func1, typename Func2, typename Integrator, int P = 2>
auto Distance(Func1 &&func1, Func2 &&func2, const Integrator &integrator) {
  using Global = typename Integrator::Global;
  using Value1 = std::invoke_result_t<Func1, Global>;
  using Value2 = std::invoke_result_t<Func2, Global>;
  static_assert(std::is_same_v<Value1, Value2>);
  auto diff = [&func1, &func2](Global const &global) -> Value1 {
    return func1(global) - func2(global);
  };
  return Norm(diff, integrator);
}

/**
 * @brief Change a group of linearly independent functions into an orthonormal basis.
 * 
 * @tparam Basis the type of the basis
 * @tparam Integrator the type of the integrator
 * @param basis the basis to be orthonormalized, whose components are linearly independent from each other
 * @param integrator the Integrator object
 */
template <class Basis, class Integrator>
void OrthoNormalize(Basis *basis, const Integrator &integrator) {
  constexpr int N = Basis::N;
  using MatNxN = typename Basis::MatNxN;
  using Global = typename Integrator::Global;
  using Scalar = typename Integrator::Real;
  MatNxN S; S.setIdentity();
  auto A = Integrate([basis](const Global &xyz){
    auto col = (*basis)(xyz);
    MatNxN result = col * col.transpose();
    return result;
  }, integrator);
  S(0, 0) = 1 / std::sqrt(A(0, 0));
  for (int i = 1; i < N; ++i) {
    for (int j = 0; j < i; ++j) {
      Scalar temp = 0;
      for (int k = 0; k <= j; ++k) {
        temp += S(j, k) * A(k, i);
      }
      for (int l = 0; l <= j; ++l) {
        S(i, l) -= temp * S(j, l);
      }
    }
    Scalar norm_sq = 0;
    for (int j = 0; j <= i; ++j) {
      Scalar sum = 0, Sij = S(i, j);
      for (int k = 0; k < j; ++k) {
        sum += 2 * S(i, k) * A(k, j);
      }
      norm_sq += Sij * (Sij * A(j, j) + sum);
    }
    S.row(i) /= std::sqrt(norm_sq);
  }
  basis->Transform(algebra::GetLowerTriangularView(S));
}

}  // namespace integrator
}  // namespace mini

#endif  // MINI_INTEGRATOR_FUNCTION_HPP_
