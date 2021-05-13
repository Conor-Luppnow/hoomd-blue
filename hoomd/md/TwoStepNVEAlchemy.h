// Copyright (c) 2009-2019 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

// Maintainer: jproc


#ifndef __TWO_STEP_NVE_ALCHEMY_H__
#define __TWO_STEP_NVE_ALCHEMY_H__


#include "AlchemostatTwoStep.h"

/*! \file TwoStepNVEAlchemy.h
    \brief Declares the TwoStepNVEAlchemy class
*/

#ifdef NVCC
#error This header cannot be compiled by nvcc
#endif

#include <hoomd/extern/nano-signal-slot/nano_signal_slot.hpp>
#include <pybind11/pybind11.h>

//! Integrates part of the system forward in two steps in the NVE ensemble
/*! Implements velocity-verlet NVE integration through the IntegrationMethodTwoStep interface

    \ingroup updaters
*/
class TwoStepNVEAlchemy : public AlchemostatTwoStep
    {
    public:
    //! Constructs the integration method and associates it with the system
    TwoStepNVEAlchemy(std::shared_ptr<SystemDefinition> sysdef);
    virtual ~TwoStepNVEAlchemy();

    //! Performs the first step of the integration
    void integrateStepOne(uint64_t timestep) override;

    //! Performs the second step of the integration
    void integrateStepTwo(uint64_t timestep) override;

    //! Alchemical Stuff follows

    protected:
    // bool m_pre;
    std::string m_log_name; //!< Name of the reservior quantity that we log
    };

//! Exports the TwoStepNVEAlchemy class to python
void export_TwoStepNVEAlchemy(pybind11::module& m);

#endif // #ifndef __TWO_STEP_NVE_ALCHEMY_H__
