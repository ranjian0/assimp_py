#define PY_SSIZE_T_CLEAN
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
    {"materials_index", T_UINT, offsetof(Mesh, material_index), READONLY, NULL},
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
    PyObject *materials;

} Scene;

static int Scene_init(Scene *scene, PyObject *args, PyObject *kwds) {
    scene->meshes = Py_None;
    scene->materials = Py_None;
    return 1;
}

static void Scene_dealloc(Scene *scene) {
    Py_CLEAR(scene->meshes);
    Py_CLEAR(scene->materials);
    Py_TYPE(scene)->tp_free(scene);
}

static PyObject* Scene_get_meshes(Scene *scene, void *closure) {
    Py_INCREF(scene->meshes);
    return scene->meshes;
}

static PyObject* Scene_get_materials(Scene *scene, void *closure) {
    Py_INCREF(scene->materials);
    return scene->materials;
}

static PyGetSetDef Scene_getsetters[] = {
    {"meshes", (getter)Scene_get_meshes, NULL, NULL},
    {"materials", (getter)Scene_get_materials, NULL, NULL},
    {NULL}
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
    .tp_getset=Scene_getsetters,
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

static void process_meshes(Scene *py_scene, const struct aiScene *c_scene) {
    uint num_meshes = c_scene->mNumMeshes;
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
    py_scene->materials = PyList_New(0);
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
        return Py_None;
    }
    const struct aiScene *scene = aiImportFile(filename, flags);
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
