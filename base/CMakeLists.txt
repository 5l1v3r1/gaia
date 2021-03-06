add_library(base arena.cc bits.cc crc32c.cc coder.cc hash.cc histogram.cc
            init.cc logging.cc simd.cc varint.cc walltime.cc pthread_utils.cc)
cxx_link(base TRDP::glog TRDP::gflags TRDP::pmr TRDP::xxhash atomic rt
         absl_symbolize absl_failure_signal_handler)  # rt for timer_create etc.
add_dependencies(base sparsehash_project)

cxx_test(array_test base LABELS CI)
cxx_test(bits_test LABELS CI)
cxx_test(pod_array_test base LABELS CI)
cxx_test(arena_test base strings LABELS CI)
cxx_test(pmr_test base TRDP::pmr LABELS CI)
cxx_test(simd_test base LABELS CI)
cxx_test(crc32c_test base strings LABELS CI)
cxx_test(walltime_test base LABELS CI)
cxx_test(flit_test base strings LABELS CI)
cxx_test(cxx_test base LABELS CI)
cxx_test(hash_test base file DATA testdata/ids.txt.gz LABELS CI)
cxx_test(RWSpinLock_test base LABELS CI)
cxx_test(event_count_test base LABELS CI)
cxx_test(coder_test base LABELS CI)
cxx_test(wheel_timer_test base LABELS CI)
cxx_test(lambda_test base LABELS CI)
cxx_test(mpmc_bounded_queue_test base LABELS CI)



find_package(LibXml2) # libxml2 package
include_directories(${LIBXML2_INCLUDE_DIR})
cxx_test(xml_test base absl_str_format ${LIBXML2_LIBRARIES})

# Define default gtest_main for tests.
add_library(gaia_gtest_main gtest_main.cc)
target_link_libraries(gaia_gtest_main TRDP::glog TRDP::gflags TRDP::gtest base TRDP::benchmark)

