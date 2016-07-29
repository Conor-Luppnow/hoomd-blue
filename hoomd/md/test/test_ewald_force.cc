// Copyright (c) 2009-2016 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.


// this include is necessary to get MPI included before anything else to support intel MPI
#include "hoomd/ExecutionConfiguration.h"

#include <iostream>
#include <fstream>

#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <memory>

#include "hoomd/md/AllPairPotentials.h"

#include "hoomd/md/NeighborListTree.h"
#include "hoomd/Initializers.h"

#include <math.h>

using namespace std;


/*! \file ewald_force_test.cc
    \brief Implements unit tests for PotentialPairEwald and descendants
    \ingroup unit_tests
*/

#include "hoomd/test/upp11_config.h"

HOOMD_UP_MAIN();




//! Typedef'd PotentialPairEwald factory
typedef boost::function<std::shared_ptr<PotentialPairEwald> (std::shared_ptr<SystemDefinition> sysdef,
                                                         std::shared_ptr<NeighborList> nlist)> ewaldforce_creator;

//! Test the ability of the ewald force compute to actually calucate forces
void ewald_force_particle_test(ewaldforce_creator ewald_creator, std::shared_ptr<ExecutionConfiguration> exec_conf)
    {
    // this 3-particle test subtly checks several conditions
    // the particles are arranged on the x axis,  1   2   3
    // such that 2 is inside the cuttoff radius of 1 and 3, but 1 and 3 are outside the cuttoff
    // of course, the buffer will be set on the neighborlist so that 3 is included in it
    // thus, this case tests the ability of the force summer to sum more than one force on
    // a particle and ignore a particle outside the radius

    // periodic boundary conditions will be handeled in another test
    std::shared_ptr<SystemDefinition> sysdef_3(new SystemDefinition(3, BoxDim(1000.0), 1, 0, 0, 0, 0, exec_conf));
    std::shared_ptr<ParticleData> pdata_3 = sysdef_3->getParticleData();
    pdata_3->setFlags(~PDataFlags(0));

    pdata_3->setPosition(0,make_scalar3(0.0,0.0,0.0));
    pdata_3->setPosition(1,make_scalar3(1.0,0.0,0.0));
    pdata_3->setPosition(2,make_scalar3(2.0,0.0,0.0));
    pdata_3->setCharge(0,1.0);
    pdata_3->setCharge(1,1.0);
    pdata_3->setCharge(2,-1.0);

    std::shared_ptr<NeighborListTree> nlist_3(new NeighborListTree(sysdef_3, Scalar(1.3), Scalar(3.0)));
    std::shared_ptr<PotentialPairEwald> fc_3 = ewald_creator(sysdef_3, nlist_3);
    fc_3->setRcut(0, 0, Scalar(1.3));

    // first test: choose a basic set of values
    Scalar kappa = Scalar(0.5);
    fc_3->setParams(0,0,kappa);

    // compute the forces
    fc_3->compute(0);

    {
    ArrayHandle<Scalar4> h_force(fc_3->getForceArray(), access_location::host, access_mode::read);
    ArrayHandle<Scalar> h_virial(fc_3->getVirialArray(), access_location::host, access_mode::read);
    unsigned int pitch = fc_3->getVirialArray().getPitch();

    MY_CHECK_CLOSE(h_force.data[0].x, -0.9188914117, tol);
    MY_CHECK_SMALL(h_force.data[0].y, tol_small);
    MY_CHECK_SMALL(h_force.data[0].z, tol_small);
    MY_CHECK_CLOSE(h_force.data[0].w, 0.4795001222/2.0, tol);
    MY_CHECK_CLOSE(h_virial.data[0*pitch]
                        +h_virial.data[3*pitch]
                        +h_virial.data[5*pitch], 0.9188914117/2.0, tol);

    MY_CHECK_CLOSE(h_force.data[1].x, 0.9188914117*2.0, tol);
    MY_CHECK_SMALL(h_force.data[1].y, tol_small);
    MY_CHECK_SMALL(h_force.data[1].z, tol_small);
    MY_CHECK_SMALL(h_force.data[1].w, tol_small);
    MY_CHECK_SMALL(h_virial.data[0*pitch+1]
                        +h_virial.data[3*pitch+1]
                        +h_virial.data[5*pitch+1], tol_small);

    MY_CHECK_CLOSE(h_force.data[2].x, -0.9188914117, tol);
    MY_CHECK_SMALL(h_force.data[2].y, tol_small);
    MY_CHECK_SMALL(h_force.data[2].z, tol_small);
    MY_CHECK_CLOSE(h_force.data[2].w, -0.4795001222/2.0, tol);
    MY_CHECK_CLOSE(h_virial.data[0*pitch+2]
                        +h_virial.data[3*pitch+2]
                        +h_virial.data[5*pitch+2], -0.9188914117/2.0, tol);
    }

    // swap the order of particles 0 ans 2 in memory to check that the force compute handles this properly
    {
    ArrayHandle<Scalar4> h_pos(pdata_3->getPositions(), access_location::host, access_mode::readwrite);
    ArrayHandle<unsigned int> h_tag(pdata_3->getTags(), access_location::host, access_mode::readwrite);
    ArrayHandle<unsigned int> h_rtag(pdata_3->getRTags(), access_location::host, access_mode::readwrite);

    h_pos.data[2].x = h_pos.data[2].y = h_pos.data[2]. z = 0.0;
    h_pos.data[0].x = 2.0; h_pos.data[0].y = h_pos.data[0].z = 0.0;

    h_tag.data[0] = 2;
    h_tag.data[2] = 0;
    h_rtag.data[0] = 2;
    h_rtag.data[2] = 0;
    }

    // notify the particle data that we changed the order
    pdata_3->notifyParticleSort();

    // recompute the forces at the same timestep, they should be updated
    fc_3->compute(1);

    {
    ArrayHandle<Scalar4> h_force(fc_3->getForceArray(), access_location::host, access_mode::read);
    ArrayHandle<Scalar> h_virial(fc_3->getVirialArray(), access_location::host, access_mode::read);

    MY_CHECK_CLOSE(h_force.data[0].x, 0.9188914117, tol);
    MY_CHECK_CLOSE(h_force.data[2].x, 0.9188914117, tol);
    }
    }

//! Unit test a comparison between 2 PotentialPairEwald's on a "real" system
void ewald_force_comparison_test(ewaldforce_creator ewald_creator1,
                                  ewaldforce_creator ewald_creator2,
                                  std::shared_ptr<ExecutionConfiguration> exec_conf)
    {
    const unsigned int N = 5000;

    // create a random particle system to sum forces on
    RandomInitializer rand_init(N, Scalar(0.1), Scalar(1.0), "A");
    std::shared_ptr< SnapshotSystemData<Scalar> > snap;
    snap = rand_init.getSnapshot();
    std::shared_ptr<SystemDefinition> sysdef(new SystemDefinition(snap, exec_conf));
    std::shared_ptr<ParticleData> pdata = sysdef->getParticleData();
    pdata->setFlags(~PDataFlags(0));

    std::shared_ptr<NeighborListTree> nlist(new NeighborListTree(sysdef, Scalar(3.0), Scalar(0.8)));

    std::shared_ptr<PotentialPairEwald> fc1 = ewald_creator1(sysdef, nlist);
    std::shared_ptr<PotentialPairEwald> fc2 = ewald_creator2(sysdef, nlist);
    fc1->setRcut(0, 0, Scalar(3.0));
    fc2->setRcut(0, 0, Scalar(3.0));

    // setup some values for epsilon and sigma
    Scalar kappa = Scalar(0.5);

    // specify the force parameters
    fc1->setParams(0,0,kappa);
    fc2->setParams(0,0,kappa);

    // compute the forces
    fc1->compute(0);
    fc2->compute(0);

    // verify that the forces are identical (within roundoff errors)
    ArrayHandle<Scalar4> h_force1(fc1->getForceArray(), access_location::host, access_mode::read);
    ArrayHandle<Scalar> h_virial1(fc1->getVirialArray(), access_location::host, access_mode::read);
    ArrayHandle<Scalar4> h_force2(fc2->getForceArray(), access_location::host, access_mode::read);
    ArrayHandle<Scalar> h_virial2(fc2->getVirialArray(), access_location::host, access_mode::read);

    unsigned int pitch = fc1->getVirialArray().getPitch();

    // compare average deviation between the two computes
    double deltaf2 = 0.0;
    double deltape2 = 0.0;
    double deltav2[6];
    for (unsigned int i = 0; i < 6; i++)
        deltav2[i] = 0.0;

    for (unsigned int i = 0; i < N; i++)
        {
        deltaf2 += double(h_force1.data[i].x - h_force2.data[i].x) * double(h_force1.data[i].x - h_force2.data[i].x);
        deltaf2 += double(h_force1.data[i].y - h_force2.data[i].y) * double(h_force1.data[i].y - h_force2.data[i].y);
        deltaf2 += double(h_force1.data[i].z - h_force2.data[i].z) * double(h_force1.data[i].z - h_force2.data[i].z);
        deltape2 += double(h_force1.data[i].w - h_force2.data[i].w) * double(h_force1.data[i].w - h_force2.data[i].w);
        for (unsigned int j = 0; j < 6; j++)
            deltav2[j] += double(h_virial1.data[j*pitch+i] - h_virial2.data[j*pitch+i]) * double(h_virial1.data[j*pitch+i] - h_virial2.data[j*pitch+i]);

        // also check that each individual calculation is somewhat close
        CHECK_CLOSE(h_force1.data[i].x, h_force2.data[i].x, loose_tol);
        CHECK_CLOSE(h_force1.data[i].y, h_force2.data[i].y, loose_tol);
        CHECK_CLOSE(h_force1.data[i].z, h_force2.data[i].z, loose_tol);
        CHECK_CLOSE(h_force1.data[i].w, h_force2.data[i].w, loose_tol);
        CHECK_CLOSE(h_virial1.data[0*pitch+i], h_virial2.data[0*pitch+i], loose_tol);
        CHECK_CLOSE(h_virial1.data[1*pitch+i], h_virial2.data[1*pitch+i], loose_tol);
        CHECK_CLOSE(h_virial1.data[2*pitch+i], h_virial2.data[2*pitch+i], loose_tol);
        CHECK_CLOSE(h_virial1.data[3*pitch+i], h_virial2.data[3*pitch+i], loose_tol);
        CHECK_CLOSE(h_virial1.data[4*pitch+i], h_virial2.data[4*pitch+i], loose_tol);
        CHECK_CLOSE(h_virial1.data[5*pitch+i], h_virial2.data[5*pitch+i], loose_tol);
        }
    deltaf2 /= double(pdata->getN());
    deltape2 /= double(pdata->getN());
    for (unsigned int i = 0; i < 6; i++)
        deltav2[i] /= double(pdata->getN());

    CHECK_SMALL(deltaf2, double(tol_small));
    CHECK_SMALL(deltape2, double(tol_small));
    CHECK_SMALL(deltav2[0], double(tol_small));
    CHECK_SMALL(deltav2[1], double(tol_small));
    CHECK_SMALL(deltav2[2], double(tol_small));
    CHECK_SMALL(deltav2[3], double(tol_small));
    CHECK_SMALL(deltav2[4], double(tol_small));
    CHECK_SMALL(deltav2[5], double(tol_small));
    }

//! PotentialPairEwald creator for unit tests
std::shared_ptr<PotentialPairEwald> base_class_ewald_creator(std::shared_ptr<SystemDefinition> sysdef,
                                                          std::shared_ptr<NeighborList> nlist)
    {
    return std::shared_ptr<PotentialPairEwald>(new PotentialPairEwald(sysdef, nlist));
    }

#ifdef ENABLE_CUDA
//! PotentialPairEwaldGPU creator for unit tests
std::shared_ptr<PotentialPairEwaldGPU> gpu_ewald_creator(std::shared_ptr<SystemDefinition> sysdef,
                                                      std::shared_ptr<NeighborList> nlist)
    {
    nlist->setStorageMode(NeighborList::full);
    std::shared_ptr<PotentialPairEwaldGPU> ewald(new PotentialPairEwaldGPU(sysdef, nlist));
    return ewald;
    }
#endif

//! test case for particle test on CPU
UP_TEST( EwaldForce_particle )
    {
    ewaldforce_creator ewald_creator_base = bind(base_class_ewald_creator, _1, _2);
    ewald_force_particle_test(ewald_creator_base, std::shared_ptr<ExecutionConfiguration>(new ExecutionConfiguration(ExecutionConfiguration::CPU)));
    }

# ifdef ENABLE_CUDA
//! test case for particle test on GPU
UP_TEST( EwaldForceGPU_particle )
    {
    ewaldforce_creator ewald_creator_gpu = bind(gpu_ewald_creator, _1, _2);
    ewald_force_particle_test(ewald_creator_gpu, std::shared_ptr<ExecutionConfiguration>(new ExecutionConfiguration(ExecutionConfiguration::GPU)));
    }

//! test case for comparing GPU output to base class output
UP_TEST( EwaldForceGPU_compare )
    {
    ewaldforce_creator ewald_creator_gpu = bind(gpu_ewald_creator, _1, _2);
    ewaldforce_creator ewald_creator_base = bind(base_class_ewald_creator, _1, _2);
    ewald_force_comparison_test(ewald_creator_base,
                                 ewald_creator_gpu,
                                 std::shared_ptr<ExecutionConfiguration>(new ExecutionConfiguration(ExecutionConfiguration::GPU)));
    }

#endif
