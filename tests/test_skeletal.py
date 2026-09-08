import pytest

try:
    import numpy as np
    NUMPY_AVAILABLE = True
except ImportError:
    NUMPY_AVAILABLE = False

try:
    import assimp_py
except ImportError as e:
    pytest.fail(f"Failed to import the compiled assimp_py module: {e}", pytrace=False)

from pathlib import Path

FOX_MODEL = Path(__file__).parent.joinpath("models/fox/Fox.glb")
PLANET_MODEL = Path(__file__).parent.joinpath("models/planet/planet.obj")

# Flags used for the skeletal test scene
SKELETAL_FLAGS = (
    assimp_py.Process_Triangulate
    | assimp_py.Process_LimitBoneWeights
    | assimp_py.Process_PopulateArmatureData
)

# Values pinned from tests/models/fox/Fox.glb (assimp 6.0.5)
FOX_NUM_MESHES = 1
FOX_NUM_VERTICES = 1728
FOX_NUM_BONES = 24
FOX_TOTAL_WEIGHTS = 2731
FOX_FIRST_BONE = "_rootJoint"
FOX_ARMATURE = "root"
FOX_ANIM_NAMES = {"Survey", "Walk", "Run"}
FOX_ANIM_CHANNELS = 20
FOX_HIP_CHANNEL = "b_Hip_01"
FOX_SURVEY_DURATION = 3416.666748046875
FOX_WALK_DURATION = 708.3333129882812
FOX_RUN_DURATION = 1158.333251953125
FOX_TICKS_PER_SECOND = 1000.0
FOX_SURVEY_HIP_KEYS = 83


# --- Fixtures ---

@pytest.fixture(scope="module")
def fox_scene():
    """Fox.glb loaded with skeletal post-processing flags."""
    return assimp_py.import_file(str(FOX_MODEL), SKELETAL_FLAGS)


@pytest.fixture(scope="module")
def fox_scene_plain():
    """Fox.glb loaded without Process_PopulateArmatureData."""
    return assimp_py.import_file(str(FOX_MODEL), assimp_py.Process_Triangulate)


@pytest.fixture(scope="module")
def fox_mesh(fox_scene):
    assert fox_scene.num_meshes == FOX_NUM_MESHES
    return fox_scene.meshes[0]


def _collect_node_names(node, names=None):
    """Recursively collect node names from the hierarchy."""
    if names is None:
        names = set()
    names.add(node.name)
    for child in node.children:
        _collect_node_names(child, names)
    return names


# --- Tests ---

class TestBones:
    def test_mesh_bone_counts(self, fox_mesh):
        """Mesh exposes bone list and count consistently."""
        assert isinstance(fox_mesh.bones, list)
        assert fox_mesh.num_bones == len(fox_mesh.bones)
        assert fox_mesh.num_bones == FOX_NUM_BONES
        for bone in fox_mesh.bones:
            assert isinstance(bone, assimp_py.Bone)

    def test_bone_basic_attributes(self, fox_mesh):
        """Bone names are strings and offset matrices are 4x4 float tuples."""
        names = []
        for bone in fox_mesh.bones:
            assert isinstance(bone.name, str) and bone.name
            names.append(bone.name)

            matrix = bone.offset_matrix
            assert isinstance(matrix, tuple) and len(matrix) == 4
            for row in matrix:
                assert isinstance(row, tuple) and len(row) == 4
                for value in row:
                    assert isinstance(value, float)

        assert fox_mesh.bones[0].name == FOX_FIRST_BONE

    def test_armature_and_node_names(self, fox_mesh, fox_scene):
        """With Process_PopulateArmatureData, armature/node names are populated."""
        assert fox_mesh.bones[0].armature_name == FOX_ARMATURE
        assert fox_mesh.bones[0].node_name == FOX_FIRST_BONE

        node_names = _collect_node_names(fox_scene.root_node)
        for bone in fox_mesh.bones:
            assert isinstance(bone.armature_name, str)
            assert isinstance(bone.node_name, str)
            # The node matching this bone must exist in the hierarchy
            assert bone.node_name in node_names

    def test_armature_names_absent_without_flag(self, fox_scene_plain):
        """Without Process_PopulateArmatureData, armature/node names are None."""
        bones = fox_scene_plain.meshes[0].bones
        assert len(bones) == FOX_NUM_BONES
        for bone in bones:
            assert bone.armature_name is None
            assert bone.node_name is None

    def test_bone_weights_empty_for_static_mesh(self):
        """A static (non-skinned) mesh exposes an empty bone list."""
        scene = assimp_py.import_file(str(PLANET_MODEL), assimp_py.Process_Triangulate)
        for mesh in scene.meshes:
            assert mesh.bones == []
            assert mesh.num_bones == 0


@pytest.mark.skipif(not NUMPY_AVAILABLE, reason="NumPy not found, skipping memoryview tests")
class TestBoneWeights:
    def test_weight_memoryviews(self, fox_mesh):
        """Weights and vertex ids are parallel read-only memoryviews."""
        total = 0
        for bone in fox_mesh.bones:
            assert isinstance(bone.weights, memoryview)
            assert isinstance(bone.weight_vertex_ids, memoryview)
            assert bone.weights.format == "f" and bone.weights.itemsize == 4
            assert bone.weight_vertex_ids.format == "I" and bone.weight_vertex_ids.itemsize == 4
            assert bone.weights.readonly and bone.weight_vertex_ids.readonly

            ids = np.frombuffer(bone.weight_vertex_ids, dtype=np.uint32)
            weights = np.frombuffer(bone.weights, dtype=np.float32)
            assert ids.shape == weights.shape == (bone.num_weights,)
            assert ids.max() < fox_mesh.num_vertices
            total += bone.num_weights

        assert total == FOX_TOTAL_WEIGHTS

    def test_per_vertex_weight_sums(self, fox_mesh):
        """With Process_LimitBoneWeights, weights at each vertex sum to 1."""
        sums = np.zeros(fox_mesh.num_vertices, dtype=np.float64)
        for bone in fox_mesh.bones:
            ids = np.frombuffer(bone.weight_vertex_ids, dtype=np.uint32)
            weights = np.frombuffer(bone.weights, dtype=np.float32).astype(np.float64)
            np.add.at(sums, ids, weights)
        np.testing.assert_allclose(sums, 1.0, atol=1e-5)


class TestAnimations:
    def test_scene_animation_counts(self, fox_scene):
        """Scene exposes animation list and count consistently."""
        assert isinstance(fox_scene.animations, list)
        assert fox_scene.num_animations == len(fox_scene.animations)
        assert fox_scene.num_animations == 3
        for anim in fox_scene.animations:
            assert isinstance(anim, assimp_py.Animation)

    def test_animation_names_durations(self, fox_scene):
        """The three Fox animation cycles with pinned durations."""
        by_name = {anim.name: anim for anim in fox_scene.animations}
        assert set(by_name) == FOX_ANIM_NAMES

        for anim in fox_scene.animations:
            assert isinstance(anim.name, str)
            assert isinstance(anim.duration, float)
            assert isinstance(anim.ticks_per_second, float)

        assert by_name["Survey"].duration == pytest.approx(FOX_SURVEY_DURATION, rel=1e-6)
        assert by_name["Walk"].duration == pytest.approx(FOX_WALK_DURATION, rel=1e-6)
        assert by_name["Run"].duration == pytest.approx(FOX_RUN_DURATION, rel=1e-6)

        for anim in fox_scene.animations:
            assert anim.ticks_per_second == FOX_TICKS_PER_SECOND

    def test_animation_channels(self, fox_scene):
        """Each animation has 20 NodeAnim channels with string node names."""
        for anim in fox_scene.animations:
            assert isinstance(anim.channels, list)
            assert anim.num_channels == len(anim.channels) == FOX_ANIM_CHANNELS
            for channel in anim.channels:
                assert isinstance(channel, assimp_py.NodeAnim)
                assert isinstance(channel.node_name, str) and channel.node_name

        survey = {a.name: a for a in fox_scene.animations}["Survey"]
        assert survey.channels[0].node_name == FOX_HIP_CHANNEL

    def test_channel_key_counts(self, fox_scene):
        """Key counts pinned from the Fox file."""
        by_name = {anim.name: anim for anim in fox_scene.animations}

        survey_hip = by_name["Survey"].channels[0]
        assert survey_hip.num_position_keys == FOX_SURVEY_HIP_KEYS
        assert survey_hip.num_rotation_keys == FOX_SURVEY_HIP_KEYS
        assert survey_hip.num_scaling_keys == 1

        walk_hip = by_name["Walk"].channels[0]
        assert walk_hip.num_position_keys == 18
        assert walk_hip.num_rotation_keys == 18

        run_hip = by_name["Run"].channels[0]
        assert run_hip.num_position_keys == 25
        assert run_hip.num_rotation_keys == 25

    def test_channel_states(self, fox_scene):
        """Pre/post behaviour states are exposed as ints (DEFAULT = 0 here)."""
        for anim in fox_scene.animations:
            for channel in anim.channels:
                assert isinstance(channel.pre_state, int)
                assert isinstance(channel.post_state, int)
                assert channel.pre_state == assimp_py.AnimBehaviour_DEFAULT
                assert channel.post_state == assimp_py.AnimBehaviour_DEFAULT

    def test_animations_empty_for_static_mesh(self):
        """A scene without animations exposes an empty list."""
        scene = assimp_py.import_file(str(PLANET_MODEL), assimp_py.Process_Triangulate)
        assert scene.animations == []
        assert scene.num_animations == 0


@pytest.mark.skipif(not NUMPY_AVAILABLE, reason="NumPy not found, skipping memoryview tests")
class TestAnimationKeys:
    def test_key_memoryviews(self, fox_scene):
        """Key tracks are read-only memoryviews with documented formats."""
        by_name = {anim.name: anim for anim in fox_scene.animations}
        hip = by_name["Survey"].channels[0]

        times = hip.position_key_times
        values = hip.position_key_values
        assert times.format == "d" and times.itemsize == 8
        assert values.format == "f" and values.itemsize == 4
        assert times.readonly and values.readonly

        t_arr = np.frombuffer(times, dtype=np.float64)
        v_arr = np.frombuffer(values, dtype=np.float32)
        assert t_arr.shape == (hip.num_position_keys,)
        assert v_arr.shape == (hip.num_position_keys * 3,)

        rot_t = np.frombuffer(hip.rotation_key_times, dtype=np.float64)
        rot_v = np.frombuffer(hip.rotation_key_values, dtype=np.float32)
        assert rot_t.shape == (hip.num_rotation_keys,)
        assert rot_v.shape == (hip.num_rotation_keys * 4,)

        scl_t = np.frombuffer(hip.scaling_key_times, dtype=np.float64)
        scl_v = np.frombuffer(hip.scaling_key_values, dtype=np.float32)
        assert scl_t.shape == (hip.num_scaling_keys,)
        assert scl_v.shape == (hip.num_scaling_keys * 3,)

    def test_key_times_sorted(self, fox_scene):
        """Key times are in chronological order and span the animation."""
        for anim in fox_scene.animations:
            for channel in anim.channels:
                for track in (channel.position_key_times, channel.rotation_key_times):
                    t = np.frombuffer(track, dtype=np.float64)
                    assert np.all(np.diff(t) > 0) or t.size == 1
                    assert t[0] == pytest.approx(0.0)
                    assert t[-1] <= anim.duration + 1e-6

    def test_rotation_quaternions_normalized(self, fox_scene):
        """Rotation key quaternions are (approximately) unit length."""
        for anim in fox_scene.animations:
            for channel in anim.channels:
                quats = np.frombuffer(channel.rotation_key_values, dtype=np.float32)
                quats = quats.reshape(-1, 4)
                norms = np.linalg.norm(quats.astype(np.float64), axis=1)
                np.testing.assert_allclose(norms, 1.0, atol=1e-5)

    def test_pinned_key_values(self, fox_scene):
        """First position/rotation key values pinned from the Fox file."""
        by_name = {anim.name: anim for anim in fox_scene.animations}
        hip = by_name["Survey"].channels[0]

        first_pos = np.frombuffer(hip.position_key_values, dtype=np.float32)[:3]
        np.testing.assert_allclose(
            first_pos,
            [1.2987384252483025e-06, 24.551631927490234, 41.05862045288086],
            atol=1e-6,
        )

        first_rot = np.frombuffer(hip.rotation_key_values, dtype=np.float32)[:4]
        np.testing.assert_allclose(
            first_rot,
            [0.12769122421741486, -0.6954819560050964, -0.12769056856632233, 0.6954817771911621],
            atol=1e-6,
        )
