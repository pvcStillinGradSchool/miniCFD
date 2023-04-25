// Copyright 2021 PEI Weicheng and YANG Minghao and JIANG Yuyan
/**
 * This file defines wrappers of APIs and types in CGNS/MLL.
 */
#ifndef MINI_MESH_CGNS_HPP_
#define MINI_MESH_CGNS_HPP_

#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "cgnslib.h"

namespace mini {
namespace mesh {
namespace cgns {

template <class Real> class File;
template <class Real> class Base;
template <class Real> class Zone;
template <class Real> class Coordinates;
template <class Real> class Section;
template <class Real> class ZoneBC;
template <class Real> class Solution;

template <typename Int>
class ShiftedVector : public std::vector<Int> {
 private:
  using Base = std::vector<Int>;
  using size_type = typename Base::size_type;
  size_type shift_{0};

 public:
  ShiftedVector() = default;
  ShiftedVector(size_type size, size_type shift)
      : Base(size), shift_(shift) {
  }
  ShiftedVector(ShiftedVector const &) = default;
  ShiftedVector(ShiftedVector &&) noexcept = default;
  ShiftedVector &operator=(ShiftedVector const &) = default;
  ShiftedVector &operator=(ShiftedVector &&) noexcept = default;
  ~ShiftedVector() noexcept = default;

  Int const &operator[](size_type i) const {
    return this->Base::operator[](i - shift_);
  }
  Int const &at(size_type i) const {
    return this->Base::at(i - shift_);
  }
  Int &operator[](size_type i) {
    return this->Base::operator[](i - shift_);
  }
  Int &at(size_type i) {
    return this->Base::at(i - shift_);
  }
};

/**
 * Wrapper of the `GridCoordinates_t` type.
 */
template <class Real>
class Coordinates {
 public:  // Constructors:
  Coordinates(Zone<Real> const &zone, int size)
      : zone_(&zone), x_(size), y_(size), z_(size), name_("GridCoordinates") {
  }

 public:  // Copy Control:
  Coordinates(Coordinates const &) = default;
  Coordinates &operator=(const Coordinates &) = default;
  Coordinates(Coordinates &&) noexcept = default;
  Coordinates &operator=(Coordinates &&) noexcept = default;
  ~Coordinates() noexcept = default;

 public:  // Accessors:
  File<Real> const &file() const {
    return zone_->file();
  }
  Base<Real> const &base() const {
    return zone_->base();
  }
  Zone<Real> const &zone() const {
    return *zone_;
  }
  int CountNodes() const {
    return x_.size();
  }
  std::string const &name() const {
    return name_;
  }
  std::vector<Real> const &x() const {
    return x_;
  }
  std::vector<Real> const &y() const {
    return y_;
  }
  std::vector<Real> const &z() const {
    return z_;
  }
  /**
   * Write coordinates to a given `(file, base, zone)` tuple.
   */
  void Write() const {
    int i_coord;
    auto data_type = std::is_same_v<Real, double> ?
        CGNS_ENUMV(RealDouble) : CGNS_ENUMV(RealSingle);
    cg_coord_write(file().id(), base().id(), zone_->id(),
                   data_type, "CoordinateX", x_.data(), &i_coord);
    cg_coord_write(file().id(), base().id(), zone_->id(),
                   data_type, "CoordinateY", y_.data(), &i_coord);
    cg_coord_write(file().id(), base().id(), zone_->id(),
                   data_type, "CoordinateZ", z_.data(), &i_coord);
  }

 public:  // Mutators:
  std::vector<Real> &x() {
    return x_;
  }
  std::vector<Real> &y() {
    return y_;
  }
  std::vector<Real> &z() {
    return z_;
  }
  /**
   * Read coordinates from a given `(file, base, zone)` tuple.
   */
  void Read() {
    // All id's are 1-based when passing to CGNS/MLL.
    cgsize_t first = 1;
    cgsize_t last = CountNodes();
    auto data_type = std::is_same_v<Real, double> ?
        CGNS_ENUMV(RealDouble) : CGNS_ENUMV(RealSingle);
    cg_coord_read(file().id(), base().id(), zone_->id(), "CoordinateX",
                  data_type, &first, &last, x_.data());
    cg_coord_read(file().id(), base().id(), zone_->id(), "CoordinateY",
                  data_type, &first, &last, y_.data());
    cg_coord_read(file().id(), base().id(), zone_->id(), "CoordinateZ",
                  data_type, &first, &last, z_.data());
  }

 private:  // Data Members:
  std::string name_;
  std::vector<Real> x_, y_, z_;
  Zone<Real> const *zone_{nullptr};
  int id_;
};

/**
 * Wrapper of the `Elements_t` type.
 */
template <class Real>
class Section {
  friend class Zone<Real>;

 public:  // Constructors:
  Section() = default;
  Section(Zone<Real> const &zone, int sid,
          char const *name, cgsize_t first, cgsize_t size,
          int n_boundary_cells, CGNS_ENUMT(ElementType_t) type)
      : zone_{&zone},
        i_sect_{sid}, name_{name}, first_{first}, size_{size},
        n_boundary_cells_{n_boundary_cells}, type_{type},
        i_node_list_(size * CountNodesByType(type)) {
  }

 public:  // Copy Control:
  Section(const Section &) = delete;
  Section &operator=(const Section &) = delete;
  Section(Section &&) noexcept = default;
  Section &operator=(Section &&) noexcept = default;
  ~Section() noexcept = default;

 public:  // Accessors:
  File<Real> const &file() const {
    return zone_->file();
  }
  Base<Real> const &base() const {
    return zone_->base();
  }
  Zone<Real> const &zone() const {
    return *zone_;
  }
  std::string const &name() const {
    return name_;
  }
  int id() const {
    return i_sect_;
  }
  cgsize_t CellIdMin() const { return first_; }
  cgsize_t CellIdMax() const { return first_ + size_ - 1; }
  cgsize_t CountCells() const { return size_; }
  CGNS_ENUMT(ElementType_t) type() const {
    return type_;
  }
  static int CountNodesByType(CGNS_ENUMT(ElementType_t) type) {
    int npe;
    cg_npe(type, &npe);
    return npe;
  }
  int CountNodesByType() const {
    return CountNodesByType(type_);
  }
  static int dim(CGNS_ENUMT(ElementType_t) type) {
    int d;
    switch (type) {
    case CGNS_ENUMV(NODE):
      d = 1; break;
    case CGNS_ENUMV(BAR_2):
      d = 1; break;
    case CGNS_ENUMV(TRI_3):
    case CGNS_ENUMV(QUAD_4):
      d = 2; break;
    case CGNS_ENUMV(TETRA_4):
    case CGNS_ENUMV(PENTA_6):
    case CGNS_ENUMV(HEXA_8):
      d = 3; break;
    default:
      d = -1; break;
    }
    return d;
  }
  int dim() const {
    return dim(type_);
  }
  const cgsize_t *GetNodeIdList() const {
    return i_node_list_.data();
  }
  const cgsize_t *GetNodeIdListByNilBasedRow(cgsize_t row) const {
    return i_node_list_.data() + CountNodesByType() * row;
  }
  const cgsize_t *GetNodeIdListByOneBasedCellId(cgsize_t i_cell) const {
    return GetNodeIdListByNilBasedRow(i_cell - first_);
  }
  /**
   * Write i_node_list_ into a given `(file, base, zone)` tuple.
   */
  void Write() const {
    int i_sect;
    cg_section_write(file().id(), base().id(), zone_->id(),
        name_.c_str(), type_, CellIdMin(), CellIdMax(), 0,
        GetNodeIdList(), &i_sect);
    assert(i_sect == i_sect_);
  }

 public:  // Mutators:
  cgsize_t *GetNodeIdList() {
    return i_node_list_.data();
  }
  cgsize_t *GetNodeIdListByNilBasedRow(cgsize_t row) {
    return i_node_list_.data() + CountNodesByType() * row;
  }
  cgsize_t *GetNodeIdListByOneBasedCellId(cgsize_t i_cell) {
    return GetNodeIdListByNilBasedRow(i_cell - first_);
  }
  /**
   * Read i_node_list_ from a given `(file, base, zone)` tuple.
   */
  void Read() {
    cg_elements_read(file().id(), base().id(), zone_->id(), i_sect_,
                     GetNodeIdList(), NULL/* int *parent_data */);
  }

 private:  // Data Members:
  std::vector<cgsize_t> i_node_list_;
  std::vector<cgsize_t> start_offset_;
  std::string name_;
  Zone<Real> const *zone_{nullptr};
  cgsize_t first_, size_;
  int i_sect_, n_boundary_cells_;
  CGNS_ENUMT(ElementType_t) type_;
};

template <class Real>
struct BC {
  char name[32];
  cgsize_t ptset[2];
  cgsize_t n_pnts, normal_list_flag, normal_list_size;
  int normal_index, n_mesh;
  CGNS_ENUMT(PointSetType_t) ptset_type;
  CGNS_ENUMT(BCType_t) type;
  CGNS_ENUMT(GridLocation_t) location;
  CGNS_ENUMT(DataType_t) normal_data_type;
};
template <class Real>
class ZoneBC {
  std::vector<BC<Real>> bocos_;
  Zone<Real> const *zone_ptr_;

 public:
  explicit ZoneBC(const Zone<Real> &zone)
      : zone_ptr_(&zone) {
  }
  File<Real> const &file() const {
    return zone_ptr_->file();
  }
  Base<Real> const &base() const {
    return zone_ptr_->base();
  }
  Zone<Real> const &zone() const {
    return *zone_ptr_;
  }

  void Read() {
    int n_bocos;
    cg_nbocos(file().id(), base().id(), zone().id(), &n_bocos);
    bocos_.resize(n_bocos + 1);
    for (int i_boco = 1; i_boco <= n_bocos; ++i_boco) {
      auto &boco = bocos_.at(i_boco);
      cg_boco_info(file().id(), base().id(), zone().id(), i_boco,
          boco.name, &boco.type, &boco.ptset_type, &boco.n_pnts,
          &boco.normal_index, &boco.normal_list_size,
          &boco.normal_data_type, &boco.n_mesh);
      assert(boco.n_pnts == 2);
      cg_boco_read(file().id(), base().id(), zone().id(), i_boco,
          boco.ptset, nullptr);
      std::cout << "Read BC[" << i_boco << "] " << boco.name << ' '
          << boco.ptset[0] << ' ' << boco.ptset[1] << std::endl;
      cg_goto(file().id(), base().id(), "Zone_t", zone().id(),
          "ZoneBC_t", 1, "BC_t", i_boco, "end");
      cg_gridlocation_read(&boco.location);
    }
  }
  void UpdateRanges() {
    int n_bocos = CountBCs();
    for (int i_boco = 1; i_boco <= n_bocos; ++i_boco) {
      auto &boco = bocos_.at(i_boco);
      auto &sect = zone_ptr_->GetSection(boco.name);
      boco.ptset[0] = sect.CellIdMin();
      boco.ptset[1] = sect.CellIdMax();
    }
  }
  void Write() const {
    int n_bocos = CountBCs();
    for (int i_boco = 1; i_boco <= n_bocos; ++i_boco) {
      auto &boco = bocos_.at(i_boco);
      int boco_i;
      cg_boco_write(file().id(), base().id(), zone().id(), boco.name,
          boco.type, boco.ptset_type, boco.n_pnts, boco.ptset, &boco_i);
      assert(boco_i == i_boco);
      cg_goto(file().id(), base().id(), "Zone_t", zone().id(),
          "ZoneBC_t", 1, "BC_t", i_boco, "end");
      cg_boco_gridlocation_write(file().id(), base().id(), zone().id(), i_boco,
          boco.location);
      std::cout << "Write BC[" << i_boco << "] " << boco.name << ' '
          << boco.ptset[0] << ' ' << boco.ptset[1] << std::endl;
    }
  }

 private:
  int CountBCs() const {
    int n_bocos = bocos_.size() - (bocos_.size() > 0);
    return n_bocos;
  }
};

template <class Real>
class Field {
 public:  // Constructor:
  Field(Solution<Real> const &solution, int fid, char const *name, int size)
      : solution_{&solution}, i_field_{fid}, name_{name},  data_(size) {
  }

 public:  // Copy control:
  Field(const Field &) = default;
  Field &operator=(const Field &) = default;
  Field(Field &&) noexcept = default;
  Field &operator=(Field &&) noexcept = default;
  ~Field() noexcept = default;

 public:  // Accessors:
  Real const &at(int id) const {
    return data_[id-1];
  }
  int size() const {
    return data_.size();
  }
  std::string const &name() const {
    return name_;
  }
  void Write() const {
    int i_field;
    cg_field_write(solution_->file().id(), solution_->base().id(),
        solution_->zone().id(), solution_->id(), CGNS_ENUMV(RealDouble),
        name_.c_str(), data_.data(), &i_field);
    assert(i_field == i_field_);
  }

 public:  // Mutators:
  Real &at(int id) {
    return data_[id-1];
  }
  Real *data() {
    return data_.data();
  }

 private:
  std::vector<Real> data_;
  std::string name_;
  Solution<Real> const *solution_{nullptr};
  int i_field_;
};

template <class Real>
class Solution {
 public:  // Constructors:
  Solution(Zone<Real> const &zone, int sid, char const *name,
           CGNS_ENUMT(GridLocation_t) location)
      : zone_(&zone), i_soln_(sid), name_(name), location_(location) {
  }

 public:  // Copy Control:
  Solution(const Solution &) = delete;
  Solution &operator=(const Solution &) = delete;
  Solution(Solution &&) noexcept = default;
  Solution &operator=(Solution &&) noexcept = default;
  ~Solution() noexcept = default;

 public:  // Accessors:
  File<Real> const &file() const {
    return zone_->file();
  }
  Base<Real> const &base() const {
    return zone_->base();
  }
  Zone<Real> const &zone() const {
    return *zone_;
  }
  int id() const {
    return i_soln_;
  }
  std::string const &name() const {
    return name_;
  }
  CGNS_ENUMT(GridLocation_t) localtion() const {
    return location_;
  }
  bool OnNodes() const {
    return location_ == CGNS_ENUMV(Vertex);
  }
  bool OnCells() const {
    return location_ == CGNS_ENUMV(CellCenter);
  }
  Field<Real> const &GetField(int i_field) const {
    return *(fields_.at(i_field-1));
  }
  Field<Real> const &GetField(const std::string &name) const {
    for (auto &field : fields_)
      if (field->name() == name)
        return *field;
    throw std::invalid_argument("There is no field named \"" + name + "\".");
    return *(fields_.back());
  }
  int CountFields() const {
    return fields_.size();
  }
  void Write() const {
    int i_soln;
    cg_sol_write(file().id(), base().id(), zone_->id(),
                 name_.c_str(), location_, &i_soln);
    assert(i_soln == i_soln_);
    for (auto &field : fields_) {
      field->Write();
    }
  }

 public:  // Mutators:
  Field<Real> &GetField(int i_field) {
    return *(fields_.at(i_field-1));
  }
  Field<Real> &AddField(char const *name) {
    assert(OnNodes() || OnCells());
    // find an old one
    for (auto &field_uptr : fields_) {
      if (field_uptr->name() == name) {
        return *field_uptr;
      }
    }
    // build a new one
    int size = OnCells() ? zone_->CountCells() : zone_->CountNodes();
    fields_.emplace_back(std::make_unique<Field<Real>>(
        *this, fields_.size()+1, name, size));
    return *(fields_.back());
  }

 private:
  std::vector<std::unique_ptr<Field<Real>>> fields_;
  std::string name_;
  Zone<Real> const *zone_{nullptr};
  CGNS_ENUMT(GridLocation_t) location_;
  int i_soln_;
};

template <class Real>
class Zone {
 public:  // Constructors:
  /**
   * @brief Construct a new Zone object
   * 
   * @param base the reference to the parent
   * @param zid the id of this zone
   * @param name the name of this zone
   * @param n_cells the number of highest-dim cells in this zone
   * @param n_nodes the number of nodes in this zone
   */
  Zone(Base<Real> const &base, int zid, char const *name,
       cgsize_t n_cells, cgsize_t n_nodes)
      : base_(&base), i_zone_(zid), name_(name), n_cells_(n_cells),
        coordinates_(*this, n_nodes), zone_bc_(*this) {
  }

 public:  // Copy Control:
  Zone(const Zone &) = delete;
  Zone &operator=(const Zone &) = delete;
  Zone(Zone &&) noexcept = default;
  Zone &operator=(Zone &&) noexcept = default;
  ~Zone() noexcept = default;

 public:  // Accessors:
  File<Real> const &file() const {
    return base_->file();
  }
  Base<Real> const &base() const {
    return *base_;
  }
  int id() const {
    return i_zone_;
  }
  const std::string &name() const {
    return name_;
  }
  int CountNodes() const {
    return coordinates_.CountNodes();
  }
  /**
   * @brief 
   * 
   * @return int 
   */
  int CountAllCells() const {
    int total = 0;
    for (auto &section : sections_) {
      total += section->CountCells();
    }
    return total;
  }
  /**
   * @brief 
   * 
   * @return int 
   */
  int CountCells() const {
    return n_cells_;
  }
  int CountCellsByType(CGNS_ENUMT(ElementType_t) type) const {
    int cell_size{0};
    for (auto &section : sections_) {
      if (section->type_ == type) {
        cell_size += section->CountCells();
      }
    }
    return cell_size;
  }
  int CountSections() const {
    return sections_.size();
  }
  int CountSolutions() const {
    return solutions_.size();
  }
  Coordinates<Real> const &GetCoordinates() const {
    return coordinates_;
  }
  Section<Real> const &GetSection(int i_sect) const {
    return *(sections_.at(i_sect-1));
  }
  Section<Real> const &GetSection(const std::string &name) const {
    // TODO(PVC): use hash table
    for (auto &sect : sections_) {
      int pos = 0;
      if (sect->name().size() > name.size()) {
        pos = sect->name().size() - name.size();
      }
      if (sect->name().substr(pos) == name)
        return *sect;
    }
    throw std::invalid_argument("There is no section named \"" + name + "\".");
    return *(sections_.back());
  }
  Solution<Real> const &GetSolution(int i_soln) const {
    return *(solutions_.at(i_soln-1));
  }
  Solution<Real> const &GetSolution(const std::string &name) const {
    // TODO(PVC): use hash table
    for (auto &soln : solutions_)
      if (soln->name() == name)
        return *soln;
    throw std::invalid_argument("There is no solution named \"" + name + "\".");
    return *(solutions_.back());
  }
  void Write(int min_dim, int max_dim) const {
    int i_zone;
    cgsize_t zone_size[3] = {CountNodes(), CountCells(), 0};
    cg_zone_write(file().id(), base().id(), name_.c_str(), zone_size,
                  CGNS_ENUMV(Unstructured), &i_zone);
    assert(i_zone == i_zone_);
    coordinates_.Write();
    zone_bc_.Write();
    for (auto &section : sections_) {
      if (min_dim <= section->dim() && section->dim() <= max_dim)
        section->Write();
    }
    for (auto &solution : solutions_) {
      solution->Write();
    }
  }
  /**
  * Return true if the cell type is supported and consistent with the given dim.
  */
  static bool CheckTypeDim(CGNS_ENUMT(ElementType_t) type, int cell_dim) {
    if (cell_dim == 2) {
      if (type == CGNS_ENUMV(TRI_3) || type == CGNS_ENUMV(QUAD_4) ||
          type == CGNS_ENUMV(MIXED))
        return true;
    } else if (cell_dim == 3) {
      if (type == CGNS_ENUMV(TETRA_4) || type == CGNS_ENUMV(HEXA_8) ||
          type == CGNS_ENUMV(MIXED))
        return true;
    }
    return false;
  }

 public:  // Mutators:
  Coordinates<Real> &GetCoordinates() {
    return coordinates_;
  }
  Section<Real> &GetSection(int id) {
    return *(sections_.at(id-1));
  }
  Solution<Real> &GetSolution(int id) {
    return *(solutions_.at(id-1));
  }
  void ReadCoordinates() {
    coordinates_.Read();
  }
  void ReadAllSections() {
    int n_sections;
    cg_nsections(file().id(), base().id(), i_zone_, &n_sections);
    sections_.reserve(n_sections);
    for (int i_sect = 1; i_sect <= n_sections; ++i_sect) {
      char section_name[33];
      CGNS_ENUMT(ElementType_t) cell_type;
      cgsize_t first, last;
      int n_boundary_cells, parent_flag;
      cg_section_read(file().id(), base().id(), i_zone_, i_sect,
                      section_name, &cell_type, &first, &last,
                      &n_boundary_cells, &parent_flag);
      auto &section = sections_.emplace_back(std::make_unique<Section<Real>>(
          *this, i_sect, section_name, first, /* size = */last - first + 1,
          n_boundary_cells, cell_type));
      section->Read();
    }
    SortSectionsByDim();
  }
  void ReadZoneBC() {
    zone_bc_.Read();
  }
  void UpdateSectionRanges() {
    zone_bc_.UpdateRanges();
  }
  void ReadSolutions() {
    int n_solutions;
    cg_nsols(file().id(), base_->id(), i_zone_, &n_solutions);
    solutions_.reserve(n_solutions);
    for (int i_soln = 1; i_soln <= n_solutions; ++i_soln) {
      char sol_name[33];
      CGNS_ENUMT(GridLocation_t) location;
      cg_sol_info(file().id(), base_->id(), i_zone_,
          i_soln, sol_name, &location);
      auto &solution = solutions_.emplace_back(std::make_unique<Solution<Real>>(
          *this, i_soln, sol_name, location));
      int n_fields;
      cg_nfields(file().id(), base_->id(), i_zone_, i_soln, &n_fields);
      for (int i_field = 1; i_field <= n_fields; ++i_field) {
        CGNS_ENUMT(DataType_t) datatype;
        char field_name[33];
        cg_field_info(file().id(), base_->id(), i_zone_, i_soln,
                      i_field, &datatype, field_name);
        cgsize_t first{1}, last{1};
        if (location == CGNS_ENUMV(Vertex)) {
          last = CountNodes();
        } else if (location == CGNS_ENUMV(CellCenter)) {
          last = CountCells();
        } else {
          assert(false);
        }
        auto &field = solution->AddField(field_name);
        cg_field_read(file().id(), base_->id(), i_zone_, i_soln, field_name,
                      datatype, &first, &last, &(field.at(1)));
      }
    }
  }
  Solution<Real> &AddSolution(char const *sol_name,
      CGNS_ENUMT(GridLocation_t) location) {
    // find an old one
    for (auto &soln_uptr : solutions_) {
      if (soln_uptr->name() == sol_name && soln_uptr->localtion() == location) {
        return *soln_uptr;
      }
    }
    // build a new one
    int i_soln = solutions_.size() + 1;
    return *(solutions_.emplace_back(std::make_unique<Solution<Real>>(
        *this, i_soln, sol_name, location)));
  }

 private:
  std::string name_;
  Coordinates<Real> coordinates_;
  std::vector<std::unique_ptr<Section<Real>>> sections_;
  std::vector<std::unique_ptr<Solution<Real>>> solutions_;
  ZoneBC<Real> zone_bc_;
  Base<Real> const *base_{nullptr};
  cgsize_t n_cells_;
  int i_zone_;

  void SortSectionsByDim() {
    int n = sections_.size();
    auto dim_then_oldid = std::vector<std::pair<int, int>>();
    for (auto &sect : sections_) {
      dim_then_oldid.emplace_back(sect->dim(), sect->id());
    }
    auto order = std::vector<int>(n);
    std::iota(order.begin(), order.end(), 0);
    sort(order.begin(), order.end(), [&dim_then_oldid](int lid, int rid) {
      return dim_then_oldid[lid].first > dim_then_oldid[rid].first || (
          dim_then_oldid[lid].first == dim_then_oldid[rid].first &&
          dim_then_oldid[lid].second < dim_then_oldid[rid].second);
    });
    auto sorted_sections = std::vector<std::unique_ptr<Section<Real>>>(n);
    for (int i = 0; i < n; ++i) {
      std::swap(sorted_sections[i], sections_[order[i]]);
    }
    std::swap(sorted_sections, sections_);
    int n_cells = 1;
    for (int i_sect = 1; i_sect <= n; ++i_sect) {
      auto &sect = *sections_[i_sect-1];
      sect.i_sect_ = i_sect;
      sect.first_ = n_cells;
      n_cells += sect.CountCells();
    }
    assert(n_cells - 1 == CountAllCells());
  }
};

template <class Real>
class Base {
 public:  // Constructors:
  Base(File<Real> const &file, int bid, char const *name,
       int cell_dim, int phys_dim)
      : file_(&file), i_base_(bid), name_(name),
        cell_dim_(cell_dim), phys_dim_(phys_dim) {
  }

 public:  // Copy Control:
  Base(const Base &) = delete;
  Base &operator=(const Base &) = delete;
  Base(Base &&) noexcept = default;
  Base &operator=(Base &&) noexcept = default;
  ~Base() noexcept = default;

 public:  // Accessors:
  File<Real> const &file() const {
    return *file_;
  }
  int id() const {
    return i_base_;
  }
  int GetCellDim() const {
    return cell_dim_;
  }
  int GetPhysDim() const {
    return phys_dim_;
  }
  const std::string &name() const {
    return name_;
  }
  int CountZones() const {
    return zones_.size();
  }
  const Zone<Real> &GetZone(int id) const {
    return *(zones_.at(id-1));
  }
  void Write(int min_dim, int max_dim) const {
    int i_base;
    cg_base_write(file_->id(), name_.c_str(), cell_dim_, phys_dim_, &i_base);
    assert(i_base == i_base_);
    for (auto &zone : zones_) {
      zone->Write(min_dim, max_dim);
    }
  }

 public:  // Mutators:
  Zone<Real> &GetZone(int id) {
    return *(zones_.at(id-1));
  }
  void ReadZones() {
    int n_zones;
    cg_nzones(file().id(), i_base_, &n_zones);
    zones_.reserve(n_zones);
    for (int i_zone = 1; i_zone <= n_zones; ++i_zone) {
      char zone_name[33];
      cgsize_t zone_size[3][1];
      cg_zone_read(file().id(), i_base_, i_zone, zone_name, zone_size[0]);
      auto &zone = zones_.emplace_back(std::make_unique<Zone<Real>>(
          *this, i_zone, zone_name,
          /* n_cells */zone_size[1][0], /* n_nodes */zone_size[0][0]));
      zone->ReadCoordinates();
      zone->ReadAllSections();
      zone->ReadZoneBC();
      zone->ReadSolutions();
    }
  }
  void ReadNodeIdList(int i_file) {
    int n_zones;
    cg_nzones(file().id(), i_base_, &n_zones);
    zones_.reserve(n_zones);
    for (int i_zone = 1; i_zone <= n_zones; ++i_zone) {
      char zone_name[33];
      cgsize_t zone_size[3][1];
      cg_zone_read(file().id(), i_base_, i_zone, zone_name, zone_size[0]);
      auto &zone = zones_.emplace_back(std::make_unique<Zone<Real>>(
          *this, i_zone, zone_name,
          /* n_cells */zone_size[1][0], /* n_nodes */zone_size[0][0]));
      zone->ReadSectionsWithDim(cell_dim_);
    }
  }

 private:
  std::vector<std::unique_ptr<Zone<Real>>> zones_;
  std::string name_;
  File<Real> const *file_{nullptr};
  int i_base_, cell_dim_, phys_dim_;
};

template <class Real>
class File {
 public:  // Constructors:
  explicit File(const std::string &name)
      : name_(name) {
  }
  File(const std::string &dir, const std::string &name)
      : name_(dir) {
    if (name_.back() != '/')
      name_.push_back('/');
    name_ += name;
  }

 public:  // Copy Control:
  File(const File &) = delete;
  File &operator=(const File &) = delete;
  File(File &&) noexcept = default;
  File &operator=(File &&) noexcept = default;
  ~File() noexcept = default;

 public:  // Accessors:
  int id() const {
    return i_file_;
  }
  std::string const &name() const {
    return name_;
  }
  int CountBases() const {
    return bases_.size();
  }
  Base<Real> const &GetBase(int id) const {
    return *(bases_.at(id-1));
  }

 public:  // Mutators:
  Base<Real> &GetBase(int id) {
    return *(bases_.at(id-1));
  }
  void ReadBases() {
    if (cg_open(name_.c_str(), CG_MODE_READ, &i_file_)) {
      cg_error_exit();
    }
    bases_.clear();
    int n_bases;
    cg_nbases(i_file_, &n_bases);
    bases_.reserve(n_bases);
    for (int i_base = 1; i_base <= n_bases; ++i_base) {
      char base_name[33];
      int cell_dim{-1}, phys_dim{-1};
      cg_base_read(i_file_, i_base, base_name, &cell_dim, &phys_dim);
      auto &base = bases_.emplace_back(std::make_unique<Base<Real>>(
          *this, i_base, base_name, cell_dim, phys_dim));
      base->ReadZones();
    }
    cg_close(i_file_);
  }
  void ReadNodeIdList() {
    if (cg_open(name_.c_str(), CG_MODE_READ, &i_file_)) {
      cg_error_exit();
    }
    bases_.clear();
    int n_bases;
    cg_nbases(i_file_, &n_bases);
    bases_.reserve(n_bases);
    for (int i_base = 1; i_base <= n_bases; ++i_base) {
      char base_name[33];
      int cell_dim, phys_dim;
      cg_base_read(i_file_, i_base, base_name, &cell_dim, &phys_dim);
      auto &base = bases_.emplace_back(std::make_unique<Base<Real>>(
          *this, i_base, base_name, cell_dim, phys_dim));
      base->ReadNodeIdList(i_file_);
    }
    cg_close(i_file_);
  }
  void Write(const std::string &file_name, int min_dim = 0, int max_dim = 3) {
    name_ = file_name;
    if (cg_open(file_name.c_str(), CG_MODE_WRITE, &i_file_)) {
      cg_error_exit();
    }
    for (auto &base : bases_) {
      base->Write(min_dim, max_dim);
    }
    cg_close(i_file_);
  }

 private:
  std::vector<std::unique_ptr<Base<Real>>> bases_;
  std::string name_;
  int i_file_;
};

}  // namespace cgns
}  // namespace mesh
}  // namespace mini

#endif  // MINI_MESH_CGNS_HPP_