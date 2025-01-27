//  Copyright 2021 PEI Weicheng and JIANG Yuyan

#include <cmath>

#include "mini/integrator/function.hpp"
#include "mini/integrator/legendre.hpp"
#include "mini/integrator/hexahedron.hpp"
#include "mini/coordinate/hexahedron.hpp"
#include "mini/basis/linear.hpp"
#include "mini/polynomial/projection.hpp"

#include "gtest/gtest.h"

class TestProjection : public ::testing::Test {
 protected:
  using Taylor = mini::basis::Taylor<double, 3, 2>;
  using Basis = mini::basis::OrthoNormal<double, 3, 2>;
  using Coordinate = mini::coordinate::Hexahedron8<double>;
  using Gx = mini::integrator::Legendre<double, 5>;
  using Integrator = mini::integrator::Hexahedron<Gx, Gx, Gx>;
  using Coord = typename Integrator::Global;
  Coordinate coordinate_;
  Integrator integrator_;

  TestProjection() : coordinate_{
      Coord{-1, -1, -1}, Coord{+1, -1, -1},
      Coord{+1, +1, -1}, Coord{-1, +1, -1},
      Coord{-1, -1, +1}, Coord{+1, -1, +1},
      Coord{+1, +1, +1}, Coord{-1, +1, +1}
    }, integrator_(coordinate_) {
  }
};

TEST_F(TestProjection, ScalarFunction) {
  auto func = [](Coord const &point){
    auto x = point[0], y = point[1], z = point[2];
    return x * x + y * y + z * z;
  };
  using ProjFunc = mini::polynomial::Projection<double, 3, 2, 1>;
  auto projection = ProjFunc(integrator_);
  projection.Approximate(func);
  static_assert(ProjFunc::K == 1);
  static_assert(ProjFunc::N == 10);
  EXPECT_NEAR(projection.GlobalToValue({0, 0, 0})[0], 0.0, 1e-14);
  EXPECT_NEAR(projection.GlobalToValue({0.3, 0.4, 0.5})[0], 0.5, 1e-14);
  auto integral_f = mini::integrator::Integrate(func, integrator_);
  auto integral_1 = mini::integrator::Integrate([](auto const &){
    return 1.0;
  }, integrator_);
  EXPECT_NEAR(projection.average()[0], integral_f / integral_1, 1e-14);
}
TEST_F(TestProjection, VectorFunction) {
  using ProjFunc = mini::polynomial::Projection<double, 3, 2, 10>;
  using Value = typename ProjFunc::Value;
  auto func = [](Coord const &point){
    auto x = point[0], y = point[1], z = point[2];
    Value res = { 1, x, y, z, x * x, x * y, x * z, y * y, y * z, z * z };
    return res;
  };
  auto projection = ProjFunc(integrator_);
  projection.Approximate(func);
  static_assert(ProjFunc::K == 10);
  static_assert(ProjFunc::N == 10);
  auto v_actual = projection.GlobalToValue({0.3, 0.4, 0.5});
  auto v_expect = Taylor::GetValue({0.3, 0.4, 0.5});
  Value res = v_actual - v_expect;
  EXPECT_NEAR(res.norm(), 0.0, 1e-14);
  auto integral_f = mini::integrator::Integrate(func, integrator_);
  auto integral_1 = mini::integrator::Integrate([](auto const &){
    return 1.0;
  }, integrator_);
  res = projection.average() - integral_f / integral_1;
  EXPECT_NEAR(res.norm(), 0.0, 1e-14);
}
TEST_F(TestProjection, CoeffConsistency) {
  using ProjFunc = mini::polynomial::Projection<double, 3, 2, 5>;
  using Coeff = typename ProjFunc::Coeff;
  using Value = typename ProjFunc::Value;
  auto func = [](Coord const &point){
    auto x = point[0], y = point[1], z = point[2];
    Value res = { std::sin(x + y), std::cos(y + z), std::tan(x * z),
        std::exp(y * z), std::log(1 + z * z) };
    return res;
  };
  auto projection = ProjFunc(integrator_);
  projection.Approximate(func);
  Coeff coeff_diff = projection.GetCoeffOnTaylorBasis()
      - projection.coeff() * projection.basis().coeff();
  EXPECT_NEAR(coeff_diff.norm(), 0.0, 1e-14);
}
TEST_F(TestProjection, PartialDerivatives) {
  using ProjFunc = mini::polynomial::Projection<double, 3, 2, 10>;
  using Taylor = mini::basis::Taylor<double, 3, 2>;
  using Value = typename ProjFunc::Value;
  auto func = [](Coord const &point) {
    return Taylor::GetValue(point);
  };
  auto projection = ProjFunc(integrator_);
  projection.Approximate(func);
  static_assert(ProjFunc::K == 10);
  static_assert(ProjFunc::N == 10);
  auto x = 0.3, y = 0.4, z = 0.5;
  auto point = Coord{ x, y, z };
  EXPECT_EQ(projection.center(), Coord::Zero());
  auto pdv_actual = Taylor::GetPartialDerivatives(point,
      projection.GetCoeffOnTaylorBasis());
  auto coeff = ProjFunc::Coeff(); coeff.setIdentity();
  auto pdv_expect = Taylor::GetPartialDerivatives(point, coeff);
  ProjFunc::Coeff diff = pdv_actual - pdv_expect;
  EXPECT_NEAR(diff.norm(), 0.0, 1e-13);
}
TEST_F(TestProjection, ArithmaticOperations) {
  using ProjFunc = mini::polynomial::Projection<double, 3, 2, 10>;
  using Value = typename ProjFunc::Value;
  auto func = [](Coord const &point){
    auto x = point[0], y = point[1], z = point[2];
    Value res = { 1, x, y, z, x * x, x * y, x * z, y * y, y * z, z * z };
    return res;
  };
  auto projection = ProjFunc(integrator_);
  projection.Approximate(func);
  using Wrapper = typename ProjFunc::Wrapper;
  auto projection_wrapper = Wrapper(projection);
  EXPECT_EQ(projection_wrapper.coeff(), projection.coeff());
  EXPECT_EQ(projection_wrapper.GetCoeffOnTaylorBasis(),
      projection.GetCoeffOnTaylorBasis());
  EXPECT_EQ(projection_wrapper.average(), projection.average());
  projection_wrapper *= 2;
  EXPECT_EQ(projection_wrapper.coeff(), projection.coeff() * 2);
  projection_wrapper /= 2;
  EXPECT_EQ(projection_wrapper.coeff(), projection.coeff());
  projection_wrapper += projection_wrapper;
  EXPECT_EQ(projection_wrapper.coeff(), projection.coeff() * 2);
  Value value; value.setOnes(); value /= 2;
  projection_wrapper *= value;
  EXPECT_EQ(projection_wrapper.coeff(), projection.coeff());
  projection_wrapper += value;
  value += projection.average();
  EXPECT_NEAR((value - projection_wrapper.average()).norm(), 0, 1e-15);
}

int main(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
