/*
Highly Optimized Object-oriented Many-particle Dynamics -- Blue Edition
(HOOMD-blue) Open Source Software License Copyright 2009-2014 The Regents of
the University of Michigan All rights reserved.

HOOMD-blue may contain modifications ("Contributions") provided, and to which
copyright is held, by various Contributors who have granted The Regents of the
University of Michigan the right to modify and/or distribute such Contributions.

You may redistribute, use, and create derivate works of HOOMD-blue, in source
and binary forms, provided you abide by the following conditions:

* Redistributions of source code must retain the above copyright notice, this
list of conditions, and the following disclaimer both in the code and
prominently in any materials provided with the distribution.

* Redistributions in binary form must reproduce the above copyright notice, this
list of conditions, and the following disclaimer in the documentation and/or
other materials provided with the distribution.

* All publications and presentations based on HOOMD-blue, including any reports
or published results obtained, in whole or in part, with HOOMD-blue, will
acknowledge its use according to the terms posted at the time of submission on:
http://codeblue.umich.edu/hoomd-blue/citations.html

* Any electronic documents citing HOOMD-Blue will link to the HOOMD-Blue website:
http://codeblue.umich.edu/hoomd-blue/

* Apart from the above required attributions, neither the name of the copyright
holder nor the names of HOOMD-blue's contributors may be used to endorse or
promote products derived from this software without specific prior written
permission.

Disclaimer

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND/OR ANY
WARRANTIES THAT THIS SOFTWARE IS FREE OF INFRINGEMENT ARE DISCLAIMED.

IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// Maintainer: mphoward
#include "NeighborListGPUTree.cuh"
#include "NeighborListGPU.cuh"
#include "TextureTools.h"

#include <thrust/device_ptr.h>
#include <thrust/sort.h>

#define MORTON_CODE_N_BINS 1024     // number of bins (2^k) to use to generate 3k bit morton codes

/*! \file NeighborListGPUTree.cu
    \brief Defines GPU kernel code for neighbor list tree traversal on the GPU
*/

//! Texture for reading particle positions
scalar4_tex_t pdata_pos_tex;
//! Texture for reading leaf data
scalar4_tex_t leaf_xyzf_tex;
//! Texture for reading node upper and lower bounds
scalar4_tex_t aabb_node_bounds_tex;
//! Texture for reading node leaf head index
texture<unsigned int, 1, cudaReadModeElementType> aabb_node_head_idx_tex;
//! Texture for the head list
texture<unsigned int, 1, cudaReadModeElementType> head_list_tex;

// Expands a 10-bit integer into 30 bits
// by inserting 2 zeros after each bit.
// http://devblogs.nvidia.com/parallelforall/thinking-parallel-part-iii-tree-construction-gpu/
__device__ inline unsigned int expandBits(unsigned int v)
{
    v = (v * 0x00010001u) & 0xFF0000FFu;
    v = (v * 0x00000101u) & 0x0F00F00Fu;
    v = (v * 0x00000011u) & 0xC30C30C3u;
    v = (v * 0x00000005u) & 0x49249249u;
    return v;
}

//! slow implementation of delta from karras paper
/*!
 * computes the longest common prefix
 */
__device__ inline int delta(const unsigned int *d_morton_codes, unsigned int i, unsigned int j, int min_idx, int max_idx)
    {
    if (j > max_idx || j < min_idx)
        {
        return -1;
        }
    
    unsigned int first_code = d_morton_codes[i];
    unsigned int last_code = d_morton_codes[j];
    
    // if codes match, then use index as tie breaker
    if (first_code == last_code)
        {
        return __clz(i ^ j);
        }
    else
        {
        return __clz(first_code ^ last_code);
        }
    }

//! slow implementation of determineRange, needs refining
/*!
 * This is a literal implementation of the Karras pseudocode, with no optimizations or refinement
 */
__device__ inline uint2 determineRange(const unsigned int *d_morton_codes,
                                       const int min_idx,
                                       const int max_idx,
                                       const int idx)
    {
    int forward_prefix = delta(d_morton_codes, idx, idx+1, min_idx, max_idx);
    int backward_prefix = delta(d_morton_codes, idx, idx-1, min_idx, max_idx);
        
    // depends on sign
    int d = ((forward_prefix - backward_prefix) > 0) ? 1 : -1;
    int min_prefix = delta(d_morton_codes, idx, idx-d, min_idx, max_idx);
    
    int lmax = 2;
    while( delta(d_morton_codes, idx, idx + d*lmax, min_idx, max_idx) > min_prefix)
        {
        lmax = lmax << 1;
        }
    
    unsigned int len = 0;
    unsigned int step = lmax;
    do 
        {
        step = step >> 1;
        unsigned int new_len = len + step;
        if (delta(d_morton_codes, idx, idx + d*new_len, min_idx, max_idx) > min_prefix)
            len = new_len;
        }
    while (step > 1);
   
    uint2 range;
    if (d > 0)
        {
        range.x = idx;
        range.y = idx + len;
        }
    else
        {
        range.x = idx - len;
        range.y = idx;
        }
    return range;
    }

__device__ inline unsigned int findSplit(const unsigned int *d_morton_codes,
                                         const unsigned int first,
                                         const unsigned int last)
    {
    unsigned int first_code = d_morton_codes[first];
    unsigned int last_code = d_morton_codes[last];
    
    // if codes match, then just split evenly
    if (first_code == last_code)
        return (first + last) >> 1;
    
    // get the length of the common prefix
    int common_prefix = __clz(first_code ^ last_code);
    
    // assume split starts at first, and begin binary search
    unsigned int split = first;
    unsigned int step = last - first;
    do
        {
        // exponential decrease (is factor of 2 best?)
        step = (step + 1) >> 1;
        unsigned int new_split = split + step;
        
        // if proposed split lies within range
        if (new_split < last)
            {
            unsigned int split_code = d_morton_codes[new_split];
            int split_prefix = __clz(first_code ^ split_code);
            
            // if new split shares a longer number of bits, accept it
            if (split_prefix > common_prefix)
                {
                split = new_split;
                }
            }
        }
    while (step > 1);
    
    return split;
    }

__global__ void gpu_nlist_morton_codes_kernel(unsigned int *d_morton_codes,
                                              unsigned int *d_leaf_particles,
                                              const Scalar4 *d_pos,
                                              const unsigned int *d_map_p_global_tree,
                                              const unsigned int N,
                                              const unsigned int *d_type_head,
                                              const unsigned int ntypes,
                                              const BoxDim box,
                                              const Scalar3 ghost_width)
    {
    // shared memory cache of type head
    extern __shared__ unsigned char s_data[];
    unsigned int *s_type_head = (unsigned int *)(&s_data[0]);

    for (unsigned int cur_offset = 0; cur_offset < ntypes; cur_offset += blockDim.x)
        {
        if (cur_offset + threadIdx.x < ntypes)
            {
            s_type_head[cur_offset + threadIdx.x] = d_type_head[cur_offset + threadIdx.x];
            }
        }
    __syncthreads();
    
    // compute the particle index this thread operates on
    const unsigned int idx = blockDim.x * blockIdx.x + threadIdx.x;

    // quit now if this thread is processing past the end of the particle list
    if (idx >= N)
        return;
    
    // acquire particle data
    Scalar4 postype = d_pos[idx];
    Scalar3 pos = make_scalar3(postype.x, postype.y, postype.z);
    unsigned int type = __scalar_as_int(postype.w);
        
    uchar3 periodic = box.getPeriodic();
    Scalar3 f = box.makeFraction(pos,ghost_width);

    // check if the particle is inside the unit cell + ghost layer in all dimensions
    if ((f.x < Scalar(0.0) || f.x >= Scalar(1.00001)) ||
        (f.y < Scalar(0.0) || f.y >= Scalar(1.00001)) ||
        (f.z < Scalar(0.0) || f.z >= Scalar(1.00001)) )
        {
        // if a ghost particle is out of bounds, silently ignore it
//         if (idx < N)
//             (*d_conditions).z = idx+1;
        return;
        }

    // find the bin each particle belongs in
    unsigned int ib = (unsigned int)(f.x * MORTON_CODE_N_BINS);
    unsigned int jb = (unsigned int)(f.y * MORTON_CODE_N_BINS);
    unsigned int kb = (unsigned int)(f.z * MORTON_CODE_N_BINS);

    // need to handle the case where the particle is exactly at the box hi
    if (ib == MORTON_CODE_N_BINS && periodic.x)
        ib = 0;
    if (jb == MORTON_CODE_N_BINS && periodic.y)
        jb = 0;
    if (kb == MORTON_CODE_N_BINS && periodic.z)
        kb = 0;
    
    // inline call to some bit swizzling arithmetic
    unsigned int ii = expandBits(ib);
    unsigned int jj = expandBits(jb);
    unsigned int kk = expandBits(kb);
    unsigned int morton_code = ii * 4 + jj * 2 + kk;
    
    // sort morton code by type as we compute it
    unsigned int leaf_idx = s_type_head[type] + d_map_p_global_tree[idx];
    d_morton_codes[leaf_idx] = morton_code;
    d_leaf_particles[leaf_idx] = idx;
    }
    
__global__ void gpu_nlist_gen_hierarchy_kernel(unsigned int *d_leaf_parents,
                                               unsigned int *d_node_parents,
                                               uint2 *d_node_children,
                                               const unsigned int *d_morton_codes,
                                               const unsigned int *d_num_per_type,
                                               const unsigned int N,
                                               const unsigned int ntypes,
                                               const unsigned int nleafs)
    {
    // compute the internal node index this thread operates on
    const unsigned int idx = blockDim.x * blockIdx.x + threadIdx.x;

    if (idx >= (nleafs - ntypes))
        return;
    
    // get what type of leaf I am
    unsigned int min_idx = 0; // the "0" of the leaf node array
    unsigned int max_idx = 0; // the "N-1" of the leaf node array
    
    unsigned int node_idx = idx;
    unsigned int origin = 0;
    unsigned int end = 0;
    
    unsigned int cur_type=0;
    for (cur_type=0; cur_type < ntypes; ++cur_type)
        {
        // current min index is the previous max index
        min_idx = max_idx;
        // max index adds the number of internal nodes in this type (nleaf - 1)
        max_idx += (d_num_per_type[cur_type] + PARTICLES_PER_LEAF - 1)/PARTICLES_PER_LEAF-1;
        
        // we break the loop if we are in range
        if (idx < max_idx)
            {
            // now, we repurpose the min and max index to now correspond to the *leaf* index.
            // the min index is the minimum *leaf* index
            origin = min_idx + cur_type;
            end = max_idx + cur_type;
            node_idx += cur_type;
            break;
            }
        }
    
    uint2 range = determineRange(d_morton_codes, origin, end, node_idx);
    unsigned int first = range.x;
    unsigned int last = range.y;
    unsigned int split = findSplit(d_morton_codes, first, last);

    // we are going to encode the data by taking the split index, bitshifting twice, 
    // and then setting the lowest two bits to 1 (=leaf) or 0 (=parent)
    // this assumes that we aren't going to overflow by bitshifting, but since we typically only hold ~1MM particles,
    // we should have plenty of room to spare
//     unsigned int children = split_dat << 2;
    uint2 children;
    
    // these case defines a leaf node
    // so we need to set the appropriate bits and set the parent of the children for backtracking
    if (split == first)
        {
        children.x = split << 1;
        children.x |= 1;
        
        d_leaf_parents[split] = idx;
        }
    else
        {
        children.x = (split-cur_type) << 1;
        d_node_parents[split - cur_type] = idx;
        }
        
    if ((split + 1) == last)
        {
        children.y = (split+1) << 1;
        children.y |= 1;
        d_leaf_parents[split + 1] = idx;
        }
    else
        {
        children.y = (split-cur_type+1) << 1;
        
        d_node_parents[split - cur_type + 1] = idx;
        }
    
    // root is always number "zero", but we need to set this to ourselves so that we don't accidentally
    // run past when traversing up    
    if (node_idx == origin)
        {
        d_node_parents[idx] = idx;
        }

    // 
    // okay, now we also need to set the parents of the children
    // these are always guaranteed to be internal nodes, so just set the index
    d_node_children[idx] = children;
    
    }
    
cudaError_t gpu_nlist_gen_hierarchy(unsigned int *d_leaf_parents,
                                    unsigned int *d_node_parents,
                                    uint2 *d_node_children,
                                    const unsigned int *d_morton_codes,
                                    const unsigned int *d_num_per_type,
                                    const unsigned int N,
                                    const unsigned int ntypes,
                                    const unsigned int nleafs,
                                    const unsigned int block_size)
    {
    static unsigned int max_block_size = UINT_MAX;
    if (max_block_size == UINT_MAX)
        {
        cudaFuncAttributes attr;
        cudaFuncGetAttributes(&attr, (const void *)gpu_nlist_gen_hierarchy_kernel);
        max_block_size = attr.maxThreadsPerBlock;
        }

    int run_block_size = min(block_size,max_block_size);
    
    gpu_nlist_gen_hierarchy_kernel<<<(nleafs-ntypes)/run_block_size + 1, run_block_size>>>(d_leaf_parents,
                                                                                  d_node_parents,
                                                                                  d_node_children,
                                                                                  d_morton_codes,
                                                                                  d_num_per_type,
                                                                                  N,
                                                                                  ntypes,
                                                                                  nleafs);
    return cudaSuccess;
    }

cudaError_t gpu_nlist_morton_codes(unsigned int *d_morton_codes,
                                   unsigned int *d_leaf_particles,
                                   const Scalar4 *d_pos,
                                   const unsigned int *d_map_p_global_tree,
                                   const unsigned int N,
                                   const unsigned int *d_type_head,
                                   const unsigned int ntypes,
                                   const BoxDim& box,
                                   const Scalar3 ghost_width,
                                   const unsigned int block_size)
    {
    unsigned int shared_size = sizeof(unsigned int)*ntypes;
    
    static unsigned int max_block_size = UINT_MAX;
    if (max_block_size == UINT_MAX)
        {
        cudaFuncAttributes attr;
        cudaFuncGetAttributes(&attr, (const void *)gpu_nlist_morton_codes_kernel);
        max_block_size = attr.maxThreadsPerBlock;
        }

    int run_block_size = min(block_size,max_block_size);
    
    gpu_nlist_morton_codes_kernel<<<N/run_block_size + 1, run_block_size, shared_size>>>(d_morton_codes,
                                                                                         d_leaf_particles,
                                                                                         d_pos,
                                                                                         d_map_p_global_tree,
                                                                                         N,
                                                                                         d_type_head,
                                                                                         ntypes,
                                                                                         box,
                                                                                         ghost_width);
    return cudaSuccess;
    }
    
cudaError_t gpu_nlist_morton_sort(unsigned int *d_morton_codes,
                                  unsigned int *d_leaf_particles,
                                  const unsigned int *h_num_per_type,
                                  const unsigned int ntypes)
    {
    
    // thrust requires to wrap the pod in a thrust object
    thrust::device_ptr<unsigned int> t_morton_codes = thrust::device_pointer_cast(d_morton_codes);
    thrust::device_ptr<unsigned int> t_leaf_particles = thrust::device_pointer_cast(d_leaf_particles);
    
    // loop on types and do a sort by key
    for (unsigned int cur_type=0; cur_type < ntypes; ++cur_type)
        {
        thrust::sort_by_key(t_morton_codes,
                            t_morton_codes + h_num_per_type[cur_type],
                            t_leaf_particles);
                            
        // advance pointers to sort the next type
        t_morton_codes += h_num_per_type[cur_type];
        t_leaf_particles += h_num_per_type[cur_type];
        }
    
    return cudaSuccess;
    }

__global__ void gpu_nlist_merge_particles_kernel(Scalar4 *d_leaf_aabbs,
                                                 unsigned int *d_morton_codes_red,
                                                 const unsigned int *d_morton_codes,
                                                 const Scalar4 *d_pos,
                                                 const unsigned int *d_num_per_type,
                                                 const unsigned int ntypes,
                                                 const unsigned int *d_leaf_particles,
                                                 const unsigned int *d_leaf_offset,
                                                 const unsigned int *d_type_head,
                                                 const unsigned int N,
                                                 const unsigned int nleafs)
    {
    // compute the internal node index this thread operates on
    const unsigned int idx = blockDim.x * blockIdx.x + threadIdx.x;

    // quit now if this thread is processing past the end of the particle list
    if (idx >= nleafs)
        return;
    
    // get what type of leaf I am
    unsigned int total_bins = 0;
    int leaf_type = -1;
    unsigned int max_idx = N;
    for (unsigned int cur_type=0; leaf_type == -1 && cur_type < ntypes; ++cur_type)
        {
        total_bins += (d_num_per_type[cur_type] + PARTICLES_PER_LEAF - 1)/PARTICLES_PER_LEAF;
        
        if (idx < total_bins)
            {
            leaf_type = cur_type;
            if ( (cur_type + 1) < ntypes)
                {
                max_idx = d_type_head[cur_type + 1];
                }
            }
        }
    
    unsigned int start_idx = idx*PARTICLES_PER_LEAF - d_leaf_offset[leaf_type];
    unsigned int end_idx = (max_idx - start_idx > PARTICLES_PER_LEAF) ? start_idx + PARTICLES_PER_LEAF : max_idx;
    
    
    // upper also holds the skip value, but we have no idea what this is right now
    Scalar4 upper = d_pos[ d_leaf_particles[start_idx] ];
    upper.w = 0.0f;
    
    // lower holds the particle number, we have one already
    Scalar4 lower = upper;
    lower.w = 1.0f;
    
    for (unsigned int cur_p=start_idx+1; cur_p < end_idx; ++cur_p)
        {
        Scalar4 cur_pos = d_pos[ d_leaf_particles[cur_p] ];
        
        // merge the boxes together
        if (cur_pos.x < lower.x) lower.x = cur_pos.x;
        if (cur_pos.x > upper.x) upper.x = cur_pos.x;
            
        if (cur_pos.y < lower.y) lower.y = cur_pos.y;
        if (cur_pos.y > upper.y) upper.y = cur_pos.y;
        
        if (cur_pos.z < lower.z) lower.z = cur_pos.z;
        if (cur_pos.z > upper.z) upper.z = cur_pos.z;

        lower.w += 1.0f;
        }
    
    d_leaf_aabbs[2*idx] = upper;
    d_leaf_aabbs[2*idx + 1] = lower;
    
    d_morton_codes_red[idx] = d_morton_codes[start_idx];
    }
    
cudaError_t gpu_nlist_merge_particles(Scalar4 *d_leaf_aabbs,
                                      unsigned int *d_morton_codes_red,
                                      const unsigned int *d_morton_codes,
                                      const Scalar4 *d_pos,
                                      const unsigned int *d_num_per_type,
                                      const unsigned int ntypes,
                                      const unsigned int *d_leaf_particles,
                                      const unsigned int *d_leaf_offset,
                                      const unsigned int *d_type_head,
                                      const unsigned int N,
                                      const unsigned int nleafs,
                                      const unsigned int block_size)
    {
    
    gpu_nlist_merge_particles_kernel<<<nleafs/block_size + 1, block_size>>>(d_leaf_aabbs,
                                                                            d_morton_codes_red,
                                                                            d_morton_codes,
                                                                       d_pos,
                                                                       d_num_per_type,
                                                                       ntypes,
                                                                       d_leaf_particles,
                                                                       d_leaf_offset,
                                                                       d_type_head,
                                                                       N,
                                                                       nleafs);
    return cudaSuccess;
    }
    

__global__ void gpu_nlist_bubble_aabbs_kernel(unsigned int *d_node_locks,
                                              Scalar4 *d_tree_aabbs,
                                              const unsigned int *d_leaf_parents,
                                              const unsigned int *d_node_parents,
                                              const uint2 *d_node_children,
                                              const unsigned int ntypes,
                                              const unsigned int nleafs,
                                              const unsigned int block_size)
    {
    const unsigned int idx = blockDim.x * blockIdx.x + threadIdx.x;
    
    if (idx >= nleafs)
        return;
        
        
    // okay, first we start from the leaf and set my bounding box
    Scalar4 cur_upper = d_tree_aabbs[2*idx];
    Scalar4 cur_lower = d_tree_aabbs[2*idx+1];
    
    // zero the counters for internal nodes
    cur_upper.w = 0.0f;
    cur_lower.w = 0.0f;
    
    // then, we look up our parent
    unsigned int cur_child = idx;
    unsigned int cur_parent = d_leaf_parents[idx];
    
    // bit shift the current child, and set that it is a leaf
    cur_child <<= 1;
    cur_child |= 1;
    
    unsigned int lock_key = 0;
    do
        {
        // then, we do an atomicAdd on the lock to see if we need to process the parent
        lock_key = atomicAdd(d_node_locks + cur_parent, 1);
    
        // process the node
        if (lock_key == 1)
            {
            // get the node's children
            uint2 children = d_node_children[cur_parent];
            
            unsigned int left_child = children.x;
            unsigned int right_child = children.y;
            
            // okay, identify which node matches me
            // this would be alot easier to do if we kept sibling information
            unsigned int cur_sibling = (left_child == cur_child) ? right_child : left_child;
            cur_sibling >>= 1; // bit shift to clear out the leaf flag
            
            // if not a leaf, need to skip ahead in memory
            cur_sibling += ((left_child == cur_child && !(right_child & 1)) || (right_child == cur_child && !(left_child & 1)))*nleafs;
            
            // compute the max upper bound
            Scalar4 sib_upper = d_tree_aabbs[2*cur_sibling];
            if (sib_upper.x > cur_upper.x) cur_upper.x = sib_upper.x;
            if (sib_upper.y > cur_upper.y) cur_upper.y = sib_upper.y;
            if (sib_upper.z > cur_upper.z) cur_upper.z = sib_upper.z;
            d_tree_aabbs[2*(nleafs + cur_parent)] = cur_upper;
            
            // compute the min lower bound
            Scalar4 sib_lower = d_tree_aabbs[2*cur_sibling+1];
            if (sib_lower.x < cur_lower.x) cur_lower.x = sib_lower.x;
            if (sib_lower.y < cur_lower.y) cur_lower.y = sib_lower.y;
            if (sib_lower.z < cur_lower.z) cur_lower.z = sib_lower.z;
            d_tree_aabbs[2*(nleafs + cur_parent)+1] = cur_lower;
            
            // bit shift current child to get parent, all subsequent levels cannot be leafs
            // have to bit shift child back down to recover current id (is this faster than making a temporary var?)
            cur_child = cur_parent << 1;
            cur_parent = d_node_parents[cur_child >> 1];
            }
        }
    while (lock_key == 1 && cur_child != cur_parent);
    
    }
    
cudaError_t gpu_nlist_bubble_aabbs(unsigned int *d_node_locks,
                                   Scalar4 *d_tree_aabbs,
                                   const unsigned int *d_leaf_parents,
                                   const unsigned int *d_node_parents,
                                   const uint2 *d_node_children,
                                   const unsigned int ntypes,
                                   const unsigned int nleafs,
                                   const unsigned int block_size)
    {
    cudaMemset(d_node_locks, 0, sizeof(unsigned int)*(nleafs - ntypes));
    
    // later we can use block size to perform atomic operations in shared memory rather than
    // global memory for nodes in the same block. but for now, just use slow global memory for everything.
    gpu_nlist_bubble_aabbs_kernel<<<nleafs/block_size + 1, block_size>>>(d_node_locks,
                                                                         d_tree_aabbs,
                                                                         d_leaf_parents,
                                                                         d_node_parents,
                                                                         d_node_children,
                                                                         ntypes,
                                                                         nleafs,
                                                                         block_size);
    
    return cudaSuccess;
    }

template<unsigned char flags>
__global__ void gpu_nlist_traverse_tree_kernel(unsigned int *d_nlist,
                                     unsigned int *d_n_neigh,
                                     Scalar4 *d_last_updated_pos,
                                     unsigned int *d_conditions,
                                     const unsigned int *d_Nmax,
                                     const unsigned int *d_head_list,
                                     const Scalar4 *d_pos,
                                     const unsigned int *d_body,
                                     const Scalar *d_diameter,
                                     const unsigned int N,
                                     const AABBTreeGPU *d_aabb_trees,
                                     const Scalar4 *d_aabb_node_bounds,
                                     const unsigned int *d_aabb_node_head_idx,
                                     const unsigned int *d_aabb_leaf_particles,
                                     const Scalar4 *d_leaf_xyzf,
                                     const Scalar2 *d_leaf_db,
                                     const Scalar3 *d_image_list,
                                     const unsigned int nimages,
                                     const Scalar *d_r_cut,
                                     const Scalar r_buff,
                                     const unsigned int ntypes,
                                     bool filter_body)
    {
    filter_body = flags & 1;

    // cache the r_listsq parameters into shared memory
    const Index2D typpair_idx(ntypes);
    const unsigned int num_typ_parameters = typpair_idx.getNumElements();

    // shared data for per type pair parameters
    extern __shared__ unsigned char s_data[];
    
    // pointer for the r_listsq data
    Scalar *s_r_list = (Scalar *)(&s_data[0]);
    unsigned int *s_Nmax = (unsigned int *)(&s_data[sizeof(Scalar)*num_typ_parameters]);

    // load in the per type pair r_list
    for (unsigned int cur_offset = 0; cur_offset < num_typ_parameters; cur_offset += blockDim.x)
        {
        if (cur_offset + threadIdx.x < num_typ_parameters)
            {
            s_r_list[cur_offset + threadIdx.x] = d_r_cut[cur_offset + threadIdx.x]+r_buff;
            }
            
        if (cur_offset + threadIdx.x < ntypes)
            {
            s_Nmax[cur_offset + threadIdx.x] = d_Nmax[cur_offset + threadIdx.x];
            }
        }
    __syncthreads();
    
    
    // compute the particle index this thread operates on
    const unsigned int idx = blockDim.x * blockIdx.x + threadIdx.x;

    // quit now if this thread is processing past the end of the particle list
    if (idx >= N)
        return;  
    
    // read in the current position and orientation
    const unsigned int my_pidx = d_aabb_leaf_particles[idx];
    const Scalar4 postype_i = texFetchScalar4(d_pos, pdata_pos_tex, my_pidx);
    const Scalar3 pos_i = make_scalar3(postype_i.x, postype_i.y, postype_i.z);
    const unsigned int type_i = __scalar_as_int(postype_i.w);
    const unsigned int body_i = d_body[my_pidx];
    
    const unsigned int nlist_head_i = texFetchUint(d_head_list, head_list_tex, my_pidx);
    
    unsigned int n_neigh_i = 0;
    
    for (unsigned int cur_pair_type=0; cur_pair_type < ntypes; ++cur_pair_type)
        {
        // Check primary box
        const Scalar r_cut_i = s_r_list[typpair_idx(type_i,cur_pair_type)];
        const Scalar r_cutsq_i = r_cut_i*r_cut_i;
        const AABBTreeGPU cur_aabb_tree = d_aabb_trees[cur_pair_type];
        
        for (unsigned int cur_image = 0; cur_image < nimages; ++cur_image)
            {
            const Scalar3 pos_i_image = pos_i + d_image_list[cur_image];
            const Scalar3 aabb_upper = make_scalar3(pos_i_image.x + r_cut_i,
                                                    pos_i_image.y + r_cut_i,
                                                    pos_i_image.z + r_cut_i);
            const Scalar3 aabb_lower = make_scalar3(pos_i_image.x - r_cut_i,
                                                    pos_i_image.y - r_cut_i,
                                                    pos_i_image.z - r_cut_i);
            
            // stackless search
            for (unsigned int cur_node_idx = 0; cur_node_idx < cur_aabb_tree.num_nodes; ++cur_node_idx)
                {
                const Scalar4 upper_skip = texFetchScalar4(d_aabb_node_bounds, aabb_node_bounds_tex, 2*(cur_aabb_tree.node_head + cur_node_idx));
                const Scalar4 lower_np = texFetchScalar4(d_aabb_node_bounds, aabb_node_bounds_tex, 2*(cur_aabb_tree.node_head + cur_node_idx)+1);

//                 if (!((pos_i_image.x + r_cut_i) < lower_np.x
//                       || (pos_i_image.x - r_cut_i) > upper_skip.x
//                       || (pos_i_image.y + r_cut_i) < lower_np.y
//                       || (pos_i_image.y - r_cut_i) > upper_skip.y
//                       || (pos_i_image.z + r_cut_i) < lower_np.z
//                       || (pos_i_image.z - r_cut_i) > upper_skip.z))
                if (!(aabb_upper.x < lower_np.x
                      || aabb_lower.x > upper_skip.x
                      || aabb_upper.y < lower_np.y
                      || aabb_lower.y > upper_skip.y
                      || aabb_upper.z < lower_np.z
                      || aabb_lower.z > upper_skip.z))                
                    {      
                    for (unsigned int cur_p = 0; cur_p < __scalar_as_int(lower_np.w); ++cur_p)
                        {
                        // neighbor j
                        const Scalar4 cur_xyzf = texFetchScalar4(d_leaf_xyzf, leaf_xyzf_tex, texFetchUint(d_aabb_node_head_idx, aabb_node_head_idx_tex, cur_aabb_tree.node_head + cur_node_idx) + cur_p);
                        const Scalar3 pos_j = make_scalar3(cur_xyzf.x, cur_xyzf.y, cur_xyzf.z);
                        const unsigned int j = __scalar_as_int(cur_xyzf.w);
                        
                        const Scalar2 cur_db = d_leaf_db[texFetchUint(d_aabb_node_head_idx, aabb_node_head_idx_tex, cur_aabb_tree.node_head + cur_node_idx) + cur_p];

                        bool excluded = (my_pidx == j);

                        if (filter_body && body_i != 0xffffffff)
                            excluded = excluded | (body_i == __scalar_as_int(cur_db.y));
                            
                        if (!excluded)
                            {
                            // compute distance and wrap back into box
                            Scalar3 drij = pos_j - pos_i_image;
                            Scalar dr2 = dot(drij,drij);

                            if (dr2 <= r_cutsq_i)
                                {
                                if (n_neigh_i < s_Nmax[type_i])
                                    {
                                    d_nlist[nlist_head_i + n_neigh_i] = j;
                                    }
                                ++n_neigh_i;
                                }
                            }
                        }
                    }
                else
                    {
                    // skip ahead
                    cur_node_idx += __scalar_as_int(upper_skip.w);
                    }
                } // end stackless search  
            } // end loop over images
        } // end loop over pair types
        
    // could try reordering by idx instead of pidx, but that seems to not make much difference in microbenchmarking.
    d_n_neigh[my_pidx] = n_neigh_i;
    d_last_updated_pos[my_pidx] = postype_i;
    
    // update the number of neighbors for this type if allocated memory is exceeded
    if (n_neigh_i >= s_Nmax[type_i])
        atomicMax(&d_conditions[type_i], n_neigh_i);
    }
    
cudaError_t gpu_nlist_traverse_tree(unsigned int *d_nlist,
                                     unsigned int *d_n_neigh,
                                     Scalar4 *d_last_updated_pos,
                                     unsigned int *d_conditions,
                                     const unsigned int *d_Nmax,
                                     const unsigned int *d_head_list,
                                     const Scalar4 *d_pos,
                                     const unsigned int *d_body,
                                     const Scalar *d_diameter,
                                     const unsigned int N,
                                     const AABBTreeGPU *d_aabb_trees,
                                     const Scalar4 *d_aabb_node_bounds,
                                     const unsigned int *d_aabb_node_head_idx,
                                     const unsigned int n_nodes,
                                     const unsigned int *d_aabb_leaf_particles,
                                     const Scalar4 *d_leaf_xyzf,
                                     const Scalar2 *d_leaf_db,
                                     const Scalar3 *d_image_list,
                                     const unsigned int nimages,
                                     const Scalar *d_r_cut,
                                     const Scalar r_buff,
                                     const unsigned int ntypes,
                                     bool filter_body,
                                     const unsigned int compute_capability,
                                     const unsigned int block_size)
    {
    // shared memory = r_list + Nmax
    Index2D typpair_idx(ntypes);
    unsigned int shared_size = sizeof(Scalar)*typpair_idx.getNumElements() + sizeof(unsigned int)*ntypes;
    
    // bind the neighborlist texture
    if (compute_capability < 35)
        {
        // bind the position texture
        pdata_pos_tex.normalized = false;
        pdata_pos_tex.filterMode = cudaFilterModePoint;
        cudaBindTexture(0, pdata_pos_tex, d_pos, sizeof(Scalar4)*N);
        
        leaf_xyzf_tex.normalized = false;
        leaf_xyzf_tex.filterMode = cudaFilterModePoint;
        cudaBindTexture(0, leaf_xyzf_tex, d_leaf_xyzf, sizeof(Scalar4)*N);
        
        aabb_node_bounds_tex.normalized = false;
        aabb_node_bounds_tex.filterMode = cudaFilterModePoint;
        cudaBindTexture(0, aabb_node_bounds_tex, d_aabb_node_bounds, sizeof(Scalar4)*2*n_nodes);
        
        aabb_node_head_idx_tex.normalized = false;
        aabb_node_head_idx_tex.filterMode = cudaFilterModePoint;
        cudaBindTexture(0, aabb_node_head_idx_tex, d_aabb_node_head_idx, sizeof(unsigned int)*n_nodes);
        
        head_list_tex.normalized = false;
        head_list_tex.filterMode = cudaFilterModePoint;
        cudaBindTexture(0, head_list_tex, d_head_list, sizeof(unsigned int)*N);
        }
    
    if (!filter_body)
        {
        static unsigned int max_block_size = UINT_MAX;
        if (max_block_size == UINT_MAX)
            {
            cudaFuncAttributes attr;
            cudaFuncGetAttributes(&attr, (const void *)gpu_nlist_traverse_tree_kernel<0>);
            max_block_size = attr.maxThreadsPerBlock;
            }

        int run_block_size = min(block_size,max_block_size);
        gpu_nlist_traverse_tree_kernel<0><<<N/run_block_size + 1, run_block_size, shared_size>>>(d_nlist,
                                                                                 d_n_neigh,
                                                                                 d_last_updated_pos,
                                                                                 d_conditions,
                                                                                 d_Nmax,
                                                                                 d_head_list,
                                                                                 d_pos,
                                                                                 d_body,
                                                                                 d_diameter,
                                                                                 N,
                                                                                 d_aabb_trees,
                                                                                 d_aabb_node_bounds,
                                                                                 d_aabb_node_head_idx,
                                                                                 d_aabb_leaf_particles,
                                                                                 d_leaf_xyzf,
                                                                                 d_leaf_db,
                                                                                 d_image_list,
                                                                                 nimages,
                                                                                 d_r_cut,
                                                                                 r_buff,
                                                                                 ntypes,
                                                                                 filter_body);
        }
    else
        {
        static unsigned int max_block_size = UINT_MAX;
        if (max_block_size == UINT_MAX)
            {
            cudaFuncAttributes attr;
            cudaFuncGetAttributes(&attr, (const void *)gpu_nlist_traverse_tree_kernel<1>);
            max_block_size = attr.maxThreadsPerBlock;
            }

        int run_block_size = min(block_size,max_block_size);
        gpu_nlist_traverse_tree_kernel<1><<<N/run_block_size + 1, run_block_size, shared_size>>>(d_nlist,
                                                                                 d_n_neigh,
                                                                                 d_last_updated_pos,
                                                                                 d_conditions,
                                                                                 d_Nmax,
                                                                                 d_head_list,
                                                                                 d_pos,
                                                                                 d_body,
                                                                                 d_diameter,
                                                                                 N,
                                                                                 d_aabb_trees,
                                                                                 d_aabb_node_bounds,
                                                                                 d_aabb_node_head_idx,
                                                                                 d_aabb_leaf_particles,
                                                                                 d_leaf_xyzf,
                                                                                 d_leaf_db,
                                                                                 d_image_list,
                                                                                 nimages,
                                                                                 d_r_cut,
                                                                                 r_buff,
                                                                                 ntypes,
                                                                                 filter_body);
        }  
    return cudaSuccess;
    }

// clean up constants
#undef MORTON_CODE_N_BINS
#undef PARTICLES_PER_LEAF
