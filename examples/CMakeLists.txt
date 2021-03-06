add_executable(file_read file_read.cc)
cxx_link(file_read base TRDP::folly dl)

add_executable(asio_fibers asio_fibers.cc)
cxx_link(asio_fibers base http_v2 rpc sentry strings)

add_executable(http_client_tool http_client_tool.cc)
cxx_link(http_client_tool http_client_lib asio_fiber_lib)

add_executable(mr3 mr3.cc)
cxx_link(mr3 fiber_file asio_fiber_lib mr3_lib absl_hash absl_str_format http_v2)

add_executable(gcs_demo gcs_demo.cc)
cxx_link(gcs_demo gce_lib)

add_executable(s3_demo s3_demo.cc)
cxx_link(s3_demo file https_client_lib aws_lib)

add_executable(gsod_group gsod_group.cc)
cxx_link(gsod_group mr3_lib absl_hash absl_str_format http_v2)

add_executable(movies_join movies_join.cc)
cxx_link(movies_join mr3_lib absl_hash absl_str_format http_v2 TRDP::re2)

add_executable(mr_read_test mr_read_test.cc)
cxx_link(mr_read_test mr3_lib http_v2)

add_executable(mrgrep mrgrep.cc)
cxx_link(mrgrep mr3_lib http_v2 TRDP::re2)

add_executable(osmium_road_length osmium_road_length.cc)
cxx_link(osmium_road_length base TRDP::libosmium)

add_subdirectory(wordcount)
add_subdirectory(pingserver)
add_subdirectory(redis)
