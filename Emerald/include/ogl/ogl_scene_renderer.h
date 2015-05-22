/**
 *
 * Emerald (kbi/elude @2014-2015)
 *
 */
#ifndef OGL_SCENE_RENDERER_H
#define OGL_SCENE_RENDERER_H

#include "mesh/mesh_types.h"
#include "ogl/ogl_types.h"
#include "scene/scene_types.h"

#define DEFAULT_AABB_MAX_VALUE (FLT_MIN)
#define DEFAULT_AABB_MIN_VALUE (FLT_MAX)

typedef enum
{
    /* Passes all objects that are located in front of the current camera.
     *
     * behavior_data parameter: [0]..[2]: world-space location of the camera
     *                          [3]..[5]: normalized view direction vector.
     *
     * This behavior is required for DPSM shadow map generation passes, where
     * there is no projection matrix available (the projection is done directly
     * in VS)
     */
    OGL_SCENE_RENDERER_FRUSTUM_CULLING_BEHAVIOR_PASS_OBJECTS_IN_FRONT_OF_CAMERA,

    /* Extracts clipping planes from current projection matrix and checks
     * if the pointed mesh intersects or is fully embedded within the frustum.
     *
     * behavior_data parameter: ignored
     *
     * Recommended culling behavior, as of now.
     */
    OGL_SCENE_RENDERER_FRUSTUM_CULLING_BEHAVIOR_USE_CAMERA_CLIPPING_PLANES,

    OGL_SCENE_RENDERER_FRUSTUM_CULLING_BEHAVIOR_UNKNOWN
} _ogl_scene_renderer_frustum_culling_behavior;
typedef enum
{
    HELPER_VISUALIZATION_NONE           = 0,
    HELPER_VISUALIZATION_BOUNDING_BOXES = 1 << 0,
    HELPER_VISUALIZATION_FRUSTUMS       = 1 << 3,
    HELPER_VISUALIZATION_NORMALS        = 1 << 1,
    HELPER_VISUALIZATION_LIGHTS         = 1 << 2,
} _ogl_scene_renderer_helper_visualization;

typedef enum
{
    /* All meshes will be forward-rendered */
    RENDER_MODE_FORWARD,

    /* TODO: RENDER_MODE_DEFERRED */

    /* TODO: RENDER_MODE_INFERRED? */

    /* (debug) All meshes will be shaded with their normal data */
    RENDER_MODE_NORMALS_ONLY,
    /* (debug) All meshes will be shaded with their texcoord data */
    RENDER_MODE_TEXCOORDS_ONLY,

    /* Crucial parts of the rendering process are handed over to
     * ogl_shadow_mapping. */
    RENDER_MODE_SHADOW_MAP,

} _ogl_scene_renderer_render_mode;

typedef enum
{
    /* general property, value: ogl_context */
    OGL_SCENE_RENDERER_PROPERTY_CONTEXT,

    /* general property, value: scene_graph */
    OGL_SCENE_RENDERER_PROPERTY_GRAPH,

    /* indexed property, key: mesh id // value: mesh */
    OGL_SCENE_RENDERER_PROPERTY_MESH_INSTANCE,

    /* indexed property, key: mesh id // value: system_matrix4x4.
     *
     * May not return any value if the model was frustum-culled out.
     */
    OGL_SCENE_RENDERER_PROPERTY_MESH_MODEL_MATRIX,

    /* indexed property, key: mesh id // value: system_matrix4x4 */
    OGL_SCENE_RENDERER_PROPERTY_MESH_NORMAL_MATRIX,

    /* general property, value: float[3] */
    OGL_SCENE_RENDERER_PROPERTY_VISIBLE_WORLD_AABB_MAX,

    /* general property, value: float[3] */
    OGL_SCENE_RENDERER_PROPERTY_VISIBLE_WORLD_AABB_MIN,

    /* general property, value: system_matrix4x4 */
    OGL_SCENE_RENDERER_PROPERTY_VP,
} ogl_scene_renderer_property;


/** TODO.
 *
 *  Bakes in advance all GPU assets required to render a given scene.
 *
 */
PUBLIC EMERALD_API void ogl_scene_renderer_bake_gpu_assets(__in __notnull ogl_scene_renderer renderer);

/** TODO.
 *
 *  OGL_SCENE_RENDERER_FRUSTUM_CULLING_BEHAVIOR_USE_CAMERA_CLIPPING_PLANES behavior implementation
 *  is based on http://www.lighthouse3d.com/tutorials/view-frustum-culling/geometric-approach-testing-boxes-ii/
 *  and http://gamedevs.org/uploads/fast-extraction-viewing-frustum-planes-from-world-view-projection-matrix.pdf.
 *
 *  This function also adjusts ogl_scene_renderer's max & min AABB coordinates.
 *
 *  @param renderer      TODO
 *  @param mesh_gpu      TODO
 *  @param behavior      TODO
 *  @param behavior_data Additional behavior-specific argument. Please see documentation of
 *                       _ogl_scene_renderer_frustum_culling_behavior for more details.
 *
 *  @return true if the mesh is inside OR intersects the frustum, false otherwise.
 *
 *  Note: if "true" is returned, OGL_SCENE_RENDERER_PROPERTY_VISIBLE_WORLD_AABB_MAX and
 *        OGL_SCENE_RENDERER_PROPERTY_VISIBLE_WORLD_AABB_MIN properties of @param renderer
 *        are also adjusted.
 */
PUBLIC bool ogl_scene_renderer_cull_against_frustum(__in __notnull ogl_scene_renderer                           renderer,
                                                    __in __notnull mesh                                         mesh_gpu,
                                                    __in           _ogl_scene_renderer_frustum_culling_behavior behavior,
                                                    __in_opt       const void*                                  behavior_data);

/** TODO. **/
PUBLIC EMERALD_API ogl_scene_renderer ogl_scene_renderer_create(__in __notnull ogl_context context,
                                                                __in __notnull scene       scene);

/** TODO. Internal usage only */
PUBLIC void ogl_scene_renderer_get_indexed_property(__in  __notnull const ogl_scene_renderer    renderer,
                                                    __in            ogl_scene_renderer_property property,
                                                    __in            uint32_t                    index,
                                                    __out __notnull void*                       out_result);

/** TODO */
PUBLIC EMERALD_API void ogl_scene_renderer_get_property(__in  __notnull ogl_scene_renderer          renderer,
                                                        __in            ogl_scene_renderer_property property,
                                                        __out __notnull void*                       out_result);

/** TODO.
 *
 *  TODO: state-ify render_mode / shadow_mapping_type / helper_visualization arguments..
 **/
PUBLIC EMERALD_API RENDERING_CONTEXT_CALL void ogl_scene_renderer_render_scene_graph(__in           __notnull ogl_scene_renderer                       renderer,
                                                                                     __in           __notnull system_matrix4x4                         view,
                                                                                     __in           __notnull system_matrix4x4                         projection,
                                                                                     __in           __notnull scene_camera                             camera,
                                                                                     __in                     const _ogl_scene_renderer_render_mode&   render_mode,
                                                                                     __in                     bool                                     apply_shadow_mapping,
                                                                                     __in           __notnull _ogl_scene_renderer_helper_visualization helper_visualization,
                                                                                     __in                     system_timeline_time                     frame_time);

/** TODO. **/
PUBLIC EMERALD_API void ogl_scene_renderer_release(__in __notnull ogl_scene_renderer renderer);

/** TODO */
PUBLIC EMERALD_API void ogl_scene_renderer_set_property(__in __notnull ogl_scene_renderer          renderer,
                                                        __in           ogl_scene_renderer_property property,
                                                        __in __notnull const void*                 data);

/** TODO.
 *
 *  Internal usage only (exposed specifically for ogl_shadow_mapping which
 *  handles the RENDER_MODE_SHADOW_MAP render mode).
 *
 **/
PUBLIC void ogl_scene_renderer_update_current_model_matrix(__in __notnull system_matrix4x4 transformation_matrix,
                                                           __in __notnull void*            renderer);

/** TODO.
 *
 *  Internal usage only (exposed specifically for ogl_shadow_mapping which
 *  handles the RENDER_MODE_SHADOW_MAP render mode).
 *
 **/
PUBLIC void ogl_scene_renderer_update_light_properties(__in __notnull scene_light light,
                                                       __in           void*       renderer);

#endif /* OGL_SCENE_RENDERER_H */