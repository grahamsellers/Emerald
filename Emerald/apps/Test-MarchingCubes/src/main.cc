/**
 *
 * Marching cubes test app (kbi/elude @2015)
 *
 */
#include "shared.h"
#include "curve/curve_container.h"
#include "mesh/mesh.h"
#include "mesh/mesh_marchingcubes.h"
#include "ogl/ogl_context.h"
#include "ogl/ogl_flyby.h"
#include "ogl/ogl_pipeline.h"
#include "ogl/ogl_rendering_handler.h"
#include "ogl/ogl_scene_renderer.h"
#include "ogl/ogl_ui.h"
#include "ogl/ogl_ui_scrollbar.h"
#include "scalar_field/scalar_field_metaballs.h"
#include "scene/scene.h"
#include "scene/scene_camera.h"
#include "scene/scene_graph.h"
#include "scene/scene_light.h"
#include "scene/scene_material.h"
#include "scene/scene_mesh.h"
#include "system/system_assertions.h"
#include "system/system_event.h"
#include "system/system_hashed_ansi_string.h"
#include "system/system_log.h"
#include "system/system_math_vector.h"
#include "system/system_matrix4x4.h"
#include "system/system_pixel_format.h"
#include "system/system_screen_mode.h"
#include "system/system_variant.h"
#include "system/system_window.h"
#include <algorithm>
#include "main.h"

PRIVATE const unsigned int _blob_size[] = {50, 50, 50};

PRIVATE ogl_context            _context             = NULL;
PRIVATE ogl_flyby              _context_flyby       = NULL;
PRIVATE mesh_marchingcubes     _marching_cubes      = NULL;
PRIVATE scene_material         _material            = NULL;
PRIVATE scalar_field_metaballs _metaballs           = NULL;
PRIVATE ogl_pipeline           _pipeline            = NULL;
PRIVATE uint32_t               _pipeline_stage_id   = 0;
PRIVATE system_matrix4x4       _projection_matrix   = NULL;
PRIVATE scene                  _scene               = NULL;
PRIVATE scene_graph_node       _scene_blob_node     = NULL;
PRIVATE scene_camera           _scene_camera        = NULL;
PRIVATE scene_graph_node       _scene_camera_node   = NULL;
PRIVATE scene_light            _scene_light         = NULL;
PRIVATE scene_graph_node       _scene_light_node    = NULL;
PRIVATE scene_graph            _scene_graph         = NULL; /* do not release */
PRIVATE ogl_scene_renderer     _scene_renderer      = NULL;
PRIVATE system_event           _window_closed_event = system_event_create(true); /* manual_reset */
PRIVATE int                    _window_size[2]      = {1280, 720};
PRIVATE system_matrix4x4       _view_matrix         = NULL;

const float _start_metaball_configs[] =
{
 /* size     X      Y      Z    */
    0.15f,  0.25f, 0.25f, 0.25f,
    0.2f,   0.5f,  0.3f,  0.7f,
    0.1f,   0.7f,  0.3f,  0.2f,
    0.12f,  0.8f,  0.5f,  0.5f
};
const unsigned int _n_start_metaball_configs = 4;

PRIVATE float _isolevel = float(_n_start_metaball_configs) - 0.5f;

/* Forward declarations */
PRIVATE void _get_isolevel_value             (void*                   user_arg,
                                              system_variant          result);
PRIVATE void _init_pipeline                  ();
PRIVATE void _init_scene                     ();
PRIVATE void _render                         (ogl_context             context,
                                              system_time             frame_time,
                                              const int*              rendering_area_px_topdown,
                                              void*                   callback_user_arg);
PRIVATE void _rendering_handler              (ogl_context             context,
                                              uint32_t                n_frames_rendered,
                                              system_time             frame_time,
                                              const int*              rendering_area_px_topdown,
                                              void*                   renderer);
PRIVATE bool _rendering_rbm_callback_handler (system_window           window,
                                              unsigned short          x,
                                              unsigned short          y,
                                              system_window_vk_status new_status,
                                              void*);
PRIVATE void _set_isolevel_value             (void*                   user_arg,
                                              system_variant          new_value);
PRIVATE void _window_closed_callback_handler (system_window           window);
PRIVATE void _window_closing_callback_handler(system_window           window);


/** TODO */
PRIVATE void _get_isolevel_value(void*          user_arg,
                                 system_variant result)
{
    system_variant_set_float(result,
                             _isolevel);
}

/** TODO */
PRIVATE void _init_pipeline()
{
    ogl_ui pipeline_ui = NULL;

    /* Set up pipeline */
    _pipeline          = ogl_pipeline_create   (_context,
                                                true,  /* should_overlay_performance_info */
                                                system_hashed_ansi_string_create("Rendering pipeline") );
    _pipeline_stage_id = ogl_pipeline_add_stage(_pipeline);
    pipeline_ui        = ogl_pipeline_get_ui   (_pipeline);

    ogl_pipeline_add_stage_step(_pipeline,
                                _pipeline_stage_id,
                                system_hashed_ansi_string_create("Rendering"),
                                _render,
                                NULL); /* step_callback_user_arg */

    /* Set up UI */
    const float isolevel_scrollbar_x1y1[] = {0.8f, 0.0f};

    ogl_ui_add_scrollbar(pipeline_ui,
                         system_hashed_ansi_string_create("Isolevel"),
                         OGL_UI_SCROLLBAR_TEXT_LOCATION_ABOVE_SLIDER,
                         system_variant_create_float(float(_n_start_metaball_configs - 1)),
                         system_variant_create_float(float(_n_start_metaball_configs)    ),
                         isolevel_scrollbar_x1y1,
                         _get_isolevel_value,
                         NULL,         /* get_current_value_user_arg */
                         _set_isolevel_value,
                         NULL);        /* set_current_value_user_arg */
}

/** TODO */
PRIVATE void _init_scene()
{
    scene_mesh blob_scene_mesh = NULL;

    /* Set up the scene */
    system_hashed_ansi_string blob_has       = system_hashed_ansi_string_create("Test blob");
    system_hashed_ansi_string camera_has     = system_hashed_ansi_string_create("Test camera");
    system_hashed_ansi_string light_has      = system_hashed_ansi_string_create("Test light");
    system_hashed_ansi_string test_scene_has = system_hashed_ansi_string_create("Test scene");

    _scene        = scene_create                  (_context,
                                                   test_scene_has);
    _scene_camera = scene_camera_create           (camera_has,
                                                   test_scene_has);
    _scene_light  = scene_light_create_directional(light_has,
                                                   NULL); /* object_manager_path */

    /* Set up the metaballs mesh material */
    curve_container* color_curves = NULL;
    scene_material   material     = scene_material_create      (system_hashed_ansi_string_create("Metaballs material"),
                                                                NULL, /* object_manager_path */
                                                                _scene);
    system_variant   variant_one  = system_variant_create_float(1.0f);

    scene_material_get_property(material,
                                SCENE_MATERIAL_PROPERTY_COLOR,
                               &color_curves);

    curve_container_set_default_value(color_curves[1],
                                      variant_one);

    system_variant_release(variant_one);
    variant_one = NULL;

    /* Set up the metaballs mesh instance */
    mesh         marching_cubes_mesh_gpu      = NULL;
    GLuint       scalar_field_bo_id           = 0;
    unsigned int scalar_field_bo_size         = 0;
    unsigned int scalar_field_bo_start_offset = 0;

    _metaballs = scalar_field_metaballs_create(_context,
                                               _blob_size,
                                               system_hashed_ansi_string_create("Metaballs"));

    scalar_field_metaballs_get_property(_metaballs,
                                        SCALAR_FIELD_METABALLS_PROPERTY_DATA_BO_ID,
                                       &scalar_field_bo_id);
    scalar_field_metaballs_get_property(_metaballs,
                                        SCALAR_FIELD_METABALLS_PROPERTY_DATA_BO_SIZE,
                                       &scalar_field_bo_size);
    scalar_field_metaballs_get_property(_metaballs,
                                        SCALAR_FIELD_METABALLS_PROPERTY_DATA_BO_START_OFFSET,
                                       &scalar_field_bo_start_offset);

    scalar_field_metaballs_set_property(_metaballs,
                                        SCALAR_FIELD_METABALLS_PROPERTY_N_METABALLS,
                                       &_n_start_metaball_configs);

    for (unsigned int n_metaball = 0;
                      n_metaball < _n_start_metaball_configs;
                    ++n_metaball)
    {
        scalar_field_metaballs_set_metaball_property(_metaballs,
                                                     SCALAR_FIELD_METABALLS_METABALL_PROPERTY_SIZE,
                                                     n_metaball,
                                                     _start_metaball_configs + 4 * n_metaball + 0);
        scalar_field_metaballs_set_metaball_property(_metaballs,
                                                     SCALAR_FIELD_METABALLS_METABALL_PROPERTY_XYZ,
                                                     n_metaball,
                                                     _start_metaball_configs + 4 * n_metaball + 1);
    } /* for (all metaballs) */

    _marching_cubes = mesh_marchingcubes_create(_context,
                                                _blob_size,
                                                scalar_field_bo_id,
                                                scalar_field_bo_start_offset,
                                                scalar_field_bo_size,
                                                _isolevel,
                                                material,
                                                system_hashed_ansi_string_create("Marching cubes") );

    mesh_marchingcubes_get_property(_marching_cubes,
                                    MESH_MARCHINGCUBES_PROPERTY_MESH,
                                   &marching_cubes_mesh_gpu);

    scene_add_light        (_scene,
                            _scene_light);
    scene_add_mesh_instance(_scene,
                            marching_cubes_mesh_gpu,
                            blob_has,
                           &blob_scene_mesh);

    /* Set up the scene graph */
    system_matrix4x4 identity_matrix = system_matrix4x4_create();

    scene_get_property(_scene,
                      SCENE_PROPERTY_GRAPH,
                     &_scene_graph);

    system_matrix4x4_set_to_identity(identity_matrix);

    _scene_blob_node   = scene_graph_create_general_node                        (_scene_graph);
    _scene_camera_node = scene_graph_create_static_matrix4x4_transformation_node(_scene_graph,
                                                                                 identity_matrix,
                                                                                 SCENE_GRAPH_NODE_TAG_UNDEFINED);
    _scene_light_node  = scene_graph_create_static_matrix4x4_transformation_node(_scene_graph,
                                                                                 identity_matrix,
                                                                                 SCENE_GRAPH_NODE_TAG_UNDEFINED);

    system_matrix4x4_release(identity_matrix);
    identity_matrix = NULL;

    scene_graph_attach_object_to_node(_scene_graph,
                                      _scene_blob_node,
                                      SCENE_OBJECT_TYPE_MESH,
                                      blob_scene_mesh);
    scene_graph_attach_object_to_node(_scene_graph,
                                      _scene_camera_node,
                                      SCENE_OBJECT_TYPE_CAMERA,
                                      _scene_camera);
    scene_graph_attach_object_to_node(_scene_graph,
                                      _scene_light_node,
                                      SCENE_OBJECT_TYPE_LIGHT,
                                      _scene_light);

    scene_graph_add_node(_scene_graph,
                         scene_graph_get_root_node(_scene_graph),
                         _scene_camera_node);
    scene_graph_add_node(_scene_graph,
                         scene_graph_get_root_node(_scene_graph),
                         _scene_light_node);
    scene_graph_add_node(_scene_graph,
                         scene_graph_get_root_node(_scene_graph),
                         _scene_blob_node);

    /* Set up the scene renderer */
    _scene_renderer = ogl_scene_renderer_create(_context,
                                                _scene);
}

/** TODO */
PRIVATE void _render(ogl_context context,
                     system_time frame_time,
                     const int*  rendering_area_px_topdown,
                     void*       callback_user_arg)
{
    /* Update the metaball configuration */
    /* 1. Isolevel */
    bool has_scalar_field_changed = false;

    mesh_marchingcubes_set_property(_marching_cubes,
                                    MESH_MARCHINGCUBES_PROPERTY_ISOLEVEL,
                                   &_isolevel);

    /* 2. Metaball locations */
    for (unsigned int n_metaball = 0;
                      n_metaball < _n_start_metaball_configs;
                    ++n_metaball)
    {
        float t = float((frame_time) % 2000) / 2000.0f * 10.0f * 3.14152965f;

        float new_size = _start_metaball_configs[n_metaball * 4 + 0] + sin((t * n_metaball) * 3.14152965f) * 0.0125f;

        float new_xyz[] =
        {
            _start_metaball_configs[n_metaball * 4 + 1] + t                       / float(250 * (n_metaball + 1)),
            _start_metaball_configs[n_metaball * 4 + 2] + t * cos(t * n_metaball) / float(75 * (n_metaball + 1)),
            _start_metaball_configs[n_metaball * 4 + 3] + t * sin(t * n_metaball) / float(120 * (n_metaball + 1))

        };

        scalar_field_metaballs_set_metaball_property(_metaballs,
                                                     SCALAR_FIELD_METABALLS_METABALL_PROPERTY_SIZE,
                                                     n_metaball,
                                                    &new_size);
        scalar_field_metaballs_set_metaball_property(_metaballs,
                                                     SCALAR_FIELD_METABALLS_METABALL_PROPERTY_XYZ,
                                                     n_metaball,
                                                     new_xyz);
    }

    /* Update the scalar field */
    has_scalar_field_changed = scalar_field_metaballs_update(_metaballs);

    /* Polygonize it */
    mesh_marchingcubes_polygonize(_marching_cubes,
                                  has_scalar_field_changed);

    /* Update the light node's transformation matrix to reflect the camera position */
    system_matrix4x4 light_node_transformation_matrix = NULL;

    ogl_flyby_get_property          (_context_flyby,
                                     OGL_FLYBY_PROPERTY_VIEW_MATRIX,
                                    &_view_matrix);
    scene_graph_node_get_property   (_scene_light_node,
                                     SCENE_GRAPH_NODE_PROPERTY_TRANSFORMATION_MATRIX,
                                    &light_node_transformation_matrix);
    system_matrix4x4_set_to_identity(light_node_transformation_matrix);

    system_matrix4x4_set_from_matrix4x4(light_node_transformation_matrix,
                                        _view_matrix);
    system_matrix4x4_invert            (light_node_transformation_matrix);

    /* Render the scene */
    ogl_scene_renderer_render_scene_graph(_scene_renderer,
                                          _view_matrix,
                                          _projection_matrix,
                                          _scene_camera,
                                          RENDER_MODE_FORWARD_WITHOUT_DEPTH_PREPASS,
                                          false, /* apply_shadow_mapping */
                                          HELPER_VISUALIZATION_BOUNDING_BOXES,
                                          frame_time);
}

/** TODO */
PRIVATE void _rendering_handler(ogl_context context,
                                uint32_t    n_frames_rendered,
                                system_time frame_time,
                                const int*  rendering_area_px_topdown,
                                void*       renderer)
{
    const ogl_context_gl_entrypoints* entrypoints_ptr = NULL;
    const ogl_context_gl_limits*      limits_ptr      = NULL;
    static bool                       is_initialized  = false;

    ogl_context_get_property(context,
                             OGL_CONTEXT_PROPERTY_ENTRYPOINTS_GL,
                            &entrypoints_ptr);
    ogl_context_get_property(context,
                             OGL_CONTEXT_PROPERTY_LIMITS,
                            &limits_ptr);

    /* If this is the first frame we're rendering, initialize various stuff. */
    if (!is_initialized)
    {
        /* Initialize scene stuff */
        _init_scene();

        /* Initialize the rendering pipeline */
        _init_pipeline();

        /* Initialize projection & view matrices */
        _projection_matrix = system_matrix4x4_create_perspective_projection_matrix(DEG_TO_RAD(34),  /* fov_y */
                                                                                   float(_window_size[0]) / float(_window_size[1]),
                                                                                   0.1f,  /* z_near */
                                                                                   10.0f); /* z_far  */
        _view_matrix       = system_matrix4x4_create                              ();

        system_matrix4x4_set_to_identity(_view_matrix);

        /* Initialize the flyby */
        static const bool is_flyby_active = true;

        ogl_context_get_property(_context,
                                 OGL_CONTEXT_PROPERTY_FLYBY,
                                &_context_flyby);
        ogl_flyby_set_property  (_context_flyby,
                                 OGL_FLYBY_PROPERTY_IS_ACTIVE,
                                &is_flyby_active);

        /* All done */
        is_initialized = true;
    } /* if (!is_initialized) */

    /* Update the flyby */
    ogl_flyby_update(_context_flyby);

    ogl_pipeline_draw_stage(_pipeline,
                            _pipeline_stage_id,
                            frame_time,
                            rendering_area_px_topdown);
}

/** Event callback handlers */
PRIVATE bool _rendering_rbm_callback_handler(system_window           window,
                                             unsigned short          x,
                                             unsigned short          y,
                                             system_window_vk_status new_status,
                                             void*)
{
    system_event_set(_window_closed_event);

    return true;
}

/** TODO */
PRIVATE void _set_isolevel_value(void*          user_arg,
                                 system_variant new_value)
{
    system_variant_get_float(new_value,
                            &_isolevel);
}

PRIVATE void _window_closed_callback_handler(system_window window)
{
    system_event_set(_window_closed_event);
}

PRIVATE void _window_closing_callback_handler(system_window window)
{
    if (_marching_cubes != NULL)
    {
        mesh_marchingcubes_release(_marching_cubes);

        _marching_cubes = NULL;
    }

    if (_metaballs != NULL)
    {
        scalar_field_metaballs_release(_metaballs);

        _metaballs = NULL;
    }

    if (_projection_matrix != NULL)
    {
        system_matrix4x4_release(_projection_matrix);

        _projection_matrix = NULL;
    }

    if (_scene != NULL)
    {
        scene_release(_scene);

        _scene = NULL;
    }

    if (_scene_camera != NULL)
    {
        scene_camera_release(_scene_camera);

        _scene_camera = NULL;
    }

    if (_scene_renderer != NULL)
    {
        ogl_scene_renderer_release(_scene_renderer);

        _scene_renderer = NULL;
    }

    if (_view_matrix != NULL)
    {
        system_matrix4x4_release(_view_matrix);

        _view_matrix = NULL;
    }

    /* All done */
    system_event_set(_window_closed_event);
}


/** Entry point */
#ifdef _WIN32
    int WINAPI WinMain(HINSTANCE instance_handle,
                       HINSTANCE,
                       LPTSTR,
                       int)
#else
    int main()
#endif
{
    system_screen_mode    screen_mode              = NULL;
    system_window         window                   = NULL;
    system_pixel_format   window_pf                = NULL;
    ogl_rendering_handler window_rendering_handler = NULL;
    int                   window_x1y1x2y2[4]       = {0};

    system_screen_mode_get         (0,
                                   &screen_mode);
    system_screen_mode_get_property(screen_mode,
                                    SYSTEM_SCREEN_MODE_PROPERTY_WIDTH,
                                    _window_size + 0);
    system_screen_mode_get_property(screen_mode,
                                    SYSTEM_SCREEN_MODE_PROPERTY_HEIGHT,
                                    _window_size + 1);

    window_pf = system_pixel_format_create(8,  /* color_buffer_red_bits   */
                                           8,  /* color_buffer_green_bits */
                                           8,  /* color_buffer_blue_bits  */
                                           0,  /* color_buffer_alpha_bits */
                                           16, /* depth_buffer_bits       */
                                           SYSTEM_PIXEL_FORMAT_USE_MAXIMUM_NUMBER_OF_SAMPLES,
                                           0); /* stencil_buffer_bits     */

#if 0
    window = system_window_create_fullscreen(OGL_CONTEXT_TYPE_GL,
                                             screen_mode,
                                             true, /* vsync_enabled */
                                             window_pf);
#else
    _window_size[0] /= 2;
    _window_size[1] /= 2;

    system_window_get_centered_window_position_for_primary_monitor(_window_size,
                                                                   window_x1y1x2y2);

    window = system_window_create_not_fullscreen(OGL_CONTEXT_TYPE_GL,
                                                 window_x1y1x2y2,
                                                 system_hashed_ansi_string_create("Test window"),
                                                 false, /* scalable */
                                                 true,  /* vsync_enabled */
                                                 true,  /* visible */
                                                 window_pf);
#endif

    /* Set up the rendering contxt */
    window_rendering_handler = ogl_rendering_handler_create_with_fps_policy(system_hashed_ansi_string_create("Default rendering handler"),
                                                                            60,
                                                                            _rendering_handler,
                                                                            NULL); /* user_arg */

    /* Kick off the rendering process */
    system_window_get_property(window,
                               SYSTEM_WINDOW_PROPERTY_RENDERING_CONTEXT,
                              &_context);
    system_window_set_property(window,
                               SYSTEM_WINDOW_PROPERTY_RENDERING_HANDLER,
                              &window_rendering_handler);

    /* Set up mouse click & window tear-down callbacks */
    system_window_add_callback_func    (window,
                                        SYSTEM_WINDOW_CALLBACK_FUNC_PRIORITY_NORMAL,
                                        SYSTEM_WINDOW_CALLBACK_FUNC_RIGHT_BUTTON_DOWN,
                                        (void*) _rendering_rbm_callback_handler,
                                        NULL);
    system_window_add_callback_func    (window,
                                        SYSTEM_WINDOW_CALLBACK_FUNC_PRIORITY_NORMAL,
                                        SYSTEM_WINDOW_CALLBACK_FUNC_WINDOW_CLOSED,
                                        (void*) _window_closed_callback_handler,
                                        NULL);
    system_window_add_callback_func    (window,
                                        SYSTEM_WINDOW_CALLBACK_FUNC_PRIORITY_NORMAL,
                                        SYSTEM_WINDOW_CALLBACK_FUNC_WINDOW_CLOSING,
                                        (void*) _window_closing_callback_handler,
                                        NULL);

    /* Launch the rendering process and wait until the window is closed. */
    ogl_rendering_handler_play(window_rendering_handler,
                               0);

    system_event_wait_single(_window_closed_event);

    /* Clean up - DO NOT release any GL objects here, no rendering context is bound
     * to the main thread!
     */
    system_window_close (window);
    system_event_release(_window_closed_event);

    main_force_deinit();

    return 0;
}