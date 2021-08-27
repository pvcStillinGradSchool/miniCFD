// Copyright 2021 PEI Weicheng and JIANG Yuyan
/**
 * This file defines parser of partition info txt.
 */
#ifndef MINI_MESH_CGNS_PARSER_HPP_
#define MINI_MESH_CGNS_PARSER_HPP_

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>

#include "pcgnslib.h"
#include "mini/algebra/eigen.hpp"

#include "mini/mesh/cgns/format.hpp"
#include "mini/integrator/quad.hpp"
#include "mini/integrator/hexa.hpp"

namespace mini {
namespace mesh {
namespace cgns {

template <class Object>
void print(Object&& obj) {
  std::cout << obj << '\n' << std::endl;
}

template <class Int = cgsize_t>
struct NodeInfo {
  NodeInfo() = default;
  NodeInfo(Int zi, Int ni) : i_zone(zi), i_node(ni) {}
  NodeInfo(NodeInfo const&) = default;
  NodeInfo& operator=(NodeInfo const&) = default;
  NodeInfo(NodeInfo&&) noexcept = default;
  NodeInfo& operator=(NodeInfo&&) noexcept = default;
  ~NodeInfo() noexcept = default;
  Int i_zone{0}, i_node{0};
};
template <class Int = int>
struct CellInfo {
  CellInfo() = default;
  CellInfo(Int zi, Int si, Int ci) : i_zone(zi), i_sect(si), i_cell(ci) {}
  CellInfo(CellInfo const&) = default;
  CellInfo& operator=(CellInfo const&) = default;
  CellInfo(CellInfo&&) noexcept = default;
  CellInfo& operator=(CellInfo&&) noexcept = default;
  ~CellInfo() noexcept = default;
  Int i_zone{0}, i_sect{0}, i_cell{0};
};

template <typename Int = cgsize_t, typename Real = double>
struct NodeGroup {
  Int head_, size_;
  ShiftedVector<Int> metis_id_;
  ShiftedVector<Real> x_, y_, z_;

  NodeGroup(int head, int size)
      : head_(head), size_(size), metis_id_(size, head),
        x_(size, head), y_(size, head), z_(size, head) {
  }
  NodeGroup() = default;
  NodeGroup(NodeGroup const&) = default;
  NodeGroup(NodeGroup&&) noexcept = default;
  NodeGroup& operator=(NodeGroup const&) = default;
  NodeGroup& operator=(NodeGroup &&) noexcept = default;
  ~NodeGroup() noexcept = default;

  Int size() const {
    return size_;
  }
  bool has(int nid) const {
    return head_ <= nid && nid < size_ + head_;
  }
};

template <typename Int, typename Real>
struct Cell;

template <typename Int = cgsize_t, typename Real = double>
struct Face {
  using GaussPtr = std::unique_ptr<integrator::Face<Real, 3>>;
  using CellPtr = Cell<Int, Real>*;
  GaussPtr gauss_;
  CellPtr holder_, sharer_;

  Face(GaussPtr&& gauss, CellPtr holder, CellPtr sharer)
      : gauss_(std::move(gauss)), holder_(holder), sharer_(sharer) {
  }
  Face(const Face&) = delete;
  Face& operator=(const Face&) = delete;
  Face(Face&&) noexcept = default;
  Face& operator=(Face&&) noexcept = default;
  ~Face() noexcept = default;
};

template <typename Int = cgsize_t, typename Real = double>
struct Cell {
  static constexpr int kDim = 3;
  static constexpr int kOrder = 2;
  static constexpr int kFunc = 2;
  using ProjFunc = integrator::ProjFunc<Real, kDim, kOrder, kFunc>;
  using Basis = integrator::Basis<Real, kDim, kOrder>;
  using GaussPtr = std::unique_ptr<integrator::Cell<Real>>;
  static constexpr int K = ProjFunc::K/* number of functions */;
  static constexpr int N = ProjFunc::N/* size of the basis */;

  ProjFunc func_;
  Basis basis_;
  GaussPtr gauss_;
  Int metis_id;

  Cell(GaussPtr&& gauss, Int mid)
      : gauss_(std::move(gauss)), metis_id(mid) {
    basis_.Shift(gauss_->GetCenter());
    basis_.Orthonormalize(*gauss_);
  }
  Cell() = default;
  Cell(const Cell&) = delete;
  Cell& operator=(const Cell&) = delete;
  Cell(Cell&&) noexcept = default;
  Cell& operator=(Cell&&) noexcept = default;
  ~Cell() noexcept = default;

  template <class Callable>
  void Project(Callable&& new_func) {
    func_.Reset(new_func, basis_, *gauss_);
  }
};

template <typename Int = cgsize_t, typename Real = double>
class CellGroup {
  using CellType = Cell<Int, Real>;
  static constexpr int kFields = CellType::K * CellType::N;
  Int head_, size_;
  ShiftedVector<CellType> cells_;
  ShiftedVector<ShiftedVector<Real>> fields_/* [i_field][i_cell] */;

 public:
  CellGroup(int head, int size)
      : head_(head), size_(size), cells_(size, head), fields_(kFields, 1) {
    for (int i = 1; i <= kFields; ++i) {
      fields_[i] = ShiftedVector<Real>(size, head);
    }
  }
  CellGroup() = default;
  CellGroup(CellGroup const&) = delete;
  CellGroup(CellGroup&&) noexcept = default;
  CellGroup& operator=(CellGroup const&) = delete;
  CellGroup& operator=(CellGroup &&) noexcept = default;
  ~CellGroup() noexcept = default;

  Int head() const {
    return head_;
  }
  Int size() const {
    return size_;
  }
  bool has(int cid) const {
    return head_ <= cid && cid < size_ + head_;
  }
  auto& operator[](Int i_cell) {
    return cells_[i_cell];
  }
  const auto& GetField(Int i_field) const {
    return fields_[i_field];
  }
  void GatherFields() {
    for (int i_cell = head(); i_cell < head() + size(); ++i_cell) {
      const auto& cell = cells_.at(i_cell);
      const auto& coef = cell.func_.GetCoef();
      for (int i_field = 1; i_field <= kFields; ++i_field) {
        fields_.at(i_field).at(i_cell) = coef.reshaped()[i_field-1];
      }
    }
  }
};

template <typename Int = cgsize_t, typename Real = double>
class Parser{
  static constexpr int kLineWidth = 30;
  static constexpr int kDim = 3;

 public:
  Parser(std::string const& cgns_file, std::string const& prefix, int pid)
      : cgns_file_(cgns_file) {
    rank_ = pid;
    int fid;
    if (cgp_open(cgns_file.c_str(), CG_MODE_READ, &fid))
      cgp_error_exit();
    auto filename = prefix + std::to_string(pid) + ".txt";
    std::ifstream istrm(filename);
    char line[kLineWidth];
    // node ranges
    while (istrm.getline(line, 30) && line[0]) {
      int zid, head, tail;
      std::sscanf(line, "%d %d %d", &zid, &head, &tail);
      local_nodes_[zid] = NodeGroup<Int, Real>(head, tail - head);
      cgsize_t range_min[] = { head };
      cgsize_t range_max[] = { tail - 1 };
      auto& x = local_nodes_[zid].x_;
      auto& y = local_nodes_[zid].y_;
      auto& z = local_nodes_[zid].z_;
      if (cgp_coord_read_data(fid, 1, zid, 1, range_min, range_max, x.data()) ||
          cgp_coord_read_data(fid, 1, zid, 2, range_min, range_max, y.data()) ||
          cgp_coord_read_data(fid, 1, zid, 3, range_min, range_max, z.data()))
        cgp_error_exit();
      auto& metis_id = local_nodes_[zid].metis_id_;
      cgsize_t mem_dimensions[] = { tail - head };
      cgsize_t mem_range_min[] = { 1 };
      cgsize_t mem_range_max[] = { mem_dimensions[0] };
      if (cgp_field_general_read_data(fid, 1, zid, 1, 2, range_min, range_max,
          sizeof(Int) == 8 ? CGNS_ENUMV(LongInteger) : CGNS_ENUMV(Integer),
          1, mem_dimensions, mem_range_min, mem_range_max, metis_id.data()))
        cgp_error_exit();
      for (int nid = head; nid < tail; ++nid) {
        auto mid = metis_id[nid];
        nodes_m_to_c_[mid] = NodeInfo<Int>(zid, nid);
      }
    }
    std::map<Int, std::vector<Int>> send_nodes, recv_nodes;
    // send nodes info
    while (istrm.getline(line, 30) && line[0]) {
      int p, node;
      std::sscanf(line, "%d %d", &p, &node);
      send_nodes[p].emplace_back(node);
    }
    std::vector<MPI_Request> requests;
    std::vector<std::vector<Real>> send_bufs;
    for (auto& [target, nodes] : send_nodes) {
      auto& buf = send_bufs.emplace_back();
      for (auto metis_id : nodes) {
        auto& info = nodes_m_to_c_[metis_id];
        auto const& coord = GetCoord(info.i_zone, info.i_node);
        buf.emplace_back(coord[0]);
        buf.emplace_back(coord[1]);
        buf.emplace_back(coord[2]);
      }
      assert(std::is_sorted(nodes.begin(), nodes.end()));
      int count = 3 * nodes.size();
      MPI_Datatype datatype = MPI_DOUBLE;
      int tag = target;
      auto& req = requests.emplace_back();
      MPI_Isend(buf.data(), count, datatype, target, tag, MPI_COMM_WORLD, &req);
    }
    // recv nodes info
    while (istrm.getline(line, 30) && line[0]) {
      int p, mid, zid, nid;
      std::sscanf(line, "%d %d %d %d", &p, &mid, &zid, &nid);
      recv_nodes[p].emplace_back(mid);
      nodes_m_to_c_[mid] = NodeInfo<Int>(zid, nid);
    }
    std::vector<std::vector<Real>> recv_bufs;
    for (auto& [source, nodes] : recv_nodes) {
      assert(std::is_sorted(nodes.begin(), nodes.end()));
      int count = 3 * nodes.size();
      auto& buf = recv_bufs.emplace_back(std::vector<Real>(count));
      MPI_Datatype datatype = MPI_DOUBLE;
      int tag = rank_;
      auto& req = requests.emplace_back();
      MPI_Irecv(buf.data(), count, datatype, source, tag, MPI_COMM_WORLD, &req);
    }
    std::vector<MPI_Status> statuses(requests.size());
    MPI_Waitall(requests.size(), requests.data(), statuses.data());
    requests.clear();
    // copy node coordinates from buffer to member
    int i_source = 0;
    for (auto& [source, nodes] : recv_nodes) {
      double* xyz = recv_bufs[i_source++].data();
      for (auto metis_id : nodes) {
        auto& info = nodes_m_to_c_[metis_id];
        adj_nodes_[info.i_zone][info.i_node] = { xyz[0], xyz[1] , xyz[2] };
        xyz += 3;
        if (rank_ == -1) {
          int zid = info.i_zone, nid = info.i_node;
          std::cout << metis_id << ' ' << zid << ' ' << nid << ' ';
          print(adj_nodes_[zid][nid].transpose());
        }
      }
    }
    // cell ranges
    std::map<Int, std::map<Int, ShiftedVector<Int>>> z_s_c_index;
    std::map<Int, std::map<Int, std::vector<Int>>> z_s_nodes;
    while (istrm.getline(line, 30) && line[0]) {
      int zid, sid, head, tail;
      std::sscanf(line, "%d %d %d %d", &zid, &sid, &head, &tail);
      if (rank_ == -1)
        std::printf("zid = %4d, sid = %4d, head = %4d, tail = %4d\n",
            zid, sid, head, tail);
      local_cells_[zid][sid] = CellGroup<Int, Real>(head, tail - head);
      cgsize_t range_min[] = { head };
      cgsize_t range_max[] = { tail - 1 };
      cgsize_t mem_dimensions[] = { tail - head };
      cgsize_t mem_range_min[] = { 1 };
      cgsize_t mem_range_max[] = { mem_dimensions[0] };
      ShiftedVector<Int> metis_ids(mem_dimensions[0], head);
      char sol_name[33], field_name[33];
      GridLocation_t loc;
      DataType_t dt;
      if (cg_sol_info(fid, 1, zid, 2, sol_name, &loc) ||
          cg_field_info(fid, 1, zid, 2, 2, &dt, field_name))
        cgp_error_exit();
      if (rank_ == -1)
        std::cout << sol_name << ' ' << field_name << std::endl;
      if (cgp_field_general_read_data(fid, 1, zid, 2, 2, range_min, range_max,
          sizeof(Int) == 8 ? CGNS_ENUMV(LongInteger) : CGNS_ENUMV(Integer),
          1, mem_dimensions, mem_range_min, mem_range_max, metis_ids.data()))
        cgp_error_exit();
      for (int cid = head; cid < tail; ++cid) {
        auto mid = metis_ids[cid];
        cells_m_to_c_[mid] = CellInfo<Int>(zid, sid, cid);
        if (rank_ == -1)
          std::printf("mid = %ld, zid = %ld, sid = %ld, cid = %ld\n", mid,
              cells_m_to_c_[mid].i_zone, cells_m_to_c_[mid].i_sect,
              cells_m_to_c_[mid].i_cell);
      }
      char name[33];
      ElementType_t type;
      cgsize_t u, v;
      int x, y;
      z_s_c_index[zid][sid] = ShiftedVector<Int>(mem_dimensions[0], head);
      auto& index = z_s_c_index[zid][sid];
      auto& nodes = z_s_nodes[zid][sid];
      if (cg_section_read(fid, 1, zid, sid, name, &type, &u, &v, &x, &y))
        cgp_error_exit();
      switch (type) {
      case CGNS_ENUMV(HEXA_8):
        nodes.resize(8 * mem_dimensions[0]);
        for (int i = 0; i < index.size(); ++i) {
          index.at(head + i) = 8 * i;
        }
        break;
      default:
        assert(false);
      }
      if (cgp_elements_read_data(fid, 1, zid, sid, range_min[0], range_max[0],
          nodes.data()))
        cgp_error_exit();
      for (int cid = head; cid < tail; ++cid) {
        int i = (cid - head) * 8;
        auto hexa_ptr = std::make_unique<integrator::Hexa<Real, 4, 4, 4>>(
            GetCoord(zid, nodes[i+0]), GetCoord(zid, nodes[i+1]),
            GetCoord(zid, nodes[i+2]), GetCoord(zid, nodes[i+3]),
            GetCoord(zid, nodes[i+4]), GetCoord(zid, nodes[i+5]),
            GetCoord(zid, nodes[i+6]), GetCoord(zid, nodes[i+7]));
        auto cell = Cell<Int, Real>(std::move(hexa_ptr), metis_ids[cid]);
        local_cells_[zid][sid][cid] = std::move(cell);
        if (rank_ == -1) {
          std::cout << "zid = " << zid << ", sid = " << sid
              << ", cid = " << cid << ", mid = " << metis_ids[cid] << '\n';
        }
      }
    }
    // inner adjacency
    while (istrm.getline(line, 30) && line[0]) {
      int i, j;
      std::sscanf(line, "%d %d", &i, &j);
      inner_adjs_.emplace_back(i, j);
    }
    // interpart adjacency
    std::map<Int, std::map<Int, Int>> send_infos;  // pid_to_mid_to_cnt
    std::map<Int, std::map<Int, Int>> recv_infos;
    std::vector<std::pair<Int, Int>> interpart_adjs;
    while (istrm.getline(line, 30) && line[0]) {
      int p, i, j, cnt_i, cnt_j;
      std::sscanf(line, "%d %d %d %d %d", &p, &i, &j, &cnt_i, &cnt_j);
      send_infos[p][i] = cnt_i;
      recv_infos[p][j] = cnt_j;
      interpart_adjs.emplace_back(i, j);
    }
    // send cell info
    std::vector<std::vector<Real>> send_cells;
    for (auto& [target, cell_infos] : send_infos) {
      auto& send_buf = send_cells.emplace_back();
      for (auto& [mid, cnt] : cell_infos) {
        auto& info = cells_m_to_c_[mid];
        if (rank_ == -1)
          std::printf("info at %p\n", &info);
        Int zid = cells_m_to_c_[mid].i_zone,
            sid = cells_m_to_c_[mid].i_sect,
            cid = cells_m_to_c_[mid].i_cell;
        auto id = z_s_c_index[zid][sid][cid];
        auto nodes = z_s_nodes[zid][sid];
        send_buf.emplace_back(zid);
        for (int i = 0; i < cnt; ++i) {
          send_buf.emplace_back(nodes[id+i]);
        }
      }
      int count = send_buf.size();
      MPI_Datatype datatype = sizeof(Int) == 8 ? MPI_LONG : MPI_INT;
      int tag = target;
      auto& req = requests.emplace_back();
      MPI_Isend(send_buf.data(), count, datatype, target, tag, MPI_COMM_WORLD,
          &req);
    }
    // recv cell info
    std::vector<std::vector<Real>> recv_cells;
    for (auto& [source, cell_infos] : recv_infos) {
      auto& buf = recv_cells.emplace_back();
      int count = 0;
      for (auto& [mid, cnt] : cell_infos) {
        ++count;
        count += cnt;
      }
      MPI_Datatype datatype = sizeof(Int) == 8 ? MPI_LONG : MPI_INT;
      int tag = rank_;
      buf.resize(count);
      auto& req = requests.emplace_back();
      MPI_Irecv(buf.data(), count, datatype, source, tag, MPI_COMM_WORLD, &req);
    }
    statuses = std::vector<MPI_Status>(requests.size());
    MPI_Waitall(requests.size(), requests.data(), statuses.data());
    requests.clear();
    // build ghost cells
    int buf_id = 0;
    for (auto& [source, cell_infos] : recv_infos) {
      auto& buf = recv_cells.at(buf_id++);
      int id = 0;
      for (auto& [mid, cnt] : cell_infos) {
        int zid = buf[id++];
        auto hexa_ptr = std::make_unique<integrator::Hexa<Real, 4, 4, 4>>(
          GetCoord(zid, buf[id+0]), GetCoord(zid, buf[id+1]),
          GetCoord(zid, buf[id+2]), GetCoord(zid, buf[id+3]),
          GetCoord(zid, buf[id+4]), GetCoord(zid, buf[id+5]),
          GetCoord(zid, buf[id+6]), GetCoord(zid, buf[id+7]));
        ghost_cells_[mid] = Cell<Int, Real>(std::move(hexa_ptr), mid);
        id += cnt;
      }
    }
    // build inner faces
    for (auto [m_holder, m_sharer] : inner_adjs_) {
      auto& holder_info = cells_m_to_c_[m_holder];
      auto& sharer_info = cells_m_to_c_[m_sharer];
      auto i_zone = holder_info.i_zone;
      assert(i_zone == sharer_info.i_zone);
      // find the common nodes of the holder and the sharer
      auto m_count = std::unordered_map<Int, Int>();
      auto& holder_nodes = z_s_nodes[i_zone][holder_info.i_sect];
      auto& sharer_nodes = z_s_nodes[i_zone][sharer_info.i_sect];
      auto holder_head =
          z_s_c_index[i_zone][holder_info.i_sect][holder_info.i_cell];
      auto sharer_head =
          z_s_c_index[i_zone][sharer_info.i_sect][sharer_info.i_cell];
      for (int i = 0; i < 8; ++i) {
        ++m_count[holder_nodes[holder_head + i]];
        ++m_count[sharer_nodes[sharer_head + i]];
      }
      auto common_nodes = std::vector<Int>();
      common_nodes.reserve(4);
      for (auto [i_cell, cnt] : m_count)
        if (cnt == 2)
          common_nodes.emplace_back(i_cell);
      assert(common_nodes.size() == 4);
      // let the normal vector point from holder to sharer
      // see http://cgns.github.io/CGNS_docs_current/sids/conv.figs/hexa_8.png
      auto& zone = local_cells_[i_zone];
      auto& holder = zone[holder_info.i_sect][holder_info.i_cell];
      auto& sharer = zone[sharer_info.i_sect][sharer_info.i_cell];
      integrator::Hexa<Real, 4, 4, 4>::SortNodesOnFace(
          4, &holder_nodes[holder_head], common_nodes.data());
      if (rank_ == -1)
        std::cout << "Quad{ " << common_nodes[0] << ", " << common_nodes[1]
            << ", " << common_nodes[2] << ", " << common_nodes[3] << " }\n";
      // build the quad integrator
      auto quad_ptr = std::make_unique<integrator::Quad<Real, kDim, 4, 4>>(
          GetCoord(i_zone, common_nodes[0]), GetCoord(i_zone, common_nodes[1]),
          GetCoord(i_zone, common_nodes[2]), GetCoord(i_zone, common_nodes[3]));
      inner_faces_.emplace_back(std::move(quad_ptr), &holder, &sharer);
    }
    std::cout << inner_faces_.size() << " internal faces in rank "
        << rank_ << std::endl;
    if (cgp_close(fid))
      cgp_error_exit();
  }
  template <class Callable>
  void Project(Callable&& new_func) {
    for (auto& [i_zone, sects] : local_cells_) {
      for (auto& [i_sect, cells] : sects) {
        auto tail = cells.head() + cells.size();
        for (Int i_cell = cells.head(); i_cell < tail; ++i_cell) {
          cells[i_cell].Project(new_func);
        }
      }
    }
  }
  void WriteSolutions() {
    int n_zones = local_nodes_.size();
    for (int zid = 1; zid <= n_zones; ++zid) {
      int n_sects = local_cells_[zid].size();
      for (int sid = 1; sid <= n_sects; ++sid) {
        local_cells_[zid][sid].GatherFields();
      }
    }
    int fid;
    if (cgp_open(cgns_file_.c_str(), CG_MODE_MODIFY, &fid))
      cgp_error_exit();
    for (int zid = 1; zid <= n_zones; ++zid) {
      auto& zone = local_cells_[zid];
      int n_solns;
      if (cg_nsols(fid, 1, zid, &n_solns))
        cgp_error_exit();
      int i_sol;
      if (cg_sol_write(fid, 1, zid, "Solution0", CellCenter, &i_sol))
        cgp_error_exit();
      int n_fields = 4;
      for (int i_field = 1; i_field <= n_fields; ++i_field) {
        int n_sects = local_cells_[zid].size();
        for (int sid = 1; sid <= n_sects; ++sid) {
          auto& section = zone[sid];
          auto name = "Field" + std::to_string(i_field);
          int field_id;
          if (cgp_field_write(fid, 1, zid, i_sol, RealDouble, name.c_str(),
              &field_id))
            cgp_error_exit();
          assert(field_id == i_field);
          cgsize_t first[] = { section.head() };
          cgsize_t last[] = { section.head() + section.size() - 1 };
          if (cgp_field_write_data(fid, 1, zid, i_sol, i_field, first, last,
              section.GetField(i_field).data()))
            cgp_error_exit();
        }
      }
    }
    if (cgp_close(fid))
      cgp_error_exit();
  }

 private:
  using Mat3x1 = algebra::Matrix<Real, 3, 1>;
  std::map<Int, NodeGroup<Int, Real>> local_nodes_;
  std::unordered_map<Int, std::unordered_map<Int, Mat3x1>> adj_nodes_;
  std::unordered_map<Int, NodeInfo<Int>> nodes_m_to_c_;
  std::unordered_map<Int, CellInfo<Int>> cells_m_to_c_;
  std::map<Int, std::map<Int, CellGroup<Int, Real>>> local_cells_;
  std::unordered_map<Int, Cell<Int, Real>> ghost_cells_;
  std::vector<std::pair<Int, Int>> inner_adjs_;
  std::vector<Face<Int, Real>> inner_faces_;
  const std::string cgns_file_;
  int rank_;

  Mat3x1 GetCoord(int zid, int nid) const {
    Mat3x1 coord;
    auto iter_zone = local_nodes_.find(zid);
    if (iter_zone != local_nodes_.end() && iter_zone->second.has(nid)) {
      coord[0] = iter_zone->second.x_[nid];
      coord[1] = iter_zone->second.y_[nid];
      coord[2] = iter_zone->second.z_[nid];
    } else {
      if (rank_ == -1) {
        std::cout << "zid = " << zid << ", nid = " << nid << std::endl;
      }
      coord = adj_nodes_.at(zid).at(nid);
    }
    return coord;
  }
};

}  // namespace cgns
}  // namespace mesh
}  // namespace mini

#endif  // MINI_MESH_CGNS_PARSER_HPP_
