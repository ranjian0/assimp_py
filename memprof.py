import assimp_py
from pprint import pprint
from pathlib import Path
from memory_profiler import profile


model_a = Path(__file__).parent.joinpath("tests/models/cyborg/cyborg.obj")
model_b = Path(__file__).parent.joinpath("tests/models/planet/planet.obj")

@profile
def func(model):
    post_flags = (
        assimp_py.Process_GenNormals | assimp_py.Process_CalcTangentSpace
    )
    scn = assimp_py.ImportFile(str(model.absolute()), post_flags)
    # pprint(scn.materials)
    del scn

func(model_a)
# func(model_b)