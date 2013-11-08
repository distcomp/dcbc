/*
 A C++ interface to an AMPL problem.

 Copyright (C) 2012 AMPL Optimization LLC

 Permission to use, copy, modify, and distribute this software and its
 documentation for any purpose and without fee is hereby granted,
 provided that the above copyright notice appear in all copies and that
 both that the copyright notice and this permission notice and warranty
 disclaimer appear in supporting documentation.

 The author and AMPL Optimization LLC disclaim all warranties with
 regard to this software, including all implied warranties of
 merchantability and fitness.  In no event shall the author be liable
 for any special, indirect or consequential damages or any damages
 whatsoever resulting from loss of use, data or profits, whether in an
 action of contract, negligence or other tortious action, arising out
 of or in connection with the use or performance of this software.

 Author: Victor Zverovich
 */

#ifndef SOLVERS_UTIL_PROBLEM_H_
#define SOLVERS_UTIL_PROBLEM_H_

#include <algorithm>
#include <deque>
#include <vector>

#include "solvers/util/expr.h"
#include "solvers/util/noncopyable.h"

namespace ampl {

// An objective type.
enum ObjType { MIN = 0, MAX = 1 };

// A variable type.
enum VarType { CONTINUOUS, INTEGER };

// A solution of an optimization problem.
class Solution : Noncopyable {
 private:
  int solve_code_;
  int num_vars_;
  int num_cons_;
  double *values_;
  double *dual_values_;

 public:
  // Solution status.
  enum Status {
    UNKNOWN,
    SOLVED,
    SOLVED_MAYBE,
    INFEASIBLE,
    UNBOUNDED,
    LIMIT,
    FAILURE
  };

  // Constructs a solution with zero variables and constraints and the
  // solve code -1.
  Solution();

  ~Solution();

  // Swaps this solution with other.
  void Swap(Solution &other);

  // Returns the solution status.
  Status status() const {
    return solve_code_ < 0 || solve_code_ >= 600 ?
        UNKNOWN : static_cast<Status>(solve_code_ / 100 + 1);
  }

  // Returns the solve code.
  int solve_code() const { return solve_code_; }

  // Returns the number of variables.
  int num_vars() const { return num_vars_; }

  // Returns the number of constraints.
  int num_cons() const { return num_cons_; }

  // Returns the values of all variables.
  const double *values() const { return values_; }

  // Returns the values of all dual variables.
  const double *dual_values() const { return dual_values_; }

  // Returns the value of a variable.
  double value(int var) const {
    assert(var >= 0 && var < num_vars_);
    return values_[var];
  }

  // Returns the value of a dual variable corresponding to constraint con.
  double dual_value(int con) const {
    assert(con >= 0 && con < num_cons_);
    return dual_values_[con];
  }

  // Reads a solution from the file <stub>.sol.
  void Read(fmt::StringRef stub, int num_vars, int num_cons);
};

class ProblemChanges;

// An optimization problem.
class Problem : Noncopyable {
 public:
  ASL_fg *asl_;
 private:
  int var_capacity_;
  int obj_capacity_;
  int logical_con_capacity_;

  // Array of variable types or null if integer and binary variables precede
  // continuous variables.
  VarType *var_types_;

  static void IncreaseCapacity(int size, int &capacity) {
    if (capacity == 0 && size != 0)
      throw Error("Problem can't be modified");
    capacity = std::max(capacity, size);
    capacity = capacity ? 2 * capacity : 8;
  }

  template <typename T>
  static void Grow(T *&array, int &size, int &capacity) {
    T *new_array = new T[capacity];
    std::copy(array, array + size, new_array);
    delete [] array;
    array = new_array;
  }

  friend class BasicSolver;

  // Frees all the arrays that were allocated by modifications to the problem.
  void Free();

 public:
  // Write an .nl file.
  void WriteNL(fmt::StringRef stub, ProblemChanges *pc = 0, unsigned flags = 0);

  Problem();
  ~Problem();

  // Returns the number of variables.
  int num_vars() const { return asl_->i.n_var_; }

  // Returns the number of objectives.
  int num_objs() const { return asl_->i.n_obj_; }

  // Returns the number of constraints excluding logical constraints.
  int num_cons() const { return asl_->i.n_con_; }

  // Returns the number of integer variables including binary.
  int num_integer_vars() const {
    return asl_->i.nbv_ + asl_->i.niv_ + asl_->i.nlvbi_ +
        asl_->i.nlvci_ + asl_->i.nlvoi_;
  }

  // Returns the number of continuous variables.
  int num_continuous_vars() const {
    return num_vars() - num_integer_vars();
  }

  // Returns the number of nonlinear objectives.
  int num_nonlinear_objs() const { return asl_->i.nlo_; }

  // Returns the number of nonlinear constraints.
  int num_nonlinear_cons() const { return asl_->i.nlc_; }

  // Returns the number of logical constraints.
  int num_logical_cons() const { return asl_->i.n_lcon_; }

  // Returns the type of the variable.
  VarType var_type(int var_index) const {
    assert(var_index >= 0 && var_index < num_vars());
    if (var_types_)
      return var_types_[var_index];
    return var_index >= num_continuous_vars() ? INTEGER : CONTINUOUS;
  }

  // Returns the lower bounds for the variables.
  const double *var_lb() const { return asl_->i.LUv_; }

  // Returns the lower bound for the variable.
  double var_lb(int var_index) const {
    assert(var_index >= 0 && var_index < num_vars());
    return asl_->i.LUv_[var_index];
  }

  // Returns the upper bounds for the variables.
  const double *var_ub() const { return asl_->i.Uvx_; }

  // Returns the upper bound for the variable.
  double var_ub(int var_index) const {
    assert(var_index >= 0 && var_index < num_vars());
    return asl_->i.Uvx_[var_index];
  }

  // Returns the lower bounds for the constraints.
  const double *con_lb() const { return asl_->i.LUrhs_; }

  // Returns the lower bound for the constraint.
  double con_lb(int con_index) const {
    assert(con_index >= 0 && con_index < num_cons());
    return asl_->i.LUrhs_[con_index];
  }

  // Returns the upper bounds for the constraints.
  const double *con_ub() const { return asl_->i.Urhsx_; }

  // Returns the upper bound for the constraint.
  double con_ub(int con_index) const {
    assert(con_index >= 0 && con_index < num_cons());
    return asl_->i.Urhsx_[con_index];
  }

  // Returns the objective type.
  ObjType obj_type(int obj_index) const {
    assert(obj_index >= 0 && obj_index < num_objs());
    return static_cast<ObjType>(asl_->i.objtype_[obj_index]);
  }

  // Returns the linear part of an objective expression.
  LinearObjExpr linear_obj_expr(int obj_index) const {
    assert(obj_index >= 0 && obj_index < num_objs());
    return LinearObjExpr(asl_->i.Ograd_[obj_index]);
  }

  // Returns the linear part of a constraint expression.
  LinearConExpr linear_con_expr(int con_index) const {
    assert(con_index >= 0 && con_index < num_cons());
    return LinearConExpr(asl_->i.Cgrad_[con_index]);
  }

  // Returns the nonlinear part of an objective expression.
  NumericExpr nonlinear_obj_expr(int obj_index) const {
    assert(obj_index >= 0 && obj_index < num_objs());
    return Expr::Create<NumericExpr>(asl_->I.obj_de_[obj_index].e);
  }

  // Returns the nonlinear part of a constraint expression.
  NumericExpr nonlinear_con_expr(int con_index) const {
    assert(con_index >= 0 && con_index < num_cons());
    return Expr::Create<NumericExpr>(asl_->I.con_de_[con_index].e);
  }

  // Returns a logical constraint expression.
  LogicalExpr logical_con_expr(int lcon_index) const {
    assert(lcon_index >= 0 && lcon_index < num_logical_cons());
    return Expr::Create<LogicalExpr>(asl_->I.lcon_de_[lcon_index].e);
  }

  // Returns the solve code.
  int solve_code() const { return asl_->p.solve_code_; }

  // Sets the solve code.
  void set_solve_code(int value) {
    asl_->p.solve_code_ = value;
  }

  // Adds a variable.
  void AddVar(double lb, double ub, VarType type = CONTINUOUS);

  // Adds an objective.
  void AddObj(ObjType type, NumericExpr expr);

  // Adds a logical constraint.
  void AddCon(LogicalExpr expr);

  // Reads a problem from the file <stub>.nl.
  void Read(fmt::StringRef stub);

  // Flags for the Solve method.
  enum { IGNORE_FUNCTIONS = 1 };

  // Solves the current problem.
  void Solve(fmt::StringRef solver_name, Solution &sol,
      ProblemChanges *pc = 0, unsigned flags = 0);
};

// Writes the linear part of the problem in the AMPL format.
fmt::Writer &operator<<(fmt::Writer &w, const Problem &p);

// Changes (additions) to an optimization problem.
class ProblemChanges {
 private:
  const Problem *problem_;
  std::vector<double> var_lb_;
  std::vector<double> var_ub_;
  std::vector<double> con_lb_;
  std::vector<double> con_ub_;
  std::deque<ograd> con_terms_;
  std::deque<ograd> obj_terms_;
  std::vector<ograd*> cons_;
  std::vector<ograd*> objs_;
  std::vector<char> obj_types_;
  NewVCO vco_;

  friend class Problem;

  NewVCO *vco();

 public:
  explicit ProblemChanges(const Problem &p) : problem_(&p), vco_() {}

  ProblemChanges(const ProblemChanges &other);
  const ProblemChanges &operator=(const ProblemChanges &rhs);

  // Returns the number of additional variables.
  int num_vars() const { return static_cast<int>(var_lb_.size()); }

  // Returns the number of additional constraints.
  int num_cons() const { return static_cast<int>(cons_.size()); }

  // Returns the number of additional objectives.
  int num_objs() const { return static_cast<int>(objs_.size()); }

  // Adds a variable.
  int AddVar(double lb, double ub) {
    var_lb_.push_back(lb);
    var_ub_.push_back(ub);
    return static_cast<int>(problem_->num_vars() + var_lb_.size() - 1);
  }

  // Adds an objective.
  void AddObj(ObjType type,
      unsigned size, const double *coefs, const int *vars);

  // Adds a constraint.
  void AddCon(const double *coefs, double lb, double ub);
};
}

#endif  // SOLVERS_UTIL_PROBLEM_H_
