// Copyright (c) 2009-2021 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

// Maintainer: joaander / Everyone is free to add additional potentials

/*! \file AllDriverPotentialPairGPU.cuh
    \brief Declares driver functions for computing all types of pair forces on the GPU
*/

#ifndef __ALL_DRIVER_POTENTIAL_PAIR_GPU_CUH__
#define __ALL_DRIVER_POTENTIAL_PAIR_GPU_CUH__

#include "EvaluatorPairBuckingham.h"
#include "EvaluatorPairDLVO.h"
#include "EvaluatorPairDPDLJThermo.h"
#include "EvaluatorPairDPDThermo.h"
#include "EvaluatorPairEwald.h"
#include "EvaluatorPairForceShiftedLJ.h"
#include "EvaluatorPairFourier.h"
#include "EvaluatorPairGauss.h"
#include "EvaluatorPairLJ.h"
#include "EvaluatorPairLJ0804.h"
#include "EvaluatorPairLJ1208.h"
#include "EvaluatorPairMie.h"
#include "EvaluatorPairMoliere.h"
#include "EvaluatorPairMorse.h"
#include "EvaluatorPairOPP.h"
#include "EvaluatorPairReactionField.h"
#include "EvaluatorPairSLJ.h"
#include "EvaluatorPairTable.h"
#include "EvaluatorPairTWF.h"
#include "EvaluatorPairYukawa.h"
#include "EvaluatorPairZBL.h"
#include "PotentialPairDPDThermoGPU.cuh"
#include "PotentialPairGPU.cuh"

//! Compute lj pair forces on the GPU with PairEvaluatorLJ
hipError_t __attribute__((visibility("default")))
gpu_compute_ljtemp_forces(const pair_args_t& pair_args,
                          EvaluatorPairLJ::param_type* d_params);

//! Compute gauss pair forces on the GPU with PairEvaluatorGauss
hipError_t __attribute__((visibility("default")))
gpu_compute_gauss_forces(const pair_args_t& pair_args,
                         EvaluatorPairGauss::param_type* d_params);

//! Compute slj pair forces on the GPU with PairEvaluatorGauss
hipError_t __attribute__((visibility("default")))
gpu_compute_slj_forces(const pair_args_t& pair_args, EvaluatorPairSLJ::param_type* d_params);

//! Compute yukawa pair forces on the GPU with PairEvaluatorGauss
hipError_t __attribute__((visibility("default")))
gpu_compute_yukawa_forces(const pair_args_t& pair_args,
                          EvaluatorPairYukawa::param_type* d_params);

//! Compute morse pair forces on the GPU with PairEvaluatorMorse
hipError_t __attribute__((visibility("default")))
gpu_compute_morse_forces(const pair_args_t& pair_args,
                         EvaluatorPairMorse::param_type* d_params);

//! Compute dpd thermostat on GPU with PairEvaluatorDPDThermo
hipError_t __attribute__((visibility("default")))
gpu_compute_dpdthermodpd_forces(const dpd_pair_args_t& args,
                                const EvaluatorPairDPDThermo::param_type* d_params);

//! Compute dpd conservative force on GPU with PairEvaluatorDPDThermo
hipError_t __attribute__((visibility("default")))
gpu_compute_dpdthermo_forces(const pair_args_t& pair_args,
                             EvaluatorPairDPDThermo::param_type* d_params);

//! Compute ewlad pair forces on the GPU with PairEvaluatorEwald
hipError_t __attribute__((visibility("default")))
gpu_compute_ewald_forces(const pair_args_t& pair_args,
                         EvaluatorPairEwald::param_type* d_params);

//! Compute moliere pair forces on the GPU with EvaluatorPairMoliere
hipError_t __attribute__((visibility("default")))
gpu_compute_moliere_forces(const pair_args_t& pair_args,
                           EvaluatorPairMoliere::param_type* d_params);

//! Compute zbl pair forces on the GPU with EvaluatorPairZBL
hipError_t __attribute__((visibility("default")))
gpu_compute_zbl_forces(const pair_args_t& pair_args, EvaluatorPairZBL::param_type* d_params);

//! Compute dpdlj thermostat on GPU with PairEvaluatorDPDThermo
hipError_t __attribute__((visibility("default")))
gpu_compute_dpdljthermodpd_forces(const dpd_pair_args_t& args,
                                  const EvaluatorPairDPDLJThermo::param_type* d_params);

//! Compute dpdlj conservative force on GPU with PairEvaluatorDPDThermo
hipError_t __attribute__((visibility("default")))
gpu_compute_dpdljthermo_forces(const pair_args_t& args,
                               EvaluatorPairDPDLJThermo::param_type* d_params);

//! Compute force shifted lj pair forces on the GPU with PairEvaluatorForceShiftedLJ
hipError_t __attribute__((visibility("default")))
gpu_compute_force_shifted_lj_forces(const pair_args_t& args,
                                    EvaluatorPairForceShiftedLJ::param_type* d_params);

//! Compute mie potential pair forces on the GPU with PairEvaluatorMie
hipError_t __attribute__((visibility("default")))
gpu_compute_mie_forces(const pair_args_t& args, EvaluatorPairMie::param_type* d_params);

//! Compute mie potential pair forces on the GPU with PairEvaluatorReactionField
hipError_t __attribute__((visibility("default")))
gpu_compute_reaction_field_forces(const pair_args_t& args,
                                  EvaluatorPairReactionField::param_type* d_params);

//! Compute buckingham pair forces on the GPU with PairEvaluatorBuckingham
hipError_t __attribute__((visibility("default")))
gpu_compute_buckingham_forces(const pair_args_t& pair_args,
                              EvaluatorPairBuckingham::param_type* d_params);

//! Compute lj1208 pair forces on the GPU with PairEvaluatorLJ1208
hipError_t __attribute__((visibility("default")))
gpu_compute_lj1208_forces(const pair_args_t& pair_args,
                          EvaluatorPairLJ1208::param_type* d_params);

//! Compute lj0804 pair forces on the GPU with PairEvaluatorLJ0804
hipError_t __attribute__((visibility("default")))
gpu_compute_lj0804_forces(const pair_args_t& pair_args,
                          EvaluatorPairLJ0804::param_type* d_params);

//! Compute DLVO potential pair forces on the GPU with EvaluatorPairDLVO
hipError_t __attribute__((visibility("default")))
gpu_compute_dlvo_forces(const pair_args_t& args, EvaluatorPairDLVO::param_type* d_params);

//! Compute Fourier potential pair forces on the GPU with PairEvaluatorFourier
hipError_t __attribute__((visibility("default")))
gpu_compute_fourier_forces(const pair_args_t& pair_args,
                           EvaluatorPairFourier::param_type* d_params);

//! Compute oscillating pair potential forces on the GPU with EvaluatorPairOPP
hipError_t __attribute__((visibility("default")))
gpu_compute_opp_forces(const pair_args_t& pair_args, EvaluatorPairOPP::param_type* d_params);

//! Compute tabulated pair potential forces on the GPU with EvaluatorPairTable
hipError_t __attribute__((visibility("default")))
gpu_compute_table_forces(const pair_args_t& pair_args, EvaluatorPairTable::param_type* d_params);

//! Compute oscillating pair potential forces on the GPU with EvaluatorPairOPP
hipError_t __attribute__((visibility("default")))
gpu_compute_twf_forces(const pair_args_t& pair_args, EvaluatorPairTWF::param_type* d_params);
#endif
