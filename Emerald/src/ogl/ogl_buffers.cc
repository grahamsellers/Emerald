/**
 *
 * Emerald (kbi/elude @2015)
 *
 */
#include "shared.h"
#include "ogl/ogl_buffers.h"
#include "ogl/ogl_context.h"
#include "system/system_assertions.h"
#include "system/system_hash64map.h"
#include "system/system_log.h"
#include "system/system_memory_manager.h"
#include "system/system_resizable_vector.h"
#include "system/system_resource_pool.h"

#define NONSPARSE_IBO_BUFFER_SIZE  (16  * 1024768)
#define NONSPARSE_MISC_BUFFER_SIZE (16  * 1024768)
#define NONSPARSE_UBO_BUFFER_SIZE  (4   * 1024768)
#define NONSPARSE_VBO_BUFFER_SIZE  (64  * 1024768)
#define SPARSE_BUFFER_SIZE         (512 * 1024768)

#define MAKE_BO_HASHMAP_KEY(bo_id, bo_offset)           \
    ((int64_t(bo_id)     & ((1LL << 32) - 1))         | \
    ((int64_t(bo_offset) & ((1LL << 32) - 1)) << 32))


typedef struct _ogl_buffers_buffer
{
    GLuint                   bo_id;
    struct _ogl_buffers*     buffers_ptr;
    _ogl_buffers_mappability mappability;
    system_memory_manager    manager;
} _ogl_buffers_buffer;

typedef struct _ogl_buffers
{
    bool                                           are_sparse_buffers_in;
    system_hash64map                               bo_id_offset_hash_to_buffer_map;
    system_resource_pool                           buffer_descriptor_pool;
    ogl_context                                    context; /* do not retain - else face circular dependencies */

    ogl_context_es_entrypoints*                    entry_points_es;

    ogl_context_gl_entrypoints*                    entry_points_gl;
    ogl_context_gl_entrypoints_arb_buffer_storage* entry_points_gl_bs;
    ogl_context_gl_entrypoints_arb_sparse_buffer*  entry_points_gl_sb;

    system_hashed_ansi_string                      name;
    system_resizable_vector                        nonsparse_buffers[OGL_BUFFERS_USAGE_COUNT];
    unsigned int                                   page_size;
    system_resizable_vector                        sparse_buffers; /* sparse BOs are never mappable! */

    REFCOUNT_INSERT_VARIABLES


    _ogl_buffers(ogl_context               in_context,
                 system_hashed_ansi_string in_name);

    ~_ogl_buffers()
    {
        if (bo_id_offset_hash_to_buffer_map != NULL)
        {
            system_hash64map_release(bo_id_offset_hash_to_buffer_map);

            bo_id_offset_hash_to_buffer_map = NULL;
        }

        if (nonsparse_buffers != NULL)
        {
            for (unsigned int n_buffer_usage = 0;
                              n_buffer_usage < OGL_BUFFERS_USAGE_COUNT;
                            ++n_buffer_usage)
            {
                _ogl_buffers_buffer* buffer_ptr = NULL;

                /* Note: nonsparse buffer descriptors are carved out of the buffer descriptor pool.
                 *       The pool takes care of releasing the embedded GL objects at the release time.
                 *
                 *       Calling a destructor here would be a unforgivable mistake, but we still need
                 *       to return the descriptor to the pool.
                 */
                while (system_resizable_vector_pop(nonsparse_buffers[n_buffer_usage],
                                                  &buffer_ptr) )
                {
                    system_resource_pool_return_to_pool(buffer_descriptor_pool,
                                                        (system_resource_pool_block) buffer_ptr);
                }

                system_resizable_vector_release(nonsparse_buffers[n_buffer_usage]);

                nonsparse_buffers[n_buffer_usage] = NULL;
            } /* for (all buffer usage types) */
        } /* if (nonsparse_buffers != NULL) */

        if (sparse_buffers != NULL)
        {
            _ogl_buffers_buffer* buffer_ptr = NULL;

            /* The note above regarding the nonsparse buffer descriptors applies here, too. */
            while (system_resizable_vector_pop(sparse_buffers,
                                              &buffer_ptr) )
            {
                system_resource_pool_return_to_pool(buffer_descriptor_pool,
                                                    (system_resource_pool_block) buffer_ptr);
            } /* while (descriptors are available) */

            system_resizable_vector_release(sparse_buffers);

            sparse_buffers = NULL;
        } /* if (sparse_buffers != NULL) */

        /* The descriptor pool needs to be released at the end */
        if (buffer_descriptor_pool != NULL)
        {
            system_resource_pool_release(buffer_descriptor_pool);

            buffer_descriptor_pool = NULL;
        } /* if (buffer_descriptor_pool != NULL) */
    }
} _ogl_buffers;

/** Reference counter impl */
REFCOUNT_INSERT_IMPLEMENTATION(ogl_buffers,
                               ogl_buffers,
                              _ogl_buffers);


/** Forward declarations */
PRIVATE _ogl_buffers_buffer* _ogl_buffers_alloc_new_immutable_buffer    (_ogl_buffers*              buffers_ptr,
                                                                         _ogl_buffers_mappability   mappability,
                                                                         _ogl_buffers_usage         usage,
                                                                         unsigned int               required_size);
PRIVATE _ogl_buffers_buffer* _ogl_buffers_alloc_new_sparse_buffer       (_ogl_buffers*              buffers_ptr);
PRIVATE void                 _ogl_buffers_dealloc_sparse_buffer         (system_resource_pool_block buffer_block);
PRIVATE unsigned int         _ogl_buffers_get_nonsparse_buffer_size     (_ogl_buffers_usage         usage);
PRIVATE void                 _ogl_buffers_on_sparse_memory_block_alloced(system_memory_manager      manager,
                                                                         unsigned int               offset_aligned,
                                                                         unsigned int               size,
                                                                         void*                      user_arg);
PRIVATE void                 _ogl_buffers_on_sparse_memory_block_freed  (system_memory_manager      manager,
                                                                         unsigned int               offset_aligned,
                                                                         unsigned int               size,
                                                                         void*                      user_arg);
PRIVATE void                _ogl_buffers_release                        (void*                      buffers);


/** TODO */
_ogl_buffers::_ogl_buffers(ogl_context               in_context,
                           system_hashed_ansi_string in_name)
{
    are_sparse_buffers_in           = false;
    bo_id_offset_hash_to_buffer_map = system_hash64map_create    (sizeof(_ogl_buffers_buffer*) );
    buffer_descriptor_pool          = system_resource_pool_create(sizeof(_ogl_buffers_buffer),
                                                                  16,                                  /* n_elements_to_preallocate */
                                                                  NULL,                                /* init_fn */
                                                                  _ogl_buffers_dealloc_sparse_buffer); /* deinit_fn */
    context                         = in_context;
    entry_points_es                 = NULL;
    entry_points_gl                 = NULL;
    entry_points_gl_bs              = NULL;
    entry_points_gl_sb              = NULL;
    name                            = in_name;
    page_size                       = 0;
    sparse_buffers                  = system_resizable_vector_create(16); /* capacity */

    /* Initialize non-sparse buffer vectors */
    for (unsigned int n_buffer_usage = 0;
                      n_buffer_usage < OGL_BUFFERS_USAGE_COUNT;
                    ++n_buffer_usage)
    {
        nonsparse_buffers[n_buffer_usage] = system_resizable_vector_create(4); /* capacity */
    } /* for (all buffer usage types) */

    /* Cache the entry-points */
    ogl_context_type context_type = OGL_CONTEXT_TYPE_UNDEFINED;

    ogl_context_get_property(context,
                             OGL_CONTEXT_PROPERTY_TYPE,
                            &context_type);

    if (context_type == OGL_CONTEXT_TYPE_ES)
    {
        ogl_context_get_property(context,
                                 OGL_CONTEXT_PROPERTY_ENTRYPOINTS_ES,
                                &entry_points_es);
    } /* if (context_type == OGL_CONTEXT_TYPE_ES) */
    else
    {
        ogl_context_get_property(context,
                                 OGL_CONTEXT_PROPERTY_ENTRYPOINTS_GL,
                                &entry_points_gl);

        ogl_context_get_property(context,
                                 OGL_CONTEXT_PROPERTY_ENTRYPOINTS_GL_ARB_BUFFER_STORAGE,
                                &entry_points_gl_bs);

        /* Are sparse buffers supported? */
        ogl_context_get_property(context,
                                 OGL_CONTEXT_PROPERTY_SUPPORT_GL_ARB_SPARSE_BUFFERS,
                                &are_sparse_buffers_in);
    }

    if (are_sparse_buffers_in)
    {
        /* Retrieve entry-point & limits descriptors */
        const ogl_context_gl_limits_arb_sparse_buffer* limits_sb = NULL;

        ogl_context_get_property(context,
                                 OGL_CONTEXT_PROPERTY_ENTRYPOINTS_GL_ARB_SPARSE_BUFFER,
                                &entry_points_gl_sb);
        ogl_context_get_property(context,
                                 OGL_CONTEXT_PROPERTY_LIMITS_ARB_SPARSE_BUFFER,
                                &limits_sb);

        page_size = limits_sb->sparse_buffer_page_size;
    } /* if (are_sparse_buffers_in) */
}

/** TODO */
PRIVATE _ogl_buffers_buffer* _ogl_buffers_alloc_new_immutable_buffer(_ogl_buffers*            buffers_ptr,
                                                                     _ogl_buffers_mappability mappability,
                                                                     _ogl_buffers_usage       usage,
                                                                     unsigned int             required_size)
{
    unsigned int         bo_flags       = GL_DYNAMIC_STORAGE_BIT;
    unsigned int         bo_size        = _ogl_buffers_get_nonsparse_buffer_size(usage);
    GLenum               bo_target      = 0;
    _ogl_buffers_buffer* new_buffer_ptr = (_ogl_buffers_buffer*) system_resource_pool_get_from_pool(buffers_ptr->buffer_descriptor_pool);

    switch (usage)
    {
        /* non-mappable */
        case OGL_BUFFERS_USAGE_IBO:
        {
            bo_target = GL_ELEMENT_ARRAY_BUFFER;

            break;
        }

        case OGL_BUFFERS_USAGE_UBO:
        {
            bo_target = GL_UNIFORM_BUFFER;

            break;
        }

        case OGL_BUFFERS_USAGE_VBO:
        {
            bo_target = GL_ARRAY_BUFFER;

            break;
        }

        /* can either be mappable or non-mappable */
        case OGL_BUFFERS_USAGE_MISCELLANEOUS:
        {
            bo_flags |= (mappability == OGL_BUFFERS_MAPPABILITY_READ_AND_WRITE_OPS) ? (GL_MAP_READ_BIT | GL_MAP_WRITE_BIT) :
                        (mappability == OGL_BUFFERS_MAPPABILITY_READ_OPS_ONLY)      ? (GL_MAP_READ_BIT)                    :
                        (mappability == OGL_BUFFERS_MAPPABILITY_WRITE_OPS_ONLY)     ? (GL_MAP_WRITE_BIT)                   :
                        0;

            bo_target = GL_ARRAY_BUFFER;

            break;
        }

        default:
        {
            ASSERT_DEBUG_SYNC(false,
                              "Unrecognized _ogl_buffers_usage value.");
        }
    } /* switch (usage) */

    /* If the caller demands a larger buffer, scale up the planned BO size */
    if (bo_size < required_size)
    {
        bo_size = required_size;
    }

    if (buffers_ptr->entry_points_gl != NULL)
    {
        ASSERT_DEBUG_SYNC(buffers_ptr->entry_points_gl_bs != NULL,
                          "Immutable buffer support is missing. Crash ahead.");

        buffers_ptr->entry_points_gl->pGLGenBuffers      (1,
                                                         &new_buffer_ptr->bo_id);
        buffers_ptr->entry_points_gl->pGLBindBuffer      (bo_target,
                                                          new_buffer_ptr->bo_id);
        buffers_ptr->entry_points_gl_bs->pGLBufferStorage(bo_target,
                                                          bo_size,
                                                          NULL, /* data */
                                                          bo_flags);
    }
    else
    {
        buffers_ptr->entry_points_es->pGLGenBuffers(1,
                                                   &new_buffer_ptr->bo_id);
        buffers_ptr->entry_points_es->pGLBindBuffer(bo_target,
                                                    new_buffer_ptr->bo_id);
        buffers_ptr->entry_points_es->pGLBufferData(bo_target,
                                                    bo_size,
                                                    NULL,           /* data */
                                                    GL_STATIC_DRAW);
    }

    /* Check if the request was successful */
    GLint64 reported_bo_size = 0;

    if (buffers_ptr->entry_points_gl != NULL)
    {
        buffers_ptr->entry_points_gl->pGLGetBufferParameteri64v(bo_target,
                                                                GL_BUFFER_SIZE,
                                                               &reported_bo_size);
    }
    else
    {
        buffers_ptr->entry_points_es->pGLGetBufferParameteri64v(bo_target,
                                                                GL_BUFFER_SIZE,
                                                               &reported_bo_size);
    }

    if (reported_bo_size >= bo_size)
    {
        new_buffer_ptr->buffers_ptr = buffers_ptr;
        new_buffer_ptr->mappability = mappability;
        new_buffer_ptr->manager     = system_memory_manager_create(bo_size,
                                                                   1,             /* page_size - immutable buffers have no special requirements */
                                                                   NULL,          /* pfn_on_memory_block_alloced */
                                                                   NULL,          /* pfn_on_memory_block_freed */
                                                                   new_buffer_ptr,
                                                                   false);        /* should_be_thread_safe */

        system_resizable_vector_push(new_buffer_ptr->buffers_ptr->nonsparse_buffers[usage],
                                     new_buffer_ptr);
    } /* if (reported_bo_size >= bo_size) */
    else
    {
        /* BO allocation failure! */
        ASSERT_DEBUG_SYNC(false,
                          "Failed to allocate storage for an immutable buffer object.");

        if (buffers_ptr->entry_points_gl != NULL)
        {
            buffers_ptr->entry_points_gl->pGLDeleteBuffers(1,
                                                          &new_buffer_ptr->bo_id);
        }
        else
        {
            buffers_ptr->entry_points_es->pGLDeleteBuffers(1,
                                                          &new_buffer_ptr->bo_id);
        }

        system_resource_pool_return_to_pool(buffers_ptr->buffer_descriptor_pool,
                                            (system_resource_pool_block) new_buffer_ptr);

        new_buffer_ptr = NULL;
    }

    return new_buffer_ptr;
}

/** TODO */
PRIVATE _ogl_buffers_buffer* _ogl_buffers_alloc_new_sparse_buffer(_ogl_buffers* buffers_ptr)
{
    _ogl_buffers_buffer* new_buffer_ptr = (_ogl_buffers_buffer*) system_resource_pool_get_from_pool(buffers_ptr->buffer_descriptor_pool);

    ASSERT_DEBUG_SYNC(buffers_ptr->entry_points_gl    != NULL &&
                      buffers_ptr->entry_points_gl_bs != NULL,
                      "GL_ARB_buffer_storage and/or GL entry-points are unavailable.");

    buffers_ptr->entry_points_gl->pGLGenBuffers      (1,
                                                      &new_buffer_ptr->bo_id);
    buffers_ptr->entry_points_gl->pGLBindBuffer      (GL_ARRAY_BUFFER,
                                                      new_buffer_ptr->bo_id);
    buffers_ptr->entry_points_gl_bs->pGLBufferStorage(GL_ARRAY_BUFFER,
                                                      SPARSE_BUFFER_SIZE,
                                                      NULL, /* data */
                                                      GL_SPARSE_STORAGE_BIT_ARB |
                                                      GL_DYNAMIC_STORAGE_BIT);

    /* Check if the request was successful */
    GLint64 reported_bo_size = 0;

    buffers_ptr->entry_points_gl->pGLGetBufferParameteri64v(GL_ARRAY_BUFFER,
                                                            GL_BUFFER_SIZE,
                                                           &reported_bo_size);

    if (reported_bo_size >= SPARSE_BUFFER_SIZE)
    {
        new_buffer_ptr->buffers_ptr = buffers_ptr;
        new_buffer_ptr->mappability = OGL_BUFFERS_MAPPABILITY_NONE;
        new_buffer_ptr->manager     = system_memory_manager_create(SPARSE_BUFFER_SIZE,
                                                                   buffers_ptr->page_size,
                                                                   _ogl_buffers_on_sparse_memory_block_alloced,
                                                                   _ogl_buffers_on_sparse_memory_block_freed,
                                                                   new_buffer_ptr,
                                                                   false); /* should_be_thread_safe */

        system_resizable_vector_push(new_buffer_ptr->buffers_ptr->sparse_buffers,
                                     new_buffer_ptr);
    }
    else
    {
        /* BO allocation failure! */
        ASSERT_DEBUG_SYNC(false,
                          "Failed to allocate storage for an immutable buffer object.");

        buffers_ptr->entry_points_gl->pGLDeleteBuffers(1,
                                                      &new_buffer_ptr->bo_id);

        system_resource_pool_return_to_pool(buffers_ptr->buffer_descriptor_pool,
                                            (system_resource_pool_block) new_buffer_ptr);

        new_buffer_ptr = NULL;
    }

    return new_buffer_ptr;
}

/** TODO */
PRIVATE void _ogl_buffers_dealloc_sparse_buffer(system_resource_pool_block buffer_block)
{
    _ogl_buffers_buffer* buffer_ptr = (_ogl_buffers_buffer*) buffer_block;

    if (buffer_ptr->bo_id != 0)
    {
        if (buffer_ptr->buffers_ptr->entry_points_gl != NULL)
        {
            buffer_ptr->buffers_ptr->entry_points_gl->pGLDeleteBuffers(1,
                                                                      &buffer_ptr->bo_id);
        }
        else
        {
            ASSERT_DEBUG_SYNC(buffer_ptr->buffers_ptr->entry_points_es != NULL,
                              "ES and GL entry-point descriptors are unavailable.");

            buffer_ptr->buffers_ptr->entry_points_es->pGLDeleteBuffers(1,
                                                                      &buffer_ptr->bo_id);
        }

        buffer_ptr->bo_id = 0;
    }

    if (buffer_ptr->manager != NULL)
    {
        system_memory_manager_release(buffer_ptr->manager);

        buffer_ptr->manager = NULL;
    }
}

/** TODO */
PRIVATE unsigned int _ogl_buffers_get_nonsparse_buffer_size(_ogl_buffers_usage usage)
{
    unsigned int result = 0;

    switch (usage)
    {
        /* non-mappable */
        case OGL_BUFFERS_USAGE_IBO:
        {
            result = NONSPARSE_IBO_BUFFER_SIZE;

            break;
        }

        case OGL_BUFFERS_USAGE_UBO:
        {
            result = NONSPARSE_UBO_BUFFER_SIZE;

            break;
        }

        case OGL_BUFFERS_USAGE_VBO:
        {
            result = NONSPARSE_VBO_BUFFER_SIZE;

            break;
        }

        case OGL_BUFFERS_USAGE_MISCELLANEOUS:
        {
            result = NONSPARSE_MISC_BUFFER_SIZE;

            break;
        }

        default:
        {
            ASSERT_DEBUG_SYNC(false,
                              "Unrecognized _ogl_buffers_usage value.");
        }
    } /* switch (usage) */

    return result;
}

/** TODO */
PRIVATE void _ogl_buffers_on_sparse_memory_block_alloced(system_memory_manager manager,
                                                         unsigned int          offset_aligned,
                                                         unsigned int          size,
                                                         void*                 user_arg)
{
    _ogl_buffers_buffer* buffer_ptr = (_ogl_buffers_buffer*) user_arg;

    /* Commit the pages the caller will later fill with data. */
    ASSERT_DEBUG_SYNC(buffer_ptr->buffers_ptr->entry_points_gl_sb != NULL,
                      "GL_ARB_sparse_buffer entry-points are unavailable.");

    buffer_ptr->buffers_ptr->entry_points_gl_sb->pGLNamedBufferPageCommitmentEXT(buffer_ptr->bo_id,
                                                                                 offset_aligned,
                                                                                 size,
                                                                                 GL_TRUE); /* commit */
}

/** TODO */
PRIVATE void _ogl_buffers_on_sparse_memory_block_freed(system_memory_manager manager,
                                                       unsigned int          offset_aligned,
                                                       unsigned int          size,
                                                       void*                 user_arg)
{
    _ogl_buffers_buffer* buffer_ptr = (_ogl_buffers_buffer*) user_arg;

    /* Release the pages we no longer need.
     *
     * Note: system_memory_manager ensures the pages that we are told to release no longer hold
     *       any other data, so we need not worry about damaging data owned by other allocations.
     */
    ASSERT_DEBUG_SYNC(buffer_ptr->buffers_ptr->entry_points_gl_sb != NULL,
                      "GL_ARB_sparse_buffer entry-points are unavailable.");

    buffer_ptr->buffers_ptr->entry_points_gl_sb->pGLNamedBufferPageCommitmentEXT(buffer_ptr->bo_id,
                                                                                 offset_aligned,
                                                                                 size,
                                                                                 GL_FALSE); /* commit */
}

/** TODO */
PRIVATE void _ogl_buffers_release(void* buffers)
{
    /* Stub. The reference counter impl will call the actual destructor */
}

/** Please see header for spec */
PUBLIC EMERALD_API bool ogl_buffers_allocate_buffer_memory(ogl_buffers              buffers,
                                                           unsigned int             size,
                                                           unsigned int             alignment_requirement,
                                                           _ogl_buffers_mappability mappability,
                                                           _ogl_buffers_usage       usage,
                                                           int                      flags, /* bitfield of OGL_BUFFERS_FLAGS_ */
                                                           unsigned int*            out_bo_id_ptr,
                                                           unsigned int*            out_bo_offset_ptr)
{
    _ogl_buffers_buffer* buffer_ptr                 = NULL;
    _ogl_buffers*        buffers_ptr                = (_ogl_buffers*) buffers;
    const bool           must_be_immutable_bo_based = (flags & OGL_BUFFERS_FLAGS_IMMUTABLE_BUFFER_MEMORY_BIT) != 0;
    bool                 result                     = false;
    unsigned int         result_id                  = 0;
    unsigned int         result_offset              = -1;

    /* Check the sparse buffers first, if these are supported and no mappability
     * was requested. */
    if (buffers_ptr->are_sparse_buffers_in                                 &&
       !must_be_immutable_bo_based                                         &&
        mappability                        == OGL_BUFFERS_MAPPABILITY_NONE)
    {
        /* Try to find a suitable sparse buffer */
        unsigned int n_sparse_buffers = 0;

        system_resizable_vector_get_property(buffers_ptr->sparse_buffers,
                                             SYSTEM_RESIZABLE_VECTOR_PROPERTY_N_ELEMENTS,
                                            &n_sparse_buffers);

        for (unsigned int n_sparse_buffer = 0;
                          n_sparse_buffer < n_sparse_buffers && !result;
                        ++n_sparse_buffer)
        {
            if (system_resizable_vector_get_element_at(buffers_ptr->sparse_buffers,
                                                       n_sparse_buffer,
                                                      &buffer_ptr) )
            {
                ASSERT_DEBUG_SYNC(buffer_ptr->mappability == OGL_BUFFERS_MAPPABILITY_NONE,
                                  "Sanity check failed");

                result = system_memory_manager_alloc_block(buffer_ptr->manager,
                                                           size,
                                                           alignment_requirement,
                                                          &result_offset);

                if (result)
                {
                    result_id = buffer_ptr->bo_id;
                } /* if (result) */
            } /* if (sparse descriptor found) */
            else
            {
                ASSERT_DEBUG_SYNC(false,
                                  "Could not retrieve sparse buffer descriptor");
            }
        } /* for (all sparse buffers) */

        /* No free sparse buffer available? Create a new one IF the requested size does not
         * exceed the sparse buffer size for the requested buffer usage type. */
        if (!result)
        {
            if (size < SPARSE_BUFFER_SIZE)
            {
                buffer_ptr = _ogl_buffers_alloc_new_sparse_buffer(buffers_ptr);

                if (buffer_ptr != NULL)
                {
                    result = system_memory_manager_alloc_block(buffer_ptr->manager,
                                                               size,
                                                               alignment_requirement,
                                                              &result_offset);

                    if (result)
                    {
                        result_id = buffer_ptr->bo_id;
                    } /* if (result) */
                    else
                    {
                        ASSERT_DEBUG_SYNC(false,
                                          "Memory block allocation failed in a brand new sparse buffer.");
                    }
                } /* if (buffer_ptr != NULL) */
                else
                {
                    ASSERT_DEBUG_SYNC(false,
                                      "Sparse buffer allocation failed.");
                }
            }/* if (size < SPARSE_BUFFER_SIZE) */
            else
            {
                ASSERT_DEBUG_SYNC(false,
                                  "Requested buffer size is larger than the permitted sparse buffer size! "
                                  "Falling back to immutable buffer objects");
            }
        } /* if (!result) */
    } /* if (can use a sparse buffer) */

    if (!result)
    {
        /* Iterate over all non-sparse buffers we have cached so far. */
        ASSERT_DEBUG_SYNC(usage < OGL_BUFFERS_USAGE_COUNT,
                          "Invalid usage enum supplied.");

        unsigned int n_nonsparse_buffers = 0;

        system_resizable_vector_get_property(buffers_ptr->nonsparse_buffers[usage],
                                             SYSTEM_RESIZABLE_VECTOR_PROPERTY_N_ELEMENTS,
                                            &n_nonsparse_buffers);

        for (unsigned int n_nonsparse_buffer = 0;
                          n_nonsparse_buffer < n_nonsparse_buffers && !result;
                        ++n_nonsparse_buffer)
        {
            if (system_resizable_vector_get_element_at(buffers_ptr->nonsparse_buffers[usage],
                                                       n_nonsparse_buffer,
                                                      &buffer_ptr) )
            {
                /* Try to allocate the requested memory region */
                result = system_memory_manager_alloc_block(buffer_ptr->manager,
                                                           size,
                                                           alignment_requirement,
                                                          &result_offset);

                if (result)
                {
                    result_id = buffer_ptr->bo_id;
                }
            } /* if (non-sparse descriptor was found) */
            else
            {
                ASSERT_DEBUG_SYNC(false,
                                  "Could not retrieve a non-sparse buffer descriptor at index [%d]",
                                  n_nonsparse_buffer);
            }
        } /* for (all non-sparse buffers defined) */

        /* No suitable BO found? Try instantiating a new one */
        if (!result)
        {
            buffer_ptr = _ogl_buffers_alloc_new_immutable_buffer(buffers_ptr,
                                                                 mappability,
                                                                 usage,
                                                                 size);

            if (buffer_ptr != NULL)
            {
                result = system_memory_manager_alloc_block(buffer_ptr->manager,
                                                           size,
                                                           alignment_requirement,
                                                          &result_offset);

                if (result)
                {
                    result_id = buffer_ptr->bo_id;
                }
                else
                {
                    ASSERT_DEBUG_SYNC(false,
                                      "Could not allocate a new immutable buffer");
                }
            } /* if (buffer_ptr != NULL) */
        } /* if (!result) */
    } /* if (!result) */

    LOG_ERROR("ogl_buffers_allocate_buffer_memory(): size:[%d] alignment_requirement:[%d] => BO id:[%d] offset:[%d]",
              size,
              alignment_requirement,
              result_id,
              result_offset);

    if (result)
    {
        ASSERT_DEBUG_SYNC(result_id     != 0 &&
                          result_offset != -1,
                          "Sanity check failed");

        *out_bo_id_ptr     = result_id;
        *out_bo_offset_ptr = result_offset;

        system_hash64map_insert(buffers_ptr->bo_id_offset_hash_to_buffer_map,
                                MAKE_BO_HASHMAP_KEY(result_id,
                                                    result_offset),
                                buffer_ptr,
                                NULL,       /* on_remove_callback */
                                NULL);      /* on_remove_callback_user_arg */
    }

    return result;
}

/** Please see header for spec */
PUBLIC ogl_buffers ogl_buffers_create(ogl_context               context,
                                      system_hashed_ansi_string name)
{
    _ogl_buffers* new_buffers = new (std::nothrow) _ogl_buffers(context,
                                                                name);

    ASSERT_DEBUG_SYNC(new_buffers != NULL,
                      "Out of memory");

    if (new_buffers != NULL)
    {
        REFCOUNT_INSERT_INIT_CODE_WITH_RELEASE_HANDLER(new_buffers,
                                                       _ogl_buffers_release,
                                                       OBJECT_TYPE_OGL_BUFFERS,
                                                       system_hashed_ansi_string_create_by_merging_two_strings("\\OpenGL Buffer Managers\\",
                                                                                                               system_hashed_ansi_string_get_buffer(name)) );

        /* If sparse buffers are supported, allocate a single sparse buffer */
        if (new_buffers->are_sparse_buffers_in)
        {
            _ogl_buffers_alloc_new_sparse_buffer(new_buffers);
        } /* if (new_buffers->are_sparse_buffers_in) */
        else
        {
            for (unsigned int n_buffer_usage = 0;
                              n_buffer_usage < OGL_BUFFERS_USAGE_COUNT;
                            ++n_buffer_usage)
            {
                _ogl_buffers_mappability mappability = (n_buffer_usage == OGL_BUFFERS_USAGE_MISCELLANEOUS) ? OGL_BUFFERS_MAPPABILITY_READ_AND_WRITE_OPS
                                                                                                           : OGL_BUFFERS_MAPPABILITY_NONE;

                 _ogl_buffers_alloc_new_immutable_buffer(new_buffers,
                                                         mappability,
                                                         (_ogl_buffers_usage) n_buffer_usage,
                                                         _ogl_buffers_get_nonsparse_buffer_size( (_ogl_buffers_usage) n_buffer_usage) );
            }
        }
    } /* if (new_buffers != NULL) */

    return (ogl_buffers) new_buffers;
}

/** Please see header for spec */
PUBLIC EMERALD_API void ogl_buffers_free_buffer_memory(ogl_buffers  buffers,
                                                       unsigned int bo_id,
                                                       unsigned int bo_offset)
{
    _ogl_buffers_buffer* buffer_ptr  = NULL;
    _ogl_buffers*        buffers_ptr = (_ogl_buffers*) buffers;
    const system_hash64  bo_hash     = MAKE_BO_HASHMAP_KEY(bo_id, bo_offset);

    LOG_ERROR("ogl_buffers_free_buffer_memory(): BO id:[%d] offset:[%d]",
              bo_id,
              bo_offset);

    if (system_hash64map_get(buffers_ptr->bo_id_offset_hash_to_buffer_map,
                             bo_hash,
                            &buffer_ptr) )
    {
        system_memory_manager_free_block(buffer_ptr->manager,
                                         bo_offset);

        system_hash64map_remove(buffers_ptr->bo_id_offset_hash_to_buffer_map,
                                bo_hash);
    } /* if (allocation descriptor was found) */
    else
    {
        ASSERT_DEBUG_SYNC(false,
                          "Specified memory block (BO id:[%d] offset:[%d]) could not have been freed.",
                          bo_id,
                          bo_offset);
    }

}


