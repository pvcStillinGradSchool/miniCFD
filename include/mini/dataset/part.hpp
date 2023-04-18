// Copyright 2021 PEI Weicheng and JIANG Yuyan
#ifndef MINI_DATASET_PART_HPP_
#define MINI_DATASET_PART_HPP_

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <ios>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <unordered_map>

#include "mpi.h"
#include "pcgnslib.h"
#include "mini/algebra/eigen.hpp"
#include "mini/dataset/cgns.hpp"
#include "mini/integrator/triangle.hpp"
#include "mini/integrator/quadrangle.hpp"
#include "mini/integrator/tetra.hpp"
#include "mini/integrator/hexa.hpp"
#include "mini/polynomial/basis.hpp"
#include "mini/polynomial/projection.hpp"

/* Provide a K-way type selection mechanism. */
namespace mini {
// generic version, no instantiation:
template<unsigned N, typename... Types>
struct select;
// specialization for N > 0:
template <unsigned N, typename T, typename... Types>
struct select<N, T, Types...> {
  using type = typename select<N-1, Types...>::type;
};
// specialization for N == 0:
template <typename T, typename... Types>
struct select<0, T, Types...> {
  using type = T;
};
// STL-style type aliasing:
template<unsigned N, typename... Types>
using select_t = typename select<N, Types...>::type;
}  // namespace mini

namespace mini {
namespace mesh {
namespace cgns {

template <class Int>
struct NodeInfo {
  NodeInfo() = default;
  NodeInfo(Int zi, Int ni) : i_zone(zi), i_node(ni) {}
  NodeInfo(NodeInfo const &) = default;
  NodeInfo &operator=(NodeInfo const &) = default;
  NodeInfo(NodeInfo &&) noexcept = default;
  NodeInfo &operator=(NodeInfo &&) noexcept = default;
  ~NodeInfo() noexcept = default;
  Int i_zone{0}, i_node{0};
};

template <class Int>
struct CellInfo {
  CellInfo() = default;
  CellInfo(Int z, Int s, Int c, Int n)
      : i_zone(z), i_sect(s), i_cell(c), npe(n) {}
  CellInfo(CellInfo const &) = default;
  CellInfo &operator=(CellInfo const &) = default;
  CellInfo(CellInfo &&) noexcept = default;
  CellInfo &operator=(CellInfo &&) noexcept = default;
  ~CellInfo() noexcept = default;
  Int i_zone{0}, i_sect{0}, i_cell{0}, npe{0};
};

template <typename Int, typename Scalar>
struct NodeGroup {
  Int head_, size_;
  ShiftedVector<Int> metis_id_;
  ShiftedVector<Scalar> x_, y_, z_;
  cgsize_t zone_size_[3][1];
  char zone_name_[33];

  NodeGroup(int head, int size)
      : head_(head), size_(size), metis_id_(size, head),
        x_(size, head), y_(size, head), z_(size, head) {
  }
  NodeGroup() = default;
  NodeGroup(NodeGroup const &) = delete;
  NodeGroup &operator=(NodeGroup const &) = delete;
  NodeGroup(NodeGroup &&that) noexcept {
    *this = std::move(that);
  }
  NodeGroup &operator=(NodeGroup &&that) noexcept {
    if (this != &that) {
      head_ = that.head_;
      size_ = that.size_;
      metis_id_ = std::move(that.metis_id_);
      x_ = std::move(that.x_);
      y_ = std::move(that.y_);
      z_ = std::move(that.z_);
      std::memcpy(zone_size_, that.zone_size_, 3 * sizeof(cgsize_t));
      std::memcpy(zone_name_, that.zone_name_, 33);
    }
    return *this;
  }
  ~NodeGroup() noexcept = default;

  Int size() const {
    return size_;
  }
  Int head() const {
    return head_;
  }
  Int tail() const {
    return head_ + size_;
  }
  bool has(int i_node) const {
    return head_ <= i_node && i_node < size_ + head_;
  }
};

template <typename Int, int kDegrees, class Riemann>
struct Cell;

template <typename Int, int D, class R>
struct Face {
  constexpr static int kDegrees = D;
  using Riemann = R;
  using Scalar = typename Riemann::Scalar;
  constexpr static int kComponents = Riemann::kComponents;
  constexpr static int kDimensions = Riemann::kDimensions;
  using Gauss = integrator::Face<Scalar, kDimensions>;
  using GaussUptr = std::unique_ptr<Gauss>;
  using Cell = cgns::Cell<Int, kDegrees, Riemann>;

  GaussUptr gauss_ptr_;
  Cell *holder_, *sharer_;
  Riemann riemann_;
  Int id_{-1};

  Face(GaussUptr &&gauss_ptr, Cell *holder, Cell *sharer, Int id = 0)
      : gauss_ptr_(std::move(gauss_ptr)), holder_(holder), sharer_(sharer),
        id_(id) {
    riemann_.Rotate(gauss_ptr_->GetNormalFrame(0));
  }
  Face(const Face &) = delete;
  Face &operator=(const Face &) = delete;
  Face(Face &&) noexcept = default;
  Face &operator=(Face &&) noexcept = default;
  ~Face() noexcept = default;

  Gauss const &gauss() const {
    return *gauss_ptr_;
  }
  Scalar area() const {
    return gauss_ptr_->area();
  }
  Int id() const {
    return id_;
  }
  Cell const *other(Cell const *cell) const {
    assert(cell == sharer_ || cell == holder_);
    return cell == holder_ ? sharer_ : holder_;
  }
};

template <typename Int, int D, class R>
struct Cell {
  constexpr static int kDegrees = D;
  using Riemann = R;
  using Scalar = typename Riemann::Scalar;
  constexpr static int kComponents = Riemann::kComponents;
  constexpr static int kDimensions = Riemann::kDimensions;
  using Gauss = integrator::Cell<Scalar>;
  using GaussUptr = std::unique_ptr<Gauss>;
  using Basis = polynomial::OrthoNormal<Scalar, kDimensions, kDegrees>;
  using Projection = polynomial::
      Projection<Scalar, kDimensions, kDegrees, kComponents>;
  using Coord = typename Projection::Coord;
  using Value = typename Projection::Value;
  using Coeff = typename Projection::Coeff;
  static constexpr int K = Projection::K;  // number of functions
  static constexpr int N = Projection::N;  // size of the basis
  static constexpr int kFields = K * N;
  using Face = cgns::Face<Int, kDegrees, R>;

  std::vector<Cell *> adj_cells_;
  std::vector<Face *> adj_faces_;
  Basis basis_;
  GaussUptr gauss_ptr_;
  Projection projection_;
  Int metis_id{-1}, id_{-1};
  bool inner_ = true;

  Cell(GaussUptr &&gauss_ptr, Int m_cell)
      : basis_(*gauss_ptr), gauss_ptr_(std::move(gauss_ptr)),
        metis_id(m_cell), projection_(basis_) {
  }
  Cell() = default;
  Cell(Cell const &) = delete;
  Cell &operator=(Cell const &) = delete;
  Cell(Cell &&that) noexcept {
    *this = std::move(that);
  }
  Cell &operator=(Cell &&that) noexcept {
    adj_cells_ = std::move(that.adj_cells_);
    adj_faces_ = std::move(that.adj_faces_);
    basis_ = std::move(that.basis_);
    gauss_ptr_ = std::move(that.gauss_ptr_);
    projection_ = std::move(that.projection_);
    projection_.basis_ptr_ = &basis_;  //
    metis_id = that.metis_id;
    id_ = that.id_;
    inner_ = that.inner_;
    return *this;
  }
  ~Cell() noexcept = default;

  Scalar volume() const {
    return gauss_ptr_->volume();
  }
  Int id() const {
    return id_;
  }
  bool inner() const {
    return inner_;
  }
  Coord const &center() const {
    return basis_.center();
  }
  Gauss const &gauss() const {
    return *gauss_ptr_;
  }
  Coord LocalToGlobal(const Coord &local) const {
    return gauss().LocalToGlobal(local);
  }
  Value GetValue(const Coord &global) const {
    return projection_(global);
  }
  int CountVertices() const {
    return gauss().CountVertices();
  }

  template <class Callable>
  void Project(Callable &&func) {
    projection_.Project(func, basis_);
  }
};

template <typename Int, int kDegrees, class Riemann>
class CellGroup {
  using Cell = cgns::Cell<Int, kDegrees, Riemann>;
  using Scalar = typename Cell::Scalar;

  Int head_, size_;
  ShiftedVector<Cell> cells_;
  ShiftedVector<ShiftedVector<Scalar>> fields_;  // [i_field][i_cell]
  int npe_;

 public:
  static constexpr int kFields = Cell::kFields;

  CellGroup(int head, int size, int npe)
      : head_(head), size_(size), cells_(size, head), fields_(kFields, 1),
        npe_(npe) {
    for (int i = 1; i <= kFields; ++i) {
      fields_[i] = ShiftedVector<Scalar>(size, head);
    }
  }
  CellGroup() = default;
  CellGroup(CellGroup const &) = delete;
  CellGroup(CellGroup &&) noexcept = default;
  CellGroup &operator=(CellGroup const &) = delete;
  CellGroup &operator=(CellGroup &&) noexcept = default;
  ~CellGroup() noexcept = default;

  Int head() const {
    return head_;
  }
  Int size() const {
    return size_;
  }
  Int tail() const {
    return head_ + size_;
  }
  int npe() const {
    return npe_;
  }
  bool has(int i_cell) const {
    return head() <= i_cell && i_cell < tail();
  }
  const Cell &operator[](Int i_cell) const {
    return cells_[i_cell];
  }
  Cell &operator[](Int i_cell) {
    return cells_[i_cell];
  }
  Cell const &at(Int i_cell) const {
    return cells_.at(i_cell);
  }
  Cell &at(Int i_cell) {
    return cells_.at(i_cell);
  }
  auto begin() {
    return cells_.begin();
  }
  auto end() {
    return cells_.end();
  }
  auto begin() const {
    return cells_.begin();
  }
  auto end() const {
    return cells_.end();
  }
  auto cbegin() const {
    return cells_.cbegin();
  }
  auto cend() const {
    return cells_.cend();
  }
  ShiftedVector<Scalar> const &GetField(Int i_field) const {
    return fields_.at(i_field);
  }
  ShiftedVector<Scalar> &GetField(Int i_field) {
    return fields_.at(i_field);
  }
  void GatherFields() {
    for (int i_cell = head(); i_cell < tail(); ++i_cell) {
      const auto &cell = cells_.at(i_cell);
      const auto &coeff = cell.projection_.coeff();
      for (int i_field = 1; i_field <= kFields; ++i_field) {
        fields_.at(i_field).at(i_cell) = coeff.reshaped()[i_field-1];
      }
    }
  }
  void ScatterFields() {
    for (int i_cell = head(); i_cell < tail(); ++i_cell) {
      auto &cell = cells_.at(i_cell);
      auto &coeff = cell.projection_.coeff();
      for (int i_field = 1; i_field <= kFields; ++i_field) {
        coeff.reshaped()[i_field-1] = fields_.at(i_field).at(i_cell);
      }
    }
  }
};

template <typename Int, int D, class R>
class Part {
 public:
  constexpr static int kDegrees = D;
  using Riemann = R;
  using Face = cgns::Face<Int, kDegrees, Riemann>;
  using Cell = cgns::Cell<Int, kDegrees, Riemann>;
  using Scalar = typename Riemann::Scalar;
  using Coord = typename Cell::Coord;
  using Value = typename Cell::Value;
  constexpr static int kComponents = Riemann::kComponents;
  constexpr static int kDimensions = Riemann::kDimensions;

 private:
  struct Connectivity {
    ShiftedVector<Int> index;
    std::vector<Int> nodes;
    cgsize_t first, last, local_first, local_last;
    ElementType_t type;
    char name[33];
  };
  using Mat3x1 = algebra::Matrix<Scalar, 3, 1>;
  using NodeInfo = cgns::NodeInfo<Int>;
  using CellInfo = cgns::CellInfo<Int>;
  using NodeGroup = cgns::NodeGroup<Int, Scalar>;
  using CellGroup = cgns::CellGroup<Int, kDegrees, R>;
  static constexpr int kLineWidth = 128;
  static constexpr int kFields = CellGroup::kFields;
  static constexpr int i_base = 1;
  static constexpr int i_grid = 1;
  static constexpr auto kIntType
      = sizeof(Int) == 8 ? CGNS_ENUMV(LongInteger) : CGNS_ENUMV(Integer);
  static constexpr auto kRealType
      = sizeof(Scalar) == 8 ? CGNS_ENUMV(RealDouble) : CGNS_ENUMV(RealSingle);
  static const MPI_Datatype kMpiIntType;
  static const MPI_Datatype kMpiRealType;

 private:
  using GaussOnTriangle = mini::select_t<kDegrees,
    integrator::Triangle<Scalar, kDimensions, 1>,
    integrator::Triangle<Scalar, kDimensions, 3>,
    integrator::Triangle<Scalar, kDimensions, 6>,
    integrator::Triangle<Scalar, kDimensions, 12>>;
  using GaussOnQuadrangle = mini::select_t<kDegrees,
    integrator::Quadrangle<Scalar, kDimensions, 1, 1>,
    integrator::Quadrangle<Scalar, kDimensions, 2, 2>,
    integrator::Quadrangle<Scalar, kDimensions, 3, 3>,
    integrator::Quadrangle<Scalar, kDimensions, 4, 4>>;
  using GaussOnTetra = mini::select_t<kDegrees,
    integrator::Tetra<Scalar, 1>,
    integrator::Tetra<Scalar, 4>,
    integrator::Tetra<Scalar, 14>,
    integrator::Tetra<Scalar, 24>>;
  using GaussOnHexa = mini::select_t<kDegrees,
    integrator::Hexa<Scalar, 1, 1, 1>,
    integrator::Hexa<Scalar, 2, 2, 2>,
    integrator::Hexa<Scalar, 3, 3, 3>,
    integrator::Hexa<Scalar, 4, 4, 4>>;

 public:
  Part(std::string const &directory, int rank)
      : directory_(directory), cgns_file_(directory + "/shuffled.cgns"),
        rank_(rank) {
    int i_file;
    if (cgp_open(cgns_file_.c_str(), CG_MODE_READ, &i_file)) {
      cgp_error_exit();
    }
    auto txt_file = directory + "/partition/" + std::to_string(rank) + ".txt";
    auto istrm = std::ifstream(txt_file);
    BuildLocalNodes(istrm, i_file);
    auto [recv_nodes, recv_coords] = ShareGhostNodes(istrm);
    BuildGhostNodes(recv_nodes, recv_coords);
    BuildLocalCells(istrm, i_file);
    auto ghost_adj = BuildAdj(istrm);
    auto recv_cells = ShareGhostCells(ghost_adj);
    auto m_to_recv_cells = BuildGhostCells(ghost_adj, recv_cells);
    FillCellPtrs(ghost_adj);
    AddLocalCellId();
    BuildLocalFaces();
    BuildGhostFaces(ghost_adj, recv_cells, m_to_recv_cells);
    BuildBoundaryFaces(istrm, i_file);
    if (cgp_close(i_file)) {
      cgp_error_exit();
    }
  }
  void SetFieldNames(std::array<std::string, kComponents> const &names) {
    field_names_ = names;
  }
  const std::string &GetFieldName(int i) const {
    return field_names_.at(i);
  }
  const std::string &GetDirectoryName() const {
    return directory_;
  }
  int rank() const {
    return rank_;
  }

 private:
  int SolnNameToId(int i_file, int i_base, int i_zone,
      std::string const &name) {
    int n_solns;
    if (cg_nsols(i_file, i_base, i_zone, &n_solns)) {
      cgp_error_exit();
    }
    int i_soln;
    for (i_soln = 1; i_soln <= n_solns; ++i_soln) {
      char soln_name[33];
      GridLocation_t loc;
      if (cg_sol_info(i_file, i_base, i_zone, i_soln, soln_name, &loc)) {
        cgp_error_exit();
      }
      if (soln_name == name) {
        break;
      }
    }
    assert(i_soln <= n_solns);
    return i_soln;
  }
  int FieldNameToId(int i_file, int i_base, int i_zone, int i_soln,
      std::string const &name) {
    int n_fields;
    if (cg_nfields(i_file, i_base, i_zone, i_soln, &n_fields)) {
      cgp_error_exit();
    }
    int i_field;
    for (i_field = 1; i_field <= n_fields; ++i_field) {
      char field_name[33];
      DataType_t data_t;
      if (cg_field_info(i_file, i_base, i_zone, i_soln, i_field,
          &data_t, field_name)) {
        cgp_error_exit();
      }
      if (field_name == name) {
        break;
      }
    }
    assert(i_field <= n_fields);
    return i_field;
  }
  void BuildLocalNodes(std::ifstream &istrm, int i_file) {
    if (cg_base_read(i_file, i_base, base_name_, &cell_dim_, &phys_dim_)) {
      cgp_error_exit();
    }
    char line[kLineWidth];
    istrm.getline(line, kLineWidth); assert(line[0] == '#');
    // node coordinates
    while (istrm.getline(line, kLineWidth) && line[0] != '#') {
      int i_zone, head, tail;
      std::sscanf(line, "%d %d %d", &i_zone, &head, &tail);
      auto node_group = NodeGroup(head, tail - head);
      if (cg_zone_read(i_file, i_base, i_zone,
          node_group.zone_name_, node_group.zone_size_[0])) {
        cgp_error_exit();
      }
      cgsize_t range_min[] = { head };
      cgsize_t range_max[] = { tail - 1 };
      if (cgp_coord_read_data(i_file, i_base, i_zone, 1,
          range_min, range_max, node_group.x_.data()) ||
          cgp_coord_read_data(i_file, i_base, i_zone, 2,
          range_min, range_max, node_group.y_.data()) ||
          cgp_coord_read_data(i_file, i_base, i_zone, 3,
          range_min, range_max, node_group.z_.data())) {
        cgp_error_exit();
      }
      cgsize_t mem_dimensions[] = { tail - head };
      cgsize_t mem_range_min[] = { 1 };
      cgsize_t mem_range_max[] = { mem_dimensions[0] };
      int i_sol = SolnNameToId(i_file, i_base, i_zone, "DataOnNodes");
      int i_field = FieldNameToId(i_file, i_base, i_zone, i_sol, "MetisIndex");
      if (cgp_field_general_read_data(i_file, i_base, i_zone, i_sol, i_field,
          range_min, range_max, kIntType,
          1, mem_dimensions, mem_range_min, mem_range_max,
          node_group.metis_id_.data())) {
        cgp_error_exit();
      }
      for (int i_node = head; i_node < tail; ++i_node) {
        auto m_node = node_group.metis_id_[i_node];
        m_to_node_info_[m_node] = NodeInfo(i_zone, i_node);
      }
      local_nodes_[i_zone] = std::move(node_group);
    }
  }
  std::pair<
    std::map<Int, std::vector<Int>>,
    std::vector<std::vector<Scalar>>
  > ShareGhostNodes(std::ifstream &istrm) {
    char line[kLineWidth];
    // send nodes info
    std::map<Int, std::vector<Int>> send_nodes;
    while (istrm.getline(line, kLineWidth) && line[0] != '#') {
      int i_part, m_node;
      std::sscanf(line, "%d %d", &i_part, &m_node);
      send_nodes[i_part].emplace_back(m_node);
    }
    std::vector<MPI_Request> requests;
    std::vector<std::vector<Scalar>> send_bufs;
    for (auto &[i_part, nodes] : send_nodes) {
      auto &coords = send_bufs.emplace_back();
      for (auto m_node : nodes) {
        auto &info = m_to_node_info_.at(m_node);
        auto const &coord = GetCoord(info.i_zone, info.i_node);
        coords.emplace_back(coord[0]);
        coords.emplace_back(coord[1]);
        coords.emplace_back(coord[2]);
      }
      assert(std::is_sorted(nodes.begin(), nodes.end()));
      int n_reals = 3 * nodes.size();
      int tag = i_part;
      auto &request = requests.emplace_back();
      MPI_Isend(coords.data(), n_reals, kMpiRealType, i_part, tag,
          MPI_COMM_WORLD, &request);
    }
    // recv nodes info
    std::map<Int, std::vector<Int>> recv_nodes;
    while (istrm.getline(line, kLineWidth) && line[0] != '#') {
      int i_part, m_node, i_zone, i_node;
      std::sscanf(line, "%d %d %d %d", &i_part, &m_node, &i_zone, &i_node);
      recv_nodes[i_part].emplace_back(m_node);
      m_to_node_info_[m_node] = cgns::NodeInfo<Int>(i_zone, i_node);
    }
    std::vector<std::vector<Scalar>> recv_coords;
    for (auto &[i_part, nodes] : recv_nodes) {
      assert(std::is_sorted(nodes.begin(), nodes.end()));
      int n_reals = 3 * nodes.size();
      auto &coords = recv_coords.emplace_back(std::vector<Scalar>(n_reals));
      int tag = rank_;
      auto &request = requests.emplace_back();
      MPI_Irecv(coords.data(), n_reals, kMpiRealType, i_part, tag,
          MPI_COMM_WORLD, &request);
    }
    // wait until all send/recv finish
    std::vector<MPI_Status> statuses(requests.size());
    MPI_Waitall(requests.size(), requests.data(), statuses.data());
    requests.clear();
    return { recv_nodes, recv_coords };
  }
  void BuildGhostNodes(std::map<Int, std::vector<Int>> const &recv_nodes,
      std::vector<std::vector<Scalar>> const &recv_coords) {
    // copy node coordinates from buffer to member
    int i_source = 0;
    for (auto &[i_part, nodes] : recv_nodes) {
      auto *xyz = recv_coords[i_source++].data();
      for (auto m_node : nodes) {
        auto &info = m_to_node_info_[m_node];
        ghost_nodes_[info.i_zone][info.i_node] = { xyz[0], xyz[1] , xyz[2] };
        xyz += 3;
      }
    }
  }
  auto BuildTetraUptr(int i_zone, Int const *i_node_list) const {
    return std::make_unique<GaussOnTetra>(
        GetCoord(i_zone, i_node_list[0]), GetCoord(i_zone, i_node_list[1]),
        GetCoord(i_zone, i_node_list[2]), GetCoord(i_zone, i_node_list[3]));
  }
  auto BuildHexaUptr(int i_zone, Int const *i_node_list) const {
    return std::make_unique<GaussOnHexa>(
        GetCoord(i_zone, i_node_list[0]), GetCoord(i_zone, i_node_list[1]),
        GetCoord(i_zone, i_node_list[2]), GetCoord(i_zone, i_node_list[3]),
        GetCoord(i_zone, i_node_list[4]), GetCoord(i_zone, i_node_list[5]),
        GetCoord(i_zone, i_node_list[6]), GetCoord(i_zone, i_node_list[7]));
  }
  auto BuildGaussForCell(int npe, int i_zone, Int const *i_node_list) const {
    std::unique_ptr<integrator::Cell<Scalar>> gauss_uptr;
    switch (npe) {
      case 4:
        gauss_uptr = BuildTetraUptr(i_zone, i_node_list); break;
      case 8:
        gauss_uptr = BuildHexaUptr(i_zone, i_node_list); break;
      default:
        assert(false);
        break;
    }
    return gauss_uptr;
  }
  auto BuildTriangleUptr(int i_zone, Int const *i_node_list) const {
    auto gauss_uptr = std::make_unique<GaussOnTriangle>(
        GetCoord(i_zone, i_node_list[0]), GetCoord(i_zone, i_node_list[1]),
        GetCoord(i_zone, i_node_list[2]));
    gauss_uptr->BuildNormalFrames();
    return gauss_uptr;
  }
  auto BuildQuadrangleUptr(int i_zone, Int const *i_node_list) const {
    auto gauss_uptr = std::make_unique<GaussOnQuadrangle>(
        GetCoord(i_zone, i_node_list[0]), GetCoord(i_zone, i_node_list[1]),
        GetCoord(i_zone, i_node_list[2]), GetCoord(i_zone, i_node_list[3]));
    gauss_uptr->BuildNormalFrames();
    return gauss_uptr;
  }
  auto BuildGaussForFace(int npe, int i_zone, Int const *i_node_list) const {
    std::unique_ptr<integrator::Face<Scalar, kDimensions>> gauss_uptr;
    switch (npe) {
      case 3:
        gauss_uptr = BuildTriangleUptr(i_zone, i_node_list); break;
      case 4:
        gauss_uptr = BuildQuadrangleUptr(i_zone, i_node_list); break;
      default:
        assert(false);
        break;
    }
    return gauss_uptr;
  }
  static void SortNodesOnFace(int npe, Int const *cell, Int *face) {
    switch (npe) {
      case 4:
        GaussOnTetra::SortNodesOnFace(cell, face);
        break;
      case 8:
        GaussOnHexa::SortNodesOnFace(cell, face);
        break;
      default:
        assert(false);
        break;
    }
  }
  void BuildLocalCells(std::ifstream &istrm, int i_file) {
    char line[kLineWidth];
    // build local cells
    while (istrm.getline(line, kLineWidth) && line[0] != '#') {
      int i_zone, i_sect, head, tail;
      std::sscanf(line, "%d %d %d %d", &i_zone, &i_sect, &head, &tail);
      cgsize_t range_min[] = { head };
      cgsize_t range_max[] = { tail - 1 };
      cgsize_t mem_dimensions[] = { tail - head };
      cgsize_t mem_range_min[] = { 1 };
      cgsize_t mem_range_max[] = { mem_dimensions[0] };
      ShiftedVector<Int> metis_ids(mem_dimensions[0], head);
      int i_sol = SolnNameToId(i_file, i_base, i_zone, "DataOnCells");
      int i_field = FieldNameToId(i_file, i_base, i_zone, i_sol, "MetisIndex");
      if (cgp_field_general_read_data(i_file, i_base, i_zone, i_sol, i_field,
          range_min, range_max, kIntType,
          1, mem_dimensions, mem_range_min, mem_range_max, metis_ids.data())) {
        cgp_error_exit();
      }
      int x, y;
      auto &conn = connectivities_[i_zone][i_sect];
      auto &index = conn.index;
      auto &nodes = conn.nodes;
      index = ShiftedVector<Int>(mem_dimensions[0] + 1, head);
      if (cg_section_read(i_file, i_base, i_zone, i_sect,
          conn.name, &conn.type, &conn.first, &conn.last, &x, &y)) {
        cgp_error_exit();
      }
      int npe; cg_npe(conn.type, &npe);
      for (int i_cell = head; i_cell < tail; ++i_cell) {
        auto m_cell = metis_ids[i_cell];
        m_to_cell_info_[m_cell] = CellInfo(i_zone, i_sect, i_cell, npe);
      }
      nodes.resize(npe * mem_dimensions[0]);
      for (int i = 0; i < index.size(); ++i) {
        index.at(head + i) = npe * i;
      }
      conn.local_first = range_min[0];
      conn.local_last = range_max[0];
      if (cgp_elements_read_data(i_file, i_base, i_zone, i_sect,
          range_min[0], range_max[0], nodes.data())) {
        cgp_error_exit();
      }
      auto cell_group = CellGroup(head, tail - head, npe);
      local_cells_[i_zone][i_sect] = std::move(cell_group);
      for (int i_cell = head; i_cell < tail; ++i_cell) {
        auto *i_node_list = &nodes[(i_cell - head) * npe];
        auto gauss_uptr = BuildGaussForCell(npe, i_zone, i_node_list);
        auto cell = Cell(std::move(gauss_uptr), metis_ids[i_cell]);
        local_cells_[i_zone][i_sect][i_cell] = std::move(cell);
      }
    }
  }
  void AddLocalCellId() {
    for (auto &[i_zone, zone] : local_cells_) {
      for (auto &[i_sect, sect] : zone) {
        for (auto &cell : sect) {
          if (cell.inner()) {
            inner_cells_.emplace_back(&cell);
          } else {
            inter_cells_.emplace_back(&cell);
          }
        }
      }
    }
    Int id = 0;
    for (auto cell_ptr : inner_cells_) {
      cell_ptr->id_ = id++;
    }
    for (auto cell_ptr : inter_cells_) {
      cell_ptr->id_ = id++;
    }
  }
  struct GhostAdj {
    std::map<Int, std::map<Int, Int>>
        send_npes, recv_npes;  // [i_part][m_cell] -> npe
    std::vector<std::pair<Int, Int>>
        m_cell_pairs;
  };
  GhostAdj BuildAdj(std::ifstream &istrm) {
    char line[kLineWidth];
    // local adjacency
    while (istrm.getline(line, kLineWidth) && line[0] != '#') {
      int i, j;
      std::sscanf(line, "%d %d", &i, &j);
      local_adjs_.emplace_back(i, j);
    }
    // ghost adjacency
    auto ghost_adj = GhostAdj();
    auto &send_npes = ghost_adj.send_npes;
    auto &recv_npes = ghost_adj.recv_npes;
    auto &m_cell_pairs = ghost_adj.m_cell_pairs;
    while (istrm.getline(line, kLineWidth) && line[0] != '#') {
      int p, i, j, npe_i, npe_j;
      std::sscanf(line, "%d %d %d %d %d", &p, &i, &j, &npe_i, &npe_j);
      send_npes[p][i] = npe_i;
      recv_npes[p][j] = npe_j;
      m_cell_pairs.emplace_back(i, j);
    }
    return ghost_adj;
  }
  auto ShareGhostCells(GhostAdj const &ghost_adj) {
    auto &send_npes = ghost_adj.send_npes;
    auto &recv_npes = ghost_adj.recv_npes;
    // send cell.i_zone and cell.node_id_list
    std::vector<std::vector<Int>> send_cells;
    std::vector<MPI_Request> requests;
    for (auto &[i_part, npes] : send_npes) {
      auto &send_buf = send_cells.emplace_back();
      for (auto &[m_cell, npe] : npes) {
        auto &info = m_to_cell_info_[m_cell];
        assert(npe == info.npe);
        Int i_zone = info.i_zone, i_sect = info.i_sect, i_cell = info.i_cell;
        auto &conn = connectivities_.at(i_zone).at(i_sect);
        auto &index = conn.index;
        auto &nodes = conn.nodes;
        auto *i_node_list = &(nodes[index[i_cell]]);
        send_buf.emplace_back(i_zone);
        for (int i = 0; i < npe; ++i) {
          send_buf.emplace_back(i_node_list[i]);
        }
      }
      int n_ints = send_buf.size();
      int tag = i_part;
      auto &request = requests.emplace_back();
      MPI_Isend(send_buf.data(), n_ints, kMpiIntType, i_part, tag,
          MPI_COMM_WORLD, &request);
    }
    // recv cell.i_zone and cell.node_id_list
    std::vector<std::vector<Int>> recv_cells;
    for (auto &[i_part, npes] : recv_npes) {
      auto &recv_buf = recv_cells.emplace_back();
      int n_ints = 0;
      for (auto &[m_cell, npe] : npes) {
        ++n_ints;
        n_ints += npe;
      }
      int tag = rank_;
      recv_buf.resize(n_ints);
      auto &request = requests.emplace_back();
      MPI_Irecv(recv_buf.data(), n_ints, kMpiIntType, i_part, tag,
          MPI_COMM_WORLD, &request);
    }
    // wait until all send/recv finish
    std::vector<MPI_Status> statuses(requests.size());
    MPI_Waitall(requests.size(), requests.data(), statuses.data());
    requests.clear();
    return recv_cells;
  }
  struct GhostCellInfo {
    int source, head, npe;
  };
  std::unordered_map<Int, GhostCellInfo> BuildGhostCells(
      GhostAdj const &ghost_adj,
      std::vector<std::vector<Int>> const &recv_cells) {
    auto &recv_npes = ghost_adj.recv_npes;
    // build ghost cells
    std::unordered_map<Int, GhostCellInfo> m_to_recv_cells;
    int i_source = 0;
    for (auto &[i_part, npes] : recv_npes) {
      auto &recv_buf = recv_cells.at(i_source);
      int index = 0;
      for (auto &[m_cell, npe] : npes) {
        m_to_recv_cells[m_cell].source = i_source;
        m_to_recv_cells[m_cell].head = index + 1;
        m_to_recv_cells[m_cell].npe = npe;
        int i_zone = recv_buf[index++];
        auto *i_node_list = &recv_buf[index];
        auto gauss_uptr = BuildGaussForCell(npe, i_zone, i_node_list);
        auto cell = Cell(std::move(gauss_uptr), m_cell);
        ghost_cells_[m_cell] = std::move(cell);
        index += npe;
      }
      ++i_source;
    }
    return m_to_recv_cells;
  }
  void FillCellPtrs(GhostAdj const &ghost_adj) {
    // fill `send_cell_ptrs_`
    for (auto &[i_part, npes] : ghost_adj.send_npes) {
      auto &curr_part = send_cell_ptrs_[i_part];
      assert(curr_part.empty());
      for (auto &[m_cell, npe] : npes) {
        auto &info = m_to_cell_info_[m_cell];
        auto &cell = local_cells_.at(info.i_zone).at(info.i_sect)[info.i_cell];
        cell.inner_ = false;
        curr_part.emplace_back(&cell);
      }
      send_coeffs_.emplace_back(npes.size() * kFields);
    }
    // fill `recv_cell_ptrs_`
    for (auto &[i_part, npes] : ghost_adj.recv_npes) {
      auto &curr_part = recv_cell_ptrs_[i_part];
      assert(curr_part.empty());
      for (auto &[m_cell, npe] : npes) {
        auto &cell = ghost_cells_.at(m_cell);
        curr_part.emplace_back(&cell);
      }
      recv_coeffs_.emplace_back(npes.size() * kFields);
    }
    assert(send_cell_ptrs_.size() == send_coeffs_.size());
    assert(recv_cell_ptrs_.size() == recv_coeffs_.size());
    requests_.resize(send_coeffs_.size() + recv_coeffs_.size());
  }
  void BuildLocalFaces() {
    // build local faces
    for (auto [m_holder, m_sharer] : local_adjs_) {
      auto &holder_info = m_to_cell_info_[m_holder];
      auto &sharer_info = m_to_cell_info_[m_sharer];
      auto i_zone = holder_info.i_zone;
      // find the common nodes of the holder and the sharer
      auto i_node_cnt = std::unordered_map<Int, Int>();
      auto &conn_i_zone = connectivities_.at(i_zone);
      auto &holder_conn = conn_i_zone.at(holder_info.i_sect);
      auto &sharer_conn = conn_i_zone.at(sharer_info.i_sect);
      auto &holder_nodes = holder_conn.nodes;
      auto &sharer_nodes = sharer_conn.nodes;
      auto holder_head = holder_conn.index[holder_info.i_cell];
      auto sharer_head = sharer_conn.index[sharer_info.i_cell];
      for (int i = 0; i < holder_info.npe; ++i)
        ++i_node_cnt[holder_nodes[holder_head + i]];
      for (int i = 0; i < sharer_info.npe; ++i)
        ++i_node_cnt[sharer_nodes[sharer_head + i]];
      auto common_nodes = std::vector<Int>();
      common_nodes.reserve(4);
      for (auto [i_node, cnt] : i_node_cnt)
        if (cnt == 2)
          common_nodes.emplace_back(i_node);
      int face_npe = common_nodes.size();
      // let the normal vector point from holder to sharer
      // see http://cgns.github.io/CGNS_docs_current/sids/conv.figs/hexa_8.png
      auto &zone = local_cells_[i_zone];
      auto &holder = zone[holder_info.i_sect][holder_info.i_cell];
      auto &sharer = zone[sharer_info.i_sect][sharer_info.i_cell];
      holder.adj_cells_.emplace_back(&sharer);
      sharer.adj_cells_.emplace_back(&holder);
      auto *i_node_list = common_nodes.data();
      SortNodesOnFace(holder_info.npe, &holder_nodes[holder_head], i_node_list);
      auto gauss_uptr = BuildGaussForFace(face_npe, i_zone, i_node_list);
      auto face_uptr = std::make_unique<Face>(
          std::move(gauss_uptr), &holder, &sharer, local_faces_.size());
      holder.adj_faces_.emplace_back(face_uptr.get());
      sharer.adj_faces_.emplace_back(face_uptr.get());
      local_faces_.emplace_back(std::move(face_uptr));
    }
  }
  void BuildGhostFaces(GhostAdj const &ghost_adj,
      std::vector<std::vector<Int>> const &recv_cells,
      std::unordered_map<Int, GhostCellInfo> const &m_to_recv_cells) {
    auto &m_cell_pairs = ghost_adj.m_cell_pairs;
    // build ghost faces
    for (auto [m_holder, m_sharer] : m_cell_pairs) {
      auto &holder_info = m_to_cell_info_[m_holder];
      auto &sharer_info = m_to_recv_cells.at(m_sharer);
      auto i_zone = holder_info.i_zone;
      // find the common nodes of the holder and the sharer
      auto i_node_cnt = std::unordered_map<Int, Int>();
      auto &holder_conn = connectivities_.at(i_zone).at(holder_info.i_sect);
      auto &holder_nodes = holder_conn.nodes;
      auto &sharer_nodes = recv_cells[sharer_info.source];
      auto holder_head = holder_conn.index[holder_info.i_cell];
      auto sharer_head = sharer_info.head;
      for (int i = 0; i < holder_info.npe; ++i)
        ++i_node_cnt[holder_nodes[holder_head + i]];
      for (int i = 0; i < sharer_info.npe; ++i)
        ++i_node_cnt[sharer_nodes[sharer_head + i]];
      auto common_nodes = std::vector<Int>();
      common_nodes.reserve(4);
      for (auto [i_node, cnt] : i_node_cnt) {
        if (cnt == 2)
          common_nodes.emplace_back(i_node);
      }
      int face_npe = common_nodes.size();
      // let the normal vector point from holder to sharer
      // see http://cgns.github.io/CGNS_docs_current/sids/conv.figs/hexa_8.png
      auto &zone = local_cells_[i_zone];
      auto &holder = zone[holder_info.i_sect][holder_info.i_cell];
      auto &sharer = ghost_cells_.at(m_sharer);
      holder.adj_cells_.emplace_back(&sharer);
      auto *i_node_list = common_nodes.data();
      SortNodesOnFace(holder_info.npe, &holder_nodes[holder_head], i_node_list);
      auto gauss_uptr = BuildGaussForFace(face_npe, i_zone, i_node_list);
      auto face_uptr = std::make_unique<Face>(
          std::move(gauss_uptr), &holder, &sharer,
          local_faces_.size() + ghost_faces_.size());
      holder.adj_faces_.emplace_back(face_uptr.get());
      ghost_faces_.emplace_back(std::move(face_uptr));
    }
  }

 public:
  template <class Callable>
  void Project(Callable &&new_func) {
    for (auto &[i_zone, sects] : local_cells_) {
      for (auto &[i_sect, cells] : sects) {
        for (auto &cell : cells) {
          cell.Project(new_func);
        }
      }
    }
  }

  template <class Callable>
  Value MeasureL1Error(Callable &&exact_solution, Scalar t_next) const {
    Value l1_error; l1_error.setZero();
    auto visitor = [&t_next, &exact_solution, &l1_error](Cell const &cell){
      auto func = [&t_next, &exact_solution, &cell](Coord const &xyz){
        auto value = cell.GetValue(xyz);
        value -= exact_solution(xyz, t_next);
        value = value.cwiseAbs();
        return value;
      };
      l1_error += mini::integrator::Integrate(func, cell.gauss());
    };
    ForEachConstLocalCell(visitor);
    return l1_error;
  }

  Int CountLocalCells() const {
    return inner_cells_.size() + inter_cells_.size();
  }
  void GatherSolutions() {
    int n_zones = local_nodes_.size();
    for (int i_zone = 1; i_zone <= n_zones; ++i_zone) {
      int n_sects = local_cells_[i_zone].size();
      for (int i_sect = 1; i_sect <= n_sects; ++i_sect) {
        local_cells_[i_zone][i_sect].GatherFields();
      }
    }
  }
  void ScatterSolutions() {
    int n_zones = local_nodes_.size();
    for (int i_zone = 1; i_zone <= n_zones; ++i_zone) {
      int n_sects = local_cells_[i_zone].size();
      for (int i_sect = 1; i_sect <= n_sects; ++i_sect) {
        local_cells_[i_zone][i_sect].ScatterFields();
      }
    }
  }
  void WriteSolutions(std::string const &soln_name = "0") const {
    int n_zones = local_nodes_.size();
    int i_file, i;
    auto cgns_file = directory_ + "/" + soln_name + ".cgns";
    if (rank_ == 0) {
      if (cg_open(cgns_file.c_str(), CG_MODE_WRITE, &i_file)) {
        cgp_error_exit();
      }
      if (cg_base_write(i_file, base_name_, cell_dim_, phys_dim_, &i)
          || i != i_base) {
        cgp_error_exit();
      }
      for (int i_zone = 1; i_zone <= n_zones; ++i_zone) {
        auto &node_group = local_nodes_.at(i_zone);
        if (cg_zone_write(i_file, i_base, node_group.zone_name_,
            node_group.zone_size_[0], CGNS_ENUMV(Unstructured), &i)
            || i != i_zone) {
          cgp_error_exit();
        }
        if (cg_grid_write(i_file, i_base, i_zone, "GridCoordinates", &i)
            || i != i_grid) {
          cgp_error_exit();
        }
      }
      if (cg_close(i_file)) {
        cgp_error_exit();
      }
    }
    MPI_Barrier(MPI_COMM_WORLD);
    if (cgp_open(cgns_file.c_str(), CG_MODE_MODIFY, &i_file)) {
      cgp_error_exit();
    }
    for (int i_zone = 1; i_zone <= n_zones; ++i_zone) {
      // write node coordinates
      auto &node_group = local_nodes_.at(i_zone);
      cgsize_t range_min[] = { node_group.head() };
      cgsize_t range_max[] = { node_group.tail() - 1 };
      auto data_type = std::is_same_v<Scalar, double> ?
          CGNS_ENUMV(RealDouble) : CGNS_ENUMV(RealSingle);
      int i_coord;
      if (cgp_coord_write(i_file, i_base, i_zone, data_type, "CoordinateX",
          &i_coord) || cgp_coord_write_data(i_file, i_base, i_zone, i_coord,
          range_min, range_max, node_group.x_.data())) {
        cgp_error_exit();
      }
      if (cgp_coord_write(i_file, i_base, i_zone, data_type, "CoordinateY",
          &i_coord) || cgp_coord_write_data(i_file, i_base, i_zone, i_coord,
          range_min, range_max, node_group.y_.data())) {
        cgp_error_exit();
      }
      if (cgp_coord_write(i_file, i_base, i_zone, data_type, "CoordinateZ",
          &i_coord) || cgp_coord_write_data(i_file, i_base, i_zone, i_coord,
          range_min, range_max, node_group.z_.data())) {
        cgp_error_exit();
      }
      int n_sects = connectivities_.at(i_zone).size();
      for (int i_sect = 1; i_sect <= n_sects; ++i_sect) {
        auto &sect = connectivities_.at(i_zone).at(i_sect);
        if (cgp_section_write(i_file, i_base, i_zone, sect.name,
            sect.type, sect.first, sect.last, 0/* n_boundary */, &i)
            || i != i_sect) {
          cgp_error_exit();
        }
        if (cgp_elements_write_data(i_file, i_base, i_zone, i_sect,
            sect.local_first, sect.local_last, sect.nodes.data())) {
          cgp_error_exit();
        }
      }
      int n_solns;
      if (cg_nsols(i_file, i_base, i_zone, &n_solns)) {
        cgp_error_exit();
      }
      int i_soln;
      if (cg_sol_write(i_file, i_base, i_zone, "DataOnCells",
          CGNS_ENUMV(CellCenter), &i_soln)) {
        cgp_error_exit();
      }
      auto &zone = local_cells_.at(i_zone);
      for (int i_field = 1; i_field <= kFields; ++i_field) {
        int n_sects = zone.size();
        for (int i_sect = 1; i_sect <= n_sects; ++i_sect) {
          auto &section = zone.at(i_sect);
          auto field_name = "Field" + std::to_string(i_field);
          int field_id;
          if (cgp_field_write(i_file, i_base, i_zone, i_soln, kRealType,
              field_name.c_str(),  &field_id)) {
            cgp_error_exit();
          }
          assert(field_id == i_field);
          cgsize_t first[] = { section.head() };
          cgsize_t last[] = { section.tail() - 1 };
          if (cgp_field_write_data(i_file, i_base, i_zone, i_soln, i_field,
              first, last, section.GetField(i_field).data())) {
            cgp_error_exit();
          }
        }
      }
    }
    if (cgp_close(i_file)) {
      cgp_error_exit();
    }
  }
  void ReadSolutions(std::string const &soln_name) {
    int n_zones = local_nodes_.size();
    int i_file;
    auto cgns_file = directory_ + "/" + soln_name + ".cgns";
    if (cgp_open(cgns_file.c_str(), CG_MODE_READ, &i_file)) {
      cgp_error_exit();
    }
    for (int i_zone = 1; i_zone <= n_zones; ++i_zone) {
      auto &zone = local_cells_.at(i_zone);
      int n_solns;
      if (cg_nsols(i_file, i_base, i_zone, &n_solns)) {
        cgp_error_exit();
      }
      int i_soln = SolnNameToId(i_file, i_base, i_zone, "DataOnCells");
      for (int i_field = 1; i_field <= kFields; ++i_field) {
        int n_sects = zone.size();
        for (int i_sect = 1; i_sect <= n_sects; ++i_sect) {
          auto &section = zone.at(i_sect);
          char field_name[33];
          DataType_t data_type;
          if (cg_field_info(i_file, i_base, i_zone, i_soln, i_field,
              &data_type, field_name)) {
            cgp_error_exit();
          }
          cgsize_t first[] = { section.head() };
          cgsize_t last[] = { section.tail() - 1 };
          if (cgp_field_read_data(i_file, i_base, i_zone, i_soln, i_field,
              first, last, section.GetField(i_field).data())) {
            cgp_error_exit();
          }
        }
      }
    }
    if (cgp_close(i_file)) {
      cgp_error_exit();
    }
  }
  void ShareGhostCellCoeffs() {
    int i_req = 0;
    // send cell.projection_.coeff_
    int i_buf = 0;
    for (auto &[i_part, cell_ptrs] : send_cell_ptrs_) {
      auto &send_buf = send_coeffs_[i_buf++];
      int i_real = 0;
      for (auto *cell_ptr : cell_ptrs) {
        const auto &coeff = cell_ptr->projection_.coeff();
        static_assert(kFields == Cell::K * Cell::N);
        for (int c = 0; c < Cell::N; ++c) {
          for (int r = 0; r < Cell::K; ++r) {
            send_buf[i_real++] = coeff(r, c);
          }
        }
      }
      int tag = i_part;
      auto &request = requests_[i_req++];
      MPI_Isend(send_buf.data(), send_buf.size(), kMpiRealType, i_part, tag,
          MPI_COMM_WORLD, &request);
    }
    // recv cell.projection_.coeff_
    i_buf = 0;
    for (auto &[i_part, cell_ptrs] : recv_cell_ptrs_) {
      auto &recv_buf = recv_coeffs_[i_buf++];
      int tag = rank_;
      auto &request = requests_[i_req++];
      MPI_Irecv(recv_buf.data(), recv_buf.size(), kMpiRealType, i_part, tag,
          MPI_COMM_WORLD, &request);
    }
    assert(i_req == send_coeffs_.size() + recv_coeffs_.size());
  }
  void UpdateGhostCellCoeffs() {
    // wait until all send/recv finish
    std::vector<MPI_Status> statuses(requests_.size());
    MPI_Waitall(requests_.size(), requests_.data(), statuses.data());
    int req_size = requests_.size();
    requests_.clear();
    requests_.resize(req_size);
    // update coeffs
    int i_buf = 0;
    for (auto &[i_part, cell_ptrs] : recv_cell_ptrs_) {
      auto *recv_buf = recv_coeffs_[i_buf++].data();
      for (auto *cell_ptr : cell_ptrs) {
        cell_ptr->projection_.UpdateCoeffs(recv_buf);
        recv_buf += kFields;
      }
    }
  }

  template <typename Callable>
  void Reconstruct(Callable &&limiter) {
    if (kDegrees == 0) {
      return;
    }
    // run the limiter on inner cells that need no ghost cells
    ShareGhostCellCoeffs();
    Reconstruct(limiter, inner_cells_.begin(), inner_cells_.end());
    // run the limiter on inter cells that need ghost cells
    UpdateGhostCellCoeffs();
    Reconstruct(limiter, inter_cells_.begin(), inter_cells_.end());
  }

  template <typename Limiter, typename CellPtrIter>
  void Reconstruct(Limiter &&limiter, CellPtrIter iter, CellPtrIter end) {
    auto troubled_cells = std::vector<Cell *>();
    while (iter != end) {
      Cell *cell_ptr = *iter++;
      if (limiter.IsNotSmooth(*cell_ptr)) {
        troubled_cells.push_back(cell_ptr);
      }
    }
    auto new_projections = std::vector<typename Cell::Projection>();
    for (Cell *cell_ptr : troubled_cells) {
      new_projections.emplace_back(limiter(*cell_ptr));
    }
    int i = 0;
    for (Cell *cell_ptr : troubled_cells) {
      cell_ptr->projection_.UpdateCoeffs(new_projections[i++].coeff());
    }
    assert(i == troubled_cells.size());
  }

  // Accessors:
  template<class Visitor>
  void ForEachConstLocalCell(Visitor &&visit) const {
    for (const auto &[i_zone, zone] : local_cells_) {
      for (const auto &[i_sect, sect] : zone) {
        for (const auto &cell : sect) {
          visit(cell);
        }
      }
    }
  }
  template<class Visitor>
  void ForEachConstLocalFace(Visitor &&visit) const {
    for (auto &face_uptr : local_faces_) {
      visit(*face_uptr);
    }
  }
  template<class Visitor>
  void ForEachConstGhostFace(Visitor &&visit) const {
    for (auto &face_uptr : ghost_faces_) {
      visit(*face_uptr);
    }
  }
  template<class Visitor>
  void ForEachConstBoundaryFace(Visitor &&visit) const {
    for (auto &[i_zone, zone] : bound_faces_) {
      for (auto &[i_sect, sect] : zone) {
        for (auto &face_uptr : sect) {
          visit(*face_uptr);
        }
      }
    }
  }
  template<class Visitor>
  void ForEachConstBoundaryFace(Visitor &&visit,
      std::string const &name) const {
    const auto &faces = *name_to_faces_.at(name);
    for (const auto &face_uptr : faces) {
      visit(*face_uptr);
    }
  }
  // Mutators:
  template<class Visitor>
  void ForEachLocalCell(Visitor &&visit) {
    for (auto &[i_zone, zone] : local_cells_) {
      for (auto &[i_sect, sect] : zone) {
        for (auto &cell : sect) {
          visit(&cell);
        }
      }
    }
  }
  template<class Visitor>
  void ForEachLocalFace(Visitor &&visit) {
    for (auto &face_uptr : local_faces_) {
      visit(face_uptr.get());
    }
  }
  template<class Visitor>
  void ForEachGhostFace(Visitor &&visit) {
    for (auto &face_uptr : ghost_faces_) {
      visit(face_uptr.get());
    }
  }
  template<class Visitor>
  void ForEachBoundaryFace(Visitor &&visit) {
    for (auto &[i_zone, zone] : bound_faces_) {
      for (auto &[i_sect, sect] : zone) {
        for (auto &face_uptr : sect) {
          visit(face_uptr.get());
        }
      }
    }
  }

 private:
  std::map<Int, NodeGroup>
      local_nodes_;  // [i_zone] -> a NodeGroup obj
  std::unordered_map<Int, std::unordered_map<Int, Mat3x1>>
      ghost_nodes_;  // [i_zone][i_node] -> a Mat3x1 obj
  std::unordered_map<Int, NodeInfo>
      m_to_node_info_;  // [m_node] -> a NodeInfo obj
  std::unordered_map<Int, CellInfo>
      m_to_cell_info_;  // [m_cell] -> a CellInfo obj
  std::map<Int, std::map<Int, Connectivity>>
      connectivities_;  // [i_zone][i_sect] -> a Connectivity obj
  std::map<Int, std::map<Int, CellGroup>>
      local_cells_;  // [i_zone][i_sect][i_cell] -> a Cell obj
  std::vector<Cell *>
      inner_cells_, inter_cells_;  // [i_cell] -> Cell *
  std::map<Int, std::vector<Cell *>>
      send_cell_ptrs_, recv_cell_ptrs_;  // [i_part] -> vector<Cell *>
  std::vector<std::vector<Scalar>>
      send_coeffs_, recv_coeffs_;
  std::unordered_map<Int, Cell>
      ghost_cells_;  // [m_cell] -> a Cell obj
  std::vector<std::pair<Int, Int>>
      local_adjs_;  // [i_pair] -> { m_holder, m_sharer }
  std::vector<std::unique_ptr<Face>>
      local_faces_, ghost_faces_;  // [i_face] -> a uptr of Face
  std::map<Int, std::map<Int, ShiftedVector<std::unique_ptr<Face>>>>
      bound_faces_;  // [i_zone][i_sect][i_face] -> a uptr of Face
  std::unordered_map<std::string, ShiftedVector<std::unique_ptr<Face>> *>
      name_to_faces_;
  std::vector<MPI_Request> requests_;
  std::array<std::string, kComponents> field_names_;
  const std::string directory_;
  const std::string cgns_file_;
  int rank_, cell_dim_, phys_dim_;
  char base_name_[33];

  void BuildBoundaryFaces(std::ifstream &istrm, int i_file) {
    // build a map from (i_zone, i_node) to cells using it
    std::unordered_map<Int, std::unordered_map<Int, std::vector<Int>>>
        z_n_to_m_cells;  // [i_zone][i_node] -> vector of `m_cell`s
    for (auto &[i_zone, zone] : local_cells_) {
      auto &n_to_m_cells = z_n_to_m_cells[i_zone];
      for (auto &[i_sect, sect] : zone) {
        auto &conn = connectivities_.at(i_zone).at(i_sect);
        auto &index = conn.index;
        auto &nodes = conn.nodes;
        for (int i_cell = sect.head(); i_cell < sect.tail(); ++i_cell) {
          auto &cell = sect[i_cell];
          auto m_cell = cell.metis_id;
          for (int i = index.at(i_cell); i < index.at(i_cell+1); ++i) {
            n_to_m_cells[nodes[i]].emplace_back(m_cell);
          }
        }
      }
    }
    char line[kLineWidth];
    Int face_id = local_faces_.size() + ghost_faces_.size();
    // build boundary faces
    std::unordered_map<std::string, std::pair<int, int>>
        name_to_z_s;  // name -> { i_zone, i_sect }
    while (istrm.getline(line, kLineWidth) && line[0] != '#') {
      int i_zone, i_sect, head, tail;
      std::sscanf(line, "%d %d %d %d", &i_zone, &i_sect, &head, &tail);
      auto &faces = bound_faces_[i_zone][i_sect];
      cgsize_t range_min[] = { head };
      cgsize_t range_max[] = { tail - 1 };
      cgsize_t mem_dimensions[] = { tail - head };
      int x, y;
      auto &conn = connectivities_[i_zone][i_sect];
      auto &index = conn.index;
      auto &nodes = conn.nodes;
      if (cg_section_read(i_file, i_base, i_zone, i_sect,
          conn.name, &conn.type, &conn.first, &conn.last, &x, &y)) {
        cgp_error_exit();
      }
      name_to_z_s[conn.name] = { i_zone, i_sect };
      int npe; cg_npe(conn.type, &npe);
      nodes = std::vector<Int>(npe * mem_dimensions[0]);
      index = ShiftedVector<Int>(mem_dimensions[0] + 1, head);
      for (int i = 0; i < index.size(); ++i) {
        index.at(head + i) = npe * i;
      }
      conn.local_first = range_min[0];
      conn.local_last = range_max[0];
      if (cgp_elements_read_data(i_file, i_base, i_zone, i_sect,
          range_min[0], range_max[0], nodes.data())) {
        cgp_error_exit();
      }
      auto &n_to_m_cells = z_n_to_m_cells.at(i_zone);
      for (int i_face = head; i_face < tail; ++i_face) {
        auto *i_node_list = &nodes[(i_face - head) * npe];
        auto cell_cnt = std::unordered_map<int, int>();
        for (int i = index.at(i_face); i < index.at(i_face+1); ++i) {
          for (auto m_cell : n_to_m_cells[nodes[i]]) {
            cell_cnt[m_cell]++;
          }
        }
        Cell *holder_ptr;
        for (auto [m_cell, cnt] : cell_cnt) {
          assert(cnt <= npe);
          if (cnt == npe) {  // this cell holds this face
            auto &info = m_to_cell_info_[m_cell];
            Int z = info.i_zone, s = info.i_sect, c = info.i_cell;
            holder_ptr = &(local_cells_.at(z).at(s).at(c));
            auto &holder_conn = connectivities_.at(z).at(s);
            auto &holder_nodes = holder_conn.nodes;
            auto holder_head = holder_conn.index[c];
            SortNodesOnFace(info.npe, &holder_nodes[holder_head], i_node_list);
            break;
          }
        }
        auto gauss_uptr = BuildGaussForFace(npe, i_zone, i_node_list);
        auto face_uptr = std::make_unique<Face>(
            std::move(gauss_uptr), holder_ptr, nullptr, face_id++);
        // the face's normal vector always point from holder to the exterior
        assert((face_uptr->center() - holder_ptr->center()).dot(
            face_uptr->GetNormalFrame(0).col(0)) > 0);
        // holder_ptr->adj_faces_.emplace_back(face_uptr.get());
        faces.emplace_back(std::move(face_uptr));
      }
    }
    // build name to ShiftedVector of faces
    for (auto &[name, z_s] : name_to_z_s) {
      auto [i_zone, i_sect] = z_s;
      auto &faces = bound_faces_.at(i_zone).at(i_sect);
      name_to_faces_[name] = &faces;
    }
  }

  Mat3x1 GetCoord(int i_zone, int i_node) const {
    Mat3x1 coord;
    auto iter_zone = local_nodes_.find(i_zone);
    if (iter_zone != local_nodes_.end() && iter_zone->second.has(i_node)) {
      coord[0] = iter_zone->second.x_[i_node];
      coord[1] = iter_zone->second.y_[i_node];
      coord[2] = iter_zone->second.z_[i_node];
    } else {
      coord = ghost_nodes_.at(i_zone).at(i_node);
    }
    return coord;
  }

 public:
  std::ofstream GetFileStream(std::string const &soln_name, bool binary,
      std::string const &suffix) const {
    char temp[1024];
    if (rank_ == 0) {
      std::snprintf(temp, sizeof(temp), "mkdir -p %s/%s",
          directory_.c_str(), soln_name.c_str());
      if (std::system(temp))
        throw std::runtime_error(temp + std::string(" failed."));
    }
    MPI_Barrier(MPI_COMM_WORLD);
    std::snprintf(temp, sizeof(temp), "%s/%s/%d.%s",
        directory_.c_str(), soln_name.c_str(), rank_, suffix.c_str());
    return std::ofstream(temp,
        std::ios::out | (binary ? (std::ios::binary) : std::ios::out));
  }
};
template <typename Int, int kDegrees, class Riemann>
MPI_Datatype const Part<Int, kDegrees, Riemann>::kMpiIntType
    = sizeof(Int) == 8 ? MPI_LONG : MPI_INT;
template <typename Int, int kDegrees, class Riemann>
MPI_Datatype const Part<Int, kDegrees, Riemann>::kMpiRealType
    = sizeof(Scalar) == 8 ? MPI_DOUBLE : MPI_FLOAT;

}  // namespace cgns
}  // namespace mesh
}  // namespace mini

#endif  // MINI_DATASET_PART_HPP_
