// SOUFFLE: dynamic-loading wrapper for the Uno solver's C API (implementation).
//
// See SouffleApi.h for the rationale. In short: MSVC (which builds EMTG) cannot link the MinGW
// import library shipped with Uno's Windows release, but libuno.dll exports undecorated C
// symbols, so GetProcAddress is a reliable bridge.
//
// Licensed under the NASA Open Source Agreement 1.3, like the rest of EMTG.

#include "SouffleApi.h"

#include <windows.h>

#include <cstdlib>
#include <sstream>
#include <vector>

#ifndef SOUFFLE_UNO_ROOT_DEFAULT
//Fallback used when the SOUFFLE_UNO_ROOT environment variable is not set. Supplied by CMake.
#define SOUFFLE_UNO_ROOT_DEFAULT "G:\\Py\\DeepSeekHarness\\Uno"
#endif

namespace
{
    struct ResolveReport
    {
        int resolved = 0;
        std::vector<std::string> missing;
    };

    //Resolves one symbol. On failure the name is recorded and the pointer is left null;
    //resolution continues so that the error message can list everything that is wrong.
    template <typename FnPtr>
    void resolve(FnPtr& target, HMODULE module, const char* symbol, ResolveReport& report)
    {
        FARPROC address = ::GetProcAddress(module, symbol);
        if (address == nullptr)
        {
            report.missing.push_back(symbol);
            return;
        }
        target = reinterpret_cast<FnPtr>(address);
        ++report.resolved;
    }

    std::string environment_or_default(const char* variable_name, const char* fallback)
    {
        const char* value = std::getenv(variable_name);
        if (value != nullptr && value[0] != '\0')
            return std::string(value);
        return std::string(fallback);
    }

    std::wstring to_wide(const std::string& text)
    {
        if (text.empty())
            return std::wstring();
        const int size = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
            static_cast<int>(text.size()), nullptr, 0);
        std::wstring wide(static_cast<size_t>(size), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
            &wide[0], size);
        return wide;
    }
}//end anonymous namespace

namespace EMTG
{
    namespace Solvers
    {
        SouffleApi::SouffleApi()
        {
            this->uno_root = environment_or_default("SOUFFLE_UNO_ROOT", SOUFFLE_UNO_ROOT_DEFAULT);

            const std::string bin_dir  = this->uno_root + "\\bin";
            const std::string deps_dir = this->uno_root + "\\deps";
            const std::string dll_path = bin_dir + "\\libuno.dll";

            //Restrict DLL resolution to the application directory plus directories we add
            //explicitly. This prevents a stale libuno/dependency on PATH from being picked up,
            //which previously produced a silent 0xC0000139 (STATUS_ENTRYPOINT_NOT_FOUND) exit.
            ::SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            ::AddDllDirectory(to_wide(bin_dir).c_str());
            ::AddDllDirectory(to_wide(deps_dir).c_str());

            HMODULE module = ::LoadLibraryExW(to_wide(dll_path).c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);

            if (module == nullptr)
            {
                std::ostringstream message;
                message << "SOUFFLE: failed to load '" << dll_path
                        << "' (GetLastError=" << ::GetLastError() << "). "
                        << "Set SOUFFLE_UNO_ROOT to the Uno installation directory.";
                this->error_message = message.str();
                return;
            }

            ResolveReport report;

            resolve(this->get_version, module, "uno_get_version", report);

            resolve(this->create_model, module, "uno_create_model", report);
            resolve(this->destroy_model, module, "uno_destroy_model", report);

            resolve(this->set_model_name, module, "uno_set_model_name", report);
            resolve(this->set_objective, module, "uno_set_objective", report);
            resolve(this->set_constraints, module, "uno_set_constraints", report);
            resolve(this->set_initial_primal_iterate, module, "uno_set_initial_primal_iterate", report);
            resolve(this->set_user_data, module, "uno_set_user_data", report);

            resolve(this->create_solver, module, "uno_create_solver", report);
            resolve(this->destroy_solver, module, "uno_destroy_solver", report);

            resolve(this->set_solver_integer_option, module, "uno_set_solver_integer_option", report);
            resolve(this->set_solver_double_option, module, "uno_set_solver_double_option", report);
            resolve(this->set_solver_bool_option, module, "uno_set_solver_bool_option", report);
            resolve(this->set_solver_string_option, module, "uno_set_solver_string_option", report);
            resolve(this->set_solver_preset, module, "uno_set_solver_preset", report);
            resolve(this->set_solver_callbacks, module, "uno_set_solver_callbacks", report);

            resolve(this->optimize, module, "uno_optimize", report);

            resolve(this->get_optimization_status, module, "uno_get_optimization_status", report);
            resolve(this->get_solution_status, module, "uno_get_solution_status", report);
            resolve(this->get_solution_objective, module, "uno_get_solution_objective", report);
            resolve(this->get_primal_solution, module, "uno_get_primal_solution", report);
            resolve(this->get_solution_primal_feasibility, module, "uno_get_solution_primal_feasibility", report);
            resolve(this->get_solution_stationarity, module, "uno_get_solution_stationarity", report);
            resolve(this->get_solution_complementarity, module, "uno_get_solution_complementarity", report);
            resolve(this->get_number_iterations, module, "uno_get_number_iterations", report);
            resolve(this->get_cpu_time, module, "uno_get_cpu_time", report);
            resolve(this->get_number_objective_evaluations, module, "uno_get_number_objective_evaluations", report);
            resolve(this->get_number_constraint_evaluations, module, "uno_get_number_constraint_evaluations", report);
            resolve(this->get_number_objective_gradient_evaluations, module, "uno_get_number_objective_gradient_evaluations", report);
            resolve(this->get_number_jacobian_evaluations, module, "uno_get_number_jacobian_evaluations", report);
            resolve(this->get_method_description, module, "uno_get_method_description", report);

            if (!report.missing.empty())
            {
                std::ostringstream message;
                message << "SOUFFLE: " << dll_path << " is missing " << report.missing.size()
                        << " expected symbol(s):";
                for (size_t index = 0; index < report.missing.size(); ++index)
                    message << ' ' << report.missing[index];
                this->error_message = message.str();
                return;
            }

            this->loaded = true;
        }

        const SouffleApi& SouffleApi::instance()
        {
            static SouffleApi singleton;
            return singleton;
        }
    }//end namespace Solvers
}//end namespace EMTG
