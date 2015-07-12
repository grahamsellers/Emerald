/**
 *
 * Emerald (kbi/elude @2014)
 *
 * Private functions only.
 */
#ifndef COLLADA_DATA_SAMPLER2D_H
#define COLLADA_DATA_SAMPLER2D_H

#include "collada/collada_data_surface.h"
#include "collada/collada_types.h"
#include "tinyxml2.h"

/** TODO */
PUBLIC collada_data_sampler2D collada_data_sampler2D_create(system_hashed_ansi_string id,
                                                            tinyxml2::XMLElement*     element_ptr,
                                                            system_hash64map          surfaces_by_id_map);

/** TODO */
PUBLIC void collada_data_sampler2D_get_properties(const collada_data_sampler2D        sampler,
                                                       collada_data_surface*         out_surface,
                                                       _collada_data_sampler_filter* out_mag_filter,
                                                       _collada_data_sampler_filter* out_min_filter);

/** TODO */
PUBLIC _collada_data_sampler_filter collada_data_get_sampler_filter_from_filter_node(tinyxml2::XMLElement* element_ptr);

/** TODO */
PUBLIC void collada_data_sampler2D_release(collada_data_sampler2D sampler);

#endif /* COLLADA_DATA_SAMPLER2D_H */
