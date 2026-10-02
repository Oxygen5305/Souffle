// SOUFFLE: dynamic-loading wrapper for the Uno solver's C API.
//
// WHY DYNAMIC LOADING
// -------------------
// The original EMTG is built with MSVC (cl.exe + NMake + Boost vc143), while the official
// Uno Windows binaries are MinGW builds. MSVC cannot link a MinGW import library
// (libuno.dll.a). However libuno.dll exports the Uno C API with *undecorated C names*
// (uno_create_model, uno_optimize, ...), and the C ABI is stable across compilers, so
// LoadLibraryEx + GetProcAddress works from MSVC code. See _probe/msvc_probe.c.
//
// This keeps the build on the original toolchain: Boost, SPICE, the Windows SDK and the
// SNOPT path are unchanged.
//
// Licensed under the NASA Open Source Agreement 1.3, like the rest of EMTG.

#pragma once

#include <string>

//The Uno C API header is the single source of truth for signatures and typedefs.
//SOUFFLE_UNO_INCLUDE_DIR is supplied by CMake.
#include "Uno_C_API.h"

namespace EMTG
{
    namespace Solvers
    {
        //Holds function pointers for the subset of the Uno C API that SOUFFLE uses.
        //
        //A pointer is non-null only if it was resolved successfully. load() resolves all of
        //them in one pass and reports the first missing symbol instead of failing silently.
        class SouffleApi
        {
        public:
            //Process-wide instance. The library is loaded on first use and never unloaded:
            //Uno keeps global state and user callbacks alive, and the process is short-lived
            //relative to the cost of re-resolving 40+ symbols.
            static const SouffleApi& instance();

            //True if libuno.dll was loaded and every required symbol resolved.
            bool ok() const { return this->loaded; }

            //Human-readable failure reason (empty when ok()).
            const std::string& error() const { return this->error_message; }

            //Directory that libuno.dll was loaded from (empty when not loaded).
            const std::string& root() const { return this->uno_root; }

            //---- model construction ----
            void* (*create_model)(const char* problem_type, uno_int number_variables,
                const double* variables_lower_bounds, const double* variables_upper_bounds,
                uno_int base_indexing) = nullptr;
            void  (*destroy_model)(void* model) = nullptr;

            //---- model definition ----
            bool  (*set_model_name)(void* model, const char* name) = nullptr;
            bool  (*set_objective)(void* model, uno_int optimization_sense,
                uno_objective_callback objective_function,
                uno_objective_gradient_callback objective_gradient) = nullptr;
            bool  (*set_constraints)(void* model, uno_int number_constraints,
                uno_constraints_callback constraint_functions,
                const double* constraints_lower_bounds, const double* constraints_upper_bounds,
                uno_int number_jacobian_nonzeros,
                const uno_int* jacobian_row_indices, const uno_int* jacobian_column_indices,
                uno_constraints_jacobian_callback jacobian) = nullptr;
            bool  (*set_initial_primal_iterate)(void* model, const double* initial_primal_iterate) = nullptr;
            bool  (*set_user_data)(void* model, void* user_data) = nullptr;

            //---- solver lifecycle ----
            void* (*create_solver)() = nullptr;
            void  (*destroy_solver)(void* solver) = nullptr;

            //---- solver configuration ----
            bool  (*set_solver_integer_option)(void* solver, const char* option_name, uno_int option_value) = nullptr;
            bool  (*set_solver_double_option)(void* solver, const char* option_name, double option_value) = nullptr;
            bool  (*set_solver_bool_option)(void* solver, const char* option_name, bool option_value) = nullptr;
            bool  (*set_solver_string_option)(void* solver, const char* option_name, const char* option_value) = nullptr;
            bool  (*set_solver_preset)(void* solver, const char* preset_name) = nullptr;
            bool  (*set_solver_callbacks)(void* solver,
                uno_notify_acceptable_iterate_callback notify_acceptable_iterate_callback,
                uno_termination_callback termination_callback, void* user_data) = nullptr;

            //---- solve ----
            void  (*optimize)(void* solver, void* model) = nullptr;

            //---- result inspection ----
            uno_int (*get_optimization_status)(void* solver) = nullptr;
            uno_int (*get_solution_status)(void* solver) = nullptr;
            double  (*get_solution_objective)(void* solver) = nullptr;
            void    (*get_primal_solution)(void* solver, double* primal_solution) = nullptr;
            double  (*get_solution_primal_feasibility)(void* solver) = nullptr;
            double  (*get_solution_stationarity)(void* solver) = nullptr;
            double  (*get_solution_complementarity)(void* solver) = nullptr;
            uno_int (*get_number_iterations)(void* solver) = nullptr;
            double  (*get_cpu_time)(void* solver) = nullptr;
            uno_int (*get_number_objective_evaluations)(void* solver) = nullptr;
            uno_int (*get_number_constraint_evaluations)(void* solver) = nullptr;
            uno_int (*get_number_objective_gradient_evaluations)(void* solver) = nullptr;
            uno_int (*get_number_jacobian_evaluations)(void* solver) = nullptr;
            const char* (*get_method_description)(void* solver) = nullptr;

            //---- diagnostics ----
            void (*get_version)(uno_int* major, uno_int* minor, uno_int* patch) = nullptr;

        private:
            SouffleApi();
            SouffleApi(const SouffleApi&) = delete;
            SouffleApi& operator=(const SouffleApi&) = delete;

            bool loaded = false;
            std::string error_message;
            std::string uno_root;
        };
    }//end namespace Solvers
}//end namespace EMTG
