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
////////////////////////////////////////////////////////////////////////////////

#ifndef LBANN_LIBRARY_HPP
#define LBANN_LIBRARY_HPP

#include "lbann/execution_algorithms/batch_functional_inference_algorithm.hpp"
#include "lbann/models/model.hpp"
#include "lbann/proto/proto_common.hpp"

namespace lbann {

const int lbann_default_random_seed = 42;

/** @brief Loads a trained model from checkpoint for inference only
 * @param[in] lc An LBANN Communicator
 * @param[in] cp_dir The model checkpoint directory
 * @param[in] mbs The max mini-batch size
 * @param[in] input_dims The dimension of the input tensor
 * @param[in] output_dims The dimension of the output tensor
 * @return Model loaded from checkpoint
 */
std::unique_ptr<model> load_inference_model(lbann_comm* lc,
                                            std::string cp_dir,
                                            int mbs,
                                            std::vector<int> input_dims,
                                            std::vector<int> output_dims);

/** @brief Creates execution algorithm and infers on samples using a model
 * @param[in] model A trained model
 * @param[in] samples A distributed matrix containing samples for model input
 * @param[in] mbs The max mini-batch size
 * @return Matrix of predicted labels
 */
template <typename DataT, El::Dist CDist, El::Dist RDist, El::DistWrap DistView, El::Device Device>
El::Matrix<int, El::Device::CPU>
infer(observer_ptr<model> model,
      El::DistMatrix<DataT, CDist, RDist, DistView, Device> const& samples,
      size_t mbs) {
  auto inf_alg = batch_functional_inference_algorithm();
  return inf_alg.infer(model, samples, mbs);
}

int allocate_trainer_resources(lbann_comm *comm);

// The constructed trainer has global scope. This returns a reference
// to this global trainer.
trainer& construct_trainer(lbann_comm* comm,
                           lbann_data::Trainer* pb_trainer,
                           lbann_data::LbannPB& pb);

std::unique_ptr<thread_pool> construct_io_thread_pool(lbann_comm* comm,
                                                      bool serialized_io);

std::unique_ptr<model> build_model_from_prototext(
    int argc, char **argv,
    const lbann_data::Trainer* pb_trainer,
    lbann_data::LbannPB &pb,
    lbann_comm *comm,
    thread_pool& io_thread_pool,
    std::vector<std::shared_ptr<callback_base>>& shared_callbacks,
    int training_dr_linearized_data_size);

void print_lbann_configuration(lbann_comm *comm,
                               int io_threads_per_process,
                               int io_threads_offset);

} // namespace lbann

#endif // LBANN_LIBRARY_HPP
