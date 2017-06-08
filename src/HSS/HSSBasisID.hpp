#ifndef HSS_BASIS_ID_HPP
#define HSS_BASIS_ID_HPP

#include <cassert>

#include "DenseMatrix.hpp"

namespace strumpack {
  namespace HSS {


    /**
     * The basis is represented as P [I; E],
     * where P is a permutation, I is the identity matrix.
     */
    template<typename scalar_t> class HSSBasisID {
    private:
      // _P uses 1-based numbering!
      std::vector<int> _P; // TODO create a permutation class?
      DenseMatrix<scalar_t> _E;

    public:
      HSSBasisID() {}
      HSSBasisID(std::size_t r);
      inline std::size_t rows() const { return _E.cols()+_E.rows(); }
      inline std::size_t cols() const { return _E.cols(); }
      inline const DenseMatrix<scalar_t>& E() const { return _E; }
      inline const std::vector<int>& P() const { return _P; }
      inline DenseMatrix<scalar_t>& E() { return _E; }
      inline std::vector<int>& P() { return _P; }

      void clear();
      void print() const { print("basis"); }
      void print(std::string name) const;
      inline void check() const;

      DenseMatrix<scalar_t> dense() const;
      std::size_t memory() const { return E().memory()+sizeof(int)*P().size(); }
      std::size_t nonzeros() const { return E().nonzeros()+P().size(); }

      DenseMatrix<scalar_t> apply(const DenseMatrix<scalar_t>& b, int depth=0) const;
      DenseMatrix<scalar_t> apply(std::size_t n, const scalar_t* b, int ldb, int depth=0) const;
      void apply(const DenseMatrix<scalar_t>& b, DenseMatrix<scalar_t>& c, int depth=0) const;

      DenseMatrix<scalar_t> applyC(const DenseMatrix<scalar_t>& b, int depth=0) const;
      DenseMatrix<scalar_t> applyC(std::size_t n, const scalar_t* b, int ldb, int depth=0) const;
      void applyC(const DenseMatrix<scalar_t>& b, DenseMatrix<scalar_t>& c, int depth=0) const;
      void applyC(std::size_t n, const scalar_t* b, int ldb, DenseMatrix<scalar_t>& c, int depth=0) const;

      DenseMatrix<scalar_t> extract_rows(const std::vector<std::size_t>& I) const;
    };

    template<typename scalar_t> HSSBasisID<scalar_t>::HSSBasisID(std::size_t r) {
      _P.reserve(r);
      for (std::size_t i=1; i<=r; i++) _P.push_back(i);
      _E = DenseMatrix<scalar_t>(0, r);
    }

    template<typename scalar_t> void HSSBasisID<scalar_t>::clear() {
      _P.clear();
      _E.clear();
    }

    template<typename scalar_t> void HSSBasisID<scalar_t>::print(std::string name) const {
      std::cout << name << " = { " << rows() << "x" << cols() << std::endl << "\tP = [";
      for (auto Pi : P()) std::cout << Pi << " ";
      std::cout << "]" << std::endl;
      _E.print("\tE");
      std::cout << "}" << std::endl;
    }

    template<typename scalar_t> void HSSBasisID<scalar_t>::check() const {
      assert(P().size() == rows());
#if !defined(NDEBUG)
      for (auto Pi : P()) assert(Pi >= 1 && Pi <= int(rows()));
#endif
    }

    template<typename scalar_t> DenseMatrix<scalar_t> HSSBasisID<scalar_t>::dense() const {
      DenseMatrix<scalar_t> ret(rows(), cols());
      ret.eye();
      copy(E(), ret, cols(), 0);
      ret.permute_rows_bwd(P());
      return ret;
    }

    template<typename scalar_t> DenseMatrix<scalar_t>
    HSSBasisID<scalar_t>::apply(const DenseMatrix<scalar_t>& b, int depth) const {
      check();
      assert(cols() == b.rows());
      if (!rows() || !b.cols()) return DenseMatrix<scalar_t>(rows(), b.cols());
      DenseMatrix<scalar_t> c(rows(), b.cols());
      copy(cols(), b.cols(), b, 0, 0, c, 0, 0);
      if (E().rows()) gemm(Trans::N, Trans::N, scalar_t(1), E(), b, scalar_t(0.), c.ptr(cols(), 0), c.ld());
      c.permute_rows_bwd(P());
      return c;
    }

    template<typename scalar_t> DenseMatrix<scalar_t>
    HSSBasisID<scalar_t>::apply(std::size_t n, const scalar_t* b, int ldb, int depth) const {
      auto B = ConstDenseMatrixWrapperPtr<scalar_t>(cols(), n, b, ldb);
      return apply(*B, depth);
    }

    template<typename scalar_t> void HSSBasisID<scalar_t>::apply
    (const DenseMatrix<scalar_t>& b, DenseMatrix<scalar_t>& c, int depth) const {
      c.copy(apply(b, depth)); // TODO avoid copy!!
    }


    template<typename scalar_t> DenseMatrix<scalar_t>
    HSSBasisID<scalar_t>::applyC(const DenseMatrix<scalar_t>& b, int depth) const {
      check();
      assert(rows() == b.rows());
      if (!cols() || !b.cols()) return DenseMatrix<scalar_t>(E().cols(), b.cols());
      DenseMatrix<scalar_t> PtB(b);
      PtB.permute_rows_fwd(P());
      if (!E().rows()) return PtB;
      DenseMatrix<scalar_t> c(cols(), b.cols(), PtB.ptr(0, 0), PtB.ld());
      gemm(Trans::C, Trans::N, scalar_t(1.), E(), PtB.ptr(cols(), 0), PtB.ld(), scalar_t(1.), c, depth);
      return c;
    }

    template<typename scalar_t> DenseMatrix<scalar_t>
    HSSBasisID<scalar_t>::applyC(std::size_t n, const scalar_t* b, int ldb, int depth) const {
      auto B = ConstDenseMatrixWrapperPtr<scalar_t>(rows(), n, b, ldb);
      return applyC(*B, depth);
    }

    template<typename scalar_t> void HSSBasisID<scalar_t>::applyC
    (const DenseMatrix<scalar_t>& b, DenseMatrix<scalar_t>& c, int depth) const {
      c.copy(applyC(b, depth)); // TODO avoid copy!!
    }

    template<typename scalar_t> void HSSBasisID<scalar_t>::applyC
    (std::size_t n, const scalar_t* b, int ldb, DenseMatrix<scalar_t>& c, int depth) const {
      auto B = ConstDenseMatrixWrapperPtr<scalar_t>(rows(), n, b, ldb);
      c.copy(applyC(*B, depth)); // TODO avoid copy!!
    }

    template<typename scalar_t> DenseMatrix<scalar_t>
    HSSBasisID<scalar_t>::extract_rows(const std::vector<std::size_t>& I) const {
      // TODO implement this without explicitly forming the dense basis matrix
      return dense().extract_rows(I);
    }

  } // end namespace HSS
} // end namespace strumpack

#endif // HSS_BASIS_ID_HPP