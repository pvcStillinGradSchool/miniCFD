//  Copyright 2022 PEI Weicheng
#include "sourceless.hpp"

/* Set initial conditions. */
auto primitive = Primitive(1.4, 0.04, 0.0, 0.03, 1.0);
Value given_value = Gas::PrimitiveToConservative(primitive);

Value MyIC(const Global &xyz) {
  return given_value;
}

/* Set boundary conditions. */
auto given_state = [](const Global& xyz, double t){
  return given_value;
};

void MyBC(const std::string &suffix, Spatial *spatial) {
  spatial->SetSubsonicInlet("3_S_1", given_state);
  spatial->SetSubsonicInlet("3_S_5", given_state);
  spatial->SetSubsonicOutlet("3_S_3", given_state);
  spatial->SetSubsonicOutlet("3_S_6", given_state);
  // spatial->SetSupersonicOutlet("3_S_3");
  // spatial->SetSupersonicOutlet("3_S_6");
  spatial->SetSolidWall("3_S_2");
  spatial->SetSolidWall("3_S_4");
  spatial->SetSolidWall("3_S_7");
}

int main(int argc, char* argv[]) {
  return Main(argc, argv, MyIC, MyBC);
}
