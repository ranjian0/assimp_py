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
scene = assimp_py.ImportFile("models/planet/planet.obj", process_flags)

# -- getting data
for m in scene.meshes:
    # -- getting vertex data
    # vertices are guaranteed to exist
    verts = m.vertices

    # other components must be checked for None
    normals = [] or m.normals
    texcoords = [] or m.texcoords
    tangents = [] or m.tangents
    bitangent = [] or m.bitangents

    # -- getting materials
    # mat is a dict consisting of assimp material properties
    mat = scene.materials[m.material_index]

    # -- getting color
    diffuse_color = mat["COLOR_DIFFUSE"]

    # -- getting textures
    diffuse_tex = mat["TEXTURES"][assimp_py.TextureType_DIFFUSE]
```
# Supported Mesh Formats

> AMF 3DS AC ASE ASSBIN B3D BVH COLLADA DXF CSM HMP IRRMESH IRR LWO LWS M3D MD2 MD3 MD5 MDC MDL NFF NDO OFF OGRE OPENGEX PLY MS3D COB BLEND IFC XGL FBX Q3D Q3BSP RAW SIB SMD STL TERRAGEN 3D X X3D GLTF 3MF MMD OBJ
