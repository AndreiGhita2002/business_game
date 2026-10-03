# Fails when `float` or `double` appears anywhere under SIM_DIR.
#
# Run by CTest (sim_uses_no_floating_point in CMakeLists.txt). The simulation
# keeps every fractional value in sim::Fixed so that it comes out the same on
# every machine; this catches the most likely way for that to break. It matches
# whole words in comments as well as code, so the simulation's comments say
# "fractional" or "hardware maths" rather than naming the types.

file(GLOB_RECURSE sim_files "${SIM_DIR}/*.hpp" "${SIM_DIR}/*.cpp")

set(offenders "")
foreach(path IN LISTS sim_files)
    file(STRINGS "${path}" lines REGEX "(^|[^A-Za-z0-9_])(float|double)([^A-Za-z0-9_]|$)")
    if(lines)
        list(APPEND offenders "${path}")
    endif()
endforeach()

if(offenders)
    list(JOIN offenders "\n  " listed)
    message(FATAL_ERROR "float or double used in the simulation:\n  ${listed}")
endif()
