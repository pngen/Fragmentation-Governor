# Reference coordinator and worker executables (used by the multiprocess proof).
add_executable(fg_coordinator fg_coordinator.cpp)
target_link_libraries(fg_coordinator PRIVATE FragmentationGovernor)
add_executable(fg_worker fg_worker.cpp)
target_link_libraries(fg_worker PRIVATE FragmentationGovernor)

# CLI inspection tool.
add_executable(fg_inspect fg_inspect.cpp)
target_link_libraries(fg_inspect PRIVATE FragmentationGovernor)

if(WIN32)
  target_link_libraries(fg_coordinator PRIVATE ws2_32)
  target_link_libraries(fg_worker PRIVATE ws2_32)
endif()
