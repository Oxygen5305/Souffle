// SOUFFLE: NLP solver interface backed by Uno (implementation).
// See Souffle_interface.h for the contract.
//
// Licensed under the NASA Open Source Agreement 1.3, like the rest of EMTG.

#include "Souffle_interface.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include "SouffleApi.h"
#include "problem.h"

namespace
{
    //RAII guards: Uno hands out raw pointers; every exit path must release them.
    struct ModelGuard
    {
        explicit ModelGuard(void* handle) : model(handle) {}
        ~ModelGuard() { if (model != nullptr) EMTG::Solvers::SouffleApi::instance().destroy_model(model); }
        void* model = nullptr;
    private:
        ModelGuard(const ModelGuard&);
        ModelGuard& operator=(const ModelGuard&);
    };

    struct SolverGuard
    {
        explicit SolverGuard(void* handle) : solver(handle) {}
        ~SolverGuard() { if (solver != nullptr) EMTG::Solvers::SouffleApi::instance().destroy_solver(solver); }
        void* solver = nullptr;
    private:
        SolverGuard(const SolverGuard&);
        SolverGuard& operator=(const SolverGuard&);
    };
}//end anonymous namespace

namespace EMTG
{
    namespace Solvers
    {
        Souffle_interface::Souffle_interface(problem* myProblem,
            const NLPoptions& myOptions) :
            NLP_interface::NLP_interface(myProblem, myOptions)
        {
            //Fail fast and loudly if the Uno runtime is not reachable. A clear message here is
            //much better than a mysterious NLP failure later.
            const SouffleApi& api = SouffleApi::instance();
            if (!api.ok())
                throw std::runtime_error(api.error());

            this->cached_x_scaled.assign(this->nX, 0.0);
        }

        //--------------------------------------------------------------------------
        // evaluation plumbing
        //--------------------------------------------------------------------------

        void Souffle_interface::set_scaled_iterate(const double* x_scaled)
        {
            for (std::size_t Xindex = 0; Xindex < this->nX; ++Xindex)
            {
                this->X_scaled[Xindex] = x_scaled[Xindex];
                //Same un-scaling rule as NLP_interface::unscaleX().
                this->X_unscaled[Xindex] = x_scaled[Xindex] * this->myProblem->X_scale_factors[Xindex]
                    + this->myProblem->Xlowerbounds[Xindex];
            }
        }

        void Souffle_interface::evaluate_problem(bool need_gradient)
        {
            if (this->myOptions.get_SolverMode() == NLPMode::FilamentFinder)
            {
                //Filament finding is effectively unconstrained: the objective becomes the sum of
                //squares of the equality constraints. Same construction as SNOPT_interface.
                this->myProblem->evaluate(this->X_unscaled, this->myProblem->F, this->myProblem->G, need_gradient);

                this->F.front() = 0.0;
                for (std::size_t Findex = 1; Findex < this->myProblem->total_number_of_constraints; ++Findex)
                {
                    if (this->myProblem->F_equality_or_inequality[Findex - 1])
                        this->F.front() += this->myProblem->F[Findex] * this->myProblem->F[Findex];
                }

                std::fill(this->G.begin(), this->G.end(), 0.0);

                const std::size_t Problem_nG = this->myProblem->Gdescriptions.size();
                for (std::size_t Gindex = 0; Gindex < Problem_nG && Gindex < this->G.size(); ++Gindex)
                {
                    const std::size_t Findex = this->myProblem->iGfun[Gindex];
                    const std::size_t Xindex = this->myProblem->jGvar[Gindex];

                    if (Findex > 0 && Xindex < this->G.size()
                        && this->myProblem->F_equality_or_inequality[Findex - 1])
                    {
                        this->G[Xindex] += 2.0 * this->myProblem->F[Findex] * this->myProblem->G[Gindex];
                    }
                }
            }
            else
            {
                //Regular problem: EMTG fills F (objective at index 0, then constraints) and the
                //sparse Jacobian G in the order given by iGfun/jGvar.
                this->myProblem->evaluate(this->X_unscaled, this->F, this->G, need_gradient);
            }
        }

        void Souffle_interface::ensure_evaluated(const double* x_scaled, bool need_gradient)
        {
            //Uno calls objective and constraints separately; EMTG computes both at once, so
            //cache the evaluation until the point changes.
            bool same_point = this->cache_valid;
            if (same_point)
            {
                for (std::size_t Xindex = 0; Xindex < this->nX; ++Xindex)
                {
                    if (this->cached_x_scaled[Xindex] != x_scaled[Xindex])
                    {
                        same_point = false;
                        break;
                    }
                }
            }

            const bool have_what_we_need = same_point
                && this->cache_has_F
                && (!need_gradient || this->cache_has_G);

            if (have_what_we_need)
            {
                //Refresh the iterate bookkeeping (cheap) and return the cached values.
                this->set_scaled_iterate(x_scaled);
                return;
            }

            //A gradient request needs G computed; a plain value request can reuse a cached G
            //but must recompute F if we only had the gradient before.
            const bool evaluate_gradient = need_gradient || !this->cache_has_F;

            this->set_scaled_iterate(x_scaled);
            this->evaluate_problem(evaluate_gradient);

            for (std::size_t Xindex = 0; Xindex < this->nX; ++Xindex)
                this->cached_x_scaled[Xindex] = x_scaled[Xindex];

            this->cache_valid = true;
            this->cache_has_F = true;
            this->cache_has_G = evaluate_gradient;
        }

        //--------------------------------------------------------------------------
        // Uno C API callbacks
        //--------------------------------------------------------------------------

        uno_int Souffle_interface::objective_callback(uno_int number_variables, const double* x,
            double* objective_value, void* user_data)
        {
            (void)number_variables;
            Souffle_interface* self = static_cast<Souffle_interface*>(user_data);
            try
            {
                self->ensure_evaluated(x, false);
                self->bank_candidate();               //SOUFFLE FIX (D9): trial-point pooling
                *objective_value = self->F.front();
                return 0;
            }
            catch (const std::exception&)
            {
                std::cerr << "SOUFFLE: objective evaluation failed at the current iterate" << std::endl;
                //A positive return tells Uno the evaluation failed; it will handle the failure
                //instead of us throwing across the C boundary.
                return 1;
            }
        }

        uno_int Souffle_interface::objective_gradient_callback(uno_int number_variables, const double* x,
            double* gradient, void* user_data)
        {
            (void)number_variables;
            Souffle_interface* self = static_cast<Souffle_interface*>(user_data);
            try
            {
                self->ensure_evaluated(x, true);
                self->bank_candidate();               //SOUFFLE FIX (D9): trial-point pooling

                std::fill(gradient, gradient + self->nX, 0.0);
                for (std::size_t Gindex = 0; Gindex < self->nG; ++Gindex)
                {
                    if (self->iGfun[Gindex] == 0)
                        gradient[self->jGvar[Gindex]] += self->G[Gindex];
                }
                //SOUFFLE FIX (D1): the objective gradient was identically zero for
                //objective_type=1, so Uno solved a different problem than SNOPT.
                //EMTG's MinTOF objective keeps its ONLY non-zero derivatives in the *linear*
                //Jacobian A -- MinimizeTimeObjective.cpp fills iAfun=0 / jAvar=<time column> /
                //A=<scale>, and nothing else in the tree writes iAfun. SNOPT is handed that matrix
                //through setA(); Uno has no linear-part API at all, so without the loop below the
                //objective gradient stayed zero and **every feasible point was a KKT point**:
                //Uno "converged" after 2-3 iterations at whatever point MBH supplied (measured:
                //opt_status=0 / FEASIBLE_KKT_POINT after ~0.06 s with J frozen and dv pinned at
                //27.83 km/s instead of reaching its 28 km/s cap).
                for (std::size_t Aindex = 0; Aindex < self->nA; ++Aindex)
                {
                    if (self->iAfun[Aindex] == 0)
                        gradient[self->jAvar[Aindex]] += self->A[Aindex];
                }
                return 0;
            }
            catch (const std::exception&)
            {
                std::cerr << "SOUFFLE: gradient evaluation failed at the current iterate" << std::endl;
                return 1;
            }
        }

        uno_int Souffle_interface::constraints_callback(uno_int number_variables, uno_int number_constraints,
            const double* x, double* constraint_values, void* user_data)
        {
            (void)number_variables;
            (void)number_constraints;
            Souffle_interface* self = static_cast<Souffle_interface*>(user_data);
            try
            {
                //Uno only sees the constraints: EMTG's F[0] is the objective.
                self->ensure_evaluated(x, false);
                const std::size_t count = self->nF - 1;
                //SOUFFLE FIX (D5b) diagnostic: while the scaling path was crashing the solver
                //immediately after the layout probe, this check distinguishes "the factor array does
                //not line up with what Uno asks for" from "the scaled values themselves are rejected".
                if (!self->constraint_scale.empty() && self->constraint_scale.size() != count)
                {
                    std::cerr << "SOUFFLE: constraint scale size mismatch (" << self->constraint_scale.size()
                              << " vs " << count << "); disabling scaling" << std::endl;
                    self->constraint_scale.clear();
                }
                if (self->constraint_scale.empty())
                {
                    for (std::size_t Findex = 0; Findex < count; ++Findex)
                        constraint_values[Findex] = self->F[Findex + 1];
                }
                else
                {
                    //SOUFFLE FIX (D5): same row factors as the bounds, so the feasible set is
                    //unchanged and only the subproblem conditioning improves.
                    for (std::size_t Findex = 0; Findex < count; ++Findex)
                        constraint_values[Findex] = self->F[Findex + 1] * self->constraint_scale[Findex];
                }
                return 0;
            }
            catch (const std::exception&)
            {
                return 1;
            }
        }

        uno_int Souffle_interface::jacobian_callback(uno_int number_variables, uno_int number_jacobian_nonzeros,
            const double* x, double* jacobian_values, void* user_data)
        {
            (void)number_variables;
            (void)number_jacobian_nonzeros;
            Souffle_interface* self = static_cast<Souffle_interface*>(user_data);
            try
            {
                self->ensure_evaluated(x, true);

                //Uno's model only contains constraint rows, and its sparsity was built from
                //constraint_jacobian_source in this exact order, so copy through that mapping.
                //SOUFFLE FIX (D5): each entry additionally carries its row's scaling factor, to stay
                //consistent with the scaled constraint values and bounds.
                const std::size_t count = self->constraint_jacobian_source.size();
                if (self->constraint_scale.empty())
                {
                    for (std::size_t entry = 0; entry < count; ++entry)
                        jacobian_values[entry] = self->G[self->constraint_jacobian_source[entry]];
                }
                else
                {
                    for (std::size_t entry = 0; entry < count; ++entry)
                    {
                        const std::size_t source = self->constraint_jacobian_source[entry];
                        jacobian_values[entry] =
                            self->G[source] * self->constraint_scale[self->iGfun[source] - 1];
                    }
                }
                return 0;
            }
            catch (const std::exception&)
            {
                return 1;
            }
        }

        //--------------------------------------------------------------------------
        // chaperone
        //--------------------------------------------------------------------------

        //SOUFFLE FIX (D9): bank a candidate point using only the values already in hand.
        //
        //Why this exists: SNOPT_interface's chaperone runs on every needG evaluation
        //(SNOPT_interface.cpp:423), so the SNOPT build searches with every line-search trial point
        //available as an incumbent. Under Uno the accepted-iterate callback fires roughly once per
        //solve -- measured on a 2198-iteration / 2857-evaluation 10-year solve it banked exactly ONE
        //point (the final iterate). That asymmetry is a strong candidate for the residual gap.
        //
        //Why it is not just a call to update_chaperone(): that function calls
        //myProblem->check_feasibility(), which re-enters the problem evaluation. Running it from an
        //Uno callback made EMTG die during the layout probe. Here the point's feasibility is judged
        //from F and the bounds already sitting in the cache, so nothing re-enters.
        //
        //Inert unless SOUFFLE_TRIAL_INCUMBENTS=1, so existing results stay reproducible.
        void Souffle_interface::bank_candidate()
        {
            //Opt-in switch, resolved once. Keeps the default path byte-for-byte reproducible.
            static const bool enabled = []()
            {
                const char* value = std::getenv("SOUFFLE_TRIAL_INCUMBENTS");
                return value && *value == '1';
            }();
            if (!enabled)
                return;

            if (this->nF < 2 || this->F.size() < this->nF)
                return;

            //Worst constraint violation, in EMTG's own units. No extra normalisation: EMTG's
            //Flowerbounds/Fupperbounds already carry the feasibility tolerance, so "inside the
            //bounds" *is* the feasibility test -- an earlier version divided by the row magnitude,
            //which was too permissive and let infeasible trial points into the incumbent pool
            //(observed: MBH accepted one and then failed with no feasible solution).
            double worst_violation = 0.0;
            for (std::size_t Findex = 1; Findex < this->nF; ++Findex)
            {
                const double value = this->F[Findex];
                const double lower = this->Flowerbounds[Findex];
                const double upper = this->Fupperbounds[Findex];
                if (value < lower)
                    worst_violation = std::max(worst_violation, lower - value);
                else if (value > upper)
                    worst_violation = std::max(worst_violation, value - upper);
            }

            const bool feasible_enough = (worst_violation <= 0.0);
            //Minimal-footprint policy: record only the point, and only when it is strictly better.
            //Deliberately NOT touching EMTG's feasibility bookkeeping variables
            //(feasibility_metric_NLP_incumbent / normalized_feasibility_metric / ...): writing those
            //from here desynchronised the chaperone state machine and made MBH fail with no feasible
            //solution (measured twice, at 80 s and at 5 s).
            if (!feasible_enough || this->F.front() >= this->J_NLP_incumbent)
                return;

            this->X_NLP_incumbent_scaled = this->X_scaled;
            this->X_NLP_incumbent_unscaled = this->X_unscaled;
            this->F_NLP_incumbent = this->F;
            this->G_NLP_incumbent = this->G;
            this->J_NLP_incumbent = this->F.front();
            this->newBestIncumbent = true;
        }

        void Souffle_interface::update_chaperone()
        {
            double normalized_feasibility = 0.0;
            double decision_variable_feasibility_metric = 0.0;

            try
            {
                this->myProblem->check_feasibility(this->X_unscaled,
                    this->F,
                    this->worst_decision_variable,
                    this->worst_constraint,
                    this->feasibility_metric,
                    normalized_feasibility,
                    this->distance_from_equality_filament,
                    decision_variable_feasibility_metric,
                    true);
            }
            catch (const std::runtime_error& error)
            {
                if (!this->myOptions.get_quiet_NLP())
                    std::cout << error.what() << std::endl;
                return;
            }

            this->decision_vector_feasibility_metric = decision_variable_feasibility_metric;
            this->normalized_feasibility_metric = normalized_feasibility;
            this->feasibility_metric = std::max(normalized_feasibility, decision_variable_feasibility_metric);

            //Keep the most feasible (and, among equally feasible, the best) point seen so far.
            const bool better_feasibility = this->feasibility_metric < this->feasibility_metric_NLP_incumbent;
            const bool feasible_enough = this->feasibility_metric < this->myOptions.get_feasibility_tolerance();
            const bool better_objective = feasible_enough
                && this->feasibility_metric_NLP_incumbent < this->myOptions.get_feasibility_tolerance()
                && this->F.front() < this->J_NLP_incumbent;

            if (better_feasibility || better_objective)
            {
                this->X_NLP_incumbent_scaled = this->X_scaled;
                this->X_NLP_incumbent_unscaled = this->X_unscaled;
                this->F_NLP_incumbent = this->F;
                this->G_NLP_incumbent = this->G;
                this->feasibility_metric_NLP_incumbent = this->feasibility_metric;
                this->J_NLP_incumbent = this->F.front();
                this->newBestIncumbent = true;
            }
        }

        void Souffle_interface::notify_acceptable_iterate_callback(uno_int number_variables,
            uno_int number_constraints, const double* primals,
            const double* lower_bound_multipliers, const double* upper_bound_multipliers,
            const double* constraint_multipliers, double objective_multiplier,
            double primal_feasibility_residual, double stationarity_residual,
            double complementarity_residual, void* user_data)
        {
            (void)number_variables; (void)number_constraints;
            (void)lower_bound_multipliers; (void)upper_bound_multipliers;
            (void)constraint_multipliers; (void)objective_multiplier;
            (void)primal_feasibility_residual; (void)stationarity_residual;
            (void)complementarity_residual;

            Souffle_interface* self = static_cast<Souffle_interface*>(user_data);
            if (self == nullptr)
                return;

            try
            {
                self->ensure_evaluated(primals, false);

                if (self->myOptions.get_enable_NLP_chaperone())
                    self->update_chaperone();
            }
            catch (const std::exception&)
            {
                //Do not let an exception escape into Uno's call stack.
            }
        }

        uno_int Souffle_interface::termination_callback(uno_int number_variables,
            uno_int number_constraints, const double* primals,
            const double* lower_bound_multipliers, const double* upper_bound_multipliers,
            const double* constraint_multipliers, double objective_multiplier,
            double primal_feasibility_residual, double stationarity_residual,
            double complementarity_residual, void* user_data)
        {
            (void)number_variables; (void)number_constraints;
            (void)lower_bound_multipliers; (void)upper_bound_multipliers;
            (void)constraint_multipliers; (void)objective_multiplier;
            (void)stationarity_residual; (void)complementarity_residual;

            Souffle_interface* self = static_cast<Souffle_interface*>(user_data);
            if (self == nullptr)
                return 0;

            try
            {
                //Hard wall-clock limit, matching NLPoptions::max_run_time_seconds.
                //Not registered; see the set_solver_callbacks call site.
                const time_t elapsed = time(NULL) - self->NLP_start_time;
                if (elapsed >= static_cast<time_t>(self->myOptions.get_max_run_time_seconds()))
                {
                    if (!self->myOptions.get_quiet_NLP())
                        std::cout << "Uno: wall-clock limit reached, terminating NLP." << std::endl;
                    return 1;
                }

                //Early stop when the goal is attained, matching SNOPT's *Status = -2 behaviour.
                if (self->myOptions.get_stop_on_goal_attain())
                {
                    self->ensure_evaluated(primals, false);

                    double normalized_feasibility = 0.0;
                    double decision_variable_feasibility_metric = 0.0;
                    self->myProblem->check_feasibility(self->X_unscaled,
                        self->F,
                        self->worst_decision_variable,
                        self->worst_constraint,
                        self->feasibility_metric,
                        normalized_feasibility,
                        self->distance_from_equality_filament,
                        decision_variable_feasibility_metric,
                        true);

                    if (std::max(normalized_feasibility, decision_variable_feasibility_metric)
                            < self->myOptions.get_feasibility_tolerance()
                        && self->myProblem->getUnscaledObjective() < self->myOptions.get_objective_goal())
                    {
                        if (!self->myOptions.get_quiet_NLP())
                            std::cout << "NLP goal satisfied, exiting NLP" << std::endl;
                        self->goal_attained = true;
                        return 1;
                    }
                }
            }
            catch (const std::exception&)
            {
                return 0;
            }

            (void)primal_feasibility_residual;
            return 0;
        }

        //--------------------------------------------------------------------------
        // main entry point
        //--------------------------------------------------------------------------

        void Souffle_interface::run_NLP(const bool& X0_is_scaled)
        {
            if (!SouffleApi::instance().ok())
                throw std::runtime_error(SouffleApi::instance().error());

            const SouffleApi& api = SouffleApi::instance();

            //---- initial guess, in scaled variables ----
            if (!X0_is_scaled)
                this->scaleX0();
            else
                this->unscaleX0();

            this->X_scaled = this->X0_scaled;
            this->cache_valid = false;
            this->cache_has_F = false;
            this->cache_has_G = false;
            this->goal_attained = false;
            this->inform = 99;
            this->solution_status = -1;
            this->newBestIncumbent = false;
            this->feasibility_metric_NLP_incumbent = 1.0e+101;
            this->J_NLP_incumbent = math::LARGE;
            this->NLP_start_time = time(NULL);
            this->mostRecentNLPWriteTime = this->NLP_start_time;
            this->movie_frame_count = 0;

            //Sanity check: evaluate once so that failures surface with a useful message rather
            //than as an Uno "evaluation error".
            this->set_scaled_iterate(this->X0_scaled.data());
            this->evaluate_problem(false);
            this->cache_valid = true;
            this->cache_has_F = true;
            this->cache_has_G = false;

            //---- scaled variable bounds (identical convention to SNOPT_interface) ----
            std::vector<double> x_lower(this->nX, 0.0);
            std::vector<double> x_upper(this->nX, 0.0);
            for (std::size_t Xindex = 0; Xindex < this->nX; ++Xindex)
            {
                x_lower[Xindex] = 0.0;
                x_upper[Xindex] = (this->myProblem->Xupperbounds[Xindex]
                    - this->myProblem->Xlowerbounds[Xindex])
                    / this->myProblem->X_scale_factors[Xindex];
            }

            //---- constraint bounds: drop EMTG's objective row (F[0]) ----
            const std::size_t number_constraints = (this->nF > 0) ? (this->nF - 1) : 0;
            std::vector<double> c_lower(number_constraints, 0.0);
            std::vector<double> c_upper(number_constraints, 0.0);
            for (std::size_t Findex = 0; Findex < number_constraints; ++Findex)
            {
                c_lower[Findex] = this->Flowerbounds[Findex + 1];
                c_upper[Findex] = this->Fupperbounds[Findex + 1];
            }

            //---- SOUFFLE FIX (D5): per-row constraint scaling ----
            //Uno applies no automatic scaling under filtersqp, so EMTG's mixed-unit rows reach the
            //QP unscaled. Normalise each row by the largest of (|c_i(x0)|, |lower|, |upper|) so that
            //every row enters the subproblem at order 1. The same factor is applied to the bounds
            //and to that row's Jacobian entries, which leaves the feasible set unchanged -- only the
            //conditioning of the subproblems, and the meaning of the (now aligned) tolerances.
            //Disable with SOUFFLE_CONSTRAINT_SCALING=0 for A/B runs.
            {
                //NOTE: default OFF. An earlier attempt enabled this by default and EMTG died during
                //the layout probe (no XFfile, ~4 s) on the 10-year case, so the implementation still
                //needs debugging before it can be trusted. It is left in place, switched off, because
                //the mechanism it targets (unscaled rows -> "Small radius" collapse) is still the best
                //explanation for the 83% abandoned solves. Enable with SOUFFLE_CONSTRAINT_SCALING=1.
                const char* flag = std::getenv("SOUFFLE_CONSTRAINT_SCALING");
                const bool enable = (flag && *flag == '1');
                if (enable && number_constraints)
                {
                    this->constraint_scale.assign(number_constraints, 1.0);
                    std::size_t scaled_rows = 0;
                    for (std::size_t row = 0; row < number_constraints; ++row)
                    {
                        //Rows EMTG marks as effectively unbounded carry huge bounds; normalising them
                        //would produce a meaningless factor (and was the likely cause of an earlier
                        //version dying during the layout probe), so leave those rows alone.
                        const double bound_magnitude = std::max(std::fabs(c_lower[row]),
                                                               std::fabs(c_upper[row]));
                        if (bound_magnitude > 1.0e10)
                            continue;

                        const double magnitude = std::max(
                            std::fabs(this->F[row + 1]),
                            std::max(std::fabs(c_lower[row]), std::fabs(c_upper[row])));
                        //Rows that are identically zero (and unconstrained) stay untouched.
                        if (magnitude > 1.0e-8)
                        {
                            //Clamp the factor so one pathological row cannot distort the whole problem.
                            double factor = 1.0 / magnitude;
                            factor = std::min(std::max(factor, 1.0e-8), 1.0e8);
                            if (factor != 1.0)
                            {
                                this->constraint_scale[row] = factor;
                                ++scaled_rows;
                            }
                        }
                        c_lower[row] *= this->constraint_scale[row];
                        c_upper[row] *= this->constraint_scale[row];
                    }
                    if (!this->myOptions.get_quiet_NLP())
                    {
                        //Diagnostic: a factor range spanning many orders of magnitude would itself be a
                        //conditioning problem, and printing it costs nothing.
                        double factor_min = 1.0e300;
                        double factor_max = 0.0;
                        for (std::size_t row = 0; row < number_constraints; ++row)
                        {
                            factor_min = std::min(factor_min, this->constraint_scale[row]);
                            factor_max = std::max(factor_max, this->constraint_scale[row]);
                        }
                        std::cout << "SOUFFLE: constraint scaling ON (" << scaled_rows << " of "
                                  << number_constraints << " rows normalised, factor range ["
                                  << factor_min << ", " << factor_max << "])" << std::endl;
                    }
                }
                else if (!this->myOptions.get_quiet_NLP())
                {
                    std::cout << "SOUFFLE: constraint scaling OFF" << std::endl;
                }
            }

            //---- Jacobian sparsity in COO form, 0-based, constraints re-indexed ----
            std::vector<uno_int> jacobian_row;
            std::vector<uno_int> jacobian_column;
            jacobian_row.reserve(this->nG);
            jacobian_column.reserve(this->nG);

            std::vector<std::size_t> constraint_jacobian_source;
            constraint_jacobian_source.reserve(this->nG);
            for (std::size_t Gindex = 0; Gindex < this->nG; ++Gindex)
            {
                const std::size_t Findex = this->iGfun[Gindex];
                const std::size_t Xindex = this->jGvar[Gindex];
                if (Findex == 0)
                    continue;   //objective row: handled by the objective gradient callback
                jacobian_row.push_back(static_cast<uno_int>(Findex - 1));
                jacobian_column.push_back(static_cast<uno_int>(Xindex));
                constraint_jacobian_source.push_back(Gindex);
            }

            //---- build the Uno model ----
            //Report size and preset, so an external log shows what the interface saw.
            std::cout << "SOUFFLE: building model '" << this->myProblem->options.mission_name
                      << "' with " << this->nX << " variables, " << number_constraints
                      << " constraints, " << jacobian_row.size()
                      << " Jacobian nonzeros" << std::endl;

            void* raw_model = api.create_model(UNO_PROBLEM_NONLINEAR,
                static_cast<uno_int>(this->nX), x_lower.data(), x_upper.data(),
                UNO_ZERO_BASED_INDEXING);
            if (raw_model == nullptr)
                throw std::runtime_error("SOUFFLE: uno_create_model failed.");
            ModelGuard model(raw_model);

            api.set_model_name(model.model, this->myProblem->options.mission_name.c_str());

            //Objective: EMTG already presents a minimize-sense objective at F[0].
            if (!api.set_objective(model.model, UNO_MINIMIZE,
                    &Souffle_interface::objective_callback,
                    &Souffle_interface::objective_gradient_callback))
                throw std::runtime_error("SOUFFLE: uno_set_objective failed.");

            //Constraints: EMTG's F[1..nF-1]. The Jacobian callback passes this->G through
            //unchanged, so the model sparsity must be exactly the constraint rows of G, in
            //order; constraint_jacobian_source maps Uno's compacted values back onto G.
            this->constraint_jacobian_source = constraint_jacobian_source;

            if (number_constraints > 0)
            {
                if (!api.set_constraints(model.model,
                        static_cast<uno_int>(number_constraints),
                        &Souffle_interface::constraints_callback,
                        c_lower.data(), c_upper.data(),
                        static_cast<uno_int>(this->constraint_jacobian_source.size()),
                        jacobian_row.data(), jacobian_column.data(),
                        &Souffle_interface::jacobian_callback))
                    throw std::runtime_error("SOUFFLE: uno_set_constraints failed.");
            }

            api.set_initial_primal_iterate(model.model, this->X0_scaled.data());
            api.set_user_data(model.model, this);

            //---- solver configuration ----
            void* raw_solver = api.create_solver();
            if (raw_solver == nullptr)
                throw std::runtime_error("SOUFFLE: uno_create_solver failed.");
            SolverGuard solver(raw_solver);

            //Preset: filtersqp (default) or ipopt. Set SOUFFLE_UNO_PRESET to override.
            std::string uno_preset = "filtersqp";
            if (const char* preset_from_env = std::getenv("SOUFFLE_UNO_PRESET"))
            {
                if (preset_from_env[0] != '\0')
                    uno_preset = preset_from_env;
            }

            if (!api.set_solver_preset(solver.solver, uno_preset.c_str()))
            {
                std::cout << "SOUFFLE: warning - Uno rejected preset '" << uno_preset
                          << "'; falling back to filtersqp" << std::endl;
                uno_preset = "filtersqp";
                api.set_solver_preset(solver.solver, uno_preset.c_str());
            }
            if (!this->myOptions.get_quiet_NLP())
                std::cout << "SOUFFLE: using Uno preset '" << uno_preset << "'" << std::endl;

            api.set_solver_integer_option(solver.solver, "max_iterations",
                static_cast<uno_int>(2 * this->myOptions.get_major_iterations_limit()));
            api.set_solver_double_option(solver.solver, "time_limit",
                static_cast<double>(this->myOptions.get_max_run_time_seconds()));
            api.set_solver_double_option(solver.solver, "primal_tolerance",
                this->myOptions.get_feasibility_tolerance());
            //Uno's dual_tolerance governs stationarity AND complementarity. EMTG's
            //snopt_optimality_tolerance (1e-5 in the EVVEU case) is a SNOPT-tuned value.
            //SOUFFLE FIX (D4): the original code clamped it up to 1e-4, i.e. SOUFFLE was asked to
            //stop 10x further from stationarity than SNOPT. On top of that the filtersqp preset
            //switches the residual norm to L2 (SNOPT uses max-norm -- roughly sqrt(207) = 14x
            //looser for a uniformly spread residual) and divides stationarity by a
            //multiplier-norm-relative factor that SNOPT has no analogue for. Together those made a
            //certified SOUFFLE answer much less stationary than the SNOPT answer it is compared
            //against. Honour the requested tolerance and restore the max-norm test.
            //Cost: solves need more iterations and will hit the time limit more often, so raise
            //the per-solve/MBH budget when measuring.
            //
            //SOUFFLE_D4_MODE=loose restores the pre-D4 behaviour (the 1e-4 clamp plus filtersqp's
            //default L2 residual norm) so the cost of this fix can be measured rather than guessed.
            //Default stays "strict", i.e. existing results are unchanged.
            {
                const char* d4_mode = std::getenv("SOUFFLE_D4_MODE");
                const bool d4_loose = (d4_mode && std::strcmp(d4_mode, "loose") == 0);
                const double optimality = this->myOptions.get_optimality_tolerance();
                if (d4_loose)
                {
                    api.set_solver_double_option(solver.solver, "dual_tolerance",
                        std::max(optimality, 1.0e-4));
                }
                else
                {
                    api.set_solver_double_option(solver.solver, "dual_tolerance", optimality);
                    api.set_solver_string_option(solver.solver, "residual_norm", "INF");
                    api.set_solver_double_option(solver.solver, "residual_scaling_threshold", 1.0e100);
                }
                if (!this->myOptions.get_quiet_NLP())
                    std::cout << "SOUFFLE: D4 mode = " << (d4_loose ? "loose" : "strict") << std::endl;
            }
            api.set_solver_bool_option(solver.solver, "print_solution",
                !this->myOptions.get_quiet_NLP());

            //Uno's default logger level is INFO, which prints one line per violated constraint on
            //every iteration. On an EMTG problem (hundreds of constraints, thousands of iterations)
            //that produces tens of megabytes of stdout and dominates the runtime - measured on the
            //EVVEU case: a 22 MB log for a single phase. Silence it and report through EMTG's own
            //chaperone instead.
            //
            //Note: the value must be upper case. Uno accepts
            //SILENT / DISCRETE / WARNING / INFO / DEBUG / DEBUG2 / DEBUG3.
            //SOUFFLE FIX (D12): keep SILENT by default, but allow SOUFFLE_LOG_LEVEL to override it
            //for one diagnostic run. The hard-coded SILENT hid the `Status <exception.what()>` line
            //that Uno prints when it abandons a solve with UNO_ALGORITHMIC_ERROR -- the single most
            //useful clue for why 83% of the mass-objective solves never converge.
            {
                const char* level = std::getenv("SOUFFLE_LOG_LEVEL");
                api.set_solver_string_option(solver.solver, "logger",
                    (level && *level) ? level : "SILENT");
            }

            //SOUFFLE FIX (D5): trust-region / penalty knobs, settable from the environment so they
            //can be swept without rebuilding. Motivation: in the 10-year EVVEU case 83% of the
            //mass-objective solves ended in UNO_ALGORITHMIC_ERROR, and the INFO log shows why --
            //the trust-region radius decays to ~1e-4 and then reports "Small radius" while the
            //stationarity residual is still ~1.7e-3 (vs the 1e-5 asked for). filtersqp receives
            //EMTG's raw km/kg/s model with no scaling, so ill-conditioned subproblems are the
            //likely cause; these hooks let us test that hypothesis from the outside.
            //   SOUFFLE_TR_RADIUS     (default 1.0)   initial trust-region radius
            //   SOUFFLE_TR_MIN_RADIUS (default 1e-12) radius below which the solve gives up
            //   SOUFFLE_L1_COEFF      l1_constraint_violation_coefficient (restoration penalty)
            //
            // The last four entries target the two things SNOPT does that filtersqp does not,
            // which is the likely reason SNOPT settles in seconds where a Uno solve takes ~100 s:
            //   SOUFFLE_PROGRESS_NORM    how convergence is measured (Uno defaults to L1; SNOPT
            //                            uses the max norm).  Never exercised in this project.
            //   SOUFFLE_RELAX_STRATEGY   how infeasible iterates are handled -- the analogue of
            //                            SNOPT's elastic bounds, which is why SNOPT can start from
            //                            an infeasible point and still make progress.
            //   SOUFFLE_ARMIJO_TOL       line-search acceptance.
            //   SOUFFLE_QN_MEMORY        quasi-Newton history depth (Hessian approximation quality).
            {
                struct { const char* env; const char* option; char kind; } hooks[] = {
                    {"SOUFFLE_TR_RADIUS",      "TR_radius",                           'd'},
                    {"SOUFFLE_TR_MIN_RADIUS",  "TR_min_radius",                       'd'},
                    {"SOUFFLE_L1_COEFF",       "l1_constraint_violation_coefficient", 'd'},
                    {"SOUFFLE_PRIMAL_TOL",     "primal_tolerance",                    'd'},
                    {"SOUFFLE_MAX_ITER",       "max_iterations",                      'i'},
                    {"SOUFFLE_ARMIJO_TOL",     "armijo_tolerance",                    'd'},
                    {"SOUFFLE_ARMIJO_FRAC",    "armijo_decrease_fraction",            'd'},
                    {"SOUFFLE_QN_MEMORY",      "quasi_newton_memory_size",            'i'},
                    {"SOUFFLE_PROGRESS_NORM",  "progress_norm",                       's'},
                    {"SOUFFLE_RELAX_STRATEGY", "constraint_relaxation_strategy",      's'},
                    {"SOUFFLE_HESSIAN_MODEL",  "hessian_model",                       's'},
                    //Scaling: the single biggest structural difference from SNOPT, which scales
                    //rows and columns automatically. Uno honours use_function_scaling only on the
                    //interior-point path, so this is expected to be inert under filtersqp -- the
                    //hook exists to prove or disprove that rather than assume it.
                    {"SOUFFLE_USE_SCALING",    "use_function_scaling",                'b'},
                    {"SOUFFLE_SCALE_THRESH",   "function_scaling_threshold",          'd'},
                    //The "loose" acceptance path. Its defaults are incoherent: loose_dual_tolerance
                    //defaults to 1e-6 while dual_tolerance is set to 1e-5 below, i.e. the loose path
                    //is strictly tighter than the tight one and can never engage.
                    {"SOUFFLE_LOOSE_PRIMAL",   "loose_primal_tolerance",              'd'},
                    {"SOUFFLE_LOOSE_DUAL",     "loose_dual_tolerance",                'd'},
                    {"SOUFFLE_LOOSE_ITER",     "loose_tolerance_iteration_threshold", 'i'},
                    //Phase-switching and filter shape.
                    {"SOUFFLE_SWITCHING_DELTA","switching_delta",                     'd'},
                    {"SOUFFLE_FILTER_TYPE",    "filter_type",                         's'},
                    {"SOUFFLE_FUNNEL_STRATEGY","funnel_update_strategy",              's'},
                };
                //The full campaign runs thousands of solves; one echo line per solve per option is
                //pure noise there. SOUFFLE_RELAX_QUIET=1 silences only these echo lines.
                const char* relax_quiet_env = std::getenv("SOUFFLE_RELAX_QUIET");
                const bool echo_options = !(relax_quiet_env && *relax_quiet_env == '1');
                for (const auto& hook : hooks)
                {
                    const char* value = std::getenv(hook.env);
                    if (!value || !*value)
                        continue;
                    if (hook.kind == 'i')
                        api.set_solver_integer_option(solver.solver, hook.option,
                            static_cast<uno_int>(std::atoi(value)));
                    else if (hook.kind == 's')
                        api.set_solver_string_option(solver.solver, hook.option, value);
                    else if (hook.kind == 'b')
                        api.set_solver_bool_option(solver.solver, hook.option,
                            std::atoi(value) != 0);
                    else
                        api.set_solver_double_option(solver.solver, hook.option, std::atof(value));
                    if (!this->myOptions.get_quiet_NLP() && echo_options)
                        std::cout << "SOUFFLE: " << hook.option << " = " << value
                                  << " (from " << hook.env << ")" << std::endl;
                }

                //SOUFFLE FIX (D6): the filtersqp preset runs a feasibility-restoration phase in which
                //the objective multiplier is 0, and it only switches back to the optimality phase
                //once the *linearised* constraint violation (an L2 norm over mixed-unit rows) drops
                //below primal_tolerance. The INFO log of a failing 10-year solve shows the algorithm
                //oscillating OPT <-> FEAS until the trust region collapses ("Small radius"), i.e. it
                //never gets back to optimising. Relaxing this gate is the direct lever; it is opt-in
                //because the gate is also the classic filter-SQP safeguard.
                {
                    const char* gate = std::getenv("SOUFFLE_SWITCH_GATE");
                    if (gate && *gate)
                    {
                        const bool allow = (std::atoi(gate) != 0);
                        api.set_solver_bool_option(solver.solver,
                            "switch_to_optimality_requires_linearized_feasibility", allow);
                        if (!this->myOptions.get_quiet_NLP())
                            std::cout << "SOUFFLE: switch_to_optimality_requires_linearized_feasibility"
                                      << " = " << (allow ? "true" : "false") << std::endl;
                    }
                }
            }

            //NOTE (SOUFFLE): the termination callback is not installed.
            //
            //Originally we registered both callbacks. Diagnosis showed that with it installed,
            //every Uno solve came back as opt_status=5 (UNO_USER_TERMINATION) with iters=1, i.e.
            //Uno stopped after a single iteration, even though the callback's own diagnostics
            //showed elapsed=0 against a 60 s limit and it never returned non-zero. The wall-clock
            //budget is therefore delegated entirely to Uno's own "time_limit" option (set above
            //from NLPoptions::max_run_time_seconds), and the early-stop-on-goal behaviour is left
            //out until the callback protocol is understood properly.
            //
            //The acceptable-iterate callback is still installed: it is what maintains EMTG's
            //incumbent bookkeeping, which the chaperone logic depends on.
            api.set_solver_callbacks(solver.solver,
                &Souffle_interface::notify_acceptable_iterate_callback,
                nullptr,
                this);

            //---- solve ----
            api.optimize(solver.solver, model.model);

            //---- collect the solution ----
            const uno_int optimization_status = api.get_optimization_status(solver.solver);
            const uno_int solution_status = api.get_solution_status(solver.solver);
            this->solution_status = static_cast<int>(solution_status);
            this->solution_primal_feasibility = api.get_solution_primal_feasibility(solver.solver);
            this->solution_stationarity = api.get_solution_stationarity(solver.solver);

            std::vector<double> x_solution(this->nX, 0.0);
            api.get_primal_solution(solver.solver, x_solution.data());

            if (this->goal_attained || optimization_status == UNO_SUCCESS
                || optimization_status == UNO_ITERATION_LIMIT
                || optimization_status == UNO_TIME_LIMIT
                || optimization_status == UNO_USER_TERMINATION)
            {
                this->inform = 1;   //SNOPT-like "success or ran out of budget"
            }
            else
            {
                this->inform = 10 + static_cast<int>(optimization_status);
            }

            this->uno_iterations = static_cast<std::size_t>(api.get_number_iterations(solver.solver));
            this->uno_cpu_time = api.get_cpu_time(solver.solver);
            const char* description = api.get_method_description(solver.solver);
            this->method_description = (description != nullptr) ? description : "";

            this->set_scaled_iterate(x_solution.data());
            this->evaluate_problem(false);

            //---- feasibility assessment, same as SNOPT_interface ----
            this->myProblem->check_feasibility(this->X_unscaled,
                this->F,
                this->worst_decision_variable,
                this->worst_constraint,
                this->feasibility_metric,
                this->normalized_feasibility_metric,
                this->distance_from_equality_filament,
                this->decision_vector_feasibility_metric);

            const double worst_feasibility = std::max(this->normalized_feasibility_metric,
                this->decision_vector_feasibility_metric);

            //---- adopt the incumbent when it is better than where Uno stopped ----
            if (this->myOptions.get_enable_NLP_chaperone())
            {
                this->unscaleX_NLP_incumbent();

                if (worst_feasibility < this->myOptions.get_feasibility_tolerance()
                    && this->feasibility_metric_NLP_incumbent < this->myOptions.get_feasibility_tolerance())
                {
                    if (this->J_NLP_incumbent < this->F.front())
                    {
                        this->X_unscaled = this->X_NLP_incumbent_unscaled;
                        this->X_scaled = this->X_NLP_incumbent_scaled;
                        this->F = this->F_NLP_incumbent;
                    }
                }
                else if (this->feasibility_metric_NLP_incumbent < worst_feasibility
                    && this->feasibility_metric_NLP_incumbent < this->myOptions.get_feasibility_tolerance())
                {
                    this->X_unscaled = this->X_NLP_incumbent_unscaled;
                    this->X_scaled = this->X_NLP_incumbent_scaled;
                    this->F = this->F_NLP_incumbent;
                }
                else if (this->feasibility_metric_NLP_incumbent < worst_feasibility)
                {
                    this->X_unscaled = this->X_NLP_incumbent_unscaled;
                    this->X_scaled = this->X_NLP_incumbent_scaled;
                    this->F = this->F_NLP_incumbent;
                }
            }

            if (!this->myOptions.get_quiet_NLP())
            {
                std::cout << "Uno: status=" << static_cast<int>(optimization_status)
                          << " solution=" << static_cast<int>(solution_status)
                          << " iterations=" << this->uno_iterations
                          << " cpu=" << this->uno_cpu_time << "s"
                          << " objective=" << this->F.front() << std::endl;
            }

            //Solver diagnostics, not gated on quiet_NLP. Knowing how many iterations
            //Uno actually ran and which method it used is the first thing needed when a solve
            //"does nothing", and quiet_NLP=1 would otherwise hide it completely.
            std::cout << "SOUFFLE[solve]: preset=" << uno_preset
                      << " opt_status=" << static_cast<int>(optimization_status)
                      << " sol_status=" << static_cast<int>(solution_status)
                      << " iters=" << this->uno_iterations
                      << " cpu=" << this->uno_cpu_time
                      << " f=" << this->F.front() << std::endl;
        }
    }//end namespace Solvers
}//end namespace EMTG
