// @HEADER
// *****************************************************************************
//               Rapid Optimization Library (ROL) Package
//
// Copyright 2014 NTESS and the ROL contributors.
// SPDX-License-Identifier: BSD-3-Clause
// *****************************************************************************
// @HEADER

/*! \file  test_02.cpp
    \brief Test trust region.
*/

#define USE_HESSVEC 1

#include "ROL_GetTestProblems.hpp"
#include "ROL_OptimizationSolver.hpp"
#include "ROL_Stream.hpp"
#include "Teuchos_GlobalMPISession.hpp"


#include <iostream>

typedef double RealT;

int main(int argc, char *argv[]) {

  Teuchos::GlobalMPISession mpiSession(&argc, &argv);

  // This little trick lets us print to std::cout only if a (dummy) command-line argument is provided.
  int iprint     = argc - 1;
  ROL::Ptr<std::ostream> outStream;
  ROL::nullstream bhs; // outputs nothing
  if (iprint > 0)
    outStream = ROL::makePtrFromRef(std::cout);
  else
    outStream = ROL::makePtrFromRef(bhs);

  int errorFlag  = 0;

  // *** Test body.

  try {

    std::string filename = "input.xml";
    
    auto parlist = ROL::getParametersFromXmlFile( filename );
    parlist->sublist("Step").set("Type","Trust Region");

    // Loop Through Test Objectives
    for ( ROL::ETestOptProblem objFunc = ROL::TESTOPTPROBLEM_ROSENBROCK; objFunc < ROL::TESTOPTPROBLEM_LAST; objFunc++ ) {
      for ( ROL::ETrustRegion tr = ROL::TRUSTREGION_CAUCHYPOINT; tr < ROL::TRUSTREGION_LAST; tr++ ) {
        if ( tr != ROL::TRUSTREGION_LINMORE ) {
          // Set Up Optimization Problem
          ROL::Ptr<ROL::Vector<RealT> > x0;
          std::vector<ROL::Ptr<ROL::Vector<RealT> > > z;
          ROL::Ptr<ROL::OptimizationProblem<RealT> > problem;
          ROL::GetTestProblem<RealT>(problem,x0,z,objFunc);

          if (problem->getProblemType() == ROL::TYPE_U
              && objFunc != ROL::TESTOPTPROBLEM_MINIMAX1
              && objFunc != ROL::TESTOPTPROBLEM_MINIMAX2
              && objFunc != ROL::TESTOPTPROBLEM_MINIMAX3) {
            *outStream << std::endl << std::endl << ROL::ETestOptProblemToString(objFunc) << std::endl << std::endl;

            // Get Dimension of Problem
            int dim = x0->dimension();
            parlist->sublist("General").sublist("Krylov").set("Iteration Limit", 5*dim);

            // Error Vector
            ROL::Ptr<ROL::Vector<RealT> > e = x0->clone();
            e->zero();

            *outStream << "\n\n" << ROL::ETrustRegionToString(tr) << "\n\n";
            parlist->sublist("Step").sublist("Trust Region").set("Subproblem Solver", ETrustRegionToString(tr));

            // Define Solver
            ROL::OptimizationSolver<RealT> solver(*problem,*parlist);

            // Run Solver
            solver.solve(*outStream);

            // Compute Error 
            RealT err(0);
            for (int i = 0; i < static_cast<int>(z.size()); ++i) {
              e->set(*x0);
              e->axpy(-1.0,*z[i]);
              if (i == 0) {
                err = e->norm();
              }
              else {
                err = std::min(err,e->norm());
              }
            }
            *outStream << std::endl << "Norm of Error: " << err << std::endl;
          }
        }
      }
    }
  }
  catch (std::logic_error& err) {
    *outStream << err.what() << std::endl;
    errorFlag = -1000;
  }; // end try

  if (errorFlag != 0)
    std::cout << "End Result: TEST FAILED" << std::endl;
  else
    std::cout << "End Result: TEST PASSED" << std::endl;

  return 0;

}
