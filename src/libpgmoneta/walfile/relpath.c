/*
 * Copyright (C) 2025 The pgmoneta community
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * get_relation_path - construct path to a relation's file
 *
 * Result is a palloc'd string.
 *
 * Note: ideally, backendId would be declared as type backend_id, but relpath.h
 * would have to include a backend-only header to do that; doesn't seem worth
 * the trouble considering backend_id is just int anyway.
 */

 #include <walfile/relpath.h>
 #include <assert.h>
 #include <utils.h>
 #include <stdlib.h>
 #include <errno.h>
 
 /* Constants for configuration */
 #define MAX_VERSION_DIR_SIZE 50
 #define MIN_PG_VERSION 13
 #define MAX_PG_VERSION 17
 
 /* Validation helper for fork numbers */
 static inline bool is_valid_fork_number(enum fork_number forkNumber) {
     return forkNumber >= MAIN_FORKNUM && forkNumber <= INIT_FORKNUM;
 }
 
 /* Static array for fork names to improve performance */
 static const char* const FORK_NAMES[] = {
     "main",                       /* MAIN_FORKNUM */
     "fsm",                        /* FSM_FORKNUM */
     "vm",                         /* VISIBILITYMAP_FORKNUM */
     "init"                        /* INIT_FORKNUM */
 };
 
 char* pgmoneta_wal_get_relation_path(oid dbNode, oid spcNode, oid relNode,
                                     int backendId, enum fork_number forkNumber)
 {
     /* Input validation */
     if (!is_valid_fork_number(forkNumber)) {
         errno = EINVAL;
         return NULL;
     }
 
     char* path = NULL;
 
     if (spcNode == GLOBALTABLESPACE_OID) {
         /* Shared system relations live in {datadir}/global */
         if (dbNode != 0 || backendId != INVALID_BACKEND_ID) {
             errno = EINVAL;
             return NULL;
         }
 
         path = pgmoneta_format_and_append(path,
             forkNumber != MAIN_FORKNUM ? "global/%u_%s" : "global/%u",
             relNode, forkNumber != MAIN_FORKNUM ? FORK_NAMES[forkNumber] : NULL);
     }
     else if (spcNode == DEFAULTTABLESPACE_OID) {
         /* The default tablespace is {datadir}/base */
         if (backendId == INVALID_BACKEND_ID) {
             path = pgmoneta_format_and_append(path,
                 forkNumber != MAIN_FORKNUM ? "base/%u/%u_%s" : "base/%u/%u",
                 dbNode, relNode,
                 forkNumber != MAIN_FORKNUM ? FORK_NAMES[forkNumber] : NULL);
         } else {
             path = pgmoneta_format_and_append(path,
                 forkNumber != MAIN_FORKNUM ? "base/%u/t%d_%u_%s" : "base/%u/t%d_%u",
                 dbNode, backendId, relNode,
                 forkNumber != MAIN_FORKNUM ? FORK_NAMES[forkNumber] : NULL);
         }
     }
     else {
         /* All other tablespaces are accessed via symlinks */
         char* version_directory = pgmoneta_wal_get_tablespace_version_directory();
         if (!version_directory) {
             return NULL; /* errno already set */
         }
 
         if (backendId == INVALID_BACKEND_ID) {
             path = pgmoneta_format_and_append(path,
                 forkNumber != MAIN_FORKNUM ? 
                 "pg_tblspc/%u/%s/%u/%u_%s" : "pg_tblspc/%u/%s/%u/%u",
                 spcNode, version_directory, dbNode, relNode,
                 forkNumber != MAIN_FORKNUM ? FORK_NAMES[forkNumber] : NULL);
         } else {
             path = pgmoneta_format_and_append(path,
                 forkNumber != MAIN_FORKNUM ? 
                 "pg_tblspc/%u/%s/%u/t%d_%u_%s" : "pg_tblspc/%u/%s/%u/t%d_%u",
                 spcNode, version_directory, dbNode, backendId, relNode,
                 forkNumber != MAIN_FORKNUM ? FORK_NAMES[forkNumber] : NULL);
         }
         free(version_directory);
     }
 
     if (!path) {
         errno = ENOMEM;
     }
     return path;
 }
 
 char* pgmoneta_wal_get_tablespace_version_directory(void)
 {
     if (!server_config) {
         errno = EINVAL;
         return NULL;
     }
 
     char* result = (char*)malloc(MAX_VERSION_DIR_SIZE);
     if (!result) {
         errno = ENOMEM;
         return NULL;
     }
 
     const char* catalog_version = pgmoneta_wal_get_catalog_version_number();
     if (!catalog_version) {
         free(result);
         return NULL; /* errno already set */
     }
 
     if (!pgmoneta_format_and_append(result, "PG_%d_%s", 
         server_config->version, catalog_version)) {
         free(result);
         errno = ENOMEM;
         return NULL;
     }
 
     return result;
 }
 
 char* pgmoneta_wal_get_catalog_version_number(void)
 {
     if (!server_config || server_config->version < MIN_PG_VERSION || 
         server_config->version > MAX_PG_VERSION) {
         errno = EINVAL;
         return NULL;
     }
 
     switch (server_config->version) {
         case 13: return "202004022";
         case 14: return "202104081";
         case 15: return "202204062";
         case 16: return "202303311";
         case 17: return "202407111";
         default:
             errno = EINVAL;
             return NULL;
     }
 }