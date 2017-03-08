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

#include "eos-test-utils.h"
#include "misc-utils.h"
#include "ostree-spawn.h"

#include <errno.h>
#include <ostree.h>
#include <string.h>

#ifndef GPG_BINARY
#error "GPG_BINARY is not defined"
#endif

const gchar *const default_vendor = "VENDOR";
const gchar *const default_product = "PRODUCT";
const gchar *const default_ref = "REF";
const gchar *const default_ostree_path = "OSTREE/PATH";
const gchar *const default_remote_name = "REMOTE";

void
eos_updater_fixture_setup (EosUpdaterFixture *fixture,
                           gconstpointer user_data)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *tmpdir_path = NULL;
  gsize i;
  const gchar * const gpg_home_files[] =
    {
      "C1EB8F4E.asc",
      "keyid",
      "pubring.gpg",
      "random_seed",
      "secring.gpg",
    };

  fixture->dbus = g_test_dbus_new (G_TEST_DBUS_NONE);
  g_test_dbus_up (fixture->dbus);

  tmpdir_path = g_dir_make_tmp ("eos-updater-test-XXXXXX", &error);
  g_assert_no_error (error);
  fixture->tmpdir = g_file_new_for_path (tmpdir_path);

  g_test_message ("Using fixture directory ‘%s’", tmpdir_path);

  /* Copy the GPG files from the source directory into the fixture directory,
   * as running GPG with them as its homedir might alter them; we don’t want
   * that to happen in the source directory, which might be read-only (and in
   * any case, we want determinism). */
  fixture->gpg_home = g_file_get_child (fixture->tmpdir, "gpghome");
  g_file_make_directory (fixture->gpg_home, NULL, &error);
  g_assert_no_error (error);

  g_file_set_attribute_uint32 (fixture->gpg_home, G_FILE_ATTRIBUTE_UNIX_MODE,
                               0700, G_FILE_QUERY_INFO_NONE, NULL, &error);
  g_assert_no_error (error);

  for (i = 0; i < G_N_ELEMENTS (gpg_home_files); i++)
    {
      g_autofree gchar *source_path = NULL;
      g_autoptr (GFile) source = NULL, destination = NULL;

      source_path = g_test_build_filename (G_TEST_DIST, "gpghome",
                                           gpg_home_files[i], NULL);
      source = g_file_new_for_path (source_path);
      destination = g_file_get_child (fixture->gpg_home, gpg_home_files[i]);

      g_file_copy (source, destination,
                   G_FILE_COPY_NONE, NULL, NULL, NULL,
                   &error);
      g_assert_no_error (error);

      g_file_set_attribute_uint32 (destination, G_FILE_ATTRIBUTE_UNIX_MODE,
                                   0600, G_FILE_QUERY_INFO_NONE, NULL, &error);
      g_assert_no_error (error);
    }
}

void
eos_updater_fixture_teardown (EosUpdaterFixture *fixture,
                              gconstpointer user_data)
{
  g_autoptr (GError) error = NULL;

  rm_rf (fixture->gpg_home, &error);
  g_assert_no_error (error);
  g_object_unref (fixture->gpg_home);

  rm_rf (fixture->tmpdir, &error);
  g_assert_no_error (error);
  g_object_unref (fixture->tmpdir);

  g_test_dbus_down (fixture->dbus);
  g_object_unref (fixture->dbus);
}

gchar *
get_keyid (GFile *gpg_home)
{
  g_autoptr(GFile) keyid = g_file_get_child (gpg_home, "keyid");
  g_autoptr(GBytes) bytes = NULL;
  g_autoptr(GError) error = NULL;
  gsize len;

  load_to_bytes (keyid, &bytes, &error);
  g_assert_no_error (error);
  len = g_bytes_get_size (bytes);
  g_assert (len == 8);

  return g_strndup (g_bytes_get_data (bytes, NULL), len);
}

static void
eos_test_device_finalize_impl (EosTestDevice *device)
{
  g_free (device->vendor);
  g_free (device->product);
  g_free (device->ref);
}

EOS_DEFINE_REFCOUNTED (EOS_TEST_DEVICE,
                       EosTestDevice,
                       eos_test_device,
                       NULL,
                       eos_test_device_finalize_impl)

EosTestDevice *
eos_test_device_new (const gchar *vendor,
                     const gchar *product,
                     const gchar *ref)
{
  EosTestDevice *device = g_object_new (EOS_TEST_TYPE_DEVICE, NULL);

  device->vendor = g_strdup (vendor);
  device->product = g_strdup (product);
  device->ref = g_strdup (ref);

  return device;
}

static void
eos_test_subserver_dispose_impl (EosTestSubserver *subserver)
{
  g_clear_pointer (&subserver->devices, g_ptr_array_unref);
  g_clear_pointer (&subserver->ref_to_commit, g_hash_table_unref);
  g_clear_object (&subserver->repo);
  g_clear_object (&subserver->tree);
  g_clear_object (&subserver->gpg_home);
}

static void
eos_test_subserver_finalize_impl (EosTestSubserver *subserver)
{
  g_free (subserver->keyid);
  g_free (subserver->ostree_path);
  g_free (subserver->url);
}

EOS_DEFINE_REFCOUNTED (EOS_TEST_SUBSERVER,
                       EosTestSubserver,
                       eos_test_subserver,
                       eos_test_subserver_dispose_impl,
                       eos_test_subserver_finalize_impl)

EosTestSubserver *
eos_test_subserver_new (GFile *gpg_home,
                        const gchar *keyid,
                        const gchar *ostree_path,
                        GPtrArray *devices,
                        GHashTable *ref_to_commit)
{
  EosTestSubserver *subserver = g_object_new (EOS_TEST_TYPE_SUBSERVER, NULL);

  subserver->gpg_home = g_object_ref (gpg_home);
  subserver->keyid = g_strdup (keyid);
  subserver->ostree_path = g_strdup (ostree_path);
  subserver->devices = g_ptr_array_ref (devices);
  subserver->ref_to_commit = g_hash_table_ref (ref_to_commit);

  return subserver;
}

static gchar *
get_commit_filename (guint commit_no)
{
  return g_strdup_printf ("commit%u", commit_no);
}

static gchar *
get_sha256sum_from_strv (const gchar * const *strv)
{
  g_autoptr(GChecksum) sum = g_checksum_new (G_CHECKSUM_SHA256);
  const gchar * const *iter;

  for (iter = strv; *iter != NULL; ++iter)
    g_checksum_update (sum, (const guchar *)*iter, strlen (*iter));

  return g_strdup (g_checksum_get_string (sum));
}

static gchar *
get_boot_checksum (const gchar *kernel_contents,
                   const gchar *initramfs_contents)
{
  const gchar *contents[] =
    {
      kernel_contents,
      initramfs_contents,
      NULL
    };

  return get_sha256sum_from_strv (contents);
}

static const gchar *const os_release =
  "NAME=\"Endless\"\n"
  "VERSION=\"2.6.1\"\n"
  "ID=\"endless\"\n"
  "VERSION_ID=\"2.6.1\"\n"
  "PRETTY_NAME=\"Endless 2.6.1\"\n";

typedef struct
{
  gchar *rel_path;
  gchar *contents;
} SimpleFile;

static SimpleFile *
simple_file_new_steal (gchar *rel_path,
                       gchar *contents)
{
  SimpleFile *file = g_new (SimpleFile, 1);

  file->rel_path = rel_path;
  file->contents = contents;

  return file;
}

static void
simple_file_free (gpointer file_ptr)
{
  SimpleFile *file = file_ptr;

  g_free (file->rel_path);
  g_free (file->contents);
  g_free (file);
}

static GPtrArray *
get_sysroot_files (const gchar *kernel_version)
{
  g_autoptr (GPtrArray) files = g_ptr_array_new_with_free_func (simple_file_free);
  const gchar *kernel_contents = "a kernel";
  const gchar *initramfs_contents = "an initramfs";
  g_autofree gchar *boot_checksum = get_boot_checksum (kernel_contents,
                                                       initramfs_contents);
  g_autofree gchar *kernel_name = g_strdup_printf ("vmlinuz-%s-%s",
                                                   kernel_version,
                                                   boot_checksum);
  g_autofree gchar *initramfs_name = g_strdup_printf ("initramfs-%s-%s",
                                                      kernel_version,
                                                      boot_checksum);

  g_ptr_array_add (files,
                   simple_file_new_steal (g_build_filename ("boot", kernel_name, NULL),
                                          g_strdup (kernel_contents)));
  g_ptr_array_add (files,
                   simple_file_new_steal (g_build_filename ("boot", initramfs_name, NULL),
                                          g_strdup (initramfs_contents)));
  g_ptr_array_add (files,
                   simple_file_new_steal (g_build_filename ("usr", "etc", "os-release", NULL),
                                          g_strdup (os_release)));

  return g_steal_pointer (&files);
}

static gchar **
get_sysroot_dirs (const gchar *kernel_version)
{
  g_autoptr(GPtrArray) dirs = string_array_new ();
  gchar *paths[] =
    {
      g_strdup ("boot"),
      g_build_filename ("usr", "bin", NULL),
      g_build_filename ("usr", "lib", "modules", kernel_version, NULL),
      g_build_filename ("usr", "share", NULL),
      g_build_filename ("usr", "etc", NULL),
      NULL
    };
  guint idx;

  for (idx = 0; idx < G_N_ELEMENTS (paths); ++idx)
    g_ptr_array_add (dirs, paths[idx]);

  return (gchar**)g_ptr_array_free (g_steal_pointer (&dirs), FALSE);
}

static gboolean
prepare_sysroot_contents (GFile *repo,
                          GFile *tree_root,
                          GError **error)
{
  const gchar *kernel_version = "4.6";
  g_autoptr(GPtrArray) files = get_sysroot_files (kernel_version);
  guint idx;
  g_auto(GStrv) dirs = get_sysroot_dirs (kernel_version);
  gchar **iter;

  for (iter = dirs; *iter != NULL; ++iter)
    {
      g_autoptr(GFile) path = g_file_get_child (tree_root, *iter);

      if (!create_directory (path, error))
        return FALSE;
    }

  for (idx = 0; idx < files->len; ++idx)
    {
      SimpleFile *file = g_ptr_array_index (files, idx);
      const gchar *contents = file->contents;
      gsize len = (contents != NULL) ? strlen (contents) : 0;
      g_autoptr(GBytes) bytes = g_bytes_new_static (contents, len);
      g_autoptr(GFile) path = g_file_get_child (tree_root, file->rel_path);

      if (!create_file (path, bytes, error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
prepare_commit (GFile *repo,
                GFile *tree_root,
                guint commit_no,
                const gchar *ref,
                GFile *gpg_home,
                const gchar *keyid,
                gchar **checksum,
                GError **error)
{
  g_autofree gchar *commit_filename = NULL;
  g_autoptr(GFile) commit_file = NULL;
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;
  g_autoptr(GDateTime) timestamp = NULL;
  const guint commit_max = 10;
  g_autofree gchar *subject = NULL;

  if (commit_no > commit_max)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "exceeded commit limit %u with %u", commit_max, commit_no);
      return FALSE;
    }
  commit_filename = get_commit_filename (commit_no);
  commit_file = g_file_get_child (tree_root, commit_filename);

  if (g_file_query_exists (commit_file, NULL))
    return TRUE;

  if (commit_no > 0)
    {
      if (!prepare_commit (repo,
                           tree_root,
                           commit_no - 1,
                           ref,
                           gpg_home,
                           keyid,
                           NULL,
                           error))
        return FALSE;
    }
  else
    if (!prepare_sysroot_contents (repo,
                                   tree_root,
                                   error))
      return FALSE;

  if (!create_file (commit_file, NULL, error))
    return FALSE;

  subject = g_strdup_printf ("Test commit %u", commit_no);
  timestamp = days_ago (commit_max - commit_no);
  if (!ostree_commit (repo,
                      tree_root,
                      subject,
                      ref,
                      gpg_home,
                      keyid,
                      timestamp,
                      &cmd,
                      error))
    return FALSE;

  if (!cmd_result_ensure_ok (&cmd, error))
    return FALSE;

  if (checksum != NULL)
    {
      g_autofree gchar *head_rel_path = g_build_filename ("refs", "heads", ref, NULL);
      g_autoptr(GFile) head = g_file_get_child (repo, head_rel_path);
      g_autoptr(GBytes) bytes = NULL;

      if (!load_to_bytes (head,
                          &bytes,
                          error))
        return FALSE;

      *checksum = g_strndup (g_bytes_get_data (bytes, NULL),
                             g_bytes_get_size (bytes));
    }

  return TRUE;
}

static GFile *
get_eos_extensions_dir (GFile *repo)
{
  g_autofree gchar *rel_path = g_build_filename ("extensions", "eos", NULL);

  return g_file_get_child (repo, rel_path);
}

static void
get_ref_file_paths (GFile *repo,
                    const gchar *ref,
                    GFile **out_file,
                    GFile **out_signature)
{
  g_autoptr(GFile) eos_dir = get_eos_extensions_dir (repo);
  g_autofree gchar *rel_path = g_build_filename ("refs.d", ref, NULL);
  g_autofree gchar *sig_rel_path = g_strdup_printf ("%s.sig", rel_path);

  *out_file = g_file_get_child (eos_dir, rel_path);
  *out_signature = g_file_get_child (eos_dir, sig_rel_path);
}

static gboolean
gpg_sign (GFile *gpg_home,
          GFile *file,
          GFile *signature,
          const gchar *keyid,
          CmdResult *cmd,
          GError **error)
{
  g_autofree gchar *gpg_home_path = g_file_get_path (gpg_home);
  g_autofree gchar *raw_signature_path = g_file_get_path (signature);
  g_autofree gchar *raw_file_path = g_file_get_path (file);
  CmdArg args[] =
    {
      { NULL, GPG_BINARY },
      { "homedir", gpg_home_path },
      { "default-key", keyid },
      { "output", raw_signature_path },
      { "detach-sig", NULL },
      { NULL, raw_file_path },
      { NULL, NULL }
    };
  g_auto(GStrv) argv = build_cmd_args (args);

  if (!rm_rf (signature, error))
    return FALSE;

  return test_spawn ((const gchar * const *) argv, NULL, cmd, error);
}

static gboolean
generate_ref_file (GFile *repo,
                   const gchar *ref,
                   const gchar *commit,
                   GFile *gpg_home,
                   const gchar *keyid,
                   GError **error)
{
  g_autoptr(GFile) ref_file = NULL;
  g_autoptr(GFile) ref_file_sig = NULL;
  g_autoptr(GFile) ref_file_parent = NULL;
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;
  g_autoptr(GKeyFile) keyfile = NULL;

  get_ref_file_paths (repo,
                      ref,
                      &ref_file,
                      &ref_file_sig);
  ref_file_parent = g_file_get_parent (ref_file);

  if (!create_directory (ref_file_parent,
                         error))
    return FALSE;

  keyfile = g_key_file_new ();
  g_key_file_set_string (keyfile, "mapping", "ref", ref);
  g_key_file_set_string (keyfile, "mapping", "commit", commit);
  if (!save_key_file (ref_file, keyfile, error))
    return FALSE;

  if (!gpg_sign (gpg_home,
                 ref_file,
                 ref_file_sig,
                 keyid,
                 &cmd,
                 error))
    return FALSE;

  return cmd_result_ensure_ok (&cmd, error);
}

static gboolean
update_commits (EosTestSubserver *subserver,
                GError **error)
{
  GHashTableIter iter;
  gpointer ref_ptr;
  gpointer commit_ptr;
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;

  g_hash_table_iter_init (&iter, subserver->ref_to_commit);
  while (g_hash_table_iter_next (&iter, &ref_ptr, &commit_ptr))
    {
      guint commit = GPOINTER_TO_UINT (commit_ptr);
      g_autofree gchar *checksum = NULL;

      if (!prepare_commit (subserver->repo,
                           subserver->tree,
                           commit,
                           ref_ptr,
                           subserver->gpg_home,
                           subserver->keyid,
                           &checksum,
                           error))
        return FALSE;

      if (!generate_ref_file (subserver->repo,
                              ref_ptr,
                              checksum,
                              subserver->gpg_home,
                              subserver->keyid,
                              error))
        return FALSE;
    }

  if (!ostree_summary (subserver->repo,
                       subserver->gpg_home,
                       subserver->keyid,
                       &cmd,
                       error))
    return FALSE;

  return cmd_result_ensure_ok (&cmd, error);
}

static gboolean
repo_config_exists (GFile *repo)
{
  g_autoptr(GFile) config = g_file_get_child (repo, "config");

  return g_file_query_exists (config, NULL);
}

gboolean
eos_test_subserver_update (EosTestSubserver *subserver,
                           GError **error)
{
  GFile *repo = subserver->repo;
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;

  if (!create_directory (repo, error))
    return FALSE;

  if (!repo_config_exists (repo))
    {
      if (!ostree_init (repo,
                        REPO_ARCHIVE_Z2,
                        &cmd,
                        error))
        return FALSE;
      if (!cmd_result_ensure_ok (&cmd, error))
        return FALSE;
    }

  return update_commits (subserver, error);
}

static void
eos_test_server_dispose_impl (EosTestServer *server)
{
  g_clear_object (&server->root);
  g_clear_pointer (&server->subservers, g_ptr_array_unref);
}

static void
eos_test_server_finalize_impl (EosTestServer *server)
{
  g_free (server->url);
}

/**
 * EosTestServer:
 *
 * #EosTestServer is a mock server implementation which uses one or more
 * *subservers* to serve ostree branches over HTTP. The content is served from
 * the `main` directory of a given httpd root, or from ostree paths below the
 * root.
 */
EOS_DEFINE_REFCOUNTED (EOS_TEST_SERVER,
                       EosTestServer,
                       eos_test_server,
                       eos_test_server_dispose_impl,
                       eos_test_server_finalize_impl)

static gboolean
run_httpd (GFile *served_root,
           GFile *httpd_dir,
           gchar **out_url,
           GError **error)
{
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;
  g_autoptr(GFile) port_file = g_file_get_child (httpd_dir, "port-file");
  guint16 port;

  if (!ostree_httpd (served_root,
                     port_file,
                     &cmd,
                     error))
    return FALSE;
  if (!cmd_result_ensure_ok (&cmd, error))
    return FALSE;

  if (!read_port_file (port_file,
                       &port,
                       error))
    return FALSE;

  *out_url = g_strdup_printf ("http://127.0.0.1:%" G_GUINT16_FORMAT, port);
  return TRUE;
}

static GFile *
get_main_tree_root (GFile *main_root)
{
  return g_file_get_child (main_root, "trees");
}

static GFile *
get_main_served_root (GFile *main_root)
{
  return g_file_get_child (main_root, "served");
}

static GFile *
get_main_httpd_dir (GFile *main_root)
{
  return g_file_get_child (main_root, "httpd");
}

static gboolean
setup_subservers (GPtrArray *subservers,
                  GFile *main_root,
                  GError **error)
{
  g_autoptr(GFile) tree_root = NULL;
  g_autoptr(GFile) served_root = NULL;
  guint idx;

  tree_root = get_main_tree_root (main_root);
  served_root = get_main_served_root (main_root);
  for (idx = 0; idx < subservers->len; ++idx)
    {
      EosTestSubserver *subserver = EOS_TEST_SUBSERVER (g_ptr_array_index (subservers, idx));
      g_autoptr(GFile) subtree = g_file_get_child (tree_root, subserver->ostree_path);
      g_autoptr(GFile) subserved = g_file_get_child (served_root, subserver->ostree_path);

      g_set_object (&subserver->repo, subserved);
      g_set_object (&subserver->tree, subtree);
      if (!eos_test_subserver_update (subserver, error))
        return FALSE;
    }

  return TRUE;
}

static void
update_subserver_urls (GPtrArray *subservers,
                       const gchar *server_url)
{
  guint idx;

  for (idx = 0; idx < subservers->len; ++idx)
    {
      EosTestSubserver *subserver = EOS_TEST_SUBSERVER (g_ptr_array_index (subservers, idx));

      g_free (subserver->url);
      subserver->url = g_strdup_printf ("%s/%s",
                                        server_url,
                                        subserver->ostree_path);
    }
}

EosTestServer *
eos_test_server_new (GFile *server_root,
                     GPtrArray *subservers,
                     GError **error)
{
  g_autoptr(EosTestServer) server = NULL;
  g_autofree gchar *server_url = NULL;
  g_autoptr(GFile) served_root = NULL;
  g_autoptr(GFile) httpd_dir = NULL;

  if (!setup_subservers (subservers,
                         server_root,
                         error))
    return FALSE;

  httpd_dir = get_main_httpd_dir (server_root);
  if (!create_directory (httpd_dir, error))
    return FALSE;

  served_root = get_main_served_root (server_root);
  if (!run_httpd (served_root,
                  httpd_dir,
                  &server_url,
                  error))
    return FALSE;

  update_subserver_urls (subservers, server_url);

  server = g_object_new (EOS_TEST_TYPE_SERVER, NULL);
  server->root = g_object_ref (server_root);
  server->url = g_steal_pointer (&server_url);
  server->subservers = g_ptr_array_ref (subservers);

  return g_steal_pointer (&server);
}

EosTestServer *
eos_test_server_new_quick (GFile *server_root,
                           const gchar *vendor,
                           const gchar *product,
                           const gchar *ref,
                           guint commit,
                           GFile *gpg_home,
                           const gchar *keyid,
                           const gchar *ostree_path,
                           GError **error)
{
  g_autoptr(GPtrArray) subservers = object_array_new ();
  g_autoptr(GPtrArray) devices = object_array_new ();
  g_autoptr(GHashTable) ref_to_commit = eos_test_subserver_ref_to_commit_new ();

  g_ptr_array_add (devices, eos_test_device_new (vendor, product, ref));
  g_hash_table_insert (ref_to_commit, g_strdup (ref), GUINT_TO_POINTER (commit));
  g_ptr_array_add (subservers, eos_test_subserver_new (gpg_home,
                                                       keyid,
                                                       ostree_path,
                                                       devices,
                                                       ref_to_commit));

  return eos_test_server_new (server_root,
                              subservers,
                              error);
}

static void
eos_test_client_dispose_impl (EosTestClient *client)
{
  g_clear_object (&client->root);
}

static void
eos_test_client_finalize_impl (EosTestClient *client)
{
  g_free (client->vendor);
  g_free (client->product);
  g_free (client->remote_name);
  g_free (client->ostree_path);
}

/**
 * EosTestClient:
 *
 * #EosTestClient is a mock client implementation. It points to a specific
 * subserver of a given ostree remote, and is set up with an initial
 * ref from that subserver.
 *
 * The client sets up a sysroot which is an ostree pull and deploy of the
 * content from the given ref on the subserver.
 */
EOS_DEFINE_REFCOUNTED (EOS_TEST_CLIENT,
                       EosTestClient,
                       eos_test_client,
                       eos_test_client_dispose_impl,
                       eos_test_client_finalize_impl)

static GFile *
get_gpg_key_file_for_keyid (GFile *gpg_home,
                            const gchar *keyid)
{
  g_autofree gchar *filename = g_strdup_printf ("%s.asc", keyid);

  return g_file_get_child (gpg_home, filename);
}

static GFile *get_sysroot_for_client (GFile *client_root)
{
  return g_file_get_child (client_root, "sysroot");
}

static GFile *get_repo_for_sysroot (GFile *sysroot)
{
  g_autofree gchar *rel_path = g_build_filename ("ostree", "repo", NULL);

  return g_file_get_child (sysroot, rel_path);
}

static gboolean
setup_stub_uboot_config (GFile *sysroot,
                         GError **error)
{
  g_autoptr (GFile) boot = g_file_get_child (sysroot, "boot");
  g_autoptr (GFile) loader0 = g_file_get_child (boot, "loader.0");
  g_autoptr (GFile) loader = g_file_get_child (boot, "loader");
  g_autoptr (GFile) uenv = g_file_get_child (loader, "uEnv.txt");
  g_autoptr (GFile) uenv_compat = g_file_get_child (boot, "uEnv.txt");
  g_autofree gchar *symlink_target = g_build_filename ("loader", "uEnv.txt", NULL);

  if (!create_directory (loader0, error))
    return FALSE;

  if (!create_symlink ("loader.0", loader, error))
    return FALSE;

  if (!create_file (uenv, NULL, error))
    return FALSE;

  if (!create_symlink (symlink_target,
                       uenv_compat,
                       error))
    return FALSE;

  return TRUE;
}

static gboolean
prepare_client_sysroot (GFile *client_root,
                        const gchar *remote_name,
                        const gchar *url,
                        const gchar *ref,
                        GFile *gpg_home,
                        const gchar *keyid,
                        GError **error)
{
  g_autoptr(GFile) sysroot = get_sysroot_for_client (client_root);
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;
  g_autoptr(GFile) gpg_key = NULL;
  g_autoptr(GFile) repo = NULL;
  g_autofree gchar *refspec = NULL;

  if (!create_directory (sysroot,
                         error))
    return FALSE;

  if (!ostree_init_fs (sysroot,
                       &cmd,
                       error))
    return FALSE;
  if (!cmd_result_ensure_ok (&cmd, error))
    return FALSE;

  cmd_result_clear (&cmd);
  if (!ostree_os_init (sysroot,
                       remote_name,
                       &cmd,
                       error))
    return FALSE;
  if (!cmd_result_ensure_ok (&cmd, error))
    return FALSE;

  if (!setup_stub_uboot_config (sysroot, error))
    return FALSE;

  gpg_key = get_gpg_key_file_for_keyid (gpg_home, keyid);
  repo = get_repo_for_sysroot (sysroot);
  cmd_result_clear (&cmd);
  if (!ostree_remote_add (repo,
                          remote_name,
                          url,
                          ref,
                          gpg_key,
                          &cmd,
                          error))
    return FALSE;
  if (!cmd_result_ensure_ok (&cmd, error))
    return FALSE;

  cmd_result_clear (&cmd);
  if (!ostree_pull (repo,
                    remote_name,
                    ref,
                    &cmd,
                    error))
    return FALSE;
  if (!cmd_result_ensure_ok (&cmd, error))
    return FALSE;

  refspec = g_strdup_printf ("%s:%s", remote_name, ref);
  cmd_result_clear (&cmd);
  if (!ostree_deploy (sysroot,
                      remote_name,
                      refspec,
                      &cmd,
                      error))
    return FALSE;
  if (!cmd_result_ensure_ok (&cmd, error))
    return FALSE;

  return TRUE;
}

static gboolean
copy_file_and_signature (GFile *source_file,
                         GFile *source_sig,
                         GFile *target_file,
                         GFile *target_sig,
                         GError **error)
{
  g_autoptr(GFile) target_parent = NULL;

  target_parent = g_file_get_parent (target_file);
  if (!create_directory (target_parent, error))
    return FALSE;

  if (!cp (source_file, target_file, error))
    return FALSE;

  if (!cp (source_sig, target_sig, error))
    return FALSE;

  return TRUE;
}

static gboolean
copy_ref_file (GFile *source_repo,
               GFile *target_repo,
               const gchar *ref,
               GError **error)
{
  g_autoptr(GFile) source_ref_file = NULL;
  g_autoptr(GFile) source_ref_file_sig = NULL;
  g_autoptr(GFile) target_ref_file = NULL;
  g_autoptr(GFile) target_ref_file_sig = NULL;

  get_ref_file_paths (source_repo,
                      ref,
                      &source_ref_file,
                      &source_ref_file_sig);
  get_ref_file_paths (target_repo,
                      ref,
                      &target_ref_file,
                      &target_ref_file_sig);

  return copy_file_and_signature (source_ref_file,
                                  source_ref_file_sig,
                                  target_ref_file,
                                  target_ref_file_sig,
                                  error);
}

static gboolean
copy_extensions (GFile *source_repo,
                 GFile *client_root,
                 const gchar *ref,
                 GError **error)
{
  g_autoptr(GFile) sysroot = get_sysroot_for_client (client_root);
  g_autoptr(GFile) repo = get_repo_for_sysroot (sysroot);


  if (!copy_ref_file (source_repo, repo, ref, error))
    return FALSE;

  return TRUE;
}

static const gchar *
download_source_to_string (DownloadSource source)
{
  switch (source)
    {
    case DOWNLOAD_MAIN:
      return "main";

    case DOWNLOAD_LAN:
      return "lan";

    case DOWNLOAD_VOLUME:
      return "volume";

    default:
      g_assert_not_reached ();
    }
}

static GFile *
get_updater_dir_for_client (GFile *client_root)
{
  return g_file_get_child (client_root, "updater");
}

static void
set_source_specific_config (GKeyFile *config,
                            DownloadSource source,
                            GVariant *source_variant)
{
  g_autofree gchar *group_name = g_strdup_printf ("Source \"%s\"",
                                                  download_source_to_string (source));
  switch (source)
    {
    case DOWNLOAD_MAIN:
    case DOWNLOAD_LAN:
      /* no specific config for now */
      return;

    case DOWNLOAD_VOLUME:
      g_key_file_set_string (config,
                             group_name,
                             "Path",
                             g_variant_get_string (source_variant,
                                                   NULL));
      return;

    default:
      g_assert_not_reached ();
    }
}

static GKeyFile *
get_updater_config (DownloadSource *order,
                    GVariant **source_variants,
                    gsize n_sources)
{
  g_autoptr(GKeyFile) config = g_key_file_new ();
  gsize idx;
  g_autoptr(GPtrArray) source_strs = g_ptr_array_sized_new (n_sources);

  for (idx = 0; idx < n_sources; ++idx)
    g_ptr_array_add (source_strs, (gpointer)download_source_to_string (order[idx]));
  g_key_file_set_string_list (config,
                              "Download",
                              "Order",
                              (const gchar * const*)source_strs->pdata,
                              source_strs->len);

  for (idx = 0; idx < n_sources; ++idx)
    set_source_specific_config (config, order[idx], source_variants[idx]);

  return g_steal_pointer (&config);
}

static GKeyFile *
get_hw_config (const gchar *vendor,
               const gchar *product)
{
  g_autoptr(GKeyFile) hw = g_key_file_new ();

  g_key_file_set_string (hw, "descriptors", "sys_vendor", vendor);
  g_key_file_set_string (hw, "descriptors", "product_name", product);

  return g_steal_pointer (&hw);
}

static GFile *
updater_avahi_services_dir (GFile *updater_dir)
{
  return g_file_get_child (updater_dir, "avahi-services");
}

static GFile *
updater_avahi_emulator_definitions_dir (GFile *updater_dir)
{
  return g_file_get_child (updater_dir, "avahi-emulator-definitions");
}

static GFile *
updater_quit_file (GFile *updater_dir)
{
  return g_file_get_child (updater_dir, "quit-file");
}

static GFile *
updater_config_file (GFile *updater_dir)
{
  return g_file_get_child (updater_dir, "config");
}

static GFile *
updater_hw_file (GFile *updater_dir)
{
  return g_file_get_child (updater_dir, "hw");
}

static gboolean
prepare_updater_dir (GFile *updater_dir,
                     GKeyFile *config_file,
                     GKeyFile *hw_file,
                     GError **error)
{
  g_autoptr(GFile) services_dir_path = updater_avahi_services_dir (updater_dir);
  g_autoptr(GFile) definitions_dir_path = NULL;
  g_autoptr(GFile) quit_file_path = NULL;
  g_autoptr(GFile) config_file_path = NULL;
  g_autoptr(GFile) hw_file_path = NULL;

  if (!create_directory (services_dir_path, error))
    return FALSE;

  definitions_dir_path = updater_avahi_emulator_definitions_dir (updater_dir);
  if (!create_directory (definitions_dir_path, error))
    return FALSE;

  quit_file_path = updater_quit_file (updater_dir);
  if (!create_file (quit_file_path, NULL, error))
    return FALSE;

  config_file_path = updater_config_file (updater_dir);
  if (!save_key_file (config_file_path, config_file, error))
    return FALSE;

  hw_file_path = updater_hw_file (updater_dir);
  if (!save_key_file (hw_file_path, hw_file, error))
    return FALSE;

  return TRUE;
}

static gchar *
get_gdb_r_command (const gchar * const *argv)
{
  g_autofree gchar *joined = g_strjoinv (" ", (gchar **) argv + 1);
  g_autofree gchar *r_command = g_strdup_printf ("r %s", joined);

  return g_shell_quote (r_command);
}

static GBytes *
get_bash_script_contents (const gchar * const *argv,
                          const gchar * const *envp)
{
  const gchar *tmpl_prolog =
    "#!/usr/bin/bash\n"
    "\n"
    "set -e\n"
    "GDB_PATH=$(which gdb)\n"
    "if [[ -f ./libtool ]] && [[ -x ./libtool ]]; then :; else\n"
    "    echo 'the script must be executed in the directory where the libtool script is located (usually toplevel build directory)'\n"
    "    exit 1\n"
    "fi\n";
  g_autofree gchar *gdb_r_command = get_gdb_r_command (argv);
  g_autofree gchar *quoted_binary = g_shell_quote (argv[0]);
  g_autoptr(GString) contents = g_string_new (NULL);
  const gchar * const *iter;

  g_string_append (contents, tmpl_prolog);
  for (iter = envp; *iter != NULL; ++iter)
    {
      g_autofree gchar *quoted = g_shell_quote (*iter);

      /* We don’t need to propagate these, and they don’t get quoted
       * properly. */
      if (g_str_has_prefix (*iter, "BASH_FUNC_"))
        continue;

      g_string_append_printf (contents, "export %s\n", quoted);
    }

  g_string_append_printf (contents,
                          "./libtool --mode=execute \"${GDB_PATH}\" -ex \"break main\" -ex %s %s\n",
                          gdb_r_command,
                          quoted_binary);

  return g_string_free_to_bytes (g_steal_pointer (&contents));
}

/* shell out to call chmod a+x <file> because modifying the
 * G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE attribute is not possible
 */
static gboolean
chmod_a_x (GFile *path,
           GError **error)
{
  CmdResult cmd = CMD_RESULT_CLEARED;
  g_autofree gchar *raw_path = g_file_get_path (path);
  const gchar *argv[] =
    {
      "chmod",
      "a+x",
      raw_path,
      NULL
    };

  if (!test_spawn ((const gchar * const *) argv, NULL, &cmd, error))
    return FALSE;

  return cmd_result_ensure_ok (&cmd, error);
}

static gboolean
generate_bash_script (GFile *bash_script,
                      const gchar * const *argv,
                      const gchar * const *envp,
                      GError **error)
{
  g_autoptr(GBytes) bash = NULL;
  g_auto(GStrv) merged = merge_parent_and_child_env (envp);

  bash = get_bash_script_contents (argv, (const gchar * const *) merged);
  if (!create_file (bash_script, bash, error))
    return FALSE;

  if (!chmod_a_x (bash_script, error))
    return FALSE;

  return TRUE;
}

typedef struct
{
  GMainLoop *loop;
  guint id;
} WatchUpdater;

static void
com_endlessm_updater_appeared (GDBusConnection *connection,
                               const gchar *name,
                               const gchar *name_owner,
                               gpointer wu_ptr)
{
  WatchUpdater *wu = wu_ptr;

  g_bus_unwatch_name (wu->id);
  g_main_loop_quit (wu->loop);
}

static gboolean
spawn_updater (GFile *sysroot,
               GFile *repo,
               GFile *config_file,
               GFile *avahi_services_dir,
               GFile *hw_file,
               GFile *avahi_emulator_definitions_dir,
               GFile *quit_file,
               const gchar *osname,
               CmdAsyncResult *cmd,
               GError **error)
{
  g_autofree gchar *eos_updater_binary = g_test_build_filename (G_TEST_BUILT,
                                                                "..",
                                                                "src",
                                                                "eos-updater",
                                                                NULL);
  CmdEnvVar envv[] =
    {
      { "EOS_UPDATER_TEST_UPDATER_CONFIG_FILE_PATH", NULL, config_file },
      { "EOS_UPDATER_TEST_UPDATER_AVAHI_SERVICES_DIR", NULL, avahi_services_dir },
      { "EOS_UPDATER_TEST_UPDATER_CUSTOM_DESCRIPTORS_PATH", NULL, hw_file },
      { "EOS_UPDATER_TEST_UPDATER_AVAHI_EMULATOR_DEFINITIONS_DIR", NULL, avahi_emulator_definitions_dir },
      { "EOS_UPDATER_TEST_UPDATER_DEPLOYMENT_FALLBACK", "yes", NULL },
      { "EOS_UPDATER_TEST_UPDATER_QUIT_FILE", NULL, quit_file },
      { "EOS_UPDATER_TEST_UPDATER_USE_SESSION_BUS", "yes", NULL },
      { "EOS_UPDATER_TEST_UPDATER_USE_AVAHI_EMULATOR", "yes", NULL },
      { "EOS_UPDATER_TEST_UPDATER_OSTREE_OSNAME", osname, NULL },
      { "OSTREE_SYSROOT", NULL, sysroot },
      { "OSTREE_REPO", NULL, repo },
      { "OSTREE_SYSROOT_DEBUG", "mutable-deployments", NULL },
      { "EOS_DISABLE_METRICS", "1", NULL },
      { NULL, NULL, NULL }
    };
  const gchar *argv[] =
    {
      eos_updater_binary,
      NULL
    };
  g_auto(GStrv) envp = build_cmd_env (envv);
  g_autofree gchar *bash_script_path = NULL;
  g_autoptr(GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  WatchUpdater wu = { loop, 0u };
  guint id = g_bus_watch_name (G_BUS_TYPE_SESSION,
                               "com.endlessm.Updater",
                               G_BUS_NAME_WATCHER_FLAGS_NONE,
                               com_endlessm_updater_appeared,
                               NULL,
                               &wu,
                               NULL);

  wu.id = id;
  bash_script_path = g_strdup (g_getenv ("EOS_CHECK_UPDATER_GDB_BASH_PATH"));
  if (bash_script_path != NULL)
    {
      g_autoptr(GFile) path = g_file_new_for_path (bash_script_path);

      if (!generate_bash_script (path, argv, (const gchar * const *) envp, error))
        return FALSE;

      g_printerr ("Bash script %s generated. Run it, make check will continue when com.endlessm.Updater appears on the test session bus\n",
                  bash_script_path);

    }
  else if (!test_spawn_async ((const gchar * const *) argv,
                              (const gchar * const *) envp, FALSE, cmd, error))
    return FALSE;

  g_main_loop_run (loop);
  return TRUE;
}

static gboolean
spawn_updater_simple (GFile *sysroot,
                      GFile *repo,
                      GFile *updater_dir,
                      const gchar *osname,
                      CmdAsyncResult *cmd,
                      GError **error)
{
  g_autoptr(GFile) config_file_path = updater_config_file (updater_dir);
  g_autoptr(GFile) services_dir_path = updater_avahi_services_dir (updater_dir);
  g_autoptr(GFile) hw_file_path = updater_hw_file (updater_dir);
  g_autoptr(GFile) definitions_dir_path = updater_avahi_emulator_definitions_dir (updater_dir);
  g_autoptr(GFile) quit_file_path = updater_quit_file (updater_dir);

  return spawn_updater (sysroot,
                        repo,
                        config_file_path,
                        services_dir_path,
                        hw_file_path,
                        definitions_dir_path,
                        quit_file_path,
                        osname,
                        cmd,
                        error);
}

static gboolean
run_updater (GFile *client_root,
             DownloadSource *order,
             GVariant **source_variants,
             gsize n_sources,
             const gchar *vendor,
             const gchar *product,
             const gchar *remote_name,
             CmdAsyncResult *updater_cmd,
             GError **error)
{
  g_autoptr(GKeyFile) updater_config = NULL;
  g_autoptr(GKeyFile) hw_config = NULL;
  g_autoptr(GFile) sysroot = get_sysroot_for_client (client_root);
  g_autoptr(GFile) repo = get_repo_for_sysroot (sysroot);
  g_autoptr(GFile) updater_dir = get_updater_dir_for_client (client_root);

  updater_config = get_updater_config (order,
                                       source_variants,
                                       n_sources);
  hw_config = get_hw_config (vendor, product);
  if (!prepare_updater_dir (updater_dir,
                            updater_config,
                            hw_config,
                            error))
    return FALSE;
  if (!spawn_updater_simple (sysroot,
                             repo,
                             updater_dir,
                             remote_name,
                             updater_cmd,
                             error))
    return FALSE;

  return TRUE;
}

static gboolean
ensure_ref_in_subserver (const gchar *ref,
                         EosTestSubserver *subserver)
{
  return g_hash_table_lookup_extended (subserver->ref_to_commit, ref, NULL, NULL);
}

static gboolean
ensure_vendor_and_product_in_subserver (const gchar *vendor,
                                        const gchar *product,
                                        EosTestSubserver *subserver)
{
  guint idx;

  for (idx = 0; idx < subserver->devices->len; ++idx)
    {
      EosTestDevice *device = EOS_TEST_DEVICE (g_ptr_array_index (subserver->devices, idx));

      if (g_strcmp0 (vendor, device->vendor) == 0 &&
          g_strcmp0 (product, device->product) == 0)
        return TRUE;
    }

  return FALSE;
}

EosTestClient *
eos_test_client_new (GFile *client_root,
                     const gchar *remote_name,
                     EosTestSubserver *subserver,
                     const gchar *ref,
                     const gchar *vendor,
                     const gchar *product,
                     GError **error)
{
  g_autoptr(EosTestClient) client = NULL;

  if (!ensure_ref_in_subserver (ref, subserver))
    return FALSE;
  if (!ensure_vendor_and_product_in_subserver (vendor, product, subserver))
    return FALSE;

  if (!prepare_client_sysroot (client_root,
                               remote_name,
                               subserver->url,
                               ref,
                               subserver->gpg_home,
                               subserver->keyid,
                               error))
    return FALSE;

  if (!copy_extensions (subserver->repo,
                        client_root,
                        ref,
                        error))
    return FALSE;

  client = g_object_new (EOS_TEST_TYPE_CLIENT, NULL);
  client->root = g_object_ref (client_root);
  client->vendor = g_strdup (vendor);
  client->product = g_strdup (product);
  client->remote_name = g_strdup (remote_name);
  client->ostree_path = g_strdup (subserver->ostree_path);
  return g_steal_pointer (&client);
}

gboolean
eos_test_client_run_updater (EosTestClient *client,
                             DownloadSource *order,
                             GVariant **source_variants,
                             gsize n_sources,
                             CmdAsyncResult *cmd,
                             GError **error)
{
  if (!run_updater (client->root,
                    order,
                    source_variants,
                    n_sources,
                    client->vendor,
                    client->product,
                    client->remote_name,
                    cmd,
                    error))
    return FALSE;

  return TRUE;
}

static gboolean
simulated_reap_updater (EosTestClient *client,
                        CmdAsyncResult *cmd,
                        CmdResult *reaped,
                        GError **error)
{
  g_autoptr(GFile) updater_dir = get_updater_dir_for_client (client->root);
  g_autoptr(GFile) quit_file = updater_quit_file (updater_dir);

  if (!rm_rf (quit_file, error))
    return FALSE;
  g_free (reaped->cmdline);
  reaped->cmdline = g_strdup (cmd->cmdline);
  return TRUE;
}

static void
com_endlessm_updater_vanished (GDBusConnection *connection,
                               const gchar *name,
                               gpointer wu_ptr)
{
  WatchUpdater *wu = wu_ptr;

  g_bus_unwatch_name (wu->id);
  g_main_loop_quit (wu->loop);
}

static gboolean
real_reap_updater (EosTestClient *client,
                   CmdAsyncResult *cmd,
                   CmdResult *reaped,
                   GError **error)
{
  g_autoptr(GFile) updater_dir = get_updater_dir_for_client (client->root);
  g_autoptr(GFile) quit_file = updater_quit_file (updater_dir);
  g_autoptr(GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  WatchUpdater wu = { loop, 0u };

  wu.id = g_bus_watch_name (G_BUS_TYPE_SESSION,
                            "com.endlessm.Updater",
                            G_BUS_NAME_WATCHER_FLAGS_NONE,
                            NULL,
                            com_endlessm_updater_vanished,
                            &wu,
                            NULL);

  if (!rm_rf (quit_file, error))
    return FALSE;

  g_main_loop_run (loop);

  return reap_async_cmd (cmd, reaped, error);
}

gboolean
eos_test_client_reap_updater (EosTestClient *client,
                              CmdAsyncResult *cmd,
                              CmdResult *reaped,
                              GError **error)
{
  g_autofree gchar *bash_script_path = g_strdup (g_getenv ("EOS_CHECK_UPDATER_GDB_BASH_PATH"));

  if (bash_script_path != NULL)
    return simulated_reap_updater (client,
                                   cmd,
                                   reaped,
                                   error);

  return real_reap_updater (client,
                            cmd,
                            reaped,
                            error);
}

static gchar *
get_bash_script_descriptor_from_port_file (GFile *port_file)
{
  guint idx;
  g_autoptr(GFile) parent = g_object_ref (port_file);

  for (idx = 0; idx < 2; ++idx)
    g_set_object (&parent, g_file_get_parent (parent));

  /* this should return a string like "lan_server_0"
   */
  return g_file_get_basename (parent);
}

static gboolean
run_update_server (GFile *repo,
                   GFile *quit_file,
                   GFile *port_file,
                   GFile *config_file,
                   const gchar *remote_name,
                   guint16 *out_port,
                   CmdAsyncResult *cmd,
                   GError **error)
{
  guint timeout_seconds = 10;
  guint i;
  g_autofree gchar *eos_update_server_binary = g_test_build_filename (G_TEST_BUILT,
                                                                      "..",
                                                                      "src",
                                                                      "eos-update-server",
                                                                      NULL);
  g_autofree gchar *raw_port_file_path = g_file_get_path (port_file);
  g_autofree gchar *raw_config_file_path = g_file_get_path (config_file);
  CmdEnvVar envv[] =
    {
      { "OSTREE_REPO", NULL, repo },
      { "OSTREE_SYSROOT_DEBUG", "mutable-deployments", NULL },
      { "EOS_UPDATER_TEST_UPDATE_SERVER_QUIT_FILE", NULL, quit_file },
      { NULL, NULL, NULL }
    };
  CmdArg args[] =
    {
      { NULL, eos_update_server_binary },
      { "port-file", raw_port_file_path },
      { "timeout", "0" },
      { "serve-remote", remote_name },
      { "config-file", raw_config_file_path },
      { NULL, NULL }
    };
  g_auto(GStrv) envp = build_cmd_env (envv);
  g_auto(GStrv) argv = build_cmd_args (args);
  g_autofree gchar *bash_script_path_base = g_strdup (g_getenv ("EOS_CHECK_UPDATE_SERVER_GDB_BASH_PATH_BASE"));

  if (bash_script_path_base != NULL)
    {
      g_autoptr(GRegex) regex = g_regex_new ("XXXXXX", 0, 0, error);
      g_autofree gchar *descriptor = NULL;
      g_autofree gchar *bash_script_path = NULL;
      g_autoptr(GFile) bash_script = NULL;

      if (regex == NULL)
        return FALSE;

      descriptor = get_bash_script_descriptor_from_port_file (port_file);
      bash_script_path = g_regex_replace_literal (regex,
                                                  bash_script_path_base,
                                                  -1,
                                                  0,
                                                  descriptor,
                                                  0,
                                                  error);
      if (bash_script_path == NULL)
        return FALSE;

      bash_script = g_file_new_for_path (bash_script_path);
      if (!generate_bash_script (bash_script, (const gchar * const *) argv,
                                 (const gchar * const *) envp, error))
        return FALSE;

      g_printerr ("Bash script %s generated. Run it, make check will continue when port file at %s is generated\n",
                  bash_script_path,
                  raw_port_file_path);
    }
  else if (!test_spawn_async ((const gchar * const *) argv,
                              (const gchar * const *) envp, FALSE, cmd, error))
    return FALSE;

  /* Keep a rough count of the timeout.
   *
   * FIXME: Really, we should be using GSubprocess, tracking the child PID and
   * erroring if it exits earlier than expected, and using a GMainContext
   * rather than sleep(); but those are fairly major changes. */
  i = 0;
  while (!g_file_query_exists (port_file, NULL) &&
         (bash_script_path_base != NULL || i < timeout_seconds))
    {
      sleep (1);
      i++;
    }

  if (!g_file_query_exists (port_file, NULL))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
                   "Timed out waiting for eos-update-server to create port file.");
      return FALSE;
    }

  if (!read_port_file (port_file,
                       out_port,
                       error))
    return FALSE;

  return TRUE;
}

static gboolean
get_head_commit_timestamp (GFile *sysroot_path,
                           GDateTime **out_timestamp,
                           GError **error)
{
  g_autoptr(OstreeRepo) repo = NULL;
  g_autoptr(OstreeSysroot) sysroot = NULL;
  OstreeDeployment *deployment;
  const gchar *checksum;
  g_autoptr(GVariant) commit = NULL;
  g_autoptr(GPtrArray) deployments = NULL;

  sysroot = ostree_sysroot_new (sysroot_path);

  if (!ostree_sysroot_load (sysroot, NULL, error))
    return FALSE;

  deployments = ostree_sysroot_get_deployments (sysroot);
  g_assert (deployments != NULL && deployments->len > 0);

  deployment = OSTREE_DEPLOYMENT (deployments->pdata[0]);
  checksum = ostree_deployment_get_csum (deployment);

  if (!ostree_sysroot_get_repo (sysroot, &repo, NULL, error))
    return FALSE;

  if (!ostree_repo_load_commit (repo, checksum, &commit, NULL, error))
    return FALSE;

  *out_timestamp = g_date_time_new_from_unix_utc (ostree_commit_get_timestamp (commit));

  return TRUE;
}

static GKeyFile *
generate_definition (GFile *client_root,
                     guint16 port,
                     GDateTime *timestamp,
                     const gchar *ostree_path)
{
  GKeyFile *definition = g_key_file_new ();
  g_autofree gchar *basename = g_file_get_basename (client_root);
  g_autofree gchar *service_name = g_strdup_printf ("Test Update Server at %s",
                                                    basename);
  g_autofree gchar *domain_name = g_strdup_printf ("%s.local", basename);
  g_autofree gchar *unix_utc_str = g_date_time_format (timestamp, "%s");
  CmdEnvVar txt_records[] =
    {
      { "eos_txt_version", "1", NULL },
      { "eos_head_commit_timestamp", unix_utc_str, NULL },
      { "eos_ostree_path", ostree_path, NULL },
      { NULL, NULL, NULL }
    };
  g_auto(GStrv) txt = build_cmd_env (txt_records);

  g_key_file_set_string (definition,
                         "service",
                         "name",
                         service_name);

  g_key_file_set_string (definition,
                         "service",
                         "domain",
                         domain_name);

  g_key_file_set_string (definition,
                         "service",
                         "address",
                         "127.0.0.1");

  g_key_file_set_integer (definition,
                          "service",
                          "port",
                          port);

  g_key_file_set_string_list (definition,
                              "service",
                              "txt",
                              (const gchar *const *) txt,
                              g_strv_length (txt));

  return definition;
}

static GFile *
get_update_server_quit_file (GFile *update_server_dir)
{
  return g_file_get_child (update_server_dir, "quit-file");
}

static GFile *
get_update_server_port_file (GFile *update_server_dir)
{
  return g_file_get_child (update_server_dir, "port-file");
}

static GFile *
get_update_server_config_file (GFile *update_server_dir)
{
  return g_file_get_child  (update_server_dir, "config-file.conf");
}

static gboolean
prepare_update_server_dir (GFile *update_server_dir,
                           GError **error)
{
  g_autoptr(GFile) quit_file = NULL;
  g_autoptr(GFile) config_file = NULL;
  g_autofree gchar *config_file_path = NULL;
  const gchar *config = "[Local Network Updates]\nAdvertiseUpdates=true";

  if (!create_directory (update_server_dir, error))
    return FALSE;

  quit_file = get_update_server_quit_file (update_server_dir);
  if (!create_file (quit_file, NULL, error))
    return FALSE;

  config_file = get_update_server_config_file (update_server_dir);
  config_file_path = g_file_get_path (config_file);
  if (!g_file_set_contents (config_file_path, config, -1, error))
    return FALSE;

  return TRUE;
}

static GFile *
get_update_server_dir (GFile *client_root)
{
  return g_file_get_child (client_root, "update-server");
}

gboolean
eos_test_client_run_update_server (EosTestClient *client,
                                   CmdAsyncResult *cmd,
                                   GKeyFile **out_avahi_definition,
                                   GError **error)
{
  g_autoptr(GFile) update_server_dir = get_update_server_dir (client->root);
  g_autoptr(GFile) sysroot = NULL;
  g_autoptr(GFile) repo = NULL;
  g_autoptr(GFile) quit_file = NULL;
  g_autoptr(GFile) port_file = NULL;
  g_autoptr(GFile) config_file = NULL;
  g_autoptr(GDateTime) timestamp = NULL;
  guint16 port;

  if (!prepare_update_server_dir (update_server_dir, error))
    return FALSE;

  sysroot = get_sysroot_for_client (client->root);
  repo = get_repo_for_sysroot (sysroot);
  quit_file = get_update_server_quit_file (update_server_dir);
  port_file = get_update_server_port_file (update_server_dir);
  config_file = get_update_server_config_file (update_server_dir);
  if (!run_update_server (repo,
                          quit_file,
                          port_file,
                          config_file,
                          client->remote_name,
                          &port,
                          cmd,
                          error))
    return FALSE;

  if (!get_head_commit_timestamp (sysroot, &timestamp, error))
    return FALSE;

  *out_avahi_definition = generate_definition (client->root,
                                               port,
                                               timestamp,
                                               client->ostree_path);
  return TRUE;
}

gboolean
eos_test_client_remove_update_server_quit_file (EosTestClient *client,
                                                GError **error)
{
  g_autoptr(GFile) update_server_dir = get_update_server_dir (client->root);
  g_autoptr(GFile) quit_file = get_update_server_quit_file (update_server_dir);

  return rm_rf (quit_file, error);
}

gboolean
eos_test_client_wait_for_update_server (EosTestClient *client,
                                        CmdAsyncResult *cmd,
                                        CmdResult *reaped,
                                        GError **error)
{
  const gchar *bash_script_path_base = NULL;

  bash_script_path_base = g_getenv ("EOS_CHECK_UPDATE_SERVER_GDB_BASH_PATH_BASE");
  if (bash_script_path_base != NULL)
    {
      g_free (reaped->cmdline);
      reaped->cmdline = g_strdup (cmd->cmdline);
      return TRUE;
    }

  return reap_async_cmd (cmd, reaped, error);
}

gboolean
eos_test_client_reap_update_server (EosTestClient *client,
                                    CmdAsyncResult *cmd,
                                    CmdResult *reaped,
                                    GError **error)
{
  if (!eos_test_client_remove_update_server_quit_file (client, error))
    return FALSE;

  return eos_test_client_wait_for_update_server (client,
                                                 cmd,
                                                 reaped,
                                                 error);
}

gboolean
eos_test_client_store_definition (EosTestClient *client,
                                  const gchar *name,
                                  GKeyFile *avahi_definition,
                                  GError **error)
{
  g_autoptr(GFile) updater_dir = get_updater_dir_for_client (client->root);
  g_autoptr(GFile) definitions_dir = updater_avahi_emulator_definitions_dir (updater_dir);
  g_autofree gchar *filename = g_strdup_printf ("%s.ini", name);
  g_autoptr(GFile) definitions_file = g_file_get_child (definitions_dir, filename);

  if (!create_directory (definitions_dir, error))
    return FALSE;

  return save_key_file (definitions_file, avahi_definition, error);
}

static gboolean
get_deploy_ids (GFile *sysroot,
                const gchar *osname,
                gchar ***out_ids,
                GError **error)
{
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;
  g_auto(GStrv) lines = NULL;
  gchar **iter;
  gsize len;
  g_autoptr(GPtrArray) ids = NULL;

  if (!ostree_status (sysroot, &cmd, error))
    return FALSE;

  lines = g_strsplit (cmd.standard_output, "\n", -1);
  len = strlen (osname);
  ids = string_array_new ();
  for (iter = lines; *iter != NULL; ++iter)
    {
      gchar *line = g_strstrip (*iter);

      if (!g_str_has_prefix (line, osname))
        continue;
      if (strlen (line) <= len + 2)
        continue;

      g_ptr_array_add (ids, g_strdup (line + len + 1));
    }
  g_ptr_array_add (ids, NULL);

  *out_ids = (gchar **)g_ptr_array_free (g_steal_pointer (&ids), FALSE);
  return TRUE;
}

static GFile *
get_deployment_dir (GFile *sysroot,
                    const gchar *osname,
                    const gchar *id)
{
  g_autofree gchar *rel_path = g_build_filename ("ostree",
                                                 "deploy",
                                                 osname,
                                                 "deploy",
                                                 id,
                                                 NULL);

  return g_file_get_child (sysroot, rel_path);
}

gboolean
eos_test_client_has_commit (EosTestClient *client,
                            const gchar *osname,
                            guint commit_no,
                            gboolean *out_result,
                            GError **error)
{
  g_auto(GStrv) ids = NULL;
  g_autoptr(GFile) sysroot = get_sysroot_for_client (client->root);
  gchar **iter;

  if (!get_deploy_ids (sysroot, osname, &ids, error))
    return FALSE;

  for (iter = ids; *iter != NULL; ++iter)
    {
      g_autofree gchar *commit_filename = get_commit_filename (commit_no);
      g_autoptr(GFile) dir = get_deployment_dir (sysroot, osname, *iter);
      g_autoptr(GFile) commit_file = g_file_get_child (dir, commit_filename);

      if (g_file_query_exists (commit_file, NULL))
        {
          *out_result = TRUE;
          return TRUE;
        }
    }

  *out_result = FALSE;
  return TRUE;
}

gboolean
eos_test_client_prepare_volume (EosTestClient *client,
                                GFile *volume_path,
                                GError **error)
{
  g_autofree gchar *eos_updater_prepare_volume_binary = g_test_build_filename (G_TEST_BUILT,
                                                                               "..",
                                                                               "src",
                                                                               "eos-updater-prepare-volume",
                                                                               NULL);
  g_autoptr(GFile) sysroot = get_sysroot_for_client (client->root);
  CmdEnvVar envv[] =
    {
      { "EOS_UPDATER_TEST_UPDATER_DEPLOYMENT_FALLBACK", "yes", NULL },
      { "OSTREE_SYSROOT", NULL, sysroot },
      { "OSTREE_SYSROOT_DEBUG", "mutable-deployments", NULL },
      { NULL, NULL, NULL }
    };
  g_autofree gchar *raw_volume_path = g_file_get_path (volume_path);
  const gchar *argv[] =
    {
      eos_updater_prepare_volume_binary,
      raw_volume_path,
      NULL
    };
  g_auto(GStrv) envp = build_cmd_env (envv);
  g_autofree gchar *bash_script_path = NULL;

  if (!create_directory (volume_path, error))
    return FALSE;

  bash_script_path = g_strdup (g_getenv ("EOS_CHECK_UPDATER_PREPARE_VOLUME_GDB_BASH_PATH"));
  if (bash_script_path != NULL)
    {
      g_autoptr(GFile) bash_script = NULL;
      g_autofree gchar *delete_me_path = NULL;
      g_autoptr(GFile) delete_me = NULL;

      bash_script = g_file_new_for_path (bash_script_path);
      if (!generate_bash_script (bash_script, argv, (const gchar * const *) envp, error))
        return FALSE;

      delete_me_path = g_strconcat (bash_script_path, ".deleteme", NULL);
      delete_me = g_file_new_for_path (delete_me_path);
      g_printerr ("Bash script %s generated. Run it, make check will continue when %s is deleted\n",
                  bash_script_path,
                  delete_me_path);

      if (!create_file (delete_me, NULL, error))
        return FALSE;

      while (g_file_query_exists (delete_me, NULL))
        sleep (1);
    }
  else
    {
      g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;

      if (!test_spawn ((const gchar * const *) argv,
                       (const gchar * const *) envp, &cmd, error))
        return FALSE;

      if (!cmd_result_ensure_ok (&cmd, error))
        return FALSE;
    }

  return TRUE;
}

static void
eos_test_autoupdater_dispose_impl (EosTestAutoupdater *autoupdater)
{
  g_clear_object (&autoupdater->root);
}

static void
eos_test_autoupdater_finalize_impl (EosTestAutoupdater *autoupdater)
{
  cmd_result_free (autoupdater->cmd);
}

EOS_DEFINE_REFCOUNTED (EOS_TEST_AUTOUPDATER,
                       EosTestAutoupdater,
                       eos_test_autoupdater,
                       eos_test_autoupdater_dispose_impl,
                       eos_test_autoupdater_finalize_impl)

static GKeyFile *
get_autoupdater_config (UpdateStep step,
                        gint update_interval_in_days,
                        gboolean update_on_mobile)
{
  g_autoptr(GKeyFile) config = g_key_file_new ();

  g_key_file_set_integer (config, "Automatic Updates", "LastAutomaticStep", step);
  g_key_file_set_integer (config, "Automatic Updates", "IntervalDays", update_interval_in_days);
  g_key_file_set_integer (config, "Automatic Updates", "RandomizedDelayDays", 0);
  g_key_file_set_boolean (config, "Automatic Updates", "UpdateOnMobile", update_on_mobile);

  return g_steal_pointer (&config);
}

static GFile *
autoupdater_stamps_dir (GFile *autoupdater_dir)
{
  return g_file_get_child (autoupdater_dir, "stamps");
}

static GFile *
autoupdater_config_file (GFile *autoupdater_dir)
{
  return g_file_get_child (autoupdater_dir, "config");
}

static gboolean
prepare_autoupdater_dir (GFile *autoupdater_dir,
                         GKeyFile *config,
                         GError **error)
{
  g_autoptr(GFile) stamps_dir_path = autoupdater_stamps_dir (autoupdater_dir);
  g_autoptr(GFile) config_file_path = NULL;

  if (!create_directory (stamps_dir_path, error))
    return FALSE;

  config_file_path = autoupdater_config_file (autoupdater_dir);
  if (!save_key_file (config_file_path, config, error))
    return FALSE;

  return TRUE;
}

static const gchar *const gdb_envvars[] = {
  "EOS_CHECK_UPDATER_GDB_BASH_PATH",
  "EOS_CHECK_UPDATE_SERVER_GDB_BASH_PATH_BASE"
};

static gboolean
will_run_gdb (void)
{
  guint idx;

  for (idx = 0; idx < G_N_ELEMENTS (gdb_envvars); ++idx)
    if (g_getenv (gdb_envvars[idx]) != NULL)
      return TRUE;

  return FALSE;
}

static gboolean
will_run_valgrind (void)
{
  /* TODO */
  return FALSE;
}

static gchar *
get_dbus_timeout_value_for_autoupdater (void)
{
  if (will_run_gdb ())
    /* G_MAXINT timeout means no timeout at all.
     */
    return g_strdup_printf ("%d", G_MAXINT);

  if (will_run_valgrind ())
    /* let's optimistically assume that the code under valgrind runs
     * only 10 times slower, so raise the timeout from the default 25
     * seconds to 250.
     */
    return g_strdup_printf ("%d", 250 * 1000);

  return g_strdup ("");
}

static gboolean
spawn_autoupdater (GFile *stamps_dir,
                   GFile *config_file,
                   gboolean force_update,
                   CmdResult *cmd,
                   GError **error)
{
  g_autofree gchar *eos_autoupdater_binary = g_test_build_filename (G_TEST_BUILT,
                                                                    "..",
                                                                    "src",
                                                                    "eos-autoupdater",
                                                                    NULL);
  const gchar *force_update_flag = force_update ? "--force-update" : NULL;
  const gchar *argv[] =
    {
      eos_autoupdater_binary,
      force_update_flag,
      NULL
    };
  g_autofree gchar *dbus_timeout_value = get_dbus_timeout_value_for_autoupdater ();
  CmdEnvVar envv[] =
    {
      { "EOS_UPDATER_TEST_AUTOUPDATER_UPDATE_STAMP_DIR", NULL, stamps_dir },
      { "EOS_UPDATER_TEST_AUTOUPDATER_CONFIG_FILE_PATH", NULL, config_file },
      { "EOS_UPDATER_TEST_AUTOUPDATER_USE_SESSION_BUS", "yes", NULL },
      { "EOS_UPDATER_TEST_AUTOUPDATER_DBUS_TIMEOUT", dbus_timeout_value, NULL },
      { "OSTREE_SYSROOT_DEBUG", "mutable-deployments", NULL },
      { NULL, NULL, NULL }
    };
  g_auto(GStrv) envp = build_cmd_env (envv);

  return test_spawn ((const gchar * const *) argv,
                     (const gchar * const *) envp, cmd, error);
}

static gboolean
spawn_autoupdater_simple (GFile *autoupdater_dir,
                          gboolean force_update,
                          CmdResult *cmd,
                          GError **error)
{
  g_autoptr(GFile) stamps_dir_path = autoupdater_stamps_dir (autoupdater_dir);
  g_autoptr(GFile) config_file_path = autoupdater_config_file (autoupdater_dir);

  return spawn_autoupdater (stamps_dir_path,
                            config_file_path,
                            force_update,
                            cmd,
                            error);
}

EosTestAutoupdater *
eos_test_autoupdater_new (GFile *autoupdater_root,
                          UpdateStep final_auto_step,
                          guint interval_in_days,
                          gboolean update_on_mobile,
                          GError **error)
{
  g_autoptr(GKeyFile) autoupdater_config = NULL;
  g_autoptr(EosTestAutoupdater) autoupdater = NULL;
  g_autoptr(CmdResult) cmd = NULL;

  autoupdater_config = get_autoupdater_config (final_auto_step,
                                               interval_in_days,
                                               update_on_mobile);
  if (!prepare_autoupdater_dir (autoupdater_root,
                                autoupdater_config,
                                error))
    return NULL;

  cmd = g_new0 (CmdResult, 1);
  if (!spawn_autoupdater_simple (autoupdater_root,
                                 TRUE,
                                 cmd,
                                 error))
    return NULL;

  autoupdater = g_object_new (EOS_TEST_TYPE_AUTOUPDATER, NULL);
  autoupdater->root = g_object_ref (autoupdater_root);
  autoupdater->cmd = g_steal_pointer (&cmd);
  return g_steal_pointer (&autoupdater);
}
