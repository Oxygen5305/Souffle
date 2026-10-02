// SOUFFLE: solver factory implementation.
//
// Licensed under the NASA Open Source Agreement 1.3, like the rest of EMTG.

#include "NLP_solver_factory.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

#include "problem.h"

// SOUFFLE: SNOPT is optional. With SOUFFLE_WITH_SNOPT undefined the SNOPT interface is
// not compiled and its header is not required, so the build needs no SNOPT installation
// and the resulting executable does not import snopt7.dll.
#ifdef SOUFFLE_WITH_SNOPT
#include "SNOPT_interface.h"
#endif

#ifdef SOUFFLE_WITH_UNO
#include "Souffle_interface.h"
#endif

namespace EMTG
{
    namespace Solvers
    {
        namespace
        {
            //Normalise a solver name for case-insensitive comparison.
            std::string to_lower(std::string value)
            {
                std::transform(value.begin(), value.end(), value.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return value;
            }
        }//end anonymous namespace

        std::unique_ptr<NLP_interface> create_NLP_solver(EMTG::problem* myProblem,
            const NLPoptions& myOptions)
        {
            const std::string name = to_lower(myOptions.get_solver_name());

            if (name.empty() || name == "snopt")
            {
#ifdef SOUFFLE_WITH_SNOPT
                //SNOPT build: identical to EMTG's original behaviour.
                return std::unique_ptr<NLP_interface>(new SNOPT_interface(myProblem, myOptions));
#else
                //Default SOUFFLE build: SNOPT is not compiled in. An empty name means
                //"use the default solver", so fall through to Uno; an explicit "snopt"
                //request is an error.
#ifdef SOUFFLE_WITH_UNO
                if (name.empty())
                {
                    return std::unique_ptr<NLP_interface>(new Souffle_interface(myProblem, myOptions));
                }
                throw std::runtime_error(
                    "SOUFFLE: the SNOPT solver was requested but this build was compiled "
                    "without SNOPT (SOUFFLE_WITH_SNOPT=OFF). Use SOUFFLE_NLP_SOLVER=Uno, "
                    "or rebuild with -DSOUFFLE_WITH_SNOPT=ON and a SNOPT installation.");
#else
                throw std::runtime_error(
                    "SOUFFLE: no NLP solver is available. This build has neither SNOPT nor "
                    "Uno compiled in; rebuild with -DSOUFFLE_WITH_UNO=ON.");
#endif
#endif
            }

            if (name == "uno")
            {
#ifdef SOUFFLE_WITH_UNO
                return std::unique_ptr<NLP_interface>(new Souffle_interface(myProblem, myOptions));
#else
                throw std::runtime_error(
                    "SOUFFLE: the Uno solver was requested but this build was compiled without it. "
                    "Rebuild with SOUFFLE_WITH_UNO defined (see src/InnerLoop/CMakeLists.txt).");
#endif
            }

            throw std::runtime_error(
                "SOUFFLE: unknown NLP solver '" + myOptions.get_solver_name() +
                "'. Valid choices are 'SNOPT' and 'Uno' (set via the SOUFFLE_NLP_SOLVER environment variable).");
        }
    }//end namespace Solvers
}//end namespace EMTG
