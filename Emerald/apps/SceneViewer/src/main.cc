/**
 *
 * Internal Emerald scene viewer (kbi/elude @2014-2015)
 *
 */
#include "shared.h"
#include <stdlib.h>
#include "main.h"
#include "curve/curve_container.h"
#include "mesh/mesh.h"
#include "mesh/mesh_material.h"
#include "ogl/ogl_context.h"
#include "ogl/ogl_curve_renderer.h"
#include "ogl/ogl_flyby.h"
#include "ogl/ogl_materials.h"
#include "ogl/ogl_pipeline.h"
#include "ogl/ogl_program.h"
#include "ogl/ogl_rendering_handler.h"
#include "ogl/ogl_scene_renderer.h"
#include "ogl/ogl_shader.h"
#include "ogl/ogl_uber.h"
#include "ogl/ogl_ui.h"
#include "ogl/ogl_ui_dropdown.h"
#include "ogl/ogl_ui_texture_preview.h"
#include "scene/scene.h"
#include "scene/scene_camera.h"
#include "scene/scene_graph.h"
#include "scene/scene_light.h"
#include "scene/scene_mesh.h"
#include "system/system_assertions.h"
#include "system/system_capabilities.h"
#include "system/system_critical_section.h"
#include "system/system_event.h"
#include "system/system_file_enumerator.h"
#include "system/system_file_serializer.h"
#include "system/system_hashed_ansi_string.h"
#include "system/system_log.h"
#include "system/system_matrix4x4.h"
#include "system/system_pixel_format.h"
#include "system/system_resizable_vector.h"
#include "system/system_screen_mode.h"
#include "system/system_variant.h"
#include "system/system_window.h"
#include <string>
#include <sstream>
#include "app_config.h"
#include "state.h"
#include "ui.h"

ogl_context           _context             = NULL;
ogl_rendering_handler _rendering_handler   = NULL;
system_window         _window              = NULL;
system_event          _window_closed_event = system_event_create(true); /* manual_reset */
GLuint                _vao_id              = 0;

extern ogl_flyby      _flyby;

/** Rendering handler */
void _rendering_handler_callback(ogl_context context,
                                 uint32_t    n_frames_rendered,
                                 system_time frame_time,
                                 const int*  rendering_area_px_topdown,
                                 void*       unused)
{
    const ogl_context_gl_entrypoints* entry_points = NULL;

    ogl_context_get_property(context,
                             OGL_CONTEXT_PROPERTY_ENTRYPOINTS_GL,
                            &entry_points);

    entry_points->pGLClearColor(0.0f,
                                0.0f,
                                0.5f,
                                0.0f);
    entry_points->pGLClear     (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    entry_points->pGLEnable    (GL_DEPTH_TEST);
    entry_points->pGLEnable    (GL_CULL_FACE);

    /* Render the scene */
    ogl_pipeline_draw_stage(state_get_pipeline(),
                            state_get_pipeline_stage_id(),
                            frame_time,
                            rendering_area_px_topdown);
}

PUBLIC void _render_scene(ogl_context context,
                          system_time time,
                          const int*  rendering_area_px_topdown,
                          void*       not_used)
{
    system_time frame_time;

    if (!state_get_playback_status() )
    {
        frame_time = state_get_last_frame_time();
    }
    else
    {
        static system_time start_time = system_time_now();

        #ifdef ENABLE_ANIMATION
           frame_time = (system_time_now() - start_time) %
                        state_get_animation_duration_time();
        #else
           frame_time = 0;
        #endif
    }

    /* Update view matrix */
    scene_camera     camera                 = NULL;
    bool             camera_is_flyby_active = false;
    system_matrix4x4 projection             = system_matrix4x4_create();
    system_matrix4x4 view                   = system_matrix4x4_create();

    state_lock_current_camera(&camera,
                              &camera_is_flyby_active);
    {
        /* If there is no projection matrix available, initialize one now */
        /* Retrieve the camera descriptro */
        ogl_ui_control active_path_control = ui_get_active_path_control();
        float          new_zfar            = CAMERA_SETTING_Z_FAR;
        float          new_znear           = 1.0f;

        if (!camera_is_flyby_active)
        {
            bool new_visibility = false;

            /* Create projection matrix */
            float yfov_value;

            scene_camera_get_property(camera,
                                      SCENE_CAMERA_PROPERTY_VERTICAL_FOV_FROM_ZOOM_FACTOR,
                                      time,
                                      &yfov_value);

            scene_camera_set_property(camera,
                                      SCENE_CAMERA_PROPERTY_FAR_PLANE_DISTANCE,
                                     &new_zfar);
            scene_camera_set_property(camera,
                                      SCENE_CAMERA_PROPERTY_NEAR_PLANE_DISTANCE,
                                     &new_znear);

            projection = system_matrix4x4_create_perspective_projection_matrix(yfov_value,
                                                                               float(WINDOW_WIDTH) / float(WINDOW_HEIGHT),
                                                                               new_znear,
                                                                               new_zfar);

            if (active_path_control != NULL)
            {
                ogl_ui_set_control_property(active_path_control,
                                            OGL_UI_CONTROL_PROPERTY_DROPDOWN_VISIBLE,
                                           &new_visibility);
            }
        } /* if (!camera_ptr->is_flyby) */
        else
        {
            bool new_visibility = true;

            projection = system_matrix4x4_create_perspective_projection_matrix(45.0f,
                                                                               float(WINDOW_WIDTH) / float(WINDOW_HEIGHT),
                                                                               1.0f,
                                                                               CAMERA_SETTING_Z_FAR);

            if (active_path_control != NULL)
            {
                ogl_ui_set_control_property(active_path_control,
                                            OGL_UI_CONTROL_PROPERTY_DROPDOWN_VISIBLE,
                                           &new_visibility);
            }

            ogl_flyby_get_property(_flyby,
                                   OGL_FLYBY_PROPERTY_FAKE_SCENE_CAMERA,
                                  &camera);
            ogl_flyby_set_property(_flyby,
                                   OGL_FLYBY_PROPERTY_FAKE_SCENE_CAMERA_Z_FAR,
                                  &new_zfar);
            ogl_flyby_set_property(_flyby,
                                   OGL_FLYBY_PROPERTY_FAKE_SCENE_CAMERA_Z_NEAR,
                                  &new_znear);
        }

        if (camera_is_flyby_active)
        {
            ogl_flyby_lock(_flyby);
            {
                ogl_flyby_update(_flyby);

                /* Retrieve flyby view matrix */
                ogl_flyby_get_property(_flyby,
                                       OGL_FLYBY_PROPERTY_VIEW_MATRIX,
                                      &view);
            }
            ogl_flyby_unlock(_flyby);
        } /* if (camera_ptr->is_flyby) */
        else
        {
            scene_graph scene_renderer_graph = NULL;

            /* Compute matrices for all nodes */
            ogl_scene_renderer_get_property(state_get_scene_renderer(),
                                            OGL_SCENE_RENDERER_PROPERTY_GRAPH,
                                           &scene_renderer_graph);

            scene_graph_lock(scene_renderer_graph);
            {
                scene_graph_compute(scene_renderer_graph,
                                    frame_time);

                /* Retrieve node that contains the transformation matrix for the camera */
                scene_graph_node scene_camera_node                  = NULL;
                system_matrix4x4 scene_camera_transformation_matrix = NULL;

                scene_camera_get_property(camera,
                                          SCENE_CAMERA_PROPERTY_OWNER_GRAPH_NODE,
                                          0, /* time - irrelevant for the owner graph node */
                                         &scene_camera_node);

                scene_graph_node_get_property(scene_camera_node,
                                              SCENE_GRAPH_NODE_PROPERTY_TRANSFORMATION_MATRIX,
                                             &scene_camera_transformation_matrix);

                /* Extract camera location.
                 *
                 * For the view matrix, we need to take the inverse of the transformation matrix.
                 **/
                system_matrix4x4_set_from_matrix4x4 (view,
                                                     scene_camera_transformation_matrix);
                system_matrix4x4_invert             (view);
            }
            scene_graph_unlock(scene_renderer_graph);
        }
    }
    state_unlock_current_camera();

    /* Traverse the scene graph */
    const ogl_context_gl_entrypoints* entry_points = NULL;

    ogl_context_get_property(_context,
                             OGL_CONTEXT_PROPERTY_ENTRYPOINTS_GL,
                            &entry_points);

    ogl_scene_renderer_render_scene_graph(state_get_scene_renderer(),
                                          view,
                                          projection,
                                          camera,
                                          RENDER_MODE_FORWARD_WITHOUT_DEPTH_PREPASS,
#ifdef ENABLE_SM
                                          true,  /* apply_shadow_mapping */
#else
                                          false, /* apply_shadow_mapping */
#endif
#ifdef ENABLE_BB_VISUALIZATION
                                          HELPER_VISUALIZATION_BOUNDING_BOXES,
#else
                                          HELPER_VISUALIZATION_NONE,
#endif
                                          frame_time
                                         );

    /* Draw curves marked as active. **/
    const ogl_curve_item_id current_curve_item_id = state_get_curve_renderer_item_id();
    ogl_curve_renderer      curve_renderer        = state_get_curve_renderer();

    if (current_curve_item_id != -1 &&
        camera_is_flyby_active)
    {
        const ogl_context_gl_entrypoints* entry_points = NULL;
        system_matrix4x4                  vp           = system_matrix4x4_create_by_mul(projection,
                                                                                        view);

        ogl_context_get_property(context,
                                 OGL_CONTEXT_PROPERTY_ENTRYPOINTS_GL,
                                &entry_points);

        ogl_curve_renderer_draw(state_get_curve_renderer(),
                                1,
                               &current_curve_item_id,
                                vp);

        system_matrix4x4_release(vp);
    }

    /* Configure texture preview. Shadow maps are assigned from the texture pool, so we need to
     * refresh the texture assigned to the texture preview control every frame
     */
    scene_light    light              = scene_get_light_by_index(state_get_scene(),
                                                                 0);
    ogl_texture    light_sm           = NULL;
    ogl_ui_control ui_texture_preview = ui_get_texture_preview_control();

    scene_light_get_property(light,
                             SCENE_LIGHT_PROPERTY_SHADOW_MAP_TEXTURE_COLOR,
                            &light_sm);

    if (ui_texture_preview != NULL)
    {
        ogl_ui_set_control_property(ui_texture_preview,
                                    OGL_UI_CONTROL_PROPERTY_TEXTURE_PREVIEW_TEXTURE,
                                   &light_sm);
    }

    /* Render UI */
    ui_draw();

    /* All done */
    state_set_last_frame_time(frame_time);

    system_matrix4x4_release(view);
    system_matrix4x4_release(projection);
}

bool _rendering_key_down_callback_handler(system_window  window,
                                          unsigned short key,
                                          void*          unused)
{
    bool result = true;

    if (key == ' ')
    {
        state_set_playback_status(!state_get_playback_status() );

        result = false;
    }

    return result;
}

void _rendering_rbm_callback_handler(system_window           window,
                                     unsigned short          x,
                                     unsigned short          y,
                                     system_window_vk_status new_status,
                                     void*)
{
    system_event_set(_window_closed_event);
}

bool _rendering_window_closed_callback_handler(system_window window)
{
    system_event_set(_window_closed_event);

    return true;
}

void _rendering_window_closing_callback_handler(system_window window)
{
    ui_deinit   ();
    state_deinit();
}

/** Entry point */
#ifdef _WIN32
    int WINAPI WinMain(HINSTANCE instance_handle, HINSTANCE, LPTSTR, int)
#else
    int main()
#endif
{
    system_screen_mode screen_mode        = NULL;
    int                window_size[2]     = {WINDOW_WIDTH, WINDOW_HEIGHT};
    int                window_x1y1x2y2[4] = {0};

    /* Let the user select the scene file */
    const system_hashed_ansi_string filters[] =
    {
        system_hashed_ansi_string_create("Packed Blob Files"),
        system_hashed_ansi_string_create("Scene Blob Files")
    };
    const system_hashed_ansi_string filter_extensions[] =
    {
        system_hashed_ansi_string_create("*.packed"),
        system_hashed_ansi_string_create("*.scene")
    };

    system_hashed_ansi_string scene_filename = system_file_enumerator_choose_file_via_ui(SYSTEM_FILE_ENUMERATOR_FILE_OPERATION_LOAD,
                                                                                         2, /* n_filters */
                                                                                         filters,
                                                                                         filter_extensions,
                                                                                         system_hashed_ansi_string_create("Select Emerald Scene file") );

    if (scene_filename == NULL)
    {
        goto end;
    }

    /* Carry on */
    system_pixel_format window_pf = system_pixel_format_create(8,  /* color_buffer_red_bits   */
                                                               8,  /* color_buffer_green_bits */
                                                               8,  /* color_buffer_blue_bits  */
                                                               0,  /* color_buffer_alpha_bits */
                                                               16, /* depth_buffer_bits       */
                                                               SYSTEM_PIXEL_FORMAT_USE_MAXIMUM_NUMBER_OF_SAMPLES,
                                                               0); /* stencil_buffer_bits     */

#if 0
    system_screen_mode_get         (0,
                                   &screen_mode);
    system_screen_mode_get_property(screen_mode,
                                    SYSTEM_SCREEN_MODE_PROPERTY_WIDTH,
                                    window_size + 0);
    system_screen_mode_get_property(screen_mode,
                                    SYSTEM_SCREEN_MODE_PROPERTY_HEIGHT,
                                    window_size + 1);

    _window = system_window_create_fullscreen(OGL_CONTEXT_TYPE_GL,
                                              screen_mode,
                                              true, /* vsync_enabled */
                                              window_pf);
#else
    window_size[0] /= 2;
    window_size[1] /= 2;

    system_window_get_centered_window_position_for_primary_monitor(window_size,
                                                                   window_x1y1x2y2);

    _window = system_window_create_not_fullscreen(OGL_CONTEXT_TYPE_GL,
                                                  window_x1y1x2y2,
                                                  system_hashed_ansi_string_create("Test window"),
                                                  false, /* scalable */
                                                  false, /* vsync_enabled */
                                                  true,  /* visible */
                                                  window_pf);
#endif

    _rendering_handler = ogl_rendering_handler_create_with_fps_policy(system_hashed_ansi_string_create("Default rendering handler"),
                                                                      60,
                                                                      _rendering_handler_callback,
                                                                      NULL);

    system_window_get_property(_window,
                               SYSTEM_WINDOW_PROPERTY_RENDERING_CONTEXT,
                              &_context);
    ogl_context_get_property  (_context,
                               OGL_CONTEXT_PROPERTY_FLYBY,
                              &_flyby);

    system_window_set_property(_window,
                               SYSTEM_WINDOW_PROPERTY_RENDERING_HANDLER,
                              &_rendering_handler);


    system_window_add_callback_func    (_window,
                                        SYSTEM_WINDOW_CALLBACK_FUNC_PRIORITY_NORMAL,
                                        SYSTEM_WINDOW_CALLBACK_FUNC_RIGHT_BUTTON_DOWN,
                                        (void*) _rendering_rbm_callback_handler,
                                        NULL);
    system_window_add_callback_func    (_window,
                                        SYSTEM_WINDOW_CALLBACK_FUNC_PRIORITY_NORMAL,
                                        SYSTEM_WINDOW_CALLBACK_FUNC_KEY_DOWN,
                                        (void*) _rendering_key_down_callback_handler,
                                        NULL);
    system_window_add_callback_func    (_window,
                                        SYSTEM_WINDOW_CALLBACK_FUNC_PRIORITY_NORMAL,
                                        SYSTEM_WINDOW_CALLBACK_FUNC_WINDOW_CLOSED,
                                        (void*) _rendering_window_closed_callback_handler,
                                        NULL);
    system_window_add_callback_func    (_window,
                                        SYSTEM_WINDOW_CALLBACK_FUNC_PRIORITY_NORMAL,
                                        SYSTEM_WINDOW_CALLBACK_FUNC_WINDOW_CLOSING,
                                        (void*) _rendering_window_closing_callback_handler,
                                        NULL);

    /* Initialize various states required to run the demo */
    if (!state_init(scene_filename) )
    {
        goto end;
    }

    /* Set up UI */
    ui_init();

    /* Carry on */
    ogl_rendering_handler_play(_rendering_handler,
                               0); /* time */

    system_event_wait_single(_window_closed_event);

    /* Clean up */
    ogl_rendering_handler_stop(_rendering_handler);

end:
    if (_window != NULL)
    {
        system_window_close(_window);

        _window = NULL;
    }

    system_event_release(_window_closed_event);

    main_force_deinit();

    return 0;
}