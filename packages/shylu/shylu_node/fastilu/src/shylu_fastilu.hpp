//@HEADER
// ************************************************************************
//
//               ShyLU: Hybrid preconditioner package
//                 Copyright 2012 Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Siva Rajamanickam (srajama@sandia.gov)
//
// ************************************************************************
//@HEADER

// The struct that iterates over all non-zeros for the FastILU
//  Contact for bugs and complaints - Siva Rajamanickam (srajama@sandia.gov)
//
#ifndef __FAST_ILU_HPP__
#define __FAST_ILU_HPP__

#include <iostream>
#include <algorithm>
#include <vector>
#include <queue>
#include <random>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <string>

#include <assert.h>
#include <Kokkos_Core.hpp>
#include <Kokkos_Timer.hpp>
#include <KokkosKernels_Sorting.hpp>
#include <KokkosSparse_Utils.hpp>
#include <KokkosSparse_SortCrs.hpp>
#include <KokkosSparse_spmv.hpp>
#include <KokkosSparse_sptrsv.hpp>
#include <KokkosSparse_trsv.hpp>
#include <shylu_fastutil.hpp>

// FASTILU Preprocesser options:

// whether to print extra debug output at runtime to stdout
// comment out next line to disable
//#define FASTILU_DEBUG_OUTPUT

// whether to print timings
//#define FASTILU_TIMER

template <typename View>
void print_view(const std::string& name, const View& view)
{
  std::cout << name << "(" << view.extent(0) << "): ";
  for (size_t i = 0; i < view.extent(0); ++i) {
    std::cout << view(i) << ", ";
  }
  std::cout << std::endl;
}

template <typename T>
void print_vector(const std::string& name, const std::vector<T>& vect)
{
  std::cout << name << "(" << vect.size() << "): ";
  for (size_t i = 0; i < vect.size(); ++i) {
    std::cout << vect[i] << ", ";
  }
  std::cout << std::endl;
}

template <typename F>
bool approx_same(F a, F b)
{
  static constexpr auto EPS = std::numeric_limits<F>::epsilon() * 100;
  return std::fabs(a - b) < EPS;
}

template <typename View1, typename View2, typename View3>
std::vector<std::vector<typename View3::non_const_value_type>> decompress_matrix(
  const View1& row_map,
  const View2& entries,
  const View3& values,
  const int block_size)
{
  using size_type = typename View1::non_const_value_type;
  using lno_t     = typename View2::non_const_value_type;
  using scalar_t  = typename View3::non_const_value_type;

  const size_type nbrows   = row_map.extent(0) - 1;
  const size_type nrows    = nbrows * block_size;
  const size_type block_items = block_size * block_size;
  std::vector<std::vector<scalar_t>> result;
  result.resize(nrows);
  for (auto& row : result) {
    row.resize(nrows, 0.0);
  }

  for (size_type row_idx = 0; row_idx < nbrows; ++row_idx) {
    const size_type row_nnz_begin = row_map(row_idx);
    const size_type row_nnz_end   = row_map(row_idx + 1);
    for (size_type row_nnz = row_nnz_begin; row_nnz < row_nnz_end; ++row_nnz) {
      const lno_t col_idx      = entries(row_nnz);
      for (size_type i = 0; i < block_size; ++i) {
        const size_type unc_row_idx = row_idx*block_size + i;
        for (size_type j = 0; j < block_size; ++j) {
          const size_type unc_col_idx = col_idx*block_size + j;
          result[unc_row_idx][unc_col_idx] = values(row_nnz*block_items + i*block_size + j);
        }
      }
    }
  }

  return result;
}

template <typename scalar_t>
void print_matrix(const std::vector<std::vector<scalar_t>>& matrix) {
  for (const auto& row : matrix) {
    for (const auto& item : row) {
      std::printf("%.2f ", item);
    }
    std::cout << std::endl;
  }
}

template <typename scalar_t>
bool compare_unc_matrix(
  const std::vector<std::vector<scalar_t>>& matrix1,
  const std::vector<std::vector<scalar_t>>& matrix2) {
  const size_t rows = matrix1.size();
  if (rows != matrix2.size()) return false;

  for (size_t i = 0; i < rows; ++i) {
    const size_t cols = matrix1[i].size();
    if (cols != matrix2[i].size()) return false;
    for (size_t j = 0; j < cols; ++j) {
      if (!approx_same(matrix1[i][j], matrix2[i][j])) {
        printf("Mismatch in [%ld][%ld] %40.32E != %40.32E\n", i, j, matrix1[i][j], matrix2[i][j]);
        return false;
      }
    }
  }

  return true;
}

template <typename View1, typename View2, typename View3>
bool
compare_matrices(
  const View1& row_map1,
  const View2& entries1,
  const View3& values1,
  const int block_size1,
  const View1& row_map2,
  const View2& entries2,
  const View3& values2,
  const int block_size2,
  const std::string& name)
{
  auto unc_1 = decompress_matrix(row_map1, entries1, values1, block_size1);
  auto unc_2 = decompress_matrix(row_map2, entries2, values2, block_size2);

  if (!compare_unc_matrix(unc_1, unc_2)) {
    print_matrix(unc_1);
    std::cout << name << " MATRICES DID NOT EQUAL" << std::endl;
    print_matrix(unc_2);
    return false;
  }
  return true;
}

template <typename View>
bool
compare_views(
  const View& view1,
  const View& view2,
  const std::string& name)
{
  bool equal = view1.extent(0) == view2.extent(0);

  if (equal) {
    for (size_t i = 0; i < view1.extent(0); ++i) {
      if (!approx_same(view1(i), view2(i))) {
        equal = false;
        break;
      }
    }
  }

  if (!equal) {
    print_view(name + "1", view1);
    std::cout << name << " VIEWS DID NOT EQUAL" << std::endl;
    print_view(name + "2", view2);
  }
  return equal;
}

template <typename Ordinal>
Ordinal
unblock(const Ordinal block_idx, const Ordinal block_offset, const Ordinal bsize)
{
  return block_idx * bsize + block_offset;
}

// some useful preprocessor functions
#ifdef FASTILU_TIMER
#define FASTILU_CREATE_TIMER(timer) Kokkos::Timer timer

#define FASTILU_REPORT_TIMER(timer, report)     \
  std::cout << report << " : " << timer.seconds() << std::endl;  \
  timer.reset()

#define FASTILU_FENCE_REPORT_TIMER(timer, fenceobj, report)   \
  fenceobj.fence();                                           \
  FASTILU_REPORT_TIMER(timer, report)

#else
#define FASTILU_CREATE_TIMER(name) ((void) (0))
#define FASTILU_REPORT_TIMER(timer, report) ((void) (0))
#define FASTILU_FENCE_REPORT_TIMER(timer, fenceobj, report) ((void) (0))
#endif

#ifdef FASTILU_DEBUG_OUTPUT
#define FASTILU_DBG_COUT(args) std::cout << args << std::endl;
#else
#define FASTILU_DBG_COUT(args) ((void) (0))
#endif

// forward declarations
template<class Ordinal, class Scalar, class ExecSpace>
class FastILUFunctor;

template<class Ordinal, class Scalar, class ExecSpace>
class FastICFunctor;

template<class Ordinal, class Scalar, class ExecSpace>
class JacobiIterFunctor;

template<class Ordinal, class Scalar, class ExecSpace>
class JacobiIterFunctorT;

template<class Ordinal, class Scalar, class ExecSpace>
class BlockJacobiIterFunctorU;

template<class Ordinal, class Scalar, class ExecSpace>
class BlockJacobiIterFunctorL;

template<class Ordinal, class Scalar, class ExecSpace>
class ParCopyFunctor;

template<class Ordinal, class Scalar, class ExecSpace>
class ParPermCopyFunctor;

template<class Ordinal, class Scalar, class Real, class ExecSpace>
class ParScalFunctor;

struct NonTranPermScalTag {};
struct    TranPermScalTag {};
template<class Ordinal, class Scalar, class Real, class ExecSpace>
class PermScalFunctor;

template<class Ordinal, class Scalar, class ExecSpace>
class ParInitZeroFunctor;

template<class Ordinal, class Scalar, class ExecSpace>
class MemoryPrimeFunctorN;

template<class Ordinal, class Scalar, class ExecSpace>
class MemoryPrimeFunctorNnzCoo;

template<class Ordinal, class Scalar, class ExecSpace>
class MemoryPrimeFunctorNnzCsr;

template<class Ordinal, class Scalar, class ExecSpace>
class FastILUPrec
{
    public:
        typedef typename Teuchos::ScalarTraits<Scalar>::magnitudeType Real;
        typedef Kokkos::View<Ordinal *, ExecSpace> OrdinalArray;
        typedef Kokkos::View<Scalar *, ExecSpace> ScalarArray;
        typedef Kokkos::View<Real *, ExecSpace> RealArray;
        typedef Kokkos::View<Ordinal *, typename ExecSpace::array_layout,
                             Kokkos::Serial, Kokkos::MemoryUnmanaged> UMOrdinalArray;
        typedef Kokkos::View<Scalar *, typename ExecSpace::array_layout,
                             Kokkos::Serial, Kokkos::MemoryUnmanaged> UMScalarArray;
        typedef FastILUPrec<Ordinal, Scalar, ExecSpace> FastPrec;

        using HostSpace = Kokkos::HostSpace;
        using MirrorSpace = typename OrdinalArray::host_mirror_space;

        typedef Kokkos::View<Ordinal *, HostSpace> OrdinalArrayHost;
        typedef Kokkos::View<Scalar  *, HostSpace>  ScalarArrayHost;
        typedef typename OrdinalArray::host_mirror_type OrdinalArrayMirror;
        typedef typename ScalarArray::host_mirror_type  ScalarArrayMirror;

        using STS = Kokkos::ArithTraits<Scalar>;
        using RTS = Kokkos::ArithTraits<Real>;

  static constexpr auto identity_lambda = [](const Scalar& val) { return val; };
  using idlt = decltype(identity_lambda);

  template <typename View1, typename View2, typename L = idlt>
  KOKKOS_INLINE_FUNCTION
  static void assign_block(View1& vals_dest, const View2& vals_src, const Ordinal dest, const Ordinal src, const Ordinal blockCrsSize, const L& lam = identity_lambda)
  {
    const Ordinal blockItems = blockCrsSize*blockCrsSize;
    std::transform(vals_src.data() + src*blockItems, vals_src.data() + (src+1)*blockItems, vals_dest.data() + dest*blockItems, lam);
  }

  template <typename View1>
  KOKKOS_INLINE_FUNCTION
  static void assign_block(View1& vals_dest, const Ordinal dest, const Scalar value, const Ordinal blockCrsSize)
  {
    const Ordinal blockItems = blockCrsSize*blockCrsSize;
    std::fill(vals_dest.data() + dest*blockItems, vals_dest.data() + (dest+1)*blockItems, value);
  }

  template <typename View1, typename View2, typename LO, typename L = idlt>
  KOKKOS_INLINE_FUNCTION
  static void assign_block_cond(View1& vals_dest, const View2& vals_src, const Ordinal dest, const Ordinal src, const LO& ordinal_lam, const Ordinal blockCrsSize, const L& lam = identity_lambda)
  {
    const Ordinal blockItems = blockCrsSize*blockCrsSize;
    const Ordinal dest_offset = blockItems*dest;
    const Ordinal src_offset  = blockItems*src;
    for (Ordinal i = 0; i < blockCrsSize; ++i) {
      for (Ordinal j = 0; j < blockCrsSize; ++j) {
        const Ordinal blockOffset = blockCrsSize*i + j;
        if (ordinal_lam(i, j)) {
          vals_dest(dest_offset + blockOffset) = lam(vals_src(src_offset + blockOffset));
        }
      }
    }
  }

  template <typename View1, typename View2, typename LO, typename L = idlt>
  KOKKOS_INLINE_FUNCTION
  static void assign_block_cond_trans(View1& vals_dest, const View2& vals_src, const Ordinal dest, const Ordinal src, const LO& ordinal_lam, const Ordinal blockCrsSize, const L& lam = identity_lambda)
  {
    const Ordinal blockItems = blockCrsSize*blockCrsSize;
    const Ordinal dest_offset = blockItems*dest;
    const Ordinal src_offset  = blockItems*src;
    for (Ordinal i = 0; i < blockCrsSize; ++i) {
      for (Ordinal j = 0; j < blockCrsSize; ++j) {
        const Ordinal blockOffset  = blockCrsSize*i + j;
        const Ordinal blockOffsetT = blockCrsSize*j + i;
        if (ordinal_lam(i, j)) {
          vals_dest(dest_offset + blockOffsetT) = lam(vals_src(src_offset + blockOffset));
        }
      }
    }
  }

  template <typename View1, typename View2, typename L = idlt>
  KOKKOS_INLINE_FUNCTION
  static void assign_diag_from_block(View1& diag_dest, const View2& vals_src, const Ordinal dest, const Ordinal src, const Ordinal blockCrsSize, const L& lam = identity_lambda)
  {
    const Ordinal blockItems = blockCrsSize*blockCrsSize;
    for (Ordinal i = 0, j = blockItems*src; i < blockCrsSize; ++i, j+=(blockCrsSize+1)) {
      diag_dest(i + blockCrsSize*dest) = lam(vals_src(j));
    }
  }

  template <typename View1>
  KOKKOS_INLINE_FUNCTION
  static void assign_block_diag_only(View1& vals_dest, const Ordinal dest, typename View1::const_value_type value, const Ordinal blockCrsSize)
  {
    const Ordinal blockItems = blockCrsSize*blockCrsSize;
    for (Ordinal i = 0, j = blockItems*dest; i < blockCrsSize; ++i, j+=(blockCrsSize+1)) {
      vals_dest(j) = value;
    }
  }

  template <typename View1, typename View2, typename L = idlt>
  KOKKOS_INLINE_FUNCTION
  static void assign_diag_from_diag(View1& diag_dest, const View2& diag_src, const Ordinal dest, const Ordinal src, const Ordinal blockCrsSize, const L& lam = identity_lambda)
  {
    for (Ordinal i = 0; i < blockCrsSize; ++i) {
      diag_dest(dest*blockCrsSize + i) = lam(diag_src(src*blockCrsSize + i));
    }
  }

  template <typename View1, typename View2, typename View3, typename L>
  KOKKOS_INLINE_FUNCTION
  static void assign_block_from_2diags(View1& vals, View2& diag_src1, View3& diag_src2, const Ordinal dest, const Ordinal src1, const Ordinal src2, const Ordinal blockCrsSize, const L& lam)
  {
    const Ordinal blockItems = blockCrsSize*blockCrsSize;
    const Ordinal dest_offset = blockItems*dest;
    const Ordinal src1_offset  = blockCrsSize*src1;
    const Ordinal src2_offset  = blockCrsSize*src2;
    for (Ordinal i = 0; i < blockCrsSize; ++i) {
      for (Ordinal j = 0; j < blockCrsSize; ++j) {
        const Ordinal blockOffset = blockCrsSize*i + j;
        vals(dest_offset + blockOffset) = lam(vals(dest_offset + blockOffset), diag_src1(src1_offset+i), diag_src2(src2_offset+j));
      }
    }
  }


    private:
        double computeTime;
        double applyTime;
        double initTime;

        Ordinal nRows;
        Ordinal guessFlag;
        Ordinal nFact;
        Ordinal nTrisol;
        Ordinal level;
        Ordinal blkSzILU;
        Ordinal blkSz;
        Ordinal blockCrsSize;
        Scalar omega; //Underrelaxation parameter
        Scalar shift; //Manteuffel Shift

        // Metis
        bool useMetis;
        OrdinalArray  permMetis;
        OrdinalArray ipermMetis;
        OrdinalArrayMirror  permMetisHost;
        OrdinalArrayMirror ipermMetisHost;

        //Lower triangular factor (CSR)
        bool sptrsv_KKSpMV; // use Kokkos-Kernels SpMV for Fast SpTRSV
        ScalarArray lVal;
        OrdinalArray lColIdx;
        OrdinalArray lRowMap;
        // mirrors
        ScalarArrayMirror lVal_;
        OrdinalArrayMirror lColIdx_;
        OrdinalArrayMirror lRowMap_;

        //Lower triangular factor, without (unit) diagonals,
        // for TRSV (not SpTRSV)
        ScalarArrayHost lVal_trsv_;
        OrdinalArrayHost lColIdx_trsv_;
        OrdinalArrayHost lRowMap_trsv_;

        //Upper triangular factor (CSC)
        ScalarArray uVal;
        OrdinalArray uColIdx;
        OrdinalArray uRowMap;
        OrdinalArray a2uMap;
        // mirrors
        ScalarArrayMirror uVal_;
        OrdinalArrayMirror uColIdx_;
        OrdinalArrayMirror uRowMap_;

        //Upper triangular factor (CSR)
        ScalarArray utVal;
        OrdinalArray utColIdx;
        OrdinalArray utRowMap;
        // mirrors
        ScalarArrayMirror utVal_;
        OrdinalArrayMirror utColIdx_;
        OrdinalArrayMirror utRowMap_;

        //Upper triangular factor (CSR), with diagonal extracted out
        // for TRSV (not SpTRSV)
        bool doUnitDiag_TRSV; // perform TRSV with unit diagonals
        ScalarArrayHost   dVal_trsv_;
        ScalarArrayHost  utVal_trsv_;
        OrdinalArrayHost utColIdx_trsv_;
        OrdinalArrayHost utRowMap_trsv_;

        //Pointer to the copy of input A.
        bool skipSortMatrix;
        // device
        ScalarArray        aValIn;
        OrdinalArray       aRowMapIn;
        OrdinalArray       aColIdxIn;
        // host
        ScalarArrayMirror  aValHost;
        OrdinalArrayMirror aRowMapHost;
        OrdinalArrayMirror aColIdxHost;

        //A matrix in COO format
        ScalarArray aVal;
        OrdinalArray aRowMap;
        OrdinalArray aRowIdx;
        OrdinalArray aColIdx;
        // mirrors
        ScalarArrayMirror aVal_;
        OrdinalArrayMirror aRowMap_;
        OrdinalArrayMirror aRowIdx_;
        OrdinalArrayMirror aColIdx_;
        OrdinalArrayHost   aLvlIdx_;

        //Diagonal scaling factors
        RealArray diagFact;
        ScalarArray diagElems;

        //Temporary vectors for triangular solves
        ScalarArray xOld;
        ScalarArray xTemp;
        ScalarArray onesVector;

        //This will have the continuation initial
        //guess if guessFlag=1
        Teuchos::RCP<FastPrec> initGuessPrec;

        // forward/backwar substitution for standard SpTrsv
        using MemSpace = typename ExecSpace::memory_space;
        using KernelHandle = KokkosKernels::Experimental::KokkosKernelsHandle <Ordinal, Ordinal, Scalar, ExecSpace, MemSpace, MemSpace >;
        FastILU::SpTRSV sptrsv_algo;

        KernelHandle khL;
        KernelHandle khU;

        //Internal functions
        //Serial Transpose for now.
        //TODO:convert to parallel.
        void transposeU()
        {
            //Count the elements in each row of Ut
            auto temp = OrdinalArrayHost("temp", nRows + 1);
            auto rowPtrs = OrdinalArrayHost("rowPtrs", nRows);
            for (Ordinal i = 0; i <= nRows; i++)
            {
                temp[i] = 0;
            }
            for (Ordinal i = 0; i < nRows; i++)
            {
                for (Ordinal k = uRowMap_[i]; k < uRowMap_[i+1]; k++)
                {
                    temp[uColIdx_[k]+1]++;

                }
            }
            //Perform an add scan to get the row map for
            //the transpose
            for (Ordinal i = 0; i <= nRows; i++)
            {
                utRowMap_[i] = temp[i];
            }
            for (Ordinal i = 1; i <= nRows; i++)
            {
                utRowMap_[i] += utRowMap_[i-1];
            }
            //Set the row pointers to their initial places;
            for (Ordinal i = 0; i < nRows; i++)
            {
                rowPtrs[i] = utRowMap_[i];
            }
            //Copy the data
            Kokkos::deep_copy(uVal_, uVal);
            for (Ordinal i = 0; i < nRows; i++)
            {
                for (Ordinal k = uRowMap_[i]; k < uRowMap_[i+1]; k++)
                {
                    Ordinal row = uColIdx_[k];
                    Scalar value = uVal_[k];
                    utVal_[rowPtrs[row]] = value;
                    utColIdx_[rowPtrs[row]] = i;
                    rowPtrs[row]++;
                    assert(rowPtrs[row] <= utRowMap_[row + 1]);
                }
            }
            Kokkos::deep_copy(utRowMap, utRowMap_);
            Kokkos::deep_copy(utColIdx, utColIdx_);
            Kokkos::deep_copy(utVal, utVal_);
        }

        void findFills(int levfill, OrdinalArrayMirror aRowMap, OrdinalArrayMirror aColIdx,
                       int *nzl, std::vector<int> &lRowMap, std::vector<int> &lColIdx, std::vector<int> &lLevel,
                       int *nzu, std::vector<int> &uRowMap, std::vector<int> &uColIdx, std::vector<int> &uLevel) {
            using std::vector;
            using std::cout;
            using std::sort;

            Ordinal n = nRows;

            Ordinal row = 0, i = 0;
            vector<int> lnklst(n);
            vector<int> curlev(n);
            vector<int> iwork(n);

            int knzl = 0;
            int knzu = 0;

            lRowMap[0] = 0;
            uRowMap[0] = 0;

            for (i=0; i<n; i++)
            {
                int first, next, j;
                row = (useMetis ? permMetisHost(i) : i);

                /* copy column indices of row into workspace and sort them */
                int len = aRowMap[row+1] - aRowMap[row];
                next = 0;
                for (j=aRowMap[row]; j<aRowMap[row+1]; j++) {
                    iwork[next++] = (useMetis ? ipermMetisHost(aColIdx[j]) : aColIdx[j]);
                }
                // sort column indices in non-descending (ascending) order
                sort(iwork.begin(), iwork.begin() + len);

                /* construct implied linked list for row */
                first = iwork[0];
                curlev[first] = 0;

                for (j=0; j<=len-2; j++)
                {
                    lnklst[iwork[j]] = iwork[j+1];
                    curlev[iwork[j]] = 0;
                }

                lnklst[iwork[len-1]] = n;
                curlev[iwork[len-1]] = 0;

                /* merge with rows in U */
                next = first;
                while (next < i)
                {
                    int oldlst = next;
                    int nxtlst = lnklst[next];
                    int row = next;
                    int ii;

                    /* scan row */
                    for (ii=uRowMap[row]+1; ii<uRowMap[row+1]; /*nop*/)
                    {
                        if (uColIdx[ii] < nxtlst)
                        {
                            /* new fill-in */
                            int newlev = curlev[row] + uLevel[ii] + 1;
                            if (newlev <= levfill)
                            {
                                lnklst[oldlst]  = uColIdx[ii];
                                lnklst[uColIdx[ii]] = nxtlst;
                                oldlst = uColIdx[ii];
                                curlev[uColIdx[ii]] = newlev;
                            }
                            ii++;
                        }
                        else if (uColIdx[ii] == nxtlst)
                        {
                            int newlev;
                            oldlst = nxtlst;
                            nxtlst = lnklst[oldlst];
                            newlev = curlev[row] + uLevel[ii] + 1;
                            //curlev[uColIdx[ii]] = MIN(curlev[uColIdx[ii]], newlev);
                            if (curlev[uColIdx[ii]] > newlev)
                            {
                                curlev[uColIdx[ii]] = newlev;
                            }
                            ii++;
                        }
                        else /* (jau[ii] > nxtlst) */
                        {
                            oldlst = nxtlst;
                            nxtlst = lnklst[oldlst];
                        }
                    }
                    next = lnklst[next];
                }

                /* gather the pattern into L and U */
                /* L (no diagonal) */
                next = first;
                while (next < i)
                {
                    assert(knzl < *nzl);
                    lLevel[knzl] = curlev[next];
                    lColIdx[knzl++] = next;
                    if (knzl >= *nzl)
                    {
                        *nzl = *nzl + n;
                        lColIdx.resize(*nzl);
                        lLevel.resize(*nzl);
                    }
                    next = lnklst[next];
                }
                lRowMap[i+1] = knzl;
                assert(next == i);
                /* U (with diagonal) */
                while (next < n)
                {
                    assert(knzu < *nzu);
                    uLevel[knzu] = curlev[next];
                    uColIdx[knzu++] = next;
                    if (knzu >= *nzu)
                    {
                        *nzu = *nzu + n;
                        uColIdx.resize(*nzu);
                        uLevel.resize(*nzu);
                    }
                    next = lnklst[next];
                }
                uRowMap[i+1] = knzu;
            }

            *nzl = knzl;
            *nzu = knzu;
        }

        //Symbolic ILU code
        //initializes the matrices L and U and readies them
        //according to the level of fill
        void symbolicILU()
        {
            using WithoutInit = Kokkos::ViewAllocateWithoutInitializing;
            FASTILU_CREATE_TIMER(timer);
            using std::vector;
            using std::cout;
            using std::stable_sort;
            using std::sort;
            OrdinalArrayMirror ia = aRowMapHost;
            OrdinalArrayMirror ja = aColIdxHost;
            int *nzu;
            int *nzl;
            nzu = new int[1];
            nzl = new int[1];
            Ordinal i;

            //Compute sparsity structure of ILU
            *nzl = aRowMapHost[nRows];
            *nzu = aRowMapHost[nRows];

            *nzl *= (level + 2);
            *nzu *= (level + 2);
            vector<int> ial(nRows+1);
            vector<int> jal(*nzl);
            vector<int> levell(*nzl);
            vector<int> iau(nRows+1);
            vector<int> jau(*nzu);
            vector<int> levelu(*nzu);

            // TODO: if (initGuess & level > 0), call this with (aRowMap_, aColIdx_) and level = 1
            findFills(level, aRowMapHost, aColIdxHost, // input
                      nzl, ial, jal, levell, // output L in CSR
                      nzu, iau, jau, levelu  // output U in CSR
                     );
            int knzl = *nzl;
            int knzu = *nzu;
            FASTILU_REPORT_TIMER(timer, " findFills time");

            FASTILU_DBG_COUT(
                "knzl =" << knzl << "\n" <<
                "knzu =" << knzu << "\n" <<
                "ILU: nnz = "<< knzl + knzu << "\n" <<
                "Actual nnz for ILU: " << *nzl + *nzu);

            //Initialize the A matrix that is to be used in the computation
            aRowMap = OrdinalArray(WithoutInit("aRowMap"), nRows + 1);
            aColIdx = OrdinalArray(WithoutInit("aColIdx"), knzl + knzu);
            aRowIdx = OrdinalArray(WithoutInit("aRowIds"), knzl + knzu);
            aRowMap_ = Kokkos::create_mirror_view(Kokkos::WithoutInitializing, aRowMap);
            aColIdx_ = Kokkos::create_mirror_view(Kokkos::WithoutInitializing, aColIdx);
            aRowIdx_ = Kokkos::create_mirror_view(Kokkos::WithoutInitializing, aRowIdx);

            aLvlIdx_ = OrdinalArrayHost(WithoutInit("aLvlIdx"), knzl + knzu);

            aVal = ScalarArray(WithoutInit("aVal"), aColIdx.extent(0) * blockCrsSize * blockCrsSize);
            aVal_ = Kokkos::create_mirror_view(Kokkos::WithoutInitializing, aVal);

            Ordinal aRowPtr = 0;
            aRowMap_[0] = aRowPtr;
            for (i = 0; i < nRows; i++)
            {
                FASTILU_DBG_COUT("***row:" << i);
                for(Ordinal k = ial[i]; k < ial[i+1]; k++)
                {
                    FASTILU_DBG_COUT("jal[k]=" << jal[k]);
                    aColIdx_[aRowPtr] = jal[k];
                    aRowIdx_[aRowPtr] = i;
                    aLvlIdx_[aRowPtr] = levell[k];
                    aRowPtr++;
                }
                for(Ordinal k = iau[i]; k < iau[i+1]; k++)
                {
                    aColIdx_[aRowPtr] = jau[k];
                    aRowIdx_[aRowPtr] = i;
                    aLvlIdx_[aRowPtr] = levelu[k];
                    aRowPtr++;
                }
                aRowMap_[i+1] = aRowPtr;
            }
            FASTILU_REPORT_TIMER(timer, " Copy time");
            // sort based on ColIdx, RowIdx stays the same (do we need this?)
            using host_space = typename HostSpace::execution_space;
            KokkosSparse::sort_crs_graph<host_space, OrdinalArrayMirror, OrdinalArrayMirror>
              (aRowMap_, aColIdx_);
            FASTILU_FENCE_REPORT_TIMER(timer, host_space(), " Sort time");

            symbolicILU_common();
            FASTILU_REPORT_TIMER(timer, " Mirror");
        }

        void symbolicILU(OrdinalArrayMirror pRowMap_, OrdinalArrayMirror pColIdx_, ScalarArrayMirror pVal_, OrdinalArrayHost pLvlIdx_)
        {
            using WithoutInit = Kokkos::ViewAllocateWithoutInitializing;
            Ordinal nnzA = 0;
            for (Ordinal k = 0; k < pRowMap_(nRows); k++)  {
                if(pLvlIdx_(k) <= level) {
                   nnzA++;
                }
            }
            //Initialize the A matrix that is to be used in the computation
            aRowMap = OrdinalArray(WithoutInit("aRowMap"), nRows + 1);
            aColIdx = OrdinalArray(WithoutInit("aColIdx"), nnzA);
            aRowIdx = OrdinalArray(WithoutInit("aRowIds"), nnzA);
            aRowMap_ = Kokkos::create_mirror_view(Kokkos::WithoutInitializing, aRowMap);
            aColIdx_ = Kokkos::create_mirror_view(Kokkos::WithoutInitializing, aColIdx);
            aRowIdx_ = Kokkos::create_mirror_view(Kokkos::WithoutInitializing, aRowIdx);

            aVal = ScalarArray(WithoutInit("aVal"), nnzA * blockCrsSize * blockCrsSize);
            aVal_ = Kokkos::create_mirror_view(Kokkos::WithoutInitializing, aVal);

            Ordinal aRowPtr = 0;
            aRowMap_[0] = aRowPtr;
            for (Ordinal i = 0; i < nRows; i++)
            {
                for(Ordinal k = pRowMap_(i); k < pRowMap_(i+1); k++)
                {
                    if (pLvlIdx_(k) <= level) {
                        aColIdx_[aRowPtr] = pColIdx_[k];
                        aRowIdx_[aRowPtr] = i;
                        aRowPtr++;
                    }
                }
                aRowMap_[i+1] = aRowPtr;
            }

            symbolicILU_common();
        }

        void symbolicILU_common()
        {
            using WithoutInit = Kokkos::ViewAllocateWithoutInitializing;

            Kokkos::deep_copy(aRowMap, aRowMap_);
            Kokkos::deep_copy(aColIdx, aColIdx_);
            Kokkos::deep_copy(aRowIdx, aRowIdx_);
            FASTILU_DBG_COUT("**Finished initializing A");

            //Compute RowMap for L and U.
            // > form RowMap for L
            lRowMap = OrdinalArray(WithoutInit("lRowMap"), nRows + 1);
            lRowMap_ = Kokkos::create_mirror_view(Kokkos::WithoutInitializing, lRowMap);
            const Ordinal nnzL = countL();
            FASTILU_DBG_COUT("**Finished counting L");

            // > form RowMap for U and Ut
            uRowMap  = OrdinalArray(WithoutInit("uRowMap"), nRows + 1);
            utRowMap = OrdinalArray(WithoutInit("utRowMap"), nRows + 1);
            utRowMap_ = Kokkos::create_mirror_view(Kokkos::WithoutInitializing, utRowMap);
            uRowMap_  = Kokkos::create_mirror_view(Kokkos::WithoutInitializing, uRowMap);
            const Ordinal nnzU = countU();
            FASTILU_DBG_COUT("**Finished counting U");

            //Allocate memory and initialize pattern for L, U (transpose).
            lColIdx = OrdinalArray(WithoutInit("lColIdx"), nnzL);
            uColIdx = OrdinalArray(WithoutInit("uColIdx"), nnzU);
            utColIdx = OrdinalArray(WithoutInit("utColIdx"), nnzU);

            lVal = ScalarArray("lVal", nnzL * blockCrsSize * blockCrsSize);
            uVal = ScalarArray("uVal", nnzU * blockCrsSize * blockCrsSize);
            utVal = ScalarArray(WithoutInit("utVal"), nnzU * blockCrsSize * blockCrsSize);

            //Create mirror
            lColIdx_  = Kokkos::create_mirror_view(Kokkos::WithoutInitializing, lColIdx);
            uColIdx_  = Kokkos::create_mirror_view(Kokkos::WithoutInitializing, uColIdx);
            utColIdx_ = Kokkos::create_mirror_view(Kokkos::WithoutInitializing, utColIdx);

            lVal_    = Kokkos::create_mirror_view(Kokkos::WithoutInitializing, lVal);
            uVal_    = Kokkos::create_mirror_view(Kokkos::WithoutInitializing, uVal);
            utVal_   = Kokkos::create_mirror_view(Kokkos::WithoutInitializing, utVal);
        }

        void numericILU()
        {
            const Scalar zero = STS::zero();
            FASTILU_CREATE_TIMER(Timer);
            if (useMetis && (guessFlag == 0 || level == 0)) { // applied only at the first call (level 0)
              // apply column permutation before sorting it
              FastILUPrec_Functor perm_functor(aColIdxIn, ipermMetis);
              Kokkos::RangePolicy<ColPermTag, ExecSpace> perm_policy (0, aColIdxIn.size());
              Kokkos::parallel_for(
                "numericILU::colPerm", perm_policy, perm_functor);
            }

            //Sort each row of A by ColIdx
            if (!skipSortMatrix || useMetis) {
              if (blockCrsSize > 1) {
                KokkosSparse::sort_bsr_matrix<ExecSpace, OrdinalArray, OrdinalArray, ScalarArray>(blockCrsSize, aRowMapIn, aColIdxIn, aValIn);
              }
              else {
                KokkosSparse::sort_crs_matrix<ExecSpace, OrdinalArray, OrdinalArray, ScalarArray>(aRowMapIn, aColIdxIn, aValIn);
              }
            }

            //Copy the host matrix into the initialized a;
            //a contains the structure of ILU(k), values of original Ain is copied at level-0
            FastILUPrec_Functor functor(aValIn, aRowMapIn, aColIdxIn, aVal, diagFact, aRowMap, aColIdx, aRowIdx, blockCrsSize);
            if (useMetis) {
              FastILUPrec_Functor functor_perm(aValIn, aRowMapIn, aColIdxIn, permMetis, aVal, diagFact, aRowMap, aColIdx, aRowIdx, blockCrsSize);
              Kokkos::RangePolicy<CopySortedValsPermTag, ExecSpace> copy_perm_policy (0, nRows);
              Kokkos::parallel_for(
                "numericILU::copyVals", copy_perm_policy, functor_perm);
            } else {
              Kokkos::RangePolicy<CopySortedValsTag, ExecSpace> copy_policy (0, nRows);
              Kokkos::parallel_for(
                "numericILU::copyVals", copy_policy, functor);
            }
            FASTILU_FENCE_REPORT_TIMER(Timer, ExecSpace(), "   + sort/copy/permute values");

            // obtain diagonal scaling factor
            Kokkos::RangePolicy<GetDiagsTag, ExecSpace> get_policy (0, nRows);
            Kokkos::parallel_for(
              "numericILU::getDiags", get_policy, functor);

            // apply diagonal scaling
            Kokkos::RangePolicy<DiagScalTag, ExecSpace> scale_policy (0, nRows);
            Kokkos::parallel_for(
              "numericILU::diagScal", scale_policy, functor);

            // applyShift
            if (shift != zero) {
                Kokkos::deep_copy(aVal_, aVal);
                applyManteuffelShift();
                Kokkos::deep_copy(aVal, aVal_);
            }
            FASTILU_FENCE_REPORT_TIMER(Timer, ExecSpace(), "   + apply shift/scale");
            FASTILU_DBG_COUT("**Finished diagonal scaling");

            fillL();
            FASTILU_FENCE_REPORT_TIMER(Timer, ExecSpace(), "   + fill L");
            FASTILU_DBG_COUT("**Finished copying L");

            fillU();
            FASTILU_FENCE_REPORT_TIMER(Timer, ExecSpace(), "   + fill U");
            FASTILU_DBG_COUT(
              "**Finished copying U\n" <<
              "nnz L = " << lRowMap_[nRows] << "\n" <<
              "nnz U = " << uRowMap_[nRows]);
        }

        //Initialize the rowMap (rowPtr) for L
        int countL()
        {
            lRowMap_[0] = 0;
            for (Ordinal i = 0; i < nRows; i++)
            {
                Ordinal row_count = 0;
                for (Ordinal k = aRowMap_[i]; k < aRowMap_[i+1]; k++)
                {
                    Ordinal row = i;
                    Ordinal col = aColIdx_[k];

                    if (row >= col)
                    {
                       row_count++;
                    }
                }
                lRowMap_[i+1] = lRowMap_[i] + row_count;
            }
            Kokkos::deep_copy(lRowMap, lRowMap_);
            return lRowMap_[nRows];
        }

        //Put the initial guess into L.
        void fillL()
        {
            // extract L out of A, where A contains the structure of ILU(k), and original nonzero values at level-0
            FastILUPrec_Functor functor(aVal, aRowMap, aColIdx, lVal, lRowMap, lColIdx, diagElems, blockCrsSize);
            Kokkos::RangePolicy<GetLowerTag, ExecSpace> getL_policy (0, nRows);
            Kokkos::parallel_for(
              "numericILU::getLower", getL_policy, functor);

            if ((level > 0) && (guessFlag !=0))
            {
                // overwrite initial values from warmup runs
                OrdinalArray lGRowMap;
                OrdinalArray lGColIdx;
                ScalarArray lGVal;
                ScalarArray gD;
                initGuessPrec->getL(lGRowMap, lGColIdx, lGVal);
                initGuessPrec->getD(gD);
                Kokkos::deep_copy(diagElems, gD);

                // copy LG into L
                FastILUPrec_Functor functorG(lGVal, lGRowMap, lGColIdx, lVal, lRowMap, lColIdx, blockCrsSize);
                Kokkos::RangePolicy<CopySortedValsTag, ExecSpace> copy_policy (0, nRows);
                Kokkos::parallel_for(
                  "numericILU::copyVals(G)", copy_policy, functorG);
            }
        }

        //Initialize rowMap of U
        int countU()
        {
            // extract U out of A, where A contains the structure of ILU(k), and original nonzero values at level-0
            for (Ordinal i = 0; i <= nRows; i++)
            {
                uRowMap_[i] = 0;
            }
            for(Ordinal i = 0; i < nRows; i++)
            {
                for(Ordinal k = aRowMap_[i]; k < aRowMap_[i+1]; k++)
                {
                    Ordinal row = i;
                    Ordinal col = aColIdx_[k];
                    if (row <= col)
                    {
                        uRowMap_[col+1]++;
                    }
                }
            }
            for (Ordinal i = 0; i < nRows; i++)
            {
                uRowMap_[i+1] += uRowMap_[i];
            }
            Kokkos::deep_copy(uRowMap, uRowMap_);

            // create a map from A to U (sorted)
            auto nnzU = uRowMap_[nRows];
            a2uMap = OrdinalArray("a2uMap", nnzU);
            auto a2uMap_ = Kokkos::create_mirror_view(a2uMap);
            for (Ordinal i = 0; i < nRows; i++)
            {
                for (Ordinal k = aRowMap_[i]; k < aRowMap_[i+1]; k++)
                {
                    Ordinal row = aRowIdx_[k];
                    Ordinal col = aColIdx_[k];
                    if (row <= col)
                    {
                        Ordinal pos = uRowMap_[col];
                        a2uMap_(pos) = k;
                        uRowMap_[col]++;
                    }
                }
            }
            Kokkos::deep_copy(a2uMap, a2uMap_);
            // shift back pointer
            for (Ordinal i = nRows; i > 0; i--)
            {
                uRowMap_[i] = uRowMap_[i-1];
            }
            uRowMap_[0] = 0;

            return nnzU;
        }

        //Put initial guess into U
        void fillU()
        {
            FASTILU_CREATE_TIMER(Timer);
            int nnzU = a2uMap.extent(0);
            ParPermCopyFunctor<Ordinal, Scalar, ExecSpace> permCopy(a2uMap, aVal, aRowIdx, uVal, uColIdx, blockCrsSize);
            Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nnzU), permCopy);

            if ((level > 0) && (guessFlag !=0))
            {
                // overwrite initial values from warmup runs
                ScalarArray gD;
                initGuessPrec->getD(gD);
                Kokkos::deep_copy(diagElems, gD);

                // copy UG into U
                OrdinalArray uGRowMap;
                OrdinalArray uGColIdx;
                ScalarArray uGVal;
                initGuessPrec->getU(uGRowMap, uGColIdx, uGVal);

                Kokkos::RangePolicy<CopySortedValsTag, ExecSpace> copy_policy (0, nRows);
                FastILUPrec_Functor functorG(uGVal, uGRowMap, uGColIdx, uVal, uRowMap, uColIdx, blockCrsSize);
                Kokkos::parallel_for(
                  "numericILU::copyVals(G)", copy_policy, functorG);
                FASTILU_FENCE_REPORT_TIMER(Timer, ExecSpace(), "   + merge_sorted");
            }
        }

        void getL(OrdinalArray &lRowMapOut, OrdinalArray &lColIdxOut, ScalarArray &lValOut)
        {
            lRowMapOut = lRowMap;
            lColIdxOut = lColIdx;
            lValOut = lVal;
        }

        void getU(OrdinalArray &uRowMapOut, OrdinalArray &uColIdxOut, ScalarArray &uValOut)
        {
            uRowMapOut = uRowMap;
            uColIdxOut = uColIdx;
            uValOut = uVal;
        }

        void getUt(OrdinalArray &utRowMapOut, OrdinalArray &utColIdxOut, ScalarArray &utValOut)
        {
            utRowMapOut = utRowMap;
            utColIdxOut = utColIdx;
            utValOut = utVal;
        }

        void getD(ScalarArray &diagElemsOut)
        {
            diagElemsOut = diagElems;
        }

        void applyDiagonalScaling()
        {
            const Real one = RTS::one();
            int anext = 0;
            //First fill Aj and extract the diagonal scaling factors
            //Use diag array to store scaling factors since
            //it gets set to the correct value by findFactorPattern anyway.
            auto diagFact_ = Kokkos::create_mirror_view(diagFact);
            for (int i = 0; i < nRows; i++)
            {
                for(int k = aRowMap_[i]; k < aRowMap_[i+1]; k++)
                {
                    aRowIdx_[anext++] = i;
                    if (aColIdx_[k] == i)
                    {
                        diagFact_[i] = one/(RTS::sqrt(STS::abs(aVal_[k])));
                    }
                }
            }

            //Now go through each element of A and apply the scaling
            int row;
            int col;
            Real sc1, sc2;

            for (int i = 0; i < nRows; i++)
            {
                for (int k = aRowMap_[i]; k < aRowMap_[i+1]; k++)
                {
                    row = aRowIdx_[k];
                    col = aColIdx_[k];

                    sc1 = diagFact_[row];
                    sc2 = diagFact_[col];
                    aVal_[k] = aVal_[k]*sc1*sc2;
                }
            }
            Kokkos::deep_copy(diagFact, diagFact_);
        }

        void applyManteuffelShift()
        {
            static const Scalar one = STS::one();
            static auto shift_lambda = [&](const Scalar val) { return (one/(one + shift)) * val; };
            static constexpr auto not_diag_lamb = [](const Ordinal i, const Ordinal j) { return i != j; };
            for (Ordinal i = 0; i < nRows; i++)
            {
                for (Ordinal k = aRowMap_[i]; k < aRowMap_[i+1]; k++)
                {
                    Ordinal row = i;
                    Ordinal col = aColIdx_[k];
                    if (row != col)
                    {
                      assign_block(aVal_, aVal_, k, k, blockCrsSize, shift_lambda);
                    }
                    else {
                      assign_block_cond(aVal_, aVal_, k, k, not_diag_lamb, blockCrsSize, shift_lambda);
                    }
                }
            }
        }

        void applyD_Perm(ScalarArray &x, ScalarArray &y)
        {
            Kokkos::RangePolicy<NonTranPermScalTag, ExecSpace> scale_policy (0, nRows);
            PermScalFunctor<Ordinal, Scalar, Real, ExecSpace> functor(x, y, diagFact, permMetis);
            Kokkos::parallel_for(
              "numericILU::applyD_iPerm", scale_policy, functor);
        }

        void applyD_iPerm(ScalarArray &x, ScalarArray &y)
        {
            Kokkos::RangePolicy<TranPermScalTag, ExecSpace> scale_policy (0, nRows);
            PermScalFunctor<Ordinal, Scalar, Real, ExecSpace> functor(x, y, diagFact, ipermMetis);
            Kokkos::parallel_for(
              "numericILU::applyD_iPerm", scale_policy, functor);
        }

        void applyD(ScalarArray &x, ScalarArray &y)
        {
            ParScalFunctor<Ordinal, Scalar, Real, ExecSpace> parScal(x, y, diagFact);
            Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nRows), parScal);
        }

        void applyL(ScalarArray &x, ScalarArray &y)
        {
            ParInitZeroFunctor<Ordinal, Scalar, ExecSpace> parInitZero(xOld);
            Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nRows), parInitZero);
            BlockJacobiIterFunctorL<Ordinal, Scalar, ExecSpace> jacIter(nRows, blkSz, lRowMap, lColIdx, lVal, x, y, xOld, onesVector);
            ParCopyFunctor<Ordinal, Scalar, ExecSpace> parCopy(xOld, y);
            Ordinal extent = nRows/blkSz;
            if (nRows%blkSz != 0)
            {
                extent++;
            }
            for (Ordinal i = 0; i < nTrisol; i++)
            {
                //Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nRows), jacIter);
                Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, extent), jacIter);
                Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nRows), parCopy);
            }
        }

        void applyU(ScalarArray &x, ScalarArray &y)
        {
            ParInitZeroFunctor<Ordinal, Scalar, ExecSpace> parInitZero(xOld);
            Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nRows), parInitZero);
            ExecSpace().fence();
            BlockJacobiIterFunctorU<Ordinal, Scalar, ExecSpace> jacIter(nRows, blkSz, utRowMap, utColIdx, utVal, x, y, xOld, diagElems);
            ParCopyFunctor<Ordinal, Scalar, ExecSpace> parCopy(xOld, y);
            Ordinal extent = nRows/blkSz;
            if (nRows%blkSz != 0)
            {
                extent++;
            }
            for (Ordinal i = 0; i < nTrisol; i++)
            {
                //Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nRows), jacIter);
                Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, extent), jacIter);
                Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nRows), parCopy);
            }
        }


    public:
        //Constructor
        //TODO: Use a Teuchos::ParameterList object
        FastILUPrec(bool skipSortMatrix_, OrdinalArray &aRowMapIn_, OrdinalArray &aColIdxIn_, ScalarArray &aValIn_, Ordinal nRow_,
                    FastILU::SpTRSV sptrsv_algo_, Ordinal nFact_, Ordinal nTrisol_, Ordinal level_, Scalar omega_, Scalar shift_,
                    Ordinal guessFlag_, Ordinal blkSzILU_, Ordinal blkSz_, Ordinal blockCrsSize_ = 1)
        {
            nRows = nRow_;
            sptrsv_algo = sptrsv_algo_;
            nFact = nFact_;
            nTrisol = nTrisol_;

            useMetis = false;

            auto unc = decompress_matrix(aRowMapIn_, aColIdxIn_, aValIn_, blockCrsSize_);
            std::cout << "From FastILUPrec with blockCrsSize_=" << blockCrsSize_ << ", incoming A is:" << std::endl;
            print_matrix(unc);

            computeTime = 0.0;
            applyTime = 0.0;
            initTime = 0.0;
            //icFlag = icFlag_;
            level = level_;

            // mirror & deep-copy the input matrix
            skipSortMatrix = skipSortMatrix_;
            aRowMapIn = aRowMapIn_;
            aColIdxIn = aColIdxIn_;
            aValIn    = aValIn_;
            aRowMapHost = Kokkos::create_mirror_view(aRowMapIn_);
            aColIdxHost = Kokkos::create_mirror_view(aColIdxIn_);
            aValHost = Kokkos::create_mirror_view(aValIn_);
            Kokkos::deep_copy(aRowMapHost, aRowMapIn_);
            Kokkos::deep_copy(aColIdxHost, aColIdxIn_);
            Kokkos::deep_copy(aValHost,    aValIn_);

            omega = omega_;
            guessFlag = guessFlag_;
            shift = shift_;
            blkSzILU = blkSzILU_;
            blkSz = blkSz_;
            blockCrsSize = blockCrsSize_;
            doUnitDiag_TRSV = true; // perform TRSV with unit diagonals
            sptrsv_KKSpMV = true;   // use Kokkos-Kernels SpMV for Fast SpTRSV

            const Scalar one = STS::one();
            onesVector = ScalarArray("onesVector", nRow_);
            Kokkos::deep_copy(onesVector, one);

            diagFact = RealArray("diagFact", nRow_ * blockCrsSize_);
            diagElems = ScalarArray("diagElems", nRow_ * blockCrsSize_);
            xOld = ScalarArray("xOld", nRow_);
            xTemp = ScalarArray("xTemp", nRow_);

            if ((level > 0) && (guessFlag != 0))
            {
                initGuessPrec = Teuchos::rcp(new FastPrec(skipSortMatrix_, aRowMapIn_, aColIdxIn_, aValIn_, nRow_, sptrsv_algo_,
                                                          3, 5, level_-1, omega_, shift_, guessFlag_, blkSzILU_, blkSz_, blockCrsSize_));
            }
        }

        // internal functors
        struct ColPermTag {};
        struct CopySortedValsTag {};
        struct CopySortedValsPermTag {};
        struct GetDiagsTag {};
        struct DiagScalTag {};
        struct SwapDiagTag {};
        struct GetLowerTag{};

        struct FastILUPrec_Functor
        {
            FastILUPrec_Functor(ScalarArray aValIn_, OrdinalArray aRowMapIn_, OrdinalArray aColIdxIn_,
                                ScalarArray aVal_, RealArray diagFact_, OrdinalArray aRowMap_, OrdinalArray aColIdx_, OrdinalArray aRowIdx_, Ordinal blockCrsSize_) :
            aValIn (aValIn_),
            aRowMapIn (aRowMapIn_),
            aColIdxIn (aColIdxIn_),
            aVal (aVal_),
            diagFact (diagFact_),
            aRowMap (aRowMap_),
            aColIdx (aColIdx_),
            aRowIdx (aRowIdx_),
            blockCrsSize(blockCrsSize_)
            {}

            // just calling CopySortedValsPermTag
            FastILUPrec_Functor(ScalarArray aValIn_, OrdinalArray aRowMapIn_, OrdinalArray aColIdxIn_, OrdinalArray perm_,
                                ScalarArray aVal_, RealArray diagFact_, OrdinalArray aRowMap_, OrdinalArray aColIdx_, OrdinalArray aRowIdx_, Ordinal blockCrsSize_) :
            aValIn (aValIn_),
            aRowMapIn (aRowMapIn_),
            aColIdxIn (aColIdxIn_),
            aVal (aVal_),
            diagFact (diagFact_),
            aRowMap (aRowMap_),
            aColIdx (aColIdx_),
            aRowIdx (aRowIdx_),
            iperm (perm_),
            blockCrsSize(blockCrsSize_)
            {}

            // just calling CopySortedValsTag, or GetUpperTag
            FastILUPrec_Functor(ScalarArray aValIn_, OrdinalArray aRowMapIn_, OrdinalArray aColIdxIn_,
                                ScalarArray aVal_, OrdinalArray aRowMap_, OrdinalArray aColIdx_, Ordinal blockCrsSize_) :
            aValIn (aValIn_),
            aRowMapIn (aRowMapIn_),
            aColIdxIn (aColIdxIn_),
            aVal (aVal_),
            aRowMap (aRowMap_),
            aColIdx (aColIdx_),
            blockCrsSize(blockCrsSize_)
            {}

            // just calling GetLowerTag
            FastILUPrec_Functor(ScalarArray aVal_, OrdinalArray aRowMap_, OrdinalArray aColIdx_,
                                ScalarArray lVal_, OrdinalArray lRowMap_, OrdinalArray lColIdx_,
                                ScalarArray diagElems_, Ordinal blockCrsSize_) :
            aVal (aVal_),
            diagElems (diagElems_),
            aRowMap (aRowMap_),
            aColIdx (aColIdx_),
            lVal (lVal_),
            lRowMap (lRowMap_),
            lColIdx (lColIdx_),
            blockCrsSize(blockCrsSize_)
            {}

            // just calling SwapDiagTag
            FastILUPrec_Functor(const SwapDiagTag&, ScalarArray  lVal_, OrdinalArray  lRowMap_, OrdinalArray lColIdx_,
                                ScalarArray utVal_, OrdinalArray utRowMap_, OrdinalArray utColIdx_,
                                ScalarArray diagElems_, Ordinal blockCrsSize_) :
            diagElems (diagElems_),
            lVal (lVal_),
            lRowMap (lRowMap_),
            lColIdx (lColIdx_),
            utVal (utVal_),
            utRowMap (utRowMap_),
            utColIdx (utColIdx_),
            blockCrsSize(blockCrsSize_)
            {}

            // just calling ColPerm
            FastILUPrec_Functor(OrdinalArray aColIdx_, OrdinalArray iperm_) :
            aColIdx (aColIdx_),
            iperm (iperm_),
            blockCrsSize(0)
            {}


            // ------------------------------------------------
            // functor to load values
            // both matrices are sorted and, "a" (with fills) contains "aIn" (original)
            KOKKOS_INLINE_FUNCTION
            void operator()(const CopySortedValsTag &, const int i) const {
                Ordinal aPtr = aRowMapIn[i];
                for(Ordinal k = aRowMap[i]; k < aRowMap[i+1]; k++)
                {
                    Ordinal col = aColIdx[k];
                    if (col == aColIdxIn[aPtr])
                    {
                        assign_block(aVal, aValIn, k, aPtr, blockCrsSize);
                        aPtr++;
                    } else
                    {
                      assign_block(aVal, k, STS::zero(), blockCrsSize);
                    }
                }
            }

            // ------------------------------------------------
            // functor to load values with perm
            // both matrices are sorted and, "a" (with fills) contains "aIn" (original)
            KOKKOS_INLINE_FUNCTION
            void operator()(const CopySortedValsPermTag &, const int i) const {
                Ordinal aPtr = aRowMapIn[iperm[i]];
                for(Ordinal k = aRowMap[i]; k < aRowMap[i+1]; k++)
                {
                    Ordinal col = aColIdx[k];
                    if (col == aColIdxIn[aPtr])
                    {
                        assign_block(aVal, aValIn, k, aPtr, blockCrsSize);
                        aPtr++;
                    } else
                    {
                        assign_block(aVal, k, STS::zero(), blockCrsSize);
                    }
                }
            }

            // functor to extract diagonals (inverted)
            KOKKOS_INLINE_FUNCTION
            void operator()(const GetDiagsTag &, const int i) const {
                static const Real one = RTS::one();
                static constexpr auto dlambda = [&](const Scalar& val) { return one/(RTS::sqrt(STS::abs(val))); };
                for(int k = aRowMap[i]; k < aRowMap[i+1]; k++)
                {
                    aRowIdx[k] = i;
                    if (aColIdx[k] == i)
                    {
                      assign_diag_from_block(diagFact, aVal, i, k, blockCrsSize, dlambda);
                    }
                }
            }

            // functor to swap diagonals
            KOKKOS_INLINE_FUNCTION
            void operator()(const SwapDiagTag &, const int i) const {
                static const Scalar one  = STS::one();
                static const Scalar zero = STS::zero();
                // zero the diagonal of L. If sorted, this finds it on first iter.
                Ordinal lRowBegin = lRowMap(i);
                Ordinal lRowEnd = lRowMap(i + 1);
                for(Ordinal j = 0; j < lRowEnd - lRowBegin; j++) {
                  Ordinal reversed = lRowEnd - j - 1;
                  if(lColIdx(reversed) == i) {
                    assign_block_diag_only(lVal, reversed, zero, blockCrsSize);
                    break;
                  }
                }
                // zero the diagonal of Ut. If sorted, this finds it on first iter.
                Ordinal utRowBegin = utRowMap(i);
                Ordinal utRowEnd = utRowMap(i + 1);
                for(Ordinal j = utRowBegin; j < utRowEnd; j++) {
                  if(utColIdx(j) == i) {
                    assign_block_diag_only(utVal, j, zero, blockCrsSize);
                    break;
                  }
                }
                // invert D
                static constexpr auto dlambda = [&](const Scalar& val) { return one/val; };
                assign_diag_from_diag(diagElems, diagElems, i, i, blockCrsSize, dlambda);
            }

            // functor to apply diagonal scaling
            KOKKOS_INLINE_FUNCTION
            void operator()(const DiagScalTag &, const int i) const {
                static constexpr auto dlambda = [&](const Scalar& val1, const Scalar& val2, const Scalar& val3) { return val1*val2*val3; };
                for (int k = aRowMap[i]; k < aRowMap[i+1]; k++)
                {
                    const Ordinal col = aColIdx[k];
                    assign_block_from_2diags(aVal, diagFact, diagFact, k, i, col, blockCrsSize, dlambda);
                }
            }

            // ----------------------------------------------------------
            // functor to extract L & diagongals
            KOKKOS_INLINE_FUNCTION
            void operator()(const GetLowerTag &, const int i) const {
                Ordinal lPtr = lRowMap[i];
                static constexpr auto lower_lamb = [](const Ordinal i, const Ordinal j) { return i > j; };
                for (Ordinal k = aRowMap[i]; k < aRowMap[i+1]; k++)
                {
                    Ordinal row = i;
                    Ordinal col = aColIdx[k];
                    if (row >= col)
                    {
                        if (row == col)
                        {
                          assign_diag_from_block(diagElems, aVal, row, k, blockCrsSize);
                          assign_block_diag_only(lVal, lPtr, STS::one(), blockCrsSize);
                          assign_block_cond(lVal, aVal, lPtr, k, lower_lamb, blockCrsSize);
                        } else {
                          assign_block(lVal, aVal, lPtr, k, blockCrsSize);
                        }
                        lColIdx[lPtr] = col;
                        lPtr++;
                    }
                }
            }

            // ----------------------------------------------------------
            // functor to apply column permutation
            KOKKOS_INLINE_FUNCTION
            void operator()(const ColPermTag &, const int i) const {
              aColIdx(i) = iperm(aColIdx(i));
            }

            // member variables
            // + input matrix
            ScalarArray    aValIn;
            OrdinalArray   aRowMapIn;
            OrdinalArray   aColIdxIn;
            // + output matrix
            ScalarArray    aVal;
            ScalarArray    diagElems;
            RealArray      diagFact;
            OrdinalArray   aRowMap;
            OrdinalArray   aColIdx;
            OrdinalArray   aRowIdx;
            // + output L matrix
            ScalarArray    lVal;
            OrdinalArray   lRowMap;
            OrdinalArray   lColIdx;
            // + output U matrix
            ScalarArray    utVal;
            OrdinalArray   utRowMap;
            OrdinalArray   utColIdx;
            // permutation
            OrdinalArray   iperm;
            // blockCrs block size
            const Ordinal  blockCrsSize;
        };

        // set Metis pre-ordering
        template<class MetisArrayHost>
        void setMetisPerm(MetisArrayHost permMetis_, MetisArrayHost ipermMetis_)
        {
          Ordinal nRows_ = permMetis_.size();
          if (nRows > 0) {
            permMetis = OrdinalArray("permMetis", nRows_);
            ipermMetis = OrdinalArray("ipermMetis", nRows_);

            permMetisHost = Kokkos::create_mirror_view(permMetis);
            ipermMetisHost = Kokkos::create_mirror_view(ipermMetis);
            for (Ordinal i = 0; i < nRows_; i++) {
              permMetisHost(i) = permMetis_(i);
              ipermMetisHost(i) = ipermMetis_(i);
            }
            Kokkos::deep_copy(permMetis, permMetisHost);
            Kokkos::deep_copy(ipermMetis, ipermMetisHost);
          }
          if ((level > 0) && (guessFlag != 0))
          {
            initGuessPrec->setMetisPerm(permMetis_, ipermMetis_);
          }
          useMetis = true;
        }

        //Symbolic Factorization Phase
        void initialize()
        {
            Kokkos::Timer timer;
            FASTILU_CREATE_TIMER(timer2);
            // call symbolic that generates A with level associated to each nonzero entry
            // then pass that to initialize the initGuessPrec
            symbolicILU();
            FASTILU_REPORT_TIMER(timer2, " + initial SymbolicILU (" << level << ") time");
            if ((level > 0) && (guessFlag != 0))
            {
                initGuessPrec->initialize(aRowMap_, aColIdx_, aVal_, aLvlIdx_);
                FASTILU_REPORT_TIMER(timer2, "  > SymbolicILU (" << level << ") time");
            }

            initialize_common(timer);
            FASTILU_REPORT_TIMER(timer, "Symbolic phase complete.\nInit time");
        }

        //Symbolic Factorization Phase
        void initialize(OrdinalArrayMirror pRowMap_, OrdinalArrayMirror pColIdx_, ScalarArrayMirror pVal_, OrdinalArrayHost pLvlIdx_)
        {
            Kokkos::Timer timer;
            FASTILU_CREATE_TIMER(timer2);
            // call symbolic that generates A with level associated to each nonzero entry
            // then pass that to initialize the initGuessPrec
            symbolicILU(pRowMap_, pColIdx_, pVal_, pLvlIdx_);
            FASTILU_REPORT_TIMER(timer2, " - initial SymbolicILU (" << level << ") time");
            if ((level > 0) && (guessFlag != 0))
            {
                initGuessPrec->initialize(pRowMap_, pColIdx_, pVal_, pLvlIdx_);
                FASTILU_REPORT_TIMER(timer2, "  = SymbolicILU (" << level << ") time");
            }

            initialize_common(timer);
            FASTILU_REPORT_TIMER(timer, " + Symbolic phase complete.\n + Init time");
        }

        void initialize_common(Kokkos::Timer& timer)
        {
            //Allocate memory for the local A.
            //initialize L, U, A patterns
            #ifdef SHYLU_DEBUG
            Ordinal nnzU = uRowMap[nRows];
            MemoryPrimeFunctorN<Ordinal, Scalar, ExecSpace> copyFunc1(aRowMap, lRowMap, uRowMap, diagElems);
            MemoryPrimeFunctorNnzCsr<Ordinal, Scalar, ExecSpace> copyFunc4(uColIdx, uVal);

            Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nRows), copyFunc1);
            Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nnzU), copyFunc4);

            //Note that the following is a temporary measure
            //to ensure that memory resides on the device.
            Ordinal nnzL = lRowMap[nRows];
            Ordinal nnzA = aRowMap[nRows];
            MemoryPrimeFunctorNnzCoo<Ordinal, Scalar, ExecSpace> copyFunc2(aColIdx, aRowIdx, aVal);
            MemoryPrimeFunctorNnzCsr<Ordinal, Scalar, ExecSpace> copyFunc3(lColIdx, lVal);

            Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nRows), copyFunc1);
            Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nnzA), copyFunc2);
            Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nnzL), copyFunc3);
            #endif
            ExecSpace().fence();  //Fence so that init time is accurate
            initTime = timer.seconds();
        }

        void setValues(ScalarArray& aValIn_)
        {
          this->aValIn = aValIn_;
          this->aValHost = Kokkos::create_mirror_view(aValIn_);
          Kokkos::deep_copy(this->aValHost, aValIn_);
          if(!initGuessPrec.is_null())
          {
            initGuessPrec->setValues(aValIn_);
          }
        }

  void verify(const FastPrec& rhs, const bool initialize_only=false)
  {
    std::cout << "JGF Verifying level:" << level << std::endl;
    // verify a this using brs and a rhs using crs
    assert(nRows*blockCrsSize == rhs.nRows);
    assert(guessFlag == rhs.guessFlag);
    assert(nFact == rhs.nFact);
    assert(nTrisol == rhs.nTrisol);
    assert(level == rhs.level);
    assert(blkSzILU == rhs.blkSzILU);
    assert(blkSz == rhs.blkSz);
    assert(rhs.blockCrsSize == 1);
    assert(omega == rhs.omega);
    assert(shift == rhs.shift);

    assert(!useMetis);
    assert(useMetis == rhs.useMetis);

    assert(compare_matrices(aRowMapIn, aColIdxIn, aValIn, blockCrsSize, rhs.aRowMapIn, rhs.aColIdxIn, rhs.aValIn, rhs.blockCrsSize, "Ain"));

    if (!initialize_only) {
      assert(compare_matrices(aRowMap, aColIdx, aVal, blockCrsSize, rhs.aRowMap, rhs.aColIdx, rhs.aVal, rhs.blockCrsSize, "A"));

      assert(compare_views(diagFact, rhs.diagFact, "diagFact"));
      assert(compare_views(diagElems, rhs.diagElems, "diagElems"));

      assert(compare_matrices(lRowMap, lColIdx, lVal, blockCrsSize, rhs.lRowMap, rhs.lColIdx, rhs.lVal, 1, "L"));

      assert(compare_matrices(uRowMap, uColIdx, uVal, blockCrsSize, rhs.uRowMap, rhs.uColIdx, rhs.uVal, 1, "U"));

      //assert(compare_matrices(utRowMap, utColIdx, utVal, blockCrsSize, rhs.utRowMap, rhs.utColIdx, rhs.utVal, 1, "Ut"));
    }

    if ((level > 0) && (guessFlag != 0)) {
      initGuessPrec->verify(*rhs.initGuessPrec, initialize_only);
    }
  }

        //Actual computation phase.
        //blkSzILU is the chunk size (hard coded).
        //1 gives the best performance on GPUs.
        //
        void compute()
        {
            Kokkos::Timer timer;
            FASTILU_CREATE_TIMER(Timer);
            if ((level > 0) && (guessFlag !=0))
            {
                initGuessPrec->compute();
                FASTILU_FENCE_REPORT_TIMER(Timer, ExecSpace(), "  > initGuess");
            }

            numericILU();
            FASTILU_FENCE_REPORT_TIMER(Timer, ExecSpace(), "  > numericILU ");

            FastILUFunctor<Ordinal, Scalar, ExecSpace> iluFunctor(aRowMap_[nRows], blkSzILU,
                    aRowMap, aRowIdx, aColIdx, aVal,
                    lRowMap, lColIdx, lVal, uRowMap, uColIdx, uVal, diagElems, omega, blockCrsSize);
            Ordinal extent = aRowMap_[nRows]/blkSzILU;
            if (aRowMap_[nRows]%blkSzILU != 0)
            {
                extent++;
            }

            for (int i = 0; i < nFact; i++)
            {
                Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, extent), iluFunctor);
            }
            FASTILU_FENCE_REPORT_TIMER(Timer, ExecSpace(), "  > iluFunctor (" << nFact << ")");

            return; // JGF MADE IT THIS FAR

            // transpose u
            Kokkos::deep_copy(utRowMap, 0);
            KokkosSparse::Impl::transpose_matrix<OrdinalArray, OrdinalArray, ScalarArray, OrdinalArray, OrdinalArray, ScalarArray, OrdinalArray, ExecSpace>
              (nRows, nRows, uRowMap, uColIdx, uVal, utRowMap, utColIdx, utVal);
            // sort, if the triangular solve algorithm requires a sorted matrix.
            bool sortRequired = sptrsv_algo != FastILU::SpTRSV::Fast && sptrsv_algo != FastILU::SpTRSV::StandardHost;
            if (sortRequired) {
              KokkosSparse::sort_crs_matrix<ExecSpace, OrdinalArray, OrdinalArray, ScalarArray>
                (utRowMap, utColIdx, utVal);
            }
            if (sptrsv_algo == FastILU::SpTRSV::StandardHost) {
                // deep-copy to host
                Kokkos::deep_copy(lColIdx_, lColIdx);
                Kokkos::deep_copy(lVal_, lVal);

                Kokkos::deep_copy(utRowMap_, utRowMap);
                Kokkos::deep_copy(utColIdx_, utColIdx);
                Kokkos::deep_copy(utVal_, utVal);
            }
            FASTILU_FENCE_REPORT_TIMER(Timer, ExecSpace(), "  > transposeU");

            if (sptrsv_algo == FastILU::SpTRSV::Standard) {
                #if defined(KOKKOSKERNELS_ENABLE_TPL_CUSPARSE)
                KokkosSparse::Experimental::SPTRSVAlgorithm algo = KokkosSparse::Experimental::SPTRSVAlgorithm::SPTRSV_CUSPARSE;
                #else
                KokkosSparse::Experimental::SPTRSVAlgorithm algo = KokkosSparse::Experimental::SPTRSVAlgorithm::SEQLVLSCHD_TP1;
                #endif
                // setup L solve
                khL.create_sptrsv_handle(algo, nRows, true);
                #if defined(KOKKOSKERNELS_ENABLE_TPL_CUSPARSE)
                KokkosSparse::Experimental::sptrsv_symbolic(&khL, lRowMap, lColIdx, lVal);
                #else
                KokkosSparse::Experimental::sptrsv_symbolic(&khL, lRowMap, lColIdx);
                #endif
                // setup U solve
                khU.create_sptrsv_handle(algo, nRows, false);
                #if defined(KOKKOSKERNELS_ENABLE_TPL_CUSPARSE)
                KokkosSparse::Experimental::sptrsv_symbolic(&khU, utRowMap, utColIdx, utVal);
                #else
                KokkosSparse::Experimental::sptrsv_symbolic(&khU, utRowMap, utColIdx);
                #endif
                FASTILU_FENCE_REPORT_TIMER(Timer, ExecSpace(),
                  "  > sptrsv_symbolic : nnz(L)=" << lColIdx.extent(0) << " nnz(U)=" << utColIdx.extent(0));
            } else if (sptrsv_algo == FastILU::SpTRSV::StandardHost && doUnitDiag_TRSV) {
                // Prepare L for TRSV by removing unit-diagonals
                lVal_trsv_   = ScalarArrayHost ("lVal_trsv",    lRowMap_[nRows]-nRows);
                lColIdx_trsv_ = OrdinalArrayHost("lColIdx_trsv", lRowMap_[nRows]-nRows);
                lRowMap_trsv_ = OrdinalArrayHost("lRowMap_trsv", nRows+1);

                size_t nnzL = 0;
                lRowMap_trsv_(0) = 0;
                for (Ordinal i = 0; i < nRows; i++) {
                    for (Ordinal k = lRowMap_(i); k < lRowMap_[i+1]; k++) {
                        if (lColIdx_(k) != i) {
                            lVal_trsv_(nnzL) = lVal_(k);
                            lColIdx_trsv_(nnzL) = lColIdx_(k);
                            nnzL++;
                        }
                    }
                    lRowMap_trsv_(i+1)=nnzL;
                }

                // Prepare U by extracting and scaling D
                dVal_trsv_     = ScalarArrayHost ("dVal_trsv",     nRows);
                utVal_trsv_    = ScalarArrayHost ("utVal_trsv",    utRowMap_[nRows]-nRows);
                utColIdx_trsv_ = OrdinalArrayHost("utColIdx_trsv", utRowMap_[nRows]-nRows);
                utRowMap_trsv_ = OrdinalArrayHost("utRowMap_trsv", nRows+1);

                size_t nnzU = 0;
                utRowMap_trsv_(0) = 0;
                for (Ordinal i = 0; i < nRows; i++) {
                    for (Ordinal k = utRowMap_(i); k < utRowMap_[i+1]; k++) {
                        if (utColIdx_(k) == i) {
                            dVal_trsv_(i) = utVal_(k);
                        } else {
                            utVal_trsv_(nnzU) = utVal_(k);
                            utColIdx_trsv_(nnzU) = utColIdx_(k);
                            nnzU++;
                        }
                    }
                    utRowMap_trsv_(i+1)=nnzU;
                }
                for (Ordinal i = 0; i < nRows; i++) {
                    for (Ordinal k = utRowMap_trsv_(i); k < utRowMap_trsv_[i+1]; k++) {
                        utVal_trsv_(k) = utVal_trsv_(k) / dVal_trsv_(i);
                    }
                    dVal_trsv_(i) = STS::one() / dVal_trsv_(i);
                }
            } else if (sptrsv_KKSpMV) {
                FastILUPrec_Functor functor(SwapDiagTag(), lVal, lRowMap, lColIdx, utVal, utRowMap, utColIdx, diagElems, blockCrsSize);
                Kokkos::RangePolicy<SwapDiagTag, ExecSpace> swap_policy (0, nRows);
                Kokkos::parallel_for(
                  "numericILU::swapDiag", swap_policy, functor);
            }
            ExecSpace().fence(); // Fence so computeTime is accurate
            computeTime = timer.seconds();
            FASTILU_REPORT_TIMER(timer, "  >> compute done\n");
        }

        //Preconditioner application. Note that this does
        //*not* support multiple right hand sizes.
        void apply(ScalarArray &x, ScalarArray &y)
        {
            Kokkos::Timer timer;
            const Scalar one(1.0);
            const Scalar minus_one(-1.0);

            //required to prevent contamination of the input.
            ParCopyFunctor<Ordinal, Scalar, ExecSpace> parCopyFunctor(xTemp, x);
            Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nRows), parCopyFunctor);
            //apply D
            if (useMetis) {
                applyD_Perm(x, xTemp);
            } else {
                applyD(x, xTemp);
            }
            if (sptrsv_algo == FastILU::SpTRSV::Standard) {
                // solve with L
                KokkosSparse::Experimental::sptrsv_solve(&khL, lRowMap, lColIdx, lVal, xTemp, y);
                // solve with U
                KokkosSparse::Experimental::sptrsv_solve(&khU, utRowMap, utColIdx, utVal, y, xTemp);
            } else {
                // wrap x and y into 2D views
                typedef Kokkos::View<Scalar **, ExecSpace> Scalar2dArray;
                Scalar2dArray x2d (const_cast<Scalar*>(xTemp.data()), nRows, 1);
                Scalar2dArray y2d (const_cast<Scalar*>(y.data()), nRows, 1);

                if (sptrsv_algo == FastILU::SpTRSV::StandardHost) {

                    // copy x to host
                    auto x_ = Kokkos::create_mirror_view(Kokkos::WithoutInitializing, x2d);
                    // don't need to initialize y_ because KokkosSparse::trsv always overwrites the output vectors
                    // (there is no alpha/beta, it's as if beta is always 0)
                    auto y_ = Kokkos::create_mirror_view(Kokkos::WithoutInitializing, y2d);
                    Kokkos::deep_copy(x_, x2d);

                    if (doUnitDiag_TRSV) {
                        using crsmat_host_t = KokkosSparse::CrsMatrix<Scalar, Ordinal, HostSpace, void, Ordinal>;
                        using graph_host_t  = typename crsmat_host_t::StaticCrsGraphType;

                        // wrap L into crsmat on host
                        graph_host_t static_graphL(lColIdx_trsv_, lRowMap_trsv_);
                        crsmat_host_t crsmatL("CrsMatrix", nRows, lVal_trsv_, static_graphL);

                        // wrap U into crsmat on host
                        graph_host_t static_graphU(utColIdx_trsv_, utRowMap_trsv_);
                        crsmat_host_t crsmatU("CrsMatrix", nRows, utVal_trsv_, static_graphU);

                        // solve with L, unit-diag
                        KokkosSparse::trsv ("L", "N", "U", crsmatL, x_, y_);
                        // solve with D
                        for (Ordinal i = 0; i < nRows; i++) {
                            y_(i, 0) = dVal_trsv_(i) * y_(i, 0);
                        }
                        // solve with U, unit-diag
                        KokkosSparse::trsv ("U", "N", "U", crsmatU, y_, x_);
                    } else {
                        using crsmat_mirror_t = KokkosSparse::CrsMatrix<Scalar, Ordinal, MirrorSpace, void, Ordinal>;
                        using graph_mirror_t  = typename crsmat_mirror_t::StaticCrsGraphType;

                        // wrap L into crsmat on host
                        graph_mirror_t static_graphL(lColIdx_, lRowMap_);
                        crsmat_mirror_t crsmatL("CrsMatrix", nRows, lVal_, static_graphL);

                        // wrap U into crsmat on host
                        graph_mirror_t static_graphU(utColIdx_, utRowMap_);
                        crsmat_mirror_t crsmatU("CrsMatrix", nRows, utVal_, static_graphU);

                        // solve with L
                        KokkosSparse::trsv ("L", "N", "N", crsmatL, x_, y_);
                        // solve with U
                        KokkosSparse::trsv ("U", "N", "N", crsmatU, y_, x_);
                    }
                    // copy x to device
                    Kokkos::deep_copy(x2d, x_);
                } else {
                    if (sptrsv_KKSpMV) {
                        using crsmat_t = KokkosSparse::CrsMatrix<Scalar, Ordinal, ExecSpace, void, Ordinal>;
                        using graph_t  = typename crsmat_t::StaticCrsGraphType;

                        graph_t static_graphL(lColIdx, lRowMap);
                        crsmat_t crsmatL("CrsMatrix", nRows, lVal, static_graphL);

                        graph_t static_graphU(utColIdx, utRowMap);
                        crsmat_t crsmatU("CrsMatrix", nRows, utVal, static_graphU);

                        Scalar2dArray x2d_old (const_cast<Scalar*>(xOld.data()), nRows, 1);

                        // 1) approximately solve, y = L^{-1}*x
                        // functor to copy RHS x into y (for even iteration)
                        ParCopyFunctor<Ordinal, Scalar, ExecSpace> copy_x2y(y, xTemp);
                        // functor to copy RHS x into xold (for odd iteration)
                        ParCopyFunctor<Ordinal, Scalar, ExecSpace> copy_x2xold(xOld, xTemp);

                        // functor to copy x_old to y (final iteration)
                        ParCopyFunctor<Ordinal, Scalar, ExecSpace> copy_xold2y(y, xOld);

                        // xold = zeros
                        ParInitZeroFunctor<Ordinal, Scalar, ExecSpace> initZeroX(xOld);
                        Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nRows), initZeroX);
                        //Kokkos::deep_copy(x2d_old, zero);
                        for (Ordinal i = 0; i < nTrisol; i++)
                        {
                            if (i%2 == 0) {
                                // y = y - L*x_old
                                // > y = x
                                Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nRows), copy_x2y);
                                // > y = y - L*x_old
                                KokkosSparse::spmv("N", minus_one, crsmatL, x2d_old, one, y2d);
                            } else {
                                // x_old = x_old - L*y
                                // > x_old = x
                                Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nRows), copy_x2xold);
                                // > x_old = x_old - L*y
                                KokkosSparse::spmv("N", minus_one, crsmatL, y2d, one, x2d_old);

                                if (i == nTrisol-1) {
                                    // y = x_old
                                    Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nRows), copy_xold2y);
                                }
                            }
                        }

                        // 2) approximately solve, x = U^{-1}*y
                        // functor to copy y into x
                        ParCopyFunctor<Ordinal, Scalar, ExecSpace> copy_y2x(xTemp, y);
                        // functor to copy y into xold
                        ParCopyFunctor<Ordinal, Scalar, ExecSpace> copy_y2xold(xOld, y);

                        // functor to scale x
                        ParScalFunctor<Ordinal, Scalar, Scalar, ExecSpace> scal_x(xTemp, xTemp, diagElems);
                        ParScalFunctor<Ordinal, Scalar, Scalar, ExecSpace> scal_xold(xOld, xOld, diagElems);

                        // functor to copy x_old to x (final iteration)
                        ParCopyFunctor<Ordinal, Scalar, ExecSpace> copy_xold2x(xTemp, xOld);

                        // xold = zeros
                        Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nRows), initZeroX);
                        //Kokkos::deep_copy(x2d_old, zero);
                        for (Ordinal i = 0; i < nTrisol; i++)
                        {
                            if (i%2 == 0) {
                                // x = y - U*x_old
                                // > x = y
                                Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nRows), copy_y2x);
                                // > x = x - U*x_old
                                KokkosSparse::spmv("N", minus_one, crsmatU, x2d_old, one, x2d);
                                // > scale x = inv(diag(U))*x
                                Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nRows), scal_x);
                            } else {
                                // xold = y - U*x
                                // > xold = y
                                Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nRows), copy_y2xold);
                                // > x = x - U*x_old
                                KokkosSparse::spmv("N", minus_one, crsmatU, x2d, one, x2d_old);
                                // > scale x = inv(diag(U))*x
                                Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nRows), scal_xold);

                                if (i == nTrisol-1) {
                                    // x = x_old
                                    Kokkos::parallel_for(Kokkos::RangePolicy<ExecSpace>(0, nRows), copy_xold2x);
                                }
                            }
                        }
                    } else {
                        //apply L^{-1} to xTemp
                        applyL(xTemp, y);
                        //apply U^{-1} to y
                        applyU(y, xTemp);
                    }
                }
            }
            //apply D again (we assume that the scaling is
            //symmetric for now).
            if (useMetis) {
                applyD_iPerm(xTemp, y);
            } else {
                applyD(xTemp, y);
            }
            //Only fencing here so that apply time is accurate
            ExecSpace().fence();
            double t = timer.seconds();
            applyTime = t;
        }

        Ordinal getNFact() const
        {
            return nFact;
        }

        std::string getSpTrsvType() const
        {
            if (sptrsv_algo == FastILU::SpTRSV::StandardHost) {
                return "Standard Host";
            } else if (sptrsv_algo == FastILU::SpTRSV::Standard) {
                return "Standard";
            } else if (sptrsv_algo == FastILU::SpTRSV::Fast) {
                return "Fast";
            }
            return "Invalid";
        }

        Ordinal getNTrisol() const
        {
            return nTrisol;
        }

        Ordinal getNRows() const
        {
            return nRows;
        }

        double getComputeTime() const
        {
            return computeTime;
        }

        double getInitializeTime() const
        {
            return initTime;
        }

        double getApplyTime() const
        {
            return applyTime;
        }

        //Compute the L2 norm of the nonlinear residual (A - LU) on sparsity pattern
        void checkILU() const
        {
            Scalar sum = 0.0;
            Scalar sum_diag = 0.0;
            for (int i = 0 ; i < nRows; i++)
            {
                // Each row in A matrix (result)
                for (int k = aRowMap[i]; k < aRowMap[i+1]; k++)
                {
                    Scalar acc_val = aVal[k];
                    Ordinal lptr, uptr;
                    for ( lptr = lRowMap[i], uptr = uRowMap[aColIdx[k]] ;
                            lptr < lRowMap[i+1] && uptr < uRowMap[aColIdx[k]+1] ; )
                    {
                        if (lColIdx[lptr] == uColIdx[uptr])
                        {
                            acc_val -= lVal[lptr] * uVal[uptr];
                            lptr++;
                            uptr++;
                        }
                        else if (lColIdx[lptr] < uColIdx[uptr])
                            lptr++;
                        else
                            uptr++;
                    }
                    sum += acc_val * acc_val;
                }
            }

            for (int i = 0; i < nRows; i++)
            {
                sum_diag += diagElems[i]*diagElems[i];
            }

            std::cout << "l2 norm of nonlinear residual = " << RTS::sqrt(STS::abs(sum)) << std::endl;
            std::cout << "l2 norm of diag. of U = " << RTS::sqrt(STS::abs(sum_diag)) << std::endl;
        }

        void checkIC() const
        {
            //Compute the L2 norm of the nonlinear residual (A - LLt) on sparsity pattern
            //
            Scalar sum = 0.0;
            for (int i = 0 ; i < nRows; i++)
            {
                int row = i;
                // Each row in A matrix (result)
                for (int k = aRowMap[i]; k < aRowMap[i+1]; k++)
                {

                    int col = aColIdx[k];

                    if (row >= col) {
                        Scalar acc_val = aVal[k];
                        Ordinal lptr, uptr;
                        for ( lptr = lRowMap[i], uptr = lRowMap[aColIdx[k]] ;
                                lptr < lRowMap[i+1] && uptr < lRowMap[aColIdx[k]+1] ; )
                        {
                            if (lColIdx[lptr] == lColIdx[uptr])
                            {
                                acc_val -= lVal[lptr] * lVal[uptr];
                                lptr++;
                                uptr++;
                            }
                            else if (lColIdx[lptr] < lColIdx[uptr])
                                lptr++;
                            else
                                uptr++;
                        }

                        sum += acc_val * acc_val;
                    }
                }
            }
            FASTILU_DBG_COUT("l2 norm of nonlinear residual = " << std::sqrt(sum));
        }
        friend class FastILUFunctor<Ordinal, Scalar, ExecSpace>;
        friend class FastICFunctor<Ordinal, Scalar, ExecSpace>;
        friend class JacobiIterFunctor<Ordinal, Scalar, ExecSpace>;
        friend class ParCopyFunctor<Ordinal, Scalar, ExecSpace>;
        friend class JacobiIterFunctorT<Ordinal, Scalar, ExecSpace>;
        friend class ParScalFunctor<Ordinal, Scalar, Real, ExecSpace>;
        friend class PermScalFunctor<Ordinal, Scalar, Real, ExecSpace>;
        friend class MemoryPrimeFunctorN<Ordinal, Scalar, ExecSpace>;
        friend class MemoryPrimeFunctorNnzCoo<Ordinal, Scalar, ExecSpace>;
        friend class MemoryPrimeFunctorNnzCsr<Ordinal, Scalar, ExecSpace>;
};

//TODO: find a way to avoid the if condition (only store lower triangular part of A?)
template<class Ordinal, class Scalar, class ExecSpace>
class FastICFunctor
{
    public:
        typedef ExecSpace execution_space;
        typedef Kokkos::View<Ordinal *, ExecSpace> ordinal_array_type;
        typedef Kokkos::View<Scalar *, ExecSpace> scalar_array_type;

        using STS = Kokkos::ArithTraits<Scalar>;

        FastICFunctor (Ordinal nNZ, Ordinal bs, ordinal_array_type Ap, ordinal_array_type Ai,
                ordinal_array_type Aj, scalar_array_type Ax, ordinal_array_type Lp,
                ordinal_array_type Li, scalar_array_type Lx, scalar_array_type diag, Scalar omega)
            :
                nnz(nNZ), blk_size(bs), _Ap(Ap), _Ai(Ai), _Aj(Aj),  _Lp(Lp), _Li(Li), _Ax(Ax), _Lx(Lx), _diag(diag), _omega(omega)
        {}

        KOKKOS_INLINE_FUNCTION
            void operator()(const Ordinal blk_index) const
            {
                Ordinal start = blk_index * blk_size;
                Ordinal end = start + blk_size;

                Ordinal nz_index;

                if (end > nnz)
                {
                    end = nnz;
                }

                for (nz_index = start; nz_index < end && nz_index < nnz; nz_index++)
                {
                    Ordinal i = _Ai[nz_index];
                    Ordinal j = _Aj[nz_index];
                    //Ordinal temp = i;

                    Scalar val = _Ax[nz_index];
                    Scalar acc_val = 0.0;
                    Ordinal lptr = _Lp[i];
                    Ordinal ltptr = _Lp[j];
                    Ordinal endpt = j;
                    if (i >= j) {

                        for ( ; _Li[lptr] < endpt && _Li[ltptr] < endpt; )
                        {
                            if (_Li[lptr] == _Li[ltptr])
                            {
                                acc_val += _Lx[lptr] * _Lx[ltptr];
                                lptr++;
                                ltptr++;
                            }
                            else if (_Li[lptr] < _Li[ltptr])
                            {
                                lptr++;
                            }
                            else
                            {
                                ltptr++;
                            }
                        }
                        if (i > j)
                        {
                            val = (val-acc_val) / _diag[j];
                            for ( ; _Li[lptr] < j ; lptr++) ; // dummy loop
                            assert (_Li[lptr] == j);
                            _Lx[lptr] = ((1.0 - _omega)*_Lx[lptr]) + (_omega*val);
                        }
                        else if (i == j)
                        {
                            //_diag[j] =  std::sqrt(val - acc_val);
                            val = STS::sqrt(val - acc_val);
                            _diag[j] = ((1.0 - _omega) * _diag[j]) + (_omega*val);
                            for ( ; _Li[lptr] < j ; lptr++) ; // dummy loop
                            assert(_Li[lptr]==i);
                            _Lx[lptr] = _diag[j];
                        }
                    }
                }
            }

        Ordinal nnz, blk_size;
        ordinal_array_type _Ap, _Ai, _Aj, _Lp, _Li;
        scalar_array_type _Ax, _Lx, _diag;
        Scalar _omega;
};

template<class Ordinal, class Scalar, class ExecSpace>
class MemoryPrimeFunctorNnzCsr
{
    public:
        typedef ExecSpace execution_space;
        typedef Kokkos::View<Ordinal *, ExecSpace> ordinal_array_type;
        typedef Kokkos::View<Scalar *, ExecSpace> scalar_array_type;

        MemoryPrimeFunctorNnzCsr (ordinal_array_type Ai,
                scalar_array_type Ax)
            :
                _Ai(Ai),  _Ax(Ax)
        {}

        KOKKOS_INLINE_FUNCTION
            void operator()(const Ordinal index) const
            {
                _Ai[index];
                _Ax[index];
            }

        ordinal_array_type _Ai;
        scalar_array_type _Ax;
};

template<class Ordinal, class Scalar, class ExecSpace>
class MemoryPrimeFunctorNnzCoo
{
    public:
        typedef ExecSpace execution_space;
        typedef Kokkos::View<Ordinal *, ExecSpace> ordinal_array_type;
        typedef Kokkos::View<Scalar *, ExecSpace> scalar_array_type;

        MemoryPrimeFunctorNnzCoo (ordinal_array_type Ai,
                ordinal_array_type Aj,
                scalar_array_type Ax)
            :
                _Ai(Ai), _Aj(Aj), _Ax(Ax)
        {}

        KOKKOS_INLINE_FUNCTION
            void operator()(const Ordinal index) const
            {
                /*
                Ordinal v1, v2;
                Scalar v3;
                */

                _Ai[index];
                _Aj[index];
                _Ax[index];
            }

        ordinal_array_type _Ai, _Aj;
        scalar_array_type _Ax;
};

template<class Ordinal, class Scalar, class ExecSpace>
class MemoryPrimeFunctorN
{
    public:
        typedef ExecSpace execution_space;
        typedef Kokkos::View<Ordinal *, ExecSpace> ordinal_array_type;
        typedef Kokkos::View<Scalar *, ExecSpace> scalar_array_type;

        MemoryPrimeFunctorN (ordinal_array_type Ap,
                ordinal_array_type Lp,
                ordinal_array_type Up,
                scalar_array_type diag)
            :
                _Ap(Ap), _Lp(Lp), _Up(Up),
                 _diag(diag)
        {}

        KOKKOS_INLINE_FUNCTION
            void operator()(const Ordinal index) const
            {
                //bmk: fix unused warnings?
                //does this functor actually have any side effects, or is it just incomplete?
                /*
                Ordinal v1, v2, v3;
                Scalar v4;
                */

                _Ap[index];
                _Lp[index];
                _Up[index];
                _diag[index];
            }

        ordinal_array_type _Ap, _Lp, _Up;
        scalar_array_type _diag;
};

template<class Ordinal, class Scalar, class ExecSpace>
class FastILUFunctor
{
    public:
        typedef ExecSpace execution_space;
        typedef Kokkos::View<Ordinal *, ExecSpace> ordinal_array_type;
        typedef Kokkos::View<Scalar *, ExecSpace> scalar_array_type;

        using STS = Kokkos::ArithTraits<Scalar>;

        FastILUFunctor (Ordinal nNZ, Ordinal bs,
                ordinal_array_type Ap, ordinal_array_type Ai, ordinal_array_type Aj, scalar_array_type Ax,
                ordinal_array_type Lp, ordinal_array_type Li, scalar_array_type Lx,
                ordinal_array_type Up, ordinal_array_type Ui, scalar_array_type Ux,
                scalar_array_type diag, Scalar omega, Ordinal blockCrsSize)
            :
                nnz(nNZ), blk_size(bs), _Ap(Ap), _Ai(Ai), _Aj(Aj),  _Lp(Lp), _Li(Li),_Up(Up),
                _Ui(Ui), _Ax(Ax), _Lx(Lx), _Ux(Ux), _diag(diag), _omega(omega), _blockCrsSize(blockCrsSize)
        {
              std::cout << "JGF starting a new Functor" << std::endl;
              auto unc_a = decompress_matrix(Ap, Aj, Ax, blockCrsSize);
              auto unc_l = decompress_matrix(Lp, Li, Lx, blockCrsSize);
              auto unc_u = decompress_matrix(Up, Ui, Ux, blockCrsSize);

              std::cout << "JGF A" << std::endl;
              print_matrix(unc_a);
              std::cout << "JGF L" << std::endl;
              print_matrix(unc_l);
              std::cout << "JGF U" << std::endl;
              print_matrix(unc_u);
              print_view("JGF Diag elems diag", diag);
        }

        KOKKOS_INLINE_FUNCTION
        void operator()(const Ordinal blk_index) const
        {
          Ordinal start = blk_index * blk_size;
          Ordinal end = start + blk_size;
          end = (end > nnz) ? nnz : end;
          if (_blockCrsSize == 1) {
            functor_impl(start, end);
          }
          else {
            functor_bcrs_impl(start, end);
          }
        }

        KOKKOS_INLINE_FUNCTION
        void functor_impl(const Ordinal start, const Ordinal end) const
        {
              static const Scalar zero = STS::zero();
              static const Scalar one = STS::one();

                for (Ordinal nz_index = start; nz_index < end && nz_index < nnz; nz_index++)
                {
                    Ordinal i = _Ai[nz_index];
                    Ordinal j = _Aj[nz_index];
                    Ordinal lCol;
                    Ordinal uCol;
                    Scalar val = _Ax[nz_index];
                    Scalar acc_val = zero;
                    Scalar lAdd = zero;
                    Ordinal lptr = _Lp[i];
                    Ordinal uptr = _Up[j];

                    const bool lower = i > j;
                    std::cout << "JGF1 A(" << i << ")(" << j << ")" << (lower ? "lower" : "upper") << std::endl;

                    int diag_matches = 0;
                    int non_diag_matches = 0;
                    while ( lptr < _Lp[i+1] && uptr < _Up[j+1] )
                    {
                        lCol = _Li[lptr];
                        uCol = _Ui[uptr];
                        lAdd = zero;
                        if (lCol == uCol)
                        {
                          std::cout << "    JGF1 L(" << i << ")(" << lCol << ")="<< _Lx[lptr] << " U(" << j << ")(" << uCol << ")=" << _Ux[uptr] << std::endl;

                            lAdd = _Lx[lptr] * _Ux[uptr];
                            acc_val += lAdd;
                            if (lCol == i || uCol == j) {
              //std::cout << "    JGF1 DIAG MATCH " << lCol << " " << lAdd << std::endl;
                              ++diag_matches;
                            }
                            else {
              //std::cout << "    JGF1 MATCH " << lCol << " " << lAdd << std::endl;
                              ++non_diag_matches;
                              std::cout << "    JGF1 ACCUM " << lCol << " " << lAdd << std::endl;
                            }
                        }
                        if (lCol <= uCol)
                        {
                            lptr++;
                        }
                        if (lCol >= uCol)
                        {
                            uptr++;
                        }
                    }

                    acc_val -= lAdd;

                    std::cout << "  JGF1 for nnz=" << nz_index << " lptr=" << lptr << " uptr=" << uptr << " acc_val=" << acc_val << " urowend=" << _Ux[_Up[j+1]-1] << std::endl;

                    assert(diag_matches == 1);
                    // Place the value into L or U
                    if (i > j)
                    {
                        lCol = _Li[lptr-1];
                        assert(lCol == j);
                        val = (val-acc_val) / _Ux[_Up[j+1]-1];
                        _Lx[lptr-1] = ((one - _omega) * _Lx[lptr-1]) + (_omega * val);
                        std::cout << "    JGF1 setting Lx(" << lptr-1 << ") row=" << i << " col="  << lCol << " " << _Lx[lptr-1] << std::endl;
                    }
                    else
                    {
                        uCol = _Ui[uptr-1];
                        val = (val-acc_val);
                        assert(uCol == i);
                        if (i == j) {
                          _diag[j] = val;
                          std::cout << "    JGF1 setting diag(" << j << ") = " << val << std::endl;
                        }
                        _Ux[uptr-1] = ((one - _omega) * _Ux[uptr - 1]) + (_omega * val);
                        std::cout << "    JGF1 setting Ux(" << uptr-1 << ") row=" << j << " col="  << uCol <<" " << _Ux[uptr-1] << std::endl;
                    }
                }
        }

        KOKKOS_INLINE_FUNCTION
        void functor_bcrs_impl(const Ordinal start, const Ordinal end) const
        {
              static const Scalar zero = STS::zero();
              static const Scalar one = STS::one();

           const Ordinal blockItems = _blockCrsSize*_blockCrsSize;
           for (Ordinal nz_index = start; nz_index < end && nz_index < nnz; nz_index++) {
              Ordinal i = _Ai[nz_index]; // row of this nnz block in A
              Ordinal j = _Aj[nz_index]; // col of this nnz block in A

              const Ordinal a_offset = blockItems*nz_index;
              // A[i][j] has non-zero entries
              for (Ordinal bi = 0; bi < _blockCrsSize; ++bi) {
                for (Ordinal bj = 0; bj < _blockCrsSize; ++bj) {
                  const Ordinal blockOffset = _blockCrsSize*bi + bj;
                  Scalar val = _Ax[a_offset + blockOffset];
                  if (val != zero) {
                    Scalar acc_val = zero;
                    Ordinal lptr = _Lp[i]; // lptr= curr col of row i
                    Ordinal uptr = _Up[j]; // uptr= curr col of row j
                    const Ordinal i_unblock = unblock(i, bi, _blockCrsSize);
                    const Ordinal j_unblock = unblock(j, bj, _blockCrsSize);

                    const bool lower = i_unblock > j_unblock;
                    std::cout << "JGF2 A(" << i_unblock << ")(" << j_unblock << ")" << (lower ? "lower" : "upper") << std::endl;

                    // Iterate over bi row of L
                    // Iterate over bj row of U
                    while ( lptr < _Lp[i+1] && uptr < _Up[j+1] )
                    {
                      Ordinal lCol = _Li[lptr];
                      Ordinal uCol = _Ui[uptr];
                      if (lCol == uCol) {
                        const Ordinal l_offset = blockItems*lptr;
                        const Ordinal u_offset  = blockItems*uptr;
                        for (Ordinal bjj = 0; bjj < _blockCrsSize; ++bjj) {
                          const Ordinal blockOffsetL = _blockCrsSize*bi + bjj;
                          const Ordinal blockOffsetU = _blockCrsSize*bj + bjj;
                          const Scalar lVal = _Lx[l_offset + blockOffsetL];
                          const Scalar uVal = _Ux[u_offset + blockOffsetU];
                          const Ordinal lCol_unblock = unblock(lCol, bjj, _blockCrsSize);
                          const Ordinal uCol_unblock = unblock(uCol, bjj, _blockCrsSize);

                          const bool diag_item = (lCol_unblock == i_unblock || uCol_unblock == j_unblock);
                          if (lVal != zero && uVal != zero && !diag_item) {
                            std::cout << "    JGF2 L(" << i_unblock << ")(" << lCol_unblock << ") U(" << j_unblock << ")(" << uCol_unblock << ")" << std::endl;
                            const Scalar curr_val = lVal * uVal;
                            acc_val += curr_val;
                            std::cout << "      JGF2 ACCUM " << curr_val << std::endl;
                          }
                        }
                      }
                      if (lCol <= uCol)
                      {
                        lptr++;
                      }
                      if (lCol >= uCol)
                      {
                        uptr++;
                      }
                    }

                    // The last item in the row of U will always be the diagonal
                    Scalar lastU = _diag[j*_blockCrsSize + bj];

                    std::cout << "  JGF2 for nnz=" << nz_index << " lptr=" << lptr << " uptr=" << uptr << " acc_val=" << acc_val << " urowend=" << lastU << std::endl;

                    // Place the value into L or U
                    Ordinal lCol = _Ui[lptr-1];
                    Ordinal uCol = _Ui[uptr-1];
                    const Ordinal lCol_unblock = unblock(lCol, bj, _blockCrsSize);
                    const Ordinal uCol_unblock = unblock(uCol, bj, _blockCrsSize);
                    const Ordinal l_offset = blockItems*(lptr-1);
                    const Ordinal u_offset = blockItems*(uptr-1);
                    const Ordinal blockOffset = _blockCrsSize*bi + bj;
                    const Ordinal blockOffsetT = _blockCrsSize*bj + bi;
                    if ( (i == j && bi > bj) || i > j) {
                      val = (val-acc_val) / lastU;
                      _Lx[l_offset + blockOffset] = ((one - _omega) * _Lx[l_offset + blockOffset]) + (_omega * val);
                      std::cout << "      JGF2 setting Lx(" << l_offset + blockOffset << ") row=" << i_unblock << " col="  << lCol_unblock << " " << _Lx[l_offset + blockOffset] << std::endl;
                    }
                    else {
                      val = (val-acc_val);
                      if (i == j && bi == bj) {
                        _diag[j*_blockCrsSize + bj] = val;
                        std::cout << "      JGF2 setting diag(" << j*_blockCrsSize + bj << ") = " << val << std::endl;
                      }
                      _Ux[u_offset + blockOffsetT] = ((one - _omega) * _Ux[u_offset + blockOffsetT]) + (_omega * val);
                      std::cout << "      JGF2 setting Ux(" << u_offset + blockOffsetT << ") row=" << j_unblock << " col="  << uCol_unblock << " " << _Ux[u_offset + blockOffsetT] << std::endl;
                    }
                  }
                }
              }
            }
        }

        Ordinal nnz, blk_size;
        ordinal_array_type _Ap, _Ai, _Aj, _Lp, _Li, _Up, _Ui;
        scalar_array_type _Ax, _Lx, _Ux, _diag;
        Scalar _omega;
        const Ordinal _blockCrsSize;
};


template<class Ordinal, class Scalar, class ExecSpace>
class BlockJacobiIterFunctorL
{
    public:
        typedef ExecSpace ESpace;
        typedef Kokkos::View<Ordinal*, ExecSpace> OrdinalArray;
        typedef Kokkos::View<Scalar *, ExecSpace> ScalarArray;

        BlockJacobiIterFunctorL (Ordinal n, Ordinal bs, OrdinalArray aI, OrdinalArray aJ,
                ScalarArray aVal, ScalarArray b, ScalarArray xNew,
                ScalarArray xOld, ScalarArray diag)
            :
                nRow(n), blkSize(bs), aRPtr(aI), aColIdx(aJ), aVal2(aVal), rhs(b), x2(xNew), x1(xOld),
                diagElems(diag)
        {}

        KOKKOS_INLINE_FUNCTION
            void operator()(const Ordinal blkID) const
            {
                Ordinal idx1 = blkID * blkSize;
                Ordinal idx2 = idx1 + blkSize;
                Scalar val;
                Ordinal row;
                Ordinal col;
                Ordinal k;

                if (idx2 > nRow)
                {
                    idx2 = nRow;
                }

                for (row = idx1; row < idx2; row++)
                {
                    val = 0.0;
                    val = rhs[row];
                    for (k = aRPtr[row]; k < aRPtr[row+1]; k++)
                    {
                        col = aColIdx[k];
                        if (col >= idx1 && col < row)
                        {
                            val -= aVal2[k]*x2[col];
                        }
                        else if (col < idx1 || col > row)
                        {
                            val -= aVal2[k]*x1[col];
                        }
                    }
                    x2[row] = val/diagElems[row];
                }
            }
        Ordinal nRow;
        Ordinal blkSize;
        OrdinalArray aRPtr, aColIdx;
        ScalarArray aVal2, rhs, x2, x1, diagElems;
};


template<class Ordinal, class Scalar, class ExecSpace>
class BlockJacobiIterFunctorU
{
    public:
        typedef ExecSpace ESpace;
        typedef Kokkos::View<Ordinal*, ExecSpace> OrdinalArray;
        typedef Kokkos::View<Scalar *, ExecSpace> ScalarArray;

        BlockJacobiIterFunctorU (Ordinal n, Ordinal bs, OrdinalArray aI, OrdinalArray aJ,
                ScalarArray aVal, ScalarArray b, ScalarArray xNew,
                ScalarArray xOld, ScalarArray diag)
            :
                nRow(n), blkSize(bs), aRPtr(aI), aColIdx(aJ), aVal2(aVal), rhs(b), x2(xNew), x1(xOld),
                diagElems(diag)
        {}

        KOKKOS_INLINE_FUNCTION
            void operator()(const Ordinal blkID) const
            {
                Ordinal idx1 = blkID * blkSize;
                Ordinal idx2 = idx1 + blkSize;
                Scalar val;
                Ordinal row;
                Ordinal col;
                Ordinal k;

                if (idx2 > nRow)
                {
                    idx2 = nRow;
                }

                for (row = idx2 - 1; row >= idx1; row--)
                {
                    val = 0.0;
                    val = rhs[row];
                    for (k = aRPtr[row]; k < aRPtr[row+1]; k++)
                    {
                        col = aColIdx[k];
                        if (col < idx2 && col > row)
                        {
                            val -= aVal2[k]*x2[col];
                        }
                        else if (col >= idx2 || col < row)
                        {
                            val -= aVal2[k]*x1[col];
                        }
                    }
                    x2[row] = val/diagElems[row];
                }
            }
        Ordinal nRow;
        Ordinal blkSize;
        OrdinalArray aRPtr, aColIdx;
        ScalarArray aVal2, rhs, x2, x1, diagElems;
};

template<class Ordinal, class Scalar, class ExecSpace>
class JacobiIterFunctor
{
    public:
        typedef ExecSpace execution_space;
        typedef Kokkos::View<Ordinal *, ExecSpace> ordinal_array_type;
        typedef Kokkos::View<Scalar *, ExecSpace> scalar_array_type;

        JacobiIterFunctor (Ordinal n, ordinal_array_type aI, ordinal_array_type aJ,
                scalar_array_type aVal, scalar_array_type b, scalar_array_type xNew,
                scalar_array_type xOld, scalar_array_type diag)
            :
                aI_(aI), aJ_(aJ), aVal_(aVal), b_(b), xNew_(xNew), xOld_(xOld), diag_(diag)
        {}

        KOKKOS_INLINE_FUNCTION
            void operator()(const Ordinal xId) const
            {
                Scalar rowDot = 0.0;
                Ordinal k;

                //The equation is x_{k+1} = D^{-1}b + (I - D^{-1}A)x_{k}
                //The individual updates are x^{k+1}_{i} = b_{i}/d_{i} + x^{k}_{i} -
                // \sum_{j = 1}^{n} r_{ij} x_{j}^{k}
                xNew_[xId] = b_[xId]/diag_[xId];
                xNew_[xId] += xOld_[xId];

                for (k = aI_[xId]; k < aI_[xId+1]; k++)
                {
                    rowDot += aVal_[k]*xOld_[aJ_[k]];
                }
                xNew_[xId] -= rowDot/diag_[xId];
            }

        ordinal_array_type aI_, aJ_;
        scalar_array_type aVal_, b_, xNew_, xOld_, diag_;
};


//Parallel copy operation
template<class Ordinal, class Scalar, class ExecSpace>
class ParCopyFunctor
{
    public:
        typedef ExecSpace execution_space;
        typedef Kokkos::View<Scalar *, ExecSpace> scalar_array_type;

        ParCopyFunctor (scalar_array_type xDestination, scalar_array_type xSource)
            :
                xDestination_(xDestination), xSource_(xSource)
        {}

        KOKKOS_INLINE_FUNCTION
            void operator()(const Ordinal xId) const
            {
                xDestination_[xId] = xSource_[xId];
            }

        scalar_array_type xDestination_, xSource_;
};

//Parallel copy operation with permutation
template<class Ordinal, class Scalar, class ExecSpace>
class ParPermCopyFunctor
{
    public:
        typedef ExecSpace execution_space;
        typedef Kokkos::View<Ordinal *, ExecSpace> ordinal_array_type;
        typedef Kokkos::View<Scalar *, ExecSpace> scalar_array_type;
        using parent = FastILUPrec<Ordinal, Scalar, ExecSpace>;

        ParPermCopyFunctor (ordinal_array_type a2uMap, scalar_array_type aVal, ordinal_array_type aRowIdx,
                scalar_array_type uVal, ordinal_array_type uColIdx, const Ordinal blockCrsSize)
            :
          a2uMap_(a2uMap), aVal_(aVal), aRowIdx_(aRowIdx), uVal_(uVal), uColIdx_(uColIdx), blockCrsSize_(blockCrsSize)
        {}

        static constexpr auto upper_lamb = [](const Ordinal i, const Ordinal j) { return i <= j; };

        KOKKOS_INLINE_FUNCTION
            void operator()(const Ordinal k) const
            {
                auto pos = a2uMap_(k);
                parent::assign_block_cond_trans(uVal_, aVal_, k, pos, upper_lamb, blockCrsSize_);
                uColIdx_(k) = aRowIdx_[pos];
            }

        ordinal_array_type a2uMap_;
        scalar_array_type  aVal_;
        ordinal_array_type aRowIdx_;
        scalar_array_type  uVal_;
        ordinal_array_type uColIdx_;
        const Ordinal      blockCrsSize_;
};


template<class Ordinal, class Scalar, class ExecSpace>
class JacobiIterFunctorT
{
    public:
        typedef ExecSpace execution_space;
        typedef Kokkos::View<Ordinal *, ExecSpace> ordinal_array_type;
        typedef Kokkos::View<Scalar *, ExecSpace> scalar_array_type;

        JacobiIterFunctorT (Ordinal n, ordinal_array_type aI, ordinal_array_type aJ,
                scalar_array_type aVal, scalar_array_type b, scalar_array_type xNew,
                scalar_array_type xOld, scalar_array_type diag)
            :
                aI_(aI), aJ_(aJ), aVal_(aVal), b_(b), xNew_(xNew), xOld_(xOld), diag_(diag), n_(n)
        {}

        KOKKOS_INLINE_FUNCTION
            void operator()(const Ordinal xId) const
            {
                Ordinal k;

               // xNew_[xId] += b_[xId]/diag_[xId];
                //xNew_[xId] += xOld_[xId];

                Kokkos::atomic_add(&xNew_[xId], b_[xId]/diag_[xId]);
                Kokkos::atomic_add(&xNew_[xId], xOld_[xId]);

                for (k = aI_[xId]; k < aI_[xId+1]; k++)
                {

                    //y[aJ_[k]] += (aVal_[k]*xOld_[aJ_[k]])/diag_[aJ_[k]];
                    Kokkos::atomic_add(&xNew_[aJ_[k]], -(aVal_[k]*xOld_[xId])/diag_[aJ_[k]]);
                }
            }

        ordinal_array_type aI_, aJ_;
        scalar_array_type aVal_, b_, xNew_, xOld_, diag_;
        Ordinal n_;
};

template<class Ordinal, class Scalar, class Real, class ExecSpace>
class ParScalFunctor
{
    public:
        typedef ExecSpace execution_space;
        typedef Kokkos::View<Scalar *, ExecSpace> scalar_array_type;
        typedef Kokkos::View<Real *, ExecSpace> real_array_type;

        ParScalFunctor (scalar_array_type x, scalar_array_type y, real_array_type scaleFactors)
            :
                x_(x), y_(y), scaleFactors_(scaleFactors)
        {
        }


        KOKKOS_INLINE_FUNCTION
            void operator()(const Ordinal xId) const
            {
               y_[xId] = x_[xId]*scaleFactors_[xId];
            }

        scalar_array_type x_, y_;
        real_array_type scaleFactors_;
};


template<class Ordinal, class Scalar, class Real, class ExecSpace>
class PermScalFunctor
{
    public:
        typedef ExecSpace execution_space;
        typedef Kokkos::View<Ordinal *, ExecSpace> ordinal_array_type;
        typedef Kokkos::View<Scalar *, ExecSpace> scalar_array_type;
        typedef Kokkos::View<Real *, ExecSpace> real_array_type;

        PermScalFunctor (scalar_array_type x, scalar_array_type y, real_array_type scaleFactors, ordinal_array_type perm)
            :
                x_(x), y_(y), scaleFactors_(scaleFactors), perm_(perm)
        {
        }

        KOKKOS_INLINE_FUNCTION
            void operator()(const NonTranPermScalTag &, const Ordinal xId) const
            {
               // y = D*P*x
               Ordinal row = perm_(xId);
               y_[xId] = scaleFactors_[xId]*x_[row];
            }

        KOKKOS_INLINE_FUNCTION
            void operator()(const TranPermScalTag &, const Ordinal xId) const
            {
               // y = P'*D*x
               Ordinal row = perm_(xId);
               y_[xId] = x_[row]*scaleFactors_[row];
            }

        scalar_array_type x_, y_;
        real_array_type scaleFactors_;
        ordinal_array_type perm_;
};


template<class Ordinal, class Scalar, class ExecSpace>
class ParInitZeroFunctor
{
    public:
        typedef ExecSpace execution_space;
        typedef Kokkos::View<Scalar *, ExecSpace> scalar_array_type;

        ParInitZeroFunctor(scalar_array_type x)
            :
                x_(x)
    {
    }


        KOKKOS_INLINE_FUNCTION
            void operator()(const Ordinal xId) const
            {
                x_[xId] = 0.0;
            }

        scalar_array_type x_ ;
};

#undef FASTILU_CREATE_TIMER
#undef FASTILU_REPORT_TIMER
#undef FASTILU_FENCE_REPORT_TIMER
#undef FASTILU_DBG_COUT

#endif
