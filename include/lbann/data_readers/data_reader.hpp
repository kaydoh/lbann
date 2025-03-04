////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2014-2021, Lawrence Livermore National Security, LLC.
// Produced at the Lawrence Livermore National Laboratory.
// Written by the LBANN Research Team (B. Van Essen, et al.) listed in
// the CONTRIBUTORS file. <lbann-dev@llnl.gov>
//
// LLNL-CODE-697807.
// All rights reserved.
//
// This file is part of LBANN: Livermore Big Artificial Neural Network
// Toolkit. For details, see http://software.llnl.gov/LBANN or
// https://github.com/LLNL/LBANN.
//
// Licensed under the Apache License, Version 2.0 (the "Licensee"); you
// may not use this file except in compliance with the License.  You may
// obtain a copy of the License at:
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the license.
//
// lbann_data_reader .hpp - Input data base class for training, testing
////////////////////////////////////////////////////////////////////////////////

#ifndef LBANN_DATA_READER_HPP
#define LBANN_DATA_READER_HPP

#include "lbann/base.hpp"
#include "lbann/comm.hpp"
#include "lbann/data_coordinator/data_coordinator_metadata.hpp"
#include "lbann/data_readers/utils/input_data_type.hpp"
#include "lbann/io/file_io.hpp"
#include "lbann/io/persist.hpp"
#include "lbann/transforms/transform_pipeline.hpp"
#include "lbann/utils/argument_parser.hpp"
#include "lbann/utils/distconv.hpp"
#include "lbann/utils/exception.hpp"
#include "lbann/utils/options.hpp"
#include "lbann/utils/random_number_generators.hpp"

#include <cassert>
#include <algorithm>
#include <string>
#include <vector>
#include <unistd.h>
#include <unordered_set>

#define NOT_IMPLEMENTED(n) { \
  std::stringstream s; \
  s << "the method " << n << " has not been implemented"; \
  throw lbann_exception(s.str()); }

namespace lbann {

class data_store_conduit;
class thread_pool;
class trainer;

/**
 * A data reader manages reading in data in a particular format.
 * This abstract base class manages common functionality. In particular, child
 * classes should implement load and the appropriate subset of fetch_datum,
 * fetch_label, and fetch_response.
 */
class generic_data_reader {
 public:
  using unused_index_map_t = std::map<execution_mode,std::vector<int>>;

  /**
   * ctor
   */
  generic_data_reader(bool shuffle = true)
    : m_verbose(global_argument_parser().get<bool>(LBANN_OPTION_VERBOSE)),
      m_data_store(nullptr),
      m_comm(nullptr),
      m_mini_batch_size(0),
      m_current_pos(0),
      m_stride_to_next_mini_batch(0),
      m_base_offset(0),
      m_model_offset(0),
      m_sample_stride(1),
      m_iteration_stride(1),
      m_last_mini_batch_size(0),
      m_stride_to_last_mini_batch(0),
      m_reset_mini_batch_index(0),
      m_loaded_mini_batch_idx(0),
      m_current_mini_batch_idx(0),
      m_num_iterations_per_epoch(0),
      m_global_mini_batch_size(0),
      m_global_last_mini_batch_size(0),
      m_world_master_mini_batch_adjustment(0),
      m_num_parallel_readers(0),
      m_max_files_to_load(0),
      m_file_dir(""),
      m_data_sample_list(""),
      m_data_fn(""),
      m_label_fn(""),
      m_shuffle(shuffle),
      m_absolute_sample_count(0),
      m_use_percent(1.0),
      m_gan_labelling(false), //default, not GAN
      m_gan_label_value(0),  //If GAN, default for fake label, discriminator model
      m_io_thread_pool(nullptr),
      m_keep_sample_order(false),
      m_issue_warning(true)
  {
    // By default only support fetching input samples
    m_supported_input_types[INPUT_DATA_TYPE_SAMPLES] = true;
  }
  generic_data_reader(const generic_data_reader&) = default;
  generic_data_reader& operator=(const generic_data_reader&) = default;

  virtual ~generic_data_reader();
  virtual generic_data_reader* copy() const = 0;

  /** Archive for checkpoint and restart */
  template <class Archive> void serialize( Archive & ar );

  /// set the comm object
  void set_comm(lbann_comm *comm) {
    m_comm = comm;
  }

  /// returns a (possibly nullptr) to comm
  lbann_comm * get_comm() const {
    return m_comm;
  }

  // These non-virtual methods are used to specify where data is, how much to
  // load, etc.

  /**
   * Set base directory for your data.
   */
  void set_file_dir(std::string s);

  /**
   * Set base directory for your locally cached (e.g, on ssd) data.
   */
  void set_local_file_dir(std::string s);

  /**
   * for some data readers (jag_conduit) we load from multiple files;
   * for testing we want to be able to restrict that number
   */
  void set_max_files_to_load(size_t n) {
    m_max_files_to_load = n;
  }

  /**
   * Returns the base directory for your data.
   * If set_file_dir was not called, returns the empty string
   */
  std::string get_file_dir() const;

  /**
   * Returns the base directory for caching files in local ssd
   * If set_local_file_dir was not called, returns the empty string
   */
  std::string get_local_file_dir() const;

  /**
   * Set the sample list for your data (images, etc).
   * The sample lists contains an enumeration of all samples in the
   * data set.
   */
  void set_data_sample_list(std::string s);

  /**
   * Returns the complete sample list for your data set.
   */
  std::string get_data_sample_list() const;

  /**
   * To facilictate the testing, maintain the order of loaded samples
   * in the sample list as it is in the list file.
   */
  void keep_sample_order(bool same_order = false);

  /**
   * Set the filename for your data (images, etc).
   * This may either be a complete filepath, or a subdirectory;
   * see note for set_file_dir(). Also, use this method
   * for cases where the file contains a list of files (e.g, imagenet)
   */
  void set_data_filename(std::string s);

  /**
   * Returns the complete filepath to you data file.
   * See not for set_file_dir()
   */
  std::string get_data_filename() const;

  /**
   * Set the filename for your data (images, etc).
   * This may either be a complete filepath, or a subdirectory;
   * see note for set_file_dir()
   */
  void set_label_filename(std::string s);

  /**
   * Returns the complete filepath to you data file.
   * See not for set_file_dir(). Note: some pipelines (autoencoders)
   * will not make use of this method.
   */
  std::string get_label_filename() const;

  /**
   * If set to false, indices (data samples) are not shuffled
   * default (in ctor) is true.
   */
  void set_shuffle(bool b) { m_shuffle = b; }

  /**
   * Returns true if data samples are shuffled.
   */
  bool is_shuffled() const { return m_shuffle; }

  /**
   * Set shuffled indices; primary use is for testing
   * and reproducibility
   */
  void set_shuffled_indices(const std::vector<int> &indices) {
    m_shuffled_indices = indices;
  }

  /**
   * Returns the shuffled indices; primary use is for testing.
   */
  const std::vector<int> & get_shuffled_indices() const {
    return m_shuffled_indices;
  }

  /**
   * Read the first 'n' samples. If nonzero, this over-rides
   * set_absolute_sample_count, set_use_percent. The intent
   * is to use this for testing. A problem with set_absolute_sample_count
   * and set_use_percent is that the entire data set is read in, then
   * a subset is selected
   */
  void set_first_n(int n);

  /**
   * Sets the absolute number of data samples that will be used for training or
   * testing.
   */
  void set_absolute_sample_count(size_t s);

  /**
   * Set the percentage of the data set to use for training and validation or
   * testing.
   * @param s The percentage used, in the range [0, 1].
   */
  void set_use_percent(double s);

  /**
   * Sets the percentage of the dataset to be used for validation.
   * @param m The execution mode.
   * @param s The percentage used, in the range [0, 1].
   */
  virtual void set_execution_mode_split_percent(execution_mode m, double s);

  /**
   * Set an idenifier for the dataset.
   * The role should be one of "train", "test", or "validate".
   */
  virtual void set_role(std::string role);

  /**
   * Get the role for this dataset.
   */
  std::string get_role() const {
    return m_role;
  }

  /**
   * Load the dataset.
   * Each data reader implementation should implement this to initialize its
   * internal data structures, determine the number of samples and their
   * dimensionality (if needed), and set up and shuffle samples.
   */
  virtual void load() = 0;

  /**
   * Prepare to start processing an epoch of data.
   * If shuffle is true, then shuffle the indices of the data set
   * If the base offset is not specified set it to 0
   * If the stride is not specified set it to batch size
   */
  virtual void setup(int num_io_threads, observer_ptr<thread_pool> io_thread_pool);

  /** Return this data_reader's type */
  virtual std::string get_type() const = 0;

  /** @brief Fetch a mini-batch worth of data, including samples, labels, responses (as appropriate) */
  int fetch(std::map<data_field_type, CPUMat*>& input_buffers,
            El::Matrix<El::Int>& indices_fetched, size_t mb_size);

  /** @brief Check to see if the data reader supports this specific data field
   */
  virtual bool has_data_field(data_field_type data_field) const
  {
    if (m_supported_input_types.find(data_field) !=
        m_supported_input_types.end()) {
      return m_supported_input_types.at(data_field);
    }
    else {
      return false;
    }
  }

  virtual bool has_labels() const
  {
    return has_data_field(INPUT_DATA_TYPE_LABELS);
  }
  virtual bool has_responses() const
  {
    return has_data_field(INPUT_DATA_TYPE_RESPONSES);
  }

  /// Whether or not a data reader has a data field
  void set_has_data_field(data_field_type const data_field, const bool b)
  {
    m_supported_input_types[data_field] = b;
  }

  /// Whether or not a data reader has labels
  virtual void set_has_labels(const bool b)
  {
    m_supported_input_types[INPUT_DATA_TYPE_LABELS] = b;
  }
  /// Whether or not a data reader has a response field
  virtual void set_has_responses(const bool b)
  {
    m_supported_input_types[INPUT_DATA_TYPE_RESPONSES] = b;
  }

  /**
   * During the network's update phase, the data reader will
   * advanced the current position pointer.  If the pointer wraps
   * around, then reshuffle the data indicies.
   */
  virtual bool update(bool is_active_reader);

  /**
   * This is called at the end of update; it permits data readers to
   * perform actions that are specific to their data sets, for example,
   * data_reader_jag_conduit_hdf5 has the 'primary' data reader
   * bcast its shuffled indices to the other data readers. In general
   * most data readers will probably not overide this method.
   * It may also be called outside of update.
   */

  /// Return the number of labels (classes) in this dataset.
  virtual int get_num_labels() const {
    return 0;
  }
  /// Return the number of responses in this dataset.
  virtual int get_num_responses() const {
    return 1;
  }
  /// Get the linearized size (i.e. number of elements) in a sample.
  virtual int get_linearized_data_size() const {
    return 0;
  }
  /// Get the linearized size (i.e. number of elements) in a label.
  virtual int get_linearized_label_size() const {
    return 0;
  }
  /// Get the linearized size (i.e. number of elements) in a response.
  virtual int get_linearized_response_size() const {
    return 1;
  }
  /// get the linearized size of what is identified by desc.
  virtual int get_linearized_size(data_field_type const& data_field) const
  {
    if (data_field == INPUT_DATA_TYPE_SAMPLES) {
      return get_linearized_data_size();
    }
    else if (data_field == INPUT_DATA_TYPE_LABELS) {
      return get_linearized_label_size();
    }
    else if (data_field == INPUT_DATA_TYPE_RESPONSES) {
      return get_linearized_response_size();
    }
    else {
      LBANN_ERROR("Unknown data_field_type value provided: " + data_field);
    }
    return 0;
  }
  /// Get the dimensions of the data.
  virtual const std::vector<int> get_data_dims() const {
    return std::vector<int>(0);
  }

  virtual std::vector<El::Int> get_slice_points(const slice_points_mode var_category,
                                                bool& is_supported) {
    is_supported = false;
    return {};
  }

  /// True if the data reader's current position is valid.
  virtual bool position_valid() const {
    return (m_current_pos < get_num_data());
  }
  /// True if the data reader's current position is not valid but within # ranks per model
  /// of the end of the data set (e.g. it is a rank with no valid data on the last iteration)
  virtual bool position_is_overrun() const {
    int end_pos = (int)m_shuffled_indices.size();
    return (m_current_pos >= end_pos && (m_current_pos - end_pos) < m_comm->get_procs_per_trainer());
  }
  /// True if the data reader is at the start of an epoch.
  bool at_new_epoch() const {
    /// Note that data readers can start at a non-zero index if there
    /// are parallel data readers in a model
    return ((m_loaded_mini_batch_idx == m_reset_mini_batch_index)
            && (m_current_mini_batch_idx == 0));
  }
  /// Set the mini batch size
  void set_mini_batch_size(const int s);
  /// Get the mini batch size
  int get_mini_batch_size() const {
    return m_mini_batch_size;
  }
  /// Get the loaded mini-batch size
  int get_loaded_mini_batch_size() const;
  /// Get the current mini-batch size.
  int get_current_mini_batch_size() const;
  /// Get the current global mini-batch size.
  int get_current_global_mini_batch_size() const;
  /// Get the current mini-batch size.
  int get_current_world_master_mini_batch_adjustment(int model_rank) const;
  /// Return the full mini_batch_size.
  int get_mini_batch_max() const {
    return m_mini_batch_size;
  }
  /// Set the mini batch size across all models (global)
  void set_global_mini_batch_size(const int s) {
    m_global_mini_batch_size = s;
  }
  /// Return the mini_batch_size across all models (global)
  int get_global_mini_batch_size() const {
    return m_global_mini_batch_size;
  }
  /// Set the mini batch stride
  void set_stride_to_next_mini_batch(const int s) {
    m_stride_to_next_mini_batch = s;
  }
  /// Return the mini batch stride.
  int get_stride_to_next_mini_batch() const {
    return m_stride_to_next_mini_batch;
  }
  /// Set the sample stride
  void set_sample_stride(const int s) {
    m_sample_stride = s;
  }
  /// Return the sample stride.
  int get_sample_stride() const {
    return m_sample_stride;
  }
  /// Set the iteration stride
  void set_iteration_stride(const int s) {
    m_iteration_stride = s;
  }
  /// Return the iteration stride.
  int get_iteration_stride() const {
    return m_iteration_stride;
  }
  /// Return the base offset.
  virtual void set_base_offset(const int s) {
    m_base_offset = s;
  }
  /// Return the base offset.
  int get_base_offset() const {
    return m_base_offset;
  }
  /// Set the model offset
  void set_model_offset(const int s) {
    m_model_offset = s;
  }
  /// Return the model offset.
  int get_model_offset() const {
    return m_model_offset;
  }
  /// Set the last mini batch size
  void set_last_mini_batch_size(const int s) {
    m_last_mini_batch_size = s;
  }
  /// Return the last mini batch size
  int get_last_mini_batch_size() const {
    return m_last_mini_batch_size;
  }
  /// Set the last mini batch size across all models (global)
  void set_global_last_mini_batch_size(const int s) {
    m_global_last_mini_batch_size = s;
  }
  /// Return the last mini batch size across all models (global)
  int get_global_last_mini_batch_size() const {
    return m_global_last_mini_batch_size;
  }
  /// Set the world master mini batch adjustment (global)
  void set_world_master_mini_batch_adjustment(const int s) {
    m_world_master_mini_batch_adjustment = s;
  }
  /// Return the world master mini batch adjustment (global)
  int get_world_master_mini_batch_adjustment() const {
    return m_world_master_mini_batch_adjustment;
  }
  /// Set the last mini batch stride
  void set_stride_to_last_mini_batch(const int s) {
    m_stride_to_last_mini_batch = s;
  }
  /// Return the last mini batch stride
  int get_stride_to_last_mini_batch() const {
    return m_stride_to_last_mini_batch;
  }
  /// Set the number of parallel readers per model
  void set_num_parallel_readers(const int s) {
    m_num_parallel_readers = s;
  }
  /// Return the number of parallel readers per model
  int get_num_parallel_readers() const {
    return m_num_parallel_readers;
  }
  /// Set the starting mini-batch index for the epoch
  virtual void set_reset_mini_batch_index(const int s) {
    m_reset_mini_batch_index = s;
  }
  /// Return the starting mini-batch index for the epoch
  int get_reset_mini_batch_index() const {
    return m_reset_mini_batch_index;
  }
  /// Return the current mini-batch index for the epoch
  int get_loaded_mini_batch_index() const {
    return m_loaded_mini_batch_idx;
  }
  /// Return the current mini-batch index for the epoch
  int get_current_mini_batch_index() const {
    return m_current_mini_batch_idx;
  }
  /// Set the current position based on the base and model offsets
  void set_initial_position() {
    m_current_pos = m_base_offset + m_model_offset;
    m_loaded_mini_batch_idx = m_reset_mini_batch_index;
    m_current_mini_batch_idx = 0;
  }
  /// Get the current position in the data reader.
  int get_position() const {
    return m_current_pos;
  }
  /// Get the next position in the data reader.
  int get_next_position() const;
  /// Get a pointer to the start of the shuffled indices.
  int *get_indices() {
    return &m_shuffled_indices[0];
  }
  /// Get the number of samples in this dataset.
  virtual int get_num_data() const {
    return (int)m_shuffled_indices.size();
  }
  /// Get the number of unused samples in this dataset.
  int get_num_unused_data(execution_mode m) const {
    if(m_unused_indices.count(m)) {
      return (int)m_unused_indices.at(m).size();
    }else {
      LBANN_ERROR("Invalid execution mode ", to_string(m), " for unused indices");
    }
  }
  /// Get a pointer to the start of the unused sample indices.
  int *get_unused_data(execution_mode m) {
    if(m_unused_indices.count(m)) {
      return &(m_unused_indices[m][0]);
    }else {
      LBANN_ERROR("Invalid execution mode ", to_string(m), " for unused indices");
    }
  }
  const std::vector<int>& get_unused_indices(execution_mode m) {
    if(m_unused_indices.count(m)) {
      return m_unused_indices.at(m);
    }else {
      LBANN_ERROR("Invalid execution mode ", to_string(m), " for unused indices");
    }
  }
  /// Set the number of iterations in each epoch.
  void set_num_iterations_per_epoch(int num_iterations_per_epoch) {
    m_num_iterations_per_epoch = num_iterations_per_epoch;  /// @todo BVE FIXME merge this with alternate approach
  }
  /// Get the number of iterations in each epoch.
  int get_num_iterations_per_epoch() const {
    return m_num_iterations_per_epoch;  /// @todo BVE FIXME merge this with alternate approach
  }

  /// Return the index of the current iteration step in the epoch (also the mini-batch index)
  int get_current_step_in_epoch() const {
    return  m_current_mini_batch_idx;
  }

  /**
   * Optionally resizes the shuffled indices based on the data reader
   * prototext settings: absolute_sample_count, percent_of_data_to_use.
   * (dah - this was formerly part of select_subset_of_data)
   */
  void resize_shuffled_indices();

  /**
   * Select the appropriate subset of data for the additional
   * execution modes such as validation or tournament  set based on
   * the data reader prototext setting: validation_percent or
   * tournament_percent
   */
  void select_subset_of_data();

  /**
   * Replaced the shuffled index set with the unused index set, empying the
   * unused set.
   */
  virtual void use_unused_index_set(execution_mode m);

  /// Does the data reader have a unqiue sample list per model
  virtual bool has_list_per_model() const { return false; }
  /// Does the data reader have a unqiue sample list per trainer
  virtual bool has_list_per_trainer() const { return false; }


  /** \brief Given directory to store checkpoint files, write state to file and add to number of bytes written */
  bool save_to_checkpoint_shared(persist& p, execution_mode mode);

  /** \brief Given directory to store checkpoint files, read state from file and add to number of bytes read */
  bool load_from_checkpoint_shared(persist& p, execution_mode mode);

  bool save_to_checkpoint_distributed(persist& p, execution_mode mode);

  /** \brief Given directory to store checkpoint files, read state from file and add to number of bytes read */
  bool load_from_checkpoint_distributed(persist& p, execution_mode mode);

  /// returns a const ref to the data store
  const data_store_conduit& get_data_store() const {
    if (m_data_store == nullptr) {
      LBANN_ERROR("m_data_store is nullptr");
    }
    return *m_data_store;
  }

  /// returns a non-const ref to the data store
  data_store_conduit& get_data_store() {
    return const_cast<data_store_conduit&>(
      static_cast<const generic_data_reader&>(*this).get_data_store());
  }

  data_store_conduit* get_data_store_ptr() const {
    return m_data_store;
  }

  /// sets up a data_store; this is called from build_model_from_prototext()
  /// in utils/lbann_library.cpp. This is a bit awkward: would like to call it
  /// when we instantiate the data_store, but we don;t know the mini_batch_size
  /// until later.
  void setup_data_store(int mini_batch_size);

  void instantiate_data_store();

  virtual void preload_data_store();

  void set_gan_labelling(bool has_gan_labelling) {
     m_gan_labelling = has_gan_labelling;
  }
  void set_gan_label_value(int gan_label_value) { m_gan_label_value = gan_label_value; }

  /// support of data store functionality
  void set_data_store(data_store_conduit *g);

  virtual bool data_store_active() const;

  virtual bool priming_data_store() const;

  /// experimental; used to ensure all readers for jag_conduit_hdf5
  /// have identical shuffled indices
  virtual void post_update() {}

  /** Set the transform pipeline this data reader will use. */
  void set_transform_pipeline(transform::transform_pipeline&& tp) {
    m_transform_pipeline = std::move(tp);
  }

#ifdef LBANN_HAS_DISTCONV
  /**
   * Returns whether shuffle (which refers to input data shuffling for
   * Distconv but not random sample shuffling) is required.
   */
  virtual bool is_tensor_shuffle_required() const { return true; }
#endif // LBANN_HAS_DISTCONV

 protected:

  bool m_verbose = false;

  // For use with conduit when samples are corrupt.
  mutable std::unordered_set<int> m_using_random_node;

  /**
   * Return the absolute number of data samples that will be used for training
   * or testing.
   */
  size_t get_absolute_sample_count() const;

  /**
   * Returns the percent of the dataset to be used for training or testing.
   * If training, this is the total for training and validation. Throws if
   * set_use_percent was not called.
   */
  double get_use_percent() const;

  /**
   * Return the percent of the dataset to be used for
   * other execution modes such as validation or tournament.
   */
  double get_execution_mode_split_percent(execution_mode m) const;

  data_store_conduit *m_data_store;

  lbann_comm *m_comm;

  virtual bool
  fetch_data_block(std::map<data_field_type, CPUMat*>& input_buffers,
                   El::Int block_offset,
                   El::Int block_stride,
                   El::Int mb_size,
                   El::Matrix<El::Int>& indices_fetched);

  /** @brief Called by fetch_data, fetch_label, fetch_response
   *
   * Fetch data from a single data field into a matrix.
   * @param data_field The name of the data field.  May be one of the commonly
   *        used (samples, labels, responses) or any data_field that exists
   *        within an HDF5 experiment schema, Python DR schema, or synthetic
   *        data reader
   * @param Y The matrix to load data into.
   * @param data_id The index of the datum to fetch.
   * @param mb_idx The index within the mini-batch.
   *
   */
  virtual bool fetch_data_field(data_field_type data_field, CPUMat& Y, int data_id, int mb_idx)
  {
    NOT_IMPLEMENTED("fetch_data_field");
    return false;
  }

  /**
   * Fetch a single sample into a matrix.
   * @param X The matrix to load data into.
   * @param data_id The index of the datum to fetch.
   * @param mb_idx The index within the mini-batch.
   */
  virtual bool fetch_datum(CPUMat& X, int data_id, int mb_idx) {
    NOT_IMPLEMENTED("fetch_dataum");
    return false;
  }

  /**
   * Fetch a single label into a matrix.
   * @param Y The matrix to load data into.
   * @param data_id The index of the datum to fetch.
   * @param mb_idx The index within the mini-batch.
   */
  virtual bool fetch_label(CPUMat& Y, int data_id, int mb_idx) {
    NOT_IMPLEMENTED("fetch_label");
    return false;
  }

  /**
   * Fetch a single response into a matrix.
   * @param Y The matrix to load data into.
   * @param data_id The index of the datum to fetch.
   * @param mb_idx The index within the mini-batch.
   */
  virtual bool fetch_response(CPUMat& Y, int data_id, int mb_idx) {
    NOT_IMPLEMENTED("fetch_response");
    return false;
  }

  /**
   * Called before fetch_datum/label/response to allow initialization.
   */
  virtual void preprocess_data_source(int tid) {};
  /**
   * Called after fetch_datum/label/response to allow initialization.
   */
  virtual void postprocess_data_source(int tid) {};

  /// Shuffle indices (uses the data_seq_generator)
  virtual void shuffle_indices();
  /// Shuffle indices and profide a random number generator
  virtual void shuffle_indices(rng_gen& gen);

public:
  int m_mini_batch_size;
  int m_current_pos;
  /// Batch Stride is typically batch_size, but may be a multiple of batch size if there are multiple readers
  int m_stride_to_next_mini_batch;
  /// If there are multiple instances of the reader,
  /// then it may not reset to zero
  int m_base_offset;
  /// If there are multiple models with multiple instances of the reader,
  /// each model's set of readers may not reset to zero
  /// Provide a set of size, strides, and thresholds to handle the last mini batch of a dataset
  int m_model_offset;
  /// Sample stride is used when a mini-batch is finely interleaved across a DATA_PARALELL
  /// distribution.
  int m_sample_stride;
  /// Stride used by parallel data readers within the model
  int m_iteration_stride;

  std::vector<int> m_shuffled_indices;
  /// Record of the indicies that are not being used for training
  unused_index_map_t m_unused_indices;

  int m_last_mini_batch_size;
  int m_stride_to_last_mini_batch;
  /// The index at which this data reader starts its epoch
  int m_reset_mini_batch_index;
  /// The index of the current mini-batch that has been loaded
  int m_loaded_mini_batch_idx;
  /// The index of the current mini-batch that is being processed (train/test/validate)
  int m_current_mini_batch_idx;
  int m_num_iterations_per_epoch; /// How many iterations all readers will execute

  int m_global_mini_batch_size;
  int m_global_last_mini_batch_size;
  int m_world_master_mini_batch_adjustment;

  int m_num_parallel_readers; /// How many parallel readers are being used

  size_t m_max_files_to_load;
  std::string m_file_dir;
  std::string m_local_file_dir;
  std::string m_data_sample_list;
  std::string m_data_fn;
  std::string m_label_fn;
  bool m_shuffle;
  size_t m_absolute_sample_count;
  std::map<execution_mode, double> m_execution_mode_split_percentage;
  double m_use_percent;
  int m_first_n;
  std::string m_role;

  /** @brief Print the return values from various get_X methods to file
   *
   * For use in unit testing. Only the master prints.
   * Currently only prints values from get_X methods that only depend
   * on the data_reader (i.e, not on the trainer, model, etc)
   */
  void print_get_methods(const std::string filename);

  /**
   * Returns the number of the shuffled indices that are to be
   * used. Code in this method was formerly in select_subset_of_data()
   */
  size_t get_num_indices_to_use() const;

  friend class data_reader_merge_features;
  friend class data_reader_merge_samples;

  void set_use_data_store(bool s) { m_use_data_store = s; }

private:

  virtual void do_preload_data_store() {
    LBANN_ERROR("Not implemented.");
  }

 protected :

  bool m_use_data_store = false;

  /** @brief Holds a true value for each input data type that is supported.
   *  Use an ordered map so that checkpoints are stable. */
  std::map<data_field_type, bool> m_supported_input_types;

  //var to support GAN
  bool m_gan_labelling; //boolean flag of whether its GAN binary label, default is false
  int m_gan_label_value; //zero(0) or 1 label value for discriminator, default is 0

  observer_ptr<thread_pool> m_io_thread_pool;

  /** Whether to keep the order of loaded samples same as it is in the
   *  file to make testing and validation easier */
  bool m_keep_sample_order;

  /** Transform pipeline for preprocessing data. */
  transform::transform_pipeline m_transform_pipeline;

  /// for use with data_store: issue a warning a single time if m_data_store != nullptr,
  /// but we're not retrieving a conduit::Node from the store. This typically occurs
  /// during the test phase
  bool m_issue_warning;

  /// throws exception if get_absolute_sample_count() and
  /// get_use_percent() are incorrect
  void error_check_counts() const;
};

template<typename T>
inline void set_minibatch_item(Mat& M, const int mb_idx, const T* const ptr, const size_t count) {
  if ((count > 0u) && (ptr == nullptr)) {
    throw lbann_exception(std::string{} + __FILE__ + " " + std::to_string(__LINE__) +
                          " :: attempt to dereference a nullptr ");
  }
  for (size_t i = 0u; i < count; ++i) {
    M.Set(static_cast<El::Int>(i), static_cast<El::Int>(mb_idx), static_cast<DataType>(ptr[i]));
  }
}

}  // namespace lbann

#endif  // LBANN_DATA_READER_HPP
