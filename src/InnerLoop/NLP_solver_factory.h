// SOUFFLE: solver factory
//
// EMTG's original code instantiated Solvers::SNOPT_interface directly in three places in
// problem.cpp (MBM/NLP/FilamentWalker inner loops), which hard-wired the global search layer
// to SNOPT. This factory breaks that dependency: callers receive a
// std::unique_ptr<NLP_interface> and never see a concrete solver type.
//
// Which solver is built is decided by NLPoptions::get_solver_name(), which in turn reads the
// SOUFFLE_NLP_SOLVER environment variable (see NLPoptions.cpp). The default is "SNOPT" so that
// a freshly built SOUFFLE behaves exactly like the original EMTG until Uno is explicitly
// requested. This keeps the two solvers comparable from a single binary.
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
        //Builds the NLP solver named by myOptions.get_solver_name().
        //
        //Recognised names (case insensitive):
        //  "SNOPT" (default) - SNOPT via SNOPT_interface; requires the SNOPT library to be linked
        //  "UNO"             - Uno via Souffle_interface;  requires libuno.dll to be locatable at runtime
        //
        //Throws std::runtime_error for an unknown name, or if the requested solver is not
        //available in this build.
        std::unique_ptr<NLP_interface> create_NLP_solver(EMTG::problem* myProblem,
            const NLPoptions& myOptions);
    }//end namespace Solvers
}//end namespace EMTG
