// EMTG: Evolutionary Mission Trajectory Generator
// An open-source global optimization tool for preliminary mission design
// Provided by NASA Goddard Space Flight Center
//
// Copyright (c) 2013 - 2024 United States Government as represented by the
// Administrator of the National Aeronautics and Space Administration.
// All Other Rights Reserved.

// Licensed under the NASA Open Source License (the "License"); 
// You may not use this file except in compliance with the License. 
// You may obtain a copy of the License at:
// https://opensource.org/licenses/NASA-1.3
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either 
// express or implied.   See the License for the specific language
// governing permissions and limitations under the License.

//NLP options class

#include "NLPoptions.h"

namespace EMTG
{
    namespace Solvers
    {
        namespace
        {
            //Maps the missionoptions NLP_solver_type index onto a solver name. The GUI's
            //dropdown writes this field, so it is what "NLP solver" in the interface means.
            const char* solver_name_for_type(int type)
            {
                switch (type)
                {
                case 1:  return "IPOPT";
                case 2:  return "SNOPT";
                default: return "Uno";
                }
            }
        }//end anonymous namespace

        NLPoptions::NLPoptions() :
            SolverMode(NLPMode::Optimize),
            enable_auto_scale(false),
            enable_NLP_chaperone(false),
            check_derivatives(false),
            quiet_NLP(false),
            print_NLP_movie_frames(false),
            stop_on_goal_attain(false),
            major_iterations_limit(1000),
            minor_iterations_limit(500),
            max_run_time_seconds(3600),
            max_step(1.0),
            feasibility_tolerance(1.0e-5),
            optimality_tolerance(1.0e-6),
            specs_file_path(""),
            output_file_path(""),
#ifndef SOUFFLE_DEFAULT_SOLVER_NAME
            solver_name("SNOPT")
#else
            solver_name(SOUFFLE_DEFAULT_SOLVER_NAME)
#endif
        {
            //SOUFFLE: allow the NLP solver to be selected without touching any EMTG options file.
            //The compiled-in default comes from the CMake option SOUFFLE_DEFAULT_SOLVER; the
            //SOUFFLE_NLP_SOLVER environment variable overrides it at runtime, which is what the
            //packaged launcher uses.
            const char* solver_from_env = std::getenv("SOUFFLE_NLP_SOLVER");
            if (solver_from_env != nullptr && solver_from_env[0] != '\0')
                this->solver_name = solver_from_env;
        }


#ifndef PORTABLE_SOLVER
        NLPoptions::NLPoptions(const missionoptions& options) :
            NLPoptions()
        {
            this->SolverMode = options.NLP_solver_mode;
            this->enable_auto_scale = options.enable_Scalatron;
            this->enable_NLP_chaperone = options.enable_NLP_chaperone;
            this->check_derivatives = options.check_derivatives;
            this->quiet_NLP = options.quiet_NLP;
            this->print_NLP_movie_frames = options.print_NLP_movie_frames;
            this->stop_on_goal_attain = options.NLP_stop_on_goal_attain;
            this->major_iterations_limit = options.snopt_major_iterations;
            this->minor_iterations_limit = options.snopt_minor_iterations;
            this->max_run_time_seconds = options.snopt_max_run_time;
            this->max_step = options.NLP_max_step;
            this->feasibility_tolerance = options.snopt_feasibility_tolerance;
            this->optimality_tolerance = options.snopt_optimality_tolerance;
            this->objective_goal = options.NLP_objective_goal;

            //The case file's NLP solver selection. SOUFFLE_NLP_SOLVER still wins when set, so
            //the launcher and the command line can override whatever the GUI wrote.
            if (std::getenv("SOUFFLE_NLP_SOLVER") == nullptr)
                this->solver_name = solver_name_for_type(options.NLP_solver_type);
        }
#endif
    }//end namespace Solvers
}//end namespace EMTG