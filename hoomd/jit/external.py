# Copyright (c) 2009-2021 The Regents of the University of Michigan
# This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

from hoomd.jit import _jit
from hoomd.jit.patch import _JITCompute
from hoomd.hpmc import integrate
import hoomd

class UserExternal(_JITCompute):
    R''' Define an external field imposed on all particles in the system.

    Args:
        code (str): C++ code to compile
        llvm_ir_fname (str): File name of the llvm IR file to load.
        clang_exec (str): The Clang executable to use

    Potentials in jit.external behave similarly to external fields assigned via
    hpmc.field.callback. Potentials added using external.user are added to the total
    energy calculation in :py:mod:`hpmc <hoomd.hpmc>` integrators. The
    :py:class:`user` external field takes C++ code, JIT compiles it at run time
    and executes the code natively in the MC loop at with full performance. It
    enables researchers to quickly and easily implement custom energetic
    interactions without the need to modify and recompile HOOMD.

    .. rubric:: C++ code

    Supply C++ code to the *code* argument and :py:class:`user` will compile the code and call it to evaluate
    forces. Compilation assumes that a recent ``clang`` installation is on your PATH. This is convenient
    when the energy evaluation is simple or needs to be modified in python. More complex code (i.e. code that
    requires auxiliary functions or initialization of static data arrays) should be compiled outside of HOOMD
    and provided via the *llvm_ir_file* input (see below).

    The text provided in *code* is the body of a function with the following signature:

    .. code::

        float eval(const BoxDim& box,
        unsigned int type_i,
        const vec3<Scalar>& r_i,
        const quat<Scalar>& q_i
        Scalar diameter,
        Scalar charge
        )

    * ``vec3`` and ``quat`` are is defined in HOOMDMath.h.
    * *box* is the system box.
    * *type_i* is the particle type.
    * *r_i* is the particle position
    * *q_i* the particle orientation.
    * *diameter* the particle diameter.
    * *charge* the particle charge.
    * Your code *must* return a value.

    Once initialized, the following log quantities are provided to analyze.log:

    * **external_field_jit** -- total energy of the field

    Example:

    .. code-block:: python

        gravity = """return r_i.z + box.getL().z/2;"""
        external = hoomd.jit.external.user(mc=mc, code=gravity)

    .. rubric:: LLVM IR code

    You can compile outside of HOOMD and provide a direct link
    to the LLVM IR file in *llvm_ir_file*. A compatible file contains an extern "C" eval function with this signature:

    .. code::

        float eval(const BoxDim& box, unsigned int type_i, const vec3<Scalar>& r_i, const quat<Scalar>& q_i, Scalar diameter, Scalar charge)

    ``vec3`` and ``quat`` is defined in HOOMDMath.h.

    Compile the file with clang: ``clang -O3 --std=c++11 -DHOOMD_LLVMJIT_BUILD -I /path/to/hoomd/include -S -emit-llvm code.cc`` to produce
    the LLVM IR in ``code.ll``.

    .. versionadded:: 2.5
    '''
    def __init__(self, clang_exec='clang', code=None, llvm_ir_file=None):
        super().__init__(clang_exec=clang_exec, code=code, llvm_ir_file=llvm_ir_file)


    def _wrap_cpu_code(self, code):
        cpp_function = """
                        #include "hoomd/HOOMDMath.h"
                        #include "hoomd/VectorMath.h"
                        #include "hoomd/BoxDim.h"

                        extern "C"
                        {

                        float eval(const BoxDim& box,
                        unsigned int type_i,
                        const vec3<Scalar> r_i,
                        const quat<Scalar>& q_i,
                        Scalar diameter,
                        Scalar charge
                        )
                            {
                        """
        cpp_function += code
        cpp_function += """
                            }
                        }
                        """
        return cpp_function


    def _attach(self):
        integrator = self._simulation.operations.integrator
        if not isinstance(integrator, integrate.HPMCIntegrator):
            raise RuntimeError("The integrator must be a HPMC integrator.")

        if (isinstance(self._simulation.device, hoomd.device.GPU)):
            raise RuntimeError("JIT forces are not supported on the GPU.")

        integrator_pairs = [
                (integrate.Sphere,
                    _jit.ExternalFieldJITSphere),
                (integrate.ConvexPolygon,
                    _jit.ExternalFieldJITConvexPolygon),
                (integrate.SimplePolygon,
                    _jit.ExternalFieldJITSimplePolygon),
                (integrate.ConvexPolyhedron,
                    _jit.ExternalFieldJITConvexPolyhedron),
                (integrate.ConvexSpheropolyhedron,
                    _jit.ExternalFieldJITSpheropolyhedron),
                (integrate.Ellipsoid,
                    _jit.ExternalFieldJITEllipsoid),
                (integrate.ConvexSpheropolygon,
                    _jit.ExternalFieldJITSpheropolygon),
                (integrate.FacetedEllipsoid,
                    _jit.ExternalFieldJITFacetedEllipsoid),
                (integrate.Polyhedron,
                    _jit.ExternalFieldJITPolyhedron),
                (integrate.Sphinx,
                    _jit.ExternalFieldJITSphinx)
                ]

        cpp_cls = None
        for python_integrator, cpp_compute in integrator_pairs:
            if isinstance(integrator, python_integrator):
                cpp_cls = cpp_compute
        if cpp_cls is None:
            raise RuntimeError("Unsupported integrator.\n")

        # compile code if provided
        if self._code is not None:
            cpp_function = self._wrap_cpu_code(self._code)
            llvm_ir = self._compile_user(cpp_function, self._clang_exec)
        # fall back to LLVM IR file in case code is not provided
        elif self._llvm_ir_file is not None:
            # IR is a text file
            with open(self._llvm_ir_file,'r') as f:
                llvm_ir = f.read()
        else:
            raise RuntimeError("Must provide code or LLVM IR file.")

        self._cpp_obj = cpp_cls(self._simulation.state._cpp_sys_def,
                                self._simulation.device._cpp_exec_conf,
                                llvm_ir)
        super()._attach()
