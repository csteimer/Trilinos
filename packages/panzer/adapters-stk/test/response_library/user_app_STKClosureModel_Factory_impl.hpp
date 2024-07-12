// @HEADER
// *****************************************************************************
//           Panzer: A partial differential equation assembly
//       engine for strongly coupled complex multiphysics systems
//
// Copyright 2011 NTESS and the Panzer contributors.
// SPDX-License-Identifier: BSD-3-Clause
// *****************************************************************************
// @HEADER

#ifndef USER_APP_STK_CLOSURE_MODEL_FACTORY_T_HPP
#define USER_APP_STK_CLOSURE_MODEL_FACTORY_T_HPP

#include <iostream>
#include <sstream>
#include "Panzer_Constant.hpp"

#include "Teuchos_ParameterEntry.hpp"
#include "Teuchos_TypeNameTraits.hpp"

// User application evaluators for this factory
#include "TestEvaluators.hpp"

// ********************************************************************
// ********************************************************************
template<typename EvalT>
Teuchos::RCP< std::vector< Teuchos::RCP<PHX::Evaluator<panzer::Traits> > > > 
user_app::STKModelFactory<EvalT>::
buildClosureModels(const std::string& model_id,
		   const Teuchos::ParameterList& models,  
		   const panzer::FieldLayoutLibrary& /* fl */,
		   const Teuchos::RCP<panzer::IntegrationRule>& ir,
		   const Teuchos::ParameterList& /* default_params */,
		   const Teuchos::ParameterList& user_data,
		   const Teuchos::RCP<panzer::GlobalData>& /* global_data */,
		   PHX::FieldManager<panzer::Traits>& /* fm */) const
{

  using std::string;
  using std::vector;
  using Teuchos::RCP;
  using Teuchos::rcp;
  using Teuchos::ParameterList;
  using PHX::Evaluator;

  RCP< vector< RCP<Evaluator<panzer::Traits> > > > evaluators = 
    rcp(new vector< RCP<Evaluator<panzer::Traits> > > );

  const ParameterList& my_models = models.sublist(model_id);

  for (ParameterList::ConstIterator model_it = my_models.begin(); 
       model_it != my_models.end(); ++model_it) {
    const std::string key = model_it->first;
    const Teuchos::ParameterEntry& entry = model_it->second;
    const ParameterList& plist = Teuchos::getValue<Teuchos::ParameterList>(entry);
    if (plist.isType<double>("Value")) {
      Teuchos::ParameterList input;
      input.set("Name", key);
      input.set("Value", plist.get<double>("Value"));
      input.set("Data Layout", ir->dl_scalar);
      RCP< PHX::Evaluator<panzer::Traits> > e = 
        rcp(new panzer::Constant<EvalT,panzer::Traits>(input));
      evaluators->push_back(e);
    }
  }

  {
     RCP<PHX::Evaluator<panzer::Traits> > e
        = rcp(new panzer::TestEvaluator<EvalT,panzer::Traits>(user_data));

     evaluators->push_back(e);
  }


  return evaluators;
}

#endif
