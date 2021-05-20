# Copyright (c) 2009-2021 The Regents of the University of Michigan
# This file is part of the HOOMD-blue project, released under the BSD 3-Clause
# License.

"""Angle potentials."""

from hoomd.md import _md
from hoomd.md.force import Force
from hoomd.data.typeparam import TypeParameter
from hoomd.data.parameterdicts import TypeParameterDict
import hoomd


class Angle(Force):
    """Constructs the angular bond potential.

    Note:
        :py:class:`Angle` is the base class for all angular potentials.
        Users should not instantiate this class directly.
    """

    def _attach(self):
        # check that some angles are defined
        if self._simulation.state._cpp_sys_def.getAngleData().getNGlobal() == 0:
            self._simulation.device._cpp_msg.warning("No angles are defined.\n")

        # create the c++ mirror class
        if isinstance(self._simulation.device, hoomd.device.CPU):
            cpp_cls = getattr(_md, self._cpp_class_name)
        else:
            cpp_cls = getattr(_md, self._cpp_class_name + "GPU")

        self._cpp_obj = cpp_cls(self._simulation.state._cpp_sys_def)

        super()._attach()


class Harmonic(Angle):
    R""" Harmonic angle potential.

    The command angle.harmonic specifies a harmonic potential energy between
    every triplet of particles with an angle specified between them.

    .. math::

        V(\theta) = \frac{1}{2} k \left( \theta - \theta_0 \right)^2

    where :math:`\theta` is the angle between the triplet of particles.

    Attributes:
        params (TypeParameter[``angle type``, dict]):
            The parameter of the harmonic bonds for each particle type.
            The dictionary has the following keys:

            * ``k`` (`float`, **required**) - potential constant
              (in units of energy/radians^2)

            * ``t0`` (`float`, **required**) - rest angle
              (in units radians)

    Examples::

        harmonic = angle.Harmonic()
        harmonic.params['polymer'] = dict(k=3.0, t0=0.7851)
        harmonic.params['backbone'] = dict(k=100.0, t0=1.0)

    """

    _cpp_class_name = 'HarmonicAngleForceCompute'

    def __init__(self):
        params = TypeParameter('params', 'angle_types',
                               TypeParameterDict(t0=float, k=float, len_keys=1))
        self._add_typeparam(params)


class Cosinesq(Angle):
    R""" Cosine squared angle potential.

    The command angle.cosinesq specifies a cosine squared potential energy
    between every triplet of particles with an angle specified between them.

    .. math::

        V(\theta) = \frac{1}{2} k \left( \cos\theta - \cos\theta_0 \right)^2

    where :math:`\theta` is the angle between the triplet of particles.
    This angle style is also known as g96, since they were used in the
    gromos96 force field. These are also the types of angles used with the
    coarse-grained MARTINI force field.

    Attributes:
        params (TypeParameter[``angle type``, dict]):
            The parameter of the harmonic bonds for each particle type.
            The dictionary has the following keys:

            * ``k`` (`float`, **required**) - potential constant
              (in units of energy/radians^2)

            * ``t0`` (`float`, **required**) - rest angle :math:`\theta_0`
              (in units radians)

    Parameters :math:`k` and :math:`\theta_0` must be set for each type of
    angle in the simulation.  Note that the value of :math:`k` for this angle
    potential is not comparable to the value of :math:`k` for harmonic angles,
    as they have different units.

    Examples::

        cosinesq = angle.Cosinesq()
        cosinesq.params['polymer'] = dict(k=3.0, t0=0.7851)
        cosinesq.params['backbone'] = dict(k=100.0, t0=1.0)
    """

    _cpp_class_name = 'CosineSqAngleForceCompute'

    def __init__(self):
        params = TypeParameter('params', 'angle_types',
                               TypeParameterDict(t0=float, k=float, len_keys=1))
        self._add_typeparam(params)
