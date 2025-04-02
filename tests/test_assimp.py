import pytest
import sys
import os
from pathlib import Path
import struct

# Attempt import, skip if NumPy is not available for memoryview checks
try:
    import numpy as np
    NUMPY_AVAILABLE = True
except ImportError:
    NUMPY_AVAILABLE = False

# Import the compiled C extension
try:
    import assimp_py
except ImportError as e:
    pytest.fail(f"Failed to import the compiled assimp_py module: {e}", pytrace=False)

# --- Fixtures ---

@pytest.fixture(scope="module")
def test_files_dir(tmp_path_factory):
    """Creates a temporary directory for test files."""
    return tmp_path_factory.mktemp("assimp_test_files")

@pytest.fixture(scope="module")
def valid_obj_filename():
    """Filename for the valid OBJ."""
    return "test_cube.obj"

@pytest.fixture(scope="module")
def valid_mtl_filename():
    """Filename for the valid MTL."""
    return "cube.mtl"

@pytest.fixture(scope="module")
def valid_obj_file(test_files_dir, valid_obj_filename, valid_mtl_filename):
    """Creates a sample valid OBJ file and its MTL file."""
    obj_path = test_files_dir / valid_obj_filename
    mtl_path = test_files_dir / valid_mtl_filename

    obj_content = f"""
# Simple Quad OBJ for testing assimp_py

mtllib {valid_mtl_filename}

v -0.5 -0.5 0.0
v  0.5 -0.5 0.0
v  0.5  0.5 0.0
v -0.5  0.5 0.0

vt 0.0 0.0
vt 1.0 0.0
vt 1.0 1.0
vt 0.0 1.0

vn 0.0 0.0 1.0
vn 0.0 0.0 1.0
vn 0.0 0.0 1.0
vn 0.0 0.0 1.0

usemtl TestMaterial
# Quad face (v/vt/vn) - will be triangulated
f 1/1/1 2/2/2 3/3/3 4/4/4
"""

    mtl_content = """
# Material definition for cube.obj

newmtl TestMaterial
Ka 0.1 0.1 0.1
Kd 0.8 0.7 0.6
Ks 0.9 0.9 0.9
Ns 32
d 1.0
illum 2
map_Kd texture_diffuse.png
map_Ks texture_specular.png
map_bump bumpmap.png
"""
    obj_path.write_text(obj_content)
    mtl_path.write_text(mtl_content)
    # Create dummy texture files so Assimp finds them
    (test_files_dir / "texture_diffuse.png").touch()
    (test_files_dir / "texture_specular.png").touch()
    (test_files_dir / "bumpmap.png").touch()

    yield obj_path # Provide the path to the tests

    # Teardown (optional, tmp_path_factory handles it)
    # obj_path.unlink()
    # mtl_path.unlink()
    # ... unlink dummy textures

@pytest.fixture(scope="module")
def invalid_file(test_files_dir):
    """Creates an empty/invalid file."""
    path = test_files_dir / "invalid.txt"
    path.write_text("This is not a valid 3D model.")
    yield path

# Default flags: Triangulate, generate smooth normals if missing, calc tangents
DEFAULT_FLAGS = (
    assimp_py.Process_Triangulate
    | assimp_py.Process_JoinIdenticalVertices
    | assimp_py.Process_GenSmoothNormals # Ensures normals exist
    | assimp_py.Process_CalcTangentSpace # Ensures tangents exist
    # | assimp_py.Process_FlipUVs # Add if needed by your textures/renderer
)

@pytest.fixture(scope="module")
def loaded_scene(valid_obj_file):
    """Loads the valid OBJ file with default flags."""
    try:
        scene = assimp_py.import_file(str(valid_obj_file), DEFAULT_FLAGS)
        return scene
    except Exception as e:
        pytest.fail(f"Fixture failed: Could not load valid file '{valid_obj_file}' with default flags. Error: {e}")

# --- Test Classes ---

class TestModuleConstants:
    def test_postprocess_flags_exist(self):
        """Check if common post-processing flags are defined."""
        assert hasattr(assimp_py, "Process_Triangulate")
        assert hasattr(assimp_py, "Process_JoinIdenticalVertices")
        assert hasattr(assimp_py, "Process_GenSmoothNormals")
        assert hasattr(assimp_py, "Process_CalcTangentSpace")
        assert hasattr(assimp_py, "Process_FlipUVs")
        assert isinstance(assimp_py.Process_Triangulate, int)

    def test_texture_types_exist(self):
        """Check if common texture types are defined."""
        assert hasattr(assimp_py, "TextureType_DIFFUSE")
        assert hasattr(assimp_py, "TextureType_SPECULAR")
        assert hasattr(assimp_py, "TextureType_NORMALS") # Note: OBJ bump often maps here
        assert hasattr(assimp_py, "TextureType_UNKNOWN")
        assert isinstance(assimp_py.TextureType_DIFFUSE, int)


class TestImportFile:
    def test_load_valid_file(self, loaded_scene):
        """Test loading a valid file returns a Scene object."""
        assert isinstance(loaded_scene, assimp_py.Scene)

    def test_file_not_found(self):
        """Test loading a non-existent file raises FileNotFoundError."""
        with pytest.raises(FileNotFoundError):
            assimp_py.import_file("non_existent_file.obj", DEFAULT_FLAGS)

    def test_invalid_file_format(self, invalid_file):
        """Test loading an invalid file raises RuntimeError."""
        with pytest.raises(RuntimeError) as excinfo:
            assimp_py.import_file(str(invalid_file), DEFAULT_FLAGS)
        # Check if Assimp's error message is included (optional but good)
        assert "Assimp error" in str(excinfo.value) or "Failed to open file" in str(excinfo.value)

    def test_invalid_arguments_type(self):
        """Test passing incorrect argument types raises TypeError."""
        with pytest.raises(TypeError):
            assimp_py.import_file(123, DEFAULT_FLAGS) # filename not a string
        with pytest.raises(TypeError):
            assimp_py.import_file("dummy.obj", "not_an_int") # flags not an int

    def test_invalid_arguments_count(self):
        """Test passing incorrect number of arguments raises TypeError."""
        with pytest.raises(TypeError):
            assimp_py.import_file("dummy.obj") # Missing flags
        with pytest.raises(TypeError):
            assimp_py.import_file("dummy.obj", DEFAULT_FLAGS, "extra_arg") # Too many args


class TestSceneObject:
    def test_scene_attributes(self, loaded_scene):
        """Test Scene object attributes existence and basic types."""
        assert hasattr(loaded_scene, "meshes")
        assert hasattr(loaded_scene, "materials")
        assert hasattr(loaded_scene, "num_meshes")
        assert hasattr(loaded_scene, "num_materials")

        assert isinstance(loaded_scene.meshes, list)
        assert isinstance(loaded_scene.materials, list)
        assert isinstance(loaded_scene.num_meshes, int)
        assert isinstance(loaded_scene.num_materials, int)

    def test_scene_counts(self, loaded_scene):
        """Test Scene mesh and material counts match list lengths."""
        assert loaded_scene.num_meshes == len(loaded_scene.meshes)
        assert loaded_scene.num_materials == len(loaded_scene.materials)
        # For our specific test file:
        assert loaded_scene.num_meshes == 1
        assert loaded_scene.num_materials == 2  # Expect DefaultMaterial + TestMaterial (Assimp makes a default material)

    def test_scene_mesh_type(self, loaded_scene):
        """Test Scene.meshes contains Mesh objects."""
        assert len(loaded_scene.meshes) > 0 # Ensure meshes exist for the test
        for mesh in loaded_scene.meshes:
            assert isinstance(mesh, assimp_py.Mesh)

    def test_scene_material_type(self, loaded_scene):
        """Test Scene.materials contains dictionaries."""
        assert len(loaded_scene.materials) > 0 # Ensure materials exist
        for material in loaded_scene.materials:
            assert isinstance(material, dict)


@pytest.mark.skipif(not NUMPY_AVAILABLE, reason="NumPy not found, skipping memoryview tests")
class TestMeshObject:
    # Helper to check memoryview properties
    def _check_memoryview(self, mv, expected_format, expected_itemsize, expected_ndim=1):
        assert isinstance(mv, memoryview), f"Expected memoryview, got {type(mv)}"
        assert mv.format == expected_format
        assert mv.itemsize == expected_itemsize
        assert mv.ndim == expected_ndim
        assert mv.readonly, "Memoryview should be read-only"

    # Helper to check numpy array derived from memoryview
    def _check_numpy_array(self, mv, expected_dtype, expected_shape_part):
         arr = np.asarray(mv)
         assert arr.dtype == expected_dtype
         # Shape check: Allow flexibility, check first dimension or total size
         assert arr.shape[0] == expected_shape_part if mv.ndim == 1 else True # Basic check
         return arr # Return array for further checks if needed

    @pytest.fixture(scope="class")
    def mesh(self, loaded_scene):
        """Provides the first mesh from the loaded scene."""
        if not loaded_scene.meshes:
            pytest.fail("No meshes found in loaded scene for MeshObject tests.")
        return loaded_scene.meshes[0]

    def test_mesh_basic_attributes(self, mesh):
        """Test Mesh basic attributes."""
        assert hasattr(mesh, "name")
        assert hasattr(mesh, "material_index")
        assert hasattr(mesh, "num_vertices")
        assert hasattr(mesh, "num_faces")
        assert hasattr(mesh, "num_indices")

        # Name might be empty if not set in file
        assert isinstance(mesh.name, str)
        assert isinstance(mesh.material_index, int)
        assert isinstance(mesh.num_vertices, int)
        assert isinstance(mesh.num_faces, int)
        assert isinstance(mesh.num_indices, int)

        # For our specific triangulated quad file
        assert mesh.num_vertices == 4 # Joined vertices
        assert mesh.num_faces == 2 # Post-triangulation
        assert mesh.num_indices == 6 # 2 triangles * 3 indices

    def test_mesh_indices(self, mesh):
        """Test Mesh indices memoryview."""
        assert hasattr(mesh, "indices")
        if mesh.num_indices > 0:
            self._check_memoryview(mesh.indices, 'I', 4) # 'I' = unsigned int
            arr = self._check_numpy_array(mesh.indices, np.uint32, mesh.num_indices)
            assert arr.shape == (mesh.num_indices,)
            assert arr.size == mesh.num_indices
            # Optional: Check specific indices if known
            # Example for the triangulated quad (0,1,2) and (0,2,3)
            expected_indices = np.array([0, 1, 2, 0, 2, 3], dtype=np.uint32)
            np.testing.assert_array_equal(arr, expected_indices)

        else:
            assert mesh.indices is None

    def test_mesh_vertices(self, mesh):
        """Test Mesh vertices memoryview."""
        assert hasattr(mesh, "vertices")
        if mesh.num_vertices > 0:
            self._check_memoryview(mesh.vertices, 'f', 4) # 'f' = float
            arr = self._check_numpy_array(mesh.vertices, np.float32, mesh.num_vertices * 3)
            assert arr.shape == (mesh.num_vertices * 3,) # Flat array
            # Reshape for easier checking
            vertices_reshaped = arr.reshape((mesh.num_vertices, 3))
            assert vertices_reshaped.shape == (mesh.num_vertices, 3)
            # Optional: Check values
            expected_vertices = np.array([
                [-0.5, -0.5, 0.0],
                [ 0.5, -0.5, 0.0],
                [ 0.5,  0.5, 0.0],
                [-0.5,  0.5, 0.0]
            ], dtype=np.float32)
            np.testing.assert_allclose(vertices_reshaped, expected_vertices, atol=1e-6)
        else:
             assert mesh.vertices is None # Should not happen for valid mesh

    def test_mesh_normals(self, mesh):
        """Test Mesh normals memoryview (should exist due to flags)."""
        assert hasattr(mesh, "normals")
        assert mesh.normals is not None, "Normals should exist due to Process_GenSmoothNormals flag"
        if mesh.num_vertices > 0:
            self._check_memoryview(mesh.normals, 'f', 4)
            arr = self._check_numpy_array(mesh.normals, np.float32, mesh.num_vertices * 3)
            assert arr.shape == (mesh.num_vertices * 3,)
            normals_reshaped = arr.reshape((mesh.num_vertices, 3))
            assert normals_reshaped.shape == (mesh.num_vertices, 3)
            # Check normals point roughly in +Z
            assert np.all(normals_reshaped[:, 2] > 0.9) # All Z components close to 1
        else:
             pytest.fail("Mesh has no vertices to test normals")

    def test_mesh_tangents_bitangents(self, mesh):
        """Test Mesh tangents/bitangents (should exist due to flags)."""
        assert hasattr(mesh, "tangents")
        assert hasattr(mesh, "bitangents")

        assert mesh.tangents is not None, "Tangents should exist due to Process_CalcTangentSpace flag"
        assert mesh.bitangents is not None, "Bitangents should exist due to Process_CalcTangentSpace flag"

        if mesh.num_vertices > 0:
            self._check_memoryview(mesh.tangents, 'f', 4)
            arr_t = self._check_numpy_array(mesh.tangents, np.float32, mesh.num_vertices * 3)
            assert arr_t.shape == (mesh.num_vertices * 3,)
            tangents_reshaped = arr_t.reshape((mesh.num_vertices, 3))
            assert tangents_reshaped.shape == (mesh.num_vertices, 3)

            self._check_memoryview(mesh.bitangents, 'f', 4)
            arr_b = self._check_numpy_array(mesh.bitangents, np.float32, mesh.num_vertices * 3)
            assert arr_b.shape == (mesh.num_vertices * 3,)
            bitangents_reshaped = arr_b.reshape((mesh.num_vertices, 3))
            assert bitangents_reshaped.shape == (mesh.num_vertices, 3)

            # Optional: Check orthogonality (dot product should be near zero)
            # Requires normals array too
            normals_reshaped = np.asarray(mesh.normals).reshape((mesh.num_vertices, 3))
            dots_tn = np.einsum('ij,ij->i', tangents_reshaped, normals_reshaped)
            dots_bn = np.einsum('ij,ij->i', bitangents_reshaped, normals_reshaped)
            dots_tb = np.einsum('ij,ij->i', tangents_reshaped, bitangents_reshaped)
            np.testing.assert_allclose(dots_tn, 0, atol=1e-5)
            np.testing.assert_allclose(dots_bn, 0, atol=1e-5)
            np.testing.assert_allclose(dots_tb, 0, atol=1e-5)
        else:
             pytest.fail("Mesh has no vertices to test tangents/bitangents")

    def test_mesh_texcoords(self, mesh):
        """Test Mesh texture coordinates."""
        assert hasattr(mesh, "texcoords")
        assert hasattr(mesh, "num_uv_components")

        assert isinstance(mesh.texcoords, list)
        assert isinstance(mesh.num_uv_components, list)

        # Our file has one UV set
        assert len(mesh.texcoords) == 1
        assert len(mesh.num_uv_components) == 1
        assert mesh.num_uv_components[0] == 2 # u, v

        if mesh.num_vertices > 0:
            uv_set = mesh.texcoords[0]
            num_components = mesh.num_uv_components[0]
            self._check_memoryview(uv_set, 'f', 4)
            arr = self._check_numpy_array(uv_set, np.float32, mesh.num_vertices * num_components)
            assert arr.shape == (mesh.num_vertices * num_components,)
            uv_reshaped = arr.reshape((mesh.num_vertices, num_components))
            assert uv_reshaped.shape == (mesh.num_vertices, num_components)

            # Check values
            expected_uvs = np.array([
                [0.0, 0.0],
                [1.0, 0.0],
                [1.0, 1.0],
                [0.0, 1.0]
            ], dtype=np.float32)
            np.testing.assert_allclose(uv_reshaped, expected_uvs, atol=1e-6)
        else:
            pytest.fail("Mesh has no vertices to test texcoords")

    def test_mesh_colors(self, mesh):
        """Test Mesh vertex colors (should be None for our file)."""
        assert hasattr(mesh, "colors")
        # Our specific OBJ file doesn't have vertex colors
        assert mesh.colors is None or mesh.colors == [], "Expected no vertex color sets for this file"


@pytest.mark.skipif(not NUMPY_AVAILABLE, reason="NumPy not found, skipping tests requiring it")
class TestMaterials:

    @pytest.fixture(scope="class")
    def material(self, loaded_scene):
        """Provides the 'TestMaterial' material from the loaded scene."""
        if not loaded_scene.materials:
            pytest.fail("No materials found in loaded scene for Material tests.")
        # Find the correct material by name
        target_name = "TestMaterial"
        found_material = None
        for mat in loaded_scene.materials:
            # Use get() to avoid KeyError if name key is missing (though it shouldn't be)
            if mat.get("?mat.name") == target_name:
                found_material = mat
                break
        if found_material is None:
            pytest.fail(f"Material named '{target_name}' not found in loaded scene.")
        return found_material

    def test_material_is_dict(self, material):
        """Check if the material object is a dictionary."""
        assert isinstance(material, dict)

    def test_material_name(self, material):
        """Check for material name property."""
        assert "?mat.name" in material # Assimp key for name
        assert isinstance(material["?mat.name"], str)
        assert material["?mat.name"] == "TestMaterial"

    def test_material_colors(self, material):
        """Check for common color properties."""
        # Keys are defined by Assimp (ai_matkey_defines.h)
        diffuse_key = "$clr.diffuse"
        specular_key = "$clr.specular"
        ambient_key = "$clr.ambient"

        assert diffuse_key in material
        assert specular_key in material
        assert ambient_key in material

        # Assimp usually returns colors as list/tuple of floats [r, g, b] or [r, g, b, a]
        # Our C code wraps simple float[4] as tuple(f,f,f,f)
        assert isinstance(material[diffuse_key], tuple) or isinstance(material[diffuse_key], list)
        assert len(material[diffuse_key]) >= 3 # Allow for RGB or RGBA
        assert all(isinstance(c, float) for c in material[diffuse_key])
        # Check actual values from MTL
        np.testing.assert_allclose(material[diffuse_key][:3], [0.8, 0.7, 0.6], atol=1e-5)
        np.testing.assert_allclose(material[specular_key][:3], [0.9, 0.9, 0.9], atol=1e-5)
        np.testing.assert_allclose(material[ambient_key][:3], [0.1, 0.1, 0.1], atol=1e-5)

    def test_material_shininess(self, material):
        """Check for shininess property."""
        shininess_key = "$mat.shininess"
        assert shininess_key in material
        assert isinstance(material[shininess_key], float) or isinstance(material[shininess_key], int)
        assert material[shininess_key] == 32.0

    def test_material_textures(self, material):
        """Check for the TEXTURES dictionary and specific textures."""
        assert "TEXTURES" in material
        textures_dict = material["TEXTURES"]
        assert isinstance(textures_dict, dict)

        # Check for diffuse texture (Type 1)
        diffuse_type = assimp_py.TextureType_DIFFUSE
        assert diffuse_type in textures_dict
        assert isinstance(textures_dict[diffuse_type], list)
        assert len(textures_dict[diffuse_type]) == 1
        assert isinstance(textures_dict[diffuse_type][0], str)
        assert textures_dict[diffuse_type][0] == "texture_diffuse.png"

        # Check for specular texture (Type 2)
        specular_type = assimp_py.TextureType_SPECULAR
        assert specular_type in textures_dict
        assert isinstance(textures_dict[specular_type], list)
        assert len(textures_dict[specular_type]) == 1
        assert isinstance(textures_dict[specular_type][0], str)
        assert textures_dict[specular_type][0] == "texture_specular.png"

        # Check for bump map (Assimp often maps OBJ 'map_bump' to NORMALS or HEIGHT)
        # Let's check both common types where it might end up
        bump_type_norm = assimp_py.TextureType_NORMALS # Type 6
        bump_type_height = assimp_py.TextureType_HEIGHT # Type 5

        found_bump = False
        if bump_type_norm in textures_dict:
             assert isinstance(textures_dict[bump_type_norm], list)
             assert len(textures_dict[bump_type_norm]) == 1
             assert textures_dict[bump_type_norm][0] == "bumpmap.png"
             found_bump = True
        elif bump_type_height in textures_dict:
             assert isinstance(textures_dict[bump_type_height], list)
             assert len(textures_dict[bump_type_height]) == 1
             assert textures_dict[bump_type_height][0] == "bumpmap.png"
             found_bump = True

        assert found_bump, "Bump map 'bumpmap.png' not found in expected texture types (NORMALS or HEIGHT)"


# --- Standalone Function Tests (if any, otherwise covered by classes) ---

# Example: Test loading without triangulation (if you had a non-triangulated file)
# def test_load_no_triangulate_error(quad_obj_file): # Needs a file with actual quads
#     flags = DEFAULT_FLAGS & ~assimp_py.Process_Triangulate # Remove triangulate
#     with pytest.raises(ValueError, match="assumes triangulated faces"): # Match error from C code
#          assimp_py.import_file(str(quad_obj_file), flags)