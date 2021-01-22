// Copyright (c) 2009-2021 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

/*! \file DLVODriverPotentialPairGPU.cu
    \brief Defines the driver functions for computing all types of pair forces on the GPU
*/

#include "EvaluatorPairDLVO.h"
#include "AllDriverPotentialPairGPU.cuh"

hipError_t gpu_compute_dlvo_forces(const pair_args_t & args,
                                                const EvaluatorPairDLVO::param_type *d_params)
    {
    return gpu_compute_pair_forces<EvaluatorPairDLVO>(args,
                                                     d_params);
    }
