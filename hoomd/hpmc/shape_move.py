import hoomd
from hoomd.operation import _HOOMDBaseObject
from . import _hpmc
from hoomd.hpmc import integrate
from hoomd.data.parameterdicts import ParameterDict
from hoomd.logging import log


class ShapeMove(_HOOMDBaseObject):
    """Base class for all shape moves.

    A shape move is used as an argument to hoomd.hpmc.update.Shape to specify
    how to alter shape definitions

    Note:
        This class should not be instantiated by users. The class can be used
        for `isinstance` or `issubclass` checks.
    """
    def _attach(self):
        self._apply_param_dict()
        self._apply_typeparam_dict(self._cpp_obj, self._simulation)


class Callback(_HOOMDBaseObject):
    """Base class for callbacks used in Python shape moves.

    Note:
        This class should not be instantiated by users. User-defined callbacks
        should inherit from this class, defining a __call__ method that takes
        a list of floats as an input and returns a shape definition

    Examples::

        class ExampleCallback(hoomd.hpmc.shape_move.Callback):
            def __call__(self, params):
                # do something with params and define verts
                return hoomd.hpmc._hpmc.PolyhedronVertices(verts)
    """
    def __init__(self):
        pass


class Constant(ShapeMove):
    """Apply a transition to a specified shape, changing a particle shape by
    the same way every time the updater is called.

    Note:
        This is useful for calculating a specific transition probability and
        derived thermodynamic quantities.

    Args:
        shape_params (dict): Arguments defining the shape to transition to

    Examples::

        mc = hoomd.hpmc.integrate.ConvexPolyhedron(23456)
        tetrahedron_verts = [(1, 1, 1), (-1, -1, 1),
                             (1, -1, -1), (-1, 1, -1)]
        mc.shape["A"] = dict(vertices=tetrahedron_verts)
        cube_verts = [(1, 1, 1), (1, 1, -1), (1, -1, 1), (-1, 1, 1),
                      (1, -1, -1), (-1, 1, -1), (-1, -1, 1), (-1, -1, -1)])
        constant_move = hoomd.hpmc.shape_move.Constant(shape_params=cube_verts)

    Attributes:

        shape_params (dict): Arguments defining the shape to transition to

    See Also:
        hoomd.hpmc.integrate for required shape parameters.
    """
    def __init__(self, shape_params):
        self._param_dict.update(ParameterDict(shape_params=dict(shape_params)))

    def _attach(self):
        integrator = self._simulation.operations.integrator
        if not isinstance(integrator, integrate.HPMCIntegrator):
            raise RuntimeError("The integrator must be a HPMC integrator.")
        if not integrator._attached:
            raise RuntimeError("Integrator is not attached yet.")

        move_cls = None
        boltzmann_cls = None
        shapes = ['Sphere', 'ConvexPolygon', 'SimplePolygon',
                  'ConvexPolyhedron', 'ConvexSpheropolyhedron',
                  'Ellipsoid', 'ConvexSpheropolygon', 'Polyhedron',
                  'Sphinx', 'SphereUnion']
        for shape in shapes:
            if isinstance(integrator, getattr(integrate, shape)):
                move_cls = getattr(_hpmc, 'ConstantShapeMove' + shape)
                boltzmann_cls = getattr(_hpmc, 'AlchemyLogBoltzmann' + shape)
        if move_cls is None or boltzmann_cls is None:
            raise RuntimeError("Integrator not supported")

        ntypes = self._simulation.state._cpp_sys_def.getParticleData().getNTypes()
        self._cpp_obj = move_cls(self._simulation.state._cpp_sys_def, ntypes, self.shape_params)
        self._log_boltzmann_function = boltzmann_cls()
        super()._attach()


class Elastic(ShapeMove):
    """Apply scale and shear shape moves to particles.

    Args:
        stiffness (Variant): Spring stiffness when shearing particles.

        reference (dict): Arguments defining the shape to reference
            the spring to.

        stepsize (float): Largest scaling/shearing factor used.

        param_ratio (float): Fraction of scale to shear moves.

    Example::

        mc = hoomd.hpmc.integrate.ConvexPolyhedron(23456)
        verts = [(1, 1, 1), (-1, -1, 1), (1, -1, -1), (-1, 1, -1)]
        mc.shape["A"] = dict(vertices=verts)
        elastic_move = hoomd.hpmc.shape_move.Elastic(stiffness=hoomd.variant.Constant(1.0),
                                                     reference=dict(vertices=verts),
                                                     stepsize=0.05,
                                                     param_ratio=0.2)

    Attributes:

        stiffness (Variant): Spring stiffness when shearing particles.

        reference (dict): Arguments defining the shape to reference
            the spring to.

        stepsize (float): Largest scaling/shearing factor used.

        param_ratio (float): Fraction of scale to shear moves.
    """
    def __init__(self, stiffness, reference, stepsize, param_ratio):
        param_dict = ParameterDict(stiffness=hoomd.variant.Variant,
                                   reference=dict(reference),
                                   stepsize=float(stepsize),
                                   param_ratio=float(param_ratio))
        param_dict["stiffness"] = stiffness
        self._param_dict.update(param_dict)

    def _attach(self):
        integrator = self._simulation.operations.integrator
        if not isinstance(integrator, integrate.HPMCIntegrator):
            raise RuntimeError("The integrator must be a HPMC integrator.")
        if not integrator._attached:
            raise RuntimeError("Integrator is not attached yet.")

        move_cls = None
        shape_cls = None
        boltzmann_cls = None
        if isinstance(integrator, integrate.ConvexPolyhedron):
            move_cls = _hpmc.ElasticShapeMoveConvexPolyhedron
            boltzmann_cls = _hpmc.ShapeSpringLogBoltzmannConvexPolyhedron
            shape_cls = hoomd.hpmc._hpmc.PolyhedronVertices
        elif isinstance(integrator, integrate.Ellipsoid):
            move_cls = _hpmc.ElasticShapeMoveEllipsoid
            for type_shape in self.mc.type_shapes():
                if not np.isclose(type_shape["a"], type_shape["b"]) or \
                   not np.isclose(type_shape["a"], type_shape["c"]) or \
                   not np.isclose(type_shape["b"], type_shape["c"]):
                    raise ValueError("This updater only works when a=b=c initially.")
            boltzmann_cls = _hpmc.ShapeSpringLogBoltzmannEllipsoid
            shape_cls = hoomd.hpmc._hpmc.EllipsoidParams
        else:
            raise RuntimeError("Integrator not supported")

        ntypes = self._simulation.state._cpp_sys_def.getParticleData().getNTypes()
        self._cpp_obj = move_cls(self._simulation.state._cpp_sys_def,
                                 ntypes,
                                 self.stepsize,
                                 self.param_ratio)
        self._log_boltzmann_function = boltzmann_cls(self._param_dict["stiffness"], self._param_dict["reference"], self._cpp_obj)
        super()._attach()

    @property
    def stiffness(self):
        if self._attached:
            return self._log_boltzmann_function.stiffness(self._simulation.timestep)
        else:
            return self._param_dict["stiffness"]

    @stiffness.setter
    def stiffness(self, new_stiffness):
        self._param_dict["stiffness"] = new_stiffness
        if self._attached:
            self._log_boltzmann_function.stiffness = new_stiffness

    @log(category="scalar")
    def shape_move_stiffness(self):
        """float: Stiffness of the shape used to calculate shape energy.

        None when not attached
        """
        return self.stiffness


class Python(ShapeMove):
    """Apply custom shape moves to particles through a Python callback.

    Args:
        callback (Callback): The python class that will be called
            to update the particle shapes

        params (dict): Dictionary of types and the corresponding list
            of initial parameters to pass to the callback
            (ex: {'A' : [1.0], 'B': [0.0]})

        stepsize (dict): Dictionary of types and the corresponding step size
            to use when changing parameter values

        param_ratio (float): Average fraction of parameters to change during
            each shape move

    Note:
        Parameters must be given for every particle type. The callback should
        rescale the particle to have constant volume if desired.

    Example::

        mc = hoomd.hpmc.integrate.ConvexPolyhedron(23456)
        mc.shape["A"] = dict(vertices=[(1, 1, 1), (-1, -1, 1),
                                       (1, -1, -1), (-1, 1, -1)])
        # example callback
        class ExampleCallback(hoomd.hpmc.shape_move.Callback):
            def __call__(self, params):
                # do something with params and define verts
                return hoomd.hpmc._hpmc.PolyhedronVertices(verts)
        python_move = hoomd.hpmc.shape_move.Python(callback=ExampleCallback,
                                                   params={'A': [1.0]},
                                                   stepsize={'A': 0.05},
                                                   param_ratio=1.0)

    Attributes:

        callback (Callback): The python class that will be called
            to update the particle shapes

        params (dict): Dictionary of types and the corresponding list
            of initial parameters to pass to the callback
            (ex: {'A' : [1.0], 'B': [0.0]})

        stepsize (dict): Dictionary of types and the corresponding step size
            to use when changing parameter values

        param_ratio (float): Average fraction of parameters to change during
            each shape move
    """
    def __init__(self, callback, params, stepsize, param_ratio):
        param_dict = ParameterDict(callback=Callback,
                                   params=dict(params),
                                   stepsize=dict(stepsize),
                                   param_ratio=float(param_ratio))
        param_dict["callback"] = callback
        self._param_dict.update(param_dict)

    def _attach(self):
        integrator = self._simulation.operations.integrator
        if not isinstance(integrator, integrate.HPMCIntegrator):
            raise RuntimeError("The integrator must be a HPMC integrator.")
        if not integrator._attached:
            raise RuntimeError("Integrator is not attached yet.")

        move_cls = None
        boltzmann_cls = None
        shapes = ['Sphere', 'ConvexPolygon', 'SimplePolygon',
                  'ConvexPolyhedron', 'ConvexSpheropolyhedron',
                  'Ellipsoid', 'ConvexSpheropolygon', 'Polyhedron',
                  'Sphinx', 'SphereUnion']
        for shape in shapes:
            if isinstance(integrator, getattr(integrate, shape)):
                move_cls = getattr(_hpmc, 'PythonShapeMove' + shape)
                boltzmann_cls = getattr(_hpmc, 'AlchemyLogBoltzmann' + shape)
        if move_cls is None or boltzmann_cls is None:
            raise RuntimeError("Integrator not supported")

        ntypes = self._simulation.state._cpp_sys_def.getParticleData().getNTypes()
        self._cpp_obj = move_cls(self._simulation.state._cpp_sys_def,
                                 ntypes,
                                 self.callback,
                                 self.params,
                                 self.stepsize,
                                 self.param_ratio)
        self._log_boltzmann_function = boltzmann_cls()
        super()._attach()

    @log(category='object')
    def shape_param(self):
        """float: Shape parameter values being used.

        None when not attached
        """
        return self.params


class Vertex(ShapeMove):
    """Apply shape moves where particle vertices are translated.

    Args:
        stepsize (dict): Dictionary of types and the corresponding step size
            to use when changing parameter values

        param_ratio (float): Average fraction of vertices to change during
            each shape move

        volume (float): Volume of the particles to hold constant

    Note:
        Vertices are rescaled during each shape move to ensure that the shape
        maintains a constant volume

    Note:
        The shape definition used corresponds to the convex hull of the
        vertices.

    Example::

        mc = hoomd.hpmc.integrate.ConvexPolyhedron(23456)
        cube_verts = [(1, 1, 1), (1, 1, -1), (1, -1, 1), (-1, 1, 1),
                      (1, -1, -1), (-1, 1, -1), (-1, -1, 1), (-1, -1, -1)])
        mc.shape["A"] = dict(vertices=numpy.asarray(cube_verts) / 2)
        vertex_move = hoomd.hpmc.shape_move.Vertex(stepsize={'A': 0.01},
                                                   param_ratio=0.125,
                                                   volume=1.0)

    Attributes:

        stepsize (dict): Dictionary of types and the corresponding step size
            to use when changing parameter values

        param_ratio (float): Average fraction of vertices to change during
            each shape move

        volume (float): Volume of the particles to hold constant
    """
    def __init__(self, stepsize, param_ratio, volume):
        param_dict = ParameterDict(stepsize=dict(stepsize),
                                   param_ratio=float(param_ratio),
                                   volume=float(volume))
        self._param_dict.update(param_dict)

    def _attach(self):
        integrator = self._simulation.operations.integrator
        if not isinstance(integrator, integrate.HPMCIntegrator):
            raise RuntimeError("The integrator must be a HPMC integrator.")
        if not integrator._attached:
            raise RuntimeError("Integrator is not attached yet.")

        move_cls = None
        boltzmann_cls = None
        if isinstance(integrator, integrate.ConvexPolyhedron):
            move_cls = _hpmc.GeneralizedShapeMoveConvexPolyhedron
            boltzmann_cls = _hpmc.AlchemyLogBoltzmannConvexPolyhedron
        else:
            raise RuntimeError("Integrator not supported")

        ntypes = self._simulation.state._cpp_sys_def.getParticleData().getNTypes()
        self._cpp_obj = move_cls(self._simulation.state._cpp_sys_def,
                                 ntypes,
                                 self.stepsize,
                                 self.param_ratio,
                                 self.volume)
        self._log_boltzmann_function = boltzmann_cls()
        super()._attach()
