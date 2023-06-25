/*
 * Copyright (C) 2023 Red Hat
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

/* pgmoneta */
#include <pgmoneta.h>
#include <achv.h>
#include <info.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <workflow.h>
#include <restore.h>
#include <utils.h>

#include <archive.h>
#include <archive_entry.h>
#include <stdio.h>

void
pgmoneta_archive(int client_fd, int server, char* backup_id, char* position, char* directory, char** argv)
{
   char elapsed[128];
   time_t start_time;
   int total_seconds;
   int hours;
   int minutes;
   int seconds;
   char* to = NULL;
   char* id = NULL;
   char* output = NULL;
   int result = 1;
   struct workflow* workflow = NULL;
   struct workflow* current = NULL;
   struct node* i_nodes = NULL;
   struct node* o_nodes = NULL;
   struct node* i_ident = NULL;
   struct node* i_directory = NULL;
   struct node* i_output = NULL;
   struct configuration* config;

   pgmoneta_start_logging();

   config = (struct configuration*)shmem;

   pgmoneta_set_proc_title(1, argv, "archive", config->servers[server].name);

   start_time = time(NULL);

   if (!pgmoneta_restore_backup(server, backup_id, position, directory, &output, &id))
   {
      result = 0;

      if (pgmoneta_create_node_string(directory, "directory", &i_directory))
      {
         goto error;
      }

      pgmoneta_append_node(&i_nodes, i_directory);

      if (pgmoneta_create_node_string(id, "id", &i_ident))
      {
         goto error;
      }

      pgmoneta_append_node(&i_nodes, i_ident);

      if (pgmoneta_create_node_string(output, "output", &i_output))
      {
         goto error;
      }

      pgmoneta_append_node(&i_nodes, i_output);

      workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_ARCHIVE);

      current = workflow;
      while (current != NULL)
      {
         if (current->setup(server, backup_id, i_nodes, &o_nodes))
         {
            goto error;
         }
         current = current->next;
      }

      current = workflow;
      while (current != NULL)
      {
         if (current->execute(server, backup_id, i_nodes, &o_nodes))
         {
            goto error;
         }
         current = current->next;
      }

      current = workflow;
      while (current != NULL)
      {
         if (current->teardown(server, backup_id, i_nodes, &o_nodes))
         {
            goto error;
         }
         current = current->next;
      }

      total_seconds = (int)difftime(time(NULL), start_time);
      hours = total_seconds / 3600;
      minutes = (total_seconds % 3600) / 60;
      seconds = total_seconds % 60;

      memset(&elapsed[0], 0, sizeof(elapsed));
      sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, seconds);

      pgmoneta_log_info("Archive: %s/%s (Elapsed: %s)", config->servers[server].name, id, &elapsed[0]);
   }

   pgmoneta_management_write_int32(client_fd, result);
   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   pgmoneta_workflow_delete(workflow);

   pgmoneta_free_nodes(i_nodes);

   pgmoneta_free_nodes(o_nodes);

   free(id);
   free(output);
   free(to);

   free(backup_id);
   free(position);
   free(directory);

   exit(0);

error:
   pgmoneta_workflow_delete(workflow);

   pgmoneta_free_nodes(i_nodes);

   pgmoneta_free_nodes(o_nodes);

   free(id);
   free(output);
   free(to);

   free(backup_id);
   free(position);
   free(directory);

   exit(1);
}

int
pgmoneta_extract_tar_file(char* file_path, char* destination)
{
   struct archive* a;
   struct archive_entry* entry;
   a = archive_read_new();
   archive_read_support_format_tar(a);
   // open tar file in a suitable buffer size, I'm using 10240 here
   if (archive_read_open_filename(a, file_path, 10240) != ARCHIVE_OK)
   {
      pgmoneta_log_error("Failed to open the tar file for reading");
      goto error;
   }

   while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
   {
      char dst_file_path[MAX_PATH];
      memset(dst_file_path, 0, sizeof(dst_file_path));
      const char* entry_path = archive_entry_pathname(entry);
      if (pgmoneta_ends_with(destination, "/"))
      {
         snprintf(dst_file_path, sizeof(dst_file_path), "%s%s", destination, entry_path);
      }
      else
      {
         snprintf(dst_file_path, sizeof(dst_file_path), "%s/%s", destination, entry_path);
      }

      archive_entry_set_pathname(entry, dst_file_path);
      if (archive_read_extract(a, entry, 0) != ARCHIVE_OK)
      {
         pgmoneta_log_error("Failed to extract entry: %s", archive_error_string(a));
         goto error;
      }
   }

   archive_read_close(a);
   archive_read_free(a);
   return 0;

error:
   archive_read_close(a);
   archive_read_free(a);
   return 1;
}
