// SOUFFLE: solver factory implementation,using Uno.
//
// Licensed under the NASA Open Source Agreement 1.3, like the rest of EMTG.

#include "NLP_solver_factory.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

#include "problem.h"

// SOUFFLE: each solver is optional. A header is included only when its interface is compiled,
// so a build without SNOPT needs no SNOPT installation and does not import snopt7.dll.
#ifdef SOUFFLE_WITH_SNOPT
#include "SNOPT_interface.h"
#endif

#ifdef SOUFFLE_WITH_UNO
#include "Souffle_interface.h"
#endif

// Ipopt is linked in directly rather than loaded at run time: its import library is a COFF
// archive that cl.exe can link.
#ifdef SOUFFLE_WITH_IPOPT
#include "Ipopt_interface.h"
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

            //An empty name means "use whichever solver this binary was built around".
            //Precedence is SNOPT, then Uno, then Ipopt, so that adding Ipopt to a build cannot
            //change what an existing binary does when SOUFFLE_NLP_SOLVER is unset.
            if (name.empty())
            {
#if defined(SOUFFLE_WITH_SNOPT)
                return std::unique_ptr<NLP_interface>(new SNOPT_interface(myProblem, myOptions));
#elif defined(SOUFFLE_WITH_UNO)
                return std::unique_ptr<NLP_interface>(new Souffle_interface(myProblem, myOptions));
#elif defined(SOUFFLE_WITH_IPOPT)
                return std::unique_ptr<NLP_interface>(new Ipopt_interface(myProblem, myOptions));
#else
                throw std::runtime_error("SOUFFLE: this build has no NLP solver compiled in.");
#endif
            }

            if (name == "snopt")
            {
#ifdef SOUFFLE_WITH_SNOPT
                return std::unique_ptr<NLP_interface>(new SNOPT_interface(myProblem, myOptions));
#else
                throw std::runtime_error("SOUFFLE: no SNOPT in this build "
                    "(rebuild with -DSOUFFLE_WITH_SNOPT=ON).");
#endif
            }

            if (name == "uno")
            {
#ifdef SOUFFLE_WITH_UNO
                return std::unique_ptr<NLP_interface>(new Souffle_interface(myProblem, myOptions));
#else
                throw std::runtime_error("SOUFFLE: no Uno in this build "
                    "(rebuild with -DSOUFFLE_WITH_UNO=ON).");
#endif
            }

            if (name == "ipopt")
            {
#ifdef SOUFFLE_WITH_IPOPT
                return std::unique_ptr<NLP_interface>(new Ipopt_interface(myProblem, myOptions));
#else
                throw std::runtime_error("SOUFFLE: no Ipopt in this build "
                    "(rebuild with -DSOUFFLE_WITH_IPOPT=ON).");
#endif
            }

            //HYBRID (Uno and Ipopt inside one process) was retired: their mingw runtimes carry
            //the same DLL names, and Windows resolves by name, so the second one to load dies.
            //Union search now runs the two solvers as separate processes; see united/united.py.
            if (name == "hybrid")
            {
                throw std::runtime_error("SOUFFLE: HYBRID was retired -- Uno and Ipopt cannot "
                    "share a process. Use united/united.py for union search.");
            }

            throw std::runtime_error("SOUFFLE: unknown NLP solver '" +
                myOptions.get_solver_name() + "' (SOUFFLE_NLP_SOLVER accepts SNOPT, Uno, Ipopt).");
        }
    }//end namespace Solvers
}//end namespace EMTG
