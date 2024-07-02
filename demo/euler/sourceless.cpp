//  Copyright 2021 PEI Weicheng and JIANG Yuyan
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "mpi.h"
#include "pcgnslib.h"

#include "mini/mesh/shuffler.hpp"

#include "sourceless.hpp"

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

#include "mini/mesh/vtk.hpp"
using VtkWriter = mini::mesh::vtk::Writer<Part>;

#ifdef VISCOSITY
#include "mini/limiter/average.hpp"
#endif

#include <fstream>
#include <nlohmann/json.hpp>

int Main(int argc, char* argv[], IC ic, BC bc) {
  MPI_Init(NULL, NULL);
  int n_core, i_core;
  MPI_Comm_size(MPI_COMM_WORLD, &n_core);
  MPI_Comm_rank(MPI_COMM_WORLD, &i_core);
  cgp_mpi_comm(MPI_COMM_WORLD);

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
  std::string case_name = json_object.at("case_name");
  case_name.push_back('_');
  case_name += suffix;

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
  part.SetFieldNames({"Density", "MomentumX", "MomentumY", "MomentumZ",
      "EnergyStagnationDensity"});

#ifdef LIMITER
  /* Build a `Limiter` object. */
  auto limiter = Limiter(/* w0 = */0.001, /* eps = */1e-6);
  auto spatial = Spatial(&limiter, &part);
#else
  Diffusion::SetProperty(0.0);
  Diffusion::SetBetaValues(
      json_object.at("ddg_beta_0"), json_object.at("ddg_beta_1"));
  auto spatial = Spatial(&part);
  RiemannWithViscosity::SetTimeScale(json_object.at("time_scale"));
  for (int k = 0; k < kComponents; ++k) {
    VtkWriter::AddCellData("CellViscosity" + std::to_string(k + 1),
        [k](Cell const &cell) {
            return RiemannWithViscosity::GetPropertyOnCell(cell.id(), 0)[k]; });
  }
#endif

  /* Initialization. */
  if (i_frame_prev < 0) {
    spatial.Approximate(ic);
    if (i_core == 0) {
      std::printf("[Done] Approximate() on %d cores at %f sec\n",
          n_core, MPI_Wtime() - time_begin);
    }

#ifdef LIMITER
    mini::limiter::Reconstruct(&part, &limiter);
    if (suffix == "tetra") {
      mini::limiter::Reconstruct(&part, &limiter);
    }
#else  // VISCOSITY
    mini::limiter::average::Reconstruct(spatial.part_ptr());
#endif
    if (i_core == 0) {
      std::printf("[Done] Reconstruct() on %d cores at %f sec\n",
          n_core, MPI_Wtime() - time_begin);
    }

    part.GatherSolutions();
    part.WriteSolutions("Frame0");
    VtkWriter::WriteSolutions(part, "Frame0");
    if (i_core == 0) {
      std::printf("[Done] WriteSolutions(Frame0) on %d cores at %f sec\n",
          n_core, MPI_Wtime() - time_begin);
    }
  } else {
    std::string soln_name = (n_parts_prev != n_core)
        ? "shuffled" : "Frame" + std::to_string(i_frame);
    part.ReadSolutions(soln_name);
    part.ScatterSolutions();
    if (i_core == 0) {
      std::printf("[Done] ReadSolutions(Frame%d) on %d cores at %f sec\n",
          i_frame, n_core, MPI_Wtime() - time_begin);
    }
  }

  /* Define the temporal solver. */
  auto temporal = Temporal();

  /* Set boundary conditions. */
  bc(suffix, &spatial);

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
      auto frame_name = "Frame" + std::to_string(i_frame);
      part.GatherSolutions();
      part.WriteSolutions(frame_name);
      VtkWriter::WriteSolutions(part, frame_name);
      if (i_core == 0) {
        std::printf("[Done] WriteSolutions(Frame%d) on %d cores at %f sec\n",
            i_frame, n_core, MPI_Wtime() - wtime_start);
      }
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
  return 0;
}
