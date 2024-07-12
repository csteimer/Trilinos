// @HEADER
// *****************************************************************************
//        MueLu: A package for multigrid based preconditioning
//
// Copyright 2012 NTESS and the MueLu contributors.
// SPDX-License-Identifier: BSD-3-Clause
// *****************************************************************************
// @HEADER

#include "MueLu_FactoryBase.hpp"

namespace MueLu {

int FactoryBase::GenerateUniqueId() {
  static int i = 0;
  return i++;
}

}  // namespace MueLu
