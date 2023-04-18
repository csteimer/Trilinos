// @HEADER
//
// ***********************************************************************
//
//        MueLu: A package for multigrid based preconditioning
//                  Copyright 2012 Sandia Corporation
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
// Questions? Contact
//                    Jonathan Hu       (jhu@sandia.gov)
//                    Andrey Prokopenko (aprokop@sandia.gov)
//                    Ray Tuminaro      (rstumin@sandia.gov)
//
// ***********************************************************************
//
// @HEADER
#ifndef MUELU_COORDINATESTRANSFER_FACTORY_DECL_HPP
#define MUELU_COORDINATESTRANSFER_FACTORY_DECL_HPP

#include "MueLu_ConfigDefs.hpp"
#include "MueLu_TwoLevelFactoryBase.hpp"
#include "Xpetra_MultiVector_fwd.hpp"
#include "Xpetra_MultiVectorFactory_fwd.hpp"

#include "MueLu_Aggregates_fwd.hpp"

#include "MueLu_Utilities_fwd.hpp"

namespace MueLu {

/*!
  @class CoordinatesTransferFactory class.
  @brief Class for transferring coordinates from a finer level to a coarser one

    This is separate from MultiVectorTransferFactory which potentially can be used for scalar problems.
    For non-scalar problems, however, we cannot use restriction operator as that essentially is matrix Q
    from tentative prolongator initialization.

  ## Input/output of CoordinatesTransferFactory ##

  ### User parameters of CoordinatesTransferFactory ###
  Parameter | type | default | master.xml | validated | requested | description
  ----------|------|---------|:----------:|:---------:|:---------:|------------
  | Coordinates| Factory | null |   | * | (*) | Factory providing coordinates
  | Aggregates | Factory | null |   | * | (*) | Factory providing aggregates
  | CoarseMap  | Factory | null |   | * | (*) | Generating factory of the coarse map
  | write start|     int | -1   |   | * |     | first level at which coordinates should be written to file
  | write end  |     int | -1   |   | * |     | last level at which coordinates should be written to file

  The * in the @c master.xml column denotes that the parameter is defined in the @c master.xml file.<br>
  The * in the @c validated column means that the parameter is declared in the list of valid input parameters (see CoordinatesTransferFactory::GetValidParameters).<br>
  The * in the @c requested column states that the data is requested as input with all dependencies (see CoordinatesTransferFactory::DeclareInput).

  The CoordinatesTransferFact first checks whether there is already valid coarse coordinates information
  available on the coarse level. If that is the case, we can skip the coordinate transfer and just reuse
  the available information.
  Otherwise we try to build coarse grid coordinates by using the information about the
  aggregates, the fine level coordinates and the coarse map information.

  ### Variables provided by CoordinatesTransferFactory ###

  After CoordinatesTransferFactory::Build the following data is available (if requested)

  Parameter | generated by | description
  ----------|--------------|------------
  | Coordinates | CoordinatesTransferFactory   | coarse level coordinates
*/
  template<class Scalar = DefaultScalar,
           class LocalOrdinal = DefaultLocalOrdinal,
           class GlobalOrdinal = DefaultGlobalOrdinal,
           class Node = DefaultNode>
  class CoordinatesTransferFactory : public TwoLevelFactoryBase {
    public:
    typedef Scalar                                              scalar_type;
    typedef LocalOrdinal                                        local_ordinal_type;
    typedef GlobalOrdinal                                       global_ordinal_type;
    typedef typename Node::device_type                          DeviceType;
    typedef typename DeviceType::execution_space                execution_space;

  private:
#undef MUELU_COORDINATESTRANSFERFACTORY_SHORT
#include "MueLu_UseShortNames.hpp"

  public:
    //! @name Constructors/Destructors.
    //@{

    /*! @brief Constructor.

       @param vectorName The name of the quantity to be restricted.
       @param restrictionName The name of the restriction Matrix.

       The operator associated with <tt>projectionName</tt> will be applied to the MultiVector associated with
       <tt>vectorName</tt>.
    */
    CoordinatesTransferFactory() { }

    //! Destructor.
    virtual ~CoordinatesTransferFactory() { }

    RCP<const ParameterList> GetValidParameterList() const;

    //@}

    //! @name Input
    //@{

    /*! @brief Specifies the data that this class needs, and the factories that generate that data.

        If the Build method of this class requires some data, but the generating factory is not specified in DeclareInput, then this class
        will fall back to the settings in FactoryManager.
    */
    void DeclareInput(Level &finelevel, Level &coarseLevel) const;

    //@}

    //! @name Build methods.
    //@{

    //! Build an object with this factory.
    void Build(Level & fineLevel, Level &coarseLevel) const;

    //@}

  private:

  }; // class CoordinatesTransferFactory

} // namespace MueLu

#define MUELU_COORDINATESTRANSFERFACTORY_SHORT
#endif // MUELU_COORDINATESTRANSFER_FACTORY_DECL_HPP
