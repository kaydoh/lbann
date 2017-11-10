////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2014-2016, Lawrence Livermore National Security, LLC.
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
// lbann_optimizer .hpp .cpp - Abstract optimizer class
////////////////////////////////////////////////////////////////////////////////

#include "lbann/optimizers/optimizer.hpp"

namespace lbann {

optimizer::optimizer(DataType learning_rate,
                     cudnn::cudnn_manager *cudnn)
  : m_cudnn(cudnn),
    m_variable(nullptr),
    m_learning_rate(learning_rate),
    m_gradient(nullptr) {}

optimizer::optimizer(const optimizer& other)
  : m_cudnn(other.m_cudnn),
    m_variable(other.m_variable),
    m_learning_rate(other.m_learning_rate),
    m_gradient(other.m_gradient) {
  if (m_gradient != nullptr) { m_gradient = m_gradient->Copy(); }
}

optimizer& optimizer::operator=(const optimizer& other) {
  m_cudnn = other.m_cudnn;
  m_variable = other.m_variable;
  m_learning_rate = other.m_learning_rate;

  // Copy gradient matrix
  if (m_gradient != nullptr && other.m_gradient != nullptr
      && m_gradient->DistData() == other.m_gradient->DistData()) {
    El::Copy(*others.m_gradient, *m_gradient);
  }
  if (m_gradient != nullptr) {
    delete m_gradient;
    m_gradient = nullptr;
  }
  if (other.m_gradient != nullptr) {
    m_gradient = other.m_gradient->Copy();
  }

  return *this;
}

variable::~variable() {
  if (m_gradient != nullptr) { delete m_gradient; }
}

std::string optimizer::get_description() const {
  std::stringstream ss;
  ss << get_type();
  if (m_variable != nullptr) {
    ss << " is optimizing " << m_variable->get_name();
  } else {
    ss << " is not optimizing anything";
  }
  ss << "; learning_rate=" << m_learning_rate;
  return ss.str();
}

variable& optimizer::get_variable() {
  if (m_variable == nullptr) {
    std::stringstream err;
    err << __FILE__ << " " << __LINE__ << " :: "
        << "attempted to access the variable being optimized before it is set";
    throw lbann_exception(err.str());
  }
  return *m_variable;
}

void optimizer::setup(variable& var) {
  if (m_variable != nullptr) {
    std::stringstream err;
    err << __FILE__ << " " << __LINE__ << " :: "
        << "attempted to setup an optimizer that is already set up";
    throw lbann_exception(err.str());
  }
  set_variable(var);
  
  // Initialize gradient matrix
  const AbsDistMat& values = m_variable->get_values();
  m_gradient = values.Construct(values.Grid(), values.Root());
  El::Zeros(*m_gradient, values.Height(), values.Width());

}

void optimizer::clear_gradient() {
  El::Zero(*m_gradient);
}

void optimizer::add_to_gradient(AbsDistMat& gradient) {
  *m_gradient += gradient;
}

void optimizer::step() {
  AbsDistMat& values = m_variable->get_values();
  update_variable(values, *m_gradient);
  clear_gradient();
}

}  // namespace lbann
