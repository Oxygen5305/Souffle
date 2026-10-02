// SOUFFLE: NLP solver interface backed by Uno.
//
// The Uno counterpart of SNOPT_interface: implements run_NLP by building an Uno model from
// the EMTG problem and driving it through Uno's C API (loaded at run time, see SouffleApi.h).
//
// Scaling contract (same as SNOPT_interface):
//   lower = 0, upper = (Xupper - Xlower) / X_scale_factor
//   X_unscaled = X_scaled * X_scale_factor + Xlowerbounds
//
// Licensed under the NASA Open Source Agreement 1.3, like the rest of EMTG.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "NLP_interface.h"

namespace EMTG
{
    namespace Solvers
    {
        //Uno's C API spells its integer type "uno_int". Declared here as an alias so this
        //header can describe the callback signatures without including the Uno C API.
        typedef int uno_int;
        static_assert(sizeof(uno_int) == sizeof(int),
            "SOUFFLE: uno_int must match the platform int for the callback ABIs to line up");

        class Souffle_interface : public NLP_interface
        {
        public:
            //constructor
            Souffle_interface() : NLP_interface::NLP_interface() {};
            Souffle_interface(problem* myProblem,
                const NLPoptions& myOptions);

            //run NLP
            virtual void run_NLP(const bool& X0_is_scaled = true);

            //SOUFFLE: report solver success in SNOPT-like semantics (see NLP_interface::getInform).
            //Uno's own status codes are translated: anything that means "converged or ran out of
            //budget while still feasible" maps below 10, everything else maps to >= 10.
            int getInform() const override { return this->inform; }

            //Last method description reported by Uno (useful in logs).
            const std::string& get_method_description() const { return this->method_description; }

            //---- Uno C API callbacks ----
            //These are public because they are installed into Uno as plain function pointers and
            //must be reachable from the call sites in Souffle_interface.cpp (including the free-standing
            //SouffleApi function-pointer table). They are static and take the interface through
            //user_data, so they are not part of the class's usable API.
            static uno_int objective_callback(uno_int number_variables, const double* x,
                double* objective_value, void* user_data);
            static uno_int objective_gradient_callback(uno_int number_variables, const double* x,
                double* gradient, void* user_data);
            static uno_int constraints_callback(uno_int number_variables, uno_int number_constraints,
                const double* x, double* constraint_values, void* user_data);
            static uno_int jacobian_callback(uno_int number_variables, uno_int number_jacobian_nonzeros,
                const double* x, double* jacobian_values, void* user_data);

            static void notify_acceptable_iterate_callback(uno_int number_variables,
                uno_int number_constraints, const double* primals,
                const double* lower_bound_multipliers, const double* upper_bound_multipliers,
                const double* constraint_multipliers, double objective_multiplier,
                double primal_feasibility_residual, double stationarity_residual,
                double complementarity_residual, void* user_data);

            static uno_int termination_callback(uno_int number_variables,
                uno_int number_constraints, const double* primals,
                const double* lower_bound_multipliers, const double* upper_bound_multipliers,
                const double* constraint_multipliers, double objective_multiplier,
                double primal_feasibility_residual, double stationarity_residual,
                double complementarity_residual, void* user_data);

        private:
            //---- evaluation ----
            //Makes sure F (and G when needed) are valid at the supplied SCALED iterate.
            //Turns Uno's separate f/g evaluations into at most one EMTG problem evaluation per
            //distinct point, which matters because EMTG's transcription is the expensive part.
            void ensure_evaluated(const double* x_scaled, bool need_gradient);

            //Runs the EMTG problem at the given unscaled point; handles NLPMode::FilamentFinder.
            void evaluate_problem(bool need_gradient);

            //Updates X_scaled/X_unscaled from a scaled iterate without evaluating.
            void set_scaled_iterate(const double* x_scaled);

            //Chaperone bookkeeping, mirroring SNOPT_interface's logic.
            void update_chaperone();

            //---- cached evaluation state ----
            std::vector<double> cached_x_scaled;   //last scaled point evaluated
            bool cache_valid = false;
            bool cache_has_F = false;
            bool cache_has_G = false;

            //---- solver state ----
            int inform = 99;                        //SNOPT-like status code
            bool goal_attained = false;             //set by the termination callback
            std::size_t uno_iterations = 0;
            double uno_cpu_time = 0.0;
            std::string method_description;

            //Counts termination-callback invocations. Used only to emit a bounded amount of
            //diagnostic output (the callback fires once per Uno iteration).
            std::size_t termination_callback_calls = 0;

            //Indices into EMTG's G vector for the entries that belong to constraint rows
            //(iGfun != 0). The Uno model's Jacobian is built from exactly these entries, in this
            //order, so the Jacobian callback can map Uno's compacted values back onto G.
            std::vector<std::size_t> constraint_jacobian_source;
        };//end class Souffle_interface
    }//end namespace Solvers
}//end namespace EMTG
