//  Copyright 2021 PEI Weicheng and JIANG Yuyan
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "mpi.h"
#include "pcgnslib.h"

#include "mini/mesh/shuffler.hpp"
#include "mini/mesh/vtk.hpp"
#include "mini/temporal/rk.hpp"

using Scalar = double;
/* Define the Burgers equation. */
constexpr int kComponents = 1;
constexpr int kDimensions = 3;
constexpr int kDegrees = 2;

#include "mini/riemann/rotated/burgers.hpp"
using Riemann = mini::riemann::rotated::Burgers<Scalar, kDimensions>;

#define FR // exactly one of (DGFEM, DGSEM, FR) must be defined
#define VISCOSITY  // one of (LIMITER, VISCOSITY) must be defined

#ifdef DGFEM
#include "mini/integrator/legendre.hpp"
using Gx = mini::integrator::Legendre<Scalar, kDegrees + 1>;
#include "mini/polynomial/projection.hpp"
using Polynomial = mini::polynomial::Projection<Scalar, kDimensions, kDegrees, kComponents>;

#else  // common for DGSEM and FR
#include "mini/integrator/lobatto.hpp"
using Gx = mini::integrator::Lobatto<Scalar, kDegrees + 1>;

#include "mini/polynomial/hexahedron.hpp"
using Interpolation = mini::polynomial::Hexahedron<Gx, Gx, Gx, kComponents, false>;

#ifdef LIMITER
#include "mini/polynomial/extrapolation.hpp"
using Polynomial = mini::polynomial::Extrapolation<Interpolation>;

#else  // VISCOSITY
using Polynomial = Interpolation;
#endif  // LIMITER

#endif  // DGSEM

#include "mini/mesh/part.hpp"
using Part = mini::mesh::part::Part<cgsize_t, Polynomial>;
using Cell = typename Part::Cell;
using Face = typename Part::Face;
using Global = typename Cell::Global;
using Value = typename Cell::Value;
using Coeff = typename Cell::Coeff;

#include "mini/coordinate/quadrangle.hpp"
#include "mini/integrator/quadrangle.hpp"
#include "mini/coordinate/hexahedron.hpp"
#include "mini/integrator/hexahedron.hpp"

static void InstallIntegratorPrototypes(Part *part_ptr) {
  auto quadrangle = mini::coordinate::Quadrangle4<Scalar, kDimensions>();
  using QuadrangleIntegrator
    = mini::integrator::Quadrangle<kDimensions, Gx, Gx>;
  part_ptr->InstallPrototype(4,
      std::make_unique<QuadrangleIntegrator>(quadrangle));
  auto hexahedron = mini::coordinate::Hexahedron8<Scalar>();
  using HexahedronIntegrator
      = mini::integrator::Hexahedron<Gx, Gx, Gx>;
  part_ptr->InstallPrototype(8,
      std::make_unique<HexahedronIntegrator>(hexahedron));
#ifdef DGFEM  // TODO(PVC): install prototypes for Triangle, Tetrahedron, etc.
#endif
  part_ptr->BuildGeometry();
}

using VtkWriter = mini::mesh::vtk::Writer<Part>;

#include "mini/constant/index.hpp"
static void ShiftByValue(Global *global, Value const &value) {
  Scalar &z = (*global)[mini::constant::index::Z];
  z += (value[0] - 2.) * 0.2;
}

#ifdef LIMITER

#ifdef DGFEM
#include "mini/spatial/dg/general.hpp"
using General = mini::spatial::dg::General<Part, Riemann>;

#elif defined(DGSEM)
#include "mini/spatial/dg/lobatto.hpp"
using General = mini::spatial::dg::Lobatto<Part, Riemann>;

#elif defined(FR)
#include "mini/spatial/fr/lobatto.hpp"
using General = mini::spatial::fr::Lobatto<Part, Riemann>;
#endif

#include "mini/limiter/weno.hpp"
#include "mini/limiter/reconstruct.hpp"
#include "mini/spatial/with_limiter.hpp"
using Limiter = mini::limiter::weno::Lazy<Cell>;
using Spatial = mini::spatial::WithLimiter<General, Limiter>;

#endif  // LIMITER

#ifdef VISCOSITY

#include "mini/riemann/concept.hpp"
#include "mini/riemann/diffusive/linear.hpp"
#include "mini/riemann/diffusive/direct.hpp"
#include "mini/spatial/viscosity.hpp"
#include "mini/spatial/with_viscosity.hpp"

using Diffusion = mini::riemann::diffusive::Direct<
    mini::riemann::diffusive::Isotropic<Scalar, kComponents>>;
using RiemannWithViscosity = mini::spatial::EnergyBasedViscosity<Part,
    mini::riemann::ConvectionDiffusion<Riemann, Diffusion>>;
static_assert(mini::riemann::ConvectiveDiffusive<RiemannWithViscosity>);

#if defined(FR)
#include "mini/spatial/fr/lobatto.hpp"
using General = mini::spatial::fr::Lobatto<Part, RiemannWithViscosity>;
#endif

using Spatial = mini::spatial::WithViscosity<General>;

#endif  // VISCOSITY

#include <fstream>
#include <nlohmann/json.hpp>

int main(int argc, char* argv[]) {
  MPI_Init(NULL, NULL);
  int n_core, i_core;
  MPI_Comm_size(MPI_COMM_WORLD, &n_core);
  MPI_Comm_rank(MPI_COMM_WORLD, &i_core);
  cgp_mpi_comm(MPI_COMM_WORLD);

  Riemann::Convection::SetJacobians(1, 0, 0);
#ifdef VISCOSITY
  Diffusion::SetProperty(0.0);
  Diffusion::SetBetaValues(2.0, 1.0 / 12);
#endif
  if (argc != 2) {
    if (i_core == 0) {
      std::cout << "usage:\n"
          << "  mpirun -n <n_core> " << argv[0] << " <json_input_file>\n";
    }
    MPI_Finalize();
    exit(0);
  }

  auto json_input_file = std::ifstream(argv[1]);
  auto json_object = nlohmann::json::parse(json_input_file);

  std::string old_file_name = json_object.at("cgns_file");
  std::string suffix = json_object.at("cell_type");
  double t_start = json_object.at("t_start");
  double t_stop = json_object.at("t_stop");
  int n_steps_per_frame = json_object.at("n_steps_per_frame");
  int n_frames = json_object.at("n_frames");
  int n_steps = n_frames * n_steps_per_frame;
  auto dt = (t_stop - t_start) / n_steps;
  int i_frame_prev = json_object.at("i_frame_prev");
  // `i_frame_prev` might be -1, which means no previous result to be loaded.
  int i_frame = std::max(i_frame_prev, 0);
  int n_parts_prev = n_core;
  if (i_frame_prev >= 0) {
    n_parts_prev = json_object.at("n_parts_prev");
  }

  std::string case_name = "standing_" + suffix;

  auto time_begin = MPI_Wtime();

  /* Partition the mesh. */
  if (i_core == 0 && (i_frame_prev < 0 || n_parts_prev != n_core)) {
    using Shuffler = mini::mesh::Shuffler<idx_t, Scalar>;
    Shuffler::PartitionAndShuffle(case_name, old_file_name, n_core);
  }
  MPI_Barrier(MPI_COMM_WORLD);

  if (i_core == 0) {
    std::printf("Create %d `Part`s at %f sec\n",
        n_core, MPI_Wtime() - time_begin);
  }
  auto part = Part(case_name, i_core, n_core);
  InstallIntegratorPrototypes(&part);
  part.SetFieldNames({"U"});

  /* Build a `Spatial` object. */
#ifdef LIMITER
  auto limiter = Limiter(/* w0 = */0.001, /* eps = */1e-6);
  auto spatial = Spatial(&limiter, &part);
#else
  auto spatial = Spatial(&part);
  RiemannWithViscosity::SetTimeScale(json_object.at("time_scale"));
  VtkWriter::AddCellData("CellViscosity", [](Cell const &cell){
    return RiemannWithViscosity::GetPropertyOnCell(cell.id(), 0)[0];
  });
#endif
  if (json_object.at("shift_by_value")) {
    VtkWriter::InstallShiftByValue(ShiftByValue);
  }

  /* Set initial conditions. */
  auto initial_condition = [&](const Global& xyz){
    auto x = xyz[0];
    Value val;
    val[0] = x * (x - 2.0) * (x - 4.0);
    return val;
  };

  if (i_frame_prev < 0) {
    if (i_core == 0) {
      std::printf("[Start] Approximate() on %d cores at %f sec\n",
          n_core, MPI_Wtime() - time_begin);
    }
    spatial.Approximate(initial_condition);
    if (i_core == 0) {
      std::printf("[Start] WriteSolutions(Frame0) on %d cores at %f sec\n",
          n_core, MPI_Wtime() - time_begin);
    }

#ifdef LIMITER
    mini::limiter::Reconstruct(&part, &limiter);
    if (suffix == "tetra") {
      mini::limiter::Reconstruct(&part, &limiter);
    }
#endif
    part.GatherSolutions();
    part.WriteSolutions("Frame0");
    VtkWriter::WriteSolutions(part, "Frame0");
  } else {
    if (i_core == 0) {
      std::printf("[Start] ReadSolutions(Frame%d) on %d cores at %f sec\n",
          i_frame, n_core, MPI_Wtime() - time_begin);
    }
    std::string soln_name = (n_parts_prev != n_core)
        ? "shuffled" : "Frame" + std::to_string(i_frame);
    part.ReadSolutions(soln_name);
    part.ScatterSolutions();
  }

  /* Define the temporal solver. */
  constexpr int kOrders = std::min(3, kDegrees + 1);
  using Temporal = mini::temporal::RungeKutta<kOrders, Scalar>;
  auto temporal = Temporal();

  /* Set boundary conditions. */
  auto given_state = [](const Global& xyz, double t) -> Value {
    return Value::Zero();
  };
  auto prefix = std::string(suffix == "tetra" ? "3_" : "4_");
  spatial.SetInviscidWall(prefix + "S_27");  // Top
  spatial.SetInviscidWall(prefix + "S_1");   // Back
  spatial.SetInviscidWall(prefix + "S_32");  // Front
  spatial.SetInviscidWall(prefix + "S_19");  // Bottom
  spatial.SetInviscidWall(prefix + "S_15");  // Gap
  spatial.SetSmartBoundary(prefix + "S_31", given_state);  // Left
  spatial.SetSmartBoundary(prefix + "S_23", given_state);  // Right

  /* Main Loop */
  auto wtime_start = MPI_Wtime();
  for (int i_step = 1; i_step <= n_steps; ++i_step) {
    double t_curr = t_start + dt * (i_step - 1);
    temporal.Update(&spatial, t_curr, dt);

    auto wtime_curr = MPI_Wtime() - wtime_start;
    auto wtime_total = wtime_curr * n_steps / i_step;
    if (i_core == 0) {
      std::printf("[Done] Update(Step%d/%d) on %d cores at %f / %f sec\n",
          i_step, n_steps, n_core, wtime_curr, wtime_total);
    }

    if (i_step % n_steps_per_frame == 0) {
      ++i_frame;
      part.GatherSolutions();
      if (i_core == 0) {
        std::printf("[Start] WriteSolutions(Frame%d) on %d cores at %f sec\n",
            i_frame, n_core, MPI_Wtime() - wtime_start);
      }
      auto frame_name = "Frame" + std::to_string(i_frame);
      part.WriteSolutions(frame_name);
      VtkWriter::WriteSolutions(part, frame_name);
    }
  }

  if (i_core == 0) {
    json_object["n_parts_curr"] = n_core;
    std::string output_name = case_name;
    output_name += "/Frame";
    output_name += std::to_string(i_frame - n_frames) + "to";
    output_name += std::to_string(i_frame) + ".json";
    auto json_output_file = std::ofstream(output_name);
    json_output_file << std::setw(2) << json_object << std::endl;
    std::printf("time-range = [%f, %f], frame-range = [%d, %d], dt = %f\n",
        t_start, t_stop, i_frame - n_frames, i_frame, dt);
    std::printf("[Start] MPI_Finalize() on %d cores at %f sec\n",
        n_core, MPI_Wtime() - time_begin);
  }
  MPI_Finalize();
}
