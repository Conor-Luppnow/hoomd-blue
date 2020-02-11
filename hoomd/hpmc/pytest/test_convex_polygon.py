import hoomd
import numpy as np
import hoomd.hpmc._hpmc as _hpmc
from hoomd import hpmc


# This tests passing back and forth from C++
def test_polygon_vertices():

    args_1 = {'vertices': [(0, 0), (1, 1), (1, 0), (0, 1), (1, 1), (0, 0)],
              'ignore_statistics': 1,
              'sweep_radius': 1.0}
    test_convex_polygon1 = _hpmc.PolygonVertices(args_1)
    test_dict1 = test_convex_polygon1.asDict()
    assert test_dict1 == args_1

    args_2 = {'vertices': [(0, 0), (0, 1), (1, 3), (5, 1)],
              'ignore_statistics': 0,
              'sweep_radius': 0.5}
    test_convex_polygon2 = _hpmc.PolygonVertices(args_2)
    test_dict2 = test_convex_polygon2.asDict()
    assert test_dict2 == args_2

    args_3 = {'vertices': [(0, 0), (1, 1), (1, 0), (0, 1),
                           (1, 1), (0, 0), (2, 1), (1, 3)],
              'ignore_statistics': 1,
              'sweep_radius': 1.5}
    test_convex_polygon3 = _hpmc.PolygonVertices(args_3)
    test_dict3 = test_convex_polygon3.asDict()
    assert test_dict3 == args_3

    args_4 = {'vertices': [(0, 0), (1, 1), (1, 0), (0, 1), (1, 1),
                           (0, 0), (2, 1), (1, 3), (9, 8), (1, 1)],
              'ignore_statistics': 0,
              'sweep_radius': 2.0}
    test_convex_polygon4 = _hpmc.PolygonVertices(args_4)
    test_dict4 = test_convex_polygon4.asDict()
    assert test_dict4 == args_4


# these tests are for the python side
def test_convex_polygon_shape():

    verts = [(-1, 1), (1, -1), (1, 1), (-1, -1)]
    poly = hpmc.integrate.ConvexPolygon(23456)
    poly.shape['A'] = dict(vertices=verts)
    assert not poly.shape['A']['ignore_statistics']
    np.testing.assert_allclose(poly.shape['A']['vertices'], verts)


def test_convex_poly_after_attaching(device, dummy_simulation_factory):

    verts = [(-1, 1), (1, -1), (1, 1), (-1, -1)]
    verts2 = [(-1, 1), (1, -1), (1, 1)]
    poly = hpmc.integrate.ConvexPolygon(23456)
    poly.shape['A'] = dict(vertices=verts)
    poly.shape['B'] = dict(vertices=verts2, ignore_statistics=True)

    sim = dummy_simulation_factory(particle_types=['A', 'B'])
    sim.operations.add(poly)
    sim.operations.schedule()

    assert not poly.shape['A']['ignore_statistics']
    assert poly.shape['B']['ignore_statistics']
    np.testing.assert_allclose(poly.shape['A']['vertices'], verts)
    np.testing.assert_allclose(poly.shape['B']['vertices'], verts2)


def test_overlaps(device, lattice_simulation_factory):
    mc = hoomd.hpmc.integrate.ConvexPolygon(23456, d=0, a=0)
    mc.shape['A'] = dict(vertices=[(0, (0.75**0.5) / 2),
                                   (-0.5, -(0.75**0.5) / 2),
                                   (0.5, -(0.75**0.5) / 2)])
    sim = lattice_simulation_factory(dimensions=2, n=(2, 1), a=0.25)
    sim.operations.add(mc)
    sim.operations.schedule()
    sim.run(1)
    assert mc.overlaps > 0

    s = sim.state.snapshot
    if s.exists:
        s.particles.position[0] = (0, 0, 0)
        s.particles.position[1] = (0, 8, 0)
    sim.state.snapshot = s
    assert mc.overlaps == 0

    s = sim.state.snapshot
    if s.exists:
        s.particles.position[0] = (0, 0, 0)
        s.particles.position[1] = (0, 0.85, 0)
    sim.state.snapshot = s
    assert mc.overlaps > 0


def test_shape_moves(device, lattice_simulation_factory):
    mc = hoomd.hpmc.integrate.ConvexPolygon(23456)
    mc.shape['A'] = dict(vertices=[(0, (0.75**0.5) / 2),
                                   (-0.5, -(0.75**0.5) / 2),
                                   (0.5, -(0.75**0.5) / 2)])
    sim = lattice_simulation_factory(dimensions=2)
    sim.operations.add(mc)
    sim.operations.schedule()
    sim.run(100)
    accepted_rejected_rot = sum(sim.operations.integrator.rotate_moves)
    print(mc.overlaps)
    assert accepted_rejected_rot > 0
    accepted_rejected_trans = sum(sim.operations.integrator.translate_moves)
    assert accepted_rejected_trans > 0
