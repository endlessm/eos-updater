/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2016 Kinvolk GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors:
 *  - Krzesimir Nowak <krzesimir@kinvolk.io>
 */

#include <test-common/gpg.h>
#include <test-common/misc-utils.h>
#include <test-common/ostree-spawn.h>
#include <test-common/utils.h>
#include <test-common/flatpak-spawn.h>

#include <libeos-updater-util/util.h>

#include <errno.h>
#include <ostree.h>
#include <string.h>
#include <sys/utsname.h>

#ifndef GPG_BINARY
#error "GPG_BINARY is not defined"
#endif

const gchar *const default_vendor = "VENDOR";
const gchar *const default_product = "PRODUCT";
const gchar *const default_collection_id = "com.endlessm.CollectionId";
const gchar *const default_ref = "REF";
const OstreeCollectionRef _default_collection_ref = { (gchar *) "com.endlessm.CollectionId", (gchar *) "REF" };
const OstreeCollectionRef *default_collection_ref = &_default_collection_ref;
const gchar *const default_ostree_path = "OSTREE/PATH";
const gchar *const default_remote_name = "REMOTE";
const gchar *arch_override_name = "arch";
const guint max_commit_number = 10;

void
eos_updater_fixture_setup_full (EosUpdaterFixture *fixture,
                                const gchar       *top_srcdir)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *tmpdir_path = NULL;
  g_autofree gchar *source_gpg_home_path = NULL;
  g_autofree char *services_dir = NULL;

  fixture->dbus = g_test_dbus_new (G_TEST_DBUS_NONE);
  services_dir = g_test_build_filename (G_TEST_BUILT, "services", NULL);
  g_test_dbus_add_service_dir (fixture->dbus, services_dir);
  g_test_dbus_up (fixture->dbus);

  tmpdir_path = g_dir_make_tmp ("eos-updater-test-XXXXXX", &error);
  g_assert_no_error (error);
  fixture->tmpdir = g_file_new_for_path (tmpdir_path);

  g_test_message ("Using fixture directory ‘%s’", tmpdir_path);

  source_gpg_home_path = g_build_filename (top_srcdir, "tests", "gpghome", NULL);
  fixture->gpg_home = create_gpg_keys_directory (fixture->tmpdir, source_gpg_home_path);
}

void
eos_updater_fixture_setup (EosUpdaterFixture *fixture,
                           gconstpointer user_data G_GNUC_UNUSED)
{
  g_autofree gchar *top_srcdir = g_test_build_filename (G_TEST_DIST, "..", NULL);
  eos_updater_fixture_setup_full (fixture, top_srcdir);
}

void
eos_updater_fixture_teardown (EosUpdaterFixture *fixture,
                              gconstpointer user_data G_GNUC_UNUSED)
{
  g_autoptr (GError) error = NULL;
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;

  kill_gpg_agent (fixture->gpg_home);
  eos_updater_remove_recursive (fixture->gpg_home, NULL, &error);
  g_assert_no_error (error);
  g_object_unref (fixture->gpg_home);

  eos_updater_remove_recursive (fixture->tmpdir, NULL, &error);
  g_assert_no_error (error);
  g_object_unref (fixture->tmpdir);

  g_test_dbus_down (fixture->dbus);
  g_object_unref (fixture->dbus);
}

G_DEFINE_TYPE (EosTestSubserver, eos_test_subserver, G_TYPE_OBJECT)

static void
eos_test_subserver_dispose (GObject *object)
{
  EosTestSubserver *self = EOS_TEST_SUBSERVER (object);

  g_clear_pointer (&self->commit_graph, g_hash_table_unref);
  g_clear_pointer (&self->additional_files_for_commit, g_hash_table_unref);
  g_clear_pointer (&self->additional_directories_for_commit, g_hash_table_unref);
  g_clear_pointer (&self->additional_metadata_for_commit, g_hash_table_unref);
  g_clear_object (&self->repo);
  g_clear_object (&self->tree);
  g_clear_object (&self->gpg_home);

  G_OBJECT_CLASS (eos_test_subserver_parent_class)->dispose (object);
}

static void
eos_test_subserver_finalize (GObject *object)
{
  EosTestSubserver *self = EOS_TEST_SUBSERVER (object);

  g_free (self->collection_id);
  g_free (self->keyid);
  g_free (self->ostree_path);
  g_free (self->url);

  G_OBJECT_CLASS (eos_test_subserver_parent_class)->finalize (object);
}

static void
eos_test_subserver_class_init (EosTestSubserverClass *self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (self_class);

  object_class->dispose = eos_test_subserver_dispose;
  object_class->finalize = eos_test_subserver_finalize;
}

static void
eos_test_subserver_init (EosTestSubserver *self)
{
  /* nothing here */
}

EosTestSubserver *
eos_test_subserver_new (const gchar *collection_id,
                        GFile *gpg_home,
                        const gchar *keyid,
                        const gchar *ostree_path,
                        GHashTable *commit_graph,
                        GHashTable *additional_directories_for_commit,
                        GHashTable *additional_files_for_commit,
                        GHashTable *additional_metadata_for_commit  /* (element-type uint GHashTable<utf8, GVariant>) */)
{
  EosTestSubserver *subserver = g_object_new (EOS_TEST_TYPE_SUBSERVER, NULL);

  subserver->collection_id = g_strdup (collection_id);
  subserver->gpg_home = g_object_ref (gpg_home);
  subserver->keyid = g_strdup (keyid);
  subserver->ostree_path = g_strdup (ostree_path);
  subserver->commit_graph = g_hash_table_ref (commit_graph);
  subserver->commits_in_repo = g_hash_table_new_full (g_direct_hash,
                                                      g_direct_equal,
                                                      NULL,
                                                      g_free);
  subserver->additional_directories_for_commit = additional_directories_for_commit ? g_hash_table_ref (additional_directories_for_commit) : NULL;
  subserver->additional_files_for_commit = additional_files_for_commit ? g_hash_table_ref (additional_files_for_commit) : NULL;
  subserver->additional_metadata_for_commit = additional_metadata_for_commit ? g_hash_table_ref (additional_metadata_for_commit) : NULL;

  return subserver;
}

static gchar *
get_commit_filename (guint commit_number)
{
  return g_strdup_printf ("commit%u", commit_number);
}

static gchar *
get_sha256sum_from_strv (const gchar * const *strv)
{
  g_autoptr(GChecksum) sum = g_checksum_new (G_CHECKSUM_SHA256);
  const gchar * const *iter;

  for (iter = strv; *iter != NULL; ++iter)
    {
      const gchar *value = *iter;
      gsize value_len = strlen (value);
      g_assert (value_len <= G_MAXSSIZE);
      g_checksum_update (sum, (const guchar *) value, (gssize) value_len);
    }

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

struct _SimpleFile
{
  gchar *rel_path;
  gchar *contents;
};

SimpleFile *
simple_file_new_steal (gchar *rel_path,
                       gchar *contents)
{
  SimpleFile *file = g_new (SimpleFile, 1);

  file->rel_path = rel_path;
  file->contents = contents;

  return file;
}

void
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
create_directories (GFile   *tree_root,
                    GStrv    directories,
                    GError **error)
{
  gchar **iter;

  for (iter = directories; *iter != NULL; ++iter)
    {
      g_autoptr(GFile) path = g_file_get_child (tree_root, *iter);

      if (!create_directory (path, error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
create_files (GFile      *tree_root,
              GPtrArray  *files,
              GError    **error)
{
  guint idx;

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
create_additional_directories_for_commit (GFile   *tree_root,
                                          GStrv    dirs,
                                          GError **error)
{
  if (dirs == NULL)
    return TRUE;

  return create_directories (tree_root, dirs, error);
}

static gboolean
create_additional_files_for_commit (GFile      *tree_root,
                                    GPtrArray  *files,
                                    GError    **error)
{
  if (files == NULL)
    return TRUE;

  return create_files (tree_root, files, error);
}

static gboolean
prepare_sysroot_contents (GFile   *repo,
                          GFile   *tree_root,
                          GError **error)
{
  const gchar *kernel_version = "4.6";
  g_autoptr(GPtrArray) files = get_sysroot_files (kernel_version);
  g_auto(GStrv) dirs = get_sysroot_dirs (kernel_version);

  if (!create_directories (tree_root, dirs, error))
    return FALSE;

  if (!create_files (tree_root, files, error))
    return FALSE;

  return TRUE;
}

/* Generate a 10mb file at <tree root>/all-commits-dir/bigfile filled
 * with 'x' characters. One middle byte is set to something else,
 * depending on commit number. This is to make sure that the generated
 * delta file for this big file is way smaller than the bigfile.
 */
static gboolean
generate_big_file_for_delta_update (GFile *all_commits_dir,
                                    guint commit_number,
                                    GError **error)
{
  const gsize byte_count = 10 * 1024 * 1024 + 1;
  g_autofree gchar *data = NULL;
  g_autoptr(GFile) big_file = NULL;
  g_autoptr(GBytes) contents = NULL;

  g_assert_cmpint (commit_number, <=, max_commit_number);
  data = g_malloc (byte_count);
  memset (data, 'x', byte_count);
  data[byte_count / 2] = (gchar) ('a' + commit_number);
  big_file = g_file_get_child (all_commits_dir, "bigfile");
  contents = g_bytes_new_take (g_steal_pointer (&data), byte_count);

  return create_file (big_file, contents, error);
}

/* Fills the all-commits-dir directory with some files and
 * directories, so we have plenty of ostree objects in the
 * repository. The generated structure of directories and files fit
 * the following scheme for a commit X:
 *
 * /for-all-commits/commit(0…X).dir/{a,b,c}/{x,y,z}.X
 */
static gboolean
fill_all_commits_dir (GFile *all_commits_dir,
                      guint commit_number,
                      GError **error)
{
  const gchar *dirnames[] = { "a", "b", "c" };
  const gchar *filenames[] = { "x", "y", "z" };
  guint iter;

  g_assert_cmpint (commit_number, <=, max_commit_number);

  {
    g_autofree gchar *commit_dirname = g_strdup_printf ("commit%u.dir", commit_number);
    g_autoptr(GFile) commit_dir = g_file_get_child (all_commits_dir, commit_dirname);
    if (!create_directory (commit_dir, error))
      return FALSE;
  }

  for (iter = 0; iter <= commit_number; ++iter)
    {
      g_autofree gchar *commit_dirname = g_strdup_printf ("commit%u.dir", iter);
      g_autoptr(GFile) commit_dir = g_file_get_child (all_commits_dir, commit_dirname);
      guint dir_iter;

      g_assert_true (g_file_query_exists (commit_dir, NULL));

      for (dir_iter = 0; dir_iter < G_N_ELEMENTS (dirnames); ++dir_iter)
        {
          g_autoptr(GFile) dir = g_file_get_child (commit_dir, dirnames[dir_iter]);
          guint file_iter;

          if (!create_directory (dir, error))
            return FALSE;

          for (file_iter = 0; file_iter < G_N_ELEMENTS (filenames); ++file_iter)
            {
              g_autofree gchar *commit_filename = g_strdup_printf ("%s.%u", filenames[file_iter], commit_number);
              g_autoptr(GFile) file = g_file_get_child (dir, commit_filename);
              g_autoptr(GBytes) contents = g_bytes_new (commit_filename, strlen (commit_filename));

              if (!create_file (file, contents, error))
                return FALSE;
            }
        }
    }

  return TRUE;
}

static GFile *
get_all_commits_dir_for_tree_root (GFile *tree_root)
{
  const gchar *all_commits_dirname = "for-all-commits";

  return g_file_get_child (tree_root, all_commits_dirname);
}

/* Generate some files are directories specific for the given commit
 * number. This includes the commitX file at the toplevel, a plenty of
 * directories and small files, and a big file inside the
 * all-commits-dir directory.
 */
static gboolean
create_commit_files_and_directories (GFile *tree_root,
                                     guint commit_number,
                                     GError **error)
{
  g_autofree gchar *commit_filename = NULL;
  g_autoptr(GFile) commit_file = NULL;
  g_autoptr(GFile) all_commits_dir = NULL;

  commit_filename = get_commit_filename (commit_number);
  commit_file = g_file_get_child (tree_root, commit_filename);
  if (!create_file (commit_file, NULL, error))
    return FALSE;

  all_commits_dir = get_all_commits_dir_for_tree_root (tree_root);
  if (commit_number > 0)
    {
      if (!g_file_query_exists (all_commits_dir, NULL))
        {
          g_autofree gchar *path = g_file_get_path (all_commits_dir);
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "expected the directory %s to exist", path);
          return FALSE;
        }
    }
  else
    {
      if (!create_directory (all_commits_dir, error))
        return FALSE;
    }

  if (!generate_big_file_for_delta_update (all_commits_dir, commit_number, error))
    return FALSE;

  return fill_all_commits_dir (all_commits_dir, commit_number, error);
}

/* Parse <repo>/refs/heads/<ref> to get the commit checksum of the
 * latest commit in ref.
 */
static gboolean
get_current_commit_checksum (GFile *repo,
                             const OstreeCollectionRef *collection_ref,
                             gchar **out_checksum,
                             GError **error)
{
  g_autofree gchar *head_rel_path = NULL;
  g_autoptr(GFile) head = NULL;
  g_autoptr(GBytes) bytes = NULL;

  g_assert_nonnull (out_checksum);

  head_rel_path = g_build_filename ("refs", "heads", collection_ref->ref_name, NULL);
  head = g_file_get_child (repo, head_rel_path);
  if (!load_to_bytes (head,
                      &bytes,
                      error))
    return FALSE;

  *out_checksum = g_strstrip (g_strndup (g_bytes_get_data (bytes, NULL),
                                         g_bytes_get_size (bytes)));
  return TRUE;
}

static gpointer
maybe_hashtable_lookup (GHashTable *table,
                        gpointer    key)
{
  if (table == NULL)
    return NULL;

  return g_hash_table_lookup (table, key);
}

EosTestUpdaterCommitInfo *
eos_test_updater_commit_info_new (guint                      sequence_number,
                                  guint                      parent,
                                  const OstreeCollectionRef *collection_ref)
{
  EosTestUpdaterCommitInfo *commit_info = g_new0 (EosTestUpdaterCommitInfo, 1);

  commit_info->sequence_number = sequence_number;
  commit_info->parent = parent;
  commit_info->collection_ref = ostree_collection_ref_dup (collection_ref);

  return commit_info;
}

void
eos_test_updater_commit_info_free (EosTestUpdaterCommitInfo *info)
{
  g_clear_pointer (&info->collection_ref, ostree_collection_ref_free);

  g_free (info);
}

typedef gpointer (*CopyFunc) (gpointer);

static gpointer
no_copy (gpointer value)
{
  return value;
}

/* Flip keys to values and vice versa */
static GHashTable *
reverse_hashtable (GHashTable     *ht,
                   GHashFunc       hash_func,
                   GEqualFunc      equal_func,
                   GDestroyNotify  key_destroy,
                   GDestroyNotify  value_destroy,
                   CopyFunc        key_copy,
                   CopyFunc        value_copy)
{
  g_autoptr(GHashTable) reversed_ht = g_hash_table_new_full (hash_func,
                                                             equal_func,
                                                             key_destroy,
                                                             value_destroy);
  gpointer key, value;
  GHashTableIter iter;

  g_hash_table_iter_init (&iter, ht);
  while (g_hash_table_iter_next (&iter, &key, &value))
    g_hash_table_insert (reversed_ht,
                         (*key_copy) (value),
                         (*value_copy) (key));

  return g_steal_pointer (&reversed_ht);
}

void
eos_test_updater_insert_commit_steal_info (GHashTable               *commit_graph,
                                           EosTestUpdaterCommitInfo *commit_info)
{
  g_hash_table_insert (commit_graph,
                       GUINT_TO_POINTER (commit_info->sequence_number),
                       commit_info);
}

static void
populate_commit_chain (GHashTable                *commit_graph,
                       guint                      commit,
                       const OstreeCollectionRef *collection_ref,
                       GHashTable                *commit_to_ref)
{
  /* Recurse down first until we either hit a commit that is known
   * in the commit_to_ref table or the zeroeth commit */
  guint parent_commit = commit == 0 ? commit : commit - 1;
  gboolean has_parent =
    (commit > 0 && !g_hash_table_lookup (commit_to_ref, GUINT_TO_POINTER (parent_commit)));

  if (has_parent)
    populate_commit_chain (commit_graph, parent_commit, collection_ref, commit_to_ref);

  /* Populate commit info now */
  eos_test_updater_insert_commit_steal_info (commit_graph,
                                             eos_test_updater_commit_info_new (commit,
                                                                               parent_commit,
                                                                               collection_ref));
}

static gint
sort_commits (gconstpointer lhs, gconstpointer rhs)
{
  /* Technically this is playing games with casting -
   * we should not have commits larger than G_MAXINT here */
  gint lhs_commit = GPOINTER_TO_INT (lhs);
  gint rhs_commit = GPOINTER_TO_INT (rhs);

  /* Descending */
  return rhs_commit - lhs_commit;
}

/**
 * eos_test_updater_populate_commit_graph_from_leaf_nodes:
 * @commit_graph: A #GHashTable
 * @leaf_nodes: (element-type guint OstreeCollectionRef) A #GHashTable mapping
 *              leaf commit ids to OstreeCollectionRef.
 *
 * "Fill in" the rest of the commit graph from the @leaf_nodes down. Each
 * parent of a leaf node will use the same #OstreeCollectionRef, unless another
 * entry was specified in @leaf_nodes with that commit id, at which point parents
 * of that commit will use that #OstreeCollectionRef instead.
 *
 * This function destroys any existing graph structure and populates the graph
 * from scratch.
 *
 */
void
eos_test_updater_populate_commit_graph_from_leaf_nodes (GHashTable *commit_graph,
                                                        GHashTable *leaf_nodes)
{
  g_autoptr(GHashTable) commit_to_ref = reverse_hashtable (leaf_nodes,
                                                           g_direct_hash,
                                                           g_direct_equal,
                                                           NULL,
                                                           (GDestroyNotify) ostree_collection_ref_free,
                                                           no_copy,
                                                           (CopyFunc) ostree_collection_ref_dup);

  /* Each of the key-value pairs in leaf_nodes points to a candidate
   * leaf node for a given refspec. From there we recursively go down the
   * tree and create new EosTestUpdaterCommitInfo objects, unless we
   * see an entry for that commit in commit_to_ref (we'll start from
   * that key the next time around */
  g_autoptr(GList) commit_keys = g_list_sort (g_hash_table_get_keys (commit_to_ref),
                                              sort_commits);

  /* Clear the hash-table first */
  g_hash_table_remove_all (commit_graph);

  for (GList *iter = commit_keys; iter != NULL; iter = iter->next)
    {
      guint commit = GPOINTER_TO_UINT (iter->data);
      const OstreeCollectionRef *collection_ref =
        g_hash_table_lookup (commit_to_ref, GUINT_TO_POINTER (commit));
      populate_commit_chain (commit_graph,
                             commit,
                             collection_ref,
                             commit_to_ref);
    }
}

GHashTable *
eos_test_updater_commit_graph_new_from_leaf_nodes (GHashTable *leaf_nodes)
{
  GHashTable *commit_graph = g_hash_table_new_full (g_direct_hash,
                                                    g_direct_equal,
                                                    NULL,
                                                    (GDestroyNotify) eos_test_updater_commit_info_free);

  if (leaf_nodes != NULL)
    eos_test_updater_populate_commit_graph_from_leaf_nodes (commit_graph,
                                                            leaf_nodes);

  return commit_graph;
}

/* Prepare a commit. This function no longer recursively
 * prepares commits, that is now the responsibility of the caller */
static gboolean
prepare_commit (GFile *repo,
                GFile *tree_root,
                EosTestUpdaterCommitInfo *commit_info,
                GFile *gpg_home,
                const gchar *keyid,
                GHashTable *additional_directories_for_commit,
                GHashTable *additional_files_for_commit,
                GHashTable *additional_metadata_for_commit  /* (element-type uint GHashTable<utf8, GVariant>) */,
                gchar **out_checksum,
                GError **error)
{
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;
  g_autoptr(GDateTime) timestamp = NULL;
  g_autofree gchar *subject = NULL;
  guint commit_number = commit_info->sequence_number;
  const OstreeCollectionRef *collection_ref = commit_info->collection_ref;

  if (commit_number > max_commit_number)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "exceeded commit limit %u with %u",
                   max_commit_number, commit_number);
      return FALSE;
    }

  {
    g_autofree gchar *commit_filename = get_commit_filename (commit_number);
    g_autoptr(GFile) commit_file = g_file_get_child (tree_root, commit_filename);

    if (g_file_query_exists (commit_file, NULL))
      {
        if (out_checksum != NULL)
          return get_current_commit_checksum (repo,
                                              collection_ref,
                                              out_checksum,
                                              error);

        return TRUE;
      }
  }

  /* Only need to prepare sysroot contents on the first commit */
  if (commit_number == 0)
    {
      if (!prepare_sysroot_contents (repo,
                                     tree_root,
                                     error))
        return FALSE;
    }

  /* FIXME: Right now this unconditionally puts all the files for a given
   * commit into the tree and does not clean up afterwards. This is fine
   * for linear histories, but could have some unexpected results for non-linear
   * histories.
   *
   * At the moment this does not negatively impact the tests as the tests which
   * test non-linear histories don't test the actual files in a commit.
   *
   * We could clean all this up between commits, however, that would probably make
   * test performance worse since it would mean that we would have to delete
   * and recreate files (especially large ones!) on each commit.
   */
  if (!create_commit_files_and_directories (tree_root, commit_number, error))
    return FALSE;

  if (!create_additional_directories_for_commit (tree_root,
                                                 maybe_hashtable_lookup (additional_directories_for_commit,
                                                                         GUINT_TO_POINTER (commit_number)),
                                                 error))
    return FALSE;

  if (!create_additional_files_for_commit (tree_root,
                                           maybe_hashtable_lookup (additional_files_for_commit,
                                                                   GUINT_TO_POINTER (commit_number)),
                                           error))
    return FALSE;

  subject = g_strdup_printf ("Test commit %u", commit_number);
  timestamp = days_ago (max_commit_number - commit_number);

  if (!ostree_commit (repo,
                      tree_root,
                      subject,
                      commit_info->collection_ref->ref_name,
                      gpg_home,
                      keyid,
                      timestamp,
                      maybe_hashtable_lookup (additional_metadata_for_commit,
                                              GUINT_TO_POINTER (commit_number)),
                      &cmd,
                      error))
    return FALSE;

  if (!cmd_result_ensure_ok (&cmd, error))
    return FALSE;

  if (out_checksum != NULL)
    return get_current_commit_checksum (repo,
                                        commit_info->collection_ref,
                                        out_checksum,
                                        error);

  return TRUE;
}

static gboolean
generate_delta_files (GFile *repo,
                      const gchar *from,
                      const gchar *to,
                      GError **error)
{
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;

  if (!ostree_static_delta_generate (repo, from, to, &cmd, error))
    return FALSE;

  return cmd_result_ensure_ok (&cmd, error);
}

/**
 * eos_test_updater_commit_graph_walk:
 * @commit_graph: A #GHashTable
 * @walk_func: A #EosTestUpdaterCommitGraphWalkFunc called on each node in level order
 * @walk_func_data: Closure for @walk_func
 * @error: A #GError
 *
 * Walk the commit graph in a breadth-first fashion, traversing
 * in a level order. walk_func will be called on each commit with
 * the #EosTestUpdaterCommitInfo for each commit as well as its parent.
 *
 * @walk_func may mutate outer state and may fail.
 *
 * The implementation here is a little awkward since we need to do
 * an O(V) linear scan to expand children for each node, making the
 * walk cost O(V^2).
 *
 * Returns: %TRUE if no @walk_func invocation failed on the graph, %FALSE with
 *          @error set otherwise.
 */
gboolean
eos_test_updater_commit_graph_walk (GHashTable                         *commit_graph,
                                    EosTestUpdaterCommitGraphWalkFunc   walk_func,
                                    gpointer                            walk_func_data,
                                    GError                            **error)
{
  GQueue queue = G_QUEUE_INIT;

  g_queue_init (&queue);
  g_queue_push_tail (&queue, GUINT_TO_POINTER (0));

  while (!g_queue_is_empty (&queue))
    {
      guint commit = GPOINTER_TO_UINT (g_queue_pop_head (&queue));
      GHashTableIter iter;
      gpointer key, value;
      EosTestUpdaterCommitInfo *commit_info =
        g_hash_table_lookup (commit_graph,
                             GUINT_TO_POINTER (commit));
      EosTestUpdaterCommitInfo *parent_commit_info =
        g_hash_table_lookup (commit_graph,
                             GUINT_TO_POINTER (commit_info->parent));

      /* Process node */
      if (!((*walk_func) (commit_info,
                          commit == commit_info->parent ? NULL : parent_commit_info,
                          walk_func_data,
                          error)))
        {
          g_queue_clear (&queue);
          return FALSE;
        }

      g_hash_table_iter_init (&iter, commit_graph);
      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          guint candidate_child = GPOINTER_TO_UINT (key);
          guint candidate_parent = ((EosTestUpdaterCommitInfo *) value)->parent;

          /* Special case - root node has self as parent, ignore this */
          if (candidate_parent == commit &&
              candidate_parent != candidate_child)
            g_queue_push_tail (&queue, GUINT_TO_POINTER (candidate_child));
        }
    }

  g_queue_clear (&queue);
  return TRUE;
}

static gboolean
make_commit_if_not_available (EosTestUpdaterCommitInfo  *commit_info,
                              EosTestUpdaterCommitInfo  *parent_commit_info,
                              gpointer                   user_data,
                              GError                   **error)
{
  EosTestSubserver *subserver = user_data;
  g_autofree gchar *checksum = NULL;

  /* Commit is already in the repo, ignore.
   *
   * We can't insert the commit into this table just yet, we
   * need to make it first in order to get the checksum.
   */
  if (g_hash_table_contains (subserver->commits_in_repo,
                             GUINT_TO_POINTER (commit_info->sequence_number)))
    return TRUE;

  /* Make the commit */
  if (!prepare_commit (subserver->repo,
                       subserver->tree,
                       commit_info,
                       subserver->gpg_home,
                       subserver->keyid,
                       subserver->additional_directories_for_commit,
                       subserver->additional_files_for_commit,
                       subserver->additional_metadata_for_commit,
                       &checksum,
                       error))
    return FALSE;

  if (parent_commit_info != NULL)
    {
      const gchar *old_checksum = g_hash_table_lookup (subserver->commits_in_repo,
                                                       GUINT_TO_POINTER (parent_commit_info->sequence_number));

      g_assert_nonnull (old_checksum);

      if (!generate_delta_files (subserver->repo,
                                 old_checksum,
                                 checksum,
                                 error))
        return FALSE;
    }

  /* Insert commit checksum into hashtable */
  g_hash_table_insert (subserver->commits_in_repo,
                       GUINT_TO_POINTER (commit_info->sequence_number),
                       g_strdup (checksum));

  return TRUE;
}

/* Updates the subserver to reflect the state of
 * the internal commit graph.  This involves
 * creating the commits, generating ref files and
 * delta files, and updating the summary.
 */
static gboolean
update_commits (EosTestSubserver *subserver,
                GError **error)
{
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;

  if (!eos_test_updater_commit_graph_walk (subserver->commit_graph,
                                           make_commit_if_not_available,
                                           subserver,
                                           error))
    return FALSE;

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

void
eos_test_subserver_populate_commit_graph_from_leaf_nodes (EosTestSubserver *subserver,
                                                          GHashTable       *leaf_nodes)
{
  eos_test_updater_populate_commit_graph_from_leaf_nodes (subserver->commit_graph,
                                                          leaf_nodes);
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
                        subserver->collection_id,
                        &cmd,
                        error))
        return FALSE;
      if (!cmd_result_ensure_ok (&cmd, error))
        return FALSE;
    }

  return update_commits (subserver, error);
}

/**
 * EosTestServer:
 *
 * #EosTestServer is a mock server implementation which uses one or more
 * *subservers* to serve ostree branches over HTTP. The content is served from
 * the `main` directory of a given httpd root, or from ostree paths below the
 * root.
 */
G_DEFINE_TYPE (EosTestServer, eos_test_server, G_TYPE_OBJECT)

static void
eos_test_server_dispose (GObject *object)
{
  EosTestServer *self = EOS_TEST_SERVER (object);

  g_clear_object (&self->root);
  g_clear_pointer (&self->subservers, g_ptr_array_unref);

  G_OBJECT_CLASS (eos_test_server_parent_class)->dispose (object);
}

static void
eos_test_server_finalize (GObject *object)
{
  EosTestServer *self = EOS_TEST_SERVER (object);

  g_free (self->url);

  G_OBJECT_CLASS (eos_test_server_parent_class)->finalize (object);
}

static void
eos_test_server_class_init (EosTestServerClass *self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (self_class);

  object_class->dispose = eos_test_server_dispose;
  object_class->finalize = eos_test_server_finalize;
}

static void
eos_test_server_init (EosTestServer *self)
{
  /* nothing here */
}

static gboolean
run_httpd (GFile *served_root,
           GFile *httpd_dir,
           gchar **out_url,
           GError **error)
{
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;
  g_autoptr(GFile) port_file = g_file_get_child (httpd_dir, "port-file");
  g_autoptr(GFile) log_file = g_file_get_child (httpd_dir, "httpd-log");
  guint16 port;

  if (!ostree_httpd (served_root,
                     port_file,
                     log_file,
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
                           const OstreeCollectionRef *collection_ref,
                           guint commit_number,
                           GFile *gpg_home,
                           const gchar *keyid,
                           const gchar *ostree_path,
                           GHashTable *additional_directories_for_commit,
                           GHashTable *additional_files_for_commit,
                           GHashTable *additional_metadata_for_commit  /* (element-type uint GHashTable<utf8, GVariant>) */,
                           GError **error)
{
  g_autoptr(GPtrArray) subservers = object_array_new ();
  g_autoptr(GHashTable) leaf_commit_nodes = eos_test_subserver_ref_to_commit_new ();
  g_autoptr(GHashTable) commit_graph = NULL;

  g_hash_table_insert (leaf_commit_nodes,
                       ostree_collection_ref_dup (collection_ref),
                       GUINT_TO_POINTER (commit_number));
  commit_graph = eos_test_updater_commit_graph_new_from_leaf_nodes (leaf_commit_nodes);

  g_ptr_array_add (subservers, eos_test_subserver_new (collection_ref->collection_id,
                                                       gpg_home,
                                                       keyid,
                                                       ostree_path,
                                                       commit_graph,
                                                       additional_directories_for_commit,
                                                       additional_files_for_commit,
                                                       additional_metadata_for_commit));

  return eos_test_server_new (server_root,
                              subservers,
                              error);
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
G_DEFINE_TYPE (EosTestClient, eos_test_client, G_TYPE_OBJECT)

static void
eos_test_client_dispose (GObject *object)
{
  EosTestClient *self = EOS_TEST_CLIENT (object);

  g_clear_object (&self->root);

  G_OBJECT_CLASS (eos_test_client_parent_class)->dispose (object);
}

static void
eos_test_client_finalize (GObject *object)
{
  EosTestClient *self = EOS_TEST_CLIENT (object);

  g_free (self->vendor);
  g_free (self->product);
  g_free (self->remote_name);
  g_free (self->ostree_path);
  g_free (self->cpuinfo);
  g_free (self->cmdline);
  g_free (self->uname_machine);

  G_OBJECT_CLASS (eos_test_client_parent_class)->finalize (object);
}

static void
eos_test_client_class_init (EosTestClientClass *self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (self_class);

  object_class->dispose = eos_test_client_dispose;
  object_class->finalize = eos_test_client_finalize;
}

static void
eos_test_client_init (EosTestClient *self)
{
  /* nothing here */
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
                        const OstreeCollectionRef *collection_ref,
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
                          collection_ref,
                          gpg_key,
                          &cmd,
                          error))
    return FALSE;
  if (!cmd_result_ensure_ok (&cmd, error))
    return FALSE;

  cmd_result_clear (&cmd);
  if (!ostree_pull (repo,
                    remote_name,
                    collection_ref->ref_name,
                    &cmd,
                    error))
    return FALSE;
  if (!cmd_result_ensure_ok (&cmd, error))
    return FALSE;

  refspec = g_strdup_printf ("%s:%s", remote_name, collection_ref->ref_name);
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
copy_summary (GFile *source_repo,
              GFile *client_root,
              const gchar *ref,
              GError **error)
{
  g_autoptr(GFile) sysroot = get_sysroot_for_client (client_root);
  g_autoptr(GFile) repo = get_repo_for_sysroot (sysroot);

  /* FIXME: We have to propagate the signed summary to each LAN server for now;
   * once https://phabricator.endlessm.com/T19293 is fixed, we can use unsigned
   * summaries and generate them on the LAN server instead. */
  g_autoptr(GFile) src_summary = g_file_get_child (source_repo, "summary");
  g_autoptr(GFile) src_summary_sig = g_file_get_child (source_repo, "summary.sig");
  g_autoptr(GFile) dest_summary = g_file_get_child (repo, "summary");
  g_autoptr(GFile) dest_summary_sig = g_file_get_child (repo, "summary.sig");

  if (!copy_file_and_signature (src_summary, src_summary_sig,
                                dest_summary, dest_summary_sig, error))
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

static GKeyFile *
get_updater_config (DownloadSource *order,
                    gsize           n_sources,
                    GPtrArray      *override_uris)
{
  g_autoptr(GKeyFile) config = NULL;
  gsize idx;
  g_autoptr(GPtrArray) source_strs = NULL;

  g_return_val_if_fail (n_sources <= G_MAXUINT, NULL);

  config = g_key_file_new ();
  source_strs = g_ptr_array_sized_new ((guint) n_sources);

  for (idx = 0; idx < n_sources; ++idx)
    g_ptr_array_add (source_strs, (gpointer)download_source_to_string (order[idx]));
  g_key_file_set_string_list (config,
                              "Download",
                              "Order",
                              (const gchar * const*)source_strs->pdata,
                              source_strs->len);

  g_key_file_set_string_list (config, "Download", "OverrideUris",
                              (const gchar * const*) ((override_uris != NULL) ? override_uris->pdata : NULL),
                              (override_uris != NULL) ? override_uris->len : 0);

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

static GFile *
updater_cpuinfo_file (GFile *updater_dir)
{
  return g_file_get_child (updater_dir, "cpuinfo");
}

static GFile *
updater_cmdline_file (GFile *updater_dir)
{
  return g_file_get_child (updater_dir, "cmdline");
}

GFile *
get_flatpak_upgrade_state_dir_for_updater_dir (GFile *updater_dir)
{
  return g_file_get_child (updater_dir, "flatpak-deployments");
}

GFile *
get_flatpak_user_dir_for_updater_dir (GFile *updater_dir)
{
  return g_file_get_child (updater_dir, "flatpak-user");
}

GFile *
get_flatpak_autoinstall_override_dir (GFile *client_root)
{
  return g_file_get_child (client_root, "flatpak-autoinstall-override");
}

static gboolean
prepare_updater_dir (GFile *updater_dir,
                     GKeyFile *config_file,
                     GKeyFile *hw_file,
                     const gchar *cpuinfo,
                     const gchar *cmdline,
                     GError **error)
{
  g_autoptr(GFile) quit_file_path = NULL;
  g_autoptr(GFile) config_file_path = NULL;
  g_autoptr(GFile) hw_file_path = NULL;
  g_autoptr(GFile) cpuinfo_file_path = NULL;
  g_autoptr(GFile) cmdline_file_path = NULL;

  if (!create_directory (updater_dir, error))
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

  cpuinfo_file_path = updater_cpuinfo_file (updater_dir);
  if (!g_file_replace_contents (cpuinfo_file_path, cpuinfo, strlen (cpuinfo),
                                NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL, error))
    return FALSE;

  cmdline_file_path = updater_cmdline_file (updater_dir);
  if (!g_file_replace_contents (cmdline_file_path, cmdline, strlen (cmdline),
                                NULL, FALSE, G_FILE_CREATE_NONE, NULL, NULL, error))
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
    "set -e\n";
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
                          "gdb -ex \"break main\" -ex %s %s\n",
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
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;
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
  GMainContext *context;  /* (unowned) (nullable) */
  gboolean appeared;
  guint watch_id;
} WatchUpdater;

static void
com_endlessm_updater_appeared (GDBusConnection *connection,
                               const gchar *name,
                               const gchar *name_owner,
                               gpointer wu_ptr)
{
  WatchUpdater *wu = wu_ptr;

  wu->appeared = TRUE;
  g_bus_unwatch_name (wu->watch_id);
  wu->watch_id = 0;

  g_main_context_wakeup (wu->context);
}

static gboolean
spawn_updater (GFile *sysroot,
               GFile *repo,
               GFile *config_file,
               GFile *hw_file,
               GFile *quit_file,
               GFile *flatpak_upgrade_state_dir,
               GFile *flatpak_installation_dir,
               GFile *flatpak_autoinstall_override_dir,
               GFile *cpuinfo_file,
               GFile *cmdline_file,
               const gchar *osname,
               gboolean fatal_warnings,
               const gchar *uname_machine,
               gboolean is_split_disk,
               gboolean force_follow_checkpoint,
               CmdAsyncResult *cmd,
               GError **error)
{
  g_autofree gchar *eos_updater_binary = g_test_build_filename (G_TEST_BUILT,
                                                                "..",
                                                                "eos-updater",
                                                                "eos-updater",
                                                                NULL);
  CmdEnvVar envv[] =
    {
      { "EOS_UPDATER_TEST_UPDATER_CONFIG_FILE_PATH", NULL, config_file },
      { "EOS_UPDATER_TEST_UPDATER_CUSTOM_DESCRIPTORS_PATH", NULL, hw_file },
      { "EOS_UPDATER_TEST_UPDATER_DEPLOYMENT_FALLBACK", "yes", NULL },
      { "EOS_UPDATER_TEST_UPDATER_QUIT_FILE", NULL, quit_file },
      { "EOS_UPDATER_TEST_UPDATER_USE_SESSION_BUS", "yes", NULL },
      { "EOS_UPDATER_TEST_UPDATER_OSTREE_OSNAME", osname, NULL },
      { "EOS_UPDATER_TEST_UPDATER_FLATPAK_UPGRADE_STATE_DIR", NULL, flatpak_upgrade_state_dir },
      { "EOS_UPDATER_TEST_FLATPAK_INSTALLATION_DIR", NULL, flatpak_installation_dir },
      { "EOS_UPDATER_TEST_UPDATER_FLATPAK_AUTOINSTALL_OVERRIDE_DIRS", NULL, flatpak_autoinstall_override_dir },
      { "EOS_UPDATER_TEST_OVERRIDE_ARCHITECTURE", arch_override_name, NULL },
      { "EOS_UPDATER_TEST_UPDATER_OVERRIDE_LOCALES", "locale", NULL },
      { "EOS_UPDATER_TEST_IS_SPLIT_DISK", is_split_disk ? "1" : "", NULL },
      { "EOS_UPDATER_TEST_UNAME_MACHINE", uname_machine, NULL },
      { "EOS_UPDATER_TEST_CPUINFO_PATH", NULL, cpuinfo_file },
      { "EOS_UPDATER_TEST_CMDLINE_PATH", NULL, cmdline_file },
      { "EOS_UPDATER_FORCE_FOLLOW_CHECKPOINT", force_follow_checkpoint ? "1" : "", NULL },
      { "OSTREE_SYSROOT", NULL, sysroot },
      { "OSTREE_REPO", NULL, repo },
      { "OSTREE_SYSROOT_DEBUG", "mutable-deployments", NULL },
      { "EOS_DISABLE_METRICS", "1", NULL },
      { "FLATPAK_SYSTEM_HELPER_ON_SESSION", "1", NULL },
      { "G_DEBUG", fatal_warnings ? "gc-friendly,fatal-warnings" : "gc-friendly", NULL },
      /* Flatpak uses $XDG_CACHE_HOME and we need to set it explicitly since
       * we're using G_TEST_OPTION_ISOLATE_DIRS */
      { "XDG_CACHE_HOME", g_get_user_cache_dir (), NULL },
      { NULL, NULL, NULL }
    };
  const gchar *argv[] =
    {
      eos_updater_binary,
      NULL
    };
  g_auto(GStrv) envp = build_cmd_env (envv);
  g_autofree gchar *bash_script_path = NULL;
  WatchUpdater wu = { NULL, FALSE, 0u };
  guint id = g_bus_watch_name (G_BUS_TYPE_SESSION,
                               "com.endlessm.Updater",
                               G_BUS_NAME_WATCHER_FLAGS_NONE,
                               com_endlessm_updater_appeared,
                               NULL,
                               &wu,
                               NULL);

  wu.watch_id = id;
  bash_script_path = g_strdup (g_getenv ("EOS_CHECK_UPDATER_GDB_BASH_PATH"));
  if (bash_script_path != NULL)
    {
      g_autoptr(GFile) path = g_file_new_for_path (bash_script_path);

      if (!generate_bash_script (path, argv, (const gchar * const *) envp, error))
        return FALSE;

      g_test_message ("Bash script %s generated. Run it, make check will continue when com.endlessm.Updater appears on the test session bus\n",
                      bash_script_path);

    }
  else if (!test_spawn_async ((const gchar * const *) argv,
                              (const gchar * const *) envp, FALSE, cmd, error))
    return FALSE;

  while (!wu.appeared)
    g_main_context_iteration (NULL, TRUE);

  return TRUE;
}

static gboolean
spawn_updater_simple (GFile *sysroot,
                      GFile *repo,
                      GFile *updater_dir,
                      const gchar *osname,
                      gboolean fatal_warnings,
                      const gchar *uname_machine,
                      gboolean is_split_disk,
                      gboolean force_follow_checkpoint,
                      CmdAsyncResult *cmd,
                      GError **error)
{
  g_autoptr(GFile) config_file_path = updater_config_file (updater_dir);
  g_autoptr(GFile) hw_file_path = updater_hw_file (updater_dir);
  g_autoptr(GFile) cpuinfo_file = updater_cpuinfo_file (updater_dir);
  g_autoptr(GFile) cmdline_file = updater_cmdline_file (updater_dir);
  g_autoptr(GFile) quit_file_path = updater_quit_file (updater_dir);
  g_autoptr(GFile) flatpak_upgrade_state_dir_path = get_flatpak_upgrade_state_dir_for_updater_dir (updater_dir);
  g_autoptr(GFile) flatpak_installation_dir_path = get_flatpak_user_dir_for_updater_dir (updater_dir);
  g_autoptr(GFile) flatpak_autoinstall_override_dir = get_flatpak_autoinstall_override_dir (updater_dir);

  return spawn_updater (sysroot,
                        repo,
                        config_file_path,
                        hw_file_path,
                        quit_file_path,
                        flatpak_upgrade_state_dir_path,
                        flatpak_installation_dir_path,
                        flatpak_autoinstall_override_dir,
                        cpuinfo_file,
                        cmdline_file,
                        osname,
                        fatal_warnings,
                        uname_machine,
                        is_split_disk,
                        force_follow_checkpoint,
                        cmd,
                        error);
}

static gboolean
run_updater (GFile *client_root,
             DownloadSource *order,
             gsize n_sources,
             GPtrArray *override_uris,
             const gchar *cpuinfo_file_override,
             const gchar *cmdline_file_override,
             const gchar *vendor,
             const gchar *product,
             const gchar *remote_name,
             gboolean fatal_warnings,
             const gchar *uname_machine_override,
             gboolean is_split_disk,
             gboolean force_follow_checkpoint,
             CmdAsyncResult *updater_cmd,
             GError **error)
{
  g_autoptr(GKeyFile) updater_config = NULL;
  g_autoptr(GKeyFile) hw_config = NULL;
  g_autoptr(GFile) sysroot = get_sysroot_for_client (client_root);
  g_autoptr(GFile) repo = get_repo_for_sysroot (sysroot);
  g_autoptr(GFile) updater_dir = get_updater_dir_for_client (client_root);
  g_autofree gchar *cpuinfo_file_override_allocated = NULL;

  if (cpuinfo_file_override == NULL &&
      g_file_get_contents ("/proc/cpuinfo", &cpuinfo_file_override_allocated, NULL, NULL))
    cpuinfo_file_override = cpuinfo_file_override_allocated;
  if (cmdline_file_override == NULL)
    /* arbitrary default */
    cmdline_file_override = "BOOT_IMAGE=(hd0,gpt3)/boot/ostree/eos-c8cadea7ee2eb6b5fe6a15144bf2fc123327d5a0302e8e396cbb93c7e20f4be1/vmlinuz-5.11.0-12-generic root=UUID=11356111-ea76-4f63-9d7e-1d6b9d10a065 rw splash plymouth.ignore-serial-consoles quiet loglevel=0 ostree=/ostree/boot.0/eos/c8cadea7ee2eb6b5fe6a15144bf2fc123327d5a0302e8e396cbb93c7e20f4be1/0";
  if (uname_machine_override == NULL)
    {
      struct utsname buf;
      if (uname (&buf) == 0)
        uname_machine_override = buf.machine;
    }

  updater_config = get_updater_config (order,
                                       n_sources,
                                       override_uris);
  hw_config = get_hw_config (vendor, product);
  if (!prepare_updater_dir (updater_dir,
                            updater_config,
                            hw_config,
                            cpuinfo_file_override,
                            cmdline_file_override,
                            error))
    return FALSE;
  if (!spawn_updater_simple (sysroot,
                             repo,
                             updater_dir,
                             remote_name,
                             fatal_warnings,
                             uname_machine_override,
                             is_split_disk,
                             force_follow_checkpoint,
                             updater_cmd,
                             error))
    return FALSE;

  return TRUE;
}

static gboolean
ensure_ref_in_subserver (const OstreeCollectionRef *collection_ref,
                         EosTestSubserver *subserver)
{
  /* Linear scan of commit infos to check if the ref is
   * in the commit_graph */
  GHashTableIter iter;
  gpointer value;

  g_hash_table_iter_init (&iter, subserver->commit_graph);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      EosTestUpdaterCommitInfo *commit_info = value;

      if (collection_ref->collection_id != NULL)
        {
          if (ostree_collection_ref_equal (commit_info->collection_ref, collection_ref))
            return TRUE;
        }
      else
        {
          if (g_strcmp0 (commit_info->collection_ref->ref_name,
                         collection_ref->ref_name) == 0)
            return TRUE;
        }
    }

  return FALSE;
}

EosTestClient *
eos_test_client_new (GFile *client_root,
                     const gchar *remote_name,
                     EosTestSubserver *subserver,
                     const OstreeCollectionRef *collection_ref,
                     const gchar *vendor,
                     const gchar *product,
                     GError **error)
{
  g_autoptr(EosTestClient) client = NULL;

  if (!ensure_ref_in_subserver (collection_ref, subserver))
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Could not find collection ref %s:%s in subserver commits",
                   collection_ref->collection_id,
                   collection_ref->ref_name);
      return FALSE;
    }

  if (!prepare_client_sysroot (client_root,
                               remote_name,
                               subserver->url,
                               collection_ref,
                               subserver->gpg_home,
                               subserver->keyid,
                               error))
    return FALSE;

  if (!copy_summary (subserver->repo,
                     client_root,
                     collection_ref->ref_name,
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

void
eos_test_client_set_is_split_disk (EosTestClient *client,
                                   gboolean       is_split_disk)
{
  client->is_split_disk = is_split_disk;
}

void
eos_test_client_set_uname_machine (EosTestClient *client,
                                   const gchar   *uname_machine)
{
  g_free (client->uname_machine);
  client->uname_machine = g_strdup (uname_machine);
}

void
eos_test_client_set_cpuinfo (EosTestClient *client,
                             const gchar   *cpuinfo)
{
  g_free (client->cpuinfo);
  client->cpuinfo = g_strdup (cpuinfo);
}

void
eos_test_client_set_cmdline (EosTestClient *client,
                             const gchar   *cmdline)
{
  g_free (client->cmdline);
  client->cmdline = g_strdup (cmdline);
}

void
eos_test_client_set_force_follow_checkpoint (EosTestClient *client,
                                             gboolean       force_follow_checkpoint)
{
  client->force_follow_checkpoint = force_follow_checkpoint;
}

gboolean
eos_test_client_run_updater (EosTestClient *client,
                             DownloadSource *order,
                             gsize n_sources,
                             GPtrArray *override_uris,
                             CmdAsyncResult *cmd,
                             GError **error)
{
  if (!run_updater (client->root,
                    order,
                    n_sources,
                    override_uris,
                    client->cpuinfo,
                    client->cmdline,
                    client->vendor,
                    client->product,
                    client->remote_name,
                    TRUE,  /* fatal-warnings */
                    client->uname_machine,
                    client->is_split_disk,
                    client->force_follow_checkpoint,
                    cmd,
                    error))
    return FALSE;

  return TRUE;
}

gboolean
eos_test_client_run_updater_ignore_warnings (EosTestClient   *client,
                                             DownloadSource  *order,
                                             gsize            n_sources,
                                             GPtrArray       *override_uris,
                                             CmdAsyncResult  *cmd,
                                             GError         **error)
{
  if (!run_updater (client->root,
                    order,
                    n_sources,
                    override_uris,
                    client->cpuinfo,
                    client->cmdline,
                    client->vendor,
                    client->product,
                    client->remote_name,
                    FALSE,  /* not fatal-warnings */
                    client->uname_machine,
                    client->is_split_disk,
                    client->force_follow_checkpoint,
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

  if (!eos_updater_remove_recursive (quit_file, NULL, error))
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

  wu->appeared = FALSE;
  g_bus_unwatch_name (wu->watch_id);
  wu->watch_id = 0;

  g_main_context_wakeup (wu->context);
}

static gboolean
real_reap_updater (EosTestClient *client,
                   CmdAsyncResult *cmd,
                   CmdResult *reaped,
                   GError **error)
{
  g_autoptr(GFile) updater_dir = get_updater_dir_for_client (client->root);
  g_autoptr(GFile) quit_file = updater_quit_file (updater_dir);
  WatchUpdater wu = { NULL, TRUE, 0u };

  wu.watch_id = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                  "com.endlessm.Updater",
                                  G_BUS_NAME_WATCHER_FLAGS_NONE,
                                  NULL,
                                  com_endlessm_updater_vanished,
                                  &wu,
                                  NULL);

  if (!eos_updater_remove_recursive (quit_file, NULL, error))
    return FALSE;

  while (wu.appeared)
    g_main_context_iteration (NULL, TRUE);

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
                                                                      "eos-update-server",
                                                                      "eos-update-server",
                                                                      NULL);
  g_autofree gchar *raw_port_file_path = g_file_get_path (port_file);
  g_autofree gchar *raw_config_file_path = g_file_get_path (config_file);
  CmdEnvVar envv[] =
    {
      { "OSTREE_REPO", NULL, repo },
      { "OSTREE_SYSROOT_DEBUG", "mutable-deployments", NULL },
      { "EOS_UPDATER_TEST_UPDATE_SERVER_QUIT_FILE", NULL, quit_file },
      { "FLATPAK_SYSTEM_HELPER_ON_SESSION", "1", NULL },
      { "G_DEBUG", "gc-friendly,fatal-warnings", NULL },
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

      g_test_message ("Bash script %s generated. Run it, make check will continue when port file at %s is generated\n",
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
      CmdResult cmd_result = { 0, };

      /* Check if the process crashed or exited first. */
      if (!reap_async_cmd (cmd, &cmd_result, error) ||
          !cmd_result_ensure_ok (&cmd_result, error))
        {
          cmd_result_clear (&cmd_result);
          return FALSE;
        }

      cmd_result_clear (&cmd_result);

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

  *out_timestamp = g_date_time_new_from_unix_utc ((gint64) ostree_commit_get_timestamp (commit));

  return TRUE;
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
eos_test_run_flatpak_installer (GFile        *client_root,
                                const gchar  *deployment_csum,
                                const gchar  *remote,
                                GError      **error)
{
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;
  g_autofree gchar *eos_flatpak_installer_binary = g_test_build_filename (G_TEST_BUILT,
                                                                          "..",
                                                                          "eos-updater-flatpak-installer",
                                                                          "eos-updater-flatpak-installer",
                                                                          NULL);
  g_autoptr(GFile) updater_dir = g_file_get_child (client_root, "updater");
  g_autoptr(GFile) flatpak_installation_dir = get_flatpak_user_dir_for_updater_dir (updater_dir);
  g_autoptr(GFile) flatpak_upgrade_state_dir = get_flatpak_upgrade_state_dir_for_updater_dir (updater_dir);
  g_autoptr(GFile) flatpak_autoinstall_override_dir = get_flatpak_autoinstall_override_dir (updater_dir);
  g_autoptr(GFile) sysroot = get_sysroot_for_client (client_root);
  g_autofree gchar *sysroot_path = g_file_get_path (sysroot);
  g_autofree gchar *deployment_id = g_strdup_printf ("%s.0", deployment_csum);
  g_autofree gchar *deployment_datadir = g_build_filename (sysroot_path,
                                                           "ostree",
                                                           "deploy",
                                                           remote,
                                                           "deploy",
                                                           deployment_id,
                                                           "usr",
                                                           "share",
                                                           NULL);
  g_autoptr(GFile) datadir = g_file_new_for_path (deployment_datadir);

  CmdArg args[] = {
    { NULL, eos_flatpak_installer_binary },
    { NULL, NULL }
  };
  CmdEnvVar envv[] =
    {
      { "EOS_DISABLE_METRICS", "1", NULL },
      { "EOS_UPDATER_TEST_FLATPAK_INSTALLATION_DIR", NULL, flatpak_installation_dir },
      { "EOS_UPDATER_TEST_UPDATER_FLATPAK_UPGRADE_STATE_DIR", NULL, flatpak_upgrade_state_dir },
      { "EOS_UPDATER_TEST_UPDATER_FLATPAK_AUTOINSTALL_OVERRIDE_DIRS", NULL, flatpak_autoinstall_override_dir },
      { "EOS_UPDATER_TEST_OSTREE_DATADIR", NULL, datadir },
      { "EOS_UPDATER_TEST_OVERRIDE_ARCHITECTURE", arch_override_name, NULL },
      { "FLATPAK_SYSTEM_HELPER_ON_SESSION", "1", NULL },
      { "G_DEBUG", "gc-friendly,fatal-warnings", NULL },
      { NULL, NULL, NULL }
    };

  g_auto(GStrv) argv = build_cmd_args (args);
  g_auto(GStrv) envp = build_cmd_env (envv);

  if (!test_spawn ((const gchar * const *) argv,
                   (const gchar * const *) envp,
                   &cmd,
                   error))
    return FALSE;

  return cmd_result_ensure_ok (&cmd, error);
}

GStrv
eos_test_get_installed_flatpaks (GFile   *updater_dir,
                                 GError **error)
{
  g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;
  g_auto(GStrv) installed_flatpaks_lines = NULL;
  GStrv installed_flatpaks_lines_iter = NULL;
  g_autoptr(GHashTable) installed_flatpaks_set = g_hash_table_new_full (g_str_hash,
                                                                        g_str_equal,
                                                                        g_free,
                                                                        NULL);
  g_auto(GStrv) keys = NULL;

  /* To match output like:
   * Ref
   * org.gnome.Recipes/x86_64/stable
   * org.gnome.Platform/x86_64/3.24
   *
   * We use a regex here, rather than libflatpak, because the test library
   * explicitly does not depend on libflatpak to avoid tautologies.
   *
   * Note that `flatpak list` actually doesn’t output the ‘Ref’ column title
   * when not printing to a terminal.
   */
  g_autoptr(GRegex) flatpak_id_regex = g_regex_new ("(.*?)/.*?/.*?", 0, 0, error);

  if (flatpak_id_regex == NULL)
    return FALSE;

  if (!flatpak_list (updater_dir, &cmd, error))
    return FALSE;

  installed_flatpaks_lines = g_strsplit (cmd.standard_output, "\n", -1);
  for (installed_flatpaks_lines_iter = installed_flatpaks_lines;
       *installed_flatpaks_lines_iter;
       ++installed_flatpaks_lines_iter)
    {
      g_autoptr(GMatchInfo) match_info = NULL;
      g_autofree gchar *matched_flatpak_name = NULL;

      if (!g_regex_match (flatpak_id_regex,
                          *installed_flatpaks_lines_iter,
                          0,
                          &match_info))
        continue;

      matched_flatpak_name = g_match_info_fetch (match_info, 1);

      if (matched_flatpak_name == NULL)
        continue;

      g_hash_table_add (installed_flatpaks_set,
                        g_steal_pointer (&matched_flatpak_name));
    }

  keys = (gchar **) g_hash_table_get_keys_as_array (installed_flatpaks_set, NULL);
  g_hash_table_steal_all (installed_flatpaks_set);
  return g_steal_pointer (&keys);
}

static gboolean
set_flatpak_remote_collection_id (GFile        *updater_dir,
                                  const gchar  *repo_name,
                                  const gchar  *collection_id,
                                  GError      **error)
{
  g_auto(CmdResult) result = CMD_RESULT_CLEARED;
  g_autoptr(GFile) flatpak_installation_dir = get_flatpak_user_dir_for_updater_dir (updater_dir);
  g_autoptr(GFile) flatpak_installation_repo_dir = g_file_get_child (flatpak_installation_dir, "repo");

  if (!ostree_cmd_remote_set_collection_id (flatpak_installation_repo_dir,
                                            repo_name,
                                            collection_id,
                                            &result,
                                            error))
    return FALSE;

  return cmd_result_ensure_ok (&result, error);
}


GFile *
eos_test_get_flatpak_build_dir_for_updater_dir (GFile *updater_dir)
{
  return g_file_get_child (updater_dir, "flatpak");
}

static gchar *
format_flatpak_ref_name_with_branch_override_arch (const gchar *name,
                                                   const gchar *branch)
{
  return g_strdup_printf ("%s/%s/%s", name, arch_override_name, branch);
}

FlatpakExtensionPointInfo *
flatpak_extension_point_info_new (const gchar                *name,
                                  const gchar                *directory,
                                  const gchar * const        *versions,
                                  FlatpakExtensionPointFlags  flags)
{
  FlatpakExtensionPointInfo *extension_info = g_new0 (FlatpakExtensionPointInfo, 1);

  extension_info->name = g_strdup (name);
  extension_info->directory = g_strdup (directory);
  extension_info->versions = g_strdupv ((GStrv) versions);
  extension_info->flags = flags;

  return extension_info;
}

FlatpakExtensionPointInfo *
flatpak_extension_point_info_new_single_version (const gchar                *name,
                                                 const gchar                *directory,
                                                 const gchar                *version,
                                                 FlatpakExtensionPointFlags  flags)
{
  const gchar * const versions[] = {
    version,
    NULL
  };

  return flatpak_extension_point_info_new (name, directory, versions, flags);
}

void
flatpak_extension_point_info_free (FlatpakExtensionPointInfo *extension_info)
{
  g_clear_pointer (&extension_info->name, g_free);
  g_clear_pointer (&extension_info->directory, g_free);
  g_clear_pointer (&extension_info->versions, g_strfreev);

  g_free (extension_info);
}

FlatpakInstallInfo *
flatpak_install_info_new_with_extension_info (FlatpakInstallInfoType  type,
                                              const gchar            *name,
                                              const gchar            *branch,
                                              const gchar            *runtime_name,
                                              const gchar            *runtime_branch,
                                              const gchar            *repo_name,
                                              gboolean                preinstall,
                                              const gchar            *extension_of_ref,
                                              GPtrArray              *extension_infos)
{
  FlatpakInstallInfo *info = g_new0 (FlatpakInstallInfo, 1);

  info->type = type;
  info->name = g_strdup (name);
  info->branch = g_strdup (branch);
  info->runtime_name = g_strdup (runtime_name);
  info->runtime_branch = g_strdup (runtime_branch);
  info->repo_name = g_strdup (repo_name);
  info->preinstall = preinstall;
  info->extension_of_ref = g_strdup (extension_of_ref);
  info->extension_infos = extension_infos != NULL ? g_ptr_array_ref (extension_infos) : NULL;

  return info;
}

FlatpakInstallInfo *
flatpak_install_info_new (FlatpakInstallInfoType  type,
                          const gchar            *name,
                          const gchar            *branch,
                          const gchar            *runtime_name,
                          const gchar            *runtime_branch,
                          const gchar            *repo_name,
                          gboolean                preinstall)
{
  return flatpak_install_info_new_with_extension_info (type,
                                                       name,
                                                       branch,
                                                       runtime_name,
                                                       runtime_branch,
                                                       repo_name,
                                                       preinstall,
                                                       NULL,
                                                       NULL);
}

void
flatpak_install_info_free (FlatpakInstallInfo *info)
{
  g_clear_pointer (&info->name, g_free);
  g_clear_pointer (&info->branch, g_free);
  g_clear_pointer (&info->runtime_name, g_free);
  g_clear_pointer (&info->runtime_branch, g_free);
  g_clear_pointer (&info->repo_name, g_free);
  g_clear_pointer (&info->extension_of_ref, g_free);
  g_clear_pointer (&info->extension_infos, g_ptr_array_unref);

  g_free (info);
}

FlatpakRepoInfo *
flatpak_repo_info_new (const gchar *name,
                       const gchar *collection_id,
                       const gchar *remote_collection_id)
{
  FlatpakRepoInfo *info = g_new0 (FlatpakRepoInfo, 1);

  info->name = g_strdup (name);
  info->collection_id = g_strdup (collection_id);
  info->remote_collection_id = g_strdup (remote_collection_id);

  return info;
}

void
flatpak_repo_info_free (FlatpakRepoInfo *info)
{
  g_clear_pointer (&info->name, g_free);
  g_clear_pointer (&info->collection_id, g_free);
  g_clear_pointer (&info->remote_collection_id, g_free);

  g_free (info);
}

gboolean
eos_test_setup_flatpak_repo (GFile       *updater_dir,
                             GPtrArray   *install_infos,
                             GHashTable  *repository_infos,
                             GFile       *gpg_key,
                             const gchar *keyid,
                             GError     **error)
{
  g_autoptr(GFile) flatpak_build_directory = g_file_get_child (updater_dir,
                                                               "flatpak");
  g_autofree gchar *flatpak_build_directory_path = g_file_get_path (flatpak_build_directory);
  g_autofree gchar *apps_directory_path = g_build_filename (flatpak_build_directory_path,
                                                            "apps",
                                                             NULL);
  g_autofree gchar *runtimes_directory_path = g_build_filename (flatpak_build_directory_path,
                                                                "runtimes",
                                                                NULL);
  g_autoptr(GFile) runtimes_directory = g_file_new_for_path (runtimes_directory_path);
  g_autofree gchar *repos_directory_path = g_build_filename (flatpak_build_directory_path,
                                                             "repos",
                                                             NULL);
  g_autoptr(GHashTable) already_uninstalled_runtimes = NULL;
  g_autoptr(GHashTable) already_installed_runtimes = NULL;
  g_autoptr(GFile) gpg_home_dir = g_file_get_parent (gpg_key);
  guint i = 0;
  GHashTableIter iter;
  gpointer key, value;

  if (!g_file_make_directory_with_parents (flatpak_build_directory, NULL, error))
    return FALSE;

  /* First set up the repos by ostree init'ing them and adding them
   * as flatpak repos */
  g_hash_table_iter_init (&iter, repository_infos);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      FlatpakRepoInfo *repo_info = value;
      const gchar *repo_name = key;
      g_autofree gchar *repo_path = g_build_filename (repos_directory_path,
                                                      repo_name,
                                                      NULL);
      g_autoptr(GFile) repo = g_file_new_for_path (repo_path);
      g_auto(CmdResult) cmd = CMD_RESULT_CLEARED;

      if (!create_directory (repo, error))
        return FALSE;

      if (!repo_config_exists (repo))
        {
          if (!ostree_init (repo,
                            REPO_ARCHIVE_Z2,
                            repo_info->collection_id,
                            &cmd,
                            error))
            return FALSE;
          if (!cmd_result_ensure_ok (&cmd, error))
            return FALSE;
        }

      /* Verify the summary */
      if (!ostree_summary (repo,
                           gpg_home_dir,
                           keyid,
                           &cmd,
                           error))
        return FALSE;

      if (!cmd_result_ensure_ok (&cmd, error))
        return FALSE;

      if (!flatpak_remote_add (updater_dir,
                               repo_name,
                               repo_path,
                               gpg_key,
                               error))
        return FALSE;
    }

  /* Need to keep track of which runtimes we've already installed
   * if we're setting up the same runtime in multiple remotes */
  already_installed_runtimes =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  /* Go through each of the FlatpakInstallInfos in install_infos
   * and build the flatpak in the right place. It is assumed that the
   * provided install_infos are in the correct dependency order. */
  for (i = 0; i < install_infos->len; ++i)
    {
      FlatpakInstallInfo *install_info = g_ptr_array_index (install_infos, i);
      FlatpakRepoInfo *repo_info = g_hash_table_lookup (repository_infos, install_info->repo_name);
      g_autofree gchar *repo_directory_path = g_build_filename (repos_directory_path,
                                                                install_info->repo_name,
                                                                NULL);
      g_autofree gchar *formatted_ref_name =
        format_flatpak_ref_name_with_branch_override_arch (install_info->name,
                                                           install_info->branch);

      switch (install_info->type)
        {
          case FLATPAK_INSTALL_INFO_TYPE_RUNTIME:
            {
              g_autofree gchar *runtime_dir = g_build_filename (runtimes_directory_path,
                                                                install_info->repo_name,
                                                                install_info->name,
                                                                install_info->branch,
                                                                NULL);
              g_autoptr(GFile) runtime_directory = g_file_new_for_path (runtime_dir);
              if (!flatpak_populate_runtime (updater_dir,
                                             runtime_directory,
                                             repo_directory_path,
                                             install_info->name,
                                             formatted_ref_name,
                                             install_info->branch,
                                             install_info->extension_infos,
                                             repo_info->collection_id,
                                             gpg_home_dir,
                                             keyid,
                                             error))
                return FALSE;

              if (g_hash_table_insert (already_installed_runtimes,
                                       g_strdup (formatted_ref_name),
                                       NULL))
                {
                  /* Note that runtimes need to be installed in order to
                   * build the corresponding flatpaks. We will uninstall them
                   * later if they were not marked for preinstallation */
                  if (!flatpak_install (updater_dir,
                                        install_info->repo_name,
                                        formatted_ref_name,
                                        error))
                    return FALSE;
                }
            }
            break;
          case FLATPAK_INSTALL_INFO_TYPE_EXTENSION:
            {
              g_autofree gchar *runtime_dir = g_build_filename (runtimes_directory_path,
                                                                install_info->repo_name,
                                                                install_info->name,
                                                                install_info->branch,
                                                                NULL);
              g_autoptr(GFile) runtime_directory = g_file_new_for_path (runtime_dir);
              if (!flatpak_populate_extension (updater_dir,
                                               runtime_directory,
                                               repo_directory_path,
                                               install_info->name,
                                               formatted_ref_name,
                                               install_info->branch,
                                               install_info->extension_of_ref,
                                               repo_info->collection_id,
                                               gpg_home_dir,
                                               keyid,
                                               error))
                return FALSE;
            }
            break;
          case FLATPAK_INSTALL_INFO_TYPE_APP:
            {
              g_autofree gchar *app_dir = g_build_filename (apps_directory_path,
                                                            install_info->repo_name,
                                                            install_info->name,
                                                            install_info->branch,
                                                            NULL);
              g_autoptr(GFile) app_path = g_file_new_for_path (app_dir);
              g_autofree gchar *runtime_formatted_ref_name =
                format_flatpak_ref_name_with_branch_override_arch (install_info->runtime_name,
                                                                   install_info->runtime_branch);

              if (!flatpak_populate_app (updater_dir,
                                         app_path,
                                         install_info->name,
                                         runtime_formatted_ref_name,
                                         install_info->branch,
                                         install_info->extension_infos,
                                         repo_directory_path,
                                         repo_info->collection_id,
                                         gpg_home_dir,
                                         keyid,
                                         error))
                return FALSE;
            }
            break;
          default:
            g_assert_not_reached ();
            return FALSE;
        }
    }

  /* Somewhat of a niche thing: Some tests might built the same runtime
   * in two different locations. In that case, we don't want to uninstall
   * it twice, so keep track of what we uninstalled. */
  already_uninstalled_runtimes =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  /* Now that we have finished setting everything up, go through
   * the list of flatpaks that were to be preinstalled. If a runtime
   * was not marked for preinstallation, then uninstall it */
  for (i = 0; i < install_infos->len; ++i)
    {
      FlatpakInstallInfo *install_info = g_ptr_array_index (install_infos, i);
      g_autofree gchar *formatted_ref_name =
        format_flatpak_ref_name_with_branch_override_arch (install_info->name,
                                                           install_info->branch);

      switch (install_info->type)
        {
          case FLATPAK_INSTALL_INFO_TYPE_RUNTIME:
            {
              /* If we weren't going to preinstall the runtime,
               * uninstall it now */
              if (!install_info->preinstall &&
                  g_hash_table_insert (already_uninstalled_runtimes,
                                       g_strdup (formatted_ref_name),
                                       NULL))
                {
                  if (!flatpak_uninstall (updater_dir,
                                          formatted_ref_name,
                                          error))
                    return FALSE;
                }
            }
            break;
          case FLATPAK_INSTALL_INFO_TYPE_APP:
          case FLATPAK_INSTALL_INFO_TYPE_EXTENSION:
            break;
          default:
            g_assert_not_reached ();
            return FALSE;
        }

      if (install_info->preinstall)
        {
          if (!flatpak_install (updater_dir,
                                install_info->repo_name,
                                formatted_ref_name,
                                error))
            return FALSE;
        }
    }

  /* Now that we have finished preinstalling all the flatpaks,
   * set the collection-id on all remote configs in the installation
   * directory */
  g_hash_table_iter_init (&iter, repository_infos);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      FlatpakRepoInfo *repo_info = value;

      if (repo_info->remote_collection_id == NULL)
        continue;

      if (!set_flatpak_remote_collection_id (updater_dir,
                                             key,
                                             repo_info->remote_collection_id,
                                             error))
        return FALSE;
    }

  return TRUE;
}

gboolean
eos_test_setup_flatpak_repo_with_preinstalled_apps_simple (GFile        *updater_dir,
                                                           const gchar  *branch,
                                                           const gchar  *repo_name,
                                                           const gchar  *repo_collection_id,
                                                           const gchar  *remote_config_collection_id,
                                                           const gchar **flatpak_names,
                                                           const gchar **preinstall_flatpak_names,
                                                           GFile        *gpg_key,
                                                           const gchar  *keyid,
                                                           GError      **error)
{
  g_autoptr(GHashTable) repo_infos = g_hash_table_new_full (g_str_hash,
                                                            g_str_equal,
                                                            g_free,
                                                            (GDestroyNotify) flatpak_repo_info_free);
  g_autoptr(GPtrArray) flatpak_install_infos = g_ptr_array_new_full (g_strv_length ((GStrv) flatpak_names) + 1,
                                                                     (GDestroyNotify) flatpak_install_info_free);
  const gchar **flatpak_names_iter = NULL;

  g_ptr_array_add (flatpak_install_infos,
                   flatpak_install_info_new (FLATPAK_INSTALL_INFO_TYPE_RUNTIME,
                                             "org.test.Runtime",
                                             branch,
                                             NULL,
                                             NULL,
                                             repo_name,
                                             TRUE));

  for (flatpak_names_iter = flatpak_names;
       *flatpak_names_iter != NULL;
       ++flatpak_names_iter)
    {
      g_ptr_array_add (flatpak_install_infos,
                       flatpak_install_info_new (FLATPAK_INSTALL_INFO_TYPE_APP,
                                                 *flatpak_names_iter,
                                                 branch,
                                                 "org.test.Runtime",
                                                 branch,
                                                 repo_name,
                                                 g_strv_contains (preinstall_flatpak_names,
                                                                  *flatpak_names_iter)));
    }

  g_hash_table_insert (repo_infos,
                       g_strdup (repo_name),
                       flatpak_repo_info_new (repo_name,
                                              repo_collection_id,
                                              remote_config_collection_id));

  return eos_test_setup_flatpak_repo (updater_dir,
                                      flatpak_install_infos,
                                      repo_infos,
                                      gpg_key,
                                      keyid,
                                      error);
}

gboolean
eos_test_setup_flatpak_repo_simple (GFile        *updater_dir,
                                    const gchar  *branch,
                                    const gchar  *repo_name,
                                    const gchar  *repo_collection_id,
                                    const gchar  *remote_config_collection_id,
                                    const gchar **flatpak_names,
                                    GFile        *gpg_key,
                                    const gchar  *keyid,
                                    GError      **error)
{
  const gchar *empty_strv[] = { NULL };

  return eos_test_setup_flatpak_repo_with_preinstalled_apps_simple (updater_dir,
                                                                    branch,
                                                                    repo_name,
                                                                    repo_collection_id,
                                                                    remote_config_collection_id,
                                                                    flatpak_names,
                                                                    empty_strv,
                                                                    gpg_key,
                                                                    keyid,
                                                                    error);
}

gboolean
eos_test_client_run_update_server (EosTestClient *client,
                                   CmdAsyncResult *cmd,
                                   guint16 *out_port,
                                   GError **error)
{
  g_autoptr(GFile) update_server_dir = get_update_server_dir (client->root);
  g_autoptr(GFile) sysroot = NULL;
  g_autoptr(GFile) repo = NULL;
  g_autoptr(GFile) quit_file = NULL;
  g_autoptr(GFile) port_file = NULL;
  g_autoptr(GFile) config_file = NULL;
  g_autoptr(GDateTime) timestamp = NULL;

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
                          out_port,
                          cmd,
                          error))
    return FALSE;

  if (!get_head_commit_timestamp (sysroot, &timestamp, error))
    return FALSE;

  return TRUE;
}

gboolean
eos_test_client_remove_update_server_quit_file (EosTestClient *client,
                                                GError **error)
{
  g_autoptr(GFile) update_server_dir = get_update_server_dir (client->root);
  g_autoptr(GFile) quit_file = get_update_server_quit_file (update_server_dir);

  return eos_updater_remove_recursive (quit_file, NULL, error);
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
                            guint commit_number,
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
      g_autofree gchar *commit_filename = get_commit_filename (commit_number);
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
eos_test_client_get_deployments (EosTestClient *client,
                                 const gchar *osname,
                                 gchar ***out_ids,
                                 GError **error)
{
  g_autoptr(GFile) sysroot = get_sysroot_for_client (client->root);

  g_assert_nonnull (out_ids);

  return get_deploy_ids (sysroot, osname, out_ids, error);
}

gboolean
eos_test_client_prepare_volume (EosTestClient *client,
                                GFile *volume_path,
                                GError **error)
{
  g_autofree gchar *eos_updater_prepare_volume_binary = g_test_build_filename (G_TEST_DIST,
                                                                               "..",
                                                                               "eos-updater-prepare-volume",
                                                                               "eos-updater-prepare-volume",
                                                                               NULL);
  g_autofree gchar *libeos_updater_util_path = g_test_build_filename (G_TEST_BUILT,
                                                                      "..",
                                                                      "libeos-updater-util",
                                                                      NULL);
  const gchar *ld_library_path = g_getenv ("LD_LIBRARY_PATH");
  g_autofree gchar *new_ld_library_path = NULL;
  if (ld_library_path == NULL || *ld_library_path == '\0')
    new_ld_library_path = g_strdup (libeos_updater_util_path);
  else
    new_ld_library_path = g_strdup_printf ("%s:%s", libeos_updater_util_path, ld_library_path);

  g_autoptr(GFile) sysroot = get_sysroot_for_client (client->root);
  CmdEnvVar envv[] =
    {
      { "EOS_UPDATER_TEST_UPDATER_DEPLOYMENT_FALLBACK", "yes", NULL },
      { "OSTREE_SYSROOT", NULL, sysroot },
      { "OSTREE_SYSROOT_DEBUG", "mutable-deployments", NULL },
      { "GI_TYPELIB_PATH", libeos_updater_util_path, NULL },
      { "LD_LIBRARY_PATH", new_ld_library_path, NULL },
      { "FLATPAK_SYSTEM_HELPER_ON_SESSION", "1", NULL },
      /* Flatpak uses $XDG_CACHE_HOME and we need to set it explicitly since
       * we're using G_TEST_OPTION_ISOLATE_DIRS */
      { "XDG_CACHE_HOME", g_get_user_cache_dir (), NULL },
      { "G_DEBUG", "gc-friendly,fatal-warnings", NULL },
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
      g_test_message ("Bash script %s generated. Run it, make check will continue when %s is deleted\n",
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

GFile *
eos_test_client_get_repo (EosTestClient *client)
{
  g_autoptr(GFile) sysroot = get_sysroot_for_client (client->root);

  return get_repo_for_sysroot (sysroot);
}

GFile *
eos_test_client_get_sysroot (EosTestClient *client)
{
  return get_sysroot_for_client (client->root);
}

const gchar *
eos_test_client_get_big_file_path (void)
{
  return "/for-all-commits/bigfile";
}

G_DEFINE_TYPE (EosTestAutoupdater, eos_test_autoupdater, G_TYPE_OBJECT)

static void
eos_test_autoupdater_dispose (GObject *object)
{
  EosTestAutoupdater *self = EOS_TEST_AUTOUPDATER (object);

  g_clear_object (&self->root);

  G_OBJECT_CLASS (eos_test_autoupdater_parent_class)->dispose (object);
}

static void
eos_test_autoupdater_finalize (GObject *object)
{
  EosTestAutoupdater *self = EOS_TEST_AUTOUPDATER (object);

  cmd_result_free (self->cmd);

  G_OBJECT_CLASS (eos_test_autoupdater_parent_class)->finalize (object);
}

static void
eos_test_autoupdater_class_init (EosTestAutoupdaterClass *self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (self_class);

  object_class->dispose = eos_test_autoupdater_dispose;
  object_class->finalize = eos_test_autoupdater_finalize;
}

static void
eos_test_autoupdater_init (EosTestAutoupdater *self)
{
  /* nothing here */
}

static GKeyFile *
get_autoupdater_config (UpdateStep step,
                        guint update_interval_in_days)
{
  g_autoptr(GKeyFile) config = g_key_file_new ();

  g_assert (update_interval_in_days <= G_MAXINT);

  g_key_file_set_integer (config, "Automatic Updates", "LastAutomaticStep", step);
  g_key_file_set_integer (config, "Automatic Updates", "IntervalDays", (gint) update_interval_in_days);
  g_key_file_set_integer (config, "Automatic Updates", "RandomizedDelayDays", 0);

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
                                                                    "eos-autoupdater",
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
      { "FLATPAK_SYSTEM_HELPER_ON_SESSION", "1", NULL },
      { "G_DEBUG", "gc-friendly,fatal-warnings", NULL },
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
eos_test_autoupdater_new (GFile       *autoupdater_root,
                          UpdateStep   final_auto_step,
                          guint        interval_in_days,
                          gboolean     force_update,
                          GError     **error)
{
  g_autoptr(GKeyFile) autoupdater_config = NULL;
  g_autoptr(EosTestAutoupdater) autoupdater = NULL;
  g_autoptr(CmdResult) cmd = NULL;

  autoupdater_config = get_autoupdater_config (final_auto_step,
                                               interval_in_days);
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

/**
 * eos_test_has_ostree_boot_id:
 *
 * Check whether the `/proc/sys/kernel/random/boot_id` file is available, which
 * is needed by #OstreeRepo.
 *
 * Returns: %TRUE if it exists, %FALSE otherwise
 */
gboolean
eos_test_has_ostree_boot_id (void)
{
  g_autoptr(GFile) boot_id_file = NULL;

  boot_id_file = g_file_new_for_path ("/proc/sys/kernel/random/boot_id");
  return g_file_query_exists (boot_id_file, NULL);
}

/**
 * eos_test_skip_chroot:
 *
 * Check whether the test is running in a chroot and, if so, skip it using
 * g_test_skip(). This avoids issues when running the tests in an ARM chroot.
 * See commit https://github.com/endlessm/eos-updater/commit/5032d0a879bb5b22.
 *
 * Returns: %TRUE if the test has been skipped and should be returned from
 *    immediately; %FALSE to continue and run the test
 */
gboolean
eos_test_skip_chroot (void)
{
  /* We could get OSTree working by setting OSTREE_BOOTID, but shortly
   * afterwards we hit unsupported syscalls in qemu-user when running in an
   * ARM chroot (for example), so just bail. */
  if (!eos_test_has_ostree_boot_id ())
    {
      g_test_skip ("OSTree will not work without a boot ID");
      return TRUE;
    }

  return FALSE;
}

/**
 * eos_test_add_metadata_for_commit:
 *
 * Adds the provided metadata (@key and @value) for the given @commit_number
 * to the passed @commit_metadata hash table. If the latter is %NULL, it will
 * create it with the right types and assign it to @commit_metadata.
 */
void
eos_test_add_metadata_for_commit (GHashTable **commit_metadata,
                                  guint commit_number,
                                  const gchar *key,
                                  GVariant *value)
{
  GHashTable *metadata = NULL;

  if (*commit_metadata == NULL)
    *commit_metadata = g_hash_table_new_full (g_direct_hash,
                                              g_direct_equal,
                                              NULL,
                                              (GDestroyNotify) g_hash_table_unref);

  metadata = g_hash_table_lookup (*commit_metadata,
                                  GUINT_TO_POINTER (commit_number));
  if (metadata == NULL)
    {
      metadata = g_hash_table_new_full (g_str_hash,
                                        g_str_equal,
                                        g_free,
                                        (GDestroyNotify) g_variant_unref);
      g_hash_table_insert (*commit_metadata,
                           GUINT_TO_POINTER (commit_number),
                           metadata);
    }

  g_hash_table_insert (metadata, g_strdup (key), g_variant_ref_sink (value));
}
