/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright 2016 Kinvolk GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Krzesimir Nowak <krzesimir@kinvolk.io>
 */

#include "misc-utils.h"
#include "ostree-spawn.h"

#include <errno.h>

#ifndef OSTREE_BINARY
#error OSTREE_BINARY is not defined
#endif

static void
copy_strv_to_ptr_array (gchar **strv,
			GPtrArray *array)
{
  gchar **iter;

  if (strv != NULL)
    for (iter = strv; *iter != NULL; ++iter)
      g_ptr_array_add (array, g_strdup (*iter));
  g_ptr_array_add (array, NULL);
}

static gboolean
spawn_ostree_in_repo (GFile *repo,
		      gchar **args,
		      CmdStuff *cmd,
		      GError **error)
{
  SSDEF;
  g_autoptr(GPtrArray) argv = string_array_new ();
  g_auto(GStrv) raw_argv = NULL;

  g_ptr_array_add (argv, g_strdup (OSTREE_BINARY));
  g_ptr_array_add (argv, flag ("repo", SS (g_file_get_path (repo))));
  copy_strv_to_ptr_array (args, argv);
  raw_argv = (gchar**)g_ptr_array_free (g_steal_pointer (&argv), FALSE);

  return test_spawn (raw_argv, NULL, cmd, error);
}

static const gchar *
repo_mode_to_string (RepoMode mode)
{
  switch (mode)
    {
    case REPO_ARCHIVE_Z2:
      return "archive-z2";

    case REPO_BARE:
      return "bare";
    }

  g_assert_not_reached ();
}

gboolean
ostree_init (GFile *repo,
             RepoMode mode,
             CmdStuff *cmd,
             GError **error)
{
  SSDEF;
  g_auto(GStrv) args = NULL;

  args = generate_strv ("init",
                        SS (flag ("mode", repo_mode_to_string (mode))),
                        NULL);

  return spawn_ostree_in_repo (repo,
			       args,
			       cmd,
			       error);
}

gboolean
ostree_commit (GFile *repo,
               GFile *tree_root,
               const gchar *subject,
               const gchar *ref,
               const gchar *keyid,
               GDateTime *timestamp,
               CmdStuff *cmd,
               GError **error)
{
  SSDEF;
  g_auto(GStrv) args = NULL;

  args = generate_strv ("commit",
                        SS (flag ("subject", subject)),
                        SS (flag ("branch", ref)),
                        SS (flag ("gpg-sign", keyid)),
                        "--gpg-homedir=" GPG_HOME_DIRECTORY,
                        SS (flag ("timestamp", SS (g_date_time_format (timestamp, "%F")))),
                        SS (g_file_get_path (tree_root)),
                        NULL);

  return spawn_ostree_in_repo (repo,
			       args,
			       cmd,
			       error);
}

gboolean
ostree_summary (GFile *repo,
                const gchar *keyid,
                CmdStuff *cmd,
                GError **error)
{
  SSDEF;
  g_auto(GStrv) args = NULL;

  args = generate_strv ("summary",
                        "--update",
                        SS (flag ("gpg-sign", keyid)),
                        "--gpg-homedir=" GPG_HOME_DIRECTORY,
                        NULL);

  return spawn_ostree_in_repo (repo,
			       args,
			       cmd,
			       error);
}


gboolean
ostree_pull (GFile *repo,
             const gchar *remote_name,
             const gchar *ref,
             CmdStuff *cmd,
             GError **error)
{
  SSDEF;
  g_auto(GStrv) args = NULL;

  args = generate_strv ("pull",
                        remote_name,
                        ref,
                        NULL);

  return spawn_ostree_in_repo (repo,
			       args,
			       cmd,
			       error);
}

gboolean
ostree_remote_add (GFile *repo,
                   const gchar *remote_name,
                   const gchar *remote_url,
                   const gchar *ref,
                   GFile *gpg_key,
                   CmdStuff *cmd,
                   GError **error)
{
  SSDEF;
  g_auto(GStrv) args = NULL;

  args = generate_strv ("remote",
                        "add",
                        SS (flag ("gpg-import",
                                  SS (g_file_get_path (gpg_key)))),
                        remote_name,
                        remote_url,
                        ref,
                        NULL);

  return spawn_ostree_in_repo (repo,
			       args,
			       cmd,
			       error);
}

static gboolean
ostree_admin_spawn_in_sysroot (GFile *sysroot,
			       const gchar *admin_subcommand,
			       gchar **args,
			       CmdStuff *cmd,
			       GError **error)
{
  SSDEF;
  g_autoptr(GPtrArray) argv = string_array_new ();
  g_auto(GStrv) raw_argv = NULL;

  g_ptr_array_add (argv, g_strdup (OSTREE_BINARY));
  g_ptr_array_add (argv, g_strdup ("admin"));
  g_ptr_array_add (argv, g_strdup (admin_subcommand));
  g_ptr_array_add (argv, flag ("sysroot", SS (g_file_get_path (sysroot))));
  copy_strv_to_ptr_array (args, argv);
  raw_argv = (gchar**)g_ptr_array_free (g_steal_pointer (&argv), FALSE);

  return test_spawn (raw_argv, NULL, cmd, error);
}

gboolean
ostree_deploy (GFile *sysroot,
               const gchar *osname,
               const gchar *refspec,
               CmdStuff *cmd,
               GError **error)
{
  SSDEF;
  g_auto(GStrv) args = NULL;

  args = generate_strv (SS (flag ("os", osname)),
                        "--retain",
                        refspec,
                        NULL);

  return ostree_admin_spawn_in_sysroot (sysroot,
					"deploy",
					args,
					cmd,
					error);
}

gboolean
ostree_init_fs (GFile *sysroot,
                CmdStuff *cmd,
                GError **error)
{
  SSDEF;
  g_auto(GStrv) args = NULL;

  args = generate_strv (SS (g_file_get_path (sysroot)),
                        NULL);

  return ostree_admin_spawn_in_sysroot (sysroot,
					"init-fs",
					args,
					cmd,
					error);
}

gboolean
ostree_os_init (GFile *sysroot,
                const gchar *remote_name,
                CmdStuff *cmd,
                GError **error)
{
  g_auto(GStrv) args = NULL;

  args = generate_strv (remote_name,
                        NULL);

  return ostree_admin_spawn_in_sysroot (sysroot,
					"os-init",
					args,
					cmd,
					error);
}

gboolean
ostree_status (GFile *sysroot,
               CmdStuff *cmd,
               GError **error)
{
  return ostree_admin_spawn_in_sysroot (sysroot,
					"status",
					NULL,
					cmd,
					error);
}

gboolean
ostree_httpd (GFile *served_dir,
              guint16 *port,
              CmdStuff *cmd,
              GError **error)
{
  SSDEF;
  g_auto(GStrv) argv = NULL;
  g_autoptr(GFileIOStream) stream = NULL;
  g_autoptr(GFile) tmp_port = g_file_new_tmp ("eos-updater-test-httpd-port-XXXXXX", &stream, error);
  g_autoptr(GBytes) bytes = NULL;
  g_autofree gchar *port_contents = NULL;
  gchar *endptr;
  guint64 number;
  int saved_errno;

  if (tmp_port == NULL)
    return FALSE;

  argv = generate_strv (OSTREE_BINARY,
                        "trivial-httpd",
                        "--autoexit",
                        "--daemonize",
                        SS (flag ("port-file", SS (g_file_get_path (tmp_port)))),
                        SS (g_file_get_path (served_dir)),
                        NULL);

  if (!test_spawn_cwd_full (NULL,
                            argv,
                            NULL,
                            TRUE,
                            cmd,
                            error))
    return FALSE;

  if (!load_to_bytes (tmp_port,
                      &bytes,
                      error))
    return FALSE;

  if (!rm_rf (tmp_port, error))
    return FALSE;

  port_contents = g_strndup (g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes));
  g_strstrip (port_contents);
  errno = 0;
  number = g_ascii_strtoull (port_contents, &endptr, 10);
  saved_errno = errno;
  if (saved_errno != 0 || number == 0 || *endptr != '\0' || number > G_MAXUINT16)
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                   "Invalid port number %s", port_contents);
      return FALSE;
    }
  *port = number;
  return TRUE;
}
