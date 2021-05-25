Deprecated features
===================

v2.x
----

Commands and features deprecated in v2.x will be removed in v3.0.

:py:mod:`hoomd`:

.. list-table::
   :header-rows: 1

   * - Feature
     - Replace with
   * - Python 2.7
     - Python >= 3.6
   * - ``static`` parameter in ``hoomd.dump.gsd``
     - ``dynamic`` parameter
   * - ``set_params`` and other ``set_*`` methods
     - Parameters and type parameters accessed by properties.
   * - ``context.initialize``
     - ``device.CPU`` / ``device.GPU``
   * - ``util.quiet_status`` and ``util.unquiet_status``
     - No longer needed.

``hoomd.deprecated``:

.. list-table::
   :header-rows: 1

   * - Feature
     - Replace with
   * - ``deprecated.analyze.msd``
     - Offline analysis: e.g. `Freud's msd module <https://freud.readthedocs.io>`_.
   * - ``deprecated.dump.xml``
     - ``hoomd.dump.gsd``
   * - ``deprecated.dump.pos``
     - ``hoomd.dump.gsd`` with on-demand conversion to ``.pos``.
   * - ``deprecated.init.read_xml``
     - ``init.read_gsd``
   * - ``deprecated.init.create_random``
     - `mBuild <https://mosdef-hub.github.io/mbuild/>`_, `packmol <https://www.ime.unicamp.br/~martinez/packmol/userguide.shtml>`_, or user script.
   * - ``deprecated.init.create_random_polymers``
     - `mBuild <https://mosdef-hub.github.io/mbuild/>`_, `packmol <https://www.ime.unicamp.br/~martinez/packmol/userguide.shtml>`_, or user script.

:py:mod:`hoomd.hpmc`:

.. list-table::
   :header-rows: 1

   * - Feature
     - Replace with
   * - ``sphere_union::max_members`` parameter
     - no longer needed
   * - ``convex_polyhedron_union``
     - :py:class:`ConvexSpheropolyhedronUnion <hoomd.hpmc.integrate.ConvexSpheropolyhedronUnion>`, ``sweep_radius=0``
   * - ``setup_pos_writer`` member
     - n/a
   * - ``depletant_mode='circumsphere'``
     - no longer needed
   * - ``max_verts`` parameter
     - no longer needed
   * - ``depletant_mode`` parameter
     - no longer needed
   * - ``ntrial`` parameter
     - no longer needed
   * - ``implicit`` boolean parameter
     - set ``fugacity`` non-zero

:py:mod:`hoomd.md`:

.. list-table::
   :header-rows: 1

   * - Feature
     - Replace with
   * - ``group`` parameter to ``integrate.mode_minimize_fire``
     - Pass group to integration method.
   * - ``alpha`` parameter to ``pair.lj`` and related classes
     - n/a
   * - LJ 12-8 pair potential
     - Mie potential
   * - ``f_list`` and ``t_list`` parameters to ``md.force.active``
     - Per-type ``active_force`` and ``active_torque``

``hoomd.cgcmm``:

.. list-table::
   :header-rows: 1

   * - Feature
     - Replace with
   * - ``cgcmm.angle.cgcmm``
     - no longer needed
   * - ``cgcmm.pair.cgcmm``
     - no longer needed
