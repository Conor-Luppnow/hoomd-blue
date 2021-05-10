// Copyright (c) 2009-2016 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

// Include the defined classes that are to be exported to python

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "ShapeSphere.h"
#include "ShapeConvexPolygon.h"
#include "ShapeSpheropolygon.h"
#include "ShapePolyhedron.h"
#include "ShapeConvexPolyhedron.h"
#include "ShapeSpheropolyhedron.h"
#include "ShapeSimplePolygon.h"
#include "ShapeEllipsoid.h"
#include "ShapeSphinx.h"
#include "ShapeUnion.h"

#include "UpdaterShape.h"
#include "ShapeMoves.h"

namespace hpmc
{

template<class Shape>
using shape_move_function_python_class = pybind11::class_<ShapeMoveBase<Shape>, std::shared_ptr< ShapeMoveBase<Shape> > >;

template<class Shape>
void export_ShapeMoveInterface(pybind11::module& m, const std::string& name)
    {
    shape_move_function_python_class<Shape>(m, (name + "Interface").c_str())
    .def(pybind11::init< unsigned int >())
    ;
    }

template<class Shape>
void export_ElasticShapeMove(pybind11::module& m, const std::string& name)
    {
    pybind11::class_< ElasticShapeMove<Shape>, std::shared_ptr< ElasticShapeMove<Shape> >, ShapeMoveBase<Shape> >(m, name.c_str())
    .def(pybind11::init<unsigned int,
                        std::vector<Scalar>,
                        Scalar >())
    .def_property("stepsize", &ElasticShapeMove<Shape>::getStepsize, &ElasticShapeMove<Shape>::setStepsize)
    .def_property("param_ratio", &ElasticShapeMove<Shape>::getParamRatio, &ElasticShapeMove<Shape>::setParamRatio)
    ;
    }

template< typename Shape >
void export_ShapeLogBoltzmann(pybind11::module& m, const std::string& name)
    {
    pybind11::class_< ShapeLogBoltzmannFunction<Shape>, std::shared_ptr< ShapeLogBoltzmannFunction<Shape> > >
    (m, (name + "Interface").c_str() )
    .def(pybind11::init< >())
    ;
    }

template<class Shape>
void export_ShapeSpringLogBoltzmannFunction(pybind11::module& m, const std::string& name)
    {
    pybind11::class_< ShapeSpring<Shape>, std::shared_ptr< ShapeSpring<Shape> >, ShapeSpringBase<Shape> >(m, name.c_str())
    .def(pybind11::init<std::shared_ptr<Variant>,
                        typename Shape::param_type,
                        std::shared_ptr<ElasticShapeMove<Shape> > >())
    .def_property("stiffness", &ShapeSpring<Shape>::getStiffness, &ShapeSpring<Shape>::setStiffness)
    ;
    }

template<class Shape>
void export_AlchemyLogBoltzmannFunction(pybind11::module& m, const std::string& name)
    {
    pybind11::class_< AlchemyLogBoltzmannFunction<Shape>, std::shared_ptr< AlchemyLogBoltzmannFunction<Shape> >, ShapeLogBoltzmannFunction<Shape> >
    (m, name.c_str())
    .def(pybind11::init< >())
    ;
    }

template<class Shape>
void export_ConvexPolyhedronGeneralizedShapeMove(pybind11::module& m, const std::string& name)
    {
    pybind11::class_< ConvexPolyhedronVertexShapeMove, std::shared_ptr< ConvexPolyhedronVertexShapeMove >, ShapeMoveBase<Shape> >(m, name.c_str())
    .def(pybind11::init<unsigned int,
                        std::vector<Scalar>,
                        Scalar,
                        Scalar >())
    .def_property("volume", &ConvexPolyhedronVertexShapeMove::getVolume, &ConvexPolyhedronVertexShapeMove::setVolume)
    .def_property("stepsize", &ConvexPolyhedronVertexShapeMove::getStepsize, &ConvexPolyhedronVertexShapeMove::setStepsize)
    .def_property("param_ratio", &ConvexPolyhedronVertexShapeMove::getParamRatio, &ConvexPolyhedronVertexShapeMove::setParamRatio)
    ;
    }

template<class Shape>
void export_PythonShapeMove(pybind11::module& m, const std::string& name)
    {
    pybind11::class_< PythonShapeMove<Shape>, std::shared_ptr< PythonShapeMove<Shape> >, ShapeMoveBase<Shape> >(m, name.c_str())
    .def(pybind11::init<unsigned int,
                        pybind11::object,
                        std::vector< std::vector<Scalar> >,
                        std::vector<Scalar>,
                        Scalar >())
    .def_property("params", &PythonShapeMove<Shape>::getParams, &PythonShapeMove<Shape>::setParams)
    .def_property("stepsize", &PythonShapeMove<Shape>::getStepsize, &PythonShapeMove<Shape>::setStepsize)
    .def_property("param_ratio", &PythonShapeMove<Shape>::getParamRatio, &PythonShapeMove<Shape>::setParamRatio)
    .def_property("callback", &PythonShapeMove<Shape>::getCallback, &PythonShapeMove<Shape>::setCallback)
    ;
    }

template<class Shape>
void export_ConstantShapeMove(pybind11::module& m, const std::string& name)
    {
    pybind11::class_< ConstantShapeMove<Shape>, std::shared_ptr< ConstantShapeMove<Shape> >, ShapeMoveBase<Shape> >(m, name.c_str())
    .def(pybind11::init<unsigned int, std::vector< pybind11::dict > >())
    .def_property("shape_params", &ConstantShapeMove<Shape>::getShapeParams, &ConstantShapeMove<Shape>::setShapeParams)
    ;
    }

template void export_UpdaterShape< ShapeSphere >(pybind11::module& m, const std::string& name);
template void export_ShapeMoveInterface< ShapeSphere >(pybind11::module& m, const std::string& name);
template void export_ShapeLogBoltzmann< ShapeSphere >(pybind11::module& m, const std::string& name);
template void export_AlchemyLogBoltzmannFunction< ShapeSphere >(pybind11::module& m, const std::string& name);
template void export_PythonShapeMove< ShapeSphere >(pybind11::module& m, const std::string& name);
template void export_ConstantShapeMove< ShapeSphere >(pybind11::module& m, const std::string& name);

template void export_ShapeMoveInterface< ShapeEllipsoid >(pybind11::module& m, const std::string& name);
template void export_ShapeLogBoltzmann< ShapeEllipsoid >(pybind11::module& m, const std::string& name);
template void export_ElasticShapeMove< ShapeEllipsoid >(pybind11::module& m, const std::string& name);
template void export_ShapeSpringLogBoltzmannFunction< ShapeEllipsoid >(pybind11::module& m, const std::string& name);
template void export_AlchemyLogBoltzmannFunction< ShapeEllipsoid >(pybind11::module& m, const std::string& name);
template void export_UpdaterShape< ShapeEllipsoid >(pybind11::module& m, const std::string& name);
template void export_PythonShapeMove< ShapeEllipsoid >(pybind11::module& m, const std::string& name);
template void export_ConstantShapeMove< ShapeEllipsoid >(pybind11::module& m, const std::string& name);

template void export_ShapeMoveInterface< ShapeConvexPolygon >(pybind11::module& m, const std::string& name);
template void export_ShapeLogBoltzmann< ShapeConvexPolygon >(pybind11::module& m, const std::string& name);
template void export_AlchemyLogBoltzmannFunction< ShapeConvexPolygon >(pybind11::module& m, const std::string& name);
template void export_UpdaterShape< ShapeConvexPolygon >(pybind11::module& m, const std::string& name);
template void export_PythonShapeMove< ShapeConvexPolygon >(pybind11::module& m, const std::string& name);
template void export_ConstantShapeMove< ShapeConvexPolygon >(pybind11::module& m, const std::string& name);

template void export_ShapeMoveInterface< ShapeSimplePolygon >(pybind11::module& m, const std::string& name);
template void export_ShapeLogBoltzmann< ShapeSimplePolygon >(pybind11::module& m, const std::string& name);
template void export_AlchemyLogBoltzmannFunction< ShapeSimplePolygon >(pybind11::module& m, const std::string& name);
template void export_UpdaterShape< ShapeSimplePolygon >(pybind11::module& m, const std::string& name);
template void export_PythonShapeMove< ShapeSimplePolygon >(pybind11::module& m, const std::string& name);
template void export_ConstantShapeMove< ShapeSimplePolygon >(pybind11::module& m, const std::string& name);

template void export_ShapeMoveInterface< ShapeSpheropolygon >(pybind11::module& m, const std::string& name);
template void export_ShapeLogBoltzmann< ShapeSpheropolygon >(pybind11::module& m, const std::string& name);
template void export_AlchemyLogBoltzmannFunction< ShapeSpheropolygon >(pybind11::module& m, const std::string& name);
template void export_UpdaterShape< ShapeSpheropolygon >(pybind11::module& m, const std::string& name);
template void export_PythonShapeMove< ShapeSpheropolygon >(pybind11::module& m, const std::string& name);
template void export_ConstantShapeMove< ShapeSpheropolygon >(pybind11::module& m, const std::string& name);

template void export_ShapeMoveInterface< ShapePolyhedron >(pybind11::module& m, const std::string& name);
template void export_ShapeLogBoltzmann< ShapePolyhedron >(pybind11::module& m, const std::string& name);
template void export_AlchemyLogBoltzmannFunction< ShapePolyhedron >(pybind11::module& m, const std::string& name);
template void export_UpdaterShape< ShapePolyhedron >(pybind11::module& m, const std::string& name);
template void export_PythonShapeMove< ShapePolyhedron >(pybind11::module& m, const std::string& name);
template void export_ConstantShapeMove< ShapePolyhedron >(pybind11::module& m, const std::string& name);

template void export_ShapeMoveInterface< ShapeConvexPolyhedron >(pybind11::module& m, const std::string& name);
template void export_ShapeLogBoltzmann< ShapeConvexPolyhedron >(pybind11::module& m, const std::string& name);
template void export_ElasticShapeMove< ShapeConvexPolyhedron >(pybind11::module& m, const std::string& name);
template void export_ShapeSpringLogBoltzmannFunction< ShapeConvexPolyhedron >(pybind11::module& m, const std::string& name);
template void export_AlchemyLogBoltzmannFunction< ShapeConvexPolyhedron >(pybind11::module& m, const std::string& name);
template void export_ConvexPolyhedronGeneralizedShapeMove< ShapeConvexPolyhedron >(pybind11::module& m, const std::string& name);
template void export_UpdaterShape< ShapeConvexPolyhedron >(pybind11::module& m, const std::string& name);
template void export_PythonShapeMove< ShapeConvexPolyhedron >(pybind11::module& m, const std::string& name);
template void export_ConstantShapeMove< ShapeConvexPolyhedron >(pybind11::module& m, const std::string& name);

template void export_ShapeMoveInterface< ShapeSpheropolyhedron >(pybind11::module& m, const std::string& name);
template void export_ShapeLogBoltzmann< ShapeSpheropolyhedron >(pybind11::module& m, const std::string& name);
template void export_AlchemyLogBoltzmannFunction< ShapeSpheropolyhedron >(pybind11::module& m, const std::string& name);
template void export_UpdaterShape< ShapeSpheropolyhedron >(pybind11::module& m, const std::string& name);
template void export_PythonShapeMove< ShapeSpheropolyhedron >(pybind11::module& m, const std::string& name);
template void export_ConstantShapeMove< ShapeSpheropolyhedron >(pybind11::module& m, const std::string& name);

template void export_ShapeMoveInterface< ShapeSphinx >(pybind11::module& m, const std::string& name);
template void export_ShapeLogBoltzmann< ShapeSphinx >(pybind11::module& m, const std::string& name);
template void export_AlchemyLogBoltzmannFunction< ShapeSphinx >(pybind11::module& m, const std::string& name);
template void export_UpdaterShape< ShapeSphinx >(pybind11::module& m, const std::string& name);
template void export_PythonShapeMove< ShapeSphinx >(pybind11::module& m, const std::string& name);
template void export_ConstantShapeMove< ShapeSphinx >(pybind11::module& m, const std::string& name);

template void export_ShapeMoveInterface< ShapeUnion<ShapeSphere> >(pybind11::module& m, const std::string& name);
template void export_ShapeLogBoltzmann< ShapeUnion<ShapeSphere> >(pybind11::module& m, const std::string& name);
template void export_AlchemyLogBoltzmannFunction< ShapeUnion<ShapeSphere> >(pybind11::module& m, const std::string& name);
template void export_UpdaterShape< ShapeUnion<ShapeSphere> >(pybind11::module& m, const std::string& name);
template void export_PythonShapeMove< ShapeUnion<ShapeSphere> >(pybind11::module& m, const std::string& name);
template void export_ConstantShapeMove< ShapeUnion<ShapeSphere> >(pybind11::module& m, const std::string& name);

}
