// Copyright (c) 2009-2016 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

// Maintainer: jproc

// NVE Alchemo

#include "TwoStepNVTAlchemy.h"
#include "hoomd/VectorMath.h"

#ifdef ENABLE_MPI
#include "hoomd/Communicator.h"
#include "hoomd/HOOMDMPI.h"
#endif

using namespace std;
// namespace py = pybind11;

/*! \file TwoStepNVTAlchemy.cc
    \brief Contains code for the TwoStepNVTAlchemy class
*/

/*! \param sysdef SystemDefinition this method will act on. Must not be NULL.
    \param group The group of particles this integration method is to work on
    \param skip_restart Skip initialization of the restart information
*/
TwoStepNVTAlchemy::TwoStepNVTAlchemy(std::shared_ptr<SystemDefinition> sysdef,
                                     std::shared_ptr<Variant> T,
                                     const std::string& suffix)
    : AlchemostatTwoStep(sysdef), m_T(T)
    {
    m_exec_conf->msg->notice(5) << "Constructing TwoStepNVTAlchemy" << endl;

    // TODO: alchemy, add restart support, would require alpha matrix to be stored... where?
    // set initial state
    IntegratorVariables v = getIntegratorVariables();
    v.type = "nvt_alchemo";
    v.variable.resize(2);
    v.variable[0] = Scalar(0.0); // xi
    v.variable[1] = Scalar(0.0); // eta
    setIntegratorVariables(v);

    m_log_name = string("alchemostat_nvt_mtk") + suffix;
    }

TwoStepNVTAlchemy::~TwoStepNVTAlchemy()
    {
    m_exec_conf->msg->notice(5) << "Destroying TwoStepNVTAlchemy" << endl;
    }

/*! Returns a list of log quantities this compute calculates
 */
std::vector<std::string> TwoStepNVTAlchemy::getProvidedLogQuantities()
    {
    vector<string> result;
    result.push_back(m_log_name + string("_reservoir_energy"));
    result.push_back(m_log_name + string("_alchemical_kinetic_energy"));
    return result;
    }

/*! \param quantity Name of the log quantity to get
    \param timestep Current time step of the simulation
    \param my_quantity_flag passed as false, changed to true if quanity logged here
*/
Scalar TwoStepNVTAlchemy::getLogValue(const std::string& quantity,
                                      uint64_t timestep,
                                      bool& my_quantity_flag)
    {
    IntegratorVariables v = getIntegratorVariables();

    if (quantity == m_log_name + string("_reservoir_energy"))
        {
        my_quantity_flag = true;
        IntegratorVariables v = getIntegratorVariables();

        Scalar& xi = v.variable[0];
        Scalar& eta = v.variable[1];

        Scalar thermostat_energy
            = Scalar(0.5) * xi * xi * m_Q + eta * m_alchemicalParticles.size() * (*m_T)(timestep);

        return thermostat_energy;
        }
    else if (quantity == m_log_name + string("_alchemical_kinetic_energy"))
        {
        my_quantity_flag = true;
        return m_alchem_KE;
        }
    else
        {
        return Scalar(0);
        }
    }

void TwoStepNVTAlchemy::integrateStepOne(uint64_t timestep)
    {
    if (timestep != m_nextAlchemTimeStep)
        return;
    // profile this step
    if (m_prof)
        m_prof->push("NVTalchemo step 1");

    m_exec_conf->msg->notice(10) << "TwoStepNVTAlchemy: 1st Alchemcial Half Step" << endl;

    IntegratorVariables v = getIntegratorVariables();
    Scalar& xi = v.variable[0];
    m_alchem_KE = Scalar(0);

    // TODO: get any external derivatives, mapped?
    Scalar dUextdalpha = Scalar(0);

    for (auto& alpha : m_alchemicalParticles)
        {
        Scalar& q = alpha->value;
        Scalar& p = alpha->momentum;

        const Scalar& invM = alpha->mass.y;
        const Scalar& mu = alpha->mu;
        const Scalar netForce = alpha->getNetForce(timestep);

        // update position
        q += m_halfDeltaT * p * invM;
        // update momentum
        p += m_halfDeltaT * (netForce - mu - dUextdalpha);
        // rescale velocity
        p *= exp(-m_halfDeltaT * xi);
        m_alchem_KE += Scalar(0.5) * p * p * invM;
        }

    advanceThermostat(timestep);

    // done profiling
    if (m_prof)
        m_prof->pop();
    }

void TwoStepNVTAlchemy::integrateStepTwo(uint64_t timestep)
    {
    if (timestep != (m_nextAlchemTimeStep - 1))
        return;
    // profile this step
    if (m_prof)
        m_prof->push("NVTalchemo step 2");

    m_exec_conf->msg->notice(10) << "TwoStepNVTAlchemy: 2nd Alchemcial Half Step" << endl;

    IntegratorVariables v = getIntegratorVariables();
    Scalar& xi = v.variable[0];
    m_alchem_KE = Scalar(0);

    // TODO: get any external derivatives, mapped?
    Scalar dUextdalpha = Scalar(0);

    for (auto& alpha : m_alchemicalParticles)
        {
        Scalar& q = alpha->value;
        Scalar& p = alpha->momentum;

        const Scalar& invM = alpha->mass.y;
        const Scalar& mu = alpha->mu;
        const Scalar netForce = alpha->getNetForce(timestep);

        // rescale velocity
        p *= exp(-m_halfDeltaT * xi);
        // update momentum
        p += m_halfDeltaT * (netForce - mu - dUextdalpha);
        // update position
        q += m_halfDeltaT * p * invM;
        m_alchem_KE += Scalar(0.5) * p * p * invM;
        }

    // done profiling
    if (m_prof)
        m_prof->pop();
    }

void TwoStepNVTAlchemy::advanceThermostat(uint64_t timestep, bool broadcast)
    {
    IntegratorVariables v = getIntegratorVariables();
    Scalar& xi = v.variable[0];
    Scalar& eta = v.variable[1];

    // update the state variables Xi and eta
    Scalar half_delta_xi
        = m_halfDeltaT
          * ((Scalar(2) * m_alchem_KE) - (m_alchemicalParticles.size() * (*m_T)(timestep))) / m_Q;
    eta += (half_delta_xi + xi) * m_deltaT * m_nTimeFactor;
    xi += half_delta_xi + half_delta_xi;

    setIntegratorVariables(v);
    }
