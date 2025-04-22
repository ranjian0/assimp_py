import pytest
from pathlib import Path

try:
    import assimp_py
except ImportError as e:
    pytest.fail(f"Failed to import the compiled assimp_py module: {e}", pytrace=False)


@pytest.fixture(scope="module")
def cyborg():
    model = Path(__file__).parent.joinpath("models/cyborg/cyborg.obj")
    post_flags = (
        assimp_py.Process_GenNormals | assimp_py.Process_CalcTangentSpace
    )
    scn = assimp_py.import_file(str(model.absolute()), post_flags)
    yield scn


@pytest.fixture(scope="module")
def planet():
    model = Path(__file__).parent.joinpath("models/planet/planet.obj")
    post_flags = (
        assimp_py.Process_GenNormals | assimp_py.Process_CalcTangentSpace
    )
    scn = assimp_py.import_file(str(model.absolute()), post_flags)
    yield scn

class TestCyborg:
  def test_cyborg_nodes(self, cyborg):
      assert isinstance(cyborg, assimp_py.Scene)

      root_node = cyborg.root_node
      assert isinstance(root_node, assimp_py.Node)

      assert len(root_node.children) == 1
      assert root_node.num_meshes == 0
      assert root_node.children[0].num_meshes == 1

  def test_cyborg_mesh(self, cyborg):
      assert cyborg.num_meshes == 1

      me = cyborg.meshes[0]
      assert isinstance(me, assimp_py.Mesh)
      assert me.num_vertices == 16647

  def test_cyborg_material(self, cyborg):
      assert cyborg.num_materials == 2 # Assimp creates a default material

      mat = cyborg.materials[1] 
      assert isinstance(mat, dict)
      assert "NAME" in mat
      assert "TEXTURES" in mat
      assert "COLOR_DIFFUSE" in mat
      assert len(mat['TEXTURES'].values()) == 3


class TestPlanet:
  def test_cyborg_nodes(self, planet):
      assert isinstance(planet, assimp_py.Scene)

      root_node = planet.root_node
      assert isinstance(root_node, assimp_py.Node)

      assert len(root_node.children) == 1
      assert root_node.num_meshes == 0
      assert root_node.children[0].num_meshes == 1

  def test_planet_mesh(self, planet):
      assert planet.num_meshes == 1

      me = planet.meshes[0]
      assert isinstance(me, assimp_py.Mesh)
      assert me.num_vertices == 24576

  def test_planet_material(self, planet):
      assert planet.num_materials == 2 # Assimp creates a default material

      mat = planet.materials[1] 
      assert isinstance(mat, dict)
      assert "NAME" in mat
      assert "TEXTURES" in mat
      assert "COLOR_DIFFUSE" in mat
      assert len(mat['TEXTURES'].values()) == 2