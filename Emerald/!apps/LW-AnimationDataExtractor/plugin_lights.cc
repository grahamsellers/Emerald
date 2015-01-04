#define _MSWIN

#include "shared.h"

#include "plugin.h"
#include "plugin_common.h"
#include "plugin_curves.h"
#include "plugin_lights.h"
#include "plugin_ui.h"
#include "scene/scene.h"
#include "scene/scene_light.h"
#include "system/system_assertions.h"
#include "system/system_event.h"
#include "system/system_hashed_ansi_string.h"
#include "system/system_hash64map.h"
#include "system/system_thread_pool.h"


typedef struct _light_data
{
    void*           object_id;
    void*           parent_object_id;
    float           pivot             [3];
    curve_container rotation_curves   [3]; /* hpb */
    curve_container translation_curves[3]; /* xyz */

    _light_data()
    {
        object_id        = NULL;
        parent_object_id = NULL;

        memset(pivot,
               0,
               sizeof(pivot) );
        memset(rotation_curves,
               0,
               sizeof(rotation_curves) );
        memset(translation_curves,
               0,
               sizeof(translation_curves) );
    }
} _light_data;


PRIVATE system_event     job_done_event                = NULL;
PRIVATE system_hash64map scene_light_to_light_data_map = NULL;


/** TODO */
PRIVATE void _update_light_properties(__in __notnull scene_light               new_light,
                                      __in           scene_light_type          new_light_type,
                                      __in __notnull system_hashed_ansi_string new_light_name_has,
                                      __in           LWItemID                  new_light_item_id,
                                      __in           LWDVector                 new_light_pivot,
                                      __in __notnull system_hash64map          curve_id_to_curve_container_map)
{
    curve_container new_light_color_r         = NULL;
    curve_container new_light_color_g         = NULL;
    curve_container new_light_color_b         = NULL;
    curve_container new_light_color_intensity = NULL;
    curve_container new_light_rotation_b      = NULL;
    curve_container new_light_rotation_h      = NULL;
    curve_container new_light_rotation_p      = NULL;
    curve_container new_light_translation_x   = NULL;
    curve_container new_light_translation_y   = NULL;
    curve_container new_light_translation_z   = NULL;
    bool            new_light_uses_shadow_map = false;

    new_light_color_r         = GetCurveContainerForProperty(new_light_name_has,
                                                             (new_light_type == SCENE_LIGHT_TYPE_AMBIENT) ? ITEM_PROPERTY_LIGHT_AMBIENT_COLOR_R
                                                                                                          : ITEM_PROPERTY_LIGHT_COLOR_R,
                                                             new_light_item_id,
                                                             curve_id_to_curve_container_map);
    new_light_color_g         = GetCurveContainerForProperty(new_light_name_has,
                                                             (new_light_type == SCENE_LIGHT_TYPE_AMBIENT) ? ITEM_PROPERTY_LIGHT_AMBIENT_COLOR_G
                                                                                                          : ITEM_PROPERTY_LIGHT_COLOR_G,
                                                             new_light_item_id,
                                                             curve_id_to_curve_container_map);
    new_light_color_b         = GetCurveContainerForProperty(new_light_name_has,
                                                             (new_light_type == SCENE_LIGHT_TYPE_AMBIENT) ? ITEM_PROPERTY_LIGHT_AMBIENT_COLOR_B
                                                                                                          : ITEM_PROPERTY_LIGHT_COLOR_B,
                                                             new_light_item_id,
                                                             curve_id_to_curve_container_map);

    /* Extract other relevant curves */
    new_light_color_intensity = GetCurveContainerForProperty(new_light_name_has,
                                                             (new_light_type == SCENE_LIGHT_TYPE_AMBIENT) ? ITEM_PROPERTY_LIGHT_AMBIENT_INTENSITY
                                                                                                          : ITEM_PROPERTY_LIGHT_COLOR_INTENSITY,
                                                             new_light_item_id,
                                                             curve_id_to_curve_container_map);

    if (new_light_type != SCENE_LIGHT_TYPE_AMBIENT)
    {
        new_light_rotation_b      = GetCurveContainerForProperty(new_light_name_has,
                                                                 ITEM_PROPERTY_ROTATION_B,
                                                                 new_light_item_id,
                                                                 curve_id_to_curve_container_map);
        new_light_rotation_h      = GetCurveContainerForProperty(new_light_name_has,
                                                                 ITEM_PROPERTY_ROTATION_H,
                                                                 new_light_item_id,
                                                                 curve_id_to_curve_container_map);
        new_light_rotation_p      = GetCurveContainerForProperty(new_light_name_has,
                                                                 ITEM_PROPERTY_ROTATION_P,
                                                                 new_light_item_id,
                                                                 curve_id_to_curve_container_map);
        new_light_translation_x   = GetCurveContainerForProperty(new_light_name_has,
                                                                 ITEM_PROPERTY_TRANSLATION_X,
                                                                 new_light_item_id,
                                                                 curve_id_to_curve_container_map);
        new_light_translation_y   = GetCurveContainerForProperty(new_light_name_has,
                                                                 ITEM_PROPERTY_TRANSLATION_Y,
                                                                 new_light_item_id,
                                                                 curve_id_to_curve_container_map);
        new_light_translation_z   = GetCurveContainerForProperty(new_light_name_has,
                                                                 ITEM_PROPERTY_TRANSLATION_Z,
                                                                 new_light_item_id,
                                                                 curve_id_to_curve_container_map);
        new_light_uses_shadow_map = !(light_info_ptr->shadowType(new_light_item_id) == LWLSHAD_OFF);

        /* Adjust translation curves relative to the pivot */
        for (uint32_t n_translation_curve = 0;
                      n_translation_curve < 3;
                    ++n_translation_curve)
        {
            curve_container curve                       = (n_translation_curve == 0) ? new_light_translation_x :
                                                          (n_translation_curve == 1) ? new_light_translation_y :
                                                                                       new_light_translation_z;
            const float     pivot_translation_component = (float) new_light_pivot[n_translation_curve];

            AdjustCurveByDelta(curve,
                               -pivot_translation_component);
        } /* for (all three translation curves) */
    }

    /* Instantiate new light descriptor */
    _light_data* new_light_data = new (std::nothrow) _light_data;

    ASSERT_ALWAYS_SYNC(new_light_data != NULL,
                       "Out of memory");

    /* Configure the light using the properties we've retrieved */
    curve_container new_color_curves[] =
    {
        new_light_color_r,
        new_light_color_g,
        new_light_color_b
    };
    curve_container new_light_rotation_curves[] =
    {
        new_light_rotation_h,
        new_light_rotation_p,
        new_light_rotation_b
    };
    curve_container new_light_translation_curves[] =
    {
        new_light_translation_x,
        new_light_translation_y,
        new_light_translation_z
    };

    scene_light_set_property(new_light,
                             SCENE_LIGHT_PROPERTY_COLOR,
                             new_color_curves);
    scene_light_set_property(new_light,
                             SCENE_LIGHT_PROPERTY_COLOR_INTENSITY,
                            &new_light_color_intensity);

    new_light_data->object_id        = new_light_item_id;
    new_light_data->parent_object_id = item_info_ptr->parent(new_light_item_id);
    new_light_data->pivot[0]         = (float)  new_light_pivot[0];
    new_light_data->pivot[1]         = (float)  new_light_pivot[1];
    new_light_data->pivot[2]         = (float) -new_light_pivot[2];

    memcpy(new_light_data->rotation_curves,
           new_light_rotation_curves,
           sizeof(new_light_rotation_curves) );
    memcpy(new_light_data->translation_curves,
           new_light_translation_curves,
           sizeof(new_light_translation_curves) );

    if (new_light_type != SCENE_LIGHT_TYPE_AMBIENT)
    {
        scene_light_set_property(new_light,
                                 SCENE_LIGHT_PROPERTY_USES_SHADOW_MAP,
                                &new_light_uses_shadow_map);
    }

    system_hash64map_insert(scene_light_to_light_data_map,
                            (system_hash64) new_light,
                            new_light_data,
                            NULL,  /* on_remove_callback */
                            NULL); /* on_remove_callback_user_arg */
}

/** TODO */
volatile void ExtractLightDataWorkerThreadEntryPoint(__in __notnull void* in_scene_arg)
{
    system_hash64map curve_id_to_curve_container_map = GetEnvelopeIDToCurveContainerHashMap();
    scene            in_scene                        = (scene) in_scene_arg;
    char             text_buffer[1024]               = {0};

    /* Extract available lights.
     *
     * Also remember to extract ambient light data.
     */
    LWItemID light_item_id = item_info_ptr->first(LWI_LIGHT,
                                                  LWITEM_NULL);

    while (light_item_id != LWITEM_NULL)
    {
        /* Sanity check */
        ASSERT_DEBUG_SYNC(item_info_ptr->parent(light_item_id) == 0,
                          "Light parenting is not currently supported");

        /* Sanity check: no pivot */
        LWDVector pivot;

        item_info_ptr->param(light_item_id,
                             LWIP_PIVOT,
                             0, /* time */
                             pivot);

        /* Extract light name & type */
        const char*               light_name         = item_info_ptr->name             (light_item_id);
        system_hashed_ansi_string light_name_has     = system_hashed_ansi_string_create(light_name);
        int                       light_type         = light_info_ptr->type            (light_item_id);
        scene_light_type          light_type_emerald = SCENE_LIGHT_TYPE_UNKNOWN;

        sprintf_s(text_buffer,
                  sizeof(text_buffer),
                  "Extracting light [%s] data..",
                  light_name);

        SetActivityDescription(text_buffer);

        /* Instantiate a new light */
        scene_light new_light = NULL;

        switch (light_type)
        {
            case LWLIGHT_DISTANT:
            {
                light_type_emerald = SCENE_LIGHT_TYPE_DIRECTIONAL;
                new_light          = scene_light_create_directional(light_name_has);

                break;
            } /* case LWLIGHT_DISTANT: */

            case LWLIGHT_POINT:
            {
                light_type_emerald = SCENE_LIGHT_TYPE_POINT;
                new_light          = scene_light_create_point(light_name_has);

                break;
            } /* case LWLIGHT_POINT: */

            default:
            {
                ASSERT_ALWAYS_SYNC(false,
                                   "Unrecognized light type");
            }
        } /* switch (light_type) */

        ASSERT_ALWAYS_SYNC(new_light != NULL,
                           "scene_light_create() failed.");

        /* Extract light properties */
        _update_light_properties(new_light,
                                 light_type_emerald,
                                 light_name_has,
                                 light_item_id,
                                 pivot,
                                 curve_id_to_curve_container_map);

        /* Add the light */
        if (!scene_add_light(in_scene,
                             new_light) )
        {
            ASSERT_ALWAYS_SYNC(false,
                               "Could not add new light [%s]",
                               light_name);
        }

        /* Move to next light */
        light_item_id = item_info_ptr->next(light_item_id);
    } /* while (light_item_id != LWITEM_NULL) */

    /* Extract ambient light props & add the light to the scene */
    double                    ambient_light_pivot[3] = {0, 0, 0};
    system_hashed_ansi_string scene_name             = NULL;

    SetActivityDescription("Extracting ambient light data..");

    scene_get_property(in_scene,
                       SCENE_PROPERTY_NAME,
                      &scene_name);

    system_hashed_ansi_string ambient_light_name_has = system_hashed_ansi_string_create_by_merging_two_strings(system_hashed_ansi_string_get_buffer(scene_name),
                                                                                                                                 " ambient light");
    scene_light               ambient_light          = scene_light_create_ambient(ambient_light_name_has);

    _update_light_properties(ambient_light,
                             SCENE_LIGHT_TYPE_AMBIENT,
                             ambient_light_name_has,
                             0, /* new_light_item_id */
                             ambient_light_pivot,
                             curve_id_to_curve_container_map);

    if (!scene_add_light(in_scene,
                         ambient_light) )
    {
        ASSERT_ALWAYS_SYNC(false,
                           "Could not add ambient light.");
    }

    /* All done */
    system_event_set(job_done_event);

    SetActivityDescription("Inactive");
}


/** Please see header for spec */
PUBLIC void DeinitLightData()
{
    if (scene_light_to_light_data_map != NULL)
    {
        const uint32_t n_map_entries = system_hash64map_get_amount_of_elements(scene_light_to_light_data_map);

        for (uint32_t n_entry = 0;
                      n_entry < n_map_entries;
                    ++n_entry)
        {
            _light_data* light_ptr = NULL;

            if (!system_hash64map_get_element_at(scene_light_to_light_data_map,
                                                 n_entry,
                                                &light_ptr,
                                                 NULL) ) /* outHash */
            {
                ASSERT_DEBUG_SYNC(false,
                                  "Could not retrieve light descriptor at index [%d]",
                                  n_entry);

                continue;
            }

            delete light_ptr;
            light_ptr = NULL;
        } /* for (all map entries) */

        system_hash64map_release(scene_light_to_light_data_map);
        scene_light_to_light_data_map = NULL;
    }
}

/* Please see header for spec */
PUBLIC void GetLightPropertyValue(__in  __notnull scene_light   light,
                                  __in            LightProperty property,
                                  __out __notnull void*         out_result)
{
    _light_data* light_ptr = NULL;

    if (!system_hash64map_get(scene_light_to_light_data_map,
                              (system_hash64) light,
                             &light_ptr) )
    {
        ASSERT_ALWAYS_SYNC(false,
                           "Could not find descriptor for input scene_light");

        goto end;
    }

    switch (property)
    {
        case LIGHT_PROPERTY_OBJECT_ID:
        {
            *(void**) out_result = light_ptr->object_id;

            break;
        }

        case LIGHT_PROPERTY_PARENT_OBJECT_ID:
        {
            *(void**) out_result = light_ptr->parent_object_id;

            break;
        }

        case LIGHT_PROPERTY_PIVOT:
        {
            *(float**) out_result = light_ptr->pivot;

            break;
        }

        case LIGHT_PROPERTY_ROTATION_HPB_CURVES:
        {
            *(curve_container**) out_result = light_ptr->rotation_curves;

            break;
        }

        case LIGHT_PROPERTY_TRANSLATION_CURVES:
        {
            *(curve_container**) out_result = light_ptr->translation_curves;

            break;
        }

        default:
        {
            ASSERT_ALWAYS_SYNC(false,
                               "Unrecognized LightProperty value");
        }
    } /* switch (property) */

end:
    ;
}

/* Please see header for spec */
PUBLIC void InitLightData()
{
    scene_light_to_light_data_map = system_hash64map_create(sizeof(_light_data*) );
}

/** Please see header for spec */
PUBLIC system_event StartLightDataExtraction(__in __notnull scene in_scene)
{
    job_done_event = system_event_create(false,  /* manual_reset */
                                         false); /* start_state */

    /* Spawn a worker thread so that we can report the progress. */
    system_thread_pool_task_descriptor task = system_thread_pool_create_task_descriptor_handler_only(THREAD_POOL_TASK_PRIORITY_NORMAL,
                                                                                                     ExtractLightDataWorkerThreadEntryPoint,
                                                                                                     in_scene);

    system_thread_pool_submit_single_task(task);

    return job_done_event;
}