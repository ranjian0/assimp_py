Process_CalcTangentSpace: int
Process_Debone: int
Process_FindDegenerates: int
Process_FindInstances: int
Process_FindInvalidData: int
Process_FixInfacingNormals: int
Process_FlipUVs: int
Process_FlipWindingOrder: int
Process_GenNormals: int
Process_GenSmoothNormals: int
Process_GenUVCoords: int
Process_GlobalScale: int
Process_ImproveCacheLocality: int
Process_JoinIdenticalVertices: int
Process_LimitBoneWeights: int
Process_MakeLeftHanded: int
Process_OptimizeGraph: int
Process_OptimizeMeshes: int
Process_PreTransformVertices: int
Process_RemoveComponent: int
Process_RemoveRedundantMaterials: int
Process_SortByPType: int
Process_SplitByBoneCount: int
Process_SplitLargeMeshes: int
Process_TransformUVCoords: int
Process_Triangulate: int
Process_ValidateDataStructure: int
TextureType_AMBIENT: int
TextureType_DIFFUSE: int
TextureType_DISPLACEMENT: int
TextureType_EMISSIVE: int
TextureType_HEIGHT: int
TextureType_LIGHTMAP: int
TextureType_NONE: int
TextureType_NORMALS: int
TextureType_OPACITY: int
TextureType_REFLECTION: int
TextureType_SHININESS: int
TextureType_SPECULAR: int
TextureType_UNKNOWN: int

class Mesh:
    bitangents: memoryview
    colors: list[memoryview]
    indices: memoryview
    material_index: int
    name: str
    normals: memoryview
    num_faces: int
    num_indices: int
    num_uv_components: int
    num_vertices: int
    tangents: memoryview
    texcoords: list[memoryview]
    vertices: memoryview
    def __init__(self, *args, **kwargs) -> None: ...

class Node:
    children: list['Node']
    mesh_indices: list[int]
    name: str
    num_children: int
    num_meshes: int
    parent_name: str
    transformation: tuple[tuple]
    def __init__(self, *args, **kwargs) -> None: ...

class Scene:
    materials: list[dict]
    meshes: list[Mesh]
    num_materials: int
    num_meshes: int
    root_node: int
    def __init__(self, *args, **kwargs) -> None: ...

def import_file(filename: str, flags: int) -> Scene: ...
