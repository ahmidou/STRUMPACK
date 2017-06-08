#ifndef HSS_BASIS_ID_MPI_HPP
#define HSS_BASIS_ID_MPI_HPP

#include <cassert>

#include "DistributedMatrix.hpp"

namespace strumpack {
  namespace HSS {

    template<typename scalar_t> class HSSBasisIDMPI {
      using DistM_t = DistributedMatrix<scalar_t>;
    private:
      std::vector<int> _P;
      DistM_t _E;

    public:
      inline std::size_t rows() const { return _E.cols()+_E.rows(); }
      inline std::size_t cols() const { return _E.cols(); }
      inline const DistM_t& E() const { return _E; }
      inline const std::vector<int>& P() const { return _P; }
      inline DistM_t& E() { return _E; }
      inline std::vector<int>& P() { return _P; }

      void clear();
      void print() const { print("basis"); }
      void print(std::string name) const;

      DistM_t dense() const;
      std::size_t memory() const { return E().memory()+sizeof(int)*P().size(); }
      std::size_t nonzeros() const { return E().nonzeros()+P().size(); }

      DistM_t apply(const DistM_t& b) const;
      DistM_t applyC(const DistM_t& b) const;
      void apply(const DistM_t& b, DistM_t& c) const;
      void applyC(const DistM_t& b, DistM_t& c) const;

      DistM_t extract_rows(const std::vector<std::size_t>& I, int ctxt_all) const;
    };

    template<typename scalar_t> void HSSBasisIDMPI<scalar_t>::clear() {
      _P.clear();
      _E.clear();
    }

    template<typename scalar_t> void HSSBasisIDMPI<scalar_t>::print(std::string name) const {
      std::cout << name << " = { " << rows() << "x" << cols() << std::endl << "\tP = [";
      for (auto Pi : P()) std::cout << Pi << " ";
      std::cout << "]" << std::endl;
      _E.print("\tE");
      std::cout << "}" << std::endl;
    }

    template<typename scalar_t> DistributedMatrix<scalar_t> HSSBasisIDMPI<scalar_t>::dense() const {
      DistM_t ret(E().ctxt(), rows(), cols());
      ret.eye();
      copy(rows()-cols(), cols(), E(), 0, 0, ret, cols(), 0, E().ctxt());
      ret.permute_rows_bwd(P());
      return ret;
    }

    template<typename scalar_t> DistributedMatrix<scalar_t> HSSBasisIDMPI<scalar_t>::apply(const DistM_t& b) const {
      assert(E().ctxt()==b.ctxt());
      if (!b.active() || !rows() || !b.cols()) return DistM_t(b.ctxt(), rows(), b.cols());
      DistM_t c(b.ctxt(), rows(), b.cols());
      copy(cols(), b.cols(), b, 0, 0, c, 0, 0, b.ctxt()); // TODO just a local copy!!
      DistributedMatrixWrapper<scalar_t> tmpC(E().rows(), b.cols(), c, cols(), 0);
      if (E().rows()) gemm(Trans::N, Trans::N, scalar_t(1), E(), b, scalar_t(0.), tmpC);
      c.permute_rows_bwd(P());
      return c;
    }

    template<typename scalar_t> void HSSBasisIDMPI<scalar_t>::apply(const DistM_t& b, DistM_t& c) const {
      assert(E().ctxt()==b.ctxt());
      if (!b.active() || !rows() || !b.cols()) return;
      copy(cols(), b.cols(), b, 0, 0, c, 0, 0, b.ctxt()); // TODO just a local copy!!
      DistributedMatrixWrapper<scalar_t> tmpC(E().rows(), b.cols(), c, cols(), 0);
      if (E().rows()) gemm(Trans::N, Trans::N, scalar_t(1), E(), b, scalar_t(0.), tmpC);
      c.permute_rows_bwd(P());
    }

    template<typename scalar_t> DistributedMatrix<scalar_t> HSSBasisIDMPI<scalar_t>::applyC(const DistM_t& b) const {
      assert(E().ctxt()==b.ctxt());
      if (!b.active() || !cols() || !b.cols()) return DistM_t(b.ctxt(), E().cols(), b.cols());
      DistM_t PtB(b);
      PtB.permute_rows_fwd(P());
      if (!E().rows()) return PtB;
      DistM_t c(b.ctxt(), cols(), b.cols());
      c.copy(PtB, c.ctxt()); // TODO know that this does not require comm!!??, what to use for ctxt_all??
      auto tmpPtB = ConstDistributedMatrixWrapperPtr(E().rows(), b.cols(), PtB, cols(), 0);
      gemm(Trans::C, Trans::N, scalar_t(1.), E(), *tmpPtB, scalar_t(1.), c);
      return c;
    }

    template<typename scalar_t> void HSSBasisIDMPI<scalar_t>::applyC(const DistM_t& b, DistM_t& c) const {
      assert(E().ctxt()==b.ctxt());
      if (!b.active() || !cols() || !b.cols()) return;
      DistM_t PtB(b);
      PtB.permute_rows_fwd(P());
      if (!E().rows()) return PtB;
      c.copy(PtB, c.ctxt()); // TODO know that this does not require comm!!??, what to use for ctxt_all??
      auto tmpPtB = ConstDistributedMatrixWrapperPtr(E().rows(), b.cols(), PtB, cols(), 0);
      gemm(Trans::C, Trans::N, scalar_t(1.), E(), *tmpPtB, scalar_t(1.), c);
    }

    template<typename scalar_t> DistributedMatrix<scalar_t> HSSBasisIDMPI<scalar_t>::extract_rows
    (const std::vector<std::size_t>& I, int ctxt_all) const {
      // TODO implement this without explicitly forming the dense basis matrix
      DistM_t tmp(E().ctxt(), I.size(), cols());
      dense().extract_rows(I, tmp, ctxt_all);
      return tmp;
    }

  } // end namespace HSS
} // end namespace strumpack

#endif // HSS_BASIS_ID_MPI_HPP