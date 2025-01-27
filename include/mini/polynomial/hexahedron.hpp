//  Copyright 2023 PEI Weicheng
#ifndef MINI_POLYNOMIAL_HEXAHEDRON_HPP_
#define MINI_POLYNOMIAL_HEXAHEDRON_HPP_

#include <concepts>

#include <cmath>
#include <cstring>

#include <algorithm>
#include <iostream>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "mini/algebra/eigen.hpp"
#include "mini/integrator/cell.hpp"
#include "mini/integrator/hexahedron.hpp"
#include "mini/coordinate/element.hpp"
#include "mini/basis/lagrange.hpp"
#include "mini/constant/index.hpp"
#include "mini/polynomial/expansion.hpp"

namespace mini {
namespace polynomial {

using namespace mini::constant::index;

/**
 * @brief A vector-valued function interpolated on an given `basis::lagrange::Hexahedron` basis.
 * 
 * The interpolation nodes are collocated with quadrature points.
 * 
 * @tparam Gx  The quadrature rule in the 1st dimension.
 * @tparam Gy  The quadrature rule in the 2nd dimension.
 * @tparam Gz  The quadrature rule in the 3rd dimension.
 * @tparam kComponents  The number of function components.
 * @tparam kL  Formulate in local (parametric) space or not.
 */
template <class Gx, class Gy, class Gz, int kComponents, bool kL = false>
class Hexahedron : public Expansion<kComponents,
    basis::lagrange::Hexahedron<typename Gx::Scalar,
        Gx::Q - 1, Gy::Q - 1, Gz::Q - 1>> {
 public:
  static constexpr bool kLocal = kL;
  using IntegratorX = Gx;
  using IntegratorY = Gy;
  using IntegratorZ = Gz;
  using Integrator = integrator::Hexahedron<Gx, Gy, Gz>;
  using Scalar = typename Integrator::Scalar;
  using Local = typename Integrator::Local;
  using Global = typename Integrator::Global;
  using Coordinate = typename Integrator::Coordinate;
  using Jacobian = typename Coordinate::Jacobian;
  static constexpr int Px = Gx::Q - 1;
  static constexpr int Py = Gy::Q - 1;
  static constexpr int Pz = Gz::Q - 1;
  static constexpr int P = std::max({Px, Py, Pz});
  using Basis = basis::lagrange::Hexahedron<Scalar, Px, Py, Pz>;
  static constexpr int N = Basis::N;
  static constexpr int K = kComponents;
  static constexpr int D = 3;
  static constexpr int kFields = K * N;

 protected:
  using Mat6xN = algebra::Matrix<Scalar, 6, N>;
  using Mat6xK = algebra::Matrix<Scalar, 6, K>;
  using Mat3xK = algebra::Matrix<Scalar, 3, K>;
  using Mat3x3 = algebra::Matrix<Scalar, 3, 3>;
  using Mat3x1 = algebra::Matrix<Scalar, 3, 1>;
  using Mat1x3 = algebra::Matrix<Scalar, 1, 3>;

 public:
  using Coeff = algebra::Matrix<Scalar, K, N>;
  using Value = algebra::Matrix<Scalar, K, 1>;
  using Mat1xN = algebra::Matrix<Scalar, 1, N>;
  using Mat3xN = algebra::Matrix<Scalar, 3, N>;
  using Gradient = Mat3xK;
  using Hessian = Mat6xK;

 private:
  const Integrator *integrator_ptr_ = nullptr;

  struct E { };

  // cache for (kLocal == true)
  /* \f$ \det(\mathbf{J}) \f$ */
  [[no_unique_address]] std::conditional_t<true, std::array<Scalar, N>, E>
      jacobian_det_;
  /* \f$ \det(\mathbf{J})\,\mathbf{J}^{-1} \f$ */
  [[no_unique_address]] std::conditional_t<true, std::array<Jacobian, N>, E>
      jacobian_det_inv_;
  /* \f$ \mathbf{J}^{-1} \f$ */
  [[no_unique_address]] std::conditional_t<!kLocal, std::array<Jacobian, N>, E>
      jacobian_inv_;
  /* \f$ \begin{bmatrix}\partial_{\xi}\\ \partial_{\eta}\\ \partial_{\zeta} \end{bmatrix}\det(\mathbf{J}) \f$ */
  [[no_unique_address]] std::conditional_t<kLocal, std::array<Local, N>, E>
      jacobian_det_grad_;
  /* \f$ \underline{J}^{-T}\,J^{-1} \f$ */
  [[no_unique_address]] std::conditional_t<true, Jacobian[N], E>
      mat_after_hess_of_U_;
  /* \f$ \begin{bmatrix}\partial_{\xi}\\ \partial_{\eta}\\ \partial_{\zeta} \end{bmatrix} \qty(\underline{J}^{-T}\,J^{-1}) \f$ */
  [[no_unique_address]] std::conditional_t<true, Jacobian[N][3], E>
      mat_after_grad_of_U_;
  /* \f$ \underline{C}=\begin{bmatrix}\partial_{\xi}\,J & \partial_{\eta}\,J\end{bmatrix}\underline{J}^{-T}\,J^{-2} \f$ */
  [[no_unique_address]] std::conditional_t<kLocal, Mat1x3[N], E>
      mat_before_grad_of_U_;
  [[no_unique_address]] std::conditional_t<kLocal, Jacobian[N], E>
      mat_before_U_;

  // cache for (kLocal == false)
  [[no_unique_address]] std::conditional_t<kLocal, E, std::array<Mat3xN, N>>
      basis_global_gradients_;

  static constexpr void CheckSize() {
    constexpr size_t large_member_size = kLocal
        ? sizeof(std::array<Scalar, N>) + sizeof(std::array<Jacobian, N>)
            + sizeof(std::array<Local, N>)
            + sizeof(Jacobian[N]) + sizeof(Jacobian[N][3])
            + sizeof(Jacobian[N]) + sizeof(Local[N])
        : sizeof(std::array<Scalar, N>) + sizeof(std::array<Jacobian, N>)
            + sizeof(std::array<Jacobian, N>)
            + sizeof(Jacobian[N]) + sizeof(Jacobian[N][3])
            + sizeof(std::array<Mat3xN, N>);
    constexpr size_t all_member_size = large_member_size
        + sizeof(integrator_ptr_) + sizeof(Coeff);
    static_assert(sizeof(Hexahedron) >= all_member_size);
    static_assert(sizeof(Hexahedron) <= all_member_size + 16);
  }

  static const Basis basis_;
  static Basis BuildInterpolationBasis() {
    CheckSize();
    auto line_x = typename Basis::LineX{ Integrator::IntegratorX::BuildPoints() };
    auto line_y = typename Basis::LineY{ Integrator::IntegratorY::BuildPoints() };
    auto line_z = typename Basis::LineZ{ Integrator::IntegratorZ::BuildPoints() };
    return Basis(line_x, line_y, line_z);
  }

  static const std::array<Mat3xN, N> basis_local_gradients_;
  static std::array<Mat3xN, N> BuildBasisLocalGradients() {
    std::array<Mat3xN, N> gradients;
    auto basis = BuildInterpolationBasis();
    for (int ijk = 0; ijk < N; ++ijk) {
      auto &grad = gradients[ijk];
      auto [i, j, k] = basis.index(ijk);
      grad.row(0) = basis.GetDerivatives(1, 0, 0, i, j, k);
      grad.row(1) = basis.GetDerivatives(0, 1, 0, i, j, k);
      grad.row(2) = basis.GetDerivatives(0, 0, 1, i, j, k);
    }
    return gradients;
  }
  static const std::array<Mat6xN, N> basis_local_hessians_;
  static std::array<Mat6xN, N> BuildBasisLocalHessians() {
    std::array<Mat6xN, N> hessians;
    auto basis = BuildInterpolationBasis();
    for (int ijk = 0; ijk < N; ++ijk) {
      auto &hess = hessians[ijk];
      auto [i, j, k] = basis.index(ijk);
      hess.row(XX) = basis.GetDerivatives(2, 0, 0, i, j, k);
      hess.row(XY) = basis.GetDerivatives(1, 1, 0, i, j, k);
      hess.row(XZ) = basis.GetDerivatives(1, 0, 1, i, j, k);
      hess.row(YY) = basis.GetDerivatives(0, 2, 0, i, j, k);
      hess.row(YZ) = basis.GetDerivatives(0, 1, 1, i, j, k);
      hess.row(ZZ) = basis.GetDerivatives(0, 0, 2, i, j, k);
    }
    return hessians;
  }

 private:
  void InitializeJacobian() requires(kLocal) {
    for (int ijk = 0; ijk < N; ++ijk) {
      auto &local = integrator_ptr_->GetLocal(ijk);
      Jacobian mat = coordinate().LocalToJacobian(local);
      Jacobian inv = mat.inverse();
      Scalar det = mat.determinant();
      jacobian_det_[ijk] = det;
      jacobian_det_inv_[ijk] = det * inv;
      jacobian_det_grad_[ijk]
          = coordinate().LocalToJacobianDeterminantGradient(local);
      // cache for evaluating Hessian
      Jacobian inv_T = inv.transpose();
      mat_after_hess_of_U_[ijk] = inv_T / det;
      auto mat_grad = coordinate().LocalToJacobianGradient(local);
      Jacobian inv_T_grad[3];
      inv_T_grad[X] = -(inv * mat_grad[X] * inv).transpose();
      inv_T_grad[Y] = -(inv * mat_grad[Y] * inv).transpose();
      inv_T_grad[Z] = -(inv * mat_grad[Z] * inv).transpose();
      Mat3x1 const &det_grad = jacobian_det_grad_[ijk];
      Scalar det2 = det * det;
      mat_after_grad_of_U_[ijk][X] = inv_T_grad[X] / det
          + inv_T * (-det_grad[X] / det2);
      mat_after_grad_of_U_[ijk][Y] = inv_T_grad[Y] / det
          + inv_T * (-det_grad[Y] / det2);
      mat_after_grad_of_U_[ijk][Z] = inv_T_grad[Z] / det
          + inv_T * (-det_grad[Z] / det2);
      mat_before_grad_of_U_[ijk] = det_grad.transpose() * inv_T / det2;
      auto det_hess = coordinate().LocalToJacobianDeterminantHessian(local);
      auto &mat_before_U = mat_before_U_[ijk];
      mat_before_U(X, X) = det_hess[XX];
      mat_before_U(X, Y) = det_hess[XY];
      mat_before_U(X, Z) = det_hess[XZ];
      mat_before_U(Y, X) = det_hess[YX];
      mat_before_U(Y, Y) = det_hess[YY];
      mat_before_U(Y, Z) = det_hess[YZ];
      mat_before_U(Z, X) = det_hess[ZX];
      mat_before_U(Z, Y) = det_hess[ZY];
      mat_before_U(Z, Z) = det_hess[ZZ];
      mat_before_U *= inv_T / det2;
      Scalar det3 = det2 * det;
      mat_before_U.row(X) += det_grad.transpose() *
          (inv_T_grad[X] / det2 + inv_T * (-2 * det_grad[X] / det3));
      mat_before_U.row(Y) += det_grad.transpose() *
          (inv_T_grad[Y] / det2 + inv_T * (-2 * det_grad[Y] / det3));
      mat_before_U.row(Z) += det_grad.transpose() *
          (inv_T_grad[Z] / det2 + inv_T * (-2 * det_grad[Z] / det3));
    }
  }

  void InitializeJacobian() requires(!kLocal) {
    for (int ijk = 0; ijk < N; ++ijk) {
      auto &local = integrator_ptr_->GetLocal(ijk);
      Jacobian jacobian = coordinate().LocalToJacobian(local);
      Jacobian inv = jacobian.inverse();
      basis_global_gradients_[ijk] = inv * basis_local_gradients_[ijk];
      // cache for evaluating Hessian
      Scalar det = jacobian.determinant();
      jacobian_det_[ijk] = det;
      jacobian_det_inv_[ijk] = det * inv;
      jacobian_inv_[ijk] = inv;
      mat_after_hess_of_U_[ijk] = inv.transpose();
      auto mat_grad = coordinate().LocalToJacobianGradient(local);
      Jacobian (&inv_T_grad)[3] = mat_after_grad_of_U_[ijk];
      inv_T_grad[X] = -(inv * mat_grad[X] * inv).transpose();
      inv_T_grad[Y] = -(inv * mat_grad[Y] * inv).transpose();
      inv_T_grad[Z] = -(inv * mat_grad[Z] * inv).transpose();
    }
  }

 public:
  explicit Hexahedron(typename Integrator::Base const &integrator)
      : integrator_ptr_(dynamic_cast<const Integrator *>(&integrator)) {
    InitializeJacobian();
  }
  Hexahedron() = default;
  Hexahedron(const Hexahedron &) = default;
  Hexahedron(Hexahedron &&) noexcept = default;
  Hexahedron &operator=(const Hexahedron &) = default;
  Hexahedron &operator=(Hexahedron &&) noexcept = default;
  ~Hexahedron() noexcept = default;

  Hexahedron const &interpolation() const {
    return *this;
  }

 private:
  Value _LocalToValue(Local const &local) const requires(kLocal) {
    Value value = this->coeff() * basis_.GetValues(local).transpose();
    value /= coordinate().LocalToJacobian(local).determinant();
    return value;
  }
  Value _LocalToValue(Local const &local) const requires(!kLocal) {
    return this->coeff() * basis_.GetValues(local).transpose();
  }
 
 public:
  /**
   * @brief Get the value of \f$ u(x,y,z) \f$ at a point given by `Local`.
   * 
   */
  Value LocalToValue(Local const &local) const {
    return _LocalToValue(local);
  }

  void LocalToGlobalAndValue(Local const &local,
      Global *global, Value *value) const {
    *global = integrator().coordinate().LocalToGlobal(local);
    *value = LocalToValue(local);
  }

  /**
   * @brief Get the value of \f$ u(x,y,z) \f$ at a point given by `Global`.
   * 
   * It might be expensive since it has to call `Coordinate::GlobalToLocal` first.
   */
  Value GlobalToValue(Global const &global) const {
    Local local = coordinate().GlobalToLocal(global);
    return LocalToValue(local);
  }

 private:
  Value _GetValue(int i) const requires(kLocal) {
    return this->coeff_.col(i) / jacobian_det_[i];
  }
  Value _GetValue(int i) const requires(!kLocal) {
    return this->coeff_.col(i);
  }

 public:
  /**
   * @brief Get the value of \f$ u(x,y,z) \f$ at a given integratorian point.
   * 
   * @param i the index of the integratorian point
   * @return the value \f$ u(x_i,y_i,z_i) \f$
   */
  Value GetValue(int i) const {
    return _GetValue(i);
  }

 private:
  Coeff _GetValues() const requires(kLocal) {
    Coeff coeff;
    for (int j = 0; j < N; ++j) {
      coeff.col(j) = GetValue(j);
    }
    return coeff;
  }
  Coeff _GetValues() const requires(!kLocal) {
    return this->coeff_;
  }

 public:
  /**
   * @brief Get the values of \f$ u(x,y,z) \f$ at all integratorian points.
   * 
   */
  Coeff GetValues() const {
    return _GetValues();
  }

 private:
  void _SetValue(int i, Value const &value) requires(kLocal) {
    this->coeff_.col(i) = value;  // value in physical space
    this->coeff_.col(i) *= jacobian_det_[i];  // value in parametric space
  }
  void _SetValue(int i, Value const &value) requires(!kLocal) {
    this->coeff_.col(i) = value;
  }

 public:
  /**
   * @brief Set the value of \f$ u(x,y,z) \f$ at a given integratorian point.
   * 
   * @param i the index of the integratorian point
   * @param value the value \f$ u(x_i,y_i,z_i) \f$
   */
  void SetValue(int i, Value const &value) {
    _SetValue(i, value);
  }

  Mat1xN GlobalToBasisValues(Global const &global) const {
    Local local = coordinate().GlobalToLocal(global);
    return basis_.GetValues(local);
  }
  Mat3xN LocalToBasisGlobalGradients(Local const &local) const {
    Mat3xN grad;
    grad.row(0) = basis_.GetDerivatives(1, 0, 0, local);
    grad.row(1) = basis_.GetDerivatives(0, 1, 0, local);
    grad.row(2) = basis_.GetDerivatives(0, 0, 1, local);
    Jacobian jacobian = coordinate().LocalToJacobian(local);
    return jacobian.inverse() * grad;
  }
  Mat3xN GlobalToBasisGlobalGradients(Global const &global) const {
    Local local = coordinate().GlobalToLocal(global);
    return LocalToBasisGlobalGradients(local);
  }

  const Mat3xN &GetBasisLocalGradients(int ijk) const {
    return basis_local_gradients_[ijk];
  }

  const Mat3xN &GetBasisGlobalGradients(int ijk) const requires(!kLocal) {
    return basis_global_gradients_[ijk];
  }

  /**
   * @brief Get \f$ \begin{bmatrix}\partial_{\xi}\\ \partial_{\eta}\\ \cdots \end{bmatrix} U \f$ (when `kLocal == true`) or \f$ \begin{bmatrix}\partial_{\xi}\\ \partial_{\eta}\\ \cdots \end{bmatrix} u \f$ (when `kLocal == false`) at a given integratorian point.
   * 
   */
  Gradient GetLocalGradient(int ijk) const requires(true) {
    Gradient value_grad; value_grad.setZero();
    Mat3xN const &basis_grads = GetBasisLocalGradients(ijk);
    for (int abc = 0; abc < N; ++abc) {
      value_grad += basis_grads.col(abc) * this->coeff_.col(abc).transpose();
    }
    return value_grad;
  }
  Gradient LocalToLocalGradient(Local const &local) const requires(kLocal) {
    Gradient value_grad; value_grad.setZero();
    auto x = local[X], y = local[Y], z = local[Z];
    Mat3xN basis_grad;
    basis_grad.row(X) = basis_.GetDerivatives(1, 0, 0, x, y, z);
    basis_grad.row(Y) = basis_.GetDerivatives(0, 1, 0, x, y, z);
    basis_grad.row(Z) = basis_.GetDerivatives(0, 0, 1, x, y, z);
    for (int abc = 0; abc < N; ++abc) {
      value_grad += basis_grad.col(abc) * this->coeff_.col(abc).transpose();
    }
    return value_grad;
  }

 protected:
  Gradient _LocalToGlobalGradient(Local const &local) const requires(kLocal) {
    Gradient grad = LocalToLocalGradient(local);
    Jacobian mat = coordinate().LocalToJacobian(local);
    Scalar det = mat.determinant();
    Global det_grad = coordinate().LocalToJacobianDeterminantGradient(local);
    Value value = this->coeff_ * basis_.GetValues(local).transpose();
    grad -= (det_grad / det) * value.transpose();
    return mat.inverse() / det * grad;
  }

  Gradient _LocalToGlobalGradient(Local const &local) const requires(!kLocal) {
    return LocalToBasisGlobalGradients(local) * this->coeff_.transpose();
  }

 public:
  Gradient LocalToGlobalGradient(Local const &local) const {
    return _LocalToGlobalGradient(local);
  }

 protected:
  Gradient _GlobalToGlobalGradient(Global const &global) const
      requires(kLocal) {
    auto local = coordinate().GlobalToLocal(global);
    return LocalToGlobalGradient(local);
  }
  Gradient _GlobalToGlobalGradient(Global const &global) const
      requires(!kLocal) {
    return GlobalToBasisGlobalGradients(global) * this->coeff_.transpose();
  }

 public:
  Gradient GlobalToGlobalGradient(Global const &global) const {
    return _GlobalToGlobalGradient(global);
  }

 private:
  Gradient _GetGlobalGradient(int ijk) const requires(!kLocal) {
    Mat3xN const &basis_grad = GetBasisGlobalGradients(ijk);
    return basis_grad * this->coeff_.transpose();
  }
  std::pair<Value, Gradient> _GetGlobalValueGradient(int ijk) const
      requires(!kLocal) {
    return { GetValue(ijk), _GetGlobalGradient(ijk) };
  }

  Gradient _GetGlobalGradient(int ijk) const requires(kLocal) {
    return _GetGlobalGradient(GetValue(ijk), GetLocalGradient(ijk), ijk);
  }
  Gradient _GetGlobalGradient(Value const &value_ijk, Gradient local_grad_ijk,
      int ijk) const requires(kLocal) {
    auto &value_grad = local_grad_ijk;
    value_grad -= jacobian_det_grad_[ijk] * value_ijk.transpose();
    auto jacobian_det = jacobian_det_[ijk];
    value_grad /= (jacobian_det * jacobian_det);
    return GetJacobianAssociated(ijk) * value_grad;
  }
  std::pair<Value, Gradient> _GetGlobalValueGradient(int ijk) const
      requires(kLocal) {
    auto value_ijk = GetValue(ijk);
    auto local_grad_ijk = GetLocalGradient(ijk);
    return { value_ijk, _GetGlobalGradient(value_ijk, local_grad_ijk, ijk) };
  }

 public:
  /**
   * @brief Get \f$ \begin{bmatrix}\partial_{x}\\ \partial_{y}\\ \cdots \end{bmatrix} u \f$ at a given integratorian point.
   * 
   */
  Gradient GetGlobalGradient(int ijk) const {
    return _GetGlobalGradient(ijk);
  }

  /**
   * @brief Get \f$ \begin{bmatrix}\partial_{\xi}\partial_{\xi}\\ \partial_{\xi}\partial_{\eta}\\ \cdots \end{bmatrix} U \f$ at a given integratorian point.
   * 
   */
  Hessian GetLocalHessian(int ijk) const requires(true) {
    Mat6xK value_hess; value_hess.setZero();
    Mat6xN const &basis_hess = basis_local_hessians_[ijk];
    for (int abc = 0; abc < N; ++abc) {
      value_hess += basis_hess.col(abc) * this->coeff_.col(abc).transpose();
    }
    return value_hess;
  }
  /**
   * @brief Get \f$ \begin{bmatrix}\partial_{x}\partial_{x}\\ \partial_{x}\partial_{y}\\ \cdots \end{bmatrix} u \f$ at a given integratorian point.
   * 
   */
  Hessian GetGlobalHessian(int ijk) const requires(kLocal) {
    return _GetGlobalHessian(GetLocalGradient(ijk), ijk);
  }

 private:
  Hessian _GetGlobalHessian(Gradient const &local_grad_ijk, int ijk) const
      requires(!kLocal) {
    Hessian local_hess = GetLocalHessian(ijk);
    auto &global_hess = local_hess;
    for (int k = 0; k < K; ++k) {
      Mat3x3 scalar_hess;
      scalar_hess(X, X) = local_hess(XX, k);
      scalar_hess(X, Y) =
      scalar_hess(Y, X) = local_hess(XY, k);
      scalar_hess(X, Z) =
      scalar_hess(Z, X) = local_hess(XZ, k);
      scalar_hess(Y, Y) = local_hess(YY, k);
      scalar_hess(Y, Z) =
      scalar_hess(Z, Y) = local_hess(YZ, k);
      scalar_hess(Z, Z) = local_hess(ZZ, k);
      scalar_hess *= mat_after_hess_of_U_[ijk];
      Mat1x3 scalar_local_grad = local_grad_ijk.col(k);
      scalar_hess.row(X) += scalar_local_grad * mat_after_grad_of_U_[ijk][X];
      scalar_hess.row(Y) += scalar_local_grad * mat_after_grad_of_U_[ijk][Y];
      scalar_hess.row(Z) += scalar_local_grad * mat_after_grad_of_U_[ijk][Z];
      scalar_hess = jacobian_inv_[ijk] * scalar_hess;
      global_hess(XX, k) = scalar_hess(X, X);
      global_hess(XY, k) = scalar_hess(X, Y);
      global_hess(XZ, k) = scalar_hess(X, Z);
      global_hess(YY, k) = scalar_hess(Y, Y);
      global_hess(YZ, k) = scalar_hess(Y, Z);
      global_hess(ZZ, k) = scalar_hess(Z, Z);
    }
    return global_hess;
  }

  Hessian _GetGlobalHessian(Gradient const &local_grad_ijk, int ijk) const
      requires(kLocal) {
    Hessian local_hess = GetLocalHessian(ijk);
    auto &global_hess = local_hess;
    for (int k = 0; k < K; ++k) {
      Mat3x3 scalar_hess;
      scalar_hess(X, X) = local_hess(XX, k);
      scalar_hess(X, Y) =
      scalar_hess(Y, X) = local_hess(XY, k);
      scalar_hess(X, Z) =
      scalar_hess(Z, X) = local_hess(XZ, k);
      scalar_hess(Y, Y) = local_hess(YY, k);
      scalar_hess(Y, Z) =
      scalar_hess(Z, Y) = local_hess(YZ, k);
      scalar_hess(Z, Z) = local_hess(ZZ, k);
      scalar_hess *= mat_after_hess_of_U_[ijk];
      Mat1x3 scalar_local_grad = local_grad_ijk.col(k);
      scalar_hess.row(X) += scalar_local_grad * mat_after_grad_of_U_[ijk][X]
          - mat_before_grad_of_U_[ijk] * scalar_local_grad[X];
      scalar_hess.row(Y) += scalar_local_grad * mat_after_grad_of_U_[ijk][Y]
          - mat_before_grad_of_U_[ijk] * scalar_local_grad[Y];
      scalar_hess.row(Z) += scalar_local_grad * mat_after_grad_of_U_[ijk][Z]
          - mat_before_grad_of_U_[ijk] * scalar_local_grad[Z];
      Scalar scalar_local_val = this->coeff_(k, ijk);
      scalar_hess -= mat_before_U_[ijk] * scalar_local_val;
      scalar_hess = jacobian_det_inv_[ijk] * scalar_hess;
      scalar_hess /= jacobian_det_[ijk];
      global_hess(XX, k) = scalar_hess(X, X);
      global_hess(XY, k) = scalar_hess(X, Y);
      global_hess(XZ, k) = scalar_hess(X, Z);
      global_hess(YY, k) = scalar_hess(Y, Y);
      global_hess(YZ, k) = scalar_hess(Y, Z);
      global_hess(ZZ, k) = scalar_hess(Z, Z);
    }
    return global_hess;
  }

 public:
  /**
   * @brief A wrapper of Hexahedron::GetValue and Hexahedron::GetGlobalGradient for reusing intermediate results.
   * 
   */
  std::pair<Value, Gradient> GetGlobalValueGradient(int ijk) const {
    return _GetGlobalValueGradient(ijk);
  }

  /**
   * @brief A wrapper of Hexahedron::GetValue, Hexahedron::GetGlobalGradient and Hexahedron::GetGlobalHessian for reusing intermediate results.
   * 
   */
  std::tuple<Value, Gradient, Hessian> GetGlobalValueGradientHessian(int ijk) const
      requires(kLocal) {
    auto value_ijk = GetValue(ijk);
    auto local_grad_ijk = GetLocalGradient(ijk);
    return { value_ijk,
        _GetGlobalGradient(value_ijk, local_grad_ijk, ijk),
        _GetGlobalHessian(local_grad_ijk, ijk) };
  }

  std::tuple<Value, Gradient, Hessian> GetGlobalValueGradientHessian(int ijk) const
      requires(!kLocal) {
    auto value_ijk = GetValue(ijk);
    auto local_grad_ijk = GetLocalGradient(ijk);
    Gradient global_grad_ijk = jacobian_inv_[ijk] * local_grad_ijk;
    assert((GetGlobalGradient(ijk) - global_grad_ijk).norm() < 1e-10);
    return { value_ijk, global_grad_ijk,
        _GetGlobalHessian(local_grad_ijk, ijk) };
  }

  /**
   * @brief Convert a flux matrix from global to local at a given integratorian point.
   * 
   * \f$ \begin{bmatrix}F^{\xi} & F^{\eta} & F^{\zeta}\end{bmatrix}=\begin{bmatrix}f^{x} & f^{y} & f^{z}\end{bmatrix}\mathbf{J}^{*} \f$, in which \f$ \mathbf{J}^{*} = \det(\mathbf{J}) \begin{bmatrix}\partial_{x}\\\partial_{y}\\\partial_{z}\end{bmatrix}\begin{bmatrix}\xi & \eta & \zeta\end{bmatrix} \f$ is returned by `Hexahedron::GetJacobianAssociated`.
   * 
   * @tparam FluxMatrix a matrix type which has 3 columns
   * @param global_flux the global flux
   * @param ijk the index of the integratorian point
   * @return FluxMatrix the local flux
   */
  template <class FluxMatrix>
  FluxMatrix GlobalFluxToLocalFlux(const FluxMatrix &global_flux, int ijk) const
      requires(true) {
    FluxMatrix local_flux = global_flux * GetJacobianAssociated(ijk);
    return local_flux;
  }

  /**
   * @brief Get the associated matrix of the Jacobian at a given integratorian point.
   * 
   * \f$ \mathbf{J}^{*}=\det(\mathbf{J})\,\mathbf{J}^{-1} \f$, in which \f$ \mathbf{J}^{-1}=\begin{bmatrix}\partial_{x}\\\partial_{y}\\\partial_{z}\end{bmatrix}\begin{bmatrix}\xi & \eta & \zeta\end{bmatrix} \f$ is the inverse of `coordinate::Element::Jacobian`.
   * 
   * @param ijk the index of the integratorian point
   * @return Jacobian const& the associated matrix of \f$ \mathbf{J} \f$.
   */
  Jacobian const &GetJacobianAssociated(int ijk) const
      requires(true) {
    return jacobian_det_inv_[ijk];
  }

  Value average() const {
    Value average = Value::Zero();
    for (int q = 0; q < integrator().CountPoints(); ++q) {
      average += GetValue(q) * integrator().GetGlobalWeight(q);
    }
    return average / integrator().volume();
  }

  Global const &center() const {
    return integrator_ptr_->center();
  }
  Basis const &basis() const {
    return basis_;
  }
  Integrator const &integrator() const {
    return *integrator_ptr_;
  }
  Coordinate const &coordinate() const {
    return integrator().coordinate();
  }
  template <typename Callable>
  void Approximate(Callable &&global_to_value) {
    for (int ijk = 0; ijk < N; ++ijk) {
      const auto &global = integrator_ptr_->GetGlobal(ijk);
      SetValue(ijk, global_to_value(global));
    }
  }

  /**
   * @brief Add the given Value to the dofs corresponding to the given basis.
   * 
   * @param value the value to be added
   * @param output the beginning of all dofs
   * @param i_basis the (0-based) index of basis
   * @return Scalar* the end of the modified column 
   */
  static Scalar *AddValueTo(Value const &value, Scalar *output, int i_basis) {
    assert(0 <= i_basis && i_basis < N);
    output += K * i_basis;
    for (int r = 0; r < K; ++r) {
      *output++ += value[r];
    }
    return output;
  }
  static Scalar *MinusValue(Value const &value, Scalar *output, int i_basis) {
    assert(0 <= i_basis && i_basis < N);
    output += K * i_basis;
    for (int r = 0; r < K; ++r) {
      *output++ -= value[r];
    }
    return output;
  }
  /**
   * @brief Multiply the given scale to the Value at the given address.
   * 
   * @param scale the scale to be multiplied
   * @param output the address of the value
   * @return the address of the next value
   */
  static Scalar *ScaleValueAt(double scale, Scalar *output) {
    for (int r = 0; r < K; ++r) {
      *output++ *= scale;
    }
    return output;
  }

  int FindFaceId(Global const &face_center) const {
    int i_face;
    auto almost_equal = [&face_center](Global point) {
      point -= face_center;
      return point.norm() < 1e-10;
    };
    if (almost_equal(coordinate().LocalToGlobal(0, 0, -1))) { i_face = 0; }
    else if (almost_equal(coordinate().LocalToGlobal(0, -1, 0))) { i_face = 1; }
    else if (almost_equal(coordinate().LocalToGlobal(+1, 0, 0))) { i_face = 2; }
    else if (almost_equal(coordinate().LocalToGlobal(0, +1, 0))) { i_face = 3; }
    else if (almost_equal(coordinate().LocalToGlobal(-1, 0, 0))) { i_face = 4; }
    else if (almost_equal(coordinate().LocalToGlobal(0, 0, +1))) { i_face = 5; }
    else { assert(false); }
    return i_face;
  }
  std::vector<int> FindCollinearPoints(Global const &global, int i_face) const {
    std::vector<int> indices;
    using mini::coordinate::X;
    using mini::coordinate::Y;
    using mini::coordinate::Z;
    auto local = coordinate().GlobalToLocal(global);
    int i, j, k;
    auto almost_equal = [](Scalar x, Scalar y) {
      return std::abs(x - y) < 1e-10;
    };
    switch (i_face) {
    case 0:
      assert(almost_equal(local[Z], -1));
      for (i = 0; i < IntegratorX::Q; ++i) {
        if (almost_equal(local[X], IntegratorX::points[i])) {
          break;
        }
      }
      for (j = 0; j < IntegratorY::Q; ++j) {
        if (almost_equal(local[Y], IntegratorY::points[j])) {
          break;
        }
      }
      for (k = 0; k < IntegratorZ::Q; ++k) {
        indices.push_back(basis().index(i, j, k));
      }
      break;
    case 1:
      assert(almost_equal(local[Y], -1));
      for (i = 0; i < IntegratorX::Q; ++i) {
        if (almost_equal(local[X], IntegratorX::points[i])) {
          break;
        }
      }
      for (k = 0; k < IntegratorZ::Q; ++k) {
        if (almost_equal(local[Z], IntegratorZ::points[k])) {
          break;
        }
      }
      for (j = 0; j < IntegratorY::Q; ++j) {
        indices.push_back(basis().index(i, j, k));
      }
      break;
    case 2:
      assert(almost_equal(local[X], +1));
      for (j = 0; j < IntegratorY::Q; ++j) {
        if (almost_equal(local[Y], IntegratorY::points[j])) {
          break;
        }
      }
      for (k = 0; k < IntegratorZ::Q; ++k) {
        if (almost_equal(local[Z], IntegratorZ::points[k])) {
          break;
        }
      }
      for (i = 0; i < IntegratorX::Q; ++i) {
        indices.push_back(basis().index(i, j, k));
      }
      break;
    case 3:
      assert(almost_equal(local[Y], +1));
      for (i = 0; i < IntegratorX::Q; ++i) {
        if (almost_equal(local[X], IntegratorX::points[i])) {
          break;
        }
      }
      for (k = 0; k < IntegratorZ::Q; ++k) {
        if (almost_equal(local[Z], IntegratorZ::points[k])) {
          break;
        }
      }
      for (j = 0; j < IntegratorY::Q; ++j) {
        indices.push_back(basis().index(i, j, k));
      }
      break;
    case 4:
      assert(almost_equal(local[X], -1));
      for (j = 0; j < IntegratorY::Q; ++j) {
        if (almost_equal(local[Y], IntegratorY::points[j])) {
          break;
        }
      }
      for (k = 0; k < IntegratorZ::Q; ++k) {
        if (almost_equal(local[Z], IntegratorZ::points[k])) {
          break;
        }
      }
      for (i = 0; i < IntegratorX::Q; ++i) {
        indices.push_back(basis().index(i, j, k));
      }
      break;
    case 5:
      assert(almost_equal(local[Z], +1));
      for (i = 0; i < IntegratorX::Q; ++i) {
        if (almost_equal(local[X], IntegratorX::points[i])) {
          break;
        }
      }
      for (j = 0; j < IntegratorY::Q; ++j) {
        if (almost_equal(local[Y], IntegratorY::points[j])) {
          break;
        }
      }
      for (k = 0; k < IntegratorZ::Q; ++k) {
        indices.push_back(basis().index(i, j, k));
      }
      break;
    default: assert(false);
    }
    return indices;
  }
  std::tuple<int, int, int> FindCollinearIndex(Global const &global, int i_face) const {
    return FindCollinearIndexByGlobal(global, i_face);
  }
  std::tuple<int, int, int> FindCollinearIndexByGlobal(Global const &global, int i_face) const {
    int i{-1}, j{-1}, k{-1};
    using mini::coordinate::X;
    using mini::coordinate::Y;
    using mini::coordinate::Z;
    Global global_temp;
    bool done = false;
    switch (i_face) {
    case 0:
      for (i = 0; i < IntegratorX::Q; ++i) {
        for (j = 0; j < IntegratorY::Q; ++j) {
          global_temp = coordinate().LocalToGlobal(
              IntegratorX::points[i], IntegratorY::points[j], -1);
          global_temp -= global;
          if (global_temp.norm() < 1e-8) {
            done = true;
            break;
          }
        }
        if (done) {
          break;
        }
      }
      break;
    case 1:
      for (i = 0; i < IntegratorX::Q; ++i) {
        for (k = 0; k < IntegratorZ::Q; ++k) {
          global_temp = coordinate().LocalToGlobal(
              IntegratorX::points[i], -1, IntegratorZ::points[k]);
          global_temp -= global;
          if (global_temp.norm() < 1e-8) {
            done = true;
            break;
          }
        }
        if (done) {
          break;
        }
      }
      break;
    case 2:
      for (j = 0; j < IntegratorY::Q; ++j) {
        for (k = 0; k < IntegratorZ::Q; ++k) {
          global_temp = coordinate().LocalToGlobal(
              +1, IntegratorY::points[j], IntegratorZ::points[k]);
          global_temp -= global;
          if (global_temp.norm() < 1e-8) {
            done = true;
            break;
          }
        }
        if (done) {
          break;
        }
      }
      break;
    case 3:
      for (i = 0; i < IntegratorX::Q; ++i) {
        for (k = 0; k < IntegratorZ::Q; ++k) {
          global_temp = coordinate().LocalToGlobal(
              IntegratorX::points[i], +1, IntegratorZ::points[k]);
          global_temp -= global;
          if (global_temp.norm() < 1e-8) {
            done = true;
            break;
          }
        }
        if (done) {
          break;
        }
      }
      break;
    case 4:
      for (j = 0; j < IntegratorY::Q; ++j) {
        for (k = 0; k < IntegratorZ::Q; ++k) {
          global_temp = coordinate().LocalToGlobal(
              -1, IntegratorY::points[j], IntegratorZ::points[k]);
          global_temp -= global;
          if (global_temp.norm() < 1e-8) {
            done = true;
            break;
          }
        }
        if (done) {
          break;
        }
      }
      break;
    case 5:
      for (i = 0; i < IntegratorX::Q; ++i) {
        for (j = 0; j < IntegratorY::Q; ++j) {
          global_temp = coordinate().LocalToGlobal(
              IntegratorX::points[i], IntegratorY::points[j], +1);
          global_temp -= global;
          if (global_temp.norm() < 1e-8) {
            done = true;
            break;
          }
        }
        if (done) {
          break;
        }
      }
      break;
    default: assert(false);
    }
    return std::make_tuple(i, j, k);
  }
  std::tuple<int, int, int> FindCollinearIndexByLocal(Global const &global, int i_face) const {
    int i{-1}, j{-1}, k{-1};
    using mini::coordinate::X;
    using mini::coordinate::Y;
    using mini::coordinate::Z;
    Local local_hint(0, 0, 0);
    switch (i_face) {
    case 0: local_hint[Z] = -1; break;
    case 1: local_hint[Y] = -1; break;
    case 2: local_hint[X] = +1; break;
    case 3: local_hint[Y] = +1; break;
    case 4: local_hint[X] = -1; break;
    case 5: local_hint[Z] = +1; break;
    default: assert(false);
    }
    auto local = coordinate().GlobalToLocal(global, local_hint);
    auto almost_equal = [](Scalar x, Scalar y) {
      return std::abs(x - y) < 1e-10;
    };
    switch (i_face) {
    case 0:
      assert(almost_equal(local[Z], -1));
      for (i = 0; i < IntegratorX::Q; ++i) {
        if (almost_equal(local[X], IntegratorX::points[i])) {
          break;
        }
      }
      for (j = 0; j < IntegratorY::Q; ++j) {
        if (almost_equal(local[Y], IntegratorY::points[j])) {
          break;
        }
      }
      break;
    case 1:
      assert(almost_equal(local[Y], -1));
      for (i = 0; i < IntegratorX::Q; ++i) {
        if (almost_equal(local[X], IntegratorX::points[i])) {
          break;
        }
      }
      for (k = 0; k < IntegratorZ::Q; ++k) {
        if (almost_equal(local[Z], IntegratorZ::points[k])) {
          break;
        }
      }
      break;
    case 2:
      assert(almost_equal(local[X], +1));
      for (j = 0; j < IntegratorY::Q; ++j) {
        if (almost_equal(local[Y], IntegratorY::points[j])) {
          break;
        }
      }
      for (k = 0; k < IntegratorZ::Q; ++k) {
        if (almost_equal(local[Z], IntegratorZ::points[k])) {
          break;
        }
      }
      break;
    case 3:
      assert(almost_equal(local[Y], +1));
      for (i = 0; i < IntegratorX::Q; ++i) {
        if (almost_equal(local[X], IntegratorX::points[i])) {
          break;
        }
      }
      for (k = 0; k < IntegratorZ::Q; ++k) {
        if (almost_equal(local[Z], IntegratorZ::points[k])) {
          break;
        }
      }
      break;
    case 4:
      assert(almost_equal(local[X], -1));
      for (j = 0; j < IntegratorY::Q; ++j) {
        if (almost_equal(local[Y], IntegratorY::points[j])) {
          break;
        }
      }
      for (k = 0; k < IntegratorZ::Q; ++k) {
        if (almost_equal(local[Z], IntegratorZ::points[k])) {
          break;
        }
      }
      break;
    case 5:
      assert(almost_equal(local[Z], +1));
      for (i = 0; i < IntegratorX::Q; ++i) {
        if (almost_equal(local[X], IntegratorX::points[i])) {
          break;
        }
      }
      for (j = 0; j < IntegratorY::Q; ++j) {
        if (almost_equal(local[Y], IntegratorY::points[j])) {
          break;
        }
      }
      break;
    default: assert(false);
    }
    return std::make_tuple(i, j, k);
  }
};
template <class Gx, class Gy, class Gz, int kC, bool kL>
typename Hexahedron<Gx, Gy, Gz, kC, kL>::Basis const
Hexahedron<Gx, Gy, Gz, kC, kL>::basis_ =
    Hexahedron<Gx, Gy, Gz, kC, kL>::BuildInterpolationBasis();

template <class Gx, class Gy, class Gz, int kC, bool kL>
std::array<typename Hexahedron<Gx, Gy, Gz, kC, kL>::Mat3xN,
                    Hexahedron<Gx, Gy, Gz, kC, kL>::N> const
Hexahedron<Gx, Gy, Gz, kC, kL>::basis_local_gradients_ =
    Hexahedron<Gx, Gy, Gz, kC, kL>::BuildBasisLocalGradients();

template <class Gx, class Gy, class Gz, int kC, bool kL>
std::array<typename Hexahedron<Gx, Gy, Gz, kC, kL>::Mat6xN,
                    Hexahedron<Gx, Gy, Gz, kC, kL>::N> const
Hexahedron<Gx, Gy, Gz, kC, kL>::basis_local_hessians_ =
    Hexahedron<Gx, Gy, Gz, kC, kL>::BuildBasisLocalHessians();

namespace {

template <class Hexahedron>
class _LineIntegrator {
  using IntegratorX = typename Hexahedron::IntegratorX;
  using IntegratorY = typename Hexahedron::IntegratorY;
  using IntegratorZ = typename Hexahedron::IntegratorZ;

  static_assert(std::is_same_v<IntegratorX, IntegratorY>);
  static_assert(std::is_same_v<IntegratorX, IntegratorZ>);

 public:
  using type = IntegratorX;
};

}

template <class Hexahedron>
using LineIntegrator = _LineIntegrator<Hexahedron>::type;

}  // namespace polynomial
}  // namespace mini

#endif  // MINI_POLYNOMIAL_HEXAHEDRON_HPP_
