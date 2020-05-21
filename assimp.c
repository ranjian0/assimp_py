#define PY_SSIZE_T_CLEAN
#include <string.h>
#include <Python.h>
#include <structmember.h>

#include "assimp/scene.h"
#include "assimp/cimport.h"
#include "assimp/postprocess.h"



// Mesh
typedef struct {
    PyObject_HEAD
    const char* name;
    unsigned int num_faces;
    unsigned int num_vertices;
    unsigned int material_index;
    PyObject* num_uv_components;

    PyObject *faces;
    PyObject *colors;
    PyObject *normals;
    PyObject *vertices;
    PyObject *tangents;
    PyObject *texcoords;
    PyObject *bitangents;
} Mesh;

static PyMemberDef Mesh_members[] = {
    {"name", T_STRING, offsetof(Mesh, name), READONLY, NULL},
    {"material_index", T_UINT, offsetof(Mesh, material_index), READONLY, NULL},
    {"num_uv_components", T_OBJECT, offsetof(Mesh, num_uv_components), READONLY, NULL},

    {"faces", T_OBJECT, offsetof(Mesh, faces), READONLY, NULL},
    {"num_faces", T_UINT, offsetof(Mesh, num_faces), READONLY, NULL},

    {"vertices", T_OBJECT, offsetof(Mesh, vertices), READONLY, NULL},
    {"num_vertices", T_UINT, offsetof(Mesh, num_vertices), READONLY, NULL},

    {"normals", T_OBJECT, offsetof(Mesh, normals), READONLY, NULL},
    {"tangents", T_OBJECT, offsetof(Mesh, tangents), READONLY, NULL},
    {"bitangents", T_OBJECT, offsetof(Mesh, bitangents), READONLY, NULL},
    {"colors", T_OBJECT, offsetof(Mesh, colors), READONLY, NULL},
    {"texcoords", T_OBJECT, offsetof(Mesh, texcoords), READONLY, NULL},
    {NULL},
};

static int Mesh_init(Mesh *self, PyObject *args, PyObject *kwds) {
    self->name = "";
    self->num_faces = 0;
    self->num_vertices = 0;
    self->material_index = 0;

    self->vertices = Py_None;
    self->normals = Py_None;
    self->tangents = Py_None;
    self->bitangents = Py_None;

    return 1;
}

static void Mesh_dealloc(Mesh *self) {
//    Py_CLEAR(self->vertices);
    Py_TYPE(self)->tp_dealloc((PyObject*)self);
}

static PyTypeObject MeshType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name="assimp.Mesh",
    .tp_doc="Container for vertex data",
    .tp_new=PyType_GenericNew,
    .tp_basicsize=sizeof(Mesh),
    .tp_itemsize=0,
    .tp_flags=Py_TPFLAGS_DEFAULT,
    .tp_init=(initproc)Mesh_init,
    .tp_dealloc=(destructor)Mesh_dealloc,
    .tp_members=Mesh_members,
};


// Material
typedef struct {
    PyObject_HEAD
    PyDictObject *properties;
} Material;


// Scene
typedef struct {
    PyObject_HEAD
    PyObject *meshes;
    unsigned int num_meshes;

    PyObject *materials;
    unsigned int num_materials;

} Scene;

static int Scene_init(Scene *scene, PyObject *args, PyObject *kwds) {
    scene->meshes = Py_None;
    scene->materials = Py_None;

    scene->num_meshes = 0;
    scene->num_materials = 0;
    return 1;
}

static void Scene_dealloc(Scene *scene) {
    Py_CLEAR(scene->meshes);
    Py_CLEAR(scene->materials);
    Py_TYPE(scene)->tp_free(scene);
}

static PyMemberDef Scene_members[] = {
    {"meshes", T_OBJECT, offsetof(Scene, meshes), READONLY, NULL},
    {"materials", T_OBJECT, offsetof(Scene, materials), READONLY, NULL},

    {"num_meshes", T_UINT, offsetof(Scene, num_meshes), READONLY, NULL},
    {"num_materials", T_UINT, offsetof(Scene, num_materials), READONLY, NULL},
    {NULL},
};

static PyTypeObject SceneType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name="assimp.Scene",
    .tp_doc="Container for meshes and materials",
    .tp_new=PyType_GenericNew,
    .tp_basicsize=sizeof(Scene),
    .tp_itemsize=0,
    .tp_flags=Py_TPFLAGS_DEFAULT,
    .tp_init=(initproc)Scene_init,
    .tp_dealloc=(destructor)Scene_dealloc,
    .tp_members=Scene_members,
};

// - Scene Processing Methods
static PyObject* tuple_from_face(struct aiFace *f) {
    PyObject *tup = PyTuple_New(f->mNumIndices);
    for(uint i=0; i < f->mNumIndices; i++) {
        PyTuple_SetItem(tup, i, PyLong_FromUnsignedLong(f->mIndices[i]));
    }
    return tup;
}

static PyObject* tuple_from_vec3d(struct aiVector3D *vec) {
    PyObject *tup = PyTuple_New(3);
    PyTuple_SetItem(tup, 0, PyFloat_FromDouble(vec->x));
    PyTuple_SetItem(tup, 1, PyFloat_FromDouble(vec->y));
    PyTuple_SetItem(tup, 2, PyFloat_FromDouble(vec->z));
    return tup;
}

static PyObject* tuple_from_col4d(struct aiColor4D *vec) {
    PyObject *tup = PyTuple_New(3);
    PyTuple_SetItem(tup, 0, PyFloat_FromDouble(vec->r));
    PyTuple_SetItem(tup, 1, PyFloat_FromDouble(vec->g));
    PyTuple_SetItem(tup, 2, PyFloat_FromDouble(vec->b));
    PyTuple_SetItem(tup, 3, PyFloat_FromDouble(vec->a));
    return tup;
}

static PyObject* list_from_vec3d(struct aiVector3D *arr, uint size) {
    PyObject *items = PyList_New(size);
    for(uint i=0; i<size; i++) {
        PyObject *tup = tuple_from_vec3d(&arr[i]);
        PyList_SetItem(items, i, tup);
    }
    return items;
}

static PyObject* list_from_col4d(struct aiColor4D *arr, uint size) {
    PyObject *items = PyList_New(size);
    for(uint i=0; i<size; i++) {
        PyObject *tup = tuple_from_col4d(&arr[i]);
        PyList_SetItem(items, i, tup);
    }
    return items;
}

static char* get_prop_name(char* prop) {
    if (strcmp(prop, "?mat.name") == 0)
        return "NAME";
    else if (strcmp(prop, "$mat.twosided") == 0)
        return "TWOSIDED";
    else if (strcmp(prop, "$mat.shadingm") == 0)
        return "SHADING_MODEL";
    else if (strcmp(prop, "$mat.wireframe") == 0)
        return "ENABLE_WIREFRAME";
    else if (strcmp(prop, "$mat.blend") == 0)
        return "BLEND_FUNC";
    else if (strcmp(prop, "$mat.opacity") == 0)
        return "OPACITY";
    else if (strcmp(prop, "$mat.bumpscaling") == 0)
        return "BUMPSCALING";
    else if (strcmp(prop, "$mat.shininess") == 0)
        return "SHININESS";
    else if (strcmp(prop, "$mat.reflectivity") == 0)
        return "REFLECTIVITY";
    else if (strcmp(prop, "$mat.shinpercent") == 0)
        return "SHININESS_STRENGTH";
    else if (strcmp(prop, "$mat.refracti") == 0)
        return "REFRACTI";
    else if (strcmp(prop, "$clr.diffuse") == 0)
        return "COLOR_DIFFUSE";
    else if (strcmp(prop, "$clr.ambient") == 0)
        return "COLOR_AMBIENT";
    else if (strcmp(prop, "$clr.specular") == 0)
        return "COLOR_SPECULAR";
    else if (strcmp(prop, "$clr.emissive") == 0)
        return "COLOR_EMISSIVE";
    else if (strcmp(prop, "$clr.transparent") == 0)
        return "COLOR_TRANSPARENT";
    else if (strcmp(prop, "$clr.reflective") == 0)
        return "COLOR_REFLECTIVE";
    else if (strcmp(prop, "?bg.global") == 0)
        return "GLOBAL_BACKGROUND_IMAGE";
    else if (strcmp(prop, "$tex.file") == 0)
        return "TEXTURE_BASE";
    else if (strcmp(prop, "$tex.mapping") == 0)
        return "MAPPING_BASE";
    else if (strcmp(prop, "$tex.flags") == 0)
        return "TEXFLAGS_BASE";
    else if (strcmp(prop, "$tex.uvwsrc") == 0)
        return "UVWSRC_BASE";
    else if (strcmp(prop, "$tex.mapmodev") == 0)
        return "MAPPINGMODE_V_BASE";
    else if (strcmp(prop, "$tex.mapaxis") == 0)
        return "TEXMAP_AXIS_BASE";
    else if (strcmp(prop, "$tex.blend") == 0)
        return "TEXBLEND_BASE";
    else if (strcmp(prop, "$tex.uvtrafo") == 0)
        return "UVTRANSFORM_BASE";
    else if (strcmp(prop, "$tex.op") == 0)
        return "TEXOP_BASE";
    else if (strcmp(prop, "$tex.mapmodeu") == 0)
        return "MAPPINGMODE_U_BASE";
    else
        return "NONE";
}

static PyObject* props_from_material(struct aiMaterial *mat) {
    PyObject *prop_val;
    PyObject *props = PyDict_New();

    int result;
    int ival[16];
    float fval[16];
    struct aiString sval;
    unsigned int arr_size;
    for(uint i=0; i<mat->mNumProperties; i++) {
        struct aiMaterialProperty *prop = mat->mProperties[i];

        switch(prop->mType) {
            case aiPTI_String:
                result = aiGetMaterialString(mat, prop->mKey.data, -1, 0, &sval);
                break;
            case aiPTI_Float:
                arr_size = 16;
                result = aiGetMaterialFloatArray(mat, prop->mKey.data, -1, 0, fval, &arr_size);
                break;
            case aiPTI_Integer:
                arr_size = 16;
                result = aiGetMaterialIntegerArray(mat, prop->mKey.data, -1, 0, ival, &arr_size);
                break;
            default:
                continue;
        }

        if(result == aiReturn_FAILURE) {
            continue;
        } else if(result == aiReturn_OUTOFMEMORY) {
            PyErr_SetString(PyExc_MemoryError, "Memory Error Creating material Properties");
            return props;
        }

        switch(prop->mType) {
            case aiPTI_String:
                prop_val = PyUnicode_FromString(sval.data);
                break;
            case aiPTI_Float:
                if(arr_size == 1) {
                    prop_val = PyFloat_FromDouble(fval[0]);
                } else {
                    prop_val = PyList_New(arr_size);
                    for(uint j=0; j<arr_size; j++) {
                        PyList_SetItem(prop_val, j, PyFloat_FromDouble(fval[j]));
                    }
                }
                break;
            case aiPTI_Integer:
                if(arr_size == 1) {
                    prop_val = PyLong_FromLong(ival[0]);
                } else {
                    prop_val = PyList_New(arr_size);
                    for(uint j=0; j<arr_size; j++) {
                        PyList_SetItem(prop_val, j, PyLong_FromLong(ival[j]));
                    }
                }
                break;
             default:
                prop_val = Py_None;
                break;
        }

        PyDict_SetItem(props, PyUnicode_FromString(get_prop_name(prop->mKey.data)), prop_val);
    }

    // Load Textures
    struct aiString texture_path;
    PyObject *texture_dict = PyDict_New();
    enum aiTextureType TexTypes[13] = {
        aiTextureType_NONE,
        aiTextureType_DIFFUSE,
        aiTextureType_SPECULAR,
        aiTextureType_AMBIENT,
        aiTextureType_EMISSIVE,
        aiTextureType_HEIGHT,
        aiTextureType_NORMALS,
        aiTextureType_SHININESS,
        aiTextureType_OPACITY,
        aiTextureType_DISPLACEMENT,
        aiTextureType_LIGHTMAP,
        aiTextureType_REFLECTION,
        aiTextureType_UNKNOWN,
    };

    for(int i=0; i<13; i++) {
        uint tex_count = aiGetMaterialTextureCount(mat, TexTypes[i]);
        if (tex_count < 1)
            continue;

        PyObject *textures = PyList_New(tex_count);
        for(uint j=0; j<tex_count; j++) {
            aiGetMaterialTexture(mat, TexTypes[i], j, &texture_path, NULL, NULL, NULL, NULL, NULL, NULL);
            PyList_SetItem(textures, j, PyUnicode_FromString(texture_path.data));
        }
        PyDict_SetItem(texture_dict, PyLong_FromLong(TexTypes[i]), textures);
    }

    PyDict_SetItem(props, PyUnicode_FromString("TEXTURES"), texture_dict);
    return props;
}

static void process_meshes(Scene *py_scene, const struct aiScene *c_scene) {
    uint num_meshes = c_scene->mNumMeshes;

    py_scene->num_meshes = num_meshes;
    py_scene->meshes = PyList_New(num_meshes);

    for(uint i = 0; i < num_meshes; i++) {
        struct aiMesh *m = c_scene->mMeshes[i];
        Mesh *pymesh = (Mesh*)(MeshType.tp_alloc(&MeshType, 0));

        pymesh->name = m->mName.data;
        pymesh->num_faces = m->mNumFaces;
        pymesh->num_vertices = m->mNumVertices;
        pymesh->material_index = m->mMaterialIndex;

        pymesh->vertices = list_from_vec3d(m->mVertices, m->mNumVertices);
        pymesh->normals = m->mNormals != NULL ? list_from_vec3d(m->mNormals, m->mNumVertices) : Py_None;
        pymesh->tangents = m->mTangents != NULL ? list_from_vec3d(m->mTangents, m->mNumVertices) : Py_None;
        pymesh->bitangents = m->mTangents != NULL ? list_from_vec3d(m->mBitangents, m->mNumVertices) : Py_None;

        pymesh->num_uv_components = PyList_New(0);
        for(uint k=0; k<AI_MAX_NUMBER_OF_TEXTURECOORDS; k++) {
            if (m->mTextureCoords[k] != NULL)
                PyList_Append(pymesh->num_uv_components, PyLong_FromUnsignedLong(m->mNumUVComponents[k]));
        }

        pymesh->texcoords = PyList_New(0);
        for(uint k=0; k<AI_MAX_NUMBER_OF_TEXTURECOORDS; k++) {
            if (m->mTextureCoords[k] != NULL) {
                PyList_Append(pymesh->texcoords, list_from_vec3d(m->mTextureCoords[k], m->mNumVertices));
            }
        }

        pymesh->colors = PyList_New(0);
        for(uint l=0; l<AI_MAX_NUMBER_OF_COLOR_SETS; l++) {
            if (m->mColors[l] != NULL) {
                PyList_Append(pymesh->colors, list_from_col4d(m->mColors[l], m->mNumVertices));
            }
        }

        pymesh->faces = PyList_New(m->mNumFaces);
        for(uint j=0; j < m->mNumFaces; j++) {
            PyList_SetItem(pymesh->faces, j, tuple_from_face(&m->mFaces[j]));
        }

        PyList_SetItem(py_scene->meshes, i, (PyObject*)pymesh);
    }
}

static void process_materials(Scene *py_scene, const struct aiScene *c_scene) {
    py_scene->num_materials = c_scene->mNumMaterials;
    py_scene->materials = PyList_New(c_scene->mNumMaterials);
    for(uint i=0; i<c_scene->mNumMaterials; i++) {
        PyList_SetItem(py_scene->materials, i, props_from_material(c_scene->mMaterials[i]));
    }
}

// - Assimp Wrapper for aiImportFile
PyDoc_STRVAR(import_doc,
"ImportFile(filename, flags) -> Scene\n"
"\n"
"Import the given filename with the provided post-processing flags.\n"
);

static PyObject* ImportFile(PyObject *self, PyObject *args) {
    const char* filename;
    unsigned int flags;
    if(!PyArg_ParseTuple(args, "si", &filename, &flags)) {
        PyErr_SetString(PyExc_ValueError, "Invalid Arguments! Expected (str, int)");
        return (PyObject*)NULL;
    }
    // Check if given filename exists
    FILE *f = fopen(filename, "r");
    if(f) {
        fclose(f);
    } else {
        PyErr_SetString(PyExc_ValueError, "File does not exist!");
        return (PyObject*)NULL;
    }

    // Load the scene
    const struct aiScene *scene = aiImportFile(filename, flags);
    if (!scene) {
        PyErr_SetString(PyExc_ValueError, "Could not Import the file!");
        return (PyObject*)NULL;
    }

    Scene *pyscene = (Scene*)(SceneType.tp_alloc(&SceneType, 0));
    process_meshes(pyscene, scene);
    process_materials(pyscene, scene);
    return (PyObject*)pyscene;
}


// -- Module Initialization
int Initialize_Types(PyObject* module) {
    if (PyType_Ready(&MeshType) < 0)
    {
        return -1;
    }
    Py_INCREF(&MeshType);
    PyModule_AddObject(module, "Mesh", (PyObject *)&MeshType);

    if (PyType_Ready(&SceneType) < 0)
    {
        return -1;
    }
    Py_INCREF(&SceneType);
    PyModule_AddObject(module, "Scene", (PyObject *)&SceneType);

    return 1;
}

void Initialize_Constants(PyObject *module) {
    /* Texture Types */
    PyModule_AddIntConstant(module, "TextureType_NONE", aiTextureType_NONE);
    PyModule_AddIntConstant(module, "TextureType_DIFFUSE", aiTextureType_DIFFUSE);
    PyModule_AddIntConstant(module, "TextureType_SPECULAR", aiTextureType_SPECULAR);
    PyModule_AddIntConstant(module, "TextureType_AMBIENT", aiTextureType_AMBIENT);
    PyModule_AddIntConstant(module, "TextureType_EMISSIVE", aiTextureType_EMISSIVE);
    PyModule_AddIntConstant(module, "TextureType_HEIGHT", aiTextureType_HEIGHT);
    PyModule_AddIntConstant(module, "TextureType_NORMALS", aiTextureType_NORMALS);
    PyModule_AddIntConstant(module, "TextureType_SHININESS", aiTextureType_SHININESS);
    PyModule_AddIntConstant(module, "TextureType_OPACITY", aiTextureType_OPACITY);
    PyModule_AddIntConstant(module, "TextureType_DISPLACEMENT", aiTextureType_DISPLACEMENT);
    PyModule_AddIntConstant(module, "TextureType_LIGHTMAP", aiTextureType_LIGHTMAP);
    PyModule_AddIntConstant(module, "TextureType_REFLECTION", aiTextureType_REFLECTION);
    PyModule_AddIntConstant(module, "TextureType_UNKNOWN", aiTextureType_UNKNOWN);

    /* Add postprocess steps */
    PyModule_AddIntConstant(module, "Process_CalcTangentSpace", aiProcess_CalcTangentSpace);
    PyModule_AddIntConstant(module, "Process_JoinIdenticalVertices", aiProcess_JoinIdenticalVertices);
    PyModule_AddIntConstant(module, "Process_MakeLeftHanded", aiProcess_MakeLeftHanded);
    PyModule_AddIntConstant(module, "Process_Triangulate", aiProcess_Triangulate);
    PyModule_AddIntConstant(module, "Process_RemoveComponent", aiProcess_RemoveComponent);
    PyModule_AddIntConstant(module, "Process_GenNormals", aiProcess_GenNormals);
    PyModule_AddIntConstant(module, "Process_GenSmoothNormals", aiProcess_GenSmoothNormals);
    PyModule_AddIntConstant(module, "Process_SplitLargeMeshes", aiProcess_SplitLargeMeshes);
    PyModule_AddIntConstant(module, "Process_PreTransformVertices", aiProcess_PreTransformVertices);
    PyModule_AddIntConstant(module, "Process_LimitBoneWeights", aiProcess_LimitBoneWeights);
    PyModule_AddIntConstant(module, "Process_ValidateDataStructure", aiProcess_ValidateDataStructure);
    PyModule_AddIntConstant(module, "Process_ImproveCacheLocality", aiProcess_ImproveCacheLocality);
    PyModule_AddIntConstant(module, "Process_RemoveRedundantMaterials", aiProcess_RemoveRedundantMaterials);
    PyModule_AddIntConstant(module, "Process_FixInfacingNormals", aiProcess_FixInfacingNormals);
    PyModule_AddIntConstant(module, "Process_SortByPType", aiProcess_SortByPType);
    PyModule_AddIntConstant(module, "Process_FindDegenerates", aiProcess_FindDegenerates);
    PyModule_AddIntConstant(module, "Process_FindInvalidData", aiProcess_FindInvalidData);
    PyModule_AddIntConstant(module, "Process_GenUVCoords", aiProcess_GenUVCoords);
    PyModule_AddIntConstant(module, "Process_TransformUVCoords", aiProcess_TransformUVCoords);
    PyModule_AddIntConstant(module, "Process_FindInstances", aiProcess_FindInstances);
    PyModule_AddIntConstant(module, "Process_OptimizeMeshes", aiProcess_OptimizeMeshes);
    PyModule_AddIntConstant(module, "Process_OptimizeGraph", aiProcess_OptimizeGraph);
    PyModule_AddIntConstant(module, "Process_FlipUVs", aiProcess_FlipUVs);
    PyModule_AddIntConstant(module, "Process_FlipWindingOrder", aiProcess_FlipWindingOrder);
    PyModule_AddIntConstant(module, "Process_SplitByBoneCount", aiProcess_SplitByBoneCount);
    PyModule_AddIntConstant(module, "Process_Debone", aiProcess_Debone);
    PyModule_AddIntConstant(module, "Process_GlobalScale", aiProcess_GlobalScale);
}

static PyMethodDef assimp_meths[] = {
    {"ImportFile", (PyCFunction)ImportFile, METH_VARARGS, import_doc},
    {NULL, NULL, 0, NULL},
};

static PyModuleDef assimp_mod = {
  PyModuleDef_HEAD_INIT,
  "assimp",
  "Python C API to ASSIMP Library",
  -1,
   assimp_meths,
};

PyMODINIT_FUNC PyInit_assimp() {
    PyObject *mod = PyModule_Create(&assimp_mod);
    Initialize_Constants(mod);
    if(Initialize_Types(mod) < 0) {
        return NULL;
    }
    return mod;
}
