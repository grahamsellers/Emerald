/**
 *
 * Internal Emerald scene viewer (kbi/elude @2014-2015)
 *
 */
#include "shared.h"
#include "ogl/ogl_curve_renderer.h"
#include "ogl/ogl_flyby.h"
#include "ogl/ogl_pipeline.h"
#include "ogl/ogl_scene_renderer.h"
#include "scene/scene.h"
#include "scene/scene_camera.h"
#include "scene/scene_graph.h"
#include "system/system_critical_section.h"
#include "system/system_resizable_vector.h"
#include "app_config.h"
#include "include/main.h"
#include "state.h"


uint32_t                   _active_camera_index          = 0;
uint32_t                   _active_camera_path_index     = 0; /* none */
float                      _animation_duration_float     = 0.0f;
system_timeline_time       _animation_duration_time      = 0;
system_critical_section    _camera_cs                    = NULL;
void**                     _camera_indices               = NULL;
void**                     _camera_path_indices          = NULL;
system_hashed_ansi_string* _camera_path_names            = NULL;
system_hashed_ansi_string* _camera_names                 = NULL;
system_resizable_vector    _cameras                      = NULL;
ogl_curve_renderer         _curve_renderer               = NULL;
ogl_curve_item_id          _curve_renderer_item_id       = -1;
ogl_pipeline               _pipeline                     = NULL;
uint32_t                   _pipeline_stage_id            = -1;
scene                      _scene                        = NULL;
system_timeline_time       _scene_duration               = 0;
ogl_scene_renderer         _scene_renderer               = NULL;


typedef struct _camera
{
    scene_camera              camera;
    bool                      is_flyby;
    system_hashed_ansi_string name;

    explicit _camera(bool                      in_is_flyby,
                     system_hashed_ansi_string in_name,
                     scene_camera              in_camera)
    {
        camera   = in_camera;
        is_flyby = in_is_flyby;
        name     = in_name;
    }
} _camera;


/** TODO */
PRIVATE void _init_cameras()
{
    unsigned int n_cameras = 0;

    /* How many cameras do we have? */
    scene_get_property(_scene,
                       SCENE_PROPERTY_N_CAMERAS,
                      &n_cameras);

    /* Iterate over all cameras and create a descriptor for each entry */
    _cameras = system_resizable_vector_create(4, /* capacity */
                                              sizeof(_camera*) );

    for (unsigned int n_camera = 0;
                      n_camera < n_cameras;
                    ++n_camera)
    {
        scene_camera current_camera = scene_get_camera_by_index(_scene,
                                                                n_camera);

        ASSERT_ALWAYS_ASYNC(current_camera != NULL,
                            "Could not retrieve camera at index [%d]",
                            n_camera);

        if (current_camera != NULL)
        {
            system_hashed_ansi_string current_camera_name = NULL;

            scene_camera_get_property(current_camera,
                                      SCENE_CAMERA_PROPERTY_NAME,
                                      0, /* time - irrelevant for camera name */
                                     &current_camera_name);

            /* Create a new descriptor */
            _camera* new_camera = new (std::nothrow) _camera(false, /* is_flyby */
                                                             current_camera_name,
                                                             current_camera);

            ASSERT_ALWAYS_SYNC(new_camera != NULL, "Out of memory");
            if (new_camera != NULL)
            {
                system_resizable_vector_push(_cameras,
                                             new_camera);
            }
        }
    }

    /* Add the 'flyby' camera */
    _camera* flyby_camera = new (std::nothrow) _camera(true,
                                                       system_hashed_ansi_string_create("Flyby camera"),
                                                       NULL);

    ASSERT_ALWAYS_SYNC(flyby_camera != NULL, "Out of memory");
    if (flyby_camera != NULL)
    {
        system_resizable_vector_push(_cameras, flyby_camera);
    }

    /* Create the list of camera names that will be shown under "active camera" dropdown */
    const uint32_t n_total_cameras = system_resizable_vector_get_amount_of_elements(_cameras);

    _camera_indices = new void*                    [n_total_cameras];
    _camera_names   = new system_hashed_ansi_string[n_total_cameras];

    for (uint32_t n_camera = 0;
                  n_camera < n_total_cameras;
                ++n_camera)
    {
        _camera* current_camera = NULL;

        system_resizable_vector_get_element_at(_cameras,
                                               n_camera,
                                              &current_camera);

        _camera_indices[n_camera] = (void*) n_camera;
        _camera_names  [n_camera] = current_camera->name;
    }

    /* Create the list of camera names that will be shown under "show camera path" dropdown */
    _camera_path_indices = new void*                    [n_total_cameras];
    _camera_path_names   = new system_hashed_ansi_string[n_total_cameras];

    for (uint32_t n_camera = 0;
                  n_camera < n_total_cameras;
                ++n_camera)
    {
        if (n_camera == 0)
        {
            _camera_path_indices[n_camera] = (void*) -1;
            _camera_path_names  [n_camera] = system_hashed_ansi_string_create("None");
        }
        else
        {
            _camera* current_camera = NULL;

            system_resizable_vector_get_element_at(_cameras,
                                                   n_camera - 1,
                                                  &current_camera);

            _camera_path_indices[n_camera] = (void*) (n_camera - 1);
            _camera_path_names  [n_camera] = current_camera->name;
        }
    }
}


/** Please see header for spec */
PUBLIC void state_deinit()
{
    if (_scene != NULL)
    {
        scene_release(_scene);

        _scene = NULL;
    }

    if (_scene_renderer != NULL)
    {
        ogl_scene_renderer_release(_scene_renderer);

        _scene_renderer = NULL;
    }

    if (_curve_renderer != NULL)
    {
        ogl_curve_renderer_release(_curve_renderer);

        _curve_renderer = NULL;
    }

    if (_pipeline != NULL)
    {
        ogl_pipeline_release(_pipeline);

        _pipeline = NULL;
    }

    if (_cameras != NULL)
    {
        while (system_resizable_vector_get_amount_of_elements(_cameras) > 0)
        {
            _camera* current_camera = NULL;

            system_resizable_vector_pop(_cameras, &current_camera);
            delete current_camera;
        }
        system_resizable_vector_release(_cameras);
    }

    if (_camera_cs != NULL)
    {
        system_critical_section_release(_camera_cs);

        _camera_cs = NULL;
    }

    if (_camera_indices != NULL)
    {
        delete [] _camera_indices;

        _camera_indices = NULL;
    }

    if (_camera_names != NULL)
    {
        delete [] _camera_names;

        _camera_names = NULL;
    }

    if (_camera_path_indices != NULL)
    {
        delete [] _camera_path_indices;

        _camera_path_indices = NULL;
    }

    if (_camera_path_names != NULL)
    {
        delete [] _camera_path_names;

        _camera_names = NULL;
    }
}

/** Please see header for spec */
PUBLIC unsigned int state_get_active_camera_index()
{
    return _active_camera_index;
}

/** Please see header for spec */
PUBLIC unsigned int state_get_active_camera_path_index()
{
    return _active_camera_path_index;
}

/** Please see header for spec */
PUBLIC system_timeline_time state_get_animation_duration_time()
{
    return _animation_duration_time;
}

/** Please see header for spec */
PUBLIC void** state_get_camera_indices()
{
    return _camera_indices;
}

/** Please see header for spec */
PUBLIC system_hashed_ansi_string* state_get_camera_names()
{
    return _camera_names;
}

/** Please see header for spec */
PUBLIC system_resizable_vector state_get_cameras()
{
    return _cameras;
}

/** Please see header for spec */
PUBLIC ogl_curve_renderer state_get_curve_renderer()
{
    return _curve_renderer;
}

/** Please see header for spec */
PUBLIC ogl_curve_item_id state_get_curve_renderer_item_id()
{
    return _curve_renderer_item_id;
}

/** Please see header for spec */
PUBLIC uint32_t state_get_number_of_cameras()
{
    return system_resizable_vector_get_amount_of_elements(_cameras);
}

/** Please see header for spec */
PUBLIC ogl_pipeline state_get_pipeline()
{
    return _pipeline;
}

/** Please see header for spec */
PUBLIC uint32_t state_get_pipeline_stage_id()
{
    return _pipeline_stage_id;
}

/** Please see header for spec */
PUBLIC scene state_get_scene()
{
    return _scene;
}

/** Please see header for spec */
PUBLIC ogl_scene_renderer state_get_scene_renderer()
{
    return _scene_renderer;
}

/** Please see header for spec */
PUBLIC void** state_get_camera_path_indices()
{
    return _camera_path_indices;
}

/** Please see header for spec */
PUBLIC system_hashed_ansi_string* state_get_camera_path_names()
{
    return _camera_path_names;
}

/** Please see header for spec */
PUBLIC void state_init(__in __notnull system_hashed_ansi_string scene_filename)
{
    const float camera_start_position[3] = {0, 0, 0};

    /* Load the scene */
    _scene = scene_load(_context,
                        scene_filename);

    /* Enumerate all cameras */
    _camera_cs = system_critical_section_create();

    _init_cameras();

    /* Determine animation duration */
    float                animation_duration_float = 0.0f;
    system_timeline_time animation_duration       = 0;

    scene_get_property(_scene,
                       SCENE_PROPERTY_MAX_ANIMATION_DURATION,
                      &animation_duration_float);

    _scene_duration = system_time_get_timeline_time_for_msec( uint32_t(animation_duration_float * 1000.0f) );

    /* Determine the animation duration. */
    scene_get_property(_scene,
                       SCENE_PROPERTY_MAX_ANIMATION_DURATION,
                      &_animation_duration_float);

    _animation_duration_time = system_time_get_timeline_time_for_msec( uint32_t(_animation_duration_float * 1000.0f) );

    /* Carry on initializing */
    const float movement_delta = MOVEMENT_DELTA;

    _scene_renderer = ogl_scene_renderer_create(_context,
                                                _scene);

    ogl_flyby_activate    (_context,
                           camera_start_position);
    ogl_flyby_set_property(_context,
                           OGL_FLYBY_PROPERTY_MOVEMENT_DELTA,
                          &movement_delta);

    /* Construct the pipeline object */
    _pipeline = ogl_pipeline_create(_context,
                                    true, /* should_overlay_performance_info */
                                    system_hashed_ansi_string_create("Pipeline") );

    _pipeline_stage_id = ogl_pipeline_add_stage(_pipeline);

    ogl_pipeline_add_stage_step(_pipeline,
                                _pipeline_stage_id,
                                system_hashed_ansi_string_create("Scene rendering"),
                                _render_scene,
                                NULL);

    /* Spawn curve renderer */
    _curve_renderer = ogl_curve_renderer_create(_context,
                                                system_hashed_ansi_string_create("Curve renderer") );
}

/** Please see header for spec */
PUBLIC void state_lock_current_camera(__out __notnull scene_camera* out_current_camera,
                                      __out           bool*         out_is_flyby)
{
    system_critical_section_enter(_camera_cs);
    {
        _camera* camera_ptr = NULL;

        /* Retrieve camera descriptor */
        system_resizable_vector_get_element_at(state_get_cameras(),
                                               state_get_active_camera_index(),
                                              &camera_ptr);

        ASSERT_DEBUG_SYNC(camera_ptr != NULL, "Could not retrieve active camera descriptor");
        if (camera_ptr != NULL)
        {
            *out_current_camera = camera_ptr->camera;
            *out_is_flyby       = camera_ptr->is_flyby;
        }
    }
}

/** Please see header for spec */
PUBLIC void state_set_active_camera_index(unsigned int index)
{
    system_critical_section_enter(_camera_cs);
    {
        _active_camera_index = (unsigned int) index;
    }
    system_critical_section_leave(_camera_cs);
}

/** Please see header for spec */
PUBLIC void state_set_active_camera_path_index(unsigned int index)
{
    _active_camera_path_index = index;

    if (_curve_renderer_item_id != -1)
    {
        ogl_curve_renderer_delete_curve(_curve_renderer,
                                        _curve_renderer_item_id);

        _curve_renderer_item_id = -1;
    }

    if (_active_camera_path_index != -1) /* None */
    {
        /* Retrieve the camera descriptor */
        _camera* camera_ptr = NULL;

        system_resizable_vector_get_element_at(_cameras,
                                               _active_camera_path_index,
                                              &camera_ptr);

        ASSERT_DEBUG_SYNC(camera_ptr != NULL, "Could not retrieve camera descriptor");
        if (camera_ptr != NULL)
        {
            /* Configure the curve renderer to show the camera path */
            scene_graph_node     camera_node              = NULL;
            const float          curve_color[4]           = {1.0f, 1.0f, 1.0f, 1.0f};
            scene_graph          graph                    = NULL;

            scene_get_property(_scene,
                               SCENE_PROPERTY_GRAPH,
                              &graph);

            camera_node = scene_graph_get_node_for_object(graph,
                                                          SCENE_OBJECT_TYPE_CAMERA,
                                                          camera_ptr->camera);
            ASSERT_DEBUG_SYNC(camera_node != NULL,
                              "Could not retrieve owner node for selected camera.");

            _curve_renderer_item_id = ogl_curve_renderer_add_scene_graph_node_curve(_curve_renderer,
                                                                                    graph,
                                                                                    camera_node,
                                                                                    curve_color,
                                                                                    _scene_duration,
                                                                                    15,      /* n_samples_per_second */
                                                                                    10.0f); /* view_vector_length */
       }
    }
}

/** Please see header for spec */
PUBLIC void state_unlock_current_camera()
{
    system_critical_section_leave(_camera_cs);
}