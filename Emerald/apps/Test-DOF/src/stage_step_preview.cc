/**
 *
 * DOF test app
 *
 */

#include "shared.h"
#include "include\main.h"
#include "stage_step_dof_scheuermann.h"
#include "stage_step_julia.h"
#include "stage_step_preview.h"
#include "ogl\ogl_context.h"
#include "ogl\ogl_pipeline.h"

/** TODO */
static void _stage_step_preview_execute(ogl_context          context,
                                        system_timeline_time time,
                                        void*                not_used)
{
    const ogl_context_gl_entrypoints* entrypoints = NULL;

    ogl_context_get_property(context,
                             OGL_CONTEXT_PROPERTY_ENTRYPOINTS_GL,
                            &entrypoints);

    /* Blit our custom framebuffer's contents to the system one */
    entrypoints->pGLBindFramebuffer(GL_READ_FRAMEBUFFER,
                                    stage_step_dof_scheuermann_get_combination_fbo_id() );
    entrypoints->pGLBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                                    0);
    entrypoints->pGLBlitFramebuffer(0,    /* srcX0 */
                                    0,    /* srcY0 */
                                    1280, /* srcX1 */
                                    720,  /* srcY1 */
                                    0,    /* dstX0 */
                                    0,    /* dstY0 */
                                    1280, /* dstX1 */
                                    720,  /* dstY1 */
                                    GL_COLOR_BUFFER_BIT,
                                    GL_NEAREST);
}

/* Please see header for specification */
PUBLIC void stage_step_preview_deinit(ogl_context context)
{
    // Nothing to do 
}

/* Please see header for specification */
PUBLIC void stage_step_preview_init(ogl_context  context,
                                    ogl_pipeline pipeline,
                                    uint32_t     stage_id)
{
    /* Add ourselves to the pipeline */
    ogl_pipeline_add_stage_step(pipeline,
                                stage_id,
                                system_hashed_ansi_string_create("Blitting"),
                                _stage_step_preview_execute,
                                NULL); /* step_callback_user_arg */
}