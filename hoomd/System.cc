// Copyright (c) 2009-2019 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.


// Maintainer: joaander

/*! \file System.cc
    \brief Defines the System class
*/

#include "System.h"
#include "SignalHandler.h"

#ifdef ENABLE_MPI
#include "Communicator.h"
#endif

// #include <pybind11/pybind11.h>
#include <stdexcept>
#include <time.h>
#include <pybind11/stl_bind.h>

PYBIND11_MAKE_OPAQUE(std::vector<std::pair<std::shared_ptr<Analyzer>,
                                 std::shared_ptr<Trigger> > >);

PYBIND11_MAKE_OPAQUE(std::vector<std::pair<std::shared_ptr<Updater>,
                                 std::shared_ptr<Trigger> > >);

PYBIND11_MAKE_OPAQUE(std::vector<std::shared_ptr<Tuner> >);

using namespace std;
namespace py = pybind11;

PyObject* walltimeLimitExceptionTypeObj = 0;

/*! \param sysdef SystemDefinition for the system to be simulated
    \param initial_tstep Initial time step of the simulation

    \post The System is constructed with no attached computes, updaters,
    analyzers or integrators. Profiling defaults to disabled and
    statistics are printed every 10 seconds.
*/
System::System(std::shared_ptr<SystemDefinition> sysdef, unsigned int initial_tstep)
        : m_sysdef(sysdef), m_start_tstep(initial_tstep), m_end_tstep(0), m_cur_tstep(initial_tstep), m_cur_tps(0),
        m_med_tps(0), m_last_status_time(0), m_last_status_tstep(initial_tstep), m_quiet_run(false),
        m_profile(false), m_stats_period(10)
    {
    // sanity check
    assert(m_sysdef);
    m_exec_conf = m_sysdef->getParticleData()->getExecConf();

    // initialize tps array
    m_tps_list.resize(0);
    #ifdef ENABLE_MPI
    // the initial time step is defined on the root processor
    if (m_sysdef->getParticleData()->getDomainDecomposition())
        {
        bcast(m_start_tstep, 0, m_exec_conf->getMPICommunicator());
        bcast(m_cur_tstep, 0, m_exec_conf->getMPICommunicator());
        bcast(m_last_status_tstep, 0, m_exec_conf->getMPICommunicator());
        }
    #endif
    }

/*! \param analyzer Shared pointer to the Analyzer to add
    \param name A unique name to identify the Analyzer by
    \param period Analyzer::analyze() will be called for every time step that is a multiple
    of \a period.
    \param phase Phase offset. A value of -1 sets no phase, updates start on the current step. A value of 0 or greater
                 sets the analyzer to run at steps where (step % (period + phase)) == 0.

    All Analyzers will be called, in the order that they are added, and with the specified
    \a period during time step calculations performed when run() is called. An analyzer
    can be prevented from running in future runs by removing it (removeAnalyzer()) before
    calling run()
*/
/*! \param compute Shared pointer to the Compute to add
    \param name Unique name to assign to this Compute

    Computes are added to the System only as a convenience for naming,
    saving to restart files, and to activate profiling. They are never
    directly called by the system.
*/
void System::addCompute(std::shared_ptr<Compute> compute, const std::string& name)
    {
    // sanity check
    assert(compute);

    // check if the name is unique
    map< string, std::shared_ptr<Compute> >::iterator i = m_computes.find(name);
    if (i == m_computes.end())
        m_computes[name] = compute;
    else
        {
        m_exec_conf->msg->error() << "Compute " << name << " already exists" << endl;
        throw runtime_error("System: cannot add compute");
        }
    }

/*! \param compute Shared pointer to the Compute to add
    \param name Unique name to assign to this Compute

    Computes are added to the System only as a convenience for naming,
    saving to restart files, and to activate profiling. They are never
    directly called by the system. This method adds a compute, overwriting
    any existing compute by the same name.
*/
void System::overwriteCompute(std::shared_ptr<Compute> compute, const std::string& name)
    {
    // sanity check
    assert(compute);

    m_computes[name] = compute;
    }

/*! \param name Name of the Compute to remove
*/
void System::removeCompute(const std::string& name)
    {
    // see if the compute exists to be removed
    map< string, std::shared_ptr<Compute> >::iterator i = m_computes.find(name);
    if (i == m_computes.end())
        {
        m_exec_conf->msg->error() << "Compute " << name << " not found" << endl;
        throw runtime_error("System: cannot remove compute");
        }
    else
        m_computes.erase(i);
    }

/*! \param name Name of the compute to access
    \returns A shared pointer to the Compute as provided previously by addCompute()
*/
std::shared_ptr<Compute> System::getCompute(const std::string& name)
    {
    // see if the compute even exists first
    map< string, std::shared_ptr<Compute> >::iterator i = m_computes.find(name);
    if (i == m_computes.end())
        {
        m_exec_conf->msg->error() << "Compute " << name << " not found" << endl;
        throw runtime_error("System: cannot retrieve compute");
        return std::shared_ptr<Compute>();
        }
    else
        return m_computes[name];
    }

// -------------- Integrator methods

/*! \param integrator Updater to set as the Integrator for this System
*/
void System::setIntegrator(std::shared_ptr<Integrator> integrator)
    {
    m_integrator = integrator;
    }

/*! \returns A shared pointer to the Integrator for this System
*/
std::shared_ptr<Integrator> System::getIntegrator()
    {
    return m_integrator;
    }

#ifdef ENABLE_MPI
// -------------- Methods for communication
void System::setCommunicator(std::shared_ptr<Communicator> comm)
    {
    m_comm = comm;
    }
#endif

// -------------- Methods for running the simulation

/*! \param nsteps Number of simulation steps to run
    \param limit_hours Number of hours to run for (0.0 => infinity)
    \param cb_frequency Modulus of timestep number when to call the callback (0 = at end)
    \param callback Python function to be called periodically during run.
    \param limit_multiple Only allow \a limit_hours to break the simulation at steps that are a multiple of
           \a limit_multiple .

    During each simulation step, all added Analyzers and
    Updaters are called, then the Integrator to move the system
    forward one step in time. This is repeated \a nsteps times,
    or until a \a limit_hours hours have passed.

    run() can be called as many times as the user wishes:
    each time, it will continue at the time step where it left off.
*/

void System::run(unsigned int nsteps, unsigned int cb_frequency,
                 py::object callback, double limit_hours,
                 unsigned int limit_multiple)
    {
    // track if a wall clock timeout ended the run
    unsigned int timeout_end_run = 0;
    char *walltime_stop = getenv("HOOMD_WALLTIME_STOP");

    m_start_tstep = m_cur_tstep;
    m_end_tstep = m_cur_tstep + nsteps;

    // initialize the last status time
    int64_t initial_time = m_clk.getTime();
    m_last_status_time = initial_time;
    setupProfiling();

    // preset the flags before the run loop so that any analyzers/updaters run on step 0 have the info they need
    // but set the flags before prepRun, as prepRun may remove some flags that it cannot generate on the first step
    m_sysdef->getParticleData()->setFlags(determineFlags(m_cur_tstep));

    resetStats();

    #ifdef ENABLE_MPI
    if (m_comm)
        {
        // make sure we start off with a migration substep
        m_comm->forceMigrate();

        // communicate here, to run before the Logger
        m_comm->communicate(m_cur_tstep);
        }
    #endif

    // Prepare the run
    if (!m_integrator)
        {
        m_exec_conf->msg->warning() << "You are running without an integrator" << endl;
        }
    else
        {
        m_integrator->prepRun(m_cur_tstep);
        }

    // handle time steps
    for ( ; m_cur_tstep < m_end_tstep; m_cur_tstep++)
        {
        // check the clock and output a status line if needed
        uint64_t cur_time = m_clk.getTime();

        // check if the time limit has exceeded
        if (limit_hours != 0.0f)
            {
            if (m_cur_tstep % limit_multiple == 0)
                {
                int64_t time_limit = int64_t(limit_hours * 3600.0 * 1e9);
                if (int64_t(cur_time) - initial_time > time_limit)
                    timeout_end_run = 1;

                #ifdef ENABLE_MPI
                // if any processor wants to end the run, end it on all processors
                if (m_comm)
                    {
                    if (m_profiler) m_profiler->push("MPI sync");
                    MPI_Allreduce(MPI_IN_PLACE, &timeout_end_run, 1, MPI_INT, MPI_SUM, m_exec_conf->getMPICommunicator());
                    if (m_profiler) m_profiler->pop();
                    }
                #endif

                if (timeout_end_run)
                    {
                    m_exec_conf->msg->notice(2) << "Ending run at time step " << m_cur_tstep << " as " << limit_hours << " hours have passed" << endl;
                    break;
                    }
                }
            }

        // check if wall clock time limit has passed
        if (walltime_stop != NULL)
            {
            if (m_cur_tstep % limit_multiple == 0)
                {
                time_t end_time = atoi(walltime_stop);
                time_t predict_time = time(NULL);

                // predict when the next limit_multiple will be reached
                if (m_med_tps != Scalar(0))
                    predict_time += time_t(Scalar(limit_multiple) / m_med_tps);

                if (predict_time >= end_time)
                    timeout_end_run = 1;

                #ifdef ENABLE_MPI
                // if any processor wants to end the run, end it on all processors
                if (m_comm)
                    {
                    if (m_profiler) m_profiler->push("MPI sync");
                    MPI_Allreduce(MPI_IN_PLACE, &timeout_end_run, 1, MPI_INT, MPI_SUM, m_exec_conf->getMPICommunicator());
                    if (m_profiler) m_profiler->pop();
                    }
                #endif

                if (timeout_end_run)
                    {
                    m_exec_conf->msg->notice(2) << "Ending run before HOOMD_WALLTIME_STOP - current time step: " << m_cur_tstep << endl;
                    break;
                    }
                }
            }

        // execute python callback, if present and needed
        // a negative return value indicates immediate end of run.
        if (!callback.is(py::none()) && (cb_frequency > 0) && (m_cur_tstep % cb_frequency == 0))
            {
            py::object rv = callback(m_cur_tstep);
            if (!rv.is(py::none()))
                {
                int extracted_rv = py::cast<int>(rv);
                if (extracted_rv < 0)
                    {
                    m_exec_conf->msg->notice(2) << "End of run requested by python callback at step "
                         << m_cur_tstep << " / " << m_end_tstep << endl;
                    break;
                    }
                }
            }

        if (cur_time - m_last_status_time >= uint64_t(m_stats_period)*uint64_t(1000000000))
            {
            generateStatusLine();
            m_last_status_time = cur_time;
            m_last_status_tstep = m_cur_tstep;

            // check for any CUDA errors
            #ifdef ENABLE_HIP
            if (m_exec_conf->isCUDAEnabled())
                {
                CHECK_CUDA_ERROR();
                }
            #endif
            }

        // execute analyzers
        for (auto &analyzer_trigger_pair: m_analyzers)
            {
            if ((*analyzer_trigger_pair.second)(m_cur_tstep))
                analyzer_trigger_pair.first->analyze(m_cur_tstep);
            }

        // execute updaters
        for (auto &updater_trigger_pair: m_updaters)
            {
            if ((*updater_trigger_pair.second)(m_cur_tstep))
                updater_trigger_pair.first->update(m_cur_tstep);
            }

        for (auto &tuner: m_tuners)
            {
            if ((*tuner->getTrigger())(m_cur_tstep))
                tuner->update(m_cur_tstep);
            }

        // look ahead to the next time step and see which analyzers and updaters will be executed
        // or together all of their requested PDataFlags to determine the flags to set for this time step
        m_sysdef->getParticleData()->setFlags(determineFlags(m_cur_tstep+1));

        // execute the integrator
        if (m_integrator)
            m_integrator->update(m_cur_tstep);

        // quit if Ctrl-C was pressed
        if (g_sigint_recvd)
            {
            g_sigint_recvd = 0;
            return;
            }
        }

    // generate a final status line
    generateStatusLine();
    m_last_status_tstep = m_cur_tstep;

    // execute python callback, if present and needed
    if (!callback.is(py::none()) && (cb_frequency == 0))
        {
        callback(m_cur_tstep);
        }

    // calculate average TPS
    Scalar TPS = Scalar(m_cur_tstep - m_start_tstep) / Scalar(m_clk.getTime() - initial_time) * Scalar(1e9);

    m_last_TPS = TPS;

    #ifdef ENABLE_MPI
    // make sure all ranks return the same TPS
    if (m_comm)
        bcast(m_last_TPS, 0, m_exec_conf->getMPICommunicator());
    #endif

    if (!m_quiet_run)
        m_exec_conf->msg->notice(1) << "Average TPS: " << m_last_TPS << endl;

    // write out the profile data
    if (m_profiler)
        m_exec_conf->msg->notice(1) << *m_profiler;

    if (!m_quiet_run)
        printStats();

    // throw a WalltimeLimitReached exception if we timed out, but only if the user is using the HOOMD_WALLTIME_STOP feature
    if (timeout_end_run && walltime_stop != NULL)
        {
        PyErr_SetString(walltimeLimitExceptionTypeObj, "HOOMD_WALLTIME_STOP reached");
        throw py::error_already_set();
        }
    }

/*! \param enable Set to true to enable profiling during calls to run()
*/
void System::enableProfiler(bool enable)
    {
    m_profile = enable;
    }

/*! \param logger Logger to register computes and updaters with
    All computes and updaters registered with the system are also registered with the logger.
*/
void System::registerLogger(std::shared_ptr<Logger> logger)
    {
    // set the profiler on everything
    if (m_integrator)
        logger->registerUpdater(m_integrator);

    // updaters
	for (auto &updater_trigger_pair: m_updaters)
		logger->registerUpdater(updater_trigger_pair.first);

    // computes
    map< string, std::shared_ptr<Compute> >::iterator compute;
    for (compute = m_computes.begin(); compute != m_computes.end(); ++compute)
        logger->registerCompute(compute->second);
    }

/*! \param seconds Period between statistics output in seconds
*/
void System::setStatsPeriod(unsigned int seconds)
    {
    m_stats_period = seconds;
    }

/*! \param enable Enable/disable autotuning
    \param period period (approximate) in time steps when returning occurs
*/
void System::setAutotunerParams(bool enabled, unsigned int period)
    {
    // set the autotuner parameters on everything
    if (m_integrator)
        m_integrator->setAutotunerParams(enabled, period);

    // analyzers
	for (auto &analyzer_trigger_pair: m_analyzers)
		analyzer_trigger_pair.first->setAutotunerParams(enabled, period);

    // updaters
	for (auto &updater_trigger_pair: m_updaters)
		updater_trigger_pair.first->setAutotunerParams(enabled, period);

    // computes
    map< string, std::shared_ptr<Compute> >::iterator compute;
    for (compute = m_computes.begin(); compute != m_computes.end(); ++compute)
        compute->second->setAutotunerParams(enabled, period);

    #ifdef ENABLE_MPI
    if (m_comm)
        m_comm->setAutotunerParams(enabled, period);
    #endif
    }

// --------- Steps in the simulation run implemented in helper functions

void System::setupProfiling()
    {
    if (m_profile)
        m_profiler = std::shared_ptr<Profiler>(new Profiler("Simulation"));
    else
        m_profiler = std::shared_ptr<Profiler>();

    // set the profiler on everything
    if (m_integrator)
        m_integrator->setProfiler(m_profiler);
    m_sysdef->getParticleData()->setProfiler(m_profiler);
    m_sysdef->getBondData()->setProfiler(m_profiler);
    m_sysdef->getPairData()->setProfiler(m_profiler);
    m_sysdef->getAngleData()->setProfiler(m_profiler);
    m_sysdef->getDihedralData()->setProfiler(m_profiler);
    m_sysdef->getImproperData()->setProfiler(m_profiler);
    m_sysdef->getConstraintData()->setProfiler(m_profiler);

    // analyzers
	for (auto &analyzer_trigger_pair: m_analyzers)
		analyzer_trigger_pair.first->setProfiler(m_profiler);

    // updaters
	for (auto &updater_trigger_pair: m_updaters)
		updater_trigger_pair.first->setProfiler(m_profiler);

    // computes
    map< string, std::shared_ptr<Compute> >::iterator compute;
    for (compute = m_computes.begin(); compute != m_computes.end(); ++compute)
        compute->second->setProfiler(m_profiler);

#ifdef ENABLE_MPI
    // communicator
    if (m_comm)
        m_comm->setProfiler(m_profiler);
#endif
    }

void System::printStats()
    {
    m_exec_conf->msg->notice(1) << "---------" << endl;
    // print the stats for everything
    if (m_integrator)
        m_integrator->printStats();

    // analyzers
	for (auto &analyzer_trigger_pair: m_analyzers)
		analyzer_trigger_pair.first->printStats();

    // updaters
    for (auto &updater_trigger_pair: m_updaters)
        updater_trigger_pair.first->printStats();

    // computes
    map< string, std::shared_ptr<Compute> >::iterator compute;
    for (compute = m_computes.begin(); compute != m_computes.end(); ++compute)
        compute->second->printStats();

    // output memory trace information
    if (m_exec_conf->getMemoryTracer())
        m_exec_conf->getMemoryTracer()->outputTraces(m_exec_conf->msg);
    }

void System::resetStats()
    {
    if (m_integrator)
        m_integrator->resetStats();

    // analyzers
	for (auto &analyzer_trigger_pair: m_analyzers)
		analyzer_trigger_pair.first->resetStats();

    // updaters
    for (auto &updater_trigger_pair: m_updaters)
        updater_trigger_pair.first->resetStats();

    // computes
    map< string, std::shared_ptr<Compute> >::iterator compute;
    for (compute = m_computes.begin(); compute != m_computes.end(); ++compute)
        compute->second->resetStats();
    }

void System::generateStatusLine()
    {
    // a status line consists of
    // elapsed time
    // current timestep / end time step
    // time steps per second
    // ETA

    // elapsed time
    int64_t cur_time = m_clk.getTime();
    string t_elap = ClockSource::formatHMS(cur_time);

    // time steps per second
    Scalar TPS = Scalar(m_cur_tstep - m_last_status_tstep) / Scalar(cur_time - m_last_status_time) * Scalar(1e9);
    // put into the tps list
    size_t tps_size = m_tps_list.size();
    if ((unsigned int)tps_size < 10)
        {
        // add to list if list less than 10
        m_tps_list.push_back(TPS);
        }
    else
        {
        // remove the first item, add to the end
        m_tps_list.erase(m_tps_list.begin());
        m_tps_list.push_back(TPS);
        }
    tps_size = m_tps_list.size();
    std::vector<Scalar> l_tps_list = m_tps_list;
    std::sort(l_tps_list.begin(), l_tps_list.end());
    // not the "true" median calculation, but it doesn't really matter in this case
    Scalar median = l_tps_list[tps_size / 2];
    m_med_tps = median;
    m_cur_tps = TPS;

    // estimated time to go (base on current TPS)
    string ETA = ClockSource::formatHMS(int64_t((m_end_tstep - m_cur_tstep) / TPS * Scalar(1e9)));

    // write the line
    if (!m_quiet_run)
        {
        m_exec_conf->msg->notice(1) << "Time " << t_elap << " | Step " << m_cur_tstep << " / " << m_end_tstep << " | TPS " << TPS << " | ETA " << ETA << endl;
        }
    }

/*! \param tstep Time step for which to determine the flags

    The flags needed are determined by peeking to \a tstep and then using bitwise or to combine all of the flags from the
    analyzers and updaters that are to be executed on that step.
*/
PDataFlags System::determineFlags(unsigned int tstep)
    {
    PDataFlags flags(0);
    if (m_integrator)
        flags = m_integrator->getRequestedPDataFlags();

    for (auto &analyzer_trigger_pair: m_analyzers)
        {
        if ((*analyzer_trigger_pair.second)(tstep))
            flags |= analyzer_trigger_pair.first->getRequestedPDataFlags();
        }

    for (auto &updater_trigger_pair: m_updaters)
        {
        if ((*updater_trigger_pair.second)(tstep))
            flags |= updater_trigger_pair.first->getRequestedPDataFlags();
        }

    for (auto &tuner: m_tuners)
        {
        if ((*tuner->getTrigger())(tstep))
            flags |= tuner->getRequestedPDataFlags();
        }

    return flags;
    }

//! Create a custom exception
PyObject* createExceptionClass(py::module& m, const char* name, PyObject* baseTypeObj = PyExc_Exception)
    {
    // http://stackoverflow.com/questions/9620268/boost-python-custom-exception-class, modified by jproc for pybind11

    using std::string;

    string scopeName = py::cast<string>(m.attr("__name__"));
    string qualifiedName0 = scopeName + "." + name;
    char* qualifiedName1 = const_cast<char*>(qualifiedName0.c_str());

    PyObject* typeObj = PyErr_NewException(qualifiedName1, baseTypeObj, 0);
    if(!typeObj) throw py::error_already_set();
    m.attr(name) = py::reinterpret_borrow<py::object>(typeObj);
    return typeObj;
    }

void export_System(py::module& m)
    {
    walltimeLimitExceptionTypeObj = createExceptionClass(m,"WalltimeLimitReached");

    py::bind_vector<std::vector<std::pair<std::shared_ptr<Analyzer>,
                    std::shared_ptr<Trigger> > > >(m, "AnalyzerTriggerList");
    py::bind_vector<std::vector<std::pair<std::shared_ptr<Updater>,
                    std::shared_ptr<Trigger> > > >(m, "UpdaterTriggerList");
    py::bind_vector<std::vector<std::shared_ptr<Tuner> > > (m, "TunerList");

    py::class_< System, std::shared_ptr<System> > (m,"System")
    .def(py::init< std::shared_ptr<SystemDefinition>, unsigned int >())
    .def("addCompute", &System::addCompute)
    .def("overwriteCompute", &System::overwriteCompute)
    .def("removeCompute", &System::removeCompute)
    .def("getCompute", &System::getCompute)

    .def("setIntegrator", &System::setIntegrator)
    .def("getIntegrator", &System::getIntegrator)

    .def("registerLogger", &System::registerLogger)
    .def("setStatsPeriod", &System::setStatsPeriod)
    .def("setAutotunerParams", &System::setAutotunerParams)
    .def("enableProfiler", &System::enableProfiler)
    .def("enableQuietRun", &System::enableQuietRun)
    .def("run", &System::run)

    .def("getLastTPS", &System::getLastTPS)
    .def("getCurrentTimeStep", &System::getCurrentTimeStep)
    .def_property_readonly("analyzers", &System::getAnalyzers)
    .def_property_readonly("updaters", &System::getUpdaters)
    .def_property_readonly("tuners", &System::getTuners)
#ifdef ENABLE_MPI
    .def("setCommunicator", &System::setCommunicator)
    .def("getCommunicator", &System::getCommunicator)
#endif
    ;
    }
