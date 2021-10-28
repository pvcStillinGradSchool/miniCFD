//  Copyright 2021 PEI Weicheng and JIANG Yuyan
#ifndef MINI_POLYNOMIAL_LIMITER_HPP_
#define MINI_POLYNOMIAL_LIMITER_HPP_

#include <cmath>
#include <iostream>
#include <type_traits>
#include <utility>

namespace mini {
namespace polynomial {

template <typename Cell, typename Eigen>
class EigenWeno {
  using Scalar = double;
  using Projection = typename Cell::Projection;
  using Basis = typename Projection::Basis;
  using Coord = typename Projection::Coord;
  using Value = typename Projection::Value;

  Value weights_;
  Scalar eps_;
  Cell* my_cell_;
  Projection new_projection_;
  std::vector<Projection> old_projections_;

 public:
  EigenWeno(const Scalar &w0, const Scalar &eps)
      : eps_(eps) {
    weights_.setOnes();
    weights_ *= w0;
  }
  Projection operator()(Cell* cell) {
    my_cell_ = cell;
    Borrow();
    Reconstruct();
    return new_projection_;
  }

 private:
  static Coord GetNu(Cell const &cell_i, Cell const &cell_j) {
    Coord nu = cell_i.basis_.GetCenter() - cell_j.basis_.GetCenter();
    nu /= std::hypot(nu[0], nu[1], nu[2]);
    return nu;
  }
  static void GetMuPi(Coord const &nu, Coord *mu, Coord *pi) {
    int id = 0;
    for (int i = 1; i < 3; ++i) {
      if (std::abs(nu[i]) < std::abs(nu[id])) {
        id = i;
      }
    }
    auto a = nu[0], b = nu[1], c = nu[2];
    switch (id) {
    case 0:
      *mu << 0.0, -c, b;
      *pi << (b * b + c * c), -(a * b), -(a * c);
      break;
    case 1:
      *mu << c, 0.0, -a;
      *pi << -(a * b), (a * a + c * c), -(b * c);
      break;
    case 2:
      *mu << -b, a, 0.0;
      *pi << -(a * c), -(b * c), (a * a + b * b);
      break;
    default:
      break;
    }
    *mu /= std::hypot((*mu)[0], (*mu)[1], (*mu)[2]);
    *pi /= std::hypot((*pi)[0], (*pi)[1], (*pi)[2]);
  }
  /**
   * @brief Borrow projections from adjacent cells.
   * 
   */
  void Borrow() {
    old_projections_.clear();
    old_projections_.reserve(my_cell_->adj_cells_.size() + 1);
    auto& my_average = my_cell_->func_.GetAverage();
    for (auto* adj_cell : my_cell_->adj_cells_) {
      old_projections_.emplace_back(adj_cell->func_, my_cell_->basis_);
      auto& adj_proj = old_projections_.back();
      adj_proj += my_average - adj_proj.GetAverage();
    }
    old_projections_.emplace_back(my_cell_->func_);
  }
  /**
   * @brief Rotate borrowed projections onto the interface between cells
   * 
   */
  Scalar Rotate(const Cell &adj_cell) {
    int adj_cnt = my_cell_->adj_cells_.size();
    // build eigen-matrices in the rotated coordinate system
    Coord nu = GetNu(*my_cell_, adj_cell), mu, pi;
    GetMuPi(nu, &mu, &pi);
    assert(nu.cross(mu) == pi);
    auto rotated_eigen = Eigen(my_cell_->func_.GetAverage(), nu, mu, pi);
    // initialize weights
    auto weights = std::vector<Value>(adj_cnt + 1, weights_);
    weights.back() *= -adj_cnt;
    weights.back().array() += 1.0;
    // modify weights by smoothness
    auto rotated_projections = old_projections_;
    for (int i = 0; i <= adj_cnt; ++i) {
      auto& projection_i = rotated_projections[i];
      projection_i.LeftMultiply(rotated_eigen.L);
      auto beta = projection_i.GetSmoothness();
      beta.array() += eps_;
      beta.array() *= beta.array();
      weights[i].array() /= beta.array();
    }
    // normalize these weights
    Value sum; sum.setZero();
    sum = std::accumulate(weights.begin(), weights.end(), sum);
    assert(weights.size() == adj_cnt + 1);
    for (auto& weight : weights) {
      weight.array() /= sum.array();
    }
    // build the new (weighted) projection
    auto& new_projection = rotated_projections.back();
    new_projection *= weights.back();
    for (int i = 0; i < adj_cnt; ++i) {
      rotated_projections[i] *= weights[i];
      new_projection += rotated_projections[i];
    }
    // rotate the new projection back to the global system
    new_projection.LeftMultiply(rotated_eigen.R);
    // scale the new projection by volume
    auto adj_volume = adj_cell.volume();
    new_projection *= adj_volume;
    new_projection_ += new_projection;
    return adj_volume;
  }
  /**
   * @brief Reconstruct projections by weights
   * 
   */
  void Reconstruct() {
    new_projection_ = Projection(my_cell_->basis_);
    Scalar total_volume = 0.0;
    for (auto* adj_cell : my_cell_->adj_cells_) {
      total_volume += Rotate(*adj_cell);
    }
    new_projection_ /= total_volume;
  }
};

}  // namespace polynomial
}  // namespace mini

#endif  // MINI_POLYNOMIAL_LIMITER_HPP_