// @HEADER
// *****************************************************************************
//               Rapid Optimization Library (ROL) Package
//
// Copyright 2014 NTESS and the ROL contributors.
// SPDX-License-Identifier: BSD-3-Clause
// *****************************************************************************
// @HEADER

#ifndef ROL_TYPEU_ALGORITHM_DEF_H
#define ROL_TYPEU_ALGORITHM_DEF_H

#include "ROL_Types.hpp"
#include "ROL_ReduceLinearConstraint.hpp"
#include "ROL_ValidParameters.hpp"

namespace ROL {
namespace TypeU {

template<typename Real>
Algorithm<Real>::Algorithm()
  : status_(makePtr<CombinedStatusTest<Real>>()),
    state_(makePtr<AlgorithmState<Real>>()) {
  status_->reset();
  status_->add(makePtr<StatusTest<Real>>());
}

template<typename Real>
void Algorithm<Real>::initialize(const Vector<Real> &x, const Vector<Real> &g) {
  if (state_->iterateVec == nullPtr) {
    state_->iterateVec = x.clone();
  }
  state_->iterateVec->set(x);
  if (state_->stepVec == nullPtr) {
    state_->stepVec = x.clone();
  }
  state_->stepVec->zero();
  if (state_->gradientVec == nullPtr) {
    state_->gradientVec = g.clone();
  }
  state_->gradientVec->set(g);
  if (state_->minIterVec == nullPtr) {
    state_->minIterVec = x.clone();
  }
  state_->minIterVec->set(x);
  state_->minIter = state_->iter;
  state_->minValue = state_->value;
}

template<typename Real>
void Algorithm<Real>::setStatusTest(const Ptr<StatusTest<Real>> &status,
                                      bool combineStatus) {
  if (!combineStatus) { // Do not combine status tests
    status_->reset();
  }
  status_->add(status); // Add user-defined StatusTest
}

template<typename Real>
void Algorithm<Real>::run( Problem<Real> &problem,
                           std::ostream  &outStream ) {
  if (problem.getProblemType() == TYPE_U) {
    run(*problem.getPrimalOptimizationVector(),
        *problem.getDualOptimizationVector(),
        *problem.getObjective(),
        outStream);
    problem.finalizeIteration();
  }
  else {
    throw Exception::NotImplemented(">>> ROL::TypeU::Algorithm::run : Optimization problem is not Type U!");
  }
}

template<typename Real>
void Algorithm<Real>::run( Vector<Real>    &x,
                           Objective<Real> &obj,
                           std::ostream    &outStream ) {
  run(x,x.dual(),obj,outStream);
}

template<typename Real>
void Algorithm<Real>::run( Vector<Real>     &x,
                           Objective<Real>  &obj,
                           Constraint<Real> &linear_con,
                           Vector<Real>     &linear_mul,
                           std::ostream     &outStream ) {
  run(x,x.dual(),obj,linear_con,linear_mul,linear_mul.dual(),outStream);
}

template<typename Real>
void Algorithm<Real>::run( Vector<Real>       &x,
                           const Vector<Real> &g,
                           Objective<Real>    &obj,
                           Constraint<Real>   &linear_con,
                           Vector<Real>       &linear_mul,
                           const Vector<Real> &linear_c,
                           std::ostream       &outStream ) {
  Ptr<Vector<Real>> xfeas = x.clone(); xfeas->set(x);
  ReduceLinearConstraint<Real> rlc(makePtrFromRef(linear_con),xfeas,makePtrFromRef(linear_c));
  Ptr<Vector<Real>> s = x.clone(); s->zero();
  
  run(*s,g,*rlc.transform(makePtrFromRef(obj)),outStream);
  rlc.project(x,*s);
  x.plus(*rlc.getFeasibleVector());
}

template<typename Real>
void Algorithm<Real>::writeHeader( std::ostream& os ) const {
  std::ios_base::fmtflags osFlags(os.flags());
  os << "  ";
  os << std::setw(6)  << std::left << "iter";
  os << std::setw(15) << std::left << "value";
  os << std::setw(15) << std::left << "gnorm";
  os << std::setw(15) << std::left << "snorm";
  os << std::setw(10) << std::left << "#fval";
  os << std::setw(10) << std::left << "#grad";
  os << std::endl;
  os.flags(osFlags);
}

template<typename Real>
void Algorithm<Real>::writeName( std::ostream& os ) const {
  throw Exception::NotImplemented(">>> ROL::TypeU::Algorithm::writeName() is not implemented!");
}

template<typename Real>
void Algorithm<Real>::writeOutput( std::ostream& os, bool write_header ) const {
  std::ios_base::fmtflags osFlags(os.flags());
  os << std::scientific << std::setprecision(6);
  if ( write_header ) writeHeader(os);
  if ( state_->iter == 0 ) {
    os << "  ";
    os << std::setw(6)  << std::left << state_->iter;
    os << std::setw(15) << std::left << state_->value;
    os << std::setw(15) << std::left << state_->gnorm;
    os << std::endl;
  }
  else {
    os << "  "; 
    os << std::setw(6)  << std::left << state_->iter;  
    os << std::setw(15) << std::left << state_->value; 
    os << std::setw(15) << std::left << state_->gnorm; 
    os << std::setw(15) << std::left << state_->snorm; 
    os << std::setw(10) << std::left << state_->nfval;              
    os << std::setw(10) << std::left << state_->ngrad;              
    os << std::endl;
  }
  os.flags(osFlags);
}

template<typename Real>
void Algorithm<Real>::writeExitStatus( std::ostream& os ) const {
  std::ios_base::fmtflags osFlags(os.flags());
  os << "Optimization Terminated with Status: ";
  os << EExitStatusToString(state_->statusFlag);
  os << std::endl;
  os.flags(osFlags);
}

template<typename Real>
//Ptr<const AlgorithmState<Real>>& Algorithm<Real>::getState() const {
Ptr<const AlgorithmState<Real>> Algorithm<Real>::getState() const {
  return state_;
}

template<typename Real>
void Algorithm<Real>::reset() {
  state_->reset();
}

} // namespace TypeU
} // namespace ROL

#endif
