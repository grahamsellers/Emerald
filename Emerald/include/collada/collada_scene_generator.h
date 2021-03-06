/**
 *
 * Emerald (kbi/elude @2014)
 *
 * Private functions only.
 *
 * Used by collada_data to form an Emerald scene instance.
 */
#ifndef COLLADA_SCENE_GENERATOR_H
#define COLLADA_SCENE_GENERATOR_H

#include "collada/collada_types.h"
#include "ogl/ogl_context.h"
#include "scene/scene_types.h"

/** TODO */
PUBLIC scene collada_scene_generator_create(collada_data data,
                                            ogl_context  context,
                                            unsigned int n_scene);

#endif /* COLLADA_SCENE_GENERATOR_H */
