// Copyright (c) 2009-2016 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

// Include the defined classes that are to be exported to python
#include "IntegratorHPMC.h"
#include "IntegratorHPMCMono.h"
#include "IntegratorHPMCMonoImplicit.h"
#include "IntegratorHPMCMonoImplicitNew.h"
#include "ComputeFreeVolume.h"

#include "ShapeConvexPolyhedron.h"
#include "AnalyzerSDF.h"
#include "ShapeUnion.h"

#include "ExternalField.h"
#include "ExternalFieldWall.h"
#include "ExternalFieldLattice.h"
#include "ExternalFieldComposite.h"
#include "ExternalCallback.h"

#include "UpdaterExternalFieldWall.h"
#include "UpdaterRemoveDrift.h"
#include "UpdaterMuVT.h"
#include "UpdaterMuVTImplicit.h"

#include "ShapeUtils.h"
#include "ShapeMoves.h"
#include "UpdaterShape.h"

#ifdef ENABLE_CUDA
#include "IntegratorHPMCMonoGPU.h"
#include "IntegratorHPMCMonoImplicitGPU.h"
#include "IntegratorHPMCMonoImplicitNewGPU.h"
#include "ComputeFreeVolumeGPU.h"
#endif

namespace py = pybind11;
using namespace hpmc;

using namespace hpmc::detail;

namespace hpmc
{

//! Export the base HPMCMono integrators
void export_convex_polyhedron(py::module& m)
    {
    export_IntegratorHPMCMono< ShapeConvexPolyhedron >(m, "IntegratorHPMCMonoConvexPolyhedron");
    export_IntegratorHPMCMonoImplicit< ShapeConvexPolyhedron >(m, "IntegratorHPMCMonoImplicitConvexPolyhedron");
    export_IntegratorHPMCMonoImplicitNew< ShapeConvexPolyhedron >(m, "IntegratorHPMCMonoImplicitNewConvexPolyhedron");
    export_ComputeFreeVolume< ShapeConvexPolyhedron >(m, "ComputeFreeVolumeConvexPolyhedron");
    export_AnalyzerSDF< ShapeConvexPolyhedron >(m, "AnalyzerSDFConvexPolyhedron");
    export_UpdaterMuVT< ShapeConvexPolyhedron >(m, "UpdaterMuVTConvexPolyhedron");
    export_UpdaterMuVTImplicit< ShapeConvexPolyhedron, IntegratorHPMCMonoImplicit<ShapeConvexPolyhedron> >(m, "UpdaterMuVTImplicitConvexPolyhedron");
    export_UpdaterMuVTImplicit< ShapeConvexPolyhedron, IntegratorHPMCMonoImplicitNew<ShapeConvexPolyhedron> >(m, "UpdaterMuVTImplicitNewConvexPolyhedron");

    export_ExternalFieldInterface<ShapeConvexPolyhedron >(m, "ExternalFieldConvexPolyhedron");
    export_LatticeField<ShapeConvexPolyhedron >(m, "ExternalFieldLatticeConvexPolyhedron");
    export_ExternalFieldComposite<ShapeConvexPolyhedron >(m, "ExternalFieldCompositeConvexPolyhedron");
    export_RemoveDriftUpdater<ShapeConvexPolyhedron >(m, "RemoveDriftUpdaterConvexPolyhedron");
    export_ExternalFieldWall<ShapeConvexPolyhedron >(m, "WallConvexPolyhedron");
    export_UpdaterExternalFieldWall<ShapeConvexPolyhedron >(m, "UpdaterExternalFieldWallConvexPolyhedron");
    export_ExternalCallback<ShapeConvexPolyhedron>(m, "ExternalCallbackConvexPolyhedron");

    export_massProperties< ShapeConvexPolyhedron >(m, "MassPropertiesConvexPolyhedron");
    export_ShapeMoveInterface< ShapeConvexPolyhedron >(m, "ShapeMoveConvexPolyhedron");
    export_ShapeLogBoltzmann< ShapeConvexPolyhedron >(m, "LogBoltzmannConvexPolyhedron");
    export_ScaleShearShapeMove< ShapeConvexPolyhedron >(m, "ScaleShearShapeMoveConvexPolyhedron");
    export_ShapeSpringLogBoltzmannFunction<ShapeConvexPolyhedron >(m, "ShapeSpringLogBoltzmannConvexPolyhedron");
    export_AlchemyLogBoltzmannFunction< ShapeConvexPolyhedron >(m, "AlchemyLogBoltzmannConvexPolyhedron");
    export_ConvexPolyhedronGeneralizedShapeMove< ShapeConvexPolyhedron >(m, "GeneralizedShapeMoveConvexPolyhedron");
    export_UpdaterShape< ShapeConvexPolyhedron >(m, "UpdaterShapeConvexPolyhedron");
    export_PythonShapeMove< ShapeConvexPolyhedron >(m, "PythonShapeMoveConvexPolyhedron");
    export_ConstantShapeMove< ShapeConvexPolyhedron >(m, "ConstantShapeMoveConvexPolyhedron");

    #ifdef ENABLE_CUDA

    export_IntegratorHPMCMonoGPU< ShapeConvexPolyhedron >(m, "IntegratorHPMCMonoGPUConvexPolyhedron");
    export_IntegratorHPMCMonoImplicitGPU< ShapeConvexPolyhedron >(m, "IntegratorHPMCMonoImplicitGPUConvexPolyhedron");
    export_IntegratorHPMCMonoImplicitNewGPU< ShapeConvexPolyhedron >(m, "IntegratorHPMCMonoImplicitNewGPUConvexPolyhedron");
    export_ComputeFreeVolumeGPU< ShapeConvexPolyhedron >(m, "ComputeFreeVolumeGPUConvexPolyhedron");

    #endif
    }

}
