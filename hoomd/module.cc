// Copyright (c) 2009-2016 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.


// Maintainer: joaander All developers are free to add the calls needed to export their modules

#include "HOOMDMath.h"
#include "ExecutionConfiguration.h"
#include "ClockSource.h"
#include "Profiler.h"
#include "ParticleData.h"
#include "SystemDefinition.h"
#include "BondedGroupData.h"
#include "Initializers.h"
#include "GetarInitializer.h"
#include "GSDReader.h"
#include "Compute.h"
#include "ComputeThermo.h"
#include "CellList.h"
#include "CellListStencil.h"
#include "ForceCompute.h"
#include "ForceConstraint.h"
#include "ConstForceCompute.h"
#include "Analyzer.h"
#include "IMDInterface.h"
#include "DCDDumpWriter.h"
#include "GetarDumpWriter.h"
#include "GSDDumpWriter.h"
#include "Logger.h"
#include "CallbackAnalyzer.h"
#include "Updater.h"
#include "Integrator.h"
#include "SFCPackUpdater.h"
#include "BoxResizeUpdater.h"
#include "System.h"
#include "Variant.h"
#include "Messenger.h"
#include "SnapshotSystemData.h"

// include GPU classes
#ifdef ENABLE_CUDA
#include <cuda.h>
#include "CellListGPU.h"
#include "ComputeThermoGPU.h"
#include "SFCPackUpdaterGPU.h"

#include <cuda_profiler_api.h>
#endif

// include MPI classes
#ifdef ENABLE_MPI
#include "Communicator.h"
#include "DomainDecomposition.h"
#include "LoadBalancer.h"

#ifdef ENABLE_CUDA
#include "CommunicatorGPU.h"
#include "LoadBalancerGPU.h"
#endif // ENABLE_CUDA
#endif // ENABLE_MPI

#include "SignalHandler.h"

#include "HOOMDVersion.h"
#include "hoomd/extern/num_util.h"

#include <hoomd/extern/pybind/include/pybind11/pybind11.h>
#include <hoomd/extern/pybind/include/pybind11/stl_bind.h>

#include <iostream>
#include <sstream>
#include <fstream>
using namespace std;

/*! \file hoomd_module.cc
    \brief Brings all of the export_* functions together to export the hoomd python module
*/

/* numpy is terrible (see /opt/local/Library/Frameworks/Python.framework/Versions/2.7/
lib/python2.7/site-packages/numpy/core/generate_numpy_array.py)
The following #defines help get around this
*/

#if PY_VERSION_HEX >= 0x03000000
#define MY_PY_VER_3x
#else
#define MY_PY_VER_2x
#endif

#ifdef MY_PY_VER_3x
void *my_import_array()
    {
    import_array();
    return NULL;
    }
#endif
#ifdef MY_PY_VER_2x
void my_import_array()
    {
    import_array();
    }
#endif

//! Method for getting the current version of HOOMD
/*! \returns Current HOOMD version identification string
*/
string get_hoomd_version()
    {
    ostringstream ver;
    // always outputting main version number: #402
    ver << "HOOMD-blue " << HOOMD_VERSION << endl;

    return ver.str();
    }

//! Layer for omp_get_num_procs()
int get_num_procs()
    {
    return 1;
    }

//! Get the hoomd version as a tuple
pybind11::object get_hoomd_version_tuple()
    {
    return pybind11::make_tuple(HOOMD_VERSION_MAJOR, HOOMD_VERSION_MINOR, HOOMD_VERSION_PATCH);
    }

//! Get the CUDA version as a tuple
pybind11::object get_cuda_version_tuple()
    {
    #ifdef ENABLE_CUDA
    int major = CUDA_VERSION / 1000;
    int minor = CUDA_VERSION / 10 % 100;
    return pybind11::make_tuple(major, minor);
    #else
    return pybind11::make_tuple(0,0);
    #endif
    }

//! Get the compiler version
string get_compiler_version()
    {
    #if defined(__GNUC__) && !(defined(__clang__) || defined(__INTEL_COMPILER))
    ostringstream o;
    o << "gcc " << __GNUC__ << "." << __GNUC_MINOR__ << "." <<  __GNUC_PATCHLEVEL__;
    return o.str();

    #elif defined(__clang__)
    ostringstream o;
    o << "clang " << __clang_major__ << "." << __clang_minor__ << "." <<  __clang_patchlevel__;
    return o.str();

    #elif defined(__INTEL_COMPILER)
    ostringstream o;
    o << "icc " << __INTEL_COMPILER;
    return o.str();

    #else
    return string("unknown");

    #endif
    }

//! Determine availability of MPI support
bool is_MPI_available()
   {
   return
#ifdef ENABLE_MPI
       true;
#else
       false;
#endif
    }

void mpi_barrier_world()
    {
    #ifdef ENABLE_MPI
    MPI_Barrier(MPI_COMM_WORLD);
    #endif
    }

//! Start the CUDA profiler
void cuda_profile_start()
    {
    #ifdef ENABLE_CUDA
    cudaDeviceSynchronize();
    cudaProfilerStart();
    #endif
    }

//! Stop the CUDA profiler
void cuda_profile_stop()
    {
    #ifdef ENABLE_CUDA
    cudaDeviceSynchronize();
    cudaProfilerStop();
    #endif
    }

// values used in measuring hoomd launch timing
unsigned int hoomd_launch_time, hoomd_start_time, hoomd_mpi_init_time;
bool hoomd_launch_timing=false;

#ifdef ENABLE_MPI
//! Environment variables needed for setting up MPI
char env_enable_mpi_cuda[] = "MV2_USE_CUDA=1";

//! Initialize the MPI environment
void initialize_mpi()
    {
    #ifdef ENABLE_MPI_CUDA
    // if we are using an MPI-CUDA implementation, enable this feature
    // before the MPI_Init
    putenv(env_enable_mpi_cuda);
    #endif

    // benchmark hoomd launch times
    if (getenv("HOOMD_LAUNCH_TIME"))
        {
        // get the time that mpirun was called
        hoomd_launch_time = atoi(getenv("HOOMD_LAUNCH_TIME"));

        // compute the number of seconds to get here
        timeval t;
        gettimeofday(&t, NULL);
        hoomd_start_time = t.tv_sec - hoomd_launch_time;
        hoomd_launch_timing = true;
        }

    // initalize MPI
    MPI_Init(0, (char ***) NULL);

    if (hoomd_launch_timing)
        {
        // compute the number of seconds to get past mpi_init
        timeval t;
        gettimeofday(&t, NULL);
        hoomd_mpi_init_time = t.tv_sec - hoomd_launch_time;
        }
    }

//! Get the processor name associated to this rank
string get_mpi_proc_name()
    {
    char proc_name[MPI_MAX_PROCESSOR_NAME];
    int name_len;
    MPI_Get_processor_name(proc_name, &name_len);
    return string(proc_name);
    }

//! Finalize MPI environment
void finalize_mpi()
    {
    MPI_Finalize();
    }
#endif

//! Abort MPI runs
void abort_mpi(std::shared_ptr<ExecutionConfiguration> exec_conf)
    {
    #ifdef ENABLE_MPI
    if(exec_conf->getNRanksGlobal() > 1)
        {
        MPI_Abort(exec_conf->getMPICommunicator(), MPI_ERR_OTHER);
        }
    #endif
    }

//! Create the python module
/*! each class setup their own python exports in a function export_ClassName
    create the hoomd python module and define the exports here.
*/
PYBIND11_PLUGIN(_hoomd)
    {
    pybind11::module m("_hoomd");

    #ifdef ENABLE_MPI
    // initialize MPI early
    initialize_mpi();

    // register clean-up function
    Py_AtExit(finalize_mpi);
    m.def("get_mpi_proc_name", get_mpi_proc_name);
    #endif

    // setup needed for numpy
    my_import_array();

    m.def("abort_mpi", abort_mpi);
    m.def("mpi_barrier_world", mpi_barrier_world);

    m.def("hoomd_compile_flags", &hoomd_compile_flags);
    m.def("output_version_info", &output_version_info);
    m.def("get_hoomd_version", &get_hoomd_version);

    m.def("get_num_procs", &get_num_procs);
    m.attr("__version__") = get_hoomd_version_tuple();
    m.attr("__git_sha1__") = pybind11::str(HOOMD_GIT_SHA1);
    m.attr("__git_refspec__") = pybind11::str(HOOMD_GIT_REFSPEC);
    m.attr("__cuda_version__") = get_cuda_version_tuple();
    m.attr("__compiler_version__") = pybind11::str(get_compiler_version());

    m.def("is_MPI_available", &is_MPI_available);

    m.def("cuda_profile_start", &cuda_profile_start);
    m.def("cuda_profile_stop", &cuda_profile_stop);

    pybind11::bind_vector<Scalar>(m,"std_vector_scalar");
    pybind11::bind_vector< std::vector<Scalar> >(m,"std_vector2_scalar");
    pybind11::bind_vector<string>(m,"std_vector_string");
    pybind11::bind_vector<unsigned int>(m,"std_vector_uint");
    pybind11::bind_vector<int>(m,"std_vector_int");
    pybind11::bind_vector<Scalar3>(m,"std_vector_scalar3");
    pybind11::bind_vector<Scalar4>(m,"std_vector_scalar4");

    InstallSIGINTHandler();

    // utils
    export_hoomd_math_functions(m);
    export_ClockSource(m);
    export_Profiler(m);

    // data structures
    export_BoxDim(m);
    export_ParticleData(m);
    export_SnapshotParticleData(m);
    export_ExecutionConfiguration(m);
    export_SystemDefinition(m);
    export_SnapshotSystemData(m);
    export_BondedGroupData<BondData,Bond>(m,"BondData","BondDataSnapshot");
    export_BondedGroupData<AngleData,Angle>(m,"AngleData","AngleDataSnapshot");
    export_BondedGroupData<DihedralData,Dihedral>(m,"DihedralData","DihedralDataSnapshot");
    export_BondedGroupData<ImproperData,Dihedral>(m,"ImproperData","ImproperDataSnapshot",false);
    export_BondedGroupData<ConstraintData,Constraint>(m,"ConstraintData","ConstraintDataSnapshot");

    // initializers
    export_GSDReader(m);
    getardump::export_GetarInitializer(m);

    // computes
    export_Compute(m);
    export_ComputeThermo(m);
    export_CellList(m);
    export_CellListStencil(m);
    export_ForceCompute(m);
    export_ForceConstraint(m);
    export_ConstForceCompute(m);

#ifdef ENABLE_CUDA
    export_CellListGPU(m);
    export_ComputeThermoGPU(m);
#endif

    // analyzers
    export_Analyzer(m);
    export_IMDInterface(m);
    export_DCDDumpWriter(m);
    getardump::export_GetarDumpWriter(m);
    export_GSDDumpWriter(m);
    export_Logger(m);
    export_CallbackAnalyzer(m);
    export_ParticleGroup(m);

    // updaters
    export_Updater(m);
    export_Integrator(m);
    export_BoxResizeUpdater(m);
    export_SFCPackUpdater(m);
#ifdef ENABLE_CUDA
    export_SFCPackUpdaterGPU(m);
#endif

#ifdef ENABLE_MPI
    export_Communicator(m);
    export_DomainDecomposition(m);
    export_LoadBalancer(m);
#ifdef ENABLE_CUDA
    export_CommunicatorGPU(m);
    export_LoadBalancerGPU(m);
#endif // ENABLE_CUDA
#endif // ENABLE_MPI

    // system
    export_System(m);

    // variant
    export_Variant(m);

    // messenger
    export_Messenger(m);

    return m.ptr();
    }
