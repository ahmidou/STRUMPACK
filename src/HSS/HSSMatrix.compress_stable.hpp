#ifndef HSS_MATRIX_COMPRESS_STABLE_HPP
#define HSS_MATRIX_COMPRESS_STABLE_HPP

#include "Random_wrapper.hpp"

namespace strumpack {
  namespace HSS {

    template<typename scalar_t> void HSSMatrix<scalar_t>::compress_stable
    (const DenseM_t& A, const opts_t& opts) {
      AFunctor<scalar_t> afunc(A, opts.q());
      compress_stable(afunc, afunc, opts);
    }

    template<typename scalar_t> void HSSMatrix<scalar_t>::compress_stable
    (const mult_t& Amult, const elem_t& Aelem, const opts_t& opts) {
      auto d = opts.d0();
      auto dd = opts.dd();
      // assert(dd <= d);
      auto n = this->cols();
      DenseM_t Rr(n, d+dd), Rc(n, d+dd), Sr(n, d+dd), Sc(n, d+dd);
      std::unique_ptr<random::RandomGeneratorBase<real_t>> rgen;
      if (!opts.user_defined_random()) {
	rgen = random::make_random_generator<real_t>
	  (opts.random_engine(), opts.random_distribution());
	auto f0 = params::flops;
	Rr.random(*rgen);
	params::random_flops += params::flops - f0;
	Rc.copy(Rr);
      }
      Amult(Rr, Rc, Sr, Sc);
      WorkCompress<scalar_t> w;
      while (!this->is_compressed()) {
	if (d != opts.d0()) {
	  Rr.resize(n, d+dd);
	  Rc.resize(n, d+dd);
	  Sr.resize(n, d+dd);
	  Sc.resize(n, d+dd);
	  DenseMW_t Rr_new(n, dd, Rr, 0, d);
	  DenseMW_t Rc_new(n, dd, Rc, 0, d);
	  DenseMW_t Sr_new(n, dd, Sr, 0, d);
	  DenseMW_t Sc_new(n, dd, Sc, 0, d);
	  if (!opts.user_defined_random()) {
	    auto f0 = params::flops;
	    Rr_new.random(*rgen);
	    params::random_flops += params::flops - f0;
	    Rc_new.copy(Rr_new);
	  }
	  Amult(Rr_new, Rc_new, Sr_new, Sc_new);
	}
	if (opts.verbose()) std::cout << "# compressing with d+dd = " << d << "+" << dd
				      << " (stable)" << std::endl;
	compress_recursive_stable(Rr, Rc, Sr, Sc, Aelem, opts, w, d, dd, this->_openmp_task_depth);
	d += dd;
	dd = std::min(dd, opts.max_rank()-d);
      }
    }

    template<typename scalar_t> void HSSMatrix<scalar_t>::compress_recursive_stable
    (DenseM_t& Rr, DenseM_t& Rc, DenseM_t& Sr, DenseM_t& Sc, const elem_t& Aelem, const opts_t& opts,
     WorkCompress<scalar_t>& w, int d, int dd, int depth) {
      if (this->leaf()) {
	if (this->is_untouched()) {
	  std::vector<std::size_t> I, J;
	  I.reserve(this->rows());
	  J.reserve(this->cols());
	  for (std::size_t i=0; i<this->rows(); i++) I.push_back(i+w.offset.first);
	  for (std::size_t j=0; j<this->cols(); j++) J.push_back(j+w.offset.second);
	  _D = DenseM_t(this->rows(), this->cols());
	  Aelem(I, J, _D);
	}
      } else {
	w.split(this->_ch[0]->dims());
	bool tasked = depth < params::task_recursion_cutoff_level;
	bool isfinal = depth >= params::task_recursion_cutoff_level-1;
	if (tasked) {
#pragma omp task default(shared) final(isfinal) mergeable
	  this->_ch[0]->compress_recursive_stable(Rr, Rc, Sr, Sc, Aelem, opts, w.c[0], d, dd, depth+1);
#pragma omp task default(shared) final(isfinal) mergeable
	  this->_ch[1]->compress_recursive_stable(Rr, Rc, Sr, Sc, Aelem, opts, w.c[1], d, dd, depth+1);
#pragma omp taskwait
	  if (!this->_ch[0]->is_compressed() || !this->_ch[1]->is_compressed()) return;
	} else {
	  this->_ch[0]->compress_recursive_stable(Rr, Rc, Sr, Sc, Aelem, opts, w.c[0], d, dd, depth+1);
	  if (!this->_ch[0]->is_compressed()) return;
	  this->_ch[1]->compress_recursive_stable(Rr, Rc, Sr, Sc, Aelem, opts, w.c[1], d, dd, depth+1);
	  if (!this->_ch[1]->is_compressed()) return;
	}
	if (this->is_untouched()) {
#pragma omp task default(shared) if(tasked) final(isfinal) mergeable
	  {
	    _B01 = DenseM_t(this->_ch[0]->U_rank(), this->_ch[1]->V_rank());
	    Aelem(w.c[0].Ir, w.c[1].Ic, _B01);
	  }
#pragma omp task default(shared) if(tasked) final(isfinal) mergeable
	  {
	    _B10 = DenseM_t(this->_ch[1]->U_rank(), this->_ch[0]->V_rank());
	    Aelem(w.c[1].Ir, w.c[0].Ic, _B10);
	  }
#pragma omp taskwait
	}
      }
      if (w.lvl == 0) this->_U_state = this->_V_state = State::COMPRESSED;
      else {
	if (this->is_untouched())
	  compute_local_samples(Rr, Rc, Sr, Sc, w, 0, d+dd, depth);
	else compute_local_samples(Rr, Rc, Sr, Sc, w, d, dd, depth);
	if (!this->is_compressed()) {
	  compute_U_basis_stable(Sr, opts, w, d, dd, depth);
	  compute_V_basis_stable(Sc, opts, w, d, dd, depth);
	  if (this->is_compressed())
	    reduce_local_samples(Rr, Rc, w, 0, d+dd, depth);
	} else reduce_local_samples(Rr, Rc, w, d, dd, depth);
      }
    }

    template<typename scalar_t> void HSSMatrix<scalar_t>::compress_level_stable
    (DenseM_t& Rr, DenseM_t& Rc, DenseM_t& Sr, DenseM_t& Sc, const opts_t& opts,
     WorkCompress<scalar_t>& w, int d, int dd, int lvl, int depth) {
      if (this->leaf()) {
	if (w.lvl < lvl) return;
      } else {
	if (w.lvl < lvl) {
	  // TODO tasking
	  this->_ch[0]->compress_level_stable(Rr, Rc, Sr, Sc, opts, w.c[0], d, dd, lvl, depth);
	  this->_ch[1]->compress_level_stable(Rr, Rc, Sr, Sc, opts, w.c[1], d, dd, lvl, depth);
	  return;
	}
	if (!this->_ch[0]->is_compressed() || !this->_ch[1]->is_compressed()) return;
      }
      if (w.lvl==0) this->_U_state = this->_V_state = State::COMPRESSED;
      else {
	if (this->is_untouched())
	  compute_local_samples(Rr, Rc, Sr, Sc, w, 0, d+dd, depth);
	else compute_local_samples(Rr, Rc, Sr, Sc, w, d, dd, depth);
	if (!this->is_compressed()) {
	  compute_U_basis_stable(Sr, opts, w, d, dd, depth);
	  compute_V_basis_stable(Sc, opts, w, d, dd, depth);
	  if (this->is_compressed())
	    reduce_local_samples(Rr, Rc, w, 0, d+dd, depth);
	} else reduce_local_samples(Rr, Rc, w, d, dd, depth);
      }
    }


    template<typename scalar_t> void HSSMatrix<scalar_t>::compute_U_basis_stable
    (DenseM_t& Sr, const opts_t& opts, WorkCompress<scalar_t>& w, int d, int dd, int depth) {
      if (this->_U_state == State::COMPRESSED) return;
      int u_rows = this->leaf() ? this->rows() : this->_ch[0]->U_rank()+this->_ch[1]->U_rank();
      DenseMW_t lSr(u_rows, d+dd, Sr, w.offset.second, 0);
      constexpr double c = 1.25331413731550e-01; //1.0 / (10.0 * std::sqrt(2. / M_PI));
      if (d+dd >= opts.max_rank() || d+dd >= int(u_rows) ||
	  update_orthogonal_basis(lSr, w.Qr, d, dd, this->_U_state == State::UNTOUCHED, depth)
	  < opts.rel_tol() * c) {
	w.Qr.clear();
	auto f0 = params::flops;
	// if (basisOK) DenseMW_t(u_rows, d, lSr, 0, 0).ID_row(_U.E(), _U.P(), w.Jr, opts.rel_tol(), opts.abs_tol(), opts.max_rank(), depth);
	// else
	lSr.ID_row(_U.E(), _U.P(), w.Jr, opts.rel_tol(), opts.abs_tol(), opts.max_rank(), depth);
	params::ID_flops += params::flops - f0;
	this->_U_rank = _U.cols();  this->_U_rows = _U.rows();
	w.Ir.reserve(_U.cols());
	if (this->leaf()) for (auto i : w.Jr) w.Ir.push_back(w.offset.first + i);
	else {
	  auto r0 = w.c[0].Ir.size();
	  for (auto i : w.Jr) w.Ir.push_back((i < r0) ? w.c[0].Ir[i] : w.c[1].Ir[i-r0]);
	}
	this->_U_state = State::COMPRESSED;
      } else {
	// if (2*d > u_rows) set_U_full_rank(w);
	// else
	this->_U_state = State::PARTIALLY_COMPRESSED;
      }
    }

    template<typename scalar_t> void HSSMatrix<scalar_t>::compute_V_basis_stable
    (DenseM_t& Sc, const opts_t& opts, WorkCompress<scalar_t>& w, int d, int dd, int depth) {
      if (this->_V_state == State::COMPRESSED) return;
      int v_rows = this->leaf() ? this->rows() : this->_ch[0]->V_rank()+this->_ch[1]->V_rank();
      DenseMW_t lSc(v_rows, d+dd, Sc, w.offset.second, 0);
      constexpr double c = 1.25331413731550e-01; //1.0 / (10.0 * std::sqrt(2. / M_PI));
      if (d+dd >= opts.max_rank() || d+dd >= v_rows ||
	  update_orthogonal_basis(lSc, w.Qc, d, dd, this->_V_state == State::UNTOUCHED, depth)
	  < opts.rel_tol() * c) {
	w.Qc.clear();
	auto f0 = params::flops;
	// if (basisOK)	DenseMW_t(v_rows, d, lSc, 0, 0).ID_row(_V.E(), _V.P(), w.Jc, opts.rel_tol(), opts.abs_tol(), opts.max_rank(), depth);
	// else
	lSc.ID_row(_V.E(), _V.P(), w.Jc, opts.rel_tol(), opts.abs_tol(), opts.max_rank(), depth);
	params::ID_flops += params::flops - f0;
	this->_V_rank = _V.cols();  this->_V_rows = _V.rows();
	w.Ic.reserve(_V.cols());
	if (this->leaf()) for (auto j : w.Jc) w.Ic.push_back(w.offset.second + j);
	else {
	  auto r0 = w.c[0].Ic.size();
	  for (auto j : w.Jc) w.Ic.push_back((j < r0) ? w.c[0].Ic[j] : w.c[1].Ic[j-r0]);
	}
	this->_V_state = State::COMPRESSED;
      } else {
	// if (2*d > v_rows) set_V_full_rank(w);
	// else
	this->_V_state = State::PARTIALLY_COMPRESSED;
      }
    }

    template<typename scalar_t> void HSSMatrix<scalar_t>::set_U_full_rank(WorkCompress<scalar_t>& w) {
      auto u_rows = this->leaf() ? this->rows() : this->_ch[0]->U_rank()+this->_ch[1]->U_rank();
      _U = HSSBasisID<scalar_t>(u_rows);
      w.Jr.reserve(u_rows);
      for (std::size_t i=0; i<u_rows; i++) w.Jr.push_back(i);
      w.Ir.reserve(u_rows);
      if (this->leaf()) for (std::size_t i=0; i<u_rows; i++) w.Ir.push_back(w.offset.first + i);
      else {
	for (std::size_t i=0; i<this->_ch[0]->U_rank(); i++) w.Ir.push_back(w.c[0].Ir[i]);
	for (std::size_t i=0; i<this->_ch[1]->U_rank(); i++) w.Ir.push_back(w.c[1].Ir[i]);
      }
      this->_U_state = State::COMPRESSED;
    }

    template<typename scalar_t> void HSSMatrix<scalar_t>::set_V_full_rank(WorkCompress<scalar_t>& w) {
      auto v_rows = this->leaf() ? this->rows() : this->_ch[0]->V_rank()+this->_ch[1]->V_rank();
      _V = HSSBasisID<scalar_t>(v_rows);
      w.Jc.reserve(v_rows);
      for (std::size_t j=0; j<v_rows; j++) w.Jc.push_back(j);
      w.Ic.reserve(v_rows);
      if (this->leaf()) for (std::size_t j=0; j<v_rows; j++) w.Ic.push_back(w.offset.second + j);
      else {
	for (std::size_t j=0; j<this->_ch[0]->V_rank(); j++) w.Ic.push_back(w.c[0].Ic[j]);
	for (std::size_t j=0; j<this->_ch[1]->V_rank(); j++) w.Ic.push_back(w.c[1].Ic[j]);
      }
      this->_V_state = State::COMPRESSED;
    }

    template<typename scalar_t> typename RealType<scalar_t>::value_type
    HSSMatrix<scalar_t>::update_orthogonal_basis(DenseM_t& S, DenseM_t& Q, int d, int dd, bool untouched, int depth) {
      int m = S.rows();
      if (d >= m) return real_t(0.);
      Q.resize(m, d+dd);
      copy(m, dd, S, 0, d, Q, 0, d);
      DenseMW_t Q2, Q12;
      if (untouched) {
	Q2 = DenseMW_t(m, std::min(d, m), Q, 0, 0);
	Q12 = DenseMW_t(m, std::min(d, m), Q, 0, 0);
	copy(m, d, S, 0, 0, Q, 0, 0);
      } else {
	Q2 = DenseMW_t(m, std::min(dd, m-(d-dd)), Q, 0, d-dd);
	Q12 = DenseMW_t(m, std::min(d, m), Q, 0, 0);
      }
      auto f0 = params::flops;
      Q2.orthogonalize(depth);
      params::QR_flops += params::flops - f0;
      DenseMW_t Q3(m, dd, Q, 0, d);
      DenseM_t Q12tQ3(Q12.cols(), Q3.cols());
      f0 = params::flops;
      gemm(Trans::C, Trans::N, scalar_t(1.), Q12, Q3, scalar_t(0.), Q12tQ3, depth);
      gemm(Trans::N, Trans::N, scalar_t(-1.), Q12, Q12tQ3, scalar_t(1.), Q3, depth);
      params::ortho_flops += params::flops - f0;
      return Q3.norm();
    }

  } // end namespace HSS
} // end namespace strumpack

#endif // HSS_MATRIX_COMPRESS_HPP
