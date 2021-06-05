// Copyright (c) 2009-2021 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

// Maintainer: joaander

#include "hoomd/ForceConstraint.h"
#include "hoomd/ParticleGroup.h"

#include <memory>

/*! \file ConstraintSphere.h
    \brief Declares a class for computing sphere constraint forces
*/

#ifdef __HIPCC__
#error This header cannot be compiled by nvcc
#endif

#include <pybind11/pybind11.h>

#ifndef __CONSTRAINT_SPHERE_H__
#define __CONSTRAINT_SPHERE_H__

//! Applys a constraint force to keep a group of particles on a sphere
/*! \ingroup computes
 */
class PYBIND11_EXPORT ConstraintSphere : public ForceConstraint
    {
    public:
    //! Constructs the compute
    ConstraintSphere(std::shared_ptr<SystemDefinition> sysdef,
                     std::shared_ptr<ParticleGroup> group,
                     Scalar3 P,
                     Scalar r);

    //! Destructor
    virtual ~ConstraintSphere();

    //! Set the force to a new value
    void setSphere(Scalar3 P, Scalar r);

    /** ConstraintSphere removes 1 degree of freedom per particle in the group

        @param query The group over which to compute the removed degrees of freedom
    */
    virtual Scalar getNDOFRemoved(std::shared_ptr<ParticleGroup> query);

    protected:
    std::shared_ptr<ParticleGroup>
        m_group; //!< Group of particles on which this constraint is applied
    Scalar3 m_P; //!< Position of the sphere
    Scalar m_r;  //!< Radius of the sphere

    //! Actually compute the forces
    virtual void computeForces(uint64_t timestep);

    private:
    //! Validate that the sphere is in the box and all particles are very near the constraint
    void validate();
    };

//! Exports the ConstraintSphere class to python
void export_ConstraintSphere(pybind11::module& m);

#endif
