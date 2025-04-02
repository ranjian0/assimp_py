#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <structmember.h> // For PyMemberDef, T_* flags, offsetof
#include <stdio.h>        // For FILE, fopen, etc. (though only used for existence check)
#include <string.h>       // For strcmp, memcpy
#include <stdlib.h>       // For malloc, free

// Define ASSIMP_DLL for dynamic linking if needed (common on Windows)
// #define ASSIMP_DLL
#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h> // For AI_MATKEY_* definitions

// Forward declarations for type objects
static PyTypeObject MeshType;
static PyTypeObject SceneType;

// --- Mesh Type Definition ---

typedef struct {
    PyObject_HEAD
    // --- Python Objects (exposed as attributes) ---
    PyObject *name;             // PyUnicodeObject
    PyObject *num_uv_components;// List of ints (or maybe just one int if needed?)
    PyObject *indices;          // PyMemoryView (uint32)
    PyObject *vertices;         // PyMemoryView (float32 x 3)
    PyObject *normals;          // PyMemoryView (float32 x 3) or None
    PyObject *tangents;         // PyMemoryView (float32 x 3) or None
    PyObject *bitangents;       // PyMemoryView (float32 x 3) or None
    PyObject *colors;           // List of PyMemoryView (float32 x 4) or None
    PyObject *texcoords;        // List of PyMemoryView (float32 x N) or None

    // --- C Data Pointers (managed internally) ---
    // We store these to manage the lifetime of the memory backing the memoryviews
    unsigned int *c_indices;
    float *c_vertices;
    float *c_normals;
    float *c_tangents;
    float *c_bitangents;
    float **c_colors;           // Array of pointers to color sets
    float **c_texcoords;        // Array of pointers to texcoord sets

    // --- Other Attributes ---
    unsigned int num_vertices;
    unsigned int num_indices;    // Total number of indices (num_faces * 3 if triangulated)
    unsigned int num_faces;      // Original number of faces
    unsigned int material_index;
    unsigned int num_color_sets;
    unsigned int num_texcoord_sets;
    unsigned int *c_num_uv_components; // Array for UV component counts

} Mesh;

static int Mesh_init(Mesh *self, PyObject *args, PyObject *kwds) {
    // Initialize all PyObject pointers to NULL is crucial for safe deallocation
    self->name = NULL;
    self->num_uv_components = NULL;
    self->indices = NULL;
    self->vertices = NULL;
    self->normals = NULL;
    self->tangents = NULL;
    self->bitangents = NULL;
    self->colors = NULL;
    self->texcoords = NULL;

    // Initialize C pointers to NULL and counts to 0
    self->c_indices = NULL;
    self->c_vertices = NULL;
    self->c_normals = NULL;
    self->c_tangents = NULL;
    self->c_bitangents = NULL;
    self->c_colors = NULL;
    self->c_texcoords = NULL;
    self->c_num_uv_components = NULL;

    self->num_vertices = 0;
    self->num_indices = 0;
    self->num_faces = 0;
    self->material_index = 0;
    self->num_color_sets = 0;
    self->num_texcoord_sets = 0;

    // This init shouldn't be called directly by Python users typically.
    // Initialization happens during the scene loading process.
    return 0;
}

static void Mesh_dealloc(Mesh *self) {
    // Clear Python objects (decrements reference count)
    Py_CLEAR(self->name);
    Py_CLEAR(self->num_uv_components);
    Py_CLEAR(self->indices);
    Py_CLEAR(self->vertices);
    Py_CLEAR(self->normals);
    Py_CLEAR(self->tangents);
    Py_CLEAR(self->bitangents);
    Py_CLEAR(self->colors);
    Py_CLEAR(self->texcoords);

    // Free C arrays
    free(self->c_indices);
    free(self->c_vertices);
    free(self->c_normals);
    free(self->c_tangents);
    free(self->c_bitangents);
    if (self->c_colors) {
        for (unsigned int i = 0; i < self->num_color_sets; ++i) {
            free(self->c_colors[i]);
        }
        free(self->c_colors);
    }
    if (self->c_texcoords) {
        for (unsigned int i = 0; i < self->num_texcoord_sets; ++i) {
            free(self->c_texcoords[i]);
        }
        free(self->c_texcoords);
    }
    free(self->c_num_uv_components);


    // Free the object itself
    Py_TYPE(self)->tp_free((PyObject *)self);
}

// Expose attributes as read-only members
static PyMemberDef Mesh_members[] = {
    {"name", T_OBJECT_EX, offsetof(Mesh, name), READONLY, "Mesh name"},
    {"material_index", T_UINT, offsetof(Mesh, material_index), READONLY, "Material index for this mesh"},
    {"num_vertices", T_UINT, offsetof(Mesh, num_vertices), READONLY, "Number of vertices"},
    {"num_faces", T_UINT, offsetof(Mesh, num_faces), READONLY, "Number of faces"},
    {"num_indices", T_UINT, offsetof(Mesh, num_indices), READONLY, "Total number of indices"},

    // Data attributes (MemoryViews or None)
    {"indices", T_OBJECT_EX, offsetof(Mesh, indices), READONLY, "Vertex indices (memoryview, uint32)"},
    {"vertices", T_OBJECT_EX, offsetof(Mesh, vertices), READONLY, "Vertex positions (memoryview, float32, Nx3)"},
    {"normals", T_OBJECT_EX, offsetof(Mesh, normals), READONLY, "Vertex normals (memoryview, float32, Nx3 or None)"},
    {"tangents", T_OBJECT_EX, offsetof(Mesh, tangents), READONLY, "Vertex tangents (memoryview, float32, Nx3 or None)"},
    {"bitangents", T_OBJECT_EX, offsetof(Mesh, bitangents), READONLY, "Vertex bitangents (memoryview, float32, Nx3 or None)"},
    {"colors", T_OBJECT_EX, offsetof(Mesh, colors), READONLY, "List of vertex color sets (list of memoryview, float32, Nx4 or None)"},
    {"texcoords", T_OBJECT_EX, offsetof(Mesh, texcoords), READONLY, "List of vertex texture coordinate sets (list of memoryview, float32, NxNcomp or None)"},
    {"num_uv_components", T_OBJECT_EX, offsetof(Mesh, num_uv_components), READONLY, "List of component counts for each texcoord set"},
    {NULL} /* Sentinel */
};

static PyTypeObject MeshType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "assimp_py.Mesh",
    .tp_doc = "Mesh object containing vertex data and indices",
    .tp_basicsize = sizeof(Mesh),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_init = (initproc)Mesh_init,
    .tp_dealloc = (destructor)Mesh_dealloc,
    .tp_members = Mesh_members,
};


// --- Scene Type Definition ---

typedef struct {
    PyObject_HEAD
    PyObject *meshes;     // List of Mesh objects
    PyObject *materials;  // List of Material dictionaries
    unsigned int num_meshes;
    unsigned int num_materials;
    // Could add nodes, animations, etc. here in the future
} Scene;

static int Scene_init(Scene *self, PyObject *args, PyObject *kwds) {
    self->meshes = NULL;
    self->materials = NULL;
    self->num_meshes = 0;
    self->num_materials = 0;
    return 0;
}

static void Scene_dealloc(Scene *self) {
    Py_CLEAR(self->meshes);
    Py_CLEAR(self->materials);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyMemberDef Scene_members[] = {
    {"meshes", T_OBJECT_EX, offsetof(Scene, meshes), READONLY, "List of meshes in the scene"},
    {"materials", T_OBJECT_EX, offsetof(Scene, materials), READONLY, "List of materials (dictionaries) in the scene"},
    {"num_meshes", T_UINT, offsetof(Scene, num_meshes), READONLY, "Number of meshes"},
    {"num_materials", T_UINT, offsetof(Scene, num_materials), READONLY, "Number of materials"},
    {NULL} /* Sentinel */
};

static PyTypeObject SceneType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "assimp_py.Scene",
    .tp_doc = "Scene object containing meshes and materials loaded from a file",
    .tp_basicsize = sizeof(Scene),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_init = (initproc)Scene_init,
    .tp_dealloc = (destructor)Scene_dealloc,
    .tp_members = Scene_members,
};


// --- Helper Functions ---

// Safely create a memory view from a C array. Returns new reference or NULL on error.
static PyObject* create_memoryview(void* data, Py_ssize_t len, const char* format, Py_ssize_t itemsize) {
    if (!data) {
        Py_RETURN_NONE; // Return None if the C data pointer is NULL
    }
    Py_buffer buf_info;
    // PyBUF_SIMPLE is often enough, but PyBUF_FORMAT ensures the format string is used.
    // PyBUF_WRITABLE is *not* set, making the memoryview read-only by default.
    if (PyBuffer_FillInfo(&buf_info, NULL, data, len, 1, PyBUF_FORMAT | PyBUF_STRIDES) == -1) {
        // Error already set by PyBuffer_FillInfo
        return NULL;
    }
    buf_info.format = (char*)format; // Cast needed for C compatibility
    buf_info.itemsize = itemsize; // *** SET EXPLICIT ITEMSIZE ***

    PyObject *memview = PyMemoryView_FromBuffer(&buf_info);
    if (!memview) {
        // Error should be set by PyMemoryView_FromBuffer
        return NULL;
    }
    return memview; // Return new reference
}

// Helper to create a Python list of floats from a C array of aiColor4D
// Returns a new reference or NULL on error.
static PyObject* list_from_color4d_array(const struct aiColor4D* colors, unsigned int count) {
    PyObject* list = PyList_New(count);
    if (!list) return NULL;

    for (unsigned int i = 0; i < count; ++i) {
        PyObject* tuple = Py_BuildValue("ffff", colors[i].r, colors[i].g, colors[i].b, colors[i].a);
        if (!tuple) {
            Py_DECREF(list);
            return NULL;
        }
        // PyList_SetItem steals the reference to tuple, no DECREF needed on success
        if (PyList_SetItem(list, i, tuple) < 0) {
             Py_DECREF(tuple);
             Py_DECREF(list);
             return NULL;
        }
    }
    return list;
}

// Helper to get a material property value. Returns new reference or NULL on error.
static PyObject* get_material_property(struct aiMaterial *mat, const char *key, unsigned int type, unsigned int index) {
    aiReturn ret;
    PyObject *py_value = NULL;

    const struct aiMaterialProperty *outProp;
    aiGetMaterialProperty(mat, key, type, index, &outProp);
    switch (outProp->mType) {
        case aiPTI_String: {
            struct aiString str;
            ret = aiGetMaterialString(mat, key, type, index, &str);
            if (ret == aiReturn_SUCCESS) {
                py_value = PyUnicode_FromString(str.data);
            }
            break;
        }
        case aiPTI_Float: {
            unsigned int num_floats = 0; // Max number expected
             // First call to get the size
            ret = aiGetMaterialFloatArray(mat, key, type, index, NULL, &num_floats);
            if(ret != aiReturn_SUCCESS || num_floats == 0) break;

            // Allocate buffer + 1 for safety? No, aiGetMaterialProperty ensures size.
            float* floats = (float*)malloc(num_floats * sizeof(float));
            if (!floats) { PyErr_NoMemory(); return NULL; }

            ret = aiGetMaterialFloatArray(mat, key, type, index, floats, &num_floats);
            if (ret == aiReturn_SUCCESS) {
                if (num_floats == 1) {
                    py_value = PyFloat_FromDouble((double)floats[0]);
                } else if (num_floats == 4 && 
                           strcmp(key, "$clr.diffuse") == 0 || // Common colors
                           strcmp(key, "$clr.specular") == 0 ||
                           strcmp(key, "$clr.ambient") == 0 ||
                           strcmp(key, "$clr.emissive") == 0 ||
                           strcmp(key, "$clr.transparent") == 0 ||
                           strcmp(key, "$clr.reflective") == 0)
                 {
                     py_value = Py_BuildValue("ffff", floats[0], floats[1], floats[2], floats[3]);
                 }
                else { // Generic list of floats
                    py_value = PyList_New(num_floats);
                    if (py_value) {
                        for (unsigned int i = 0; i < num_floats; ++i) {
                            PyObject* f = PyFloat_FromDouble((double)floats[i]);
                            if (!f) { Py_DECREF(py_value); py_value = NULL; break; }
                            PyList_SET_ITEM(py_value, i, f); // Steals reference to f
                        }
                    }
                }
            }
            free(floats);
            break;
        }
        case aiPTI_Integer: {
             unsigned int num_ints = 0;
             ret = aiGetMaterialIntegerArray(mat, key, type, index, NULL, &num_ints);
             if(ret != aiReturn_SUCCESS || num_ints == 0) break;

             int* ints = (int*)malloc(num_ints * sizeof(int));
             if(!ints) { PyErr_NoMemory(); return NULL; }

             ret = aiGetMaterialIntegerArray(mat, key, type, index, ints, &num_ints);
             if (ret == aiReturn_SUCCESS) {
                 if (num_ints == 1) {
                     py_value = PyLong_FromLong((long)ints[0]);
                 } else {
                     py_value = PyList_New(num_ints);
                     if (py_value) {
                         for (unsigned int i = 0; i < num_ints; ++i) {
                             PyObject* integer = PyLong_FromLong((long)ints[i]);
                             if (!integer) { Py_DECREF(py_value); py_value = NULL; break; }
                             PyList_SET_ITEM(py_value, i, integer); // Steals reference
                         }
                     }
                 }
             }
             free(ints);
             break;
        }
        // aiPTI_Buffer not handled here for simplicity
        default:
            // Unsupported type or error
            break;
    }

    if (ret != aiReturn_SUCCESS && !PyErr_Occurred()) {
        // Don't overwrite an existing error (like NoMemory)
        // PyErr_Format(PyExc_RuntimeError, "Failed to retrieve material property '%s'", key);
        // Return NULL, signifies failure but maybe not critical? Or maybe return None?
        // Let's return NULL to indicate failure.
        return NULL;
    }

    // If py_value is still NULL after checks, it means the property was
    // found but conversion failed or wasn't supported.
    if (!py_value && !PyErr_Occurred()) {
         Py_RETURN_NONE; // Property exists but we didn't convert it, return None.
    }

    return py_value; // Return new reference (or NULL on error, or None)
}

// --- Scene Processing Logic ---

// Process materials from aiScene into a Python list of dictionaries.
// Returns a new reference to the list, or NULL on error.
static PyObject* process_materials(const struct aiScene *c_scene) {
    unsigned int num_materials = c_scene->mNumMaterials;
    PyObject *py_materials_list = PyList_New(num_materials);
    if (!py_materials_list) return NULL;

    for (unsigned int i = 0; i < num_materials; ++i) {
        struct aiMaterial *mat = c_scene->mMaterials[i];
        PyObject *mat_dict = PyDict_New();
        if (!mat_dict) goto fail_mat_list;

        // Iterate through material properties
        for (unsigned int p = 0; p < mat->mNumProperties; ++p) {
            struct aiMaterialProperty *prop = mat->mProperties[p];
            // Use the original key directly
            PyObject *py_key = PyUnicode_FromString(prop->mKey.data);
            if (!py_key) {
                Py_DECREF(mat_dict);
                goto fail_mat_list;
            }

            PyObject *py_value = get_material_property(mat, prop->mKey.data, prop->mSemantic, prop->mIndex);
            if (!py_value) { // Error occurred in get_material_property
                Py_DECREF(py_key);
                Py_DECREF(mat_dict);
                goto fail_mat_list;
            }

            // Only add if value is not None (unless None signifies absence vs error)
            // Let's include None values for completeness.
            if (PyDict_SetItem(mat_dict, py_key, py_value) < 0) {
                Py_DECREF(py_key);
                Py_DECREF(py_value);
                Py_DECREF(mat_dict);
                goto fail_mat_list;
            }
            Py_DECREF(py_key);   // Dereference temporary key
            Py_DECREF(py_value); // Dereference value (SetItem increments)
        }

        // Add textures separately for clarity
        PyObject *textures_dict = PyDict_New();
        if (!textures_dict) {
            Py_DECREF(mat_dict);
            goto fail_mat_list;
        }

        for (int tt = aiTextureType_NONE; tt <= aiTextureType_UNKNOWN; ++tt) {
             enum aiTextureType tex_type = (enum aiTextureType)tt;
             unsigned int tex_count = aiGetMaterialTextureCount(mat, tex_type);
             if (tex_count == 0) continue;

             PyObject *texture_list = PyList_New(tex_count);
             if (!texture_list) {
                 Py_DECREF(textures_dict);
                 Py_DECREF(mat_dict);
                 goto fail_mat_list;
             }

             for(unsigned int tex_idx = 0; tex_idx < tex_count; ++tex_idx) {
                 struct aiString path;
                 // We only retrieve the path here. Other details could be added.
                 aiReturn ret = aiGetMaterialTexture(mat, tex_type, tex_idx, &path,
                                                     NULL, NULL, NULL, NULL, NULL, NULL);
                 if (ret == aiReturn_SUCCESS) {
                     PyObject *py_path = PyUnicode_FromString(path.data);
                     if (!py_path) {
                         Py_DECREF(texture_list);
                         Py_DECREF(textures_dict);
                         Py_DECREF(mat_dict);
                         goto fail_mat_list;
                     }
                     PyList_SET_ITEM(texture_list, tex_idx, py_path); // Steals ref
                 } else {
                     // Handle error or insert None? Let's skip on error.
                     PyErr_Format(PyExc_RuntimeError, "Failed to get texture path for type %d index %d", tt, tex_idx);
                     Py_DECREF(texture_list);
                     Py_DECREF(textures_dict);
                     Py_DECREF(mat_dict);
                     goto fail_mat_list;
                 }
             }
             // Use the texture type enum value as the key
             PyObject *py_tex_type_key = PyLong_FromLong(tex_type);
             if (!py_tex_type_key) {
                 Py_DECREF(texture_list); // List contains owned refs now
                 Py_DECREF(textures_dict);
                 Py_DECREF(mat_dict);
                 goto fail_mat_list;
             }
             if (PyDict_SetItem(textures_dict, py_tex_type_key, texture_list) < 0) {
                 Py_DECREF(py_tex_type_key);
                 Py_DECREF(texture_list);
                 Py_DECREF(textures_dict);
                 Py_DECREF(mat_dict);
                 goto fail_mat_list;
             }
             Py_DECREF(py_tex_type_key);
             Py_DECREF(texture_list); // SetItem increments ref
        }

        // Add the textures dictionary to the main material dictionary
        PyObject *textures_key = PyUnicode_FromString("TEXTURES");
         if (!textures_key) {
             Py_DECREF(textures_dict);
             Py_DECREF(mat_dict);
             goto fail_mat_list;
         }
        if (PyDict_SetItem(mat_dict, textures_key, textures_dict) < 0) {
            Py_DECREF(textures_key);
            Py_DECREF(textures_dict);
            Py_DECREF(mat_dict);
            goto fail_mat_list;
        }
        Py_DECREF(textures_key);
        Py_DECREF(textures_dict); // SetItem increments ref

        // PyList_SetItem steals the reference to mat_dict
        if (PyList_SetItem(py_materials_list, i, mat_dict) < 0) {
            Py_DECREF(mat_dict); // Decref if SetItem failed
            goto fail_mat_list;
        }
    }

    return py_materials_list; // Success

fail_mat_list:
    Py_DECREF(py_materials_list); // Decrement ref count on the main list
    return NULL; // Error
}


// Process meshes from aiScene into a Python list of Mesh objects.
// Returns a new reference to the list, or NULL on error.
static PyObject* process_meshes(const struct aiScene *c_scene) {
    unsigned int num_meshes = c_scene->mNumMeshes;
    PyObject *py_meshes_list = PyList_New(num_meshes);
    if (!py_meshes_list) return NULL;

    for (unsigned int i = 0; i < num_meshes; ++i) {
        struct aiMesh *c_mesh = c_scene->mMeshes[i];
        Mesh *py_mesh = (Mesh *)MeshType.tp_alloc(&MeshType, 0); // Create new Mesh object
        if (!py_mesh) goto fail_mesh_list;

        // --- Basic Info ---
        py_mesh->name = PyUnicode_FromString(c_mesh->mName.data);
        if (!py_mesh->name) { Py_DECREF(py_mesh); goto fail_mesh_list; }
        py_mesh->num_vertices = c_mesh->mNumVertices;
        py_mesh->num_faces = c_mesh->mNumFaces;
        py_mesh->material_index = c_mesh->mMaterialIndex;

        size_t buffer_size; // Used for calculating allocation sizes
        Py_ssize_t itemsize; // For memoryview

        // --- Indices ---
        // Calculate total number of indices assuming triangulation (most common case)
        // If aiProcess_Triangulate is not used, this needs adjustment or checking mNumIndices per face.
        py_mesh->num_indices = 0;
        for (unsigned int f = 0; f < c_mesh->mNumFaces; ++f) {
            // Ensure faces are triangles if assuming flat index buffer
             if (c_mesh->mFaces[f].mNumIndices != 3) {
                 // Consider raising an error if triangulation wasn't enforced by flags
                 PyErr_SetString(PyExc_ValueError, "Mesh processing assumes triangulated faces (use aiProcess_Triangulate flag).");
                 Py_DECREF(py_mesh);
                 goto fail_mesh_list;
             }
            py_mesh->num_indices += c_mesh->mFaces[f].mNumIndices;
        }

        if (py_mesh->num_indices > 0) {
            buffer_size = py_mesh->num_indices * sizeof(unsigned int);
            itemsize = sizeof(unsigned int);
            py_mesh->c_indices = (unsigned int*)malloc(buffer_size);
            if (!py_mesh->c_indices) { PyErr_NoMemory(); Py_DECREF(py_mesh); goto fail_mesh_list; }

            unsigned int idx_count = 0;
            for (unsigned int f = 0; f < c_mesh->mNumFaces; ++f) {
                memcpy(py_mesh->c_indices + idx_count, c_mesh->mFaces[f].mIndices, c_mesh->mFaces[f].mNumIndices * sizeof(unsigned int));
                idx_count += c_mesh->mFaces[f].mNumIndices;
            }
            // Format 'I' is standard unsigned int
            py_mesh->indices = create_memoryview(py_mesh->c_indices, buffer_size, "I", itemsize);
            if (!py_mesh->indices) { Py_DECREF(py_mesh); goto fail_mesh_list; }
        } else {
            Py_INCREF(Py_None); py_mesh->indices = Py_None; // No indices
        }


        // --- Vertices ---
        if (c_mesh->mVertices) {
            buffer_size = py_mesh->num_vertices * 3 * sizeof(float);
            itemsize = sizeof(float);
            py_mesh->c_vertices = (float*)malloc(buffer_size);
            if (!py_mesh->c_vertices) { PyErr_NoMemory(); Py_DECREF(py_mesh); goto fail_mesh_list; }
            memcpy(py_mesh->c_vertices, c_mesh->mVertices, buffer_size);
            // Format 'f' is standard float
            py_mesh->vertices = create_memoryview(py_mesh->c_vertices, buffer_size, "f", itemsize);
             if (!py_mesh->vertices) { Py_DECREF(py_mesh); goto fail_mesh_list; }
        } else {
             Py_INCREF(Py_None); py_mesh->vertices = Py_None; // Should not happen for valid mesh
        }

        // --- Normals ---
        if (c_mesh->mNormals) {
            buffer_size = py_mesh->num_vertices * 3 * sizeof(float);
            itemsize = sizeof(float);
            py_mesh->c_normals = (float*)malloc(buffer_size);
            if (!py_mesh->c_normals) { PyErr_NoMemory(); Py_DECREF(py_mesh); goto fail_mesh_list; }
            memcpy(py_mesh->c_normals, c_mesh->mNormals, buffer_size);
            py_mesh->normals = create_memoryview(py_mesh->c_normals, buffer_size, "f", itemsize);
             if (!py_mesh->normals) { Py_DECREF(py_mesh); goto fail_mesh_list; }
        } else {
             Py_INCREF(Py_None); py_mesh->normals = Py_None;
        }

        // --- Tangents ---
        if (c_mesh->mTangents) {
            buffer_size = py_mesh->num_vertices * 3 * sizeof(float);
            itemsize = sizeof(float);
            py_mesh->c_tangents = (float*)malloc(buffer_size);
            if (!py_mesh->c_tangents) { PyErr_NoMemory(); Py_DECREF(py_mesh); goto fail_mesh_list; }
            memcpy(py_mesh->c_tangents, c_mesh->mTangents, buffer_size);
            py_mesh->tangents = create_memoryview(py_mesh->c_tangents, buffer_size, "f", itemsize);
             if (!py_mesh->tangents) { Py_DECREF(py_mesh); goto fail_mesh_list; }
        } else {
             Py_INCREF(Py_None); py_mesh->tangents = Py_None;
        }

        // --- Bitangents ---
        if (c_mesh->mBitangents) {
            buffer_size = py_mesh->num_vertices * 3 * sizeof(float);
            itemsize = sizeof(float);
            py_mesh->c_bitangents = (float*)malloc(buffer_size);
            if (!py_mesh->c_bitangents) { PyErr_NoMemory(); Py_DECREF(py_mesh); goto fail_mesh_list; }
            memcpy(py_mesh->c_bitangents, c_mesh->mBitangents, buffer_size);
            py_mesh->bitangents = create_memoryview(py_mesh->c_bitangents, buffer_size, "f", itemsize);
             if (!py_mesh->bitangents) { Py_DECREF(py_mesh); goto fail_mesh_list; }
        } else {
             Py_INCREF(Py_None); py_mesh->bitangents = Py_None;
        }

        // --- Colors ---
        py_mesh->num_color_sets = 0;
        for(unsigned int k=0; k < AI_MAX_NUMBER_OF_COLOR_SETS; ++k) {
            if(c_mesh->mColors[k] != NULL) py_mesh->num_color_sets++; else break;
        }

        if (py_mesh->num_color_sets > 0) {
            py_mesh->colors = PyList_New(py_mesh->num_color_sets);
            if (!py_mesh->colors) { Py_DECREF(py_mesh); goto fail_mesh_list; }
            py_mesh->c_colors = (float**)calloc(py_mesh->num_color_sets, sizeof(float*)); // Use calloc for NULL init
             if (!py_mesh->c_colors) { PyErr_NoMemory(); Py_DECREF(py_mesh); goto fail_mesh_list; }

            for(unsigned int k=0; k < py_mesh->num_color_sets; ++k) {
                 // Colors are aiColor4D (r,g,b,a) -> 4 floats
                 buffer_size = py_mesh->num_vertices * 4 * sizeof(float);
                 itemsize = sizeof(float);
                 py_mesh->c_colors[k] = (float*)malloc(buffer_size);
                 if (!py_mesh->c_colors[k]) { PyErr_NoMemory(); Py_DECREF(py_mesh); goto fail_mesh_list; }
                 // Need to copy component-wise from aiColor4D
                 for(unsigned int v=0; v < py_mesh->num_vertices; ++v) {
                     py_mesh->c_colors[k][v*4 + 0] = c_mesh->mColors[k][v].r;
                     py_mesh->c_colors[k][v*4 + 1] = c_mesh->mColors[k][v].g;
                     py_mesh->c_colors[k][v*4 + 2] = c_mesh->mColors[k][v].b;
                     py_mesh->c_colors[k][v*4 + 3] = c_mesh->mColors[k][v].a;
                 }
                 PyObject *memview = create_memoryview(py_mesh->c_colors[k], buffer_size, "f", itemsize);
                 if (!memview) { Py_DECREF(py_mesh); goto fail_mesh_list; }
                 PyList_SET_ITEM(py_mesh->colors, k, memview); // Steals ref
            }
        } else {
             Py_INCREF(Py_None); py_mesh->colors = Py_None;
        }

        // --- Texture Coordinates ---
        py_mesh->num_texcoord_sets = 0;
        for(unsigned int k=0; k < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++k) {
            if(c_mesh->mTextureCoords[k] != NULL) py_mesh->num_texcoord_sets++; else break;
        }

        if (py_mesh->num_texcoord_sets > 0) {
            py_mesh->texcoords = PyList_New(py_mesh->num_texcoord_sets);
             if (!py_mesh->texcoords) { Py_DECREF(py_mesh); goto fail_mesh_list; }
            py_mesh->num_uv_components = PyList_New(py_mesh->num_texcoord_sets);
             if (!py_mesh->num_uv_components) { Py_DECREF(py_mesh); goto fail_mesh_list; }

            py_mesh->c_texcoords = (float**)calloc(py_mesh->num_texcoord_sets, sizeof(float*));
             if (!py_mesh->c_texcoords) { PyErr_NoMemory(); Py_DECREF(py_mesh); goto fail_mesh_list; }
            py_mesh->c_num_uv_components = (unsigned int*)malloc(py_mesh->num_texcoord_sets * sizeof(unsigned int));
             if (!py_mesh->c_num_uv_components) { PyErr_NoMemory(); Py_DECREF(py_mesh); goto fail_mesh_list; }


            for(unsigned int k=0; k < py_mesh->num_texcoord_sets; ++k) {
                 unsigned int ncomp = c_mesh->mNumUVComponents[k]; // 1, 2 or 3
                 py_mesh->c_num_uv_components[k] = ncomp;
                 PyObject *comp_obj = PyLong_FromUnsignedLong(ncomp);
                 if (!comp_obj) { Py_DECREF(py_mesh); goto fail_mesh_list; }
                 PyList_SET_ITEM(py_mesh->num_uv_components, k, comp_obj); // Steals ref

                 buffer_size = py_mesh->num_vertices * ncomp * sizeof(float);
                 itemsize = sizeof(float);
                 py_mesh->c_texcoords[k] = (float*)malloc(buffer_size);
                 if (!py_mesh->c_texcoords[k]) { PyErr_NoMemory(); Py_DECREF(py_mesh); goto fail_mesh_list; }

                 // Copy from aiVector3D, taking only ncomp components
                 for(unsigned int v=0; v < py_mesh->num_vertices; ++v) {
                    memcpy(py_mesh->c_texcoords[k] + v * ncomp, &(c_mesh->mTextureCoords[k][v].x), ncomp * sizeof(float));
                 }

                 PyObject *memview = create_memoryview(py_mesh->c_texcoords[k], buffer_size, "f", itemsize);
                 if (!memview) { Py_DECREF(py_mesh); goto fail_mesh_list; }
                 PyList_SET_ITEM(py_mesh->texcoords, k, memview); // Steals ref
            }
        } else {
             Py_INCREF(Py_None); py_mesh->texcoords = Py_None;
             py_mesh->num_uv_components = PyList_New(0); // Empty list if no texcoords
             if (!py_mesh->num_uv_components) { Py_DECREF(py_mesh); goto fail_mesh_list; }
        }


        // --- Add Mesh to List ---
        // PyList_SetItem steals the reference, no DECREF needed on success
        if (PyList_SetItem(py_meshes_list, i, (PyObject*)py_mesh) < 0) {
             Py_DECREF(py_mesh); // Decref if SetItem failed
             goto fail_mesh_list;
        }
    }

    return py_meshes_list; // Success

fail_mesh_list:
    Py_DECREF(py_meshes_list); // Clean up the main list on error
    return NULL;
}


// --- Module Methods ---

PyDoc_STRVAR(import_file_doc,
"import_file(filename: str, flags: int) -> Scene\n"
"--\n\n"
"Imports the 3D model from the given filename.\n\n"
"Args:\n"
"    filename: Path to the model file.\n"
"    flags: Post-processing flags (e.g., Process_Triangulate | Process_GenNormals).\n\n"
"Returns:\n"
"    A Scene object containing the loaded data.\n\n"
"Raises:\n"
"    FileNotFoundError: If the file does not exist.\n"
"    RuntimeError: If Assimp fails to load the file.\n"
"    MemoryError: If memory allocation fails.\n"
"    ValueError: If arguments are invalid or mesh data is inconsistent (e.g., non-triangulated when expected).");

static PyObject* py_import_file(PyObject *self, PyObject *args) {
    const char* filename = NULL;
    unsigned int flags = 0;
    const struct aiScene *c_scene = NULL;
    Scene *py_scene = NULL; // The Python Scene object we will return

    if (!PyArg_ParseTuple(args, "sI:import_file", &filename, &flags)) {
        // Error already set by PyArg_ParseTuple
        return NULL;
    }

    // Basic check if file exists before calling Assimp
    // Use Python's built-in os.path.exists for better cross-platform compatibility?
    // For C extension, fopen is reasonable.
    FILE *f = fopen(filename, "rb"); // Use "rb" for binary check
    if (!f) {
        // Map C's file not found to Python's FileNotFoundError
        // Note: Python 3.3+ needed for FileNotFoundError. Use IOError for older.
        PyErr_SetString(PyExc_FileNotFoundError, filename);
        return NULL;
    }
    fclose(f);

    // Import the file using Assimp
    // aiProcess_Triangulate is highly recommended for predictable index buffers
    // aiProcess_JoinIdenticalVertices is useful for reducing vertex count
    // aiProcess_CalcTangentSpace is needed if you require tangents/bitangents
    // aiProcess_GenSmoothNormals or aiProcess_GenNormals if normals are missing
    // aiProcess_FlipUVs can be important depending on texture conventions
    c_scene = aiImportFile(filename, flags);

    // Check for Assimp loading errors
    if (!c_scene || !c_scene->mRootNode || (c_scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        PyErr_Format(PyExc_RuntimeError, "Assimp error loading '%s': %s", filename, aiGetErrorString());
        aiReleaseImport(c_scene); // Release even if partially loaded
        return NULL;
    }

    // Create the Python Scene object
    py_scene = (Scene *)SceneType.tp_alloc(&SceneType, 0);
    if (!py_scene) {
        aiReleaseImport(c_scene);
        return NULL; // Error already set (likely MemoryError)
    }

    py_scene->num_meshes = c_scene->mNumMeshes;
    py_scene->num_materials = c_scene->mNumMaterials;

    // Process Meshes
    py_scene->meshes = process_meshes(c_scene);
    if (!py_scene->meshes) {
        goto fail; // Error occurred during mesh processing
    }

    // Process Materials
    py_scene->materials = process_materials(c_scene);
    if (!py_scene->materials) {
        goto fail; // Error occurred during material processing
    }

    // Success! Release the C scene and return the Python scene
    aiReleaseImport(c_scene);
    return (PyObject *)py_scene;

fail:
    // Cleanup on error
    aiReleaseImport(c_scene);
    Py_XDECREF(py_scene); // Use XDECREF as py_scene might be NULL if allocation failed
    return NULL;
}


// --- Module Definition ---

static PyMethodDef assimp_py_methods[] = {
    {"import_file", py_import_file, METH_VARARGS, import_file_doc},
    {NULL, NULL, 0, NULL} /* Sentinel */
};

// Function to add integer constants to the module
static int add_int_constant(PyObject *module, const char *name, long value) {
    if (PyModule_AddIntConstant(module, name, value) < 0) {
        fprintf(stderr, "Failed to add constant: %s\n", name);
        return -1;
    }
    return 0;
}


static PyModuleDef assimp_py_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "assimp_py",
    .m_doc = "Python C bindings for the Assimp library",
    .m_size = -1, // No global state for this module
    .m_methods = assimp_py_methods,
};

PyMODINIT_FUNC PyInit_assimp_py(void) {
    PyObject *module = NULL;

    // Initialize Types
    if (PyType_Ready(&MeshType) < 0) return NULL;
    if (PyType_Ready(&SceneType) < 0) return NULL;

    // Create Module
    module = PyModule_Create(&assimp_py_module);
    if (!module) {
        Py_DECREF(&MeshType); // Need cleanup if module creation fails
        Py_DECREF(&SceneType);
        return NULL;
    }

    // Add Types to Module
    Py_INCREF(&MeshType); // Module takes ownership
    if (PyModule_AddObject(module, "Mesh", (PyObject *)&MeshType) < 0) {
        Py_DECREF(&MeshType);
        Py_DECREF(&SceneType);
        Py_DECREF(module);
        return NULL;
    }

    Py_INCREF(&SceneType); // Module takes ownership
    if (PyModule_AddObject(module, "Scene", (PyObject *)&SceneType) < 0) {
        Py_DECREF(&MeshType);
        Py_DECREF(&SceneType);
        Py_DECREF(module);
        return NULL;
    }

    // Add Constants (Post-processing flags) - Abbreviated list for example
    int error = 0;
    error |= add_int_constant(module, "Process_CalcTangentSpace", aiProcess_CalcTangentSpace);
    error |= add_int_constant(module, "Process_JoinIdenticalVertices", aiProcess_JoinIdenticalVertices);
    error |= add_int_constant(module, "Process_MakeLeftHanded", aiProcess_MakeLeftHanded);
    error |= add_int_constant(module, "Process_Triangulate", aiProcess_Triangulate);
    error |= add_int_constant(module, "Process_RemoveComponent", aiProcess_RemoveComponent);
    error |= add_int_constant(module, "Process_GenNormals", aiProcess_GenNormals);
    error |= add_int_constant(module, "Process_GenSmoothNormals", aiProcess_GenSmoothNormals);
    error |= add_int_constant(module, "Process_SplitLargeMeshes", aiProcess_SplitLargeMeshes);
    error |= add_int_constant(module, "Process_PreTransformVertices", aiProcess_PreTransformVertices);
    error |= add_int_constant(module, "Process_LimitBoneWeights", aiProcess_LimitBoneWeights);
    error |= add_int_constant(module, "Process_ValidateDataStructure", aiProcess_ValidateDataStructure);
    error |= add_int_constant(module, "Process_ImproveCacheLocality", aiProcess_ImproveCacheLocality);
    error |= add_int_constant(module, "Process_RemoveRedundantMaterials", aiProcess_RemoveRedundantMaterials);
    error |= add_int_constant(module, "Process_FixInfacingNormals", aiProcess_FixInfacingNormals);
    error |= add_int_constant(module, "Process_SortByPType", aiProcess_SortByPType);
    error |= add_int_constant(module, "Process_FindDegenerates", aiProcess_FindDegenerates);
    error |= add_int_constant(module, "Process_FindInvalidData", aiProcess_FindInvalidData);
    error |= add_int_constant(module, "Process_GenUVCoords", aiProcess_GenUVCoords);
    error |= add_int_constant(module, "Process_TransformUVCoords", aiProcess_TransformUVCoords);
    error |= add_int_constant(module, "Process_FindInstances", aiProcess_FindInstances);
    error |= add_int_constant(module, "Process_OptimizeMeshes", aiProcess_OptimizeMeshes);
    error |= add_int_constant(module, "Process_OptimizeGraph", aiProcess_OptimizeGraph);
    error |= add_int_constant(module, "Process_FlipUVs", aiProcess_FlipUVs);
    error |= add_int_constant(module, "Process_FlipWindingOrder", aiProcess_FlipWindingOrder);
    error |= add_int_constant(module, "Process_SplitByBoneCount", aiProcess_SplitByBoneCount);
    error |= add_int_constant(module, "Process_Debone", aiProcess_Debone);
    error |= add_int_constant(module, "Process_GlobalScale", aiProcess_GlobalScale);
    // Add Texture Type constants
    error |= add_int_constant(module, "TextureType_NONE", aiTextureType_NONE);
    error |= add_int_constant(module, "TextureType_DIFFUSE", aiTextureType_DIFFUSE);
    error |= add_int_constant(module, "TextureType_SPECULAR", aiTextureType_SPECULAR);
    error |= add_int_constant(module, "TextureType_AMBIENT", aiTextureType_AMBIENT);
    error |= add_int_constant(module, "TextureType_EMISSIVE", aiTextureType_EMISSIVE);
    error |= add_int_constant(module, "TextureType_HEIGHT", aiTextureType_HEIGHT);
    error |= add_int_constant(module, "TextureType_NORMALS", aiTextureType_NORMALS);
    error |= add_int_constant(module, "TextureType_SHININESS", aiTextureType_SHININESS);
    error |= add_int_constant(module, "TextureType_OPACITY", aiTextureType_OPACITY);
    error |= add_int_constant(module, "TextureType_DISPLACEMENT", aiTextureType_DISPLACEMENT);
    error |= add_int_constant(module, "TextureType_LIGHTMAP", aiTextureType_LIGHTMAP);
    error |= add_int_constant(module, "TextureType_REFLECTION", aiTextureType_REFLECTION);
    error |= add_int_constant(module, "TextureType_UNKNOWN", aiTextureType_UNKNOWN);


    if (error < 0) {
        // Cleanup if adding constants failed
        Py_DECREF(&MeshType);
        Py_DECREF(&SceneType);
        Py_DECREF(module);
        PyErr_SetString(PyExc_RuntimeError, "Failed to initialize assimp_py constants");
        return NULL;
    }


    return module;
}