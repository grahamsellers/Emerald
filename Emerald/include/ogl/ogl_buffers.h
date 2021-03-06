/**
 *
 * Emerald (kbi/elude @2015)
 *
 * This module can be used optionally. It is envisioned to act as an abstraction layer
 * between GL and the applications, exposing an unified interface to perform server-side
 * memory region allocations.
 *
 *
 * If sparse buffers are supported, ogl_buffers carves slices of buffer memory out
 * of cached sparse buffers, which are stored in an internal pool.
 *
 * If not, a pool of buffer objects is preallocated at creation time, and filled with
 * data during execution. Alloc requests whose size is larger than the size of each
 * preallocated BO get their exclusive BO.
 *
 *
 * The instantiated regions come from immutable buffer objects.
 *
 * Finally, since ogl_buffers keeps track of all allocated buffers, it can also provide
 * info regarding:
 *
 * a) committed buffer memory (if sparse buffers are supported)
 * b) reserved buffer memory  (otherwise)
 *
 * TODO: Buffer coherency is currently not taken into consideration. Add if needed.
 */
#ifndef OGL_BUFFERS_H
#define OGL_BUFFERS_H

#include "ogl/ogl_types.h"
#include "system/system_types.h"

REFCOUNT_INSERT_DECLARATIONS(ogl_buffers,
                             ogl_buffers)

typedef enum
{
    OGL_BUFFERS_MAPPABILITY_NONE,
    OGL_BUFFERS_MAPPABILITY_READ_OPS_ONLY,
    OGL_BUFFERS_MAPPABILITY_READ_AND_WRITE_OPS,
    OGL_BUFFERS_MAPPABILITY_WRITE_OPS_ONLY,

    OGL_BUFFERS_MAPPABILITY_UNDEFINED
} _ogl_buffers_mappability;

typedef enum
{
    OGL_BUFFERS_USAGE_IBO,
    OGL_BUFFERS_USAGE_MISCELLANEOUS,
    OGL_BUFFERS_USAGE_UBO,
    OGL_BUFFERS_USAGE_VBO,

    /* Always last */
    OGL_BUFFERS_USAGE_COUNT
} _ogl_buffers_usage;

/* The buffer memory can come from a sparse buffer, or an immutable buffer object */
const unsigned int OGL_BUFFERS_FLAGS_NONE                        = 0;
/* The buffer memory must come from an immutable buffer object */
const unsigned int OGL_BUFFERS_FLAGS_IMMUTABLE_BUFFER_MEMORY_BIT = 1 << 0;
/* TODO: The buffer memory must come from a sparse buffer object */


/** TODO */
PUBLIC EMERALD_API bool ogl_buffers_allocate_buffer_memory(ogl_buffers              buffers,
                                                           unsigned int             size,
                                                           unsigned int             alignment_requirement,
                                                           _ogl_buffers_mappability mappability,
                                                           _ogl_buffers_usage       usage,
                                                           int                      flags, /* bitfield of OGL_BUFFERS_FLAGS_ */
                                                           unsigned int*            out_bo_id_ptr,
                                                           unsigned int*            out_bo_offset_ptr);

/** TODO.
 *
 *  Internal usage only.
 **/
PUBLIC ogl_buffers ogl_buffers_create(ogl_context               context,
                                      system_hashed_ansi_string name);

/** TODO */
PUBLIC EMERALD_API void ogl_buffers_free_buffer_memory(ogl_buffers  buffers,
                                                       unsigned int bo_id,
                                                       unsigned int bo_offset);

#endif /* OGL_BUFFERS_H */
