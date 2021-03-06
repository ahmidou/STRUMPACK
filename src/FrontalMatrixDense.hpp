/*
 * STRUMPACK -- STRUctured Matrices PACKage, Copyright (c) 2014, The Regents of
 * the University of California, through Lawrence Berkeley National Laboratory
 * (subject to receipt of any required approvals from the U.S. Dept. of Energy).
 * All rights reserved.
 *
 * If you have questions about your rights to use or distribute this software,
 * please contact Berkeley Lab's Technology Transfer Department at TTD@lbl.gov.
 *
 * NOTICE. This software is owned by the U.S. Department of Energy. As such, the
 * U.S. Government has been granted for itself and others acting on its behalf a
 * paid-up, nonexclusive, irrevocable, worldwide license in the Software to
 * reproduce, prepare derivative works, and perform publicly and display publicly.
 * Beginning five (5) years after the date permission to assert copyright is
 * obtained from the U.S. Department of Energy, and subject to any subsequent five
 * (5) year renewals, the U.S. Government is granted for itself and others acting
 * on its behalf a paid-up, nonexclusive, irrevocable, worldwide license in the
 * Software to reproduce, prepare derivative works, distribute copies to the
 * public, perform publicly and display publicly, and to permit others to do so.
 *
 * Developers: Pieter Ghysels, Francois-Henry Rouet, Xiaoye S. Li.
 *             (Lawrence Berkeley National Lab, Computational Research Division).
 *
 */
#ifndef FRONTAL_MATRIX_DENSE_HPP
#define FRONTAL_MATRIX_DENSE_HPP

#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <random>
#include "blas_lapack_wrapper.hpp"
#include "blas_lapack_omp_task.hpp"
#include "CompressedSparseMatrix.hpp"
#include "MatrixReordering.hpp"
#include "TaskTimer.hpp"

namespace strumpack {

  template<typename scalar_t,typename integer_t> class FrontalMatrixDense
    : public FrontalMatrix<scalar_t,integer_t> {
    using DenseM_t = DenseMatrix<scalar_t>;
    using DenseMW_t = DenseMatrixWrapper<scalar_t>;
  public:
    DenseM_t F11, F12, F21, F22;
    std::vector<int> piv; // this should be regular int because it is passed to BLAS

    FrontalMatrixDense(CompressedSparseMatrix<scalar_t,integer_t>* _A,
		       integer_t _sep, integer_t _sep_begin, integer_t _sep_end,
		       integer_t _dim_upd, integer_t* _upd);
    ~FrontalMatrixDense() {}
    void release_work_memory() { F22.clear(); }
    void extend_add_to_dense(FrontalMatrixDense<scalar_t,integer_t>* p, int task_depth);
    void sample_CB(const SPOptions<scalar_t>& opts, DenseM_t& R, DenseM_t& Sr, DenseM_t& Sc,
		   FrontalMatrix<scalar_t,integer_t>* pa, int task_depth);
    void multifrontal_factorization(const SPOptions<scalar_t>& opts, int etree_level=0, int task_depth=0);
    void forward_multifrontal_solve(scalar_t* b, scalar_t* wmem, int etree_level=0, int task_depth=0);
    void backward_multifrontal_solve(scalar_t* y, scalar_t* wmem, int etree_level=0, int task_depth=0);
    void extract_CB_sub_matrix(const std::vector<std::size_t>& I, const std::vector<std::size_t>& J, DenseM_t& B, int task_depth) const;
    std::string type() const { return "FrontalMatrixDense"; }

  private:
    FrontalMatrixDense(const FrontalMatrixDense&) = delete;
    FrontalMatrixDense& operator=(FrontalMatrixDense const&) = delete;
    void factor_phase1(const SPOptions<scalar_t>& opts, int etree_level, int task_depth);
    void factor_phase2(int etree_level, int task_depth);
    void fwd_solve_phase1(scalar_t* b, scalar_t* wmem, int etree_level, int task_depth);
    void fwd_solve_phase2(scalar_t* b, scalar_t* wmem, int etree_level, int task_depth);
    void bwd_solve_phase1(scalar_t* y, scalar_t* wmem, int etree_level, int task_depth);
    void bwd_solve_phase2(scalar_t* y, scalar_t* wmem, int etree_level, int task_depth);
  };

  template<typename scalar_t,typename integer_t>
  FrontalMatrixDense<scalar_t,integer_t>::FrontalMatrixDense
  (CompressedSparseMatrix<scalar_t,integer_t>* _A, integer_t _sep,
   integer_t _sep_begin, integer_t _sep_end, integer_t _dim_upd, integer_t* _upd)
    : FrontalMatrix<scalar_t,integer_t>(_A, NULL, NULL, _sep, _sep_begin, _sep_end, _dim_upd, _upd) {}

  template<typename scalar_t,typename integer_t> void
  FrontalMatrixDense<scalar_t,integer_t>::extend_add_to_dense
  (FrontalMatrixDense<scalar_t,integer_t>* p, int task_depth) {
    const std::size_t pdsep = p->dim_sep;
    const std::size_t dupd = this->dim_upd;
    std::size_t upd2sep;
    auto I = this->upd_to_parent(p, upd2sep);
    //#pragma omp taskloop default(shared) grainsize(64) if(task_depth < params::task_recursion_cutoff_level)
    for (std::size_t c=0; c<dupd; c++) {
      auto pc = I[c];
      if (pc < pdsep) {
	for (std::size_t r=0; r<upd2sep; r++) p->F11(I[r],pc) += F22(r,c);
	for (std::size_t r=upd2sep; r<dupd; r++) p->F21(I[r]-pdsep,pc) += F22(r,c);
      } else {
	for (std::size_t r=0; r<upd2sep; r++) p->F12(I[r],pc-pdsep) += F22(r, c);
	for (std::size_t r=upd2sep; r<dupd; r++) p->F22(I[r]-pdsep,pc-pdsep) += F22(r,c);
      }
    }
    STRUMPACK_FLOPS((is_complex<scalar_t>()?2:1)*static_cast<long long int>(dupd*dupd));
    release_work_memory();
  }

  template<typename scalar_t,typename integer_t> void
  FrontalMatrixDense<scalar_t,integer_t>::sample_CB
  (const SPOptions<scalar_t>& opts, DenseM_t& R, DenseM_t& Sr, DenseM_t& Sc,
   FrontalMatrix<scalar_t,integer_t>* pa, int task_depth) {
    auto I = this->upd_to_parent(pa);
    auto cR = R.extract_rows(I);
    DenseM_t cS(this->dim_upd, R.cols());
    gemm(Trans::N, Trans::N, scalar_t(1.), F22, cR, scalar_t(0.), cS, task_depth);
    Sr.scatter_rows_add(I, cS);
    gemm(Trans::C, Trans::N, scalar_t(1.), F22, cR, scalar_t(0.), cS, task_depth);
    Sc.scatter_rows_add(I, cS);
  }

  template<typename scalar_t,typename integer_t> void
  FrontalMatrixDense<scalar_t,integer_t>::multifrontal_factorization
  (const SPOptions<scalar_t>& opts, int etree_level, int task_depth) {
    if (task_depth == 0) {
      // use tasking for children and for extend-add parallelism
#pragma omp parallel if(!omp_in_parallel())
#pragma omp single
      factor_phase1(opts, etree_level, task_depth);
      // do not use tasking for blas/lapack parallelism (use system blas threading!)
      factor_phase2(etree_level, params::task_recursion_cutoff_level);
    } else {
      factor_phase1(opts, etree_level, task_depth);
      factor_phase2(etree_level, task_depth);
    }
  }

  template<typename scalar_t,typename integer_t> void
  FrontalMatrixDense<scalar_t,integer_t>::factor_phase1
  (const SPOptions<scalar_t>& opts, int etree_level, int task_depth) {
    if (task_depth < params::task_recursion_cutoff_level) {
      if (this->lchild)
#pragma omp task default(shared) final(task_depth >= params::task_recursion_cutoff_level-1) mergeable
	this->lchild->multifrontal_factorization(opts, etree_level+1, task_depth+1);
      if (this->rchild)
#pragma omp task default(shared) final(task_depth >= params::task_recursion_cutoff_level-1) mergeable
	this->rchild->multifrontal_factorization(opts, etree_level+1, task_depth+1);
#pragma omp taskwait
    } else {
      if (this->lchild) this->lchild->multifrontal_factorization(opts, etree_level+1, task_depth);
      if (this->rchild) this->rchild->multifrontal_factorization(opts, etree_level+1, task_depth);
    }
    auto f0 = params::flops;
    // TODO can we allocate the memory in one go??
    F11 = DenseM_t(this->dim_sep, this->dim_sep); F11.zero();
    F12 = DenseM_t(this->dim_sep, this->dim_upd); F12.zero();
    F21 = DenseM_t(this->dim_upd, this->dim_sep); F21.zero();
    this->A->extract_front(F11.data(), F12.data(), F21.data(), this->dim_sep, this->dim_upd,
			   this->sep_begin, this->sep_end, this->upd, task_depth);
    if (this->dim_upd) {
      F22 = DenseM_t(this->dim_upd, this->dim_upd);
      F22.zero();
    }
    if (this->lchild) this->lchild->extend_add_to_dense(this, task_depth);
    if (this->rchild) this->rchild->extend_add_to_dense(this, task_depth);
    params::full_rank_flops += params::flops - f0;
  }

  template<typename scalar_t,typename integer_t> void
  FrontalMatrixDense<scalar_t,integer_t>::factor_phase2(int etree_level, int task_depth) {
    auto f0 = params::flops;
    if (this->dim_sep) {
      piv = F11.LU(task_depth);
      if (this->dim_upd) {
	F12.permute_rows_fwd(piv);
	trsm(Side::L, UpLo::L, Trans::N, Diag::U, scalar_t(1.), F11, F12, task_depth);
	trsm(Side::R, UpLo::U, Trans::N, Diag::N, scalar_t(1.), F11, F21, task_depth);
	gemm(Trans::N, Trans::N, scalar_t(-1.), F21, F12, scalar_t(1.), F22, task_depth);
      }
    }
    params::full_rank_flops += params::flops - f0;
  }

  template<typename scalar_t,typename integer_t> void
  FrontalMatrixDense<scalar_t,integer_t>::forward_multifrontal_solve
  (scalar_t* b, scalar_t* wmem, int etree_level, int task_depth) {
    if (task_depth == 0) {
      // tasking when calling the children
#pragma omp parallel if(!omp_in_parallel())
#pragma omp single
      fwd_solve_phase1(b, wmem, etree_level, task_depth);
      // no tasking for the root node computations, use system blas threading!
      fwd_solve_phase2(b, wmem, etree_level, params::task_recursion_cutoff_level);
    } else {
      fwd_solve_phase1(b, wmem, etree_level, task_depth);
      fwd_solve_phase2(b, wmem, etree_level, task_depth);
    }
  }

  template<typename scalar_t,typename integer_t> void
  FrontalMatrixDense<scalar_t,integer_t>::fwd_solve_phase1
  (scalar_t* b, scalar_t* wmem, int etree_level, int task_depth) {
    if (task_depth < params::task_recursion_cutoff_level) {
      if (this->lchild)
#pragma omp task untied default(shared) final(task_depth >= params::task_recursion_cutoff_level-1) mergeable
	this->lchild->forward_multifrontal_solve(b, wmem, etree_level+1, task_depth+1);
      if (this->rchild)
#pragma omp task untied default(shared) final(task_depth >= params::task_recursion_cutoff_level-1) mergeable
	this->rchild->forward_multifrontal_solve(b, wmem, etree_level+1, task_depth+1);
#pragma omp taskwait
    } else {
      if (this->lchild) this->lchild->forward_multifrontal_solve(b, wmem, etree_level+1, task_depth);
      if (this->rchild) this->rchild->forward_multifrontal_solve(b, wmem, etree_level+1, task_depth);
    }
    this->look_left(b, wmem);
  }

  template<typename scalar_t,typename integer_t> void
  FrontalMatrixDense<scalar_t,integer_t>::fwd_solve_phase2
  (scalar_t* b, scalar_t* wmem, int etree_level, int task_depth) {
    if (this->dim_sep) {
      DenseMW_t rhs(this->dim_sep, 1, b+this->sep_begin, this->A->size());
      rhs.permute_rows_fwd(piv);
      trsv(UpLo::L, Trans::N, Diag::U, F11, rhs, task_depth);
      if (this->dim_upd) {
	DenseMW_t tmp(this->dim_upd, 1, wmem+this->p_wmem, this->A->size());
	gemv(Trans::N, scalar_t(-1.), F21, rhs, scalar_t(1.), tmp, task_depth);
      }
    }
  }

  template<typename scalar_t,typename integer_t> void
  FrontalMatrixDense<scalar_t,integer_t>::backward_multifrontal_solve
  (scalar_t* b, scalar_t* wmem, int etree_level, int task_depth) {
    if (task_depth == 0) {
      // no tasking in blas routines, use system threaded blas instead
      bwd_solve_phase1(b, wmem, etree_level, params::task_recursion_cutoff_level);
#pragma omp parallel if(!omp_in_parallel())
#pragma omp single
      // tasking when calling children
      bwd_solve_phase2(b, wmem, etree_level, task_depth);
    } else {
      bwd_solve_phase1(b, wmem, etree_level, task_depth);
      bwd_solve_phase2(b, wmem, etree_level, task_depth);
    }
  }

  template<typename scalar_t,typename integer_t> void
  FrontalMatrixDense<scalar_t,integer_t>::bwd_solve_phase1
  (scalar_t* y, scalar_t* wmem, int etree_level, int task_depth) {
    if (this->dim_sep) {
      DenseMW_t rhs(this->dim_sep, 1, y+this->sep_begin, this->A->size());
      if (this->dim_upd) {
	DenseMW_t tmp(this->dim_upd, 1, wmem+this->p_wmem, this->A->size());
	gemv(Trans::N, scalar_t(-1.), F12, tmp, scalar_t(1.), rhs, task_depth);
      }
      trsv(UpLo::U, Trans::N, Diag::N, F11, rhs, task_depth);
    }
  }

  template<typename scalar_t,typename integer_t> void
  FrontalMatrixDense<scalar_t,integer_t>::bwd_solve_phase2
  (scalar_t* y, scalar_t* wmem, int etree_level, int task_depth) {
    this->look_right(y, wmem);
    if (task_depth < params::task_recursion_cutoff_level) {
      if (this->lchild)
#pragma omp task untied default(shared) final(task_depth >= params::task_recursion_cutoff_level-1) mergeable
	this->lchild->backward_multifrontal_solve(y, wmem, etree_level+1, task_depth+1);
      if (this->rchild)
#pragma omp task untied default(shared) final(task_depth >= params::task_recursion_cutoff_level-1) mergeable
	this->rchild->backward_multifrontal_solve(y, wmem, etree_level+1, task_depth+1);
#pragma omp taskwait
    } else {
      if (this->lchild) this->lchild->backward_multifrontal_solve(y, wmem, etree_level+1, task_depth);
      if (this->rchild) this->rchild->backward_multifrontal_solve(y, wmem, etree_level+1, task_depth);
    }
  }

  template<typename scalar_t,typename integer_t> void
  FrontalMatrixDense<scalar_t,integer_t>::extract_CB_sub_matrix
  (const std::vector<std::size_t>& I, const std::vector<std::size_t>& J, DenseM_t& B, int task_depth) const {
    std::vector<std::size_t> lJ, oJ;
    this->find_upd_indices(J, lJ, oJ);
    if (lJ.empty()) return;
    std::vector<std::size_t> lI, oI;
    this->find_upd_indices(I, lI, oI);
    if (lI.empty()) return;
    for (std::size_t j=0; j<lJ.size(); j++)
      for (std::size_t i=0; i<lI.size(); i++)
	B(oI[i], oJ[j]) += F22(lI[i], lJ[j]);
    STRUMPACK_FLOPS((is_complex<scalar_t>() ? 2 : 1) * lJ.size() * lI.size());
  }

} // end namespace strumpack

#endif
