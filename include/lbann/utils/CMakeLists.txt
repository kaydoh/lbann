# Add the headers for this directory
set_full_path(THIS_DIR_HEADERS
  any.hpp
  argument_parser.hpp
  beta.hpp
  cloneable.hpp
  commify.hpp
  compiler_control.hpp
  cyg_profile.hpp
  dataset.hpp
  describable.hpp
  description.hpp
  dim_helpers.hpp
  dnn_enums.hpp
  entrywise_operator.hpp
  enum_iterator.hpp
  environment_variable.hpp
  exception.hpp
  factory.hpp
  factory_error_policies.hpp
  file_utils.hpp
  from_string.hpp
  glob.hpp
  graph.hpp
  hash.hpp
  hydrogen_utils.hpp
  im2col.hpp
  jag_utils.hpp
  lbann_library.hpp
  make_abstract.hpp
  memory.hpp
  mild_exception.hpp
  number_theory.hpp
  numerical_traits.hpp
  nvshmem.hpp
  omp_diagnostics.hpp
  omp_pragma.hpp
  options.hpp
  peek_map.hpp
  profiling.hpp
  protobuf_serializable.hpp
  protobuf_utils.hpp
  python.hpp
  random.hpp
  random_number_generators.hpp
  serialize.hpp
  stack_profiler.hpp
  stack_trace.hpp
  statistics.hpp
  summary.hpp
  summary_impl.hpp
  sync_info_helpers.hpp
  system_info.hpp
  tensor.hpp
  tensor_impl.hpp
  timer.hpp
  trainer_file_utils.hpp
  type_erased_matrix.hpp
  typename.hpp
  vector_dynamic_cast.hpp
  vectorwrapbuf.hpp
  vision.hpp
  visitor_hooks.hpp
)
if (LBANN_HAS_FFTW)
  list(APPEND THIS_DIR_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/fft_common.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/fftw_wrapper.hpp")
  if (LBANN_HAS_CUDA)
    list(APPEND THIS_DIR_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/cufft_wrapper.hpp")
  endif ()
endif (LBANN_HAS_FFTW)

if (LBANN_HAS_HALF)
  list(APPEND THIS_DIR_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/serialize.hpp")
endif (LBANN_HAS_HALF)

if (LBANN_HAS_DISTCONV)
  list(APPEND THIS_DIR_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/distconv.hpp")
endif ()

if (LBANN_HAS_OPENCV)
  list(APPEND THIS_DIR_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/opencv.hpp")
  list(APPEND THIS_DIR_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/image.hpp")
endif ()

if (LBANN_HAS_CNPY)
  list(APPEND THIS_DIR_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/cnpy_utils.hpp")
endif ()

# Add the subdirectories
add_subdirectory(threads)
add_subdirectory(impl)

# Propagate the files up the tree
set(HEADERS "${HEADERS}" "${THIS_DIR_HEADERS}" PARENT_SCOPE)
