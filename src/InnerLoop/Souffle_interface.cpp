// SOUFFLE: EMTG NLP solver interface backed by the Uno solver (implementation).
//
// See Souffle_interface.h for the contract. The structure deliberately mirrors
// SNOPT_interface.cpp so that the two solvers can be compared on identical problems:
//   * same scaled-variable convention,
//   * same EMTG-side feasibility/chaperone logic,
//   * only the solver call differs.
//
// Licensed under the NASA Open Source Agreement 1.3, like the rest of EMTG.

#include "Souffle_interface.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
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
            //Uno evaluates objective and constraints/gradient through separate callbacks. EMTG
            //computes everything in one shot, so cache the result and reuse it whenever the point
            //has not changed.
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
                *objective_value = self->F.front();
                return 0;
            }
            catch (const std::exception&)
            {
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

                std::fill(gradient, gradient + self->nX, 0.0);
                for (std::size_t Gindex = 0; Gindex < self->nG; ++Gindex)
                {
                    if (self->iGfun[Gindex] == 0)
                        gradient[self->jGvar[Gindex]] += self->G[Gindex];
                }
                return 0;
            }
            catch (const std::exception&)
            {
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
                for (std::size_t Findex = 0; Findex < count; ++Findex)
                    constraint_values[Findex] = self->F[Findex + 1];
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
                const std::size_t count = self->constraint_jacobian_source.size();
                for (std::size_t entry = 0; entry < count; ++entry)
                    jacobian_values[entry] = self->G[self->constraint_jacobian_source[entry]];
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
                //
                //NOTE (SOUFFLE): this callback exists but is deliberately NOT registered - see the
                //comment at the set_solver_callbacks call site. It is kept because the wall-clock
                //and goal-attainment logic here is the right place for it once the callback
                //protocol is properly understood.
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
            //Always report the problem size and the chosen preset. This is the cheapest way to
            //confirm from the outside that the interface really saw the expected model (e.g. that
            //override_num_steps/number_of_steps actually shrank it) and that the preset took.
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

            //Constraints: EMTG's F[1..nF-1] with their bounds.
            //The Jacobian callback passes this->G through unchanged, so the model sparsity must be
            //exactly the constraint part of EMTG's G in the same order. We therefore build the
            //model from a *filtered* sparsity that keeps only constraint rows, and mirror the same
            //filtering inside jacobian_callback via constraint_jacobian_source.
            //Stored so that the Jacobian callback maps Uno's compacted values back onto G.
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

            //Preset selection. Uno's two presets that matter here are:
            //  filtersqp - trust-region Fletcher-filter SQP (closest analogue of SNOPT)
            //  ipopt     - interior-point method (Uno's IPM, closest analogue of IPOPT)
            // Both work without a user Hessian (Uno falls back to L-BFGS). Selectable at runtime so
            // the two can be A/B compared on the same problem without rebuilding:
            //   set SOUFFLE_UNO_PRESET=ipopt
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
            //snopt_optimality_tolerance (1e-5 in the EVVEU case) is a SNOPT-tuned value; driving
            //Uno to that level of stationarity on every one of MBH's NLP calls is wasted work,
            //because EMTG's own chaperone re-checks feasibility and keeps the best incumbent. Cap
            //the requested stationarity so an individual solve returns in useful time.
            {
                const double optimality = this->myOptions.get_optimality_tolerance();
                const double uno_dual_tolerance = (optimality < 1.0e-4) ? 1.0e-4 : optimality;
                api.set_solver_double_option(solver.solver, "dual_tolerance", uno_dual_tolerance);
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
            api.set_solver_string_option(solver.solver, "logger", "SILENT");

            //NOTE (SOUFFLE): the termination callback is deliberately NOT installed.
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

            //Solver diagnostics, deliberately NOT gated on quiet_NLP. Knowing how many iterations
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
