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

#pragma once

#include <gio/gio.h>
#include <libeos-updater-util/refcounted.h>
#include <ostree.h>

#include "spawn-utils.h"

G_BEGIN_DECLS

typedef struct
{
  GTestDBus *dbus;
  GFile *tmpdir;
  GFile *gpg_home;
} EosUpdaterFixture;

void eos_updater_fixture_setup (EosUpdaterFixture *fixture,
                                gconstpointer user_data);
void eos_updater_fixture_setup_full (EosUpdaterFixture *fixture,
                                     const gchar       *top_srcdir);

void eos_updater_fixture_teardown (EosUpdaterFixture *fixture,
                                   gconstpointer user_data);

#define eos_test_add(testpath, tdata, ftest) g_test_add (testpath, EosUpdaterFixture, tdata, eos_updater_fixture_setup, ftest, eos_updater_fixture_teardown)

extern const gchar *const default_vendor;
extern const gchar *const default_product;
extern const gchar *const default_collection_id;
extern const gchar *const default_ref;
extern const OstreeCollectionRef *default_collection_ref;
extern const gchar *const default_ostree_path;
extern const gchar *const default_remote_name;

typedef struct {
  guint sequence_number;
  guint parent;
  OstreeCollectionRef *collection_ref;
} EosTestUpdaterCommitInfo;

EosTestUpdaterCommitInfo * eos_test_updater_commit_info_new (guint                      sequence_number,
                                                             guint                      parent,
                                                             const OstreeCollectionRef *collection_ref);

void eos_test_updater_commit_info_free (EosTestUpdaterCommitInfo *info);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (EosTestUpdaterCommitInfo, eos_test_updater_commit_info_free)

typedef gboolean (*EosTestUpdaterCommitGraphWalkFunc) (EosTestUpdaterCommitInfo  *commit_info,
                                                       EosTestUpdaterCommitInfo  *parent_commit_info,
                                                       gpointer                   user_data,
                                                       GError                   **error);
gboolean eos_test_updater_commit_graph_walk (GHashTable                         *commit_graph,
                                             EosTestUpdaterCommitGraphWalkFunc   walk_func,
                                             gpointer                            walk_func_data,
                                             GError                            **error);

GHashTable * eos_test_updater_commit_graph_new_from_leaf_nodes (GHashTable *leaf_nodes);
void eos_test_updater_insert_commit_steal_info (GHashTable               *commit_graph,
                                                EosTestUpdaterCommitInfo *commit_info);
void eos_test_updater_populate_commit_graph_from_leaf_nodes (GHashTable *commit_graph,
                                                             GHashTable *leaf_nodes);

#define EOS_TEST_TYPE_SUBSERVER eos_test_subserver_get_type ()
G_DECLARE_FINAL_TYPE (EosTestSubserver,
                      eos_test_subserver,
                      EOS_TEST,
                      SUBSERVER,
                      GObject)

struct _EosTestSubserver
{
  GObject parent_instance;

  gchar *collection_id;
  gchar *keyid;
  gchar *ostree_path;

  /*
   * Defines the "commit graph" of a given repo. OSTree repos for
   * our system updater can have non-linear histories. For instance, we
   * might be making commits on a given refspec and then either mark it
   * as 'eol-rebase' or 'checkpoint' indicating that either a new refspec
   * should be followed either immediately or upon booting into that commit.
   *
   * With 'checkpoint' it is possible that history might diverge. For
   * instance, we might make a checkpoint at the end of a refspec
   * but we find that some systems are unable to upgrade on the new
   * refspec after rebooting (due to bad system configuration or
   * bugs in the updater that were meant to support the new commits). In
   * that case, we might want to create another commit on the booted
   * refspec to fix the updater or system configuration so that systems
   * can successfully upgrade. Thus the histories can diverge.
   *
   * Thus, in our tests, we need a data structure that can represent this
   * nonlinearity (eg, a graph, just like the way git works). This graph
   * is implemented as a reverse adjacency list with hash-tables. There is
   * a hash table with a surjective mapping of commits to parents (eg, one
   * commit may have many parents). A node is a root node if it has itself
   * as its parent (typically, this is node 0). Creating a parent-child
   * relation with this structure is fairly convenient, as we only need to
   * insert a single value into the hash table. However, expanding children
   * is O(V). There is also a hash table of commit-ids to an
   * #EosTestUpdateCommitInfo struct with a little more info about that commit
   * (for instance, which collection-ref it is on).
   */
  GHashTable *commit_graph;  /* (element-type guint EosTestUpdaterCommitInfo) */

  /* Which commits we already have (mapping commit ids to checksums) */
  GHashTable *commits_in_repo;  /* (element-type guint gchar) */

  /* This is a hashtable of string vectors - the key is the commit
   * number to insert the directories on and the value is a vector of
   * directories. Note that directories are not created recursively, but the
   * value for each key is traversed in order, so you will need to create
   * any directory parents yourself by specifying them first. */
  GHashTable *additional_directories_for_commit;  /* (element-type guint GStrv) */

  /* Same thing, but for files. Note that directories are not created. Values
   * are a pointer array of SimpleFile instances. */
  GHashTable *additional_files_for_commit;  /* (element-type guint GPtrArray<SimpleFile>) */

  /* Mapping from commit numbers to hashtables of metadata string
   * key-value pairs */
  GHashTable *additional_metadata_for_commit;  /* (element-type guint GHashTable<utf8, utf8>) */

  GFile *repo;
  GFile *tree;
  gchar *url;
  GFile *gpg_home;
};

static inline GHashTable *
eos_test_subserver_ref_to_commit_new (void)
{
  return g_hash_table_new_full (ostree_collection_ref_hash, ostree_collection_ref_equal,
                                (GDestroyNotify) ostree_collection_ref_free, NULL);
}

EosTestSubserver *eos_test_subserver_new (const gchar *collection_id,
                                          GFile *gpg_home,
                                          const gchar *keyid,
                                          const gchar *ostree_path,
                                          GHashTable *commit_graph,
                                          GHashTable *additional_directories_for_commit,
                                          GHashTable *additional_files_for_commit,
                                          GHashTable *additional_metadata_for_commit);

void eos_test_subserver_populate_commit_graph_from_leaf_nodes (EosTestSubserver *subserver,
                                                               GHashTable       *leaf_nodes);
gboolean eos_test_subserver_update (EosTestSubserver *subserver,
                                    GError **error);

#define EOS_TEST_TYPE_SERVER eos_test_server_get_type ()
G_DECLARE_FINAL_TYPE (EosTestServer,
                      eos_test_server,
                      EOS_TEST,
                      SERVER,
                      GObject)

struct _EosTestServer
{
  GObject parent_instance;

  GFile *root;
  gchar *url;
  GPtrArray *subservers;
};

EosTestServer *eos_test_server_new (GFile *server_root,
                                    GPtrArray *subservers,
                                    GError **error);

EosTestServer *eos_test_server_new_quick (GFile *server_root,
                                          const gchar *vendor,
                                          const gchar *product,
                                          const OstreeCollectionRef *collection_ref,
                                          guint commit_number,
                                          GFile *gpg_home,
                                          const gchar *keyid,
                                          const gchar *ostree_path,
                                          GHashTable *additional_directories_for_commit,
                                          GHashTable *additional_files_for_commit,
                                          GHashTable *additional_metadata_for_commit,
                                          GError **error);

#define EOS_TEST_TYPE_CLIENT eos_test_client_get_type ()
G_DECLARE_FINAL_TYPE (EosTestClient,
                      eos_test_client,
                      EOS_TEST,
                      CLIENT,
                      GObject)

struct _EosTestClient
{
  GObject parent_instance;

  GFile *root;
  gchar *vendor;
  gchar *product;
  gchar *remote_name;
  gchar *ostree_path;
};

typedef enum
  {
    DOWNLOAD_MAIN,
    DOWNLOAD_LAN,
    DOWNLOAD_VOLUME
  } DownloadSource;

EosTestClient *eos_test_client_new (GFile *client_root,
                                    const gchar *remote_name,
                                    EosTestSubserver *subserver,
                                    const OstreeCollectionRef *collection_ref,
                                    const gchar *vendor,
                                    const gchar *product,
                                    GError **error);

gboolean eos_test_client_run_updater (EosTestClient *client,
                                      DownloadSource *order,
                                      gsize n_sources,
                                      GPtrArray *override_uris,
                                      CmdAsyncResult *cmd,
                                      GError **error);
gboolean eos_test_client_run_updater_ignore_warnings (EosTestClient   *client,
                                                      DownloadSource  *order,
                                                      gsize            n_sources,
                                                      GPtrArray       *override_uris,
                                                      CmdAsyncResult  *cmd,
                                                      GError         **error);

gboolean eos_test_client_reap_updater (EosTestClient *client,
                                       CmdAsyncResult *cmd,
                                       CmdResult *reaped,
                                       GError **error);

gboolean eos_test_client_run_update_server (EosTestClient *client,
                                            CmdAsyncResult *cmd,
                                            guint16 *out_port,
                                            GError **error);


gboolean eos_test_client_remove_update_server_quit_file (EosTestClient *client,
                                                         GError **error);

gboolean eos_test_client_wait_for_update_server (EosTestClient *client,
                                                 CmdAsyncResult *cmd,
                                                 CmdResult *reaped,
                                                 GError **error);

gboolean eos_test_client_reap_update_server (EosTestClient *client,
                                             CmdAsyncResult *cmd,
                                             CmdResult *reaped,
                                             GError **error);

gboolean eos_test_client_has_commit (EosTestClient *client,
                                     const gchar *osname,
                                     guint commit_number,
                                     gboolean *out_has_commit,
                                     GError **error);

gboolean eos_test_client_get_deployments (EosTestClient *client,
                                          const gchar *osname,
                                          gchar ***out_ids,
                                          GError **error);

gboolean eos_test_client_prepare_volume (EosTestClient *client,
                                         GFile *volume_path,
                                         GError **error);

GFile *eos_test_client_get_repo (EosTestClient *client);

GFile *eos_test_client_get_sysroot (EosTestClient *client);

const gchar *eos_test_client_get_big_file_path (void);

typedef enum _UpdateStep {
  UPDATE_STEP_NONE,
  UPDATE_STEP_POLL,
  UPDATE_STEP_FETCH,
  UPDATE_STEP_APPLY
} UpdateStep;

#define EOS_TEST_TYPE_AUTOUPDATER eos_test_autoupdater_get_type ()
G_DECLARE_FINAL_TYPE (EosTestAutoupdater,
                      eos_test_autoupdater,
                      EOS_TEST,
                      AUTOUPDATER,
                      GObject)

struct _EosTestAutoupdater
{
  GObject parent_instance;

  GFile *root;
  CmdResult *cmd;
};

EosTestAutoupdater *eos_test_autoupdater_new (GFile       *autoupdater_root,
                                              UpdateStep   final_auto_step,
                                              guint        interval_in_days,
                                              gboolean     force_update,
                                              GError     **error);

gboolean eos_test_has_ostree_boot_id (void);
gboolean eos_test_skip_chroot (void);

typedef struct _SimpleFile SimpleFile;

SimpleFile * simple_file_new_steal (gchar *rel_path, gchar *contents);
void         simple_file_free (gpointer file_ptr);

gboolean eos_test_setup_flatpak_repo (GFile       *updater_dir,
                                      GPtrArray   *install_infos,
                                      GHashTable  *repository_infos,
                                      GFile       *gpg_key,
                                      const gchar *keyid,
                                      GError     **error);

/* The eos_test_setup_flatpak_repo_*simple family of functions here
 * will set up a flatpak repo containing flatpaks with the given flatpak_names
 * all linked to the same runtime (org.test.Runtime), with the same branch
 * in the same repo having the same collection_id. */
gboolean eos_test_setup_flatpak_repo_simple (GFile        *updater_path,
                                             const gchar  *branch,
                                             const gchar  *repo_name,
                                             const gchar  *repo_collection_id,
                                             const gchar  *remote_config_collection_id,
                                             const gchar **flatpak_names,
                                             GFile        *gpg_key,
                                             const gchar  *keyid,
                                             GError      **error);

gboolean eos_test_setup_flatpak_repo_with_preinstalled_apps_simple (GFile        *updater_path,
                                                                    const gchar  *branch,
                                                                    const gchar  *repo_name,
                                                                    const gchar  *repo_collection_id,
                                                                    const gchar  *remote_config_collection_id,
                                                                    const gchar **flatpak_names,
                                                                    const gchar **preinstalled_flatpak_names,
                                                                    GFile        *gpg_key,
                                                                    const gchar  *keyid,
                                                                    GError      **error);

typedef enum {
  FLATPAK_INSTALL_INFO_TYPE_RUNTIME,
  FLATPAK_INSTALL_INFO_TYPE_APP,
  FLATPAK_INSTALL_INFO_TYPE_EXTENSION,
} FlatpakInstallInfoType;

typedef enum _FlatpakExtensionPointFlags {
  FLATPAK_EXTENSION_POINT_NONE = 0,
  FLATPAK_EXTENSION_POINT_NO_AUTODOWNLOAD = 1 << 0,
  FLATPAK_EXTENSION_POINT_LOCALE_SUBSET = 1 << 1,
  FLATPAK_EXTENSION_POINT_AUTODELETE = 1 << 2,
} FlatpakExtensionPointFlags;

typedef struct _FlatpakExtensionPointInfo {
  gchar                      *name;
  gchar                      *directory;
  GStrv                       versions;
  FlatpakExtensionPointFlags  flags;
} FlatpakExtensionPointInfo;

FlatpakExtensionPointInfo * flatpak_extension_point_info_new (const gchar                *name,
                                                              const gchar                *directory,
                                                              const gchar * const        *versions,
                                                              FlatpakExtensionPointFlags  flags);
FlatpakExtensionPointInfo * flatpak_extension_point_info_new_single_version (const gchar                *name,
                                                                             const gchar                *directory,
                                                                             const gchar                *version,
                                                                             FlatpakExtensionPointFlags  flags);
void flatpak_extension_point_info_free (FlatpakExtensionPointInfo *extension_info);

typedef struct {
  FlatpakInstallInfoType  type;
  gchar                  *name;
  gchar                  *branch;
  gchar                  *runtime_name;
  gchar                  *runtime_branch;
  gchar                  *repo_name;
  gboolean                preinstall;
  gchar                  *extension_of_ref;
  GPtrArray              *extension_infos;
} FlatpakInstallInfo;

FlatpakInstallInfo * flatpak_install_info_new (FlatpakInstallInfoType  type,
                                               const gchar            *name,
                                               const gchar            *branch,
                                               const gchar            *runtime_name,
                                               const gchar            *runtime_branch,
                                               const gchar            *repo_name,
                                               gboolean                preinstall);

FlatpakInstallInfo * flatpak_install_info_new_with_extension_info (FlatpakInstallInfoType  type,
                                                                   const gchar            *name,
                                                                   const gchar            *branch,
                                                                   const gchar            *runtime_name,
                                                                   const gchar            *runtime_branch,
                                                                   const gchar            *repo_name,
                                                                   gboolean                preinstall,
                                                                   const gchar            *extension_of_ref,
                                                                   GPtrArray              *extension_infos);

void flatpak_install_info_free (FlatpakInstallInfo *info);

typedef struct {
  gchar *name;
  gchar *collection_id;
  gchar *remote_collection_id;
} FlatpakRepoInfo;

FlatpakRepoInfo * flatpak_repo_info_new (const gchar *name,
                                         const gchar *collection_id,
                                         const gchar *remote_collection_id);
void flatpak_repo_info_free (FlatpakRepoInfo *info);

gboolean eos_test_run_flatpak_installer (GFile        *client_root,
                                         const gchar  *deployment_csum,
                                         const gchar  *remote,
                                         GError      **error);

GStrv eos_test_get_installed_flatpaks (GFile   *updater_path,
                                       GError **error);

GFile * get_flatpak_user_dir_for_updater_dir (GFile *updater_dir);
GFile * get_flatpak_autoinstall_override_dir (GFile *client_root);
GFile * get_flatpak_upgrade_state_dir_for_updater_dir (GFile *updater_dir);

GFile * eos_test_get_flatpak_build_dir_for_updater_dir (GFile *updater_dir);

void eos_test_add_metadata_for_commit (GHashTable **commit_metadata,
                                       guint commit_number,
                                       const gchar *key,
                                       const gchar *value);

G_END_DECLS
