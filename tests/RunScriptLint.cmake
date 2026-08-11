# Dump every generated Python script and run the static checker over them.
#
# A two-step test needs a driver, and a CMake script is the portable one: the
# dump has to happen into a fresh directory (a stale script from a previous
# build would be checked as though it were current) and the checker's exit
# status has to become the test's.
file(REMOVE_RECURSE "${WORKDIR}")
file(MAKE_DIRECTORY "${WORKDIR}")

execute_process(
    COMMAND "${SCRIPT_TEST}" --dump "${WORKDIR}"
    RESULT_VARIABLE dump_status
    OUTPUT_QUIET)
if(NOT dump_status EQUAL 0)
    message(FATAL_ERROR "dumping the generated scripts failed (${dump_status})")
endif()

execute_process(
    COMMAND "${PYTHON}" "${CHECKER}" "${WORKDIR}"
    RESULT_VARIABLE lint_status)
if(NOT lint_status EQUAL 0)
    message(FATAL_ERROR "generated scripts failed the static check")
endif()
