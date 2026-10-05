// SOUFFLE: NLP solver interface backed by Ipopt (implementation).
// See Ipopt_interface.h for the design notes and for the list of measured gaps this
// interface is meant to close.
//
// Licensed under the NASA Open Source Agreement 1.3, like the rest of EMTG.

#include "Ipopt_interface.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <stdexcept>
#include <string>

#include "coin/IpIpoptCalculatedQuantities.hpp"
#include "coin/IpIpoptData.hpp"
#include "coin/IpSmartPtr.hpp"
#include "coin/IpSolveStatistics.hpp"

#include "problem.h"

namespace
{
    //Translates an Ipopt ApplicationReturnStatus into EMTG's SNOPT-like "inform" code.
    //
    //SNOPT's convention, which the MBH loop in monotonic_basin_hopping.cpp relies on when the
    //NLP chaperone is disabled, is: inform < 10 means "the solver produced a result worth
    //looking at". The mapping below is deliberately conservative:
    //   1  fully optimal                         (SNOPT's 1)
    //   2  at an acceptable/feasible point       (SNOPT's 2, "feasible but not optimal")
    //   3  ran out of budget (iterations/time)   (SNOPT's 3)
    //  >=10 failure, with distinct codes for positive and negative Ipopt statuses so a log
    //       can always be traced back to the exact ApplicationReturnStatus.
    //Note that "failure" here does NOT mean the iterate is thrown away: run_NLP still feeds
    //it to EMTG's chaperone, which is the authority on whether the point is any good.
    int translate_status(Ipopt::ApplicationReturnStatus status, bool goal_attained)
    {
        using namespace Ipopt;

        if (goal_attained)
            return 1;

        switch (status)
        {
        case Solve_Succeeded:                    return 1;
        case Solved_To_Acceptable_Level:         return 2;
        case Feasible_Point_Found:               return 2;
        case User_Requested_Stop:                return 3;   //our own wall-clock/iteration stop
        case Maximum_Iterations_Exceeded:        return 3;
        case Maximum_CpuTime_Exceeded:           return 3;
        case Maximum_WallTime_Exceeded:          return 3;
        default:
            break;
        }

        //Everything else is a failure. Ipopt's positive failure codes are 2..6, its negative
        //ones are -2..-199; offset the two families apart so they stay distinguishable.
        const int raw = static_cast<int>(status);
        if (raw >= 0)
            return 10 + raw;
        return 20 - raw;   //raw is negative, so this is 20 + |raw|
    }

    //Reads an environment variable, returning nullptr when it is unset or empty.
    const char* get_env(const char* name)
    {
        const char* value = std::getenv(name);
        return (value != nullptr && value[0] != '\0') ? value : nullptr;
    }
}//end anonymous namespace

namespace EMTG
{
    namespace Solvers
    {
        //--------------------------------------------------------------------------
        // construction
        //--------------------------------------------------------------------------

        Ipopt_interface::Ipopt_interface(problem* myProblem,
            const NLPoptions& myOptions) :
            NLP_interface::NLP_interface(myProblem, myOptions)
        {
            //Ipopt is linked in directly (see SOUFFLE_WITH_IPOPT in CMakeLists.txt); there is
            //no run-time library lookup to fail the way there is for Uno, so the constructor
            //only has to size the evaluation cache.
            this->cached_x_scaled.assign(this->nX, 0.0);
            this->first_feasibility = false;
        }

        //--------------------------------------------------------------------------
        // evaluation plumbing
        //--------------------------------------------------------------------------

        void Ipopt_interface::set_scaled_iterate(const double* x_scaled)
        {
            for (std::size_t Xindex = 0; Xindex < this->nX; ++Xindex)
            {
                this->X_scaled[Xindex] = x_scaled[Xindex];
                //Same un-scaling rule as NLP_interface::unscaleX().
                this->X_unscaled[Xindex] = x_scaled[Xindex] * this->myProblem->X_scale_factors[Xindex]
                    + this->myProblem->Xlowerbounds[Xindex];
            }
        }

        void Ipopt_interface::evaluate_problem(bool need_gradient)
        {
            if (this->myOptions.get_SolverMode() == NLPMode::FilamentFinder)
            {
                //Filament finding is effectively unconstrained: the objective becomes the sum of
                //squares of the equality constraints. Same construction as SNOPT_interface and
                //Souffle_interface.
                //
                //The evaluation goes into THIS interface's F/G, never into myProblem->F/G. In this
                //mode the constraints and the Jacobian have to be read back while building the
                //least-squares objective, which is why the older code used the problem's buffers;
                //the local buffers carry the same values, so nothing is lost.
                this->myProblem->evaluate(this->X_unscaled, this->F, this->G, need_gradient);

                std::size_t number_of_constraints = this->myProblem->total_number_of_constraints;
                if (number_of_constraints > this->F.size())
                    number_of_constraints = this->F.size();

                this->F.front() = 0.0;
                for (std::size_t Findex = 1; Findex < number_of_constraints; ++Findex)
                {
                    if (this->myProblem->F_equality_or_inequality[Findex - 1])
                        this->F.front() += this->F[Findex] * this->F[Findex];
                }

                const std::vector<double> constraint_values = this->F;
                const std::vector<double> constraint_jacobian = this->G;

                std::fill(this->G.begin(), this->G.end(), 0.0);

                const std::size_t Problem_nG = this->myProblem->Gdescriptions.size();
                for (std::size_t Gindex = 0; Gindex < Problem_nG && Gindex < constraint_jacobian.size(); ++Gindex)
                {
                    const std::size_t Findex = this->myProblem->iGfun[Gindex];
                    const std::size_t Xindex = this->myProblem->jGvar[Gindex];

                    if (Findex > 0 && Xindex < this->G.size()
                        && this->myProblem->F_equality_or_inequality[Findex - 1])
                    {
                        this->G[Xindex] += 2.0 * constraint_values[Findex] * constraint_jacobian[Gindex];
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

        void Ipopt_interface::ensure_evaluated(const double* x_scaled, bool need_gradient)
        {
            //Ipopt calls eval_f / eval_grad_f / eval_g / eval_jac_g separately at the same
            //point (and asks for the Jacobian structure with x == NULL). EMTG computes the
            //objective, the constraints and the Jacobian in one pass, so cache the evaluation
            //until the point actually changes. This is the Ipopt port of
            //Souffle_interface::ensure_evaluated.
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
        // chaperone
        //--------------------------------------------------------------------------

        //SOUFFLE FIX (I4): bank a candidate point using only the values already in hand.
        //
        //This is the Ipopt port of Souffle_interface::bank_candidate (SOUFFLE FIX (D9)). It is
        //safe from inside an evaluation callback because it touches nothing but the cache: in
        //particular it never calls myProblem->check_feasibility(), which is what made the
        //earlier attempts to pool trial points from Uno callbacks corrupt EMTG's state.
        //
        //It is OFF by default: with Ipopt the per-iteration intermediate_callback already runs
        //the *real* chaperone (check_feasibility and all), so trial-point pooling is normally
        //redundant. IPOPT_TRIAL_INCUMBENTS=1 enables it for A/B comparisons against SNOPT,
        //whose user function banks on every evaluation rather than every iteration.
        void Ipopt_interface::bank_candidate()
        {
            if (!this->settings.bank_trial_points)
                return;

            if (this->nF < 2 || this->F.size() < this->nF)
                return;

            //Worst constraint violation in EMTG's own units: EMTG's Flowerbounds/Fupperbounds
            //already carry the feasibility tolerance, so "inside the bounds" *is* the test.
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

            if (worst_violation > 0.0 || this->F.front() >= this->J_NLP_incumbent)
                return;

            //Minimal-footprint policy, as in Souffle: record the point only, and deliberately
            //do not touch the feasibility bookkeeping (feasibility_metric_NLP_incumbent / ...),
            //which update_chaperone() owns.
            this->X_NLP_incumbent_scaled = this->X_scaled;
            this->X_NLP_incumbent_unscaled = this->X_unscaled;
            this->F_NLP_incumbent = this->F;
            this->G_NLP_incumbent = this->G;
            this->J_NLP_incumbent = this->F.front();
            this->newBestIncumbent = true;
            ++this->incumbent_updates;
        }

        void Ipopt_interface::update_chaperone()
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
                //A NaN in the iterate or the constraints: keep the previous incumbent and let
                //Ipopt deal with its own iterate.
                if (!this->myOptions.get_quiet_NLP())
                    std::cout << error.what() << std::endl;
                return;
            }

            this->decision_vector_feasibility_metric = decision_variable_feasibility_metric;
            this->normalized_feasibility_metric = normalized_feasibility;
            this->feasibility_metric = std::max(normalized_feasibility, decision_variable_feasibility_metric);

            //Keep the most feasible (and, among equally feasible, the best) point seen so far.
            //This is the same rule SNOPT_interface applies in its user function.
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
                ++this->incumbent_updates;
            }

            //SOUFFLE: remember whether anything was ever feasible for this solve. SNOPT uses
            //this to decide when to write an intermediate solution to disk; the interface keeps
            //the flag coherent so that other consumers (and future code) see the same state.
            if (feasible_enough)
                this->first_feasibility = true;
        }

        //--------------------------------------------------------------------------
        // Ipopt TNLP callbacks
        //--------------------------------------------------------------------------

        bool Ipopt_interface::EMTG_TNLP::get_nlp_info(Ipopt::Index& n, Ipopt::Index& m,
            Ipopt::Index& nnz_jac_g, Ipopt::Index& nnz_h_lag, IndexStyleEnum& index_style)
        {
            Ipopt_interface* self = this->owner;

            n = static_cast<Ipopt::Index>(self->nX);
            //EMTG's F[0] is the objective row, so the constraint count is one less.
            m = (self->nF > 0) ? static_cast<Ipopt::Index>(self->nF - 1) : 0;
            nnz_jac_g = static_cast<Ipopt::Index>(self->constraint_jacobian_source.size());

            //No exact Hessian is provided (eval_h is not implemented on purpose): Ipopt runs
            //hessian_approximation=limited-memory, which is the default and needs no Hessian
            //sparsity at all.
            nnz_h_lag = 0;

            //0-based (C) indexing, matching what Souffle_interface passes to Uno and what the
            //EMTG index vectors already are.
            index_style = C_STYLE;
            return true;
        }

        bool Ipopt_interface::EMTG_TNLP::get_bounds_info(Ipopt::Index n, Ipopt::Number* x_l,
            Ipopt::Number* x_u, Ipopt::Index m, Ipopt::Number* g_l, Ipopt::Number* g_u)
        {
            Ipopt_interface* self = this->owner;

            if (static_cast<std::size_t>(n) != self->nX)
                return false;
            if (static_cast<std::size_t>(m) != ((self->nF > 0) ? self->nF - 1 : 0))
                return false;

            //Variable bounds: identical convention to SNOPT_interface / Souffle_interface, i.e.
            //the scaled box is [0, (Xupper - Xlower) / X_scale_factor].
            for (std::size_t Xindex = 0; Xindex < self->nX; ++Xindex)
            {
                x_l[Xindex] = 0.0;
                x_u[Xindex] = (self->myProblem->Xupperbounds[Xindex]
                    - self->myProblem->Xlowerbounds[Xindex])
                    / self->myProblem->X_scale_factors[Xindex];
            }

            //Constraint bounds: EMTG's rows 1..nF-1 (row 0 is the objective).
            for (std::size_t Findex = 0; Findex < static_cast<std::size_t>(m); ++Findex)
            {
                g_l[Findex] = self->Flowerbounds[Findex + 1];
                g_u[Findex] = self->Fupperbounds[Findex + 1];
            }

            return true;
        }

        bool Ipopt_interface::EMTG_TNLP::get_starting_point(Ipopt::Index n, bool init_x,
            Ipopt::Number* x, bool init_z, Ipopt::Number* z_L, Ipopt::Number* z_U,
            Ipopt::Index m, bool init_lambda, Ipopt::Number* lambda)
        {
            (void)init_z; (void)z_L; (void)z_U; (void)init_lambda; (void)lambda; (void)m;
            Ipopt_interface* self = this->owner;

            if (init_x)
            {
                if (static_cast<std::size_t>(n) != self->nX)
                    return false;
                for (std::size_t Xindex = 0; Xindex < self->nX; ++Xindex)
                    x[Xindex] = self->X0_scaled[Xindex];
            }

            //The default Ipopt options only require the primal point; bound multipliers and
            //constraint multipliers are initialised internally (bound_push / bound_frac).
            return true;
        }

        bool Ipopt_interface::EMTG_TNLP::eval_f(Ipopt::Index n, const Ipopt::Number* x,
            bool new_x, Ipopt::Number& obj_value)
        {
            (void)new_x;
            Ipopt_interface* self = this->owner;
            try
            {
                if (static_cast<std::size_t>(n) != self->nX)
                    return false;

                self->ensure_evaluated(x, false);
                self->bank_candidate();     //SOUFFLE FIX (I4), inert unless enabled
                obj_value = self->F.front();
                return true;
            }
            catch (const std::exception& error)
            {
                if (!self->myOptions.get_quiet_NLP())
                    std::cout << "Ipopt: eval_f failed: " << error.what() << std::endl;
                return false;
            }
        }

        bool Ipopt_interface::EMTG_TNLP::eval_grad_f(Ipopt::Index n, const Ipopt::Number* x,
            bool new_x, Ipopt::Number* grad_f)
        {
            (void)new_x;
            Ipopt_interface* self = this->owner;
            try
            {
                if (static_cast<std::size_t>(n) != self->nX)
                    return false;

                self->ensure_evaluated(x, true);
                self->bank_candidate();     //SOUFFLE FIX (I4), inert unless enabled

                std::fill(grad_f, grad_f + self->nX, 0.0);

                //objective_type = 0 (MaximizeMass / delta-v): the objective derivatives live in
                //the nonlinear Jacobian G, on the rows with iGfun == 0.
                for (std::size_t Gindex = 0; Gindex < self->nG; ++Gindex)
                {
                    if (self->iGfun[Gindex] == 0)
                        grad_f[self->jGvar[Gindex]] += self->G[Gindex];
                }

                //SOUFFLE FIX (I3): objective_type = 1 (MinTOF) keeps its ONLY non-zero
                //derivatives in the *linear* Jacobian A with iAfun == 0. This is the Ipopt port
                //of SOUFFLE FIX (D1) in Souffle_interface.cpp, and it is the same bug: without
                //the loop below the objective gradient is identically zero, every feasible point
                //is a KKT point, and the solver "converges" in a handful of iterations at
                //whatever point MBH supplied.
                //
                //Reasoning (unchanged from the Uno fix): MinimizeTimeObjective.cpp fills
                //iAfun = 0 / jAvar = <time column> / A = X_scale_factor / LU, and it is the only
                //writer of A anywhere in the tree -- the grep for iAfun->push_back finds just
                //that one call site. SNOPT is handed the same matrix through setA(), so this
                //loop is exactly what makes Ipopt solve the same problem SNOPT solves.
                for (std::size_t Aindex = 0; Aindex < self->nA; ++Aindex)
                {
                    if (self->iAfun[Aindex] == 0)
                        grad_f[self->jAvar[Aindex]] += self->A[Aindex];
                }

                return true;
            }
            catch (const std::exception& error)
            {
                if (!self->myOptions.get_quiet_NLP())
                    std::cout << "Ipopt: eval_grad_f failed: " << error.what() << std::endl;
                return false;
            }
        }

        bool Ipopt_interface::EMTG_TNLP::eval_g(Ipopt::Index n, const Ipopt::Number* x,
            bool new_x, Ipopt::Index m, Ipopt::Number* g)
        {
            (void)new_x;
            Ipopt_interface* self = this->owner;
            try
            {
                if (static_cast<std::size_t>(n) != self->nX)
                    return false;
                if (static_cast<std::size_t>(m) != ((self->nF > 0) ? self->nF - 1 : 0))
                    return false;

                self->ensure_evaluated(x, false);
                self->bank_candidate();     //SOUFFLE FIX (I4), inert unless enabled

                //Ipopt only sees the constraints: EMTG's F[0] is the objective.
                for (std::size_t Findex = 0; Findex < static_cast<std::size_t>(m); ++Findex)
                    g[Findex] = self->F[Findex + 1];

                return true;
            }
            catch (const std::exception& error)
            {
                if (!self->myOptions.get_quiet_NLP())
                    std::cout << "Ipopt: eval_g failed: " << error.what() << std::endl;
                return false;
            }
        }

        bool Ipopt_interface::EMTG_TNLP::eval_jac_g(Ipopt::Index n, const Ipopt::Number* x,
            bool new_x, Ipopt::Index m, Ipopt::Index nele_jac, Ipopt::Index* iRow,
            Ipopt::Index* jCol, Ipopt::Number* values)
        {
            (void)new_x;
            Ipopt_interface* self = this->owner;

            const std::size_t count = self->constraint_jacobian_source.size();

            //First call: x and values are NULL, and Ipopt wants the sparsity structure.
            if (values == nullptr)
            {
                if (iRow == nullptr || jCol == nullptr)
                    return false;
                if (static_cast<std::size_t>(nele_jac) != count)
                    return false;

                for (std::size_t entry = 0; entry < count; ++entry)
                {
                    const std::size_t source = self->constraint_jacobian_source[entry];
                    //EMTG's F[0] is the objective, so the constraint rows shift by one.
                    iRow[entry] = static_cast<Ipopt::Index>(self->iGfun[source] - 1);
                    jCol[entry] = static_cast<Ipopt::Index>(self->jGvar[source]);
                }
                return true;
            }

            try
            {
                if (static_cast<std::size_t>(n) != self->nX)
                    return false;
                if (static_cast<std::size_t>(m) != ((self->nF > 0) ? self->nF - 1 : 0))
                    return false;
                if (static_cast<std::size_t>(nele_jac) != count)
                    return false;

                self->ensure_evaluated(x, true);
                self->bank_candidate();     //SOUFFLE FIX (I4), inert unless enabled

                //The sparsity was built from constraint_jacobian_source in this exact order, so
                //copy the EMTG values straight through that mapping.
                for (std::size_t entry = 0; entry < count; ++entry)
                    values[entry] = self->G[self->constraint_jacobian_source[entry]];

                return true;
            }
            catch (const std::exception& error)
            {
                if (!self->myOptions.get_quiet_NLP())
                    std::cout << "Ipopt: eval_jac_g failed: " << error.what() << std::endl;
                return false;
            }
        }

        void Ipopt_interface::EMTG_TNLP::finalize_solution(Ipopt::SolverReturn status,
            Ipopt::Index n, const Ipopt::Number* x, const Ipopt::Number* z_L,
            const Ipopt::Number* z_U, Ipopt::Index m, const Ipopt::Number* g,
            const Ipopt::Number* lambda, Ipopt::Number obj_value,
            const Ipopt::IpoptData* ip_data, Ipopt::IpoptCalculatedQuantities* ip_cq)
        {
            (void)z_L; (void)z_U; (void)m; (void)g; (void)lambda; (void)ip_data; (void)ip_cq;
            Ipopt_interface* self = this->owner;

            self->solution_available = false;
            if (x != nullptr && static_cast<std::size_t>(n) == self->nX)
            {
                for (std::size_t Xindex = 0; Xindex < self->nX; ++Xindex)
                    self->solution_x_scaled[Xindex] = x[Xindex];
                self->solution_obj_value = obj_value;
                self->solution_available = true;
            }

            if (!self->myOptions.get_quiet_NLP())
            {
                std::cout << "Ipopt: finalize_solution with SolverReturn="
                          << static_cast<int>(status)
                          << " f=" << obj_value
                          << " solution_available=" << (self->solution_available ? "yes" : "no")
                          << std::endl;
            }
        }

        bool Ipopt_interface::EMTG_TNLP::intermediate_callback(Ipopt::AlgorithmMode mode,
            Ipopt::Index iter, Ipopt::Number obj_value, Ipopt::Number inf_pr,
            Ipopt::Number inf_du, Ipopt::Number mu, Ipopt::Number d_norm,
            Ipopt::Number regularization_size, Ipopt::Number alpha_du,
            Ipopt::Number alpha_pr, Ipopt::Index ls_trials,
            const Ipopt::IpoptData* ip_data, Ipopt::IpoptCalculatedQuantities* ip_cq)
        {
            (void)obj_value; (void)inf_pr; (void)inf_du; (void)mu; (void)d_norm;
            (void)regularization_size; (void)alpha_du; (void)alpha_pr; (void)ls_trials;

            Ipopt_interface* self = this->owner;
            if (self == nullptr)
                return true;

            //SOUFFLE FIX (I7): diagnostic switch, off unless asked for. Skipping this callback
            //removes the incumbent pooling and the per-iteration checks, leaving Ipopt to run
            //entirely on its own; it exists to tell "Ipopt's own solve is fine" apart from "the
            //per-iteration callback is what breaks it" without a rebuild.
            if (get_env("IPOPT_NO_INTERMEDIATE") != nullptr)
                return true;

            ++self->intermediate_callback_calls;
            if (mode == Ipopt::RestorationPhaseMode)
                ++self->restoration_callback_calls;

            try
            {
                //SOUFFLE FIX (I1): obtain the current iterate from Ipopt rather than trusting
                //the evaluation cache, then run EMTG's chaperone exactly where SNOPT runs it --
                //on a real point of the solve. Ipopt's translate-in-internal-representation
                //helper maps the (possibly restoration-phase) iterate back onto the original
                //TNLP, which is what EMTG's feasibility check needs.
                const std::size_t m = (self->nF > 0) ? self->nF - 1 : 0;
                std::vector<double> x_current(self->nX, 0.0);

                const bool got_iterate = this->get_curr_iterate(ip_data, ip_cq,
                    /*scaled=*/true,
                    static_cast<Ipopt::Index>(self->nX), x_current.data(),
                    nullptr, nullptr,
                    static_cast<Ipopt::Index>(m), nullptr, nullptr);

                if (got_iterate)
                {
                    self->ensure_evaluated(x_current.data(), false);
                }
                else if (!self->cache_valid)
                {
                    //Nothing to work with: report through EMTG's own diagnostics rather than
                    //silently banking a stale point.
                    std::cerr << "SOUFFLE(IPOPT): get_curr_iterate failed at iteration "
                              << static_cast<int>(iter) << "; skipping chaperone update" << std::endl;
                    return true;
                }
                else if (self->intermediate_callback_calls <= 3
                    && !self->myOptions.get_quiet_NLP())
                {
                    //Bounded diagnostic: this is not an error, Ipopt may legitimately be
                    //unable to translate an internal iterate, but it should not be common.
                    std::cout << "SOUFFLE(IPOPT): get_curr_iterate unavailable at iteration "
                              << static_cast<int>(iter) << "; using the cached iterate" << std::endl;
                }

                //---- the actual incumbent pooling ----
                //SOUFFLE FIX (I1c): deliberately done BEFORE the budget checks below. The whole
                //point of this callback is to bank the accepted iterate, so a wall-clock stop
                //must not throw away the iteration that triggered it.
                if (self->myOptions.get_enable_NLP_chaperone())
                    self->update_chaperone();

                //---- early stop when the goal is attained (SNOPT's *Status = -2) ----
                //This is off by default (NLP_stop_on_goal_attain), so the extra feasibility
                //check costs nothing in a normal run.
                if (self->myOptions.get_stop_on_goal_attain())
                {
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
                        return false;
                    }
                }

                //---- wall-clock budget ----
                //Belt and braces with Ipopt's own max_wall_time option: this check runs on every
                //iteration and also sees a stall inside a long restoration phase.
                const time_t elapsed = time(nullptr) - self->NLP_start_time;
                if (elapsed >= static_cast<time_t>(self->myOptions.get_max_run_time_seconds()))
                {
                    if (!self->myOptions.get_quiet_NLP())
                        std::cout << "Ipopt: wall-clock limit of "
                                  << self->myOptions.get_max_run_time_seconds()
                                  << " s reached, terminating NLP." << std::endl;
                    return false;   //Ipopt stops with User_Requested_Stop
                }
            }
            catch (const std::exception& error)
            {
                //Never let an exception escape into Ipopt's call stack: report it and continue
                //the solve. run_NLP's write-back still cannot adopt a bad point, because the
                //adoption rule only ever prefers better incumbents.
                if (!self->myOptions.get_quiet_NLP())
                    std::cout << "Ipopt: intermediate_callback error: " << error.what() << std::endl;
            }

            return true;
        }

        //--------------------------------------------------------------------------
        // configuration
        //--------------------------------------------------------------------------

        void Ipopt_interface::configure_application(Ipopt::IpoptApplication& application)
        {
            Ipopt::SmartPtr<Ipopt::OptionsList> options = application.Options();

            const double feasibility_tolerance = this->myOptions.get_feasibility_tolerance();
            const double optimality_tolerance = this->myOptions.get_optimality_tolerance();

            //---- tolerances ----
            //EMTG's tolerances are SNOPT's; they map onto Ipopt's scaled-residual tests
            //(dual_inf_tol / compl_inf_tol / constr_viol_tol) almost one-for-one.
            this->settings.tol = optimality_tolerance;
            this->settings.constr_viol_tol = feasibility_tolerance;

            options->SetNumericValue("tol", optimality_tolerance);
            options->SetNumericValue("dual_inf_tol", optimality_tolerance);
            options->SetNumericValue("compl_inf_tol", optimality_tolerance);
            options->SetNumericValue("constr_viol_tol", feasibility_tolerance);

            //"Acceptable level" is Ipopt's escape hatch for a point that is feasible but not
            //stationary. It maps onto SNOPT's "feasible but not optimal" (inform 2), so give it
            //EMTG's feasibility tolerance on the constraints and a decade of slack on
            //optimality -- i.e. never call a point acceptable that EMTG would reject as
            //infeasible.
            const double acceptable_tol = std::max(optimality_tolerance * 10.0, 1.0e-6);
            options->SetNumericValue("acceptable_tol", acceptable_tol);
            options->SetNumericValue("acceptable_dual_inf_tol", acceptable_tol);
            options->SetNumericValue("acceptable_compl_inf_tol", acceptable_tol);
            options->SetNumericValue("acceptable_constr_viol_tol", feasibility_tolerance);

            //---- budget ----
            //ITERATION BUDGET: SNOPT gets NLPoptions::major_iterations_limit as its "Major
            //iterations limit". An Ipopt interior-point iteration is a different unit of work
            //(several evaluations), so the budget is set to twice the SNOPT major-iteration
            //limit -- the same 2x convention Souffle_interface uses for Uno. Override with
            //IPOPT_MAX_ITER for a sweep.
            long max_iter = 2 * static_cast<long>(this->myOptions.get_major_iterations_limit());
            if (const char* env = get_env("IPOPT_MAX_ITER"))
                max_iter = std::atol(env);
            if (max_iter <= 0)
                max_iter = 2 * static_cast<long>(this->myOptions.get_major_iterations_limit());
            this->settings.max_iter = static_cast<int>(max_iter);
            options->SetIntegerValue("max_iter", static_cast<Ipopt::Index>(this->settings.max_iter));

            this->settings.max_wall_time = static_cast<double>(this->myOptions.get_max_run_time_seconds());
            if (const char* env = get_env("IPOPT_MAX_WALL_TIME"))
                this->settings.max_wall_time = std::atof(env);
            options->SetNumericValue("max_wall_time", this->settings.max_wall_time);

            //---- output ----
            //Ipopt's default print_level is 5, which prints one line per iteration plus a
            //final summary. quiet_NLP silences it; IPOPT_PRINT_LEVEL overrides both so a
            //diagnostic run can be taken without rebuilding.
            this->settings.print_level = this->myOptions.get_quiet_NLP() ? 0 : 5;
            if (const char* env = get_env("IPOPT_PRINT_LEVEL"))
                this->settings.print_level = std::atoi(env);
            options->SetIntegerValue("print_level", static_cast<Ipopt::Index>(this->settings.print_level));
            //The banner is printed by Initialize() regardless of print_level unless suppressed.
            options->SetStringValue("sb", this->settings.print_level > 0 ? "no" : "yes");

            //---- SOUFFLE FIX (I2): scaling ----
            //This is the second measured gap: Uno's filtersqp preset does no problem scaling at
            //all, while SNOPT scales automatically. Ipopt's gradient-based NLP scaling is its
            //default, but it is set explicitly here so that it is visible in the options list
            //and so that it can be A/B'd from the environment (IPOPT_SCALING=none reproduces
            //the unscaled-Uno situation).
            this->settings.scaling_method = "gradient-based";
            if (const char* env = get_env("IPOPT_SCALING"))
                this->settings.scaling_method = env;

            static const char* const valid_scaling[] = {
                "none", "gradient-based", "equilibration-based", "user-scaling" };
            bool scaling_ok = false;
            for (const char* candidate : valid_scaling)
            {
                if (this->settings.scaling_method == candidate)
                {
                    scaling_ok = true;
                    break;
                }
            }
            if (!scaling_ok)
            {
                std::cerr << "SOUFFLE(IPOPT): unknown IPOPT_SCALING value '"
                          << this->settings.scaling_method
                          << "'; using gradient-based" << std::endl;
                this->settings.scaling_method = "gradient-based";
            }
            options->SetStringValue("nlp_scaling_method", this->settings.scaling_method);

            //One pathological row should not be able to set the scale for the whole problem.
            if (const char* env = get_env("IPOPT_SCALING_MAX_GRADIENT"))
                options->SetNumericValue("nlp_scaling_max_gradient", std::atof(env));

            //---- SOUFFLE FIX (I5): failure recovery ----
            //Uno treats an algorithmic failure as fatal for the whole NLP. Ipopt is a smooth
            //interior-point method with a restoration phase, so restoration is left enabled and
            //expect_infeasible_problem stays off by default: EMTG's models are feasible, and
            //turning this on makes Ipopt deliberately walk into the infeasible region, which
            //costs iterations and changes the answer. It is exposed because the "83% abandoned
            //solves" pathology in the Uno build is exactly what it is designed for.
            this->settings.expect_infeasible_problem = false;
            if (const char* env = get_env("IPOPT_EXPECT_INFEASIBLE"))
                this->settings.expect_infeasible_problem = (std::atoi(env) != 0);
            options->SetStringValue("expect_infeasible_problem",
                this->settings.expect_infeasible_problem ? "yes" : "no");

            //Ipopt relaxes variable bounds by bound_relax_factor by default and may then return
            //a point slightly outside EMTG's box. honour_original_bounds makes the reported
            //solution respect the bounds EMTG actually gave, which matters because EMTG runs its
            //own decision-variable feasibility check on the result.
            options->SetStringValue("honor_original_bounds", "yes");

            //---- Hessian ----
            //eval_h is not implemented, so only the limited-memory (L-BFGS) Hessian
            //approximation works. This is Ipopt's default and is the right choice for EMTG:
            //second derivatives of an integrated trajectory are not available.
            this->settings.hessian_approximation = "limited-memory";
            if (const char* env = get_env("IPOPT_HESSIAN"))
            {
                if (std::strcmp(env, "limited-memory") != 0)
                {
                    std::cerr << "SOUFFLE(IPOPT): IPOPT_HESSIAN='" << env
                              << "' ignored: this interface does not implement eval_h, so only "
                                 "'limited-memory' is usable" << std::endl;
                }
            }
            options->SetStringValue("hessian_approximation", "limited-memory");
            if (const char* env = get_env("IPOPT_LM_HISTORY"))
                options->SetIntegerValue("limited_memory_max_history",
                    static_cast<Ipopt::Index>(std::atoi(env)));

            //No warm start plumbing by default: MBH hands each solve a fresh, unrelated point, and
            //Ipopt's interior-point start is the right answer for that.
            //
            //SOUFFLE FIX (I6): warm-start hooks, inert unless the environment asks for them. They
            //matter whenever Ipopt is handed a point that is already feasible-ish and must not have
            //it shoved back inside the bounds: the default bound_push=1e-2 (scaled units) moves
            //every variable 1% of the way to its bounds, which discards most of the incumbent and
            //lands back in the restoration phase that ate 70.9% of Ipopt's callbacks on cold starts.
            //These cannot go through IPOPT_OPTIONS, which can only set options as strings and Ipopt
            //rejects bound_push/bound_frac that way.
            //
            //   IPOPT_WARM_START=1          warm_start_init_point=yes
            //   IPOPT_BOUND_PUSH=x          bound_push (default 1e-2)
            //   IPOPT_BOUND_FRAC=x          bound_frac (default 1e-2)
            //   IPOPT_BOUND_MULT_INIT=s     bound_mult_init_method (constant | mu-based)
            const bool warm_start = get_env("IPOPT_WARM_START") != nullptr;
            options->SetStringValue("warm_start_init_point", warm_start ? "yes" : "no");
            if (const char* env = get_env("IPOPT_BOUND_PUSH"))
                options->SetNumericValue("bound_push", std::atof(env));
            if (const char* env = get_env("IPOPT_BOUND_FRAC"))
                options->SetNumericValue("bound_frac", std::atof(env));
            if (const char* env = get_env("IPOPT_BOUND_MULT_INIT"))
                options->SetStringValue("bound_mult_init_method", env);

            //---- derivatives ----
            if (this->myOptions.get_check_derivatives())
            {
                this->settings.derivative_test = true;
                //"first-order" checks the objective gradient and the constraint Jacobian
                //against finite differences, which is the analogue of SNOPT's Verify level 3.
                options->SetStringValue("derivative_test", "first-order");
                options->SetStringValue("derivative_test_print_all", "yes");
                options->SetNumericValue("derivative_test_tol", 1.0e-4);
            }

            //---- SOUFFLE FIX (I4): trial-point pooling, opt-in ----
            this->settings.bank_trial_points = false;
            if (const char* env = get_env("IPOPT_TRIAL_INCUMBENTS"))
                this->settings.bank_trial_points = (std::atoi(env) != 0);

            //---- generic passthrough ----
            //IPOPT_OPTIONS="name=value;name=value" sets any registered Ipopt option, so an
            //experiment never needs a rebuild. Values are given as strings because that is the
            //only form Ipopt's option list can parse generically.
            if (const char* env = get_env("IPOPT_OPTIONS"))
            {
                std::string list(env);
                std::size_t start = 0;
                while (start <= list.size())
                {
                    const std::size_t end = list.find(';', start);
                    const std::string item = list.substr(start,
                        (end == std::string::npos) ? std::string::npos : end - start);
                    const std::size_t equals = item.find('=');
                    if (equals != std::string::npos && equals > 0)
                    {
                        const std::string name = item.substr(0, equals);
                        const std::string value = item.substr(equals + 1);
                        if (!options->SetStringValue(name, value))
                        {
                            std::cerr << "SOUFFLE(IPOPT): IPOPT_OPTIONS entry '" << name
                                      << "' was not accepted by Ipopt" << std::endl;
                        }
                        else if (!this->myOptions.get_quiet_NLP())
                        {
                            std::cout << "SOUFFLE(IPOPT): " << name << " = " << value
                                      << " (from IPOPT_OPTIONS)" << std::endl;
                        }
                    }
                    if (end == std::string::npos)
                        break;
                    start = end + 1;
                }
            }

            if (!this->myOptions.get_quiet_NLP())
            {
                std::cout << "SOUFFLE(IPOPT): nlp_scaling_method=" << this->settings.scaling_method
                          << " hessian_approximation=" << this->settings.hessian_approximation
                          << " max_iter=" << this->settings.max_iter
                          << " max_wall_time=" << this->settings.max_wall_time
                          << " print_level=" << this->settings.print_level
                          << " tol=" << this->settings.tol
                          << " constr_viol_tol=" << this->settings.constr_viol_tol
                          << " expect_infeasible_problem="
                          << (this->settings.expect_infeasible_problem ? "yes" : "no")
                          << " trial_incumbents=" << (this->settings.bank_trial_points ? "yes" : "no")
                          << std::endl;
            }
        }

        //--------------------------------------------------------------------------
        // main entry point
        //--------------------------------------------------------------------------

        void Ipopt_interface::run_NLP(const bool& X0_is_scaled)
        {
            //---- initial guess, in scaled variables ----
            if (!X0_is_scaled)
                this->scaleX0();
            else
                this->unscaleX0();

            this->X_scaled = this->X0_scaled;

            //---- per-solve state, identical to SNOPT_interface / Souffle_interface ----
            this->cache_valid = false;
            this->cache_has_F = false;
            this->cache_has_G = false;
            this->inform = 99;
            this->goal_attained = false;
            this->application_status = Ipopt::Invalid_Problem_Definition;
            this->ipopt_iterations = 0;
            this->ipopt_wall_time = 0.0;
            this->ipopt_cpu_time = 0.0;
            this->intermediate_callback_calls = 0;
            this->restoration_callback_calls = 0;
            this->incumbent_updates = 0;
            this->solution_available = false;
            this->solution_obj_value = 0.0;
            this->solution_x_scaled.assign(this->nX, 0.0);
            this->first_feasibility = false;
            this->newBestIncumbent = false;
            this->feasibility_metric_NLP_incumbent = 1.0e+101;
            this->J_NLP_incumbent = math::LARGE;
            this->NLP_start_time = time(nullptr);
            this->mostRecentNLPWriteTime = this->NLP_start_time;
            this->movie_frame_count = 0;

            //---- sanity check ----
            //Evaluate once so that a broken transcription surfaces here with a useful message
            //instead of as an opaque Ipopt "invalid problem definition" return code.
            this->set_scaled_iterate(this->X0_scaled.data());
            this->evaluate_problem(false);
            this->cache_valid = true;
            this->cache_has_F = true;
            this->cache_has_G = false;

            //---- Jacobian sparsity, 0-based, constraint rows re-indexed ----
            //EMTG's F[0] is the objective, so iGfun == 0 entries are the objective gradient
            //(handled by eval_grad_f) and everything else belongs to constraint row
            //iGfun - 1. Only MinimizeTimeObjective writes the linear Jacobian A and it always
            //writes iAfun == 0, i.e. into the objective row; if that ever changes, the entries
            //would have to be folded into both eval_g and eval_jac_g here.
            this->constraint_jacobian_source.clear();
            this->constraint_jacobian_source.reserve(this->nG);
            for (std::size_t Gindex = 0; Gindex < this->nG; ++Gindex)
            {
                if (this->iGfun[Gindex] == 0)
                    continue;
                this->constraint_jacobian_source.push_back(Gindex);
            }

            //Defensive check on the assumption above: a linear entry on a constraint row would
            //be silently missing from the Jacobian, so say so loudly rather than quietly
            //solving a different problem.
            for (std::size_t Aindex = 0; Aindex < this->nA; ++Aindex)
            {
                if (this->iAfun[Aindex] != 0)
                {
                    std::cerr << "SOUFFLE(IPOPT): linear Jacobian entry " << Aindex
                              << " has iAfun=" << this->iAfun[Aindex]
                              << " (a constraint row). This interface only expects iAfun==0 "
                                 "entries (the objective row), so the entry is NOT included in "
                                 "the constraint Jacobian. Update Ipopt_interface::run_NLP."
                              << std::endl;
                }
            }

            const std::size_t number_constraints = (this->nF > 0) ? (this->nF - 1) : 0;
            std::cout << "SOUFFLE(IPOPT): solving '" << this->myProblem->options.mission_name
                      << "' with " << this->nX << " variables, " << number_constraints
                      << " constraints, " << this->constraint_jacobian_source.size()
                      << " Jacobian nonzeros" << std::endl;

            //---- application ----
            Ipopt::SmartPtr<Ipopt::IpoptApplication> application = IpoptApplicationFactory();
            if (Ipopt::IsNull(application))
                throw std::runtime_error("SOUFFLE(IPOPT): IpoptApplicationFactory() returned null.");

            //A failure inside EMTG's evaluation is reported by returning false from the TNLP
            //callbacks; do not let a stray C++ exception unwind through Ipopt's own stack.
            application->RethrowNonIpoptException(false);

            this->configure_application(*application);

            const Ipopt::ApplicationReturnStatus initialize_status = application->Initialize();
            if (initialize_status != Ipopt::Solve_Succeeded)
            {
                //This is a configuration problem (unknown option, bad solver), not an
                //algorithmic failure: fail fast and loudly, like the Uno interface does when
                //the runtime is unreachable.
                this->application_status = initialize_status;
                this->inform = translate_status(initialize_status, false);
                throw std::runtime_error("SOUFFLE(IPOPT): IpoptApplication::Initialize() failed with status "
                    + std::to_string(static_cast<int>(initialize_status)));
            }

            //---- solve ----
            Ipopt::SmartPtr<Ipopt_interface::EMTG_TNLP> tnlp = new Ipopt_interface::EMTG_TNLP(this);

            const Ipopt::ApplicationReturnStatus status = application->OptimizeTNLP(tnlp);            this->application_status = status;
            this->inform = translate_status(status, this->goal_attained);

            {
                Ipopt::SmartPtr<Ipopt::SolveStatistics> statistics = application->Statistics();
                if (Ipopt::IsValid(statistics))
                {
                    this->ipopt_iterations = static_cast<std::size_t>(statistics->IterationCount());
                    this->ipopt_cpu_time = statistics->TotalCpuTime();
                    this->ipopt_wall_time = statistics->TotalWallclockTime();
                }
            }

            //---- assess the point Ipopt finished at ----
            //If finalize_solution never ran (a configuration-level failure), fall back to the
            //initial guess, which is exactly the "a failed solve must not manufacture a result"
            //behaviour we want.
            if (!this->solution_available)
                this->solution_x_scaled = this->X0_scaled;

            this->set_scaled_iterate(this->solution_x_scaled.data());
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

            //---- adopt the incumbent when it is better than where Ipopt stopped ----
            //Structurally identical to SNOPT_interface::run_NLP and Souffle_interface::run_NLP:
            //the exit point only wins if it is feasible and better, or strictly less infeasible.
            //A failed solve therefore cannot replace a good incumbent.
            if (this->myOptions.get_enable_NLP_chaperone())
            {
                this->unscaleX_NLP_incumbent();

                if (worst_feasibility < this->myOptions.get_feasibility_tolerance()
                    && this->feasibility_metric_NLP_incumbent < this->myOptions.get_feasibility_tolerance())
                {
                    //Both the incumbent point and the exit point are feasible.
                    if (this->J_NLP_incumbent < this->F.front())
                    {
                        this->X_unscaled = this->X_NLP_incumbent_unscaled;
                        this->X_scaled = this->X_NLP_incumbent_scaled;
                        this->F = this->F_NLP_incumbent;
                    }
                }
                //The incumbent is feasible and the exit point is not.
                else if (this->feasibility_metric_NLP_incumbent < worst_feasibility
                    && this->feasibility_metric_NLP_incumbent < this->myOptions.get_feasibility_tolerance())
                {
                    this->X_unscaled = this->X_NLP_incumbent_unscaled;
                    this->X_scaled = this->X_NLP_incumbent_scaled;
                    this->F = this->F_NLP_incumbent;
                }
                //The incumbent is infeasible but less infeasible than the exit point.
                else if (this->feasibility_metric_NLP_incumbent < worst_feasibility)
                {
                    this->X_unscaled = this->X_NLP_incumbent_unscaled;
                    this->X_scaled = this->X_NLP_incumbent_scaled;
                    this->F = this->F_NLP_incumbent;
                }
            }

            //---- diagnostics ----
            //Not gated on quiet_NLP: knowing how many iterations Ipopt ran, why it stopped and
            //how many incumbents the chaperone banked is exactly what is needed to compare this
            //interface against SNOPT and Uno.
            std::cout << "SOUFFLE[ipopt-solve]: status=" << static_cast<int>(status)
                      << " inform=" << this->inform
                      << " iters=" << this->ipopt_iterations
                      << " cpu=" << this->ipopt_cpu_time
                      << " wall=" << this->ipopt_wall_time
                      << " callbacks=" << this->intermediate_callback_calls
                      << " resto_callbacks=" << this->restoration_callback_calls
                      << " incumbents_banked=" << this->incumbent_updates
                      << " scaling=" << this->settings.scaling_method
                      << " f=" << this->F.front()
                      << " feasibility=" << worst_feasibility << std::endl;
        }
    }//end namespace Solvers
}//end namespace EMTG
