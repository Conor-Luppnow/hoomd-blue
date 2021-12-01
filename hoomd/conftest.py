# Copyright (c) 2009-2021 The Regents of the University of Michigan
# This file is part of the HOOMD-blue project, released under the BSD 3-Clause
# License.

"""Code to support unit and validation tests.

``conftest`` is not part of HOOMD-blue's public API.
"""

import logging
import pickle
import pytest
import hoomd
import atexit
import os
import numpy
import itertools
import math
import warnings
from hoomd.snapshot import Snapshot
from hoomd import Simulation

logger = logging.getLogger()

pytest_plugins = ("hoomd.pytest_plugin_validate",)

devices = [hoomd.device.CPU]
if (hoomd.device.GPU.is_available()
        and len(hoomd.device.GPU.get_available_devices()) > 0):

    if os.environ.get('_HOOMD_SKIP_CPU_TESTS_WHEN_GPUS_PRESENT_') is not None:
        devices.pop(0)

    devices.append(hoomd.device.GPU)


@pytest.fixture(scope='session', params=devices)
def device(request):
    """Parameterized Device fixture.

    Tests that use `device` will run once on the CPU and once on the GPU. The
    device object is session scoped to avoid device creation overhead when
    running tests.
    """
    d = request.param()

    # enable GPU error checking
    if isinstance(d, hoomd.device.GPU):
        d.gpu_error_checking = True

    return d


@pytest.fixture(scope='session')
def simulation_factory(device):
    """Make a Simulation object from a snapshot.

    TODO: duck type this to allow it to create state from GSD files as well
    """

    def make_simulation(snapshot=None, domain_decomposition=None):
        sim = Simulation(device)

        # reduce sorter grid to avoid Hilbert curve overhead in unit tests
        for tuner in sim.operations.tuners:
            if isinstance(tuner, hoomd.tune.ParticleSorter):
                tuner.grid = 8

        if snapshot is not None:
            if domain_decomposition is None:
                sim.create_state_from_snapshot(snapshot)
            else:
                sim.create_state_from_snapshot(snapshot, domain_decomposition)
        return sim

    return make_simulation


@pytest.fixture(scope='session')
def two_particle_snapshot_factory(device):
    """Make a snapshot with two particles."""

    def make_snapshot(particle_types=['A'], dimensions=3, d=1, L=20):
        """Make the snapshot.

        Args:
            particle_types: List of particle type names
            dimensions: Number of dimensions (2 or 3)
            d: Distance apart to place particles
            L: Box length

        The two particles are placed at (-d/2, 0, 0) and (d/2,0,0). When,
        dimensions==3, the box is L by L by L. When dimensions==2, the box is
        L by L by 0.
        """
        s = Snapshot(device.communicator)
        N = 2

        if s.communicator.rank == 0:
            box = [L, L, L, 0, 0, 0]
            if dimensions == 2:
                box[2] = 0
            s.configuration.box = box
            s.particles.N = N
            # shift particle positions slightly in z so MPI tests pass
            s.particles.position[:] = [[-d / 2, 0, .1], [d / 2, 0, .1]]
            s.particles.types = particle_types
            if dimensions == 2:
                box[2] = 0
                s.particles.position[:] = [[-d / 2, 0.1, 0], [d / 2, 0.1, 0]]

        return s

    return make_snapshot


@pytest.fixture(scope='session')
def lattice_snapshot_factory(device):
    """Make a snapshot with particles on a cubic/square lattice."""

    def make_snapshot(particle_types=['A'], dimensions=3, a=1, n=7, r=0):
        """Make the snapshot.

        Args:
            particle_types: List of particle type names
            dimensions: Number of dimensions (2 or 3)
            a: Lattice constant
            n: Number of particles along each box edge
            r: Fraction of `a` to randomly perturb particles

        Place particles on a simple cubic (dimensions==3) or square
        (dimensions==2) lattice. The box is cubic (or square) with a side length
        of `n * a`.

        Set `r` to randomly perturb particles a small amount off their lattice
        positions. This is useful in MD simulation testing so that forces do not
        cancel out by symmetry.
        """
        s = Snapshot(device.communicator)

        if s.communicator.rank == 0:
            box = [n * a, n * a, n * a, 0, 0, 0]
            if dimensions == 2:
                box[2] = 0
            s.configuration.box = box

            s.particles.N = n**dimensions
            s.particles.types = particle_types

            # create the lattice
            if n > 0:
                range_ = numpy.arange(-n / 2, n / 2)
                if dimensions == 2:
                    pos = list(itertools.product(range_, range_, [0]))
                else:
                    pos = list(itertools.product(range_, repeat=3))
                pos = numpy.array(pos) * a
                pos[:, 0] += a / 2
                pos[:, 1] += a / 2
                if dimensions == 3:
                    pos[:, 2] += a / 2

                # perturb the positions
                if r > 0:
                    shift = numpy.random.uniform(-r, r, size=(s.particles.N, 3))
                    if dimensions == 2:
                        shift[:, 2] = 0
                    pos += shift

                s.particles.position[:] = pos

        return s

    return make_snapshot


@pytest.fixture(scope='session')
def fcc_snapshot_factory(device):
    """Make a snapshot with particles in a fcc structure."""

    def make_snapshot(particle_types=['A'], a=1, n=7, r=0):
        """Make a snapshot with particles in a fcc structure.

        Args:
            particle_types: List of particle type names
            a: Lattice constant
            n: Number of unit cells along each box edge
            r: Amount to randomly perturb particles in x,y,z

        Place particles in a fcc structure. The box is cubic with a side length
        of ``n * a``. There will be ``4 * n**3`` particles in the snapshot.
        """
        s = Snapshot(device.communicator)

        if s.communicator.rank == 0:
            # make one unit cell
            s.configuration.box = [a, a, a, 0, 0, 0]
            s.particles.N = 4
            s.particles.types = particle_types
            s.particles.position[:] = [
                [0, 0, 0],
                [0, a / 2, a / 2],
                [a / 2, 0, a / 2],
                [a / 2, a / 2, 0],
            ]
            # and replicate it
            s.replicate(n, n, n)

        # perturb the positions
        if r > 0:
            shift = numpy.random.uniform(-r, r, size=(s.particles.N, 3))
            s.particles.position[:] += shift

        return s

    return make_snapshot


@pytest.fixture(autouse=True)
def skip_mpi(request):
    """Skip tests marked ``serial`` when running with MPI."""
    if request.node.get_closest_marker('serial'):
        if 'device' in request.fixturenames:
            if request.getfixturevalue('device').communicator.num_ranks > 1:
                pytest.skip('Test does not support MPI execution')
        else:
            raise ValueError('skip_mpi requires the *device* fixture')


@pytest.fixture(autouse=True)
def only_gpu(request):
    """Skip CPU tests marked ``gpu``."""
    if request.node.get_closest_marker('gpu'):
        if 'device' in request.fixturenames:
            if not isinstance(request.getfixturevalue('device'),
                              hoomd.device.GPU):
                pytest.skip('Test is run only on GPU(s).')
        else:
            raise ValueError('only_gpu requires the *device* fixture')


@pytest.fixture(autouse=True)
def only_cpu(request):
    """Skip GPU tests marked ``cpu``."""
    if request.node.get_closest_marker('cpu'):
        if 'device' in request.fixturenames:
            if not isinstance(request.getfixturevalue('device'),
                              hoomd.device.CPU):
                pytest.skip('Test is run only on CPU(s).')
        else:
            raise ValueError('only_cpu requires the *device* fixture')


@pytest.fixture(scope='function', autouse=True)
def numpy_random_seed():
    """Seed the numpy random number generator.

    Automatically reset the numpy random seed at the start of each function
    for reproducible tests.
    """
    numpy.random.seed(42)


def pytest_configure(config):
    """Add markers to pytest configuration."""
    config.addinivalue_line(
        "markers",
        "serial: Tests that will not execute with more than 1 MPI process")
    config.addinivalue_line("markers",
                            "gpu: Tests that should only run on the gpu.")
    config.addinivalue_line(
        "markers",
        "cupy_optional: tests that should pass with and without CuPy.")
    config.addinivalue_line("markers", "cpu: Tests that only run on the CPU.")
    config.addinivalue_line("markers", "gpu: Tests that only run on the GPU.")


def abort(exitstatus):
    """Call MPI_Abort when pytest tests fail."""
    # get a default mpi communicator
    communicator = hoomd.communicator.Communicator()
    # abort the deadlocked ranks
    hoomd._hoomd.abort_mpi(communicator.cpp_mpi_conf, exitstatus)


def pytest_sessionfinish(session, exitstatus):
    """Finalize pytest session.

    MPI tests may fail on one rank but not others. To prevent deadlocks in these
    situations, this code calls ``MPI_Abort`` when pytest is exiting with a
    non-zero exit code. **pytest** should be run with the ``-x`` option so that
    it exits on the first error.
    """
    if exitstatus != 0 and hoomd.version.mpi_enabled:
        atexit.register(abort, exitstatus)


def logging_check(cls, expected_namespace, expected_loggables):
    """Function for testing object logging specification.

    Args:
        cls (object): The loggable class to test for the correct logging
            specfication.
        expected_namespace (tuple[str]): A tuple of strings that indicate the
            expected namespace minus the class name.
        expected_loggables (dict[str, dict[str, Any]]): A dict with string keys
            representing the expected loggable quantities. If the value for a
            key is ``None`` then, only check for the existence of the loggable
            quantity. Otherwise, the inner `dict` should consist of some
            combination of the keys ``default`` and ``category`` indicating the
            expected value of each for the loggable.
    """
    # Check namespace
    assert all(log_quantity.namespace == expected_namespace + (cls.__name__,)
               for log_quantity in cls._export_dict.values())

    # Check specific loggables
    def check_loggable(cls, name, properties):
        assert name in cls._export_dict
        if properties is None:
            return None
        log_quantity = cls._export_dict[name]
        for name, prop in properties.items():
            assert getattr(log_quantity, name) == prop

    for name, properties in expected_loggables.items():
        check_loggable(cls, name, properties)


def _check_obj_attr_compatibility(a, b):
    """Check key compatibility."""
    a_keys = set(a.__dict__.keys())
    b_keys = set(b.__dict__.keys())
    different_keys = a_keys.symmetric_difference(b_keys) - a._skip_for_equality
    if different_keys == {}:
        return True
    # Check through reserved attributes with defaults to ensure that the
    # difference isn't an initialized default.
    compatible = True
    filtered_differences = set(different_keys)
    for key in different_keys:
        if key in a._reserved_default_attrs:
            default = a._reserved_default_attrs[key]()
            if getattr(a, key, default) == getattr(b, key, default):
                filtered_differences.remove(key)
                continue
        else:
            compatible = False

    if compatible:
        return True

    logger.debug(f"In equality check, incompatible attrs found "
                 f"{filtered_differences}.")
    return False


def equality_check(a, b):
    """Check equality between to instances of _HOOMDBaseObject."""

    def check_item(x, y, attr):
        if isinstance(x, hoomd.operation._HOOMDGetSetAttrBase):
            equal = equality_check(x, y)
        else:
            equal = numpy.all(x == y)
        if not equal:
            logger.debug(
                f"In equality check, attr '{attr}' not equal: {x} != {y}.")
            return False
        return True

    if not isinstance(a, hoomd.operation._HOOMDGetSetAttrBase):
        return a == b
    if type(a) != type(b):
        return False

    _check_obj_attr_compatibility(a, b)

    for attr in a.__dict__:
        if attr in a._skip_for_equality:
            continue

        if attr == "_param_dict":
            param_keys = a._param_dict.keys()
            b_param_keys = b._param_dict.keys()
            # Check key equality
            if param_keys != b_param_keys:
                logger.debug(
                    f"In equality check, incompatible param_dict keys: "
                    f"{param_keys}, {b_param_keys}")
                return False
            # Check item equality
            for key in param_keys:
                check_item(a._param_dict[key], b._param_dict[key], key)
            continue

        check_item(a.__dict__[attr], b.__dict__[attr], attr)
    return True


def pickling_check(instance):
    """Test that an instance can be pickled and unpickled."""
    pkled_instance = pickle.loads(pickle.dumps(instance))
    assert equality_check(instance, pkled_instance)


def operation_pickling_check(instance, sim):
    """Test that an operation can be pickled and unpickled."""
    pickling_check(instance)
    sim.operations += instance
    sim.run(0)
    pickling_check(instance)


class BlockAverage:
    """Block average method for estimating standard deviation of the mean.

    Args:
        data: List of values
    """

    def __init__(self, data):
        # round down to the nearest power of 2
        N = 2**int(math.log(len(data)) / math.log(2))
        if N != len(data):
            warnings.warn(
                "Ignoring some data. Data array should be a power of 2.")

        block_sizes = []
        block_mean = []
        block_variance = []

        # take means of blocks and the mean/variance of all blocks, growing
        # blocks by factors of 2
        block_size = 1
        while block_size <= N // 8:
            num_blocks = N // block_size
            block_data = numpy.zeros(num_blocks)

            for i in range(0, num_blocks):
                start = i * block_size
                end = start + block_size
                block_data[i] = numpy.mean(data[start:end])

            block_mean.append(numpy.mean(block_data))
            block_variance.append(numpy.var(block_data) / (num_blocks - 1))

            block_sizes.append(block_size)
            block_size *= 2

        self._block_mean = numpy.array(block_mean)
        self._block_variance = numpy.array(block_variance)
        self._block_sizes = numpy.array(block_sizes)
        self.data = numpy.array(data)

        # check for a plateau in the relative error before the last data point
        block_relative_error = numpy.sqrt(self._block_variance) / numpy.fabs(
            self._block_mean)
        relative_error_derivative = (numpy.diff(block_relative_error)
                                     / numpy.diff(self._block_sizes))
        if numpy.all(relative_error_derivative > 0):
            warnings.warn("Block averaging failed to plateau, run longer")

    def get_hierarchical_errors(self):
        """Get details on the hierarchical errors."""
        return (self._block_sizes, self._block_mean, self._block_variance)

    @property
    def standard_deviation(self):
        """float: The error estimate on the mean."""
        if numpy.all(self.data == self.data[0]):
            return 0

        return numpy.sqrt(numpy.max(self._block_variance))

    @property
    def mean(self):
        """float: The mean."""
        return self._block_mean[-1]

    @property
    def relative_error(self):
        """float: The relative error."""
        return self.standard_deviation / numpy.fabs(self.mean)

    def assert_close(self,
                     reference_mean,
                     reference_deviation,
                     z=6,
                     max_relative_error=0.02):
        """Assert that the distribution is constent with a given reference.

        Also assert that the relative error of the distribution is small.
        Otherwise, test runs with massive fluctuations would likely lead to
        passing tests.

        Args:
            reference_mean: Known good mean value
            reference_deviation: Standard deviation of the known good value
            z: Number of standard deviations
            max_relative_error: Maximum relative error to allow
        """
        sample_mean = self.mean
        sample_deviation = self.standard_deviation

        assert sample_deviation / sample_mean <= max_relative_error

        # compare if 0 is within the confidence interval around the difference
        # of the means
        deviation_diff = ((sample_deviation**2
                           + reference_deviation**2)**(1 / 2.))
        mean_diff = math.fabs(sample_mean - reference_mean)
        deviation_allowed = z * deviation_diff
        assert mean_diff <= deviation_allowed


class ListWriter(hoomd.custom.Action):
    """Log a single quantity to a list.

    On each triggered timestep, access the given attribute and add the value
    to `data`.

    Args:
        operation: Operation to log
        attribute: Name of the attribute to log

    Attributes:
        data (list): Saved data
    """

    def __init__(self, operation, attribute):
        self._operation = operation
        self._attribute = attribute
        self.data = []

    def act(self, timestep):
        """Add the attribute value to the list."""
        self.data.append(getattr(self._operation, self._attribute))


class BaseCollectionsTest:
    """Basic extensible test suite for collection classes."""

    @pytest.fixture
    def generate_plain_collection(self):
        """Return a function that generates plain collections for tests."""
        raise NotImplementedError

    def check_equivalent(self, a, b):
        """Assert whether two collections are equivalent for test purposes."""
        assert len(a) == len(b)
        for x, y in zip(a, b):
            assert self.is_equal(x, y)

    def is_equal(self, a, b):
        """Return whether two collection items are equal."""
        return a is b

    def final_check(self, test_collection):
        """Perform any final assert on the collection like object."""
        assert True

    _rng = numpy.random.default_rng(15656456)

    @property
    def rng(self):
        """Return a randon number generator."""
        return self._rng

    @pytest.fixture(autouse=True, params=(5, 10, 20))
    def n(self, request):
        """Fixture that controls tested collection sizes."""
        return request.param

    @pytest.fixture(scope="function")
    def plain_collection(self, n, generate_plain_collection):
        """Return a plain collection with specified items."""
        return generate_plain_collection(n)

    @pytest.fixture(scope="function")
    def empty_collection(self):
        """Return an empty test class collection."""
        raise NotImplementedError

    @pytest.fixture(scope="function")
    def populated_collection(self, empty_collection, plain_collection):
        """Return a test collection and the plain data the collection uses."""
        raise NotImplementedError

    def test_contains(self, populated_collection, generate_plain_collection):
        """Test __contains__."""
        test_collection, plain_collection = populated_collection
        for item in plain_collection:
            assert item in test_collection
        new_collection = generate_plain_collection(5)
        for item in new_collection:
            if item in plain_collection:
                assert item in test_collection
            else:
                assert item not in test_collection

    def test_len(self, populated_collection):
        """Test __len__."""
        test_collection, plain_collection = populated_collection
        assert len(test_collection) == len(plain_collection)

    def test_iter(self, populated_collection):
        """Test __iter__."""
        test_collection, plain_collection = populated_collection
        for t_item, p_item in zip(test_collection, plain_collection):
            assert self.is_equal(t_item, p_item)


class BaseSequenceTest(BaseCollectionsTest):
    """Basic extensible test suite for tuple-like classes."""

    def test_getitem(self, populated_collection):
        """Test __getitem__."""
        test_collection, plain_collection = populated_collection
        for i, p_item in enumerate(plain_collection):
            assert self.is_equal(test_collection[i], p_item)
        assert all(
            self.is_equal(t, p)
            for t, p in zip(test_collection[:], plain_collection))
        assert all(
            self.is_equal(t, p)
            for t, p in zip(test_collection[1:], plain_collection[1:]))
        with pytest.raises(IndexError):
            _ = test_collection[len(test_collection)]


class BaseTestList(BaseSequenceTest):
    """Basic extensible test suite for list-like classes."""

    @pytest.fixture(params=(3, 6, 11))
    def delete_index(self, request):
        """Determines the indices used for test_delitem."""
        return request.param

    def test_delitem(self, delete_index, populated_collection):
        """Test __delitem__."""
        test_list, plain_list = populated_collection
        if delete_index >= len(test_list):
            with pytest.raises(IndexError):
                del test_list[delete_index]
            return
        old_item = test_list[delete_index]
        del test_list[delete_index]
        del plain_list[delete_index]
        self.check_equivalent(test_list, plain_list)
        assert old_item not in test_list
        old_items = test_list[1:]
        del test_list[1:]
        assert len(test_list) == 1
        assert all(old_item not in test_list for old_item in old_items)
        self.final_check(test_list)

    def test_append(self, empty_collection, plain_collection):
        """Test append."""
        for i, item in enumerate(plain_collection):
            empty_collection.append(item)
            assert len(empty_collection) == i + 1
            assert self.is_equal(item, empty_collection[-1])
        self.check_equivalent(empty_collection, plain_collection)
        self.final_check(empty_collection)

    @pytest.fixture(params=(3, 6, 11))
    def insert_index(self, request):
        """Determines the indices used for test_insert."""
        return request.param

    def test_insert(self, insert_index, empty_collection, plain_collection):
        """Test insert."""
        check_collection = []
        empty_collection.extend(plain_collection[:-1])
        check_collection.extend(plain_collection[:-1])
        empty_collection.insert(insert_index, plain_collection[-1])
        check_collection.insert(insert_index, plain_collection[-1])
        assert len(empty_collection) == len(plain_collection)
        assert self.is_equal(
            empty_collection[min(len(empty_collection) - 1, insert_index)],
            plain_collection[-1],
        )
        self.check_equivalent(empty_collection, check_collection)
        self.final_check(empty_collection)

    def test_extend(self, empty_collection, plain_collection):
        """Test extend."""
        empty_collection.extend(plain_collection)
        self.check_equivalent(empty_collection, plain_collection)
        self.final_check(empty_collection)

    def test_clear(self, populated_collection):
        """Test clear."""
        test_list, plain_list = populated_collection
        test_list.clear()
        assert len(test_list) == 0
        self.final_check(test_list)

    @pytest.fixture(params=(3, 6, 11))
    def setitem_index(self, request):
        """Determines the indices used for test_setitem."""
        return request.param

    def test_setitem(self, setitem_index, populated_collection,
                     generate_plain_collection):
        """Test __setitem__."""
        test_list, plain_list = populated_collection
        item = generate_plain_collection(1)[0]
        if setitem_index >= len(test_list):
            with pytest.raises(IndexError):
                test_list[setitem_index] = item
            return

        test_list[setitem_index] = item
        assert self.is_equal(test_list[setitem_index], item)
        assert len(test_list) == len(plain_list)
        self.final_check(test_list)

    @pytest.fixture(params=(3, 6, 11))
    def pop_index(self, request):
        """Determines the indices used for test_pop."""
        return request.param

    def test_pop(self, pop_index, populated_collection):
        """Test pop."""
        test_list, plain_list = populated_collection
        if pop_index >= len(test_list):
            with pytest.raises(IndexError):
                test_list.pop(pop_index)
            return

        item = test_list.pop(pop_index)
        assert self.is_equal(item, plain_list[pop_index])
        plain_list.pop(pop_index)
        self.check_equivalent(test_list, plain_list)
        self.final_check(test_list)

    @pytest.fixture
    def remove_index(self, n):
        """Determines the indices used for test_remove."""
        return self.rng.integers(n)

    def test_remove(self, remove_index, populated_collection):
        """Test remove."""
        test_list, plain_list = populated_collection
        test_list.remove(plain_list[remove_index])
        assert plain_list[remove_index] not in test_list
        plain_list.remove(plain_list[remove_index])
        self.check_equivalent(test_list, plain_list)


class BaseMappingTest(BaseCollectionsTest):
    """Basic extensible test suite for mapping classes."""

    _allow_new_keys = True
    _deletion_error = None
    _has_default = False

    @pytest.fixture
    def populated_collection(self, empty_collection, plain_collection):
        """Return a test mapping and the plain data the collection uses."""
        empty_collection.update(plain_collection)
        return empty_collection, plain_collection

    def check_equivalent(self, a, b):
        """Assert whether two collections are equivalent for test purposes."""
        assert set(a) == set(b)
        for key in a:
            assert self.is_equal(a[key], b[key])

    def random_keys(self):
        """Generate random string keys."""
        alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        while True:
            length = self.rng.integers(10) + 1
            characters = []
            for _ in range(length):
                characters.append(alphabet[self.rng.choice(len(alphabet))])
            yield "".join(characters)

    def choose_random_key(self, mapping):
        """Pick a random existing key from mapping.

        Fails on an empty mapping.
        """
        return list(mapping)[self.rng.choice(len(mapping))]

    def test_iter(self, populated_collection):
        """Test __iter__."""
        test_mapping, plain_mapping = populated_collection
        assert set(test_mapping) == plain_mapping.keys()

    def test_contains(self, populated_collection):
        """Test __contains__."""
        test_collection, plain_collection = populated_collection
        for key in plain_collection:
            assert key in test_collection
        cnt = 0
        for key in self.random_keys():
            if key in plain_collection:
                assert key in test_collection
            else:
                assert key not in test_collection
                cnt += 1
                if cnt == 5:
                    break

    def test_getitem(self, populated_collection):
        """Test __getitem__."""
        test_mapping, plain_mapping = populated_collection
        for key, value in plain_mapping.items():
            assert self.is_equal(test_mapping[key], value)
        if self._has_default:
            for key in self.random_keys():
                if key not in test_mapping:
                    value = test_mapping[key]
                    assert self.is_equal(value, test_mapping.default)
                    return
        with pytest.raises(KeyError):
            for key in self.random_keys():
                if key not in test_mapping:
                    _ = test_mapping[key]
                    break

    def test_delitem(self, populated_collection):
        """Test __delitem__."""
        test_mapping, plain_mapping = populated_collection
        key = self.choose_random_key(test_mapping)
        old_item = test_mapping[key]
        if self._deletion_error is not None:
            with pytest.raises(self._deletion_error):
                del test_mapping[key]
            return

        del test_mapping[key]
        del plain_mapping[key]
        self.check_equivalent(test_mapping, plain_mapping)
        assert old_item not in test_mapping
        self.final_check(test_mapping)
        with pytest.raises(IndexError):
            for key in self.random_keys():
                if key not in self:
                    del test_mapping[key]

    def test_clear(self, populated_collection):
        """Test clear."""
        test_mapping, plain_mapping = populated_collection
        if self._deletion_error is not None:
            with pytest.raises(self._deletion_error):
                test_mapping.clear()
            return
        test_mapping.clear()
        assert len(test_mapping) == 0
        self.final_check(test_mapping)

    def setitem_key_value(self):
        """Determines the indices used for test_setitem."""
        raise NotImplementedError

    def test_setitem(self, setitem_key_value, populated_collection):
        """Test __setitem__."""
        test_mapping, plain_mapping = populated_collection
        key, value = setitem_key_value
        if not self._allow_new_keys and key not in test_mapping:
            with pytest.raises(KeyError):
                test_mapping[key] = value
            return
        test_mapping[key] = value
        assert self.is_equal(test_mapping[key], value)
        if key in plain_mapping:
            assert len(test_mapping) == len(plain_mapping)
        else:
            assert len(test_mapping) == len(plain_mapping) + 1
        self.final_check(test_mapping)

    def test_pop(self, populated_collection):
        """Test pop."""
        test_mapping, plain_mapping = populated_collection
        key = self.choose_random_key(test_mapping)
        if self._deletion_error is not None:
            with pytest.raises(self._deletion_error):
                item = test_mapping.pop(key)
            return
        item = test_mapping.pop(key)
        assert self.is_equal(item, plain_mapping[key])
        plain_mapping.pop(key)
        self.check_equivalent(test_mapping, plain_mapping)
        self.final_check(test_mapping)
        with pytest.raises(KeyError):
            for key in self.random_keys():
                if key not in test_mapping:
                    test_mapping.pop()
                    break
        test_mapping.pop(key, None)

    def test_keys(self, populated_collection):
        """Test keys."""
        test_mapping, plain_mapping = populated_collection
        assert set(test_mapping.keys()) == plain_mapping.keys()

    def test_values(self, populated_collection):
        """Test __iter__."""
        test_mapping, plain_mapping = populated_collection
        # We rely on keys() and values() using the same ordering
        for key, item in zip(test_mapping.keys(), test_mapping.values()):
            assert self.is_equal(item, plain_mapping[key])

    def test_items(self, populated_collection):
        """Test __iter__."""
        test_mapping, plain_mapping = populated_collection
        for key, value in test_mapping.items():
            assert self.is_equal(value, plain_mapping[key])

    def test_update(self, populated_collection, generate_plain_collection, n):
        """Test update."""
        test_mapping, plain_mapping = populated_collection
        new_mapping = generate_plain_collection(max(n - 1, 1))
        test_mapping.update(new_mapping)
        for key in new_mapping:
            assert key in test_mapping
            assert self.is_equal(test_mapping[key], new_mapping[key])
        for key in test_mapping:
            if key not in new_mapping:
                assert self.is_equal(test_mapping[key], plain_mapping[key])

    def setdefault_key_value(self):
        """Determines the indices used for test_setdefault."""
        raise NotImplementedError

    def test_setdefault(self, setdefault_key_value, populated_collection):
        """Test update."""
        test_mapping, plain_mapping = populated_collection
        key, value = setdefault_key_value
        if not self._allow_new_keys and key not in test_mapping:
            with pytest.raises(KeyError):
                test_mapping.setdefault(key, value)
            return
        test_mapping.setdefault(key, value)
        if key in plain_mapping:
            assert self.is_equal(test_mapping[key], plain_mapping[key])
        else:
            assert self.is_equal(test_mapping[key], value)
        if key in plain_mapping:
            assert len(test_mapping) == len(plain_mapping)
        else:
            assert len(test_mapping) == len(plain_mapping) + 1
        self.final_check(test_mapping)
        pass

    def test_popitem(self, populated_collection):
        """Test popitem."""
        test_mapping, plain_mapping = populated_collection
        if self._deletion_error is not None:
            with pytest.raises(self._deletion_error):
                test_mapping.popitem()
            return
        N = len(test_mapping)
        for length in range(N, -1, -1):
            key, item = test_mapping.popitem()
            assert key in plain_mapping
            assert key not in test_mapping
            assert len(test_mapping) == length

    def test_get(self, populated_collection):
        """Test get."""
        test_mapping, plain_mapping = populated_collection
        for key, value in plain_mapping.items():
            assert self.is_equal(test_mapping.get(key), value)
        for key in self.random_keys():
            if key not in test_mapping:
                assert test_mapping.get(key) is None
                assert test_mapping.get(key, 1) == 1
                break
