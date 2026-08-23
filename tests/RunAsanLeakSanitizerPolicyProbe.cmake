if(NOT DEFINED SHUBA_ASAN_LSAN_POLICY_PROBE)
	message(FATAL_ERROR "SHUBA_ASAN_LSAN_POLICY_PROBE must name the policy probe executable")
endif()

execute_process(
	COMMAND "${SHUBA_ASAN_LSAN_POLICY_PROBE}"
	RESULT_VARIABLE shuba_lsan_probe_result
	OUTPUT_VARIABLE shuba_lsan_probe_stdout
	ERROR_VARIABLE shuba_lsan_probe_stderr)

set(shuba_lsan_probe_output
	"${shuba_lsan_probe_stdout}\n${shuba_lsan_probe_stderr}")

if(shuba_lsan_probe_result EQUAL 0)
	message(FATAL_ERROR
		"The LeakSanitizer policy probe unexpectedly succeeded; unrelated project leaks must fail the ASAN lane.")
endif()

string(REGEX MATCH "LeakSanitizer: detected memory leaks"
	shuba_lsan_probe_leak_report
	"${shuba_lsan_probe_output}")

if(NOT shuba_lsan_probe_leak_report)
	message(FATAL_ERROR
		"The LeakSanitizer policy probe failed without the required leak diagnostic:\n${shuba_lsan_probe_output}")
endif()

message(STATUS "LeakSanitizer policy probe rejected an unrelated project leak as required")
