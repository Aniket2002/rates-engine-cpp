cmake_minimum_required(VERSION 3.24)
project(rates_engine VERSION 2.1.0 LANGUAGES CXX)
include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
option(RATES_BUILD_BENCHMARKS "Build measured benchmarks" ON)
option(RATES_BUILD_PYTHON "Build optional pybind11 interface" OFF)
option(RATES_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(RATES_CLANG_TIDY "Run clang-tidy while compiling" OFF)
option(RATES_COVERAGE "Instrument project code for LLVM line/branch coverage" OFF)
if(RATES_COVERAGE AND (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR MSVC))
  message(FATAL_ERROR "RATES_COVERAGE requires Clang with the GNU-style driver and matching LLVM tools")
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_library(rates_engine STATIC src/date.cpp src/conventions.cpp src/schedule.cpp
  src/discount_curve.cpp src/swap_pricer.cpp src/curve_bootstrapper.cpp src/risk_engine.cpp
  src/portfolio.cpp src/history.cpp src/trading.cpp src/multicurve.cpp src/sofr.cpp src/sofr_curve.cpp)
add_library(rates::rates_engine ALIAS rates_engine)
target_compile_features(rates_engine PUBLIC cxx_std_20)
set_target_properties(rates_engine PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
target_include_directories(rates_engine PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
function(rates_checks target)
  if(RATES_COVERAGE)
    target_compile_options(${target} PRIVATE -O0 -g -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(${target} PUBLIC -fprofile-instr-generate)
  endif()
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  if(RATES_SANITIZERS)
    if(MSVC OR WIN32)
      message(FATAL_ERROR "Combined ASan/UBSan mode is supported on GCC/Clang Linux and macOS only")
    endif()
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PUBLIC -fsanitize=address,undefined)
  endif()
  if(RATES_CLANG_TIDY)
    find_program(TIDY_EXECUTABLE clang-tidy REQUIRED)
    set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${TIDY_EXECUTABLE}")
  endif()
endfunction()
rates_checks(rates_engine)
add_executable(rates_cli apps/rates_cli.cpp)
target_link_libraries(rates_cli PRIVATE rates_engine)
rates_checks(rates_cli)
add_executable(rates_batch apps/rates_batch.cpp)
target_link_libraries(rates_batch PRIVATE rates_engine)
rates_checks(rates_batch)
add_executable(rates_simulate apps/rates_simulate.cpp)
target_link_libraries(rates_simulate PRIVATE rates_engine)
rates_checks(rates_simulate)
if(RATES_BUILD_BENCHMARKS)
  add_executable(rates_benchmark benchmarks/benchmark_main.cpp)
  target_link_libraries(rates_benchmark PRIVATE rates_engine)
  target_compile_definitions(rates_benchmark PRIVATE
    RATES_BUILD_CONFIG="$<CONFIG>" RATES_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}"
    RATES_PLATFORM="${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
  rates_checks(rates_benchmark)
  add_executable(rates_portfolio_benchmark benchmarks/portfolio_benchmark.cpp)
  target_link_libraries(rates_portfolio_benchmark PRIVATE rates_engine)
  target_compile_definitions(rates_portfolio_benchmark PRIVATE
    RATES_BUILD_CONFIG="$<CONFIG>" RATES_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}"
    RATES_PLATFORM="${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
  rates_checks(rates_portfolio_benchmark)
endif()
if(BUILD_TESTING)
  find_package(GTest CONFIG QUIET)
  if(NOT GTest_FOUND)
    include(FetchContent)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "Do not install test dependencies with the engine" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "Only GoogleTest is used" FORCE)
    FetchContent_Declare(googletest GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.17.0 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(googletest)
  endif()
  add_executable(rates_tests tests/test_engine.cpp tests/test_v2.cpp tests/test_trading.cpp tests/test_multicurve.cpp tests/test_sofr.cpp)
  target_link_libraries(rates_tests PRIVATE rates_engine GTest::gtest_main)
  rates_checks(rates_tests)
  include(GoogleTest)
  gtest_discover_tests(rates_tests DISCOVERY_MODE PRE_TEST)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME sofr_market_data_contract COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/python/test_sofr_market_data.py)
  if(RATES_BUILD_BENCHMARKS)
    add_test(NAME performance_contract COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/python/check_performance.py --benchmark $<TARGET_FILE:rates_portfolio_benchmark>
      --output ${CMAKE_CURRENT_BINARY_DIR}/performance-contract.csv)
  endif()
  add_test(NAME independent_python COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/python/validate_results.py --cli $<TARGET_FILE:rates_cli>
    --output ${CMAKE_CURRENT_BINARY_DIR}/validation)
  add_test(NAME cli_contract COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/python/test_cli.py $<TARGET_FILE:rates_cli>)
  add_test(NAME pv_change_case_study COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/python/generate_case_study.py --cli $<TARGET_FILE:rates_cli>
    --out ${CMAKE_CURRENT_BINARY_DIR}/case-study)
  add_test(NAME convention_validation COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/python/validate_conventions.py --cli $<TARGET_FILE:rates_cli>
    --output ${CMAKE_CURRENT_BINARY_DIR}/conventions)
  add_test(NAME batch_replay_contract COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/python/test_batch.py $<TARGET_FILE:rates_batch>)
  add_test(NAME real_historical_data COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/python/test_real_history.py --batch $<TARGET_FILE:rates_batch>
    --cli $<TARGET_FILE:rates_cli> --out ${CMAKE_CURRENT_BINARY_DIR}/historical-validation)
  add_test(NAME trading_contract COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/python/test_trading.py --simulator $<TARGET_FILE:rates_simulate>
    --report ${CMAKE_CURRENT_BINARY_DIR}/trading-validation.json)
  if(RATES_COVERAGE)
    find_program(LLVM_COV_EXECUTABLE NAMES llvm-cov REQUIRED)
    find_program(LLVM_PROFDATA_EXECUTABLE NAMES llvm-profdata REQUIRED)
    add_custom_target(coverage
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/python/measure_coverage.py
        --build-dir ${CMAKE_CURRENT_BINARY_DIR} --tests $<TARGET_FILE:rates_tests>
        --cli $<TARGET_FILE:rates_cli> --batch $<TARGET_FILE:rates_batch>
        --simulator $<TARGET_FILE:rates_simulate> --llvm-cov ${LLVM_COV_EXECUTABLE}
        --llvm-profdata ${LLVM_PROFDATA_EXECUTABLE}
      DEPENDS rates_tests rates_cli rates_batch rates_simulate USES_TERMINAL VERBATIM)
  endif()
endif()
if(RATES_BUILD_PYTHON)
  find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)
  set(PYBIND11_FINDPYTHON ON)
  find_package(pybind11 CONFIG REQUIRED)
  pybind11_add_module(pyrates python/bindings.cpp python/sofr_bindings.cpp)
  target_link_libraries(pyrates PRIVATE rates_engine)
  if(MINGW)
    # Python 3.8+ does not search PATH for extension-module DLL dependencies.
    # Embed the MinGW C++/unwind runtimes in this optional Python module.
    target_link_options(pyrates PRIVATE -static-libstdc++ -static-libgcc)
  endif()
  rates_checks(pyrates)
  if(BUILD_TESTING)
    add_test(NAME python_bindings COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/python/test_bindings.py $<TARGET_FILE_DIR:pyrates>)
    add_test(NAME multicurve_validation COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/python/validate_multicurve.py --module-dir $<TARGET_FILE_DIR:pyrates>
      --out ${CMAKE_CURRENT_BINARY_DIR}/multicurve-validation)
    add_test(NAME sofr_lifecycle_validation COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/python/validate_sofr.py --module-dir $<TARGET_FILE_DIR:pyrates>
      --out ${CMAKE_CURRENT_BINARY_DIR}/sofr-validation)
    add_test(NAME sofr_real_fixing_replay COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/python/test_sofr_replay.py --module-dir $<TARGET_FILE_DIR:pyrates>
      --out ${CMAKE_CURRENT_BINARY_DIR}/sofr-replay)
  endif()
endif()
install(TARGETS rates_engine EXPORT ratesEngineTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT ratesEngineTargets NAMESPACE rates:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rates_engine)
configure_package_config_file(cmake/rates_engineConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/rates_engineConfig.cmake INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rates_engine)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/rates_engineConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rates_engineConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/rates_engineConfigVersion.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rates_engine)
