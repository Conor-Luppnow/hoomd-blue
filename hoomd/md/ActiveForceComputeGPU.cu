// Copyright (c) 2009-2021 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

// Maintainer: joaander

#include "ActiveForceComputeGPU.cuh"
#include "EvaluatorConstraintEllipsoid.h"
#include "hoomd/RNGIdentifiers.h"
#include "hoomd/RandomNumbers.h"
#include "hoomd/TextureTools.h"
using namespace hoomd;

#include <assert.h>

/*! \file ActiveForceComputeGPU.cu
    \brief Declares GPU kernel code for calculating active forces forces on the GPU. Used by
   ActiveForceComputeGPU.
*/

//! Kernel for setting active force vectors on the GPU
/*! \param group_size number of particles
    \param d_index_array stores list to convert group index to global tag
    \param d_force particle force on device
    \param d_torque particle torque on device
    \param d_orientation particle orientation on device
    \param d_f_act particle active force unit vector
    \param d_t_act particle active torque unit vector
    \param P position of the ellipsoid constraint
    \param rx radius of the ellipsoid in x direction
    \param ry radius of the ellipsoid in y direction
    \param rz radius of the ellipsoid in z direction
    \param orientationLink check if particle orientation is linked to active force vector
*/
__global__ void gpu_compute_active_force_set_forces_kernel(const unsigned int group_size,
                                                           unsigned int* d_index_array,
                                                           Scalar4* d_force,
                                                           Scalar4* d_torque,
                                                           const Scalar4* d_pos,
                                                           const Scalar4* d_orientation,
                                                           const Scalar4* d_f_act,
                                                           const Scalar4* d_t_act,
                                                           const Scalar3 P,
                                                           const Scalar rx,
                                                           const Scalar ry,
                                                           const Scalar rz,
                                                           const unsigned int N)
    {
    unsigned int group_idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (group_idx >= group_size)
        return;

    unsigned int idx = d_index_array[group_idx];
    Scalar4 posidx = __ldg(d_pos + idx);
    unsigned int type = __scalar_as_int(posidx.w);

    Scalar4 fact = __ldg(d_f_act + type);

    vec3<Scalar> f(fact.w * fact.x, fact.w * fact.y, fact.w * fact.z);
    quat<Scalar> quati(__ldg(d_orientation + idx));
    vec3<Scalar> fi = rotate(quati, f);
    d_force[idx] = vec_to_scalar4(fi, 0);

    Scalar4 tact = __ldg(d_t_act + type);

    vec3<Scalar> t(tact.w * tact.x, tact.w * tact.y, tact.w * tact.z);
    vec3<Scalar> ti = rotate(quati, t);
    d_torque[idx] = vec_to_scalar4(ti, 0);
    }

//! Kernel for adjusting active force vectors to align parallel to an ellipsoid surface constraint
//! on the GPU
/*! \param group_size number of particles
    \param d_index_array stores list to convert group index to global tag
    \param d_pos particle positions on device
    \param d_f_act particle active force unit vector
    \param P position of the ellipsoid constraint
    \param rx radius of the ellipsoid in x direction
    \param ry radius of the ellipsoid in y direction
    \param rz radius of the ellipsoid in z direction
*/
__global__ void gpu_compute_active_force_set_constraints_kernel(const unsigned int group_size,
                                                                unsigned int* d_index_array,
                                                                const Scalar4* d_pos,
                                                                Scalar4* d_orientation,
                                                                const Scalar4* d_f_act,
                                                                const Scalar3 P,
                                                                const Scalar rx,
                                                                const Scalar ry,
                                                                const Scalar rz)
    {
    unsigned int group_idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (group_idx >= group_size)
        return;

    unsigned int idx = d_index_array[group_idx];
    Scalar4 posidx = __ldg(d_pos + idx);
    unsigned int type = __scalar_as_int(posidx.w);

    EvaluatorConstraintEllipsoid Ellipsoid(P, rx, ry, rz);
    Scalar3 current_pos = make_scalar3(posidx.x, posidx.y, posidx.z);

    Scalar3 norm_scalar3 = Ellipsoid.evalNormal(
        current_pos); // the normal vector to which the particles are confined.
    vec3<Scalar> norm = vec3<Scalar>(norm_scalar3);

    Scalar4 fact = __ldg(d_f_act + type);

    vec3<Scalar> f(fact.x, fact.y, fact.z);
    quat<Scalar> quati(__ldg(d_orientation + idx));
    vec3<Scalar> fi = rotate(quati, f);

    Scalar dot_prod = fi.x * norm.x + fi.y * norm.y + fi.z * norm.z;

    Scalar dot_perp_prod = slow::sqrt(1 - dot_prod * dot_prod);

    Scalar phi_half = slow::atan(dot_prod / dot_perp_prod) / 2.0;

    fi.x -= norm.x * dot_prod;
    fi.y -= norm.y * dot_prod;
    fi.z -= norm.z * dot_prod;

    Scalar new_norm = 1.0 / slow::sqrt(fi.x * fi.x + fi.y * fi.y + fi.z * fi.z);

    fi.x *= new_norm;
    fi.y *= new_norm;
    fi.z *= new_norm;

    vec3<Scalar> rot_vec = cross(norm, fi);
    rot_vec.x *= slow::sin(phi_half);
    rot_vec.y *= slow::sin(phi_half);
    rot_vec.z *= slow::sin(phi_half);

    quat<Scalar> rot_quat(cos(phi_half), rot_vec);

    quati = rot_quat * quati;

    d_orientation[idx] = quat_to_scalar4(quati);
    }

//! Kernel for applying rotational diffusion to active force vectors on the GPU
/*! \param group_size number of particles
    \param d_index_array stores list to convert group index to global tag
    \param d_pos particle positions on device
    \param d_f_act particle active force unit vector
    \param P position of the ellipsoid constraint
    \param rx radius of the ellipsoid in x direction
    \param ry radius of the ellipsoid in y direction
    \param rz radius of the ellipsoid in z direction
    \param is2D check if simulation is 2D or 3D
    \param rotationConst particle rotational diffusion constant
    \param seed seed for random number generator
*/
__global__ void gpu_compute_active_force_rotational_diffusion_kernel(const unsigned int group_size,
                                                                     unsigned int* d_tag,
                                                                     unsigned int* d_index_array,
                                                                     const Scalar4* d_pos,
                                                                     Scalar4* d_orientation,
                                                                     const Scalar4* d_f_act,
                                                                     const Scalar3 P,
                                                                     const Scalar rx,
                                                                     const Scalar ry,
                                                                     const Scalar rz,
                                                                     bool is2D,
                                                                     const Scalar rotationConst,
                                                                     const uint64_t timestep,
                                                                     const uint16_t seed)
    {
    unsigned int group_idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (group_idx >= group_size)
        return;

    unsigned int idx = d_index_array[group_idx];
    Scalar4 posidx = __ldg(d_pos + idx);
    unsigned int type = __scalar_as_int(posidx.w);
    unsigned int ptag = d_tag[group_idx];

    quat<Scalar> quati(__ldg(d_orientation + idx));

    hoomd::RandomGenerator rng(
        hoomd::Seed(hoomd::RNGIdentifier::ActiveForceCompute, timestep, seed),
        hoomd::Counter(ptag));

    if (is2D) // 2D
        {
        Scalar delta_theta; // rotational diffusion angle
        delta_theta = hoomd::NormalDistribution<Scalar>(rotationConst)(rng);
        Scalar theta
            = delta_theta / 2.0; // angle on plane defining orientation of active force vector
        vec3<Scalar> b(0, 0, slow::sin(theta));

        quat<Scalar> rot_quat(slow::cos(theta), b);

        quati = rot_quat * quati;
        d_orientation[idx] = quat_to_scalar4(quati);
        // in 2D there is only one meaningful direction for torque
        }
    else // 3D: Following Stenhammar, Soft Matter, 2014
        {
        if (rx == 0) // if no constraint
            {
            hoomd::SpherePointGenerator<Scalar> unit_vec;
            vec3<Scalar> rand_vec;
            unit_vec(rng, rand_vec);

            Scalar4 fact = __ldg(d_f_act + type);

            vec3<Scalar> f(fact.x, fact.y, fact.z);
            vec3<Scalar> fi = rotate(quati, f);

            vec3<Scalar> aux_vec;
            aux_vec.x = fi.y * rand_vec.z - fi.z * rand_vec.y;
            aux_vec.y = fi.z * rand_vec.x - fi.x * rand_vec.z;
            aux_vec.z = fi.x * rand_vec.y - fi.y * rand_vec.x;
            Scalar aux_vec_mag = 1.0
                                 / slow::sqrt(aux_vec.x * aux_vec.x + aux_vec.y * aux_vec.y
                                              + aux_vec.z * aux_vec.z);
            aux_vec.x *= aux_vec_mag;
            aux_vec.y *= aux_vec_mag;
            aux_vec.z *= aux_vec_mag;

            Scalar delta_theta = hoomd::NormalDistribution<Scalar>(rotationConst)(rng);
            Scalar theta
                = delta_theta / 2.0; // angle on plane defining orientation of active force vector
            quat<Scalar> rot_quat(slow::cos(theta), slow::sin(theta) * aux_vec);

            quati = rot_quat * quati;
            d_orientation[idx].x = quati.s;
            d_orientation[idx].y = quati.v.x;
            d_orientation[idx].z = quati.v.y;
            d_orientation[idx].w = quati.v.z;
            }
        else // if constraint
            {
            EvaluatorConstraintEllipsoid Ellipsoid(P, rx, ry, rz);
            Scalar3 current_pos = make_scalar3(posidx.x, posidx.y, posidx.z);

            Scalar3 norm_scalar3 = Ellipsoid.evalNormal(
                current_pos); // the normal vector to which the particles are confined.
            vec3<Scalar> norm;
            norm = vec3<Scalar>(norm_scalar3);

            Scalar delta_theta = hoomd::NormalDistribution<Scalar>(rotationConst)(rng);
            Scalar theta
                = delta_theta / 2.0; // angle on plane defining orientation of active force vector
            quat<Scalar> rot_quat(slow::cos(theta), slow::sin(theta) * norm);

            quati = rot_quat * quati;
            d_orientation[idx] = quat_to_scalar4(quati);
            }
        }
    }

hipError_t gpu_compute_active_force_set_forces(const unsigned int group_size,
                                               unsigned int* d_index_array,
                                               Scalar4* d_force,
                                               Scalar4* d_torque,
                                               const Scalar4* d_pos,
                                               const Scalar4* d_orientation,
                                               const Scalar4* d_f_act,
                                               const Scalar4* d_t_act,
                                               const Scalar3& P,
                                               const Scalar rx,
                                               const Scalar ry,
                                               const Scalar rz,
                                               const unsigned int N,
                                               unsigned int block_size)
    {
    // setup the grid to run the kernel
    dim3 grid(group_size / block_size + 1, 1, 1);
    dim3 threads(block_size, 1, 1);

    // run the kernel
    hipMemset(d_force, 0, sizeof(Scalar4) * N);
    hipLaunchKernelGGL((gpu_compute_active_force_set_forces_kernel),
                       dim3(grid),
                       dim3(threads),
                       0,
                       0,
                       group_size,
                       d_index_array,
                       d_force,
                       d_torque,
                       d_pos,
                       d_orientation,
                       d_f_act,
                       d_t_act,
                       P,
                       rx,
                       ry,
                       rz,
                       N);
    return hipSuccess;
    }

hipError_t gpu_compute_active_force_set_constraints(const unsigned int group_size,
                                                    unsigned int* d_index_array,
                                                    const Scalar4* d_pos,
                                                    Scalar4* d_orientation,
                                                    const Scalar4* d_f_act,
                                                    const Scalar3& P,
                                                    const Scalar rx,
                                                    const Scalar ry,
                                                    const Scalar rz,
                                                    unsigned int block_size)
    {
    // setup the grid to run the kernel
    dim3 grid(group_size / block_size + 1, 1, 1);
    dim3 threads(block_size, 1, 1);

    // run the kernel
    hipLaunchKernelGGL((gpu_compute_active_force_set_constraints_kernel),
                       dim3(grid),
                       dim3(threads),
                       0,
                       0,
                       group_size,
                       d_index_array,
                       d_pos,
                       d_orientation,
                       d_f_act,
                       P,
                       rx,
                       ry,
                       rz);
    return hipSuccess;
    }

hipError_t gpu_compute_active_force_rotational_diffusion(const unsigned int group_size,
                                                         unsigned int* d_tag,
                                                         unsigned int* d_index_array,
                                                         const Scalar4* d_pos,
                                                         Scalar4* d_orientation,
                                                         const Scalar4* d_f_act,
                                                         const Scalar3& P,
                                                         const Scalar rx,
                                                         const Scalar ry,
                                                         const Scalar rz,
                                                         bool is2D,
                                                         const Scalar rotationConst,
                                                         const uint64_t timestep,
                                                         const uint16_t seed,
                                                         unsigned int block_size)
    {
    // setup the grid to run the kernel
    dim3 grid(group_size / block_size + 1, 1, 1);
    dim3 threads(block_size, 1, 1);

    // run the kernel
    hipLaunchKernelGGL((gpu_compute_active_force_rotational_diffusion_kernel),
                       dim3(grid),
                       dim3(threads),
                       0,
                       0,
                       group_size,
                       d_tag,
                       d_index_array,
                       d_pos,
                       d_orientation,
                       d_f_act,
                       P,
                       rx,
                       ry,
                       rz,
                       is2D,
                       rotationConst,
                       timestep,
                       seed);
    return hipSuccess;
    }
