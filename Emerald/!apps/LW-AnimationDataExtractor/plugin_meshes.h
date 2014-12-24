/**
 *
 * Emerald (kbi/elude @2014)
 *
 */
#ifndef PLUGIN_MESHES_H
#define PLUGIN_MESHES_H

#include "scene/scene_types.h"
#include "system/system_types.h"

typedef enum
{
    MESH_PROPERTY_PARENT_SCENE_MESH,   /* scene_mesh         */
    MESH_PROPERTY_ROTATION_HPB_CURVES, /* curve_container[3] */
    MESH_PROPERTY_TRANSLATION_CURVES,  /* curve_container[3] */
} MeshProperty;

/** TODO */
PUBLIC void DeinitMeshData();

/** TODO */
PUBLIC void FillSceneWithMeshData(__in __notnull scene            scene,
                                  __in __notnull system_hash64map envelope_id_to_curve_container_map);

/** TODO */
PUBLIC void GetMeshProperty(__in  __notnull scene_mesh   mesh_instance,
                            __in            MeshProperty property,
                            __out __notnull void*        out_result);

/** TODO */
PUBLIC void InitMeshData();

#endif /* PLUGIN_MESHES_H */