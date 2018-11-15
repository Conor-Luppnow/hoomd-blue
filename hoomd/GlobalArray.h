// Copyright (c) 2009-2018 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.


// Maintainers: jglaser, pschwende

/*! \file GlobalArray.h
    \brief Defines the GlobalArray class
*/

/*! GlobalArray internally uses managed memory to store data, to allow buffers being accessed from
    multiple devices.

    cudaMemAdvise() can be called on GlobalArray's data, which is obtained using ::get().

    GlobalArray<> supports all functionality that GPUArray<> does, and should eventually replace GPUArray.
    In fact, for performance considerations in single GPU situations, GlobalArray internally falls
    back on GPUArray (and whenever it doesn't have an ExecutionConfiguration). This behavior is controlled
    by the result of ExecutionConfiguration::allConcurrentManagedAccess().

    One difference to GPUArray is that GlobalArray doesn't zero its memory space, so don't forget to initialize
    the data explicitly. However, if the items are objects that have a constructur, GlobalArray takes
    care of calling constructors and destructors.

    Internally, GlobalArray<> uses a smart pointer to comply with RAII semantics.

    As for GPUArray, access to the data is provided through ArrayHandle<> objects, with proper access_mode
    and access_location flags.
*/

#pragma once

#ifdef ENABLE_CUDA
#include <cuda_runtime.h>
#endif

#include <memory>

#include "GPUArray.h"
#include "MemoryTraceback.h"

#include <type_traits>
#include <string>
#include <unistd.h>
#include <vector>

#define checkAcquired(a) { \
    assert(!(a).m_acquired); \
    if ((a).m_acquired) \
        { \
        throw std::runtime_error("GlobalArray already acquired - ArrayHandle scoping mistake?"); \
        } \
    }

#define TAG_ALLOCATION(array) { \
    array.setTag(std::string(#array)); \
    }

namespace hoomd
{
namespace detail
{

#ifdef __GNUC__
#define GCC_VERSION (__GNUC__ * 10000 \
                     + __GNUC_MINOR__ * 100 \
                     + __GNUC_PATCHLEVEL__)
/* Test for GCC < 5.0 */
#if GCC_VERSION < 50000
// work around GCC missing feature

#define NO_STD_ALIGN
// https://stackoverflow.com/questions/27064791/stdalign-not-supported-by-g4-9
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=57350
inline void *my_align( std::size_t alignment, std::size_t size,
                    void *&ptr, std::size_t &space ) {
    std::uintptr_t pn = reinterpret_cast< std::uintptr_t >( ptr );
    std::uintptr_t aligned = ( pn + alignment - 1 ) & - alignment;
    std::size_t padding = aligned - pn;
    if ( space < size + padding ) return nullptr;
    space -= padding;
    return ptr = reinterpret_cast< void * >( aligned );
    }
#endif
#endif

template<class T>
class managed_deleter
    {
    public:
        //! Default constructor
        managed_deleter()
            : m_use_device(false), m_N(0), m_allocation_ptr(nullptr), m_allocation_bytes(0)
            {}

        //! Ctor
        /*! \param exec_conf Execution configuration
            \param use_device whether the array is managed or on the host
            \param N number of elements
            \param allocation_ptr true start of allocation, before alignment
         */
        managed_deleter(std::shared_ptr<const ExecutionConfiguration> exec_conf,
            bool use_device, std::size_t N, void *allocation_ptr, size_t allocation_bytes)
            : m_exec_conf(exec_conf), m_use_device(use_device), m_N(N),
            m_allocation_ptr(allocation_ptr), m_allocation_bytes(allocation_bytes)
            { }

        //! Destroy the items and delete the managed array
        /*! \param ptr Start of aligned memory allocation
         */
        void operator()(T *ptr)
            {
            if (ptr == nullptr)
                return;

            assert(m_exec_conf);

            #ifdef ENABLE_CUDA
            if (m_use_device)
                {
                cudaDeviceSynchronize();
                CHECK_CUDA_ERROR();
                }
            #endif

            // we used placement new in the allocation, so call destructors explicitly
            for (std::size_t i = 0; i < m_N; ++i)
                {
                ptr[i].~T();
                }

            #ifdef ENABLE_CUDA
            if (m_use_device)
                {
                this->m_exec_conf->msg->notice(10) << "Freeing " << m_allocation_bytes
                    << " bytes of managed memory." << std::endl;

                cudaFree(m_allocation_ptr);
                CHECK_CUDA_ERROR();
                }
            else
            #endif
                {
                free(m_allocation_ptr);
                }

            // update memory allocation table
            if (m_exec_conf->getMemoryTracer())
                this->m_exec_conf->getMemoryTracer()->unregisterAllocation(reinterpret_cast<const void *>(ptr),
                    sizeof(T)*m_N);
            }

    private:
        std::shared_ptr<const ExecutionConfiguration> m_exec_conf; //!< The execution configuration
        bool m_use_device;     //!< Whether to use cudaMallocManaged
        unsigned int m_N;      //!< Number of elements in array
        void *m_allocation_ptr;  //!< Start of unaligned allocation
        size_t m_allocation_bytes; //!< Size of actual allocation
    };

} // end namespace detail

} // end namespace hoomd

template<class T>
class GlobalArray : public GPUArray<T>
    {
    public:
        //! Empty constructor
        GlobalArray()
            : m_num_elements(0), m_pitch(0), m_height(0), m_acquired(false), m_align_bytes(0)
            { }

        /*! Allocate a 1D array in managed memory
            \param num_elements Number of elements in array
            \param exec_conf The current execution configuration
         */
        GlobalArray(unsigned int num_elements, std::shared_ptr<const ExecutionConfiguration> exec_conf,
            const std::string& tag = std::string() )
            :
            #ifndef ALWAYS_USE_MANAGED_MEMORY
            // explicit copy should be elided
            GPUArray<T>(exec_conf->allConcurrentManagedAccess() ?
                GPUArray<T>(exec_conf) : GPUArray<T>(num_elements, exec_conf)),
            #else
            GPUArray<T>(exec_conf),
            #endif
            m_num_elements(num_elements), m_pitch(num_elements), m_height(1), m_acquired(false), m_tag(tag),
            m_align_bytes(0)
            {
            #ifndef ALWAYS_USE_MANAGED_MEMORY
            if (!this->m_exec_conf->allConcurrentManagedAccess())
                return;
            #endif

            assert(this->m_exec_conf);
            #ifdef ENABLE_CUDA
            if (this->m_exec_conf->isCUDAEnabled())
                {
                // use OS page size as minimum alignment
                m_align_bytes = getpagesize();
                }
            #endif

            if (m_num_elements > 0)
                allocate();
            }

        //! Destructor
        virtual ~GlobalArray()
            { }

        //! Copy constructor
        GlobalArray(const GlobalArray& from)
            : GPUArray<T>(from), m_num_elements(from.m_num_elements),
              m_pitch(from.m_pitch), m_height(from.m_height), m_acquired(false),
              m_tag(from.m_tag), m_align_bytes(from.m_align_bytes)
            {
            if (from.m_data.get())
                {
                allocate();

                checkAcquired(from);

                #ifdef ENABLE_CUDA
                if (this->m_exec_conf && this->m_exec_conf->isCUDAEnabled())
                    {
                    // synchronize all active GPUs
                    auto gpu_map = this->m_exec_conf->getGPUIds();
                    for (int idev = this->m_exec_conf->getNumActiveGPUs() - 1; idev >= 0; --idev)
                        {
                        cudaSetDevice(gpu_map[idev]);
                        cudaDeviceSynchronize();
                        }
                    }
                #endif

                std::copy(from.m_data.get(), from.m_data.get()+from.m_num_elements, m_data.get());
                }
            }

        //! = operator
        GlobalArray& operator=(const GlobalArray& rhs)
            {
            GPUArray<T>::operator=(rhs);

            if (&rhs != this)
                {
                checkAcquired(rhs);
                checkAcquired(*this);

                m_num_elements = rhs.m_num_elements;
                m_pitch = rhs.m_pitch;
                m_height = rhs.m_height;
                m_acquired = false;
                m_align_bytes = rhs.m_align_bytes;
                m_tag = rhs.m_tag;

                if (rhs.m_data.get())
                    {
                    allocate();

                    #ifdef ENABLE_CUDA
                    if (this->m_exec_conf && this->m_exec_conf->isCUDAEnabled())
                        {
                        // synchronize all active GPUs
                        auto gpu_map = this->m_exec_conf->getGPUIds();
                        for (int idev = this->m_exec_conf->getNumActiveGPUs() - 1; idev >= 0; --idev)
                            {
                            cudaSetDevice(gpu_map[idev]);
                            cudaDeviceSynchronize();
                            }
                        }
                    #endif

                    std::copy(rhs.m_data.get(), rhs.m_data.get()+rhs.m_num_elements, m_data.get());
                    }
                else
                    {
                    m_data.release();
                    }
                }

            return *this;
            }

        //! Move constructor, provided for convenience, so std::swap can be used
        GlobalArray(GlobalArray&& other) noexcept
            : GPUArray<T>(std::move(other)),
              m_data(std::move(other.m_data)),
              m_num_elements(std::move(other.m_num_elements)),
              m_pitch(std::move(other.m_pitch)),
              m_height(std::move(other.m_height)),
              m_acquired(std::move(other.m_acquired)),
              m_tag(std::move(other.m_tag)),
              m_align_bytes(std::move(other.m_align_bytes))
            {
            }

        //! Move assignment operator
        GlobalArray& operator=(GlobalArray&& other) noexcept
            {
            // call base clas method
            GPUArray<T>::operator=(std::move(other));

            if (&other != this)
                {
                m_data = std::move(other.m_data);
                m_num_elements = std::move(other.m_num_elements);
                m_pitch = std::move(other.m_pitch);
                m_height = std::move(other.m_height);
                m_acquired = std::move(other.m_acquired);
                m_tag = std::move(other.m_tag);
                m_align_bytes = std::move(other.m_align_bytes);
                }

            return *this;
            }

        /*! Allocate a 2D array in managed memory
            \param width Width of the 2-D array to allocate (in elements)
            \param height Number of rows to allocate in the 2D array
            \param exec_conf Shared pointer to the execution configuration for managing CUDA initialization and shutdown
         */
        GlobalArray(unsigned int width, unsigned int height, std::shared_ptr<const ExecutionConfiguration> exec_conf)
            :
            #ifndef ALWAYS_USE_MANAGED_MEMORY
            // explicit copy should be elided
            GPUArray<T>(exec_conf->allConcurrentManagedAccess() ?
                GPUArray<T>(exec_conf) : GPUArray<T>(width, height, exec_conf)),
            #else
            GPUArray<T>(exec_conf),
            #endif
            m_height(height), m_acquired(false), m_align_bytes(0)
            {
            #ifndef ALWAYS_USE_MANAGED_MEMORY
            if (!this->m_exec_conf->allConcurrentManagedAccess())
                return;
            #endif

            // make m_pitch the next multiple of 16 larger or equal to the given width
            m_pitch = (width + (16 - (width & 15)));

            m_num_elements = m_pitch * m_height;

            #ifdef ENABLE_CUDA
            if (this->m_exec_conf->isCUDAEnabled())
                {
                // use OS page size as minimum alignment
                m_align_bytes = getpagesize();
                }
            #endif

            if (m_num_elements > 0)
                allocate();
            }


        //! Swap the pointers of two GlobalArrays
        inline void swap(GlobalArray &from)
            {
            // call base class method
            GPUArray<T>::swap(from);

            checkAcquired(from);
            checkAcquired(*this);

            std::swap(m_num_elements, from.m_num_elements);
            std::swap(m_data, from.m_data);
            std::swap(m_pitch,from.m_pitch);
            std::swap(m_height,from.m_height);
            std::swap(m_tag, from.m_tag);
            std::swap(m_align_bytes, from.m_align_bytes);
            }

        //! Get the underlying raw pointer
        /*! \returns the content of the underlying smart pointer

            \warning This method doesn't sync the device, so if you are using the pointer to read from while a kernel is
                  writing to it on some stream, this may cause undefined behavior

            It may be used to pass the pointer to API functions, e.g., to set memory hints or prefetch data asynchronously
         */
        const T *get() const
            {
            return m_data.get();
            }

        //! Get the number of elements
        /*!
         - For 1-D allocated GPUArrays, this is the number of elements allocated.
         - For 2-D allocated GPUArrays, this is the \b total number of elements (\a pitch * \a height) allocated
        */
        virtual unsigned int getNumElements() const
            {
            #ifndef ALWAYS_USE_MANAGED_MEMORY
            if (!this->m_exec_conf || !this->m_exec_conf->allConcurrentManagedAccess())
                return GPUArray<T>::getNumElements();
            #endif

            return m_num_elements;
            }

        //! Test if the GPUArray is NULL
        virtual bool isNull() const
            {
            #ifndef ALWAYS_USE_MANAGED_MEMORY
            if (!this->m_exec_conf || ! this->m_exec_conf->allConcurrentManagedAccess())
                return GPUArray<T>::isNull();
            #endif

            return !m_data;
            }

        //! Get the width of the allocated rows in elements
        /*!
         - For 2-D allocated GPUArrays, this is the total width of a row in memory (including the padding added for coalescing)
         - For 1-D allocated GPUArrays, this is the simply the number of elements allocated.
        */
        virtual unsigned int getPitch() const
            {
            #ifndef ALWAYS_USE_MANAGED_MEMORY
            if (!this->m_exec_conf || ! this->m_exec_conf->allConcurrentManagedAccess())
                return GPUArray<T>::getPitch();
            #endif

            return m_pitch;
            }

        //! Get the number of rows allocated
        /*!
         - For 2-D allocated GPUArrays, this is the height given to the constructor
         - For 1-D allocated GPUArrays, this is the simply 1.
        */
        virtual unsigned int getHeight() const
            {
            #ifndef ALWAYS_USE_MANAGED_MEMORY
            if (!this->m_exec_conf || ! this->m_exec_conf->allConcurrentManagedAccess())
                return GPUArray<T>::getHeight();
            #endif

            return m_height;
            }

        //! Resize the GlobalArray
        /*! This method resizes the array by allocating a new array and copying over the elements
            from the old array. Resizing is a slow operation.
        */
        virtual void resize(unsigned int num_elements)
            {
            #ifndef ALWAYS_USE_MANAGED_MEMORY
            if (! this->m_exec_conf || ! this->m_exec_conf->allConcurrentManagedAccess())
                {
                GPUArray<T>::resize(num_elements);
                return;
                }
            #endif

            checkAcquired(*this);

            // store old data in temporary vector
            std::vector<T> old(m_num_elements);
            std::copy(m_data.get(), m_data.get()+m_num_elements, old.begin());

            unsigned int num_copy_elements = m_num_elements > num_elements ? num_elements : m_num_elements;

            m_num_elements = num_elements;

            assert(m_num_elements > 0);

            allocate();

            #ifdef ENABLE_CUDA
            if (this->m_exec_conf->isCUDAEnabled())
                {
                // synchronize all active GPUs
                auto gpu_map = this->m_exec_conf->getGPUIds();
                for (int idev = this->m_exec_conf->getNumActiveGPUs() - 1; idev >= 0; --idev)
                    {
                    cudaSetDevice(gpu_map[idev]);
                    cudaDeviceSynchronize();
                    }
                }
            #endif

            std::copy(old.begin(), old.begin() + num_copy_elements, m_data.get());

            m_pitch = m_num_elements;
            m_height = 1;
            }

        //! Resize a 2D GlobalArray
        virtual void resize(unsigned int width, unsigned int height)
            {
            assert(this->m_exec_conf);

            #ifndef ALWAYS_USE_MANAGED_MEMORY
            if (! this->m_exec_conf->allConcurrentManagedAccess())
                {
                GPUArray<T>::resize(width, height);
                return;
                }
            #endif

            checkAcquired(*this);

            // make m_pitch the next multiple of 16 larger or equal to the given width
            unsigned int pitch = (width + (16 - (width & 15)));

            // store old data in temporary vector
            std::vector<T> old(m_num_elements);
            std::copy(m_data.get(), m_data.get()+m_num_elements, old.begin());

            m_num_elements = pitch * height;

            assert(m_num_elements > 0);

            allocate();

            #ifdef ENABLE_CUDA
            if (this->m_exec_conf->isCUDAEnabled())
                {
                // synchronize all active GPUs
                auto gpu_map = this->m_exec_conf->getGPUIds();
                for (int idev = this->m_exec_conf->getNumActiveGPUs() - 1; idev >= 0; --idev)
                    {
                    cudaSetDevice(gpu_map[idev]);
                    cudaDeviceSynchronize();
                    }
                }
            #endif

            // copy over data
            // every column is copied separately such as to align with the new pitch
            unsigned int num_copy_rows = m_height > height ? height : m_height;
            unsigned int num_copy_columns = m_pitch > pitch ? pitch : m_pitch;
            for (unsigned int i = 0; i < num_copy_rows; i++)
                std::copy(old.begin() + i*m_pitch, old.begin() + i*m_pitch + num_copy_columns, m_data.get() + i * pitch);

            m_height = height;
            m_pitch  = pitch;
            }

        //! Set an optional tag for memory profiling
        /*! tag The name of this allocation
         */
        void setTag(const std::string& tag)
            {
            // update the tag
            m_tag = tag;
            if (this->m_exec_conf && this->m_exec_conf->getMemoryTracer() && get() )
                this->m_exec_conf->getMemoryTracer()->updateTag(reinterpret_cast<const void*>(get()),
                    sizeof(T)*m_num_elements, m_tag);
            }

    protected:
        virtual inline T* acquire(const access_location::Enum location, const access_mode::Enum mode
        #ifdef ENABLE_CUDA
                         , bool async = false
        #endif
                        ) const
            {
            #ifndef ALWAYS_USE_MANAGED_MEMORY
            if (!this->m_exec_conf || ! this->m_exec_conf->allConcurrentManagedAccess())
                return GPUArray<T>::acquire(location, mode
                    #ifdef ENABLE_CUDA
                                     , async
                    #endif
                    );
            #endif

            checkAcquired(*this);

            #ifdef ENABLE_CUDA
            bool use_device = this->m_exec_conf && this->m_exec_conf->isCUDAEnabled();
            if (!isNull() && use_device && location == access_location::host)
                {
                // synchronize all active GPUs
                auto gpu_map = this->m_exec_conf->getGPUIds();
                for (int idev = this->m_exec_conf->getNumActiveGPUs() - 1; idev >= 0; --idev)
                    {
                    cudaSetDevice(gpu_map[idev]);
                    cudaDeviceSynchronize();
                    }
                }
            #endif

            m_acquired = true;

            return m_data.get();
            }

        //! Release the data pointer
        virtual inline void release() const
            {
            #ifndef ALWAYS_USE_MANAGED_MEMORY
            if (!this->m_exec_conf || ! this->m_exec_conf->allConcurrentManagedAccess())
                {
                GPUArray<T>::release();
                return;
                }
            #endif

            m_acquired = false;
            }

        //! Returns the acquire state
        virtual inline bool isAcquired() const
            {
            #ifndef ALWAYS_USE_MANAGED_MEMORY
            if (!this->m_exec_conf || ! this->m_exec_conf->allConcurrentManagedAccess())
                return GPUArray<T>::isAcquired();
            #endif

            return m_acquired;
            }

    private:
        std::unique_ptr<T, hoomd::detail::managed_deleter<T> > m_data; //!< Smart ptr to managed or host memory, with custom deleter

        unsigned int m_num_elements; //!< Number of elements in array
        unsigned int m_pitch;  //!< Pitch of 2D array
        unsigned int m_height; //!< Height of 2D array

        mutable bool m_acquired;       //!< Tracks if the array is already acquired

        std::string m_tag;     //!< Name tag of this buffer (optional)

        unsigned int m_align_bytes; //!< Size of alignment in bytes

        //! Allocate the managed array and construct the items
        void allocate()
            {
            assert(m_num_elements);

            void *ptr = nullptr;
            void *allocation_ptr = nullptr;
            bool use_device = this->m_exec_conf && this->m_exec_conf->isCUDAEnabled();
            size_t allocation_bytes;

            #ifdef ENABLE_CUDA
            if (use_device)
                {
                allocation_bytes = m_num_elements*sizeof(T);

                if (m_align_bytes)
                    allocation_bytes = ((m_num_elements*sizeof(T))/m_align_bytes + 1)*m_align_bytes;

                this->m_exec_conf->msg->notice(10) << "Allocating " << allocation_bytes
                    << " bytes of managed memory." << std::endl;

                cudaMallocManaged(&ptr, allocation_bytes, cudaMemAttachGlobal);
                CHECK_CUDA_ERROR();

                allocation_ptr = ptr;

                if (m_align_bytes)
                    {
                    // align to align_size
                    #ifndef NO_STD_ALIGN
                    ptr = std::align(m_align_bytes,m_num_elements*sizeof(T),ptr,allocation_bytes);
                    #else
                    ptr = hoomd::detail::my_align(m_align_bytes,m_num_elements*sizeof(T),ptr,allocation_bytes);
                    #endif

                    if (!ptr)
                        throw std::runtime_error("GlobalArray: Error aligning managed memory");
                    }
                }
            else
            #endif
                {
                int retval = posix_memalign((void **) &ptr, 32, m_num_elements*sizeof(T));
                if (retval != 0)
                    {
                    throw std::runtime_error("Error allocating aligned memory");
                    }
                allocation_bytes = m_num_elements*sizeof(T);
                allocation_ptr = ptr;
                }

            #ifdef ENABLE_CUDA
            if (use_device)
                {
                cudaDeviceSynchronize();
                CHECK_CUDA_ERROR();
                }
            #endif

            // construct objects explicitly using placement new
            for (std::size_t i = 0; i < m_num_elements; ++i) ::new ((void **) &((T *)ptr)[i]) T;

            // store allocation and custom deleter in unique_ptr
            auto deleter = hoomd::detail::managed_deleter<T>(this->m_exec_conf,use_device,
                m_num_elements, allocation_ptr, allocation_bytes);
            m_data = std::unique_ptr<T, decltype(deleter)>(reinterpret_cast<T *>(ptr), deleter);

            // register new allocation
            if (this->m_exec_conf->getMemoryTracer())
                this->m_exec_conf->getMemoryTracer()->registerAllocation(reinterpret_cast<const void *>(m_data.get()),
                    sizeof(T)*m_num_elements, typeid(T).name(), m_tag);
            }
    };
