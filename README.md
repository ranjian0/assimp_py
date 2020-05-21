#ASSIMP-PY

Minimal Python Bindings for ASSIMP Librarry using C-API


#Example

'''python
import assimp

process_flags = (assimp.Process_Triangulate)

scene = assimp.ImportFile("models/planet.planet.obj", process_flags)

'''