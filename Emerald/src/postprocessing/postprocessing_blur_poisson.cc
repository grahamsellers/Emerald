/**
 *
 * Emerald (kbi/elude @2012-2015)
 *
 */
#include "shared.h"
#include "ogl/ogl_context.h"
#include "ogl/ogl_program.h"
#include "ogl/ogl_program_ub.h"
#include "ogl/ogl_shader.h"
#include "ogl/ogl_texture.h"
#include "postprocessing/postprocessing_blur_poisson.h"
#include "shaders/shaders_vertex_fullscreen.h"
#include "system/system_assertions.h"
#include "system/system_hashed_ansi_string.h"
#include "system/system_log.h"
#include "system/system_window.h"

/** Internal type definition */
typedef struct
{
    postprocessing_blur_poisson_blur_bluriness_source bluriness_source;
    ogl_context                                       context;
    const char*                                       custom_shader_code;

    GLint blur_strength_ub_offset;

    GLuint fbo_id;

    system_hashed_ansi_string name;
    ogl_program               program;
    ogl_program_ub            program_ub;
    GLuint                    program_ub_bo_id;
    unsigned int              program_ub_bo_size;
    unsigned int              program_ub_bo_start_offset;

    REFCOUNT_INSERT_VARIABLES
} _postprocessing_blur_poisson;


/** Internal variables */ 
static const char* postprocessing_blur_poisson_uniform_blur_strength_body = "vec2 get_blur_strength()\n"
                                                                            "{\n"
                                                                            "    return vec2(blur_strength);\n"
                                                                            "}\n";
static const char* postprocessing_blur_poisson_source_input_alpha_blur_strength_body = "vec2 get_blur_strength()\n"
                                                                                       "{\n"
                                                                                       "    return texture(data, uv).aa;\n"
                                                                                       "}\n";

static const char* postprocessing_blur_poisson_fragment_shader_body_preambule = "#version 430 core\n"
                                                                                "\n";
static const char* postprocessing_blur_poisson_tap_data_body = "const float taps[] = float[](-0.37468f,     0.8566398f,\n"
                                                               "                              0.2497663f,   0.5130413f,\n"
                                                               "                             -0.3814645f,   0.4049693f,\n"
                                                               "                             -0.7627723f,   0.06273784f,\n"
                                                               "                             -0.1496337f,   0.007745424f,\n"
                                                               "                              0.07355442f,  0.984551f,\n"
                                                               "                              0.5004861f,  -0.2253174f,\n"
                                                               "                              0.7175385f,   0.4638414f,\n"
                                                               "                              0.9855714f,   0.01321317f,\n"
                                                               "                             -0.3985322f,  -0.5223456f,\n"
                                                               "                              0.2407384f,  -0.888767f,\n"
                                                               "                              0.07159682f, -0.4352953f,\n"
                                                               "                             -0.2808287f,  -0.9595322f,\n"
                                                               "                             -0.8836895f,  -0.4189175f,\n"
                                                               "                             -0.7953489f,   0.5906183f,\n"
                                                               "                              0.8182191f,  -0.5711964f);\n";
static const char* postprocessing_blur_poisson_fragment_shader_body_declarations = "uniform dataFS\n"
                                                                                   "{\n"
                                                                                   "    float blur_strength;\n"
                                                                                   "};\n"
                                                                                   "\n"
                                                                                   "uniform sampler2D data;\n"
                                                                                   "out     vec4      result;\n"
                                                                                   "in      vec2      uv;\n"
                                                                                   "\n"
                                                                                   "vec2 get_blur_strength();\n"
                                                                                   "\n"
                                                                                   "\n";
static const char* postprocessing_blur_poisson_fragment_shader_body_main ="void main()\n"
                                                                          "{\n"
                                                                          "\n"
                                                                          "   vec4 temp_result = vec4(0.0);\n"
                                                                          "\n"
                                                                          "   for (int i = 0; i < taps.length(); i += 2)\n"
                                                                          "   {\n"
                                                                          "      temp_result += texture(data, uv + vec2(taps[i], taps[i+1]) / textureSize(data, 0) * get_blur_strength() );\n"
                                                                          "   }\n"
                                                                          "\n"
                                                                          "   result = temp_result / taps.length() * 2;\n"
                                                                          "}\n";

/** Reference counter impl */
REFCOUNT_INSERT_IMPLEMENTATION(postprocessing_blur_poisson,
                               postprocessing_blur_poisson,
                              _postprocessing_blur_poisson);

/* Forward declarations */
#ifdef _DEBUG
    PRIVATE void _postprocessing_blur_poisson_verify_context_type(ogl_context);
#else
    #define _postprocessing_blur_poisson_verify_context_type(x)
#endif


/** TODO */
PUBLIC void _postprocessing_blur_poisson_init_renderer_callback(ogl_context context,
                                                                void*       instance)
{
    _postprocessing_blur_poisson* poisson_ptr     = (_postprocessing_blur_poisson*) instance;
    ogl_shader                    fragment_shader = NULL;
    shaders_vertex_fullscreen     vertex_shader   = NULL;

    const char* fragment_shader_body_parts[] =
    {
        postprocessing_blur_poisson_fragment_shader_body_preambule,
        postprocessing_blur_poisson_tap_data_body,
        postprocessing_blur_poisson_fragment_shader_body_declarations,
        postprocessing_blur_poisson_fragment_shader_body_main,
        (poisson_ptr->bluriness_source == POSTPROCESSING_BLUR_POISSON_BLUR_BLURRINESS_SOURCE_UNIFORM)     ? postprocessing_blur_poisson_uniform_blur_strength_body            :
        (poisson_ptr->bluriness_source == POSTPROCESSING_BLUR_POISSON_BLUR_BLURRINESS_SOURCE_INPUT_ALPHA) ? postprocessing_blur_poisson_source_input_alpha_blur_strength_body :
                                                                                                            "?!"
    };
    const uint32_t fragment_shader_n_body_parts = sizeof(fragment_shader_body_parts) / sizeof(fragment_shader_body_parts[0]);


    fragment_shader      = ogl_shader_create               (context,
                                                            SHADER_TYPE_FRAGMENT,
                                                            system_hashed_ansi_string_create_by_merging_two_strings("Postprocessing blur poisson fragment shader ", 
                                                                                                                    system_hashed_ansi_string_get_buffer(poisson_ptr->name) ));
    vertex_shader        = shaders_vertex_fullscreen_create(poisson_ptr->context,
                                                            true,
                                                            poisson_ptr->name);
    poisson_ptr->program = ogl_program_create              (poisson_ptr->context,
                                                            system_hashed_ansi_string_create_by_merging_two_strings("Postprocessing blur poisson program ",
                                                                                                                    system_hashed_ansi_string_get_buffer(poisson_ptr->name) ),
                                                            OGL_PROGRAM_SYNCABLE_UBS_MODE_ENABLE_GLOBAL);

    ogl_shader_set_body      (fragment_shader,
                              system_hashed_ansi_string_create_by_merging_strings(fragment_shader_n_body_parts,
                                                                                  fragment_shader_body_parts) );
    ogl_program_attach_shader(poisson_ptr->program,
                              fragment_shader);
    ogl_program_attach_shader(poisson_ptr->program,
                              shaders_vertex_fullscreen_get_shader(vertex_shader) );

    ogl_program_link(poisson_ptr->program);

    /* Retrieve attribute & uniform locations */
    const ogl_program_variable* blur_strength_uniform = NULL;

    ogl_program_get_uniform_by_name(poisson_ptr->program,
                                    system_hashed_ansi_string_create("blur_strength"),
                                   &blur_strength_uniform);

    ASSERT_DEBUG_SYNC(blur_strength_uniform->block_offset != -1,
                      "Blur strength UB offset is -1");

    poisson_ptr->blur_strength_ub_offset = blur_strength_uniform->block_offset;

    /* Retrieve UB info */
    ogl_program_get_uniform_block_by_name(poisson_ptr->program,
                                          system_hashed_ansi_string_create("dataFS"),
                                         &poisson_ptr->program_ub);

    ASSERT_DEBUG_SYNC(poisson_ptr->program_ub != NULL,
                      "dataFS uniform block descriptor is NULL");

    ogl_program_ub_get_property(poisson_ptr->program_ub,
                                OGL_PROGRAM_UB_PROPERTY_BO_ID,
                               &poisson_ptr->program_ub_bo_id);
    ogl_program_ub_get_property(poisson_ptr->program_ub,
                                OGL_PROGRAM_UB_PROPERTY_BLOCK_DATA_SIZE,
                               &poisson_ptr->program_ub_bo_size);
    ogl_program_ub_get_property(poisson_ptr->program_ub,
                                OGL_PROGRAM_UB_PROPERTY_BO_START_OFFSET,
                               &poisson_ptr->program_ub_bo_start_offset);

    /* Generate FBO */
    const ogl_context_gl_entrypoints* entrypoints = NULL;

    ogl_context_get_property(context,
                             OGL_CONTEXT_PROPERTY_ENTRYPOINTS_GL,
                            &entrypoints);

    entrypoints->pGLGenFramebuffers(1,
                                   &poisson_ptr->fbo_id);

    /* Release shaders */
    ogl_shader_release               (fragment_shader);
    shaders_vertex_fullscreen_release(vertex_shader);
}

/** TODO */
PRIVATE void _postprocessing_blur_poisson_release(void* ptr)
{
    _postprocessing_blur_poisson* data_ptr = (_postprocessing_blur_poisson*) ptr;

    ogl_program_release(data_ptr->program);
}

/** TODO */
#ifdef _DEBUG
    /* TODO */
    PRIVATE void _postprocessing_blur_poisson_verify_context_type(ogl_context context)
    {
        ogl_context_type context_type = OGL_CONTEXT_TYPE_UNDEFINED;

        ogl_context_get_property(context,
                                 OGL_CONTEXT_PROPERTY_TYPE,
                                &context_type);

        ASSERT_DEBUG_SYNC(context_type == OGL_CONTEXT_TYPE_GL,
                          "postprocessing_blur_poisson is only supported under GL contexts")
    }
#endif


/** Please see header for specification */
PUBLIC EMERALD_API postprocessing_blur_poisson postprocessing_blur_poisson_create(ogl_context                                       context,
                                                                                  system_hashed_ansi_string                         name,
                                                                                  postprocessing_blur_poisson_blur_bluriness_source bluriness_source,
                                                                                  const char*                                       custom_shader_code)
{
    _postprocessing_blur_poisson_verify_context_type(context);

    /* Instantiate the object */
    _postprocessing_blur_poisson* result_object = new (std::nothrow) _postprocessing_blur_poisson;

    ASSERT_DEBUG_SYNC(result_object != NULL,
                      "Out of memory");

    if (result_object == NULL)
    {
        LOG_ERROR("Out of memory");

        goto end;
    }

    memset(result_object,
           0,
           sizeof(_postprocessing_blur_poisson) );

    result_object->bluriness_source   = bluriness_source;
    result_object->context            = context;
    result_object->custom_shader_code = custom_shader_code;
    result_object->name               = name;

    ogl_context_request_callback_from_context_thread(context,
                                                     _postprocessing_blur_poisson_init_renderer_callback,
                                                     result_object);

    REFCOUNT_INSERT_INIT_CODE_WITH_RELEASE_HANDLER(result_object,
                                                   _postprocessing_blur_poisson_release,
                                                   OBJECT_TYPE_POSTPROCESSING_BLUR_POISSON,
                                                   system_hashed_ansi_string_create_by_merging_two_strings("\\Post-processing Blur Poisson\\",
                                                                                                           system_hashed_ansi_string_get_buffer(name)) );

    /* Return the object */
    return (postprocessing_blur_poisson) result_object;

end:
    if (result_object != NULL)
    {
        _postprocessing_blur_poisson_release(result_object);

        delete result_object; 
    }

    return NULL;
}

/* Please see header for specification */
PUBLIC EMERALD_API void postprocessing_blur_poisson_execute(postprocessing_blur_poisson blur_poisson,
                                                            ogl_texture                 input_texture,
                                                            float                       blur_strength,
                                                            ogl_texture                 result_texture)
{
    _postprocessing_blur_poisson*                             poisson_ptr     = (_postprocessing_blur_poisson*) blur_poisson;
    const ogl_context_gl_entrypoints_ext_direct_state_access* dsa_entrypoints = NULL;
    const ogl_context_gl_entrypoints*                         entrypoints     = NULL;
    unsigned int                                              texture_height  = 0;
    unsigned int                                              texture_width   = 0;
    system_window                                             window          = NULL;
    int                                                       window_size[2]  = {0};
    GLuint                                                    vao_id          = 0;

    ogl_context_get_property(poisson_ptr->context,
                             OGL_CONTEXT_PROPERTY_ENTRYPOINTS_GL_EXT_DIRECT_STATE_ACCESS,
                            &dsa_entrypoints);
    ogl_context_get_property(poisson_ptr->context,
                             OGL_CONTEXT_PROPERTY_ENTRYPOINTS_GL,
                            &entrypoints);
    ogl_context_get_property(poisson_ptr->context,
                             OGL_CONTEXT_PROPERTY_VAO_NO_VAAS,
                            &vao_id);
    ogl_context_get_property(poisson_ptr->context,
                             OGL_CONTEXT_PROPERTY_WINDOW,
                            &window);

    system_window_get_property(window,
                               SYSTEM_WINDOW_PROPERTY_DIMENSIONS,
                               window_size);

    entrypoints->pGLBindFramebuffer     (GL_FRAMEBUFFER,
                                         poisson_ptr->fbo_id);
    entrypoints->pGLBindVertexArray     (vao_id);
    entrypoints->pGLFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,
                                         GL_COLOR_ATTACHMENT0,
                                         GL_TEXTURE_2D,
                                         result_texture,
                                         0);

    entrypoints->pGLUseProgram             (ogl_program_get_id(poisson_ptr->program) );
    dsa_entrypoints->pGLBindMultiTextureEXT(GL_TEXTURE0,
                                            GL_TEXTURE_2D,
                                            input_texture);

    ogl_program_ub_set_nonarrayed_uniform_value(poisson_ptr->program_ub,
                                                poisson_ptr->blur_strength_ub_offset,
                                               &blur_strength,
                                               0, /* src_data_flags */
                                               sizeof(float) );
    ogl_program_ub_sync                        (poisson_ptr->program_ub);

    ogl_texture_get_mipmap_property(result_texture,
                                    0, /* mipmap_levle */
                                    OGL_TEXTURE_MIPMAP_PROPERTY_HEIGHT,
                                   &texture_height);
    ogl_texture_get_mipmap_property(result_texture,
                                    0, /* mipmap_levle */
                                    OGL_TEXTURE_MIPMAP_PROPERTY_WIDTH,
                                   &texture_width);

    entrypoints->pGLBindBufferRange(GL_UNIFORM_BUFFER,
                                    0, /* index */
                                    poisson_ptr->program_ub_bo_id,
                                    poisson_ptr->program_ub_bo_start_offset,
                                    poisson_ptr->program_ub_bo_size);

    entrypoints->pGLViewport  (0,
                               0,
                               texture_width,
                               texture_height);
    entrypoints->pGLDrawArrays(GL_TRIANGLE_FAN,
                               0,
                               4);
    entrypoints->pGLViewport  (0,
                               0,
                               window_size[0],
                               window_size[1]);

    entrypoints->pGLBindVertexArray(0);
}

/* Please see header for specification */
PUBLIC EMERALD_API const char* postprocessing_blur_poisson_get_tap_data_shader_code()
{
    return postprocessing_blur_poisson_tap_data_body;
}
