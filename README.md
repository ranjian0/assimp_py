# assimp-py
[![Build package](https://github.com/ranjian0/assimp_py/actions/workflows/python-package.yml/badge.svg)](https://github.com/ranjian0/assimp_py/actions/workflows/python-package.yml)
[![Publish assimp-py to PyPI](https://github.com/ranjian0/assimp_py/actions/workflows/python-publish.yml/badge.svg)](https://github.com/ranjian0/assimp_py/actions/workflows/python-publish.yml)

Minimal Python Bindings for ASSIMP Library using C-API


# Installation

```
pip install assimp-py
```

from source

```
git clone https://github.com/ranjian0/assimp_py.git
cd assimp_py
python -m pip install .
```

> **cmake>=3.18 is required for building from source**

## [Optional] Run tests to make sure everything works fine
```
pip install pytest
pytest tests
```


# Example Program

```python
import assimp_py

# -- loading the scene
process_flags = (
    assimp_py.Process_Triangulate | assimp_py.Process_CalcTangentSpace
)
scene = assimp_py.import_file("tests/models/cyborg/cyborg.obj", process_flags)

# -- getting data
for m in scene.meshes:
    # IMPORTANT 
    # All vertex data is stored as memoryviews


    # -- getting vertex data
    # vertices are guaranteed to exist
    verts = m.vertices
    # as a list
    verts_list = verts.tolist()
    # as bytes
    verts_bytes = verts.tobytes()
    # (Optional) as numpy array
    # np.asarray(verts)

    # other components must be checked for None
    normals = m.normals if m.normals else []
    tangents = m.tangents if m.tangents else []
    bitangents = m.bitangents if m.bitangents else []

    # texcoords come in sets, 'm.texcoords' is a list of memoryviews or None
    if m.texcoords:
      num_texcoords_sets = len(m.texcoords)

      texcoords1 = m.texcoords[0]
      # texcoords2 = m.texcoords[1]
      # texcoords3 = m.texcoords[2]
      print(texcoords1)


    # colors also come in sets, 'm.colors' is a list of memoryviews or None
    if m.colors:
      num_color_sets = len([] or m.colors)

      colors1 = m.colors[0]
      # colors2 = m.colors[1]
      # colors3 = m.colors[2]
      print(colors1)


    # -- getting materials
    # mat is a dict consisting of assimp material properties
    mat = scene.materials[m.material_index]

    # -- getting color
    diffuse_color = mat["COLOR_DIFFUSE"]
    print(diffuse_color)

    # -- getting textures
    diffuse_tex = mat["TEXTURES"][assimp_py.TextureType_DIFFUSE]
    print(diffuse_tex)

# Nodes are also available
root = scene.root_node

def traverse(root, indent=0):
   print(' '*indent + root.name)
   # print(root.transformation) -- transform matrix for the node
   for child in root.children:
      traverse(child, indent=indent+2)

print("Traversing nodes ...")
traverse(root)
```
# Supported Mesh Formats

> AMF 3DS AC ASE ASSBIN B3D BVH COLLADA DXF CSM HMP IRRMESH IRR LWO LWS M3D MD2 MD3 MD5 MDC MDL NFF NDO OFF OGRE OPENGEX PLY MS3D COB BLEND IFC XGL FBX Q3D Q3BSP RAW SIB SMD STL TERRAGEN 3D X X3D GLTF 3MF MMD OBJ

> ASSIMP Version 5.4.3
