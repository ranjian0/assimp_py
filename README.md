# assimp-py

![Build Package](https://github.com/ranjian0/assimp-py/workflows/Build%20Package/badge.svg)

Minimal Python Bindings for ASSIMP Library using C-API


# Example Program

```python
import assimp

# -- loading the scene
process_flags = (
    assimp.Process_Triangulate | assimp.Process_CalcTangentSpace
)
scene = assimp.ImportFile("models/planet/planet.obj", process_flags)

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
    diffuse_tex = mat["TEXTURES"][assimp.TextureType_DIFFUSE]
```
