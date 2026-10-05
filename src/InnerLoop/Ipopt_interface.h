// SOUFFLE: NLP solver interface backed by Ipopt.
//
// Wraps the EMTG problem in an Ipopt::TNLP and solves it through IpoptApplicationFactory +
// OptimizeTNLP. The Ipopt-specific behaviour is tagged SOUFFLE FIX (I1)-(I3) in the .cpp.
//
// Scaling contract (same as SNOPT_interface / Souffle_interface):
//   x_lower = 0, x_upper = (Xupper - Xlower) / X_scale_factor
//   X_unscaled = X_scaled * X_scale_factor + Xlowerbounds
//
// Licensed under the NASA Open Source Agreement 1.3, like the rest of EMTG.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "NLP_interface.h"

// Ipopt's headers are not warning-clean at /W3 on MSVC (C4251: STL members exposed from a
// dllimport class). Suppressed only around these includes.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif
#include "coin/IpIpoptApplication.hpp"
#include "coin/IpTNLP.hpp"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace EMTG
{
    namespace Solvers
    {
        class Ipopt_interface : public NLP_interface
        {
        public:
            //constructor
            Ipopt_interface() : NLP_interface::NLP_interface() {};
            Ipopt_interface(problem* myProblem,
                const NLPoptions& myOptions);

            //run NLP
            virtual void run_NLP(const bool& X0_is_scaled = true) override;

            //SOUFFLE: report solver success in SNOPT-like semantics (see
            //NLP_interface::getInform): a value below 10 means "the solver produced a result
            //worth looking at". Ipopt's ApplicationReturnStatus is translated in run_NLP.
            int getInform() const override { return this->inform; }

            //Diagnostics, for logs. Deliberately not gated on quiet_NLP: knowing why Ipopt
            //stopped is the first thing needed when a solve "does nothing".
            int get_application_status() const { return static_cast<int>(this->application_status); }
            std::size_t get_ipopt_iterations() const { return this->ipopt_iterations; }

            //---- TNLP adapter ----
            //Forwards Ipopt's four separate evaluation callbacks to the interface's single
            //cached EMTG evaluation. Public because Ipopt holds it through a SmartPtr; it is
            //not part of the interface's usable API. A nested class is used so that it may
            //reach the interface's private cache.
            class EMTG_TNLP : public Ipopt::TNLP
            {
            public:
                explicit EMTG_TNLP(Ipopt_interface* owner) : owner(owner) {}
                virtual ~EMTG_TNLP() {}

                virtual bool get_nlp_info(Ipopt::Index& n, Ipopt::Index& m, Ipopt::Index& nnz_jac_g,
                    Ipopt::Index& nnz_h_lag, IndexStyleEnum& index_style) override;

                virtual bool get_bounds_info(Ipopt::Index n, Ipopt::Number* x_l, Ipopt::Number* x_u,
                    Ipopt::Index m, Ipopt::Number* g_l, Ipopt::Number* g_u) override;

                virtual bool get_starting_point(Ipopt::Index n, bool init_x, Ipopt::Number* x,
                    bool init_z, Ipopt::Number* z_L, Ipopt::Number* z_U,
                    Ipopt::Index m, bool init_lambda, Ipopt::Number* lambda) override;

                virtual bool eval_f(Ipopt::Index n, const Ipopt::Number* x, bool new_x,
                    Ipopt::Number& obj_value) override;

                virtual bool eval_grad_f(Ipopt::Index n, const Ipopt::Number* x, bool new_x,
                    Ipopt::Number* grad_f) override;

                virtual bool eval_g(Ipopt::Index n, const Ipopt::Number* x, bool new_x,
                    Ipopt::Index m, Ipopt::Number* g) override;

                virtual bool eval_jac_g(Ipopt::Index n, const Ipopt::Number* x, bool new_x,
                    Ipopt::Index m, Ipopt::Index nele_jac, Ipopt::Index* iRow,
                    Ipopt::Index* jCol, Ipopt::Number* values) override;

                virtual void finalize_solution(Ipopt::SolverReturn status, Ipopt::Index n,
                    const Ipopt::Number* x, const Ipopt::Number* z_L, const Ipopt::Number* z_U,
                    Ipopt::Index m, const Ipopt::Number* g, const Ipopt::Number* lambda,
                    Ipopt::Number obj_value, const Ipopt::IpoptData* ip_data,
                    Ipopt::IpoptCalculatedQuantities* ip_cq) override;

                //SOUFFLE FIX (I1): Ipopt fires this once per iteration (and in the restoration
                //phase), which is the hook that lets EMTG bank an incumbent per iteration
                //instead of per solve -- the single measured behavioural gap to SNOPT.
                virtual bool intermediate_callback(Ipopt::AlgorithmMode mode, Ipopt::Index iter,
                    Ipopt::Number obj_value, Ipopt::Number inf_pr, Ipopt::Number inf_du,
                    Ipopt::Number mu, Ipopt::Number d_norm, Ipopt::Number regularization_size,
                    Ipopt::Number alpha_du, Ipopt::Number alpha_pr, Ipopt::Index ls_trials,
                    const Ipopt::IpoptData* ip_data,
                    Ipopt::IpoptCalculatedQuantities* ip_cq) override;

            private:
                Ipopt_interface* owner;

                //Not copyable: Ipopt keeps exactly one adapter per solve.
                EMTG_TNLP(const EMTG_TNLP&);
                EMTG_TNLP& operator=(const EMTG_TNLP&);
            };//end class EMTG_TNLP

            //Resolved Ipopt configuration, kept for the diagnostic line printed at the end of
            //run_NLP. Filled by configure_application().
            struct IpoptSettings
            {
                std::string scaling_method = "gradient-based";
                std::string hessian_approximation = "limited-memory";
                int print_level = 5;
                int max_iter = 2000;
                double max_wall_time = 1.0e6;
                double tol = 1.0e-6;
                double constr_viol_tol = 1.0e-5;
                bool expect_infeasible_problem = false;
                bool bank_trial_points = false;
                bool derivative_test = false;
            };//end struct IpoptSettings

        private:
            //---- evaluation ----
            //Makes sure F (and G when needed) are valid at the supplied SCALED iterate.
            //Turns Ipopt's four separate callbacks (f, grad_f, g, jac_g) into at most one EMTG
            //problem evaluation per distinct point, which matters because EMTG's transcription
            //is the expensive part. Mirrors Souffle_interface::ensure_evaluated.
            void ensure_evaluated(const double* x_scaled, bool need_gradient);

            //Runs the EMTG problem at the point currently in X_unscaled; handles
            //NLPMode::FilamentFinder. Mirrors SNOPT_interface's user function.
            void evaluate_problem(bool need_gradient);

            //Updates X_scaled/X_unscaled from a scaled iterate without evaluating.
            void set_scaled_iterate(const double* x_scaled);

            //Chaperone bookkeeping, mirroring SNOPT_interface's logic (and
            //Souffle_interface::update_chaperone).
            void update_chaperone();

            //SOUFFLE FIX (I4): bank the current cached point as an incumbent using only the
            //values already in hand, i.e. without calling myProblem->check_feasibility() (which
            //re-enters the EMTG problem). This is only safe from an evaluation callback because
            //it touches nothing but the cache; it is nevertheless off by default, because with
            //Ipopt the per-iteration intermediate_callback already does this job properly.
            //Enable with IPOPT_TRIAL_INCUMBENTS=1.
            void bank_candidate();

            //Applies NLPoptions and the environment hooks to an Ipopt application. Called
            //before Initialize(), because Ipopt only reads a few options at Initialize time.
            void configure_application(Ipopt::IpoptApplication& application);

            //---- cached evaluation state ----
            std::vector<double> cached_x_scaled;   //last scaled point evaluated
            bool cache_valid = false;
            bool cache_has_F = false;
            bool cache_has_G = false;

            //---- solver state ----
            int inform = 99;                                        //SNOPT-like status code
            bool goal_attained = false;                             //set by intermediate_callback
            Ipopt::ApplicationReturnStatus application_status = Ipopt::Invalid_Problem_Definition;
            std::size_t ipopt_iterations = 0;
            double ipopt_wall_time = 0.0;
            double ipopt_cpu_time = 0.0;

            //Solution handed back by finalize_solution(), in EMTG's scaled variables. Ipopt
            //always calls finalize_solution() (even for a failed solve), so this is the best
            //iterate Ipopt saw; run_NLP still refuses to adopt it over a better incumbent.
            std::vector<double> solution_x_scaled;
            bool solution_available = false;
            double solution_obj_value = 0.0;

            //Counts intermediate_callback invocations, used only to bound diagnostic output.
            std::size_t intermediate_callback_calls = 0;
            std::size_t restoration_callback_calls = 0;

            //SOUFFLE FIX (I1b): how many times the chaperone actually banked an incumbent
            //during this solve. This is the measurement that shows the incumbent-pool gap is
            //closed: the Uno build banks exactly ONE point per solve regardless of iteration
            //count, SNOPT banks one per evaluation, and Ipopt should bank one per iteration.
            std::size_t incumbent_updates = 0;

            //Indices into EMTG's G vector for the entries that belong to constraint rows
            //(iGfun != 0). The Ipopt Jacobian is built from exactly these entries, in this
            //order, so eval_jac_g can map Ipopt's compacted values back onto G.
            std::vector<std::size_t> constraint_jacobian_source;

            IpoptSettings settings;
        };//end class Ipopt_interface
    }//end namespace Solvers
}//end namespace EMTG
