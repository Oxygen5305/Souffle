
set(INNER_LOOP_HEADERS ${INNER_LOOP_HEADERS} ${CMAKE_CURRENT_SOURCE_DIR}/EMTG_innerloop_solution.h
                                             ${CMAKE_CURRENT_SOURCE_DIR}/EMTG_WORHP_interface.h
                                             ${CMAKE_CURRENT_SOURCE_DIR}/FilamentWalker.h
                                             ${CMAKE_CURRENT_SOURCE_DIR}/monotonic_basin_hopping.h
                                             ${CMAKE_CURRENT_SOURCE_DIR}/NLPoptions.h
                                             ${CMAKE_CURRENT_SOURCE_DIR}/NLP_interface.h
                                             ${CMAKE_CURRENT_SOURCE_DIR}/NLP_solver_factory.h
                                             ${CMAKE_CURRENT_SOURCE_DIR}/SouffleApi.h
                                             ${CMAKE_CURRENT_SOURCE_DIR}/Souffle_interface.h)

# SOUFFLE: SNOPT is optional. When SOUFFLE_WITH_SNOPT=OFF the SNOPT interface is not
# compiled at all, so no SNOPT header is needed and the executable does not import
# snopt7.dll (no license required anywhere).
if (SOUFFLE_WITH_SNOPT)
    set(INNER_LOOP_HEADERS ${INNER_LOOP_HEADERS} ${CMAKE_CURRENT_SOURCE_DIR}/SNOPT_interface.h
                                                 ${CMAKE_CURRENT_SOURCE_DIR}/snoptProblemExtension.h)
endif()

# SOUFFLE: Ipopt is optional and independent of the other two solvers. When
# SOUFFLE_WITH_IPOPT=OFF nothing about Ipopt is compiled or linked.
if (SOUFFLE_WITH_IPOPT)
    set(INNER_LOOP_HEADERS ${INNER_LOOP_HEADERS} ${CMAKE_CURRENT_SOURCE_DIR}/Ipopt_interface.h)
endif()

set(INNER_LOOP_SOURCE ${INNER_LOOP_SOURCE} ${CMAKE_CURRENT_SOURCE_DIR}/EMTG_innerloop_solution.cpp
								           ${CMAKE_CURRENT_SOURCE_DIR}/EMTG_WORHP_interface.cpp
                                           ${CMAKE_CURRENT_SOURCE_DIR}/FilamentWalker.cpp
                                           ${CMAKE_CURRENT_SOURCE_DIR}/monotonic_basin_hopping.cpp
                                           ${CMAKE_CURRENT_SOURCE_DIR}/NLPoptions.cpp
                                           ${CMAKE_CURRENT_SOURCE_DIR}/NLP_interface.cpp
                                           ${CMAKE_CURRENT_SOURCE_DIR}/NLP_solver_factory.cpp
                                           ${CMAKE_CURRENT_SOURCE_DIR}/SouffleApi.cpp
                                           ${CMAKE_CURRENT_SOURCE_DIR}/Souffle_interface.cpp)

if (SOUFFLE_WITH_SNOPT)
    set(INNER_LOOP_SOURCE ${INNER_LOOP_SOURCE} ${CMAKE_CURRENT_SOURCE_DIR}/SNOPT_interface.cpp)
endif()

# SOUFFLE: the Ipopt interface links against ipopt.lib (see SOUFFLE_WITH_IPOPT in the
# top-level CMakeLists.txt, which adds the include directory and the import library).
if (SOUFFLE_WITH_IPOPT)
    set(INNER_LOOP_SOURCE ${INNER_LOOP_SOURCE} ${CMAKE_CURRENT_SOURCE_DIR}/Ipopt_interface.cpp)
endif()

set(INNER_LOOP_FILES ${INNER_LOOP_FILES} ${INNER_LOOP_HEADERS} ${INNER_LOOP_SOURCE} PARENT_SCOPE)
    

    
