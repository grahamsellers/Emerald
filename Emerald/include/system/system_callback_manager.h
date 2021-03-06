/**
 *
 * Emerald (kbi/elude @2012-2015)
 *
 */
#ifndef SYSTEM_CALLBACK_MANAGER_H
#define SYSTEM_CALLBACK_MANAGER_H

#include "system_types.h"

/** Defines global call-back manager's call-back ID.
 *  Custom call-back manager are OK to use different sets of IDs.
 *
 *  Call-back IDs should be kept as small as possible.
 */
typedef enum
{
    CALLBACK_ID_CURVE_CONTAINER_ADDED,   /* callback_proc_data: not used */
    CALLBACK_ID_CURVE_CONTAINER_DELETED, /* callback_proc_data: not used */


    /* Always last */
    CALLBACK_ID_COUNT
} _callback_id;

typedef enum
{
    CALLBACK_SYNCHRONICITY_ASYNCHRONOUS,
    CALLBACK_SYNCHRONICITY_SYNCHRONOUS,

    CALLBACK_SYNCHRONICITY_UNKNOWN
} _callback_synchronicity;

/** Defines a call-back function pointer type */
typedef void (*PFNSYSTEMCALLBACKPROC)(const void* callback_data,
                                            void* user_arg);

/** TODO.
 *
 *  Internal usage only.
 **/
PUBLIC void system_callback_manager_call_back(system_callback_manager callback_manager,
                                              int                     callback_id,
                                              void*                   callback_proc_data);

/** TODO.
 *
 *  Internal usage only.
 **/
PUBLIC system_callback_manager system_callback_manager_create(_callback_id max_callback_id);

/** TODO */
PUBLIC void system_callback_manager_deinit();

/** TODO */
PUBLIC EMERALD_API system_callback_manager system_callback_manager_get();

/** TODO */
PUBLIC void system_callback_manager_init();

/** TODO
 *
 *  Internal usage only.
 **/
PUBLIC void system_callback_manager_release(system_callback_manager callback_manager);

/** TODO */
PUBLIC EMERALD_API void system_callback_manager_subscribe_for_callbacks(system_callback_manager callback_manager,
                                                                        int                     callback_id,
                                                                        _callback_synchronicity callback_synchronicity,
                                                                        PFNSYSTEMCALLBACKPROC   pfn_callback_proc,
                                                                        void*                   callback_proc_user_arg);

/** TODO */
PUBLIC EMERALD_API void system_callback_manager_unsubscribe_from_callbacks(system_callback_manager callback_manager,
                                                                           int                     callback_id,
                                                                           PFNSYSTEMCALLBACKPROC   pfn_callback_proc,
                                                                           void*                   callback_proc_user_arg);

#endif /* SYSTEM_CALLBACK_MANAGER_H */
