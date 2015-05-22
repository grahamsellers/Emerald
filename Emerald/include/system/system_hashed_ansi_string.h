/**
 *
 * Emerald (kbi/elude @2012-2015)
 *
 */
#ifndef SYSTEM_HASHED_ANSI_STRING_H
#define SYSTEM_HASHED_ANSI_STRING_H

#include "system/system_types.h"

/** Returns true if a given hashed ansi string contains another hashed ansi string.
 *
 *  @param system_hashed_ansi_string String to search for given sub-string.
 *  @param system_hashed_ansi_string Sub-string to look for.
 *
 *  @return true if found, false otherwise.
 */
PUBLIC EMERALD_API bool system_hashed_ansi_string_contains(__in __notnull system_hashed_ansi_string,
                                                           __in __notnull system_hashed_ansi_string);

/** Creates a new hashed ansi string instance. Such strings cannot be released and modified. 
 *  Text is copied by value so make sure input argument is valid.
 *
 *  If input text has already been used to create a hashed ansi string, existing instance
 *  will be returned.
 *
 *  @param const char* Text to store.
 *
 *  @return Hashed ansi string instance.
 */
PUBLIC EMERALD_API system_hashed_ansi_string system_hashed_ansi_string_create(__in __notnull const char*);

/** TODO */
PUBLIC EMERALD_API system_hashed_ansi_string system_hashed_ansi_string_create_substring(__in __notnull const char*,
                                                                                        __in           uint32_t    start_offset,
                                                                                        __in           uint32_t    length);

/** TODO */
PUBLIC EMERALD_API system_hashed_ansi_string system_hashed_ansi_string_create_by_merging_strings(uint32_t, __in __notnull const char**);

/** TODO */
PUBLIC EMERALD_API system_hashed_ansi_string system_hashed_ansi_string_create_by_merging_two_strings(__in __notnull const char*,
                                                                                                     __in __notnull const char*);

/** Retrieves an empty hashed ansi string.
 *
 *  @return Empty hashed ansi string.
 */
PUBLIC EMERALD_API system_hashed_ansi_string system_hashed_ansi_string_get_default_empty_string();

/** Returns length of a given hashed ansi string.
 *
 *  @param system_hashed_ansi_string Hashed ansi string to examine.
 * 
 *  @return Length.
 */
PUBLIC EMERALD_API uint32_t system_hashed_ansi_string_get_length(__in __notnull system_hashed_ansi_string);

/** Retrieves text stored for a given hashed ansi string.
 *
 *  @param system_hashed_ansi_string Hashed ansi string.
 *
 *  @return Raw data pointer.
 */
PUBLIC EMERALD_API const char* system_hashed_ansi_string_get_buffer(__in __notnull system_hashed_ansi_string);

/** Tells whether given hashed ansi string encapsulates the same text as stored at user-provided location.
 *
 *  @param system_hashed_ansi_string Hashed ansi string to use for comparison.
 *  @param system_hashed_ansi_string Raw pointer to use for comparison.
 *
 *  @return true if strings match, false otehrwise.
 */
PUBLIC EMERALD_API bool system_hashed_ansi_string_is_equal_to_raw_string(__in __notnull system_hashed_ansi_string,
                                                                         __in __notnull const char*);

/** Retrieves hash for a given hashed ansi string.
 *
 *  @param system_hashed_ansi_string Hashed ansi string.
 *
 *  @return Hash for the string.
 */
PUBLIC EMERALD_API system_hash64 system_hashed_ansi_string_get_hash(__in __notnull system_hashed_ansi_string);

/** Tells whether two hashed ansi strings are equal.
 *
 *  @param system_hashed_ansi_string First hashed ansi string to use for comparison.
 *  @param system_hashed_ansi_string Second hashed ansi string to use for comparison.
 *
 *  @return true if strings match, false otherwise. 
 */
PUBLIC EMERALD_API bool system_hashed_ansi_string_is_equal_to_hash_string(__in __notnull system_hashed_ansi_string,
                                                                          __in __notnull system_hashed_ansi_string);

/** Initializes dictionary used by hashed ansi string implementation. Should be called once from DLL entry point. */
PUBLIC void system_hashed_ansi_string_init();

/** Deinitializes dictionary used by hashed ansi string implementation. Should be called once from DLL entry point. */
PUBLIC void system_hashed_ansi_string_deinit();

#endif /* SYSTEM_HASHED_ANSI_STRING_H */
