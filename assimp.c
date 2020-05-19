#include <python3.7m/Python.h>
#include "assimp/cimport.h"
#include "assimp/postprocess.h"

// -- String
typedef struct  {
    PyObject_HEAD
    struct aiString *ptr;
} String;

static int String_init(String *self, PyObject *args, PyObject *kwds) {
    char *data;
    if (!PyArg_ParseTuple(args, "s", &data))
    {
        PyErr_SetString(PyExc_ValueError, "Invalid arguments!");
        return -1;
    }

    self->ptr = malloc(sizeof(struct aiString));
    if (!self->ptr)
    {
        PyErr_SetString(PyExc_MemoryError, "Couldn't Create aiString!");
        return -1;
    }

    strcpy(self->ptr->data, data);
    self->ptr->length = strlen(data);
    return 1;
}

static void String_dealloc(String *self) {
    free(self->ptr);
    Py_TYPE(self)->tp_free(self);
}

static PyObject *String_getdata(String *self, void *closure) {
    return PyUnicode_FromString(self->ptr->data);
}

static PyGetSetDef String_getsetters[] = {
    {"data", (getter)String_getdata, NULL, NULL},
    {NULL}
};

static PyTypeObject StringType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "assimp.String",
    .tp_doc = "Assimp String Wrapper",
    .tp_new = PyType_GenericNew,
    .tp_basicsize = sizeof(String),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_init = (initproc)String_init,
    .tp_dealloc = (destructor)String_dealloc,
    .tp_getset = String_getsetters,
};

// -- Vec2D
typedef struct {
    PyObject_HEAD
    struct aiVector2D *ptr;
} Vec2D;

static int Vec2D_init(Vec2D *self, PyObject *args, PyObject *kwds) {
    double x, y;
    if (!PyArg_ParseTuple(args, "dd", &x, &y))
    {
        PyErr_SetString(PyExc_ValueError, "Invalid arguments");
        return -1;
    }

    self->ptr = malloc(sizeof(struct aiVector2D));
    if (!self->ptr)
    {
        PyErr_SetString(PyExc_MemoryError, "Couldn't Create aiVector2D!");
        return -1;
    }

    self->ptr->x = x;
    self->ptr->y = y;
    return 1;
}

static void Vec2D_dealloc(Vec2D *self) {
    free(self->ptr);
    Py_TYPE(self)->tp_free(self);
}

static PyObject *Vec2D_getxy(Vec2D *self, void *closure) {
    PyObject *items = PyTuple_New(2);
    PyTuple_SetItem(items, 0, PyFloat_FromDouble(self->ptr->x));
    PyTuple_SetItem(items, 1, PyFloat_FromDouble(self->ptr->y));
    return items;
}

static PyGetSetDef Vec2D_getsetters[] = {
    {"xy", (getter)Vec2D_getxy, NULL, NULL},
    {NULL}
};

static PyTypeObject Vec2DType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "assimp.Vec2D",
    .tp_doc = "Assimp Vec2D Wrapper",
    .tp_new = PyType_GenericNew,
    .tp_basicsize = sizeof(Vec2D),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_init = (initproc)Vec2D_init,
    .tp_dealloc = (destructor)Vec2D_dealloc,
    .tp_getset = Vec2D_getsetters,
};

// -- Vec3D
typedef struct {
    PyObject_HEAD
    struct aiVector3D *ptr;
} Vec3D;

static int Vec3D_init(Vec3D *self, PyObject *args, PyObject *kwds) {
    double x, y, z;
    if (!PyArg_ParseTuple(args, "ddd", &x, &y, &z))
    {
        PyErr_SetString(PyExc_ValueError, "Invalid arguments");
        return -1;
    }

    self->ptr = malloc(sizeof(struct aiVector3D));
    if (!self->ptr)
    {
        PyErr_SetString(PyExc_MemoryError, "Couldn't Create aiVector3D!");
        return -1;
    }

    self->ptr->x = x;
    self->ptr->y = y;
    self->ptr->z = z;
    return 1;
}

static void Vec3D_dealloc(Vec3D *self) {
    free(self->ptr);
    Py_TYPE(self)->tp_free(self);
}

static PyObject *Vec3D_getxyz(Vec3D *self, void *closure) {
    PyObject *items = PyTuple_New(3);
    PyTuple_SetItem(items, 0, PyFloat_FromDouble(self->ptr->x));
    PyTuple_SetItem(items, 1, PyFloat_FromDouble(self->ptr->y));
    PyTuple_SetItem(items, 2, PyFloat_FromDouble(self->ptr->z));
    return items;
}

static PyGetSetDef Vec3D_getsetters[] = {
    {"xyz", (getter)Vec3D_getxyz, NULL, NULL},
    {NULL}
};

static PyTypeObject Vec3DType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "assimp.Vec3D",
    .tp_doc = "Assimp Vec3D Wrapper",
    .tp_new = PyType_GenericNew,
    .tp_basicsize = sizeof(Vec3D),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_init = (initproc)Vec3D_init,
    .tp_dealloc = (destructor)Vec3D_dealloc,
    .tp_getset = Vec3D_getsetters,
};

// -- Quaternion
typedef struct {
    PyObject_HEAD
    struct aiQuaternion *ptr;
} Quaternion;

static int Quaternion_init(Quaternion *self, PyObject *args, PyObject *kwds) {
    double x, y, z, w;
    if (!PyArg_ParseTuple(args, "dddd", &w, &x, &y, &z))
    {
        PyErr_SetString(PyExc_ValueError, "Invalid arguments");
        return -1;
    }

    self->ptr = malloc(sizeof(struct aiQuaternion));
    if (!self->ptr)
    {
        PyErr_SetString(PyExc_MemoryError, "Couldn't Create aiQuaternion!");
        return -1;
    }

    self->ptr->w = w;
    self->ptr->x = x;
    self->ptr->y = y;
    self->ptr->z = z;
    return 1;
}

static void Quaternion_dealloc(Quaternion *self) {
    free(self->ptr);
    Py_TYPE(self)->tp_free(self);
}

static PyObject *Quaternion_getwxyz(Quaternion *self, void *closure) {
    PyObject *items = PyTuple_New(4);
    PyTuple_SetItem(items, 0, PyFloat_FromDouble(self->ptr->w));
    PyTuple_SetItem(items, 1, PyFloat_FromDouble(self->ptr->x));
    PyTuple_SetItem(items, 2, PyFloat_FromDouble(self->ptr->y));
    PyTuple_SetItem(items, 3, PyFloat_FromDouble(self->ptr->z));
    return items;
}

static PyGetSetDef Quaternion_getsetters[] = {
    {"wxyz", (getter)Quaternion_getwxyz, NULL, NULL},
    {NULL}
};

static PyTypeObject QuaternionType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "assimp.Quaternion",
    .tp_doc = "Assimp Quaternion Wrapper",
    .tp_new = PyType_GenericNew,
    .tp_basicsize = sizeof(Quaternion),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_init = (initproc)Quaternion_init,
    .tp_dealloc = (destructor)Quaternion_dealloc,
    .tp_getset = Quaternion_getsetters,
};

// -- Color3D
typedef struct {
    PyObject_HEAD
    struct aiColor3D *ptr;
} Color3D;

static int Color3D_init(Color3D *self, PyObject *args, PyObject *kwds) {
    double r, g, b;
    if (!PyArg_ParseTuple(args, "ddd", &r, &g, &b))
    {
        PyErr_SetString(PyExc_ValueError, "Invalid arguments");
        return -1;
    }

    self->ptr = malloc(sizeof(struct aiColor3D));
    if (!self->ptr)
    {
        PyErr_SetString(PyExc_MemoryError, "Couldn't Create aiColor3D!");
        return -1;
    }

    self->ptr->r = r;
    self->ptr->g = g;
    self->ptr->b = b;
    return 1;
}

static void Color3D_dealloc(Color3D *self) {
    free(self->ptr);
    Py_TYPE(self)->tp_free(self);
}

static PyObject *Color3D_getrgb(Color3D *self, void *closure) {
    PyObject *items = PyTuple_New(3);
    PyTuple_SetItem(items, 0, PyFloat_FromDouble(self->ptr->r));
    PyTuple_SetItem(items, 1, PyFloat_FromDouble(self->ptr->g));
    PyTuple_SetItem(items, 2, PyFloat_FromDouble(self->ptr->b));
    return items;
}

static PyGetSetDef Color3D_getsetters[] = {
    {"rgb", (getter)Color3D_getrgb, NULL, NULL},
    {NULL}
};

static PyTypeObject Color3DType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "assimp.Color3D",
    .tp_doc = "Assimp Color3D Wrapper",
    .tp_new = PyType_GenericNew,
    .tp_basicsize = sizeof(Color3D),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_init = (initproc)Color3D_init,
    .tp_dealloc = (destructor)Color3D_dealloc,
    .tp_getset = Color3D_getsetters,
};

// -- Color4D
typedef struct {
    PyObject_HEAD
    struct aiColor4D *ptr;
} Color4D;

static int Color4D_init(Color4D *self, PyObject *args, PyObject *kwds) {
    double r, g, b, a;
    if (!PyArg_ParseTuple(args, "dddd", &r, &g, &b, &a))
    {
        PyErr_SetString(PyExc_ValueError, "Invalid arguments");
        return -1;
    }

    self->ptr = malloc(sizeof(struct aiColor4D));
    if (!self->ptr)
    {
        PyErr_SetString(PyExc_MemoryError, "Couldn't Create aiColor4D!");
        return -1;
    }

    self->ptr->r = r;
    self->ptr->g = g;
    self->ptr->b = b;
    self->ptr->a = a;
    return 1;
}

static void Color4D_dealloc(Color4D *self) {
    free(self->ptr);
    Py_TYPE(self)->tp_free(self);
}

static PyObject *Color4D_getrgba(Color4D *self, void *closure) {
    PyObject *items = PyTuple_New(4);
    PyTuple_SetItem(items, 0, PyFloat_FromDouble(self->ptr->r));
    PyTuple_SetItem(items, 1, PyFloat_FromDouble(self->ptr->g));
    PyTuple_SetItem(items, 2, PyFloat_FromDouble(self->ptr->b));
    PyTuple_SetItem(items, 3, PyFloat_FromDouble(self->ptr->a));
    return items;
}

static PyGetSetDef Color4D_getsetters[] = {
    {"rgba", (getter)Color4D_getrgba, NULL, NULL},
    {NULL}
};

static PyTypeObject Color4DType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "assimp.Color4D",
    .tp_doc = "Assimp Color4D Wrapper",
    .tp_new = PyType_GenericNew,
    .tp_basicsize = sizeof(Color4D),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_init = (initproc)Color4D_init,
    .tp_dealloc = (destructor)Color4D_dealloc,
    .tp_getset = Color4D_getsetters,
};

// -- Assimp Functions
static PyObject* ImportFile(PyObject *self, PyObject *args) {
    char *filepath;
    unsigned int flags;

    if (!PyArg_ParseTuple(args, "si", &filepath, &flags)) {
        return NULL;
    }

    const struct aiScene* scene = aiImportFile(filepath, flags);
    if (!scene) {
        PyErr_SetString(PyExc_ValueError, "Import Failed!");
        return NULL;
    }

    return PyLong_FromLong(1);
}

static PyObject* ReleaseImporter(PyObject *self, PyObject *args) {
    // printf("Importer Released\n");
    // Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *GetErrorString(PyObject *self, PyObject *Py_UNUSED(args)) {
    const char* error = aiGetErrorString();
    return PyUnicode_FromString(error);
}

// -- Python Module Definition and Initialization
static PyMethodDef AssimpMethods[] = {
    {"ImportFile", (PyCFunction)ImportFile, METH_VARARGS, "Interface for assimp aiImportFile()"},
    {"ReleaseImporter", (PyCFunction)ReleaseImporter, METH_VARARGS, "Interface for assimp aiReleaseImporter()"},
    {"GetErrorString", (PyCFunction)GetErrorString, METH_VARARGS, "Interface for assimp aiGetErrorString()"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef assimpmodule = {
    PyModuleDef_HEAD_INIT,
    "assimp",
    "Python interface for the assimp library",
    -1,
    AssimpMethods
};


int Initialize_Types(PyObject* module) {
    if (PyType_Ready(&StringType) < 0)
    {
        return -1;
    }
    Py_INCREF(&StringType);
    PyModule_AddObject(module, "String", (PyObject *)&StringType);

    if (PyType_Ready(&Vec2DType) < 0)
    {
        return -1;
    }
    Py_INCREF(&Vec2DType);
    PyModule_AddObject(module, "Vec2D", (PyObject *)&Vec2DType);

    if (PyType_Ready(&Vec3DType) < 0)
    {
        return -1;
    }
    Py_INCREF(&Vec3DType);
    PyModule_AddObject(module, "Vec3D", (PyObject *)&Vec3DType);

    if (PyType_Ready(&QuaternionType) < 0)
    {
        return -1;
    }
    Py_INCREF(&QuaternionType);
    PyModule_AddObject(module, "Quaternion", (PyObject *)&QuaternionType);

    if (PyType_Ready(&Color3DType) < 0)
    {
        return -1;
    }
    Py_INCREF(&Color3DType);
    PyModule_AddObject(module, "Color3D", (PyObject *)&Color3DType);

    if (PyType_Ready(&Color4DType) < 0)
    {
        return -1;
    }
    Py_INCREF(&Color4DType);
    PyModule_AddObject(module, "Color4D", (PyObject *)&Color4DType);

    return 1;
}


void Initialize_Constants(PyObject *module) {
    /* Add return flags */
    PyModule_AddIntConstant(module, "Return_SUCCESS", aiReturn_SUCCESS);
    PyModule_AddIntConstant(module, "Return_FAILURE", aiReturn_FAILURE);
    PyModule_AddIntConstant(module, "Return_OUTOFMEMORY", aiReturn_OUTOFMEMORY);

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
PyMODINIT_FUNC PyInit_assimp(void) {
    PyObject *module = PyModule_Create(&assimpmodule);

    Initialize_Constants(module);
    if (Initialize_Types(module) < 0) {
        return NULL;
    }

    return module;
}
