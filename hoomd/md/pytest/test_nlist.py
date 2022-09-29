# Copyright (c) 2009-2022 The Regents of the University of Michigan.
# Part of HOOMD-blue, released under the BSD 3-Clause License.

import copy as cp
import hoomd
from hoomd.logging import LoggerCategories
import matplotlib.pyplot as plt
import numpy as np
import pytest
import random
from hoomd.md.nlist import Cell, Stencil, Tree
from hoomd.conftest import (logging_check, pickling_check,
                            autotuned_kernel_parameter_check)

try:
    from mpi4py import MPI
    MPI4PY_IMPORTED = True
except ImportError:
    MPI4PY_IMPORTED = False


def _nlist_params():
    """Each entry in the lsit is a tuple (class_obj, dict(required_args))."""
    nlists = []
    nlists.append((Cell, {}))
    nlists.append((Tree, {}))
    nlists.append((Stencil, dict(cell_width=0.5)))
    return nlists


@pytest.fixture(scope="function",
                params=_nlist_params(),
                ids=(lambda x: x[0].__name__))
def nlist_params(request):
    return cp.deepcopy(request.param)


def _assert_nlist_params(nlist, param_dict):
    """Assert the params of the nlist are the same as in the dictionary."""
    for param, item in param_dict.items():
        if isinstance(item, (tuple, list)):
            assert all(
                a == b
                for a, b in zip(getattr(nlist, param), param_dict[param]))
        else:
            assert getattr(nlist, param) == param_dict[param]


def test_common_params(nlist_params):
    nlist_cls, required_args = nlist_params
    nlist = nlist_cls(**required_args, buffer=0.4)
    default_params_dict = {
        "buffer": 0.4,
        "exclusions": ('bond',),
        "rebuild_check_delay": 1,
        "check_dist": True,
    }
    _assert_nlist_params(nlist, default_params_dict)
    new_params_dict = {
        "buffer":
            np.random.uniform(5.0),
        "exclusions":
            random.sample([
                'bond', '1-4', 'angle', 'dihedral', 'special_pair', 'body',
                '1-3', 'constraint', 'meshbond'
            ], np.random.randint(9)),
        "rebuild_check_delay":
            np.random.randint(8),
        "check_dist":
            False,
    }
    for param in new_params_dict.keys():
        setattr(nlist, param, new_params_dict[param])
    _assert_nlist_params(nlist, new_params_dict)


def test_cell_specific_params():
    nlist = Cell(buffer=0.4)
    _assert_nlist_params(nlist, dict(deterministic=False))
    nlist.deterministic = True
    _assert_nlist_params(nlist, dict(deterministic=True))


def test_stencil_specific_params():
    cell_width = np.random.uniform(12.1)
    nlist = Stencil(cell_width=cell_width, buffer=0.4)
    _assert_nlist_params(nlist, dict(deterministic=False,
                                     cell_width=cell_width))
    nlist.deterministic = True
    x = np.random.uniform(25.5)
    nlist.cell_width = x
    _assert_nlist_params(nlist, dict(deterministic=True, cell_width=x))


def test_simple_simulation(nlist_params, simulation_factory,
                           lattice_snapshot_factory):
    nlist_cls, required_args = nlist_params
    nlist = nlist_cls(**required_args, buffer=0.4)
    lj = hoomd.md.pair.LJ(nlist, default_r_cut=1.1)
    lj.params[('A', 'A')] = dict(epsilon=1, sigma=1)
    lj.params[('A', 'B')] = dict(epsilon=1, sigma=1)
    lj.params[('B', 'B')] = dict(epsilon=1, sigma=1)
    integrator = hoomd.md.Integrator(0.005)
    integrator.forces.append(lj)
    integrator.methods.append(
        hoomd.md.methods.Langevin(hoomd.filter.All(), kT=1))

    sim = simulation_factory(lattice_snapshot_factory(n=10))
    sim.operations.integrator = integrator
    sim.run(2)

    # Force nlist to update every step to ensure autotuning occurs.
    nlist.check_dist = False
    nlist.rebuild_check_delay = 1
    autotuned_kernel_parameter_check(instance=nlist,
                                     activate=lambda: sim.run(1))


def test_auto_detach_simulation(simulation_factory,
                                two_particle_snapshot_factory):
    nlist = Cell(buffer=0.4)
    lj = hoomd.md.pair.LJ(nlist, default_r_cut=1.1)
    lj.params[('A', 'A')] = dict(epsilon=1, sigma=1)
    lj.params[('A', 'B')] = dict(epsilon=1, sigma=1)
    lj.params[('B', 'B')] = dict(epsilon=1, sigma=1)
    lj_2 = cp.deepcopy(lj)
    lj_2.nlist = nlist
    integrator = hoomd.md.Integrator(0.005, forces=[lj, lj_2])
    integrator.methods.append(
        hoomd.md.methods.Langevin(hoomd.filter.All(), kT=1))

    sim = simulation_factory(
        two_particle_snapshot_factory(particle_types=["A", "B"], d=2.0))
    sim.operations.integrator = integrator
    sim.run(0)
    del integrator.forces[1]
    assert nlist._attached
    assert hasattr(nlist, "_cpp_obj")
    del integrator.forces[0]
    assert not nlist._attached
    assert nlist._cpp_obj is None


def test_pickling(simulation_factory, two_particle_snapshot_factory):
    nlist = Cell(0.4)
    pickling_check(nlist)
    lj = hoomd.md.pair.LJ(nlist, default_r_cut=1.1)
    lj.params[('A', 'A')] = dict(epsilon=1, sigma=1)
    lj.params[('A', 'B')] = dict(epsilon=1, sigma=1)
    lj.params[('B', 'B')] = dict(epsilon=1, sigma=1)
    integrator = hoomd.md.Integrator(0.005, forces=[lj])
    integrator.methods.append(
        hoomd.md.methods.Langevin(hoomd.filter.All(), kT=1))

    sim = simulation_factory(
        two_particle_snapshot_factory(particle_types=["A", "B"], d=2.0))
    sim.operations.integrator = integrator
    sim.run(0)
    pickling_check(nlist)


def test_cell_properties(simulation_factory, lattice_snapshot_factory):
    nlist = hoomd.md.nlist.Cell(buffer=0)
    lj = hoomd.md.pair.LJ(nlist, default_r_cut=1.1)
    lj.params[('A', 'A')] = dict(epsilon=1, sigma=1)
    lj.params[('A', 'B')] = dict(epsilon=1, sigma=1)
    lj.params[('B', 'B')] = dict(epsilon=1, sigma=1)
    integrator = hoomd.md.Integrator(0.005)
    integrator.forces.append(lj)
    integrator.methods.append(
        hoomd.md.methods.Langevin(hoomd.filter.All(), kT=1))

    sim = simulation_factory(lattice_snapshot_factory(n=10))
    sim.operations.integrator = integrator

    sim.run(10)

    assert nlist.num_builds == 10
    assert nlist.shortest_rebuild == 1
    dim = nlist.dimensions
    assert len(dim) == 3
    assert dim >= (1, 1, 1)
    assert nlist.allocated_particles_per_cell >= 1


def test_logging():
    base_loggables = {
        'shortest_rebuild': {
            'category': LoggerCategories.scalar,
            'default': True
        },
        'num_builds': {
            'category': LoggerCategories.scalar,
            'default': False
        }
    }
    logging_check(hoomd.md.nlist.NeighborList, ('md', 'nlist'), base_loggables)

    logging_check(
        hoomd.md.nlist.Cell, ('md', 'nlist'), {
            **base_loggables,
            'dimensions': {
                'category': LoggerCategories.sequence,
                'default': False
            },
            'allocated_particles_per_cell': {
                'category': LoggerCategories.scalar,
                'default': False
            },
        })


_TRUE_PAIR_LIST = [[76, 70], [97, 90], [236, 285], [209, 203], [324, 325],
                   [296, 247], [139, 132], [93, 142], [157, 150], [152, 151],
                   [277, 278], [74, 67], [67, 68], [115, 164], [149, 198],
                   [130, 131], [273, 279], [3, 4], [51, 52], [124,
                                                              173], [57, 50],
                   [16, 17], [28, 77], [179, 180], [184, 183], [195, 188],
                   [260, 261], [340, 341], [276, 277], [64, 65], [268, 317],
                   [241, 199], [116, 117], [2, 44], [157, 206], [114, 115],
                   [124, 125], [213, 262], [338, 44], [194, 195], [304, 303],
                   [73, 74], [160, 154], [324, 30], [202, 244], [315, 316],
                   [204, 197], [288, 289], [100, 142], [180, 173], [21, 22],
                   [142, 135], [240, 198], [285, 278], [60, 109], [109, 102],
                   [200, 199], [264, 313], [40, 39], [289, 282], [284, 333],
                   [304, 311], [19, 68], [37, 30], [240, 241], [316, 317],
                   [152, 153], [208, 209], [121, 170], [48, 47], [320, 271],
                   [237, 286], [150, 151], [1, 295], [314, 20], [91, 92],
                   [61, 54], [4, 5], [296, 297], [101, 143], [320, 26],
                   [172, 173], [73, 122], [276, 325], [304, 10], [267, 268],
                   [286, 279], [289, 338], [261, 254], [38, 87], [283, 284],
                   [11, 60], [100, 101], [257, 250], [235, 236], [12, 61],
                   [24, 25], [134, 183], [42, 35], [171, 220], [28, 21],
                   [107, 100], [161, 167], [248, 290], [121, 114], [340, 333],
                   [67, 60], [64, 113], [155, 148], [25, 319], [45,
                                                                38], [48, 41],
                   [76, 125], [36, 37], [170, 219], [256, 207], [142, 143],
                   [248, 297], [132, 125], [50, 99], [219, 268], [112, 63],
                   [224, 225], [246, 247], [146, 140], [5, 47], [57, 106],
                   [104, 103], [184, 177], [126, 127], [90, 139], [101, 150],
                   [272, 266], [160, 159], [37, 38], [192, 191], [253, 254],
                   [300, 6], [128, 121], [202, 196], [325, 31], [145, 146],
                   [281, 330], [99, 148], [198, 199], [236, 237], [245, 294],
                   [270, 271], [209, 258], [218, 219], [137, 130], [241, 242],
                   [91, 84], [136, 135], [243, 236], [83, 77], [254, 247],
                   [264, 265], [189, 238], [80, 73], [82, 131], [11, 4],
                   [326, 319], [325, 326], [208, 207], [59, 108], [268, 261],
                   [227, 276], [201, 250], [195, 244], [257, 306], [321, 27],
                   [156, 205], [189, 182], [112, 118], [161, 162], [178, 227],
                   [88, 95], [306, 299], [19, 20], [3, 52], [53, 54], [56, 63],
                   [250, 251], [9, 10], [248, 249], [72, 65], [18, 11],
                   [328, 322], [150, 199], [176, 127], [10, 59], [201, 202],
                   [193, 242], [286, 335], [27, 20], [43, 92], [125, 119],
                   [290, 291], [57, 58], [172, 165], [203, 196], [252, 253],
                   [40, 47], [173, 166], [174, 167], [266, 259], [43, 44],
                   [276, 269], [228, 221], [190, 183], [229, 222], [206, 255],
                   [176, 183], [58, 107], [146, 139], [341, 47], [99, 141],
                   [75, 124], [32, 39], [222, 215], [262, 311], [282, 275],
                   [281, 282], [136, 185], [51, 93], [264, 215], [145, 138],
                   [30, 23], [273, 274], [214, 263], [82, 75], [56, 57],
                   [164, 213], [128, 135], [336, 287], [3, 45], [188, 237],
                   [0, 294], [34, 83], [320, 321], [164, 165], [148, 190],
                   [28, 29], [168, 175], [238, 239], [272, 321], [120, 113],
                   [273, 322], [10, 3], [112, 119], [124, 117], [290, 339],
                   [284, 277], [322, 28], [305, 11], [80, 129], [304, 305],
                   [280, 287], [288, 246], [2, 3], [341, 342], [149, 150],
                   [244, 238], [210, 203], [339, 45], [14, 7], [162, 155],
                   [130, 179], [258, 251], [269, 262], [106, 99], [236, 229],
                   [187, 180], [220, 213], [5, 54], [61, 62], [81,
                                                               130], [22, 23],
                   [312, 319], [16, 15], [92, 93], [145, 194], [232, 231],
                   [292, 285], [227, 228], [4, 46], [165, 158], [264, 271],
                   [256, 257], [173, 222], [274, 267], [228, 277], [208, 201],
                   [139, 188], [1, 43], [250, 292], [152, 159], [233, 282],
                   [275, 276], [283, 332], [314, 308], [277, 326], [16, 23],
                   [158, 207], [8, 57], [166, 159], [328, 327], [244, 293],
                   [320, 319], [298, 4], [107, 108], [270, 263], [265, 314],
                   [48, 342], [216, 215], [168, 161], [129, 130], [221, 214],
                   [209, 202], [312, 311], [274, 323], [41, 35], [315, 308],
                   [238, 231], [158, 151], [275, 324], [269, 318], [251, 300],
                   [0, 49], [35, 28], [285, 286], [280, 329], [196, 197],
                   [120, 127], [201, 243], [32, 25], [334, 327], [138, 139],
                   [250, 299], [64, 63], [168, 169], [192, 143], [169, 162],
                   [84, 77], [179, 172], [192, 241], [203, 204], [61, 110],
                   [147, 148], [96, 54], [68, 117], [152, 194], [105, 154],
                   [123, 124], [65, 66], [219, 212], [309, 310], [93, 94],
                   [176, 177], [217, 223], [232, 239], [94, 143], [206, 199],
                   [112, 113], [110, 103], [20, 13], [102, 103], [123, 116],
                   [309, 15], [97, 91], [317, 23], [141, 190], [315, 21],
                   [72, 23], [164, 157], [4, 53], [302, 303], [333, 334],
                   [96, 47], [132, 181], [85, 78], [245, 246], [237, 231],
                   [216, 265], [275, 268], [336, 337], [170, 163], [72, 73],
                   [289, 290], [160, 209], [98, 140], [314, 307], [6, 55],
                   [171, 164], [136, 143], [190, 239], [227, 220], [328, 279],
                   [147, 189], [144, 145], [42, 91], [24, 73], [40, 89],
                   [297, 298], [332, 325], [88, 81], [40, 33], [69, 62],
                   [97, 146], [108, 101], [128, 129], [157, 158], [165, 214],
                   [204, 205], [218, 211], [280, 231], [235, 228], [290, 283],
                   [104, 146], [177, 170], [120, 121], [211, 212], [248, 247],
                   [338, 339], [17, 311], [102, 151], [298, 299], [46, 95],
                   [149, 191], [137, 186], [224, 217], [51, 100], [262, 263],
                   [22, 71], [101, 102], [17, 10], [214, 207], [249, 291],
                   [280, 281], [318, 319], [336, 342], [326, 327], [171, 172],
                   [182, 175], [68, 69], [216, 223], [74, 123], [190, 191],
                   [288, 281], [283, 276], [186, 235], [5, 6], [185, 178],
                   [234, 283], [184, 185], [193, 151], [45, 46], [185, 234],
                   [58, 51], [313, 306], [96, 145], [32, 81], [78, 127],
                   [161, 210], [21, 70], [168, 174], [273, 266], [265, 259],
                   [144, 95], [200, 242], [96, 97], [49, 91], [269, 270],
                   [98, 147], [264, 257], [25, 26], [32, 31], [69, 63],
                   [339, 340], [29, 22], [333, 326], [60, 53], [113, 106],
                   [65, 58], [337, 43], [16, 65], [316, 22], [128, 177],
                   [284, 285], [313, 314], [108, 157], [328, 335], [34, 28],
                   [64, 57], [168, 119], [162, 163], [212, 261], [94, 95],
                   [9, 2], [238, 287], [186, 179], [317, 318], [336, 329],
                   [332, 38], [49, 98], [27, 76], [181, 174], [226, 275],
                   [308, 309], [138, 187], [168, 217], [331, 324], [180, 181],
                   [113, 114], [112, 161], [181, 230], [312, 263], [136, 137],
                   [334, 335], [12, 5], [83, 132], [88, 87], [182, 231],
                   [33, 327], [100, 149], [233, 226], [232, 183], [24, 318],
                   [266, 315], [89, 90], [312, 18], [123, 172], [256, 249],
                   [106, 155], [120, 71], [304, 297], [140, 141], [30, 31],
                   [245, 287], [12, 13], [26, 19], [109, 158], [321, 315],
                   [52, 101], [110, 159], [214, 215], [256, 263], [85, 86],
                   [160, 111], [160, 167], [213, 206], [240, 239], [20, 69],
                   [32, 33], [180, 229], [25, 18], [178, 171], [112, 105],
                   [187, 188], [222, 271], [187, 236], [272, 223], [225, 218],
                   [243, 244], [281, 274], [274, 275], [158, 159], [26, 75],
                   [72, 71], [174, 223], [267, 260], [296, 295], [80, 81],
                   [36, 29], [240, 233], [8, 15], [242, 243], [256, 255],
                   [54, 103], [212, 205], [292, 293], [270, 319], [75, 68],
                   [74, 75], [224, 273], [296, 2], [156, 157], [176, 225],
                   [133, 182], [298, 340], [27, 21], [211, 204], [307, 301],
                   [144, 102], [320, 327], [48, 97], [233, 234], [249, 298],
                   [110, 111], [48, 6], [316, 309], [342, 335], [104, 55],
                   [205, 254], [240, 191], [115, 108], [115, 116], [53, 95],
                   [77, 78], [163, 156], [188, 181], [90, 84], [216, 210],
                   [332, 333], [11, 12], [41, 335], [256, 305], [160, 153],
                   [282, 283], [288, 337], [184, 191], [38, 31], [192, 193],
                   [282, 331], [13, 6], [56, 7], [59, 60], [20, 14], [42, 43],
                   [217, 266], [30, 79], [252, 245], [29, 30], [41, 34],
                   [329, 330], [44, 93], [144, 193], [176, 169], [300, 342],
                   [163, 164], [301, 294], [14, 63], [301, 7], [22, 15],
                   [277, 270], [262, 255], [300, 294], [37, 86], [241, 234],
                   [296, 338], [0, 42], [125, 118], [131, 132], [251, 245],
                   [293, 342], [40, 334], [84, 85], [117, 110], [133, 126],
                   [163, 212], [170, 171], [67, 116], [92, 141], [301, 302],
                   [221, 222], [72, 121], [169, 218], [193, 186], [96, 89],
                   [225, 274], [306, 12], [70, 71], [117, 118], [193, 194],
                   [106, 107], [253, 246], [24, 17], [172, 221], [229, 230],
                   [182, 183], [148, 197], [131, 124], [205, 206], [216, 209],
                   [181, 175], [248, 199], [217, 210], [192, 150], [188, 182],
                   [210, 211], [307, 13], [126, 119], [241, 290], [54, 55],
                   [278, 271], [310, 311], [96, 95], [267, 316], [206, 207],
                   [293, 286], [153, 202], [88, 39], [52, 94], [291, 292],
                   [200, 207], [305, 306], [140, 133], [91, 140], [136, 129],
                   [64, 71], [189, 190], [212, 213], [272, 265], [308, 301],
                   [317, 310], [8, 1], [330, 331], [80, 87], [76,
                                                              69], [251, 293],
                   [134, 127], [337, 295], [219, 220], [104, 98], [134, 135],
                   [196, 245], [228, 229], [237, 230], [25, 74], [208, 215],
                   [225, 226], [224, 175], [33, 26], [43, 36], [17, 66],
                   [176, 175], [24, 31], [85, 134], [210, 259], [235, 284],
                   [340, 46], [122, 171], [109, 110], [13, 7], [226, 219],
                   [200, 201], [278, 279], [72, 79], [220, 269], [195, 189],
                   [162, 211], [56, 105], [309, 302], [60, 61], [318, 311],
                   [118, 167], [196, 238], [254, 303], [337, 330], [40, 41],
                   [249, 250], [254, 255], [86, 79], [299, 300], [120, 119],
                   [8, 7], [58, 59], [81, 82], [161, 154], [65, 114], [83, 76],
                   [243, 292], [133, 134], [297, 3], [154, 155], [169, 170],
                   [114, 163], [137, 138], [328, 34], [322, 315], [234, 227],
                   [264, 263], [50, 92], [330, 323], [203, 252], [197, 239],
                   [222, 223], [272, 271], [80, 31], [56, 49], [194, 187],
                   [108, 109], [328, 321], [35, 36], [304, 255], [136, 87],
                   [192, 185], [69, 118], [252, 301], [324, 317], [81, 74],
                   [339, 332], [146, 195], [242, 291], [285, 334], [337, 338],
                   [86, 135], [78, 79], [177, 226], [62, 111],
                   [17, 18], [26, 27], [259, 252], [307, 300], [64, 15],
                   [105, 98], [125, 174], [107, 156], [139, 133], [118, 111],
                   [165, 166], [19, 12], [99, 100], [184, 233], [323, 316],
                   [312, 313], [2, 51], [194, 243], [73, 66], [129, 178],
                   [141, 134], [205, 198], [234, 235], [293, 287], [49, 55],
                   [177, 178], [52, 53], [213, 214], [145, 103], [117, 166],
                   [302, 295], [154, 203], [70, 63], [10, 11], [97, 55],
                   [336, 42], [36, 85], [229, 278], [268, 269], [333, 39],
                   [128, 127], [90, 83], [226, 227], [321, 314], [88, 89],
                   [297, 339], [232, 225], [114, 107], [265, 258], [278, 327],
                   [294, 295], [202, 251], [197, 198], [82, 83], [24, 23],
                   [240, 289], [299, 5], [98, 99], [116, 165], [130, 123],
                   [122, 115], [46, 39], [221, 270], [336, 294], [68, 61],
                   [35, 84], [329, 35], [59, 52], [44, 45], [144, 137],
                   [166, 215], [217, 218], [232, 281], [197, 246], [258, 307],
                   [329, 322], [93, 86], [178, 179], [77, 126], [104, 111],
                   [259, 308], [154, 147], [208, 257], [272, 279], [132, 126],
                   [140, 189], [248, 255], [116, 109], [260, 309], [198, 247],
                   [232, 233], [288, 287], [289, 247], [44, 37], [331, 37],
                   [14, 15], [331, 332], [138, 131], [184, 135], [77, 70],
                   [120, 169], [305, 298], [46, 47], [292, 341], [323, 324],
                   [246, 295], [86, 87], [253, 302], [18, 19], [308, 14],
                   [128, 79], [21, 14], [166, 167], [173, 174], [230, 223],
                   [261, 310], [16, 9], [9, 58], [1, 50], [0, 1], [48, 42],
                   [32, 326], [224, 231], [75, 76], [185, 186], [38, 39],
                   [320, 313], [89, 138], [224, 230], [330, 36], [204, 253],
                   [9, 303], [50, 51], [1, 2], [155, 156], [8, 9], [310, 303],
                   [13, 62], [291, 284], [89, 82], [104, 153], [259, 260],
                   [66, 115], [142, 191], [258, 252], [266, 267], [45, 94],
                   [18, 67], [122, 123], [78, 71], [33, 82], [105, 111],
                   [299, 341], [325, 318], [62, 55], [84, 133], [329, 335],
                   [200, 249], [280, 286], [244, 237], [341, 334], [141, 142],
                   [216, 167], [242, 235], [0, 6], [288, 239], [16, 310],
                   [280, 273], [156, 149], [313, 19], [148, 149], [70, 119],
                   [218, 267], [105, 106], [0, 7], [126, 175], [220, 221],
                   [186, 187], [153, 147], [129, 122], [8, 302], [312, 305],
                   [200, 151], [261, 262], [144, 143], [260, 253], [80, 79],
                   [49, 50], [121, 122], [66, 67], [33, 34], [152, 201],
                   [153, 195], [179, 228], [257, 258], [291, 340], [94, 87],
                   [306, 307], [92, 85], [208, 159], [322, 323], [296, 303],
                   [41, 90], [66, 59], [211, 260], [113, 162], [29, 78],
                   [53, 102], [230, 279], [56, 62], [152, 103], [88, 137],
                   [338, 331], [34, 27], [155, 204], [147, 196], [323, 29],
                   [131, 180]]

TRUE_PAIR_LIST = set([frozenset(pair) for pair in _TRUE_PAIR_LIST])


def test_global_pair_list(simulation_factory, lattice_snapshot_factory):

    nlist = hoomd.md.nlist.Cell(buffer=0, default_r_cut=1.1)

    sim = simulation_factory(lattice_snapshot_factory())
    integrator = hoomd.md.Integrator(0.005)
    integrator.methods.append(hoomd.md.methods.NVE(hoomd.filter.All()))
    sim.operations.integrator = integrator
    sim.operations.computes.append(nlist)

    sim.run(0)

    pair_list = nlist.pair_list
    print(pair_list.shape)
    print(MPI4PY_IMPORTED)
    if sim.device.communicator.rank == 0:
        pair_list = set([frozenset(pair) for pair in pair_list])
        assert pair_list == TRUE_PAIR_LIST


def test_rank_local_pair_list(simulation_factory, lattice_snapshot_factory):

    nlist = hoomd.md.nlist.Tree(buffer=0, default_r_cut=1.1)

    print(len(_TRUE_PAIR_LIST), len(_TRUE_PAIR_LIST))

    sim: hoomd.Simulation = simulation_factory(lattice_snapshot_factory())
    integrator = hoomd.md.Integrator(0.005)
    integrator.methods.append(hoomd.md.methods.NVE(hoomd.filter.All()))
    sim.operations.integrator = integrator
    sim.operations.computes.append(nlist)

    sim.run(0)

    local_pair_list = nlist.local_pair_list

    tag_pair_list = []
    with sim.state.cpu_local_snapshot as data:
        rtags = data.particles.tag_with_ghost
        for pair in local_pair_list:
            tag_pair_list.append([rtags[pair[0]], rtags[pair[1]]])

    tag_pair_list = np.array(tag_pair_list)

    if MPI4PY_IMPORTED:

        comm = MPI.COMM_WORLD
        # size = comm.Get_size()
        rank = comm.Get_rank()

        snapshot = sim.state.get_snapshot()
        if rank == 0:
            data = np.copy(snapshot.particles.position)
        else:
            data = None

        data = comm.bcast(data, root=0)
        
        # box = sim.state.box._cpp_obj

        with sim.state.cpu_local_snapshot as snap:
            pos = snap.particles.position_with_ghost
            dpos = snap.particles.position
            for p, tag_p in zip(local_pair_list, tag_pair_list):
                if np.any(data[tag_p[1]] != pos[p[1]]):
                    print("BAD!", tag_p[1], p[1], data[tag_p[1]], pos[p[1]])
                elif p[1] >= len(dpos):
                    print("Ghost particle OK")
                else:
                    print("Particle OK")

    # box = sim.state.box._cpp_obj

    # with sim.state.cpu_local_snapshot as snap:
    #     diffs = 0
    #     # print(snap.particles.N)
    #     pos = snap.particles.position_with_ghost
    #     tag = snap.particles.tag_with_ghost
        # # rtag = snap.particles.rtag
        # len_pp = len(snap.particles.position)
        # len_pt = len(tag)
        # # print(tag)
        # # print(rtag[10])
        # # print(pos[rtag[10]])
        # print(len_pp, len_pt)
        # for i, j in local_pair_list:
        #     # assert i != j
        #     # if j >= len_pp:
        #     #     continue
        #     pos_i = pos[i]
        #     pos_j = pos[j]
        #     v = box.minImage(hoomd.box._make_scalar3(pos_i - pos_j))
        #     diff = np.sqrt(np.sum(v.x**2 + v.y**2 + v.z**2))
        #     if diff == 0.0:
        #         print(i, j, tag[i], tag[j], pos_i, pos_j)
        #     if diff <= 1.1:
        #         diffs += 1
        # print(diffs)

    # if MPI4PY_IMPORTED:

    #     tag_pair_list = np.array(tag_pair_list)

    #     comm = MPI.COMM_WORLD
    #     size = comm.Get_size()
    #     rank = comm.Get_rank()
    #     print(len(tag_pair_list))
    #     sendbuf = np.int32(len(tag_pair_list))
    #     recvbuf = None
    #     if rank == 0:
    #         recvbuf = np.empty(size, dtype=np.int32)
    #     comm.Gather(sendbuf, recvbuf, root=0)

    #     print(recvbuf)


def test_rank_local_nlist_arrays(simulation_factory, lattice_snapshot_factory):

    nlist = hoomd.md.nlist.Cell(buffer=0, default_r_cut=1.1)

    sim = simulation_factory(lattice_snapshot_factory())
    integrator = hoomd.md.Integrator(0.005)
    integrator.methods.append(hoomd.md.methods.NVE(hoomd.filter.All()))
    sim.operations.integrator = integrator
    sim.operations.computes.append(nlist)

    sim.run(0)

    local_pair_list = nlist.local_pair_list

    with nlist.cpu_local_nlist_arrays as data:
        k = 0
        for i, (head, nn) in enumerate(zip(data.head_list, data.n_neigh)):
            for j_idx in range(head, head + nn):
                j = data.nlist[j_idx]
                pair = local_pair_list[k]
                assert i == pair[0]
                assert j == pair[1]
                k += 1
