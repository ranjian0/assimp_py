# assimp-py
![Build](https://github.com/ranjian0/assimp-py/workflows/Build/badge.svg)
![PyPI Linux](https://github.com/ranjian0/assimp_py/workflows/PyPI%20ManyLinux/badge.svg)
![PyPI Windows](https://github.com/ranjian0/assimp_py/workflows/PyPI%20WinMac/badge.svg)
![PyPI MacOS](https://github.com/ranjian0/assimp_py/workflows/PyPI%20WinMac/badge.svg)

Minimal Python Bindings for ASSIMP Library using C-API


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
