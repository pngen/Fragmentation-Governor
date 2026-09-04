# Fragmentation Governor tests.

add_executable(fg_core_tests unit/fg_core_tests.cpp)
target_link_libraries(fg_core_tests PRIVATE FragmentationGovernor)
add_test(NAME fg_core_tests COMMAND fg_core_tests)

add_executable(fg_concurrency_test concurrency/fg_concurrency_test.cpp)
target_link_libraries(fg_concurrency_test PRIVATE FragmentationGovernor)
add_test(NAME fg_concurrency_test COMMAND fg_concurrency_test)

add_executable(fg_multiprocess_proof multiprocess/multiprocess_proof.cpp)
target_link_libraries(fg_multiprocess_proof PRIVATE FragmentationGovernor)
if(WIN32)
  target_link_libraries(fg_multiprocess_proof PRIVATE ws2_32)
endif()
add_test(NAME fg_multiprocess_proof COMMAND fg_multiprocess_proof
         $<TARGET_FILE:fg_coordinator> $<TARGET_FILE:fg_worker>)

# Adversarial / hardening tests reuse the core test binary with an env flag; we
# keep them as a dedicated executable for clarity.
add_executable(fg_adversarial_test adversarial/fg_adversarial_test.cpp)
target_link_libraries(fg_adversarial_test PRIVATE FragmentationGovernor)
add_test(NAME fg_adversarial_test COMMAND fg_adversarial_test)
