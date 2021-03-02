// Copyright (c) 2009-2021 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.


// Maintainer: joaander


#include "ActiveForceCompute.h"
#include "hoomd/RandomNumbers.h"
#include "hoomd/RNGIdentifiers.h"

#include <vector>

using namespace std;
using namespace hoomd;
namespace py = pybind11;

/*! \file ActiveForceCompute.cc
    \brief Contains code for the ActiveForceCompute class
*/

/*! \param rotation_diff rotational diffusion constant for all particles.
    \param constraint specifies a constraint surface, to which particles are confined,
    such as update.constraint_ellipsoid. Have to replace it with manifolds when implemented
*/
ActiveForceCompute::ActiveForceCompute(std::shared_ptr<SystemDefinition> sysdef,
                                        std::shared_ptr<ParticleGroup> group,
                                        Scalar rotation_diff,
                                        Scalar3 P,
                                        Scalar rx,
                                        Scalar ry,
                                        Scalar rz)
        : ForceCompute(sysdef), m_group(group),
            m_rotationDiff(rotation_diff), m_P(P), m_rx(rx), m_ry(ry), m_rz(rz)
    {
    // allocate memory for the per-type active_force storage and initialize them to (1.0,0,0)
    GlobalVector<Scalar4> tmp_f_activeVec(m_pdata->getNTypes(), m_exec_conf);

    m_f_activeVec.swap(tmp_f_activeVec);
    TAG_ALLOCATION(m_f_activeVec);

    ArrayHandle<Scalar4> h_f_activeVec(m_f_activeVec, access_location::host, access_mode::overwrite);
    for (unsigned int i = 0; i < m_f_activeVec.size(); i++)
        h_f_activeVec.data[i] = make_scalar4(1.0,0.0,0.0,1.0);

    // allocate memory for the per-type active_force storage and initialize them to (0,0,0)
    GlobalVector<Scalar4> tmp_t_activeVec(m_pdata->getNTypes(), m_exec_conf);

    m_t_activeVec.swap(tmp_t_activeVec);
    TAG_ALLOCATION(m_t_activeVec);

    ArrayHandle<Scalar4> h_t_activeVec(m_t_activeVec, access_location::host, access_mode::overwrite);
    for (unsigned int i = 0; i < m_t_activeVec.size(); i++)
        h_t_activeVec.data[i] = make_scalar4(1.0,0.0,0.0,0.0);


    #if defined(ENABLE_HIP) && defined(__HIP_PLATFORM_NVCC__)
    if (m_exec_conf->isCUDAEnabled() && m_exec_conf->allConcurrentManagedAccess())
        {
        cudaMemAdvise(m_f_activeVec.get(), sizeof(Scalar4)*m_f_activeVec.getNumElements(), cudaMemAdviseSetReadMostly, 0);

        cudaMemAdvise(m_t_activeVec.get(), sizeof(Scalar4)*m_t_activeVec.getNumElements(), cudaMemAdviseSetReadMostly, 0);
        }
    #endif

    }

ActiveForceCompute::~ActiveForceCompute()
    {
    m_exec_conf->msg->notice(5) << "Destroying ActiveForceCompute" << endl;
    }



void ActiveForceCompute::setActiveForce(const std::string& type_name, pybind11::tuple v)
    {
    unsigned int typ = this->m_pdata->getTypeByName(type_name);

    if (pybind11::len(v) != 3)
        {
        throw invalid_argument("gamma_r values must be 3-tuples");
        }

    // check for user errors
    if (typ >= m_pdata->getNTypes())
        {
        throw invalid_argument("Type does not exist");
        }

    Scalar4 f_activeVec;
    f_activeVec.x = pybind11::cast<Scalar>(v[0]);
    f_activeVec.y = pybind11::cast<Scalar>(v[1]);
    f_activeVec.z = pybind11::cast<Scalar>(v[2]);

    Scalar f_activeMag = slow::sqrt(f_activeVec.x*f_activeVec.x+f_activeVec.y*f_activeVec.y+f_activeVec.z*f_activeVec.z);

    if(f_activeMag >0)
        {
        f_activeVec.x /= f_activeMag;
        f_activeVec.y /= f_activeMag;
        f_activeVec.z /= f_activeMag;
        f_activeVec.w = f_activeMag;
        }
    else
        {
        f_activeVec.x = 0;
        f_activeVec.y = 0;
        f_activeVec.z = 0;
        f_activeVec.w = 0;
        }

    ArrayHandle<Scalar4> h_f_activeVec(m_f_activeVec, access_location::host, access_mode::readwrite);
    h_f_activeVec.data[typ] = f_activeVec;

    }

pybind11::tuple ActiveForceCompute::getActiveForce(const std::string& type_name)
    {
    pybind11::list v;
    unsigned int typ = this->m_pdata->getTypeByName(type_name);

    ArrayHandle<Scalar4> h_f_activeVec(m_f_activeVec, access_location::host, access_mode::read);

    Scalar4 f_activeVec = h_f_activeVec.data[typ];
    v.append(f_activeVec.w*f_activeVec.x);
    v.append(f_activeVec.w*f_activeVec.y);
    v.append(f_activeVec.w*f_activeVec.z);
    return pybind11::tuple(v);
    }

void ActiveForceCompute::setActiveTorque(const std::string& type_name, pybind11::tuple v)
    {
    unsigned int typ = this->m_pdata->getTypeByName(type_name);

    if (pybind11::len(v) != 3)
        {
        throw invalid_argument("gamma_r values must be 3-tuples");
        }

    // check for user errors
    if (typ >= m_pdata->getNTypes())
        {
        throw invalid_argument("Type does not exist");
        }

    Scalar4 t_activeVec;
    t_activeVec.x = pybind11::cast<Scalar>(v[0]);
    t_activeVec.y = pybind11::cast<Scalar>(v[1]);
    t_activeVec.z = pybind11::cast<Scalar>(v[2]);

    Scalar t_activeMag = slow::sqrt(t_activeVec.x*t_activeVec.x+t_activeVec.y*t_activeVec.y+t_activeVec.z*t_activeVec.z);

    if(t_activeMag > 0)
        {
        t_activeVec.x /= t_activeMag;
        t_activeVec.y /= t_activeMag;
        t_activeVec.z /= t_activeMag;
        t_activeVec.w = t_activeMag;
        }
    else
       {
        t_activeVec.x = 0;
        t_activeVec.y = 0;
        t_activeVec.z = 0;
        t_activeVec.w = 0;
        }

    ArrayHandle<Scalar4> h_t_activeVec(m_t_activeVec, access_location::host, access_mode::readwrite);
    h_t_activeVec.data[typ] = t_activeVec;

    }

pybind11::tuple ActiveForceCompute::getActiveTorque(const std::string& type_name)
    {
    pybind11::list v;
    unsigned int typ = this->m_pdata->getTypeByName(type_name);

    ArrayHandle<Scalar4> h_t_activeVec(m_t_activeVec, access_location::host, access_mode::read);
    Scalar4 t_activeVec = h_t_activeVec.data[typ];
    v.append(t_activeVec.w*t_activeVec.x);
    v.append(t_activeVec.w*t_activeVec.y);
    v.append(t_activeVec.w*t_activeVec.z);
    return pybind11::tuple(v);
    }

/*! This function sets appropriate active forces on all active particles.
*/
void ActiveForceCompute::setForces()
    {

    //  array handles
    ArrayHandle<Scalar4> h_f_actVec(m_f_activeVec, access_location::host, access_mode::read);
    ArrayHandle<Scalar4> h_t_actVec(m_t_activeVec, access_location::host, access_mode::read);
    ArrayHandle<Scalar4> h_force(m_force,access_location::host,access_mode::overwrite);
    ArrayHandle<Scalar4> h_torque(m_torque,access_location::host,access_mode::overwrite);
    ArrayHandle<Scalar4> h_pos(m_pdata->getPositions(), access_location::host, access_mode::read);
    ArrayHandle<Scalar4> h_orientation(m_pdata->getOrientationArray(), access_location::host, access_mode::read);

    // sanity check
    assert(h_f_actVec.data != NULL);
    assert(h_t_actVec.data != NULL);
    assert(h_orientation.data != NULL);
    assert(h_pos.data != NULL);

    // zero forces so we don't leave any forces set for indices that are no longer part of our group
    memset(h_force.data, 0, sizeof(Scalar4) * m_force.getNumElements());
    memset(h_torque.data, 0, sizeof(Scalar4) * m_force.getNumElements());

    for (unsigned int i = 0; i < m_group->getNumMembers(); i++)
        {
        unsigned int idx = m_group->getMemberIndex(i);
        unsigned int type = __scalar_as_int(h_pos.data[idx].w);

        vec3<Scalar> f(h_f_actVec.data[type].w*h_f_actVec.data[type].x, h_f_actVec.data[type].w*h_f_actVec.data[type].y, h_f_actVec.data[type].w*h_f_actVec.data[type].z);
        quat<Scalar> quati(h_orientation.data[idx]);
        vec3<Scalar> fi = rotate(quati, f);
        h_force.data[idx] = vec_to_scalar4(fi, 0);

        vec3<Scalar> t(h_t_actVec.data[type].w*h_t_actVec.data[type].x, h_t_actVec.data[type].w*h_t_actVec.data[type].y, h_t_actVec.data[type].w*h_t_actVec.data[type].z);
        vec3<Scalar> ti = rotate(quati, t);
        h_torque.data[idx] = vec_to_scalar4(ti, 0);
        }
    }


/*! This function applies rotational diffusion to the orientations of all active particles. The orientation of any torque vector
 * relative to the force vector is preserved
    \param timestep Current timestep
*/
void ActiveForceCompute::rotationalDiffusion(uint64_t timestep)
    {
    //  array handles
    ArrayHandle<Scalar4> h_f_actVec(m_f_activeVec, access_location::host, access_mode::read);
    ArrayHandle<Scalar4> h_pos(m_pdata -> getPositions(), access_location::host, access_mode::read);
    ArrayHandle<Scalar4> h_orientation(m_pdata->getOrientationArray(), access_location::host, access_mode::readwrite);
    ArrayHandle<unsigned int> h_tag(m_pdata->getTags(), access_location::host, access_mode::read);

    assert(h_f_actVec.data != NULL);
    assert(h_pos.data != NULL);
    assert(h_orientation.data != NULL);
    assert(h_tag.data != NULL);

    for (unsigned int i = 0; i < m_group->getNumMembers(); i++)
        {
        unsigned int idx = m_group->getMemberIndex(i);
        unsigned int type = __scalar_as_int(h_pos.data[idx].w);
        unsigned int ptag = h_tag.data[idx];
        hoomd::RandomGenerator rng(hoomd::Seed(hoomd::RNGIdentifier::ActiveForceCompute,
                                               timestep,
                                               m_sysdef->getSeed()),
                                   hoomd::Counter(ptag));

        quat<Scalar> quati(h_orientation.data[idx]);


        if (m_sysdef->getNDimensions() == 2) // 2D
            {
            Scalar delta_theta; // rotational diffusion angle
            delta_theta = hoomd::NormalDistribution<Scalar>(m_rotationConst)(rng);
            Scalar theta = delta_theta/2.0; // half angle to calculate the quaternion which represents the rotation
            vec3<Scalar> b(0,0,slow::sin(theta));

            quat<Scalar> rot_quat(slow::cos(theta),b);// rotational diffusion quaternion

            quati = rot_quat*quati; //rotational diffusion quaternion applied to orientation
            h_orientation.data[idx].x = quati.s;
            h_orientation.data[idx].y = quati.v.x;
            h_orientation.data[idx].z = quati.v.y;
            h_orientation.data[idx].w = quati.v.z;
            // In 2D, the only meaningful torque vector is out of plane and should not change
            }
        else // 3D: Following Stenhammar, Soft Matter, 2014
            {
            if (m_rx == 0) // if no constraint
                {
                hoomd::SpherePointGenerator<Scalar> unit_vec;
                vec3<Scalar> rand_vec;
                unit_vec(rng, rand_vec);

                vec3<Scalar> f(h_f_actVec.data[type].x, h_f_actVec.data[type].y, h_f_actVec.data[type].z);
                vec3<Scalar> fi = rotate(quati, f); //rotate active force vector from local to global frame

                vec3<Scalar> aux_vec; // rotation axis
                aux_vec.x = fi.y * rand_vec.z - fi.z * rand_vec.y;
                aux_vec.y = fi.z * rand_vec.x - fi.x * rand_vec.z;
                aux_vec.z = fi.x * rand_vec.y - fi.y * rand_vec.x;
                Scalar aux_vec_mag = 1.0/slow::sqrt(aux_vec.x*aux_vec.x + aux_vec.y*aux_vec.y + aux_vec.z*aux_vec.z);
                aux_vec.x *= aux_vec_mag;
                aux_vec.y *= aux_vec_mag;
                aux_vec.z *= aux_vec_mag;

                Scalar delta_theta = hoomd::NormalDistribution<Scalar>(m_rotationConst)(rng);
                Scalar theta = delta_theta/2.0; // half angle to calculate the quaternion which represents the rotation
                quat<Scalar> rot_quat(slow::cos(theta),slow::sin(theta)*aux_vec); // rotational diffusion quaternion

                quati = rot_quat*quati; //rotational diffusion quaternion applied to orientation
                h_orientation.data[idx] = quat_to_scalar4(quati);
                }
            else // if constraint exists
                {
                EvaluatorConstraintEllipsoid Ellipsoid(m_P, m_rx, m_ry, m_rz);

                Scalar3 current_pos = make_scalar3(h_pos.data[idx].x, h_pos.data[idx].y, h_pos.data[idx].z);
                Scalar3 norm_scalar3 = Ellipsoid.evalNormal(current_pos); // the normal vector to which the particles are confined.

                vec3<Scalar> norm;
                norm = vec3<Scalar> (norm_scalar3);

                Scalar delta_theta = hoomd::NormalDistribution<Scalar>(m_rotationConst)(rng);
                Scalar theta = delta_theta/2.0; // half angle to calculate the quaternion which represents the rotation
                quat<Scalar> rot_quat(slow::cos(theta),slow::sin(theta)*norm);//rotational diffusion quaternion

                quati = rot_quat*quati; //rotational diffusion quaternion applied to orientation
                h_orientation.data[idx] = quat_to_scalar4(quati);
                }
            }
        }
    }

/*! This function sets an ellipsoid surface constraint for all active particles. Torque is not considered here
*/
void ActiveForceCompute::setConstraint()
    {
    EvaluatorConstraintEllipsoid Ellipsoid(m_P, m_rx, m_ry, m_rz);

    //  array handles
    ArrayHandle<Scalar4> h_f_actVec(m_f_activeVec, access_location::host, access_mode::read);
    ArrayHandle<Scalar4> h_orientation(m_pdata->getOrientationArray(), access_location::host, access_mode::readwrite);
    ArrayHandle<Scalar4> h_pos(m_pdata -> getPositions(), access_location::host, access_mode::read);

    assert(h_f_actVec.data != NULL);
    assert(h_pos.data != NULL);
    assert(h_orientation.data != NULL);

    for (unsigned int i = 0; i < m_group->getNumMembers(); i++)
        {
        unsigned int idx = m_group->getMemberIndex(i);
        unsigned int type = __scalar_as_int(h_pos.data[idx].w);

        Scalar3 current_pos = make_scalar3(h_pos.data[idx].x, h_pos.data[idx].y, h_pos.data[idx].z);

        Scalar3 norm_scalar3 = Ellipsoid.evalNormal(current_pos); // the normal vector to which the particles are confined.
        vec3<Scalar> norm = vec3<Scalar>(norm_scalar3);

        Scalar3 f = make_scalar3(h_f_actVec.data[type].x, h_f_actVec.data[type].y, h_f_actVec.data[type].z);
        quat<Scalar> quati(h_orientation.data[idx]);
        vec3<Scalar> fi = rotate(quati, vec3<Scalar>(f));//rotate active force vector from local to global frame


        Scalar dot_prod = fi.x * norm.x + fi.y * norm.y + fi.z * norm.z;

        Scalar dot_perp_prod = slow::sqrt(1-dot_prod*dot_prod);

        Scalar phi_half = slow::atan(dot_prod/dot_perp_prod)/2.0;


        fi.x -= norm.x * dot_prod;
        fi.y -= norm.y * dot_prod;
        fi.z -= norm.z * dot_prod;

        Scalar new_norm = 1.0/slow::sqrt(fi.x*fi.x + fi.y*fi.y + fi.z*fi.z);

        fi.x *= new_norm;
        fi.y *= new_norm;
        fi.z *= new_norm;

        vec3<Scalar> rot_vec = cross(norm,fi);
        rot_vec.x *= slow::sin(phi_half);
        rot_vec.y *= slow::sin(phi_half);
        rot_vec.z *= slow::sin(phi_half);

        quat<Scalar> rot_quat(cos(phi_half),rot_vec);

        quati = rot_quat*quati;

        h_orientation.data[idx] = quat_to_scalar4(quati);
        }
    }

/*! This function applies constraints, rotational diffusion, and sets forces for all active particles
    \param timestep Current timestep
*/
void ActiveForceCompute::computeForces(uint64_t timestep)
    {
    if (m_prof) m_prof->push(m_exec_conf, "ActiveForceCompute");

    if (last_computed != timestep)
        {
        m_rotationConst = slow::sqrt(2.0 * m_rotationDiff * m_deltaT);

        last_computed = timestep;

        if (m_rx != 0)
            {
            setConstraint(); // apply surface constraints to active particles active force vectors
            }
        if (m_rotationDiff != 0)
            {
            rotationalDiffusion(timestep); // apply rotational diffusion to active particles
            }
        setForces(); // set forces for particles
        }

    #ifdef ENABLE_HIP
    if(m_exec_conf->isCUDAErrorCheckingEnabled())
        CHECK_CUDA_ERROR();
    #endif

    if (m_prof)
        m_prof->pop(m_exec_conf);
    }


void export_ActiveForceCompute(py::module& m)
    {
    py::class_< ActiveForceCompute, ForceCompute, std::shared_ptr<ActiveForceCompute> >(m, "ActiveForceCompute")
    .def(py::init< std::shared_ptr<SystemDefinition>, std::shared_ptr<ParticleGroup>, Scalar,
                    Scalar3, Scalar, Scalar, Scalar >())
    .def_property("rotation_diff", &ActiveForceCompute::getRdiff, &ActiveForceCompute::setRdiff)
    .def("setActiveForce", &ActiveForceCompute::setActiveForce)
    .def("getActiveForce", &ActiveForceCompute::getActiveForce)
    .def("setActiveTorque", &ActiveForceCompute::setActiveTorque)
    .def("getActiveTorque", &ActiveForceCompute::getActiveTorque)
    ;
    }
