// SOUFFLE: solver factory.
//
// Callers get a std::unique_ptr<NLP_interface> and never name a concrete solver type.
// The solver comes from NLPoptions::get_solver_name(), i.e. SOUFFLE_NLP_SOLVER.
//
// Licensed under the NASA Open Source Agreement 1.3, like the rest of EMTG.

#pragma once

#include <memory>

#include "NLP_interface.h"
#include "NLPoptions.h"

//forward declaration: avoid pulling problem.h into every translation unit that only needs
//the factory declaration.
namespace EMTG
{
    class problem;
}

namespace EMTG
{
    namespace Solvers
    {
        //Builds the solver named by myOptions.get_solver_name() (case insensitive):
        //"SNOPT" or "UNO". Throws std::runtime_error for an unknown name or one that this
        //build does not contain.
        std::unique_ptr<NLP_interface> create_NLP_solver(EMTG::problem* myProblem,
            const NLPoptions& myOptions);
    }//end namespace Solvers
}//end namespace EMTG
