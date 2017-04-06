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

#include "eos-prepare-usb-update.h"

#include <libeos-updater-util/extensions.h>
#include <libeos-updater-util/util.h>

#include <libsoup/soup.h>
/**
 * SECTION:prepare-update
 * @title: Prepare update volume
 * @short_description: Functions for preparing update volume
 * @include: eos-prepare-usb-update.h
 *
 * The following functions prepares the update on a given volume.
 */

static gboolean
strv_contains (gchar **strv,
               const gchar *str)
{
  return g_strv_contains ((const gchar *const *)strv, str);
}

static gchar *
repo_get_raw_path (OstreeRepo *repo)
{
  return g_file_get_path (ostree_repo_get_path (repo));
}

#define EOS_TYPE_REFSPEC eos_refspec_get_type ()
EOS_DECLARE_REFCOUNTED (EosRefspec,
                        eos_refspec,
                        EOS,
                        REFSPEC)

struct _EosRefspec
{
  GObject parent_instance;

  const gchar *str;
  const gchar *remote;
  const gchar *ref;
};

static void
eos_refspec_finalize_impl (EosRefspec *refspec)
{
  g_clear_pointer (&refspec->str, g_free);
  g_clear_pointer (&refspec->remote, g_free);
  g_clear_pointer (&refspec->ref, g_free);
}

EOS_DEFINE_REFCOUNTED (EOS_REFSPEC,
                       EosRefspec,
                       eos_refspec,
                       NULL,
                       eos_refspec_finalize_impl)

static EosRefspec *
eos_refspec_new (const gchar *refspec_str,
                 GError **error)
{
  g_autoptr(EosRefspec) refspec = NULL;
  g_autofree gchar *remote = NULL;
  g_autofree gchar *ref = NULL;

  if (!ostree_parse_refspec (refspec_str, &remote, &ref, error))
    return NULL;

  refspec = g_object_new (EOS_TYPE_REFSPEC, NULL);
  refspec->str = g_strdup (refspec_str);
  refspec->remote = g_steal_pointer (&remote);
  refspec->ref = g_steal_pointer (&ref);

  return g_steal_pointer (&refspec);
}

static gboolean
ensure_coherency (OstreeRepo *repo,
                  EosRefspec *refspec,
                  const gchar *commit_id,
                  GError **error)
{
  g_auto(GStrv) remotes = NULL;
  g_auto(GStrv) refs = NULL;
  g_autofree gchar *ref_commit_id = NULL;

  remotes = ostree_repo_remote_list (repo, NULL);
  if (!strv_contains (remotes, refspec->remote))
    {
      g_autofree gchar *raw_path = repo_get_raw_path (repo);

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Repository at %s has no remote %s",
                   raw_path,
                   refspec->remote);
      return FALSE;
    }

  if (!ostree_repo_get_remote_list_option (repo,
                                           refspec->remote,
                                           "branches",
                                           &refs,
                                           error))
    return FALSE;

  if (refs == NULL || !strv_contains (refs, refspec->ref))
    {
      g_autofree gchar *raw_path = repo_get_raw_path (repo);

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Remote %s in repository at %s has no ref %s",
                   refspec->remote,
                   raw_path,
                   refspec->ref);
      return FALSE;
    }

  if (!ostree_repo_resolve_rev (repo,
                                refspec->str,
                                FALSE,
                                &ref_commit_id,
                                error))
    return FALSE;

  while (g_strcmp0 (commit_id, ref_commit_id) != 0)
    {
      g_autoptr(GVariant) ref_commit = NULL;

      if (!ostree_repo_load_commit (repo,
                                    ref_commit_id,
                                    &ref_commit,
                                    NULL,
                                    error))
        return FALSE;
      g_free (ref_commit_id);
      ref_commit_id = ostree_commit_get_parent (ref_commit);
      if (ref_commit_id == NULL)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Commit %s is not reachable from refspec %s",
                       commit_id,
                       refspec->str);
          return FALSE;
        }
    }

  return TRUE;
}

static gboolean
create_usb_repo (OstreeRepo *repo,
                 EosRefspec *refspec,
                 GFile *usb_path,
                 GCancellable *cancellable,
                 OstreeRepo **out_usb_repo,
                 GError **error)
{
  g_autoptr(GFile) usb_repo_path = NULL;
  g_autoptr(OstreeRepo) usb_repo = NULL;
  g_autofree gchar *url = NULL;
  g_auto(GVariantBuilder) builder = { { { 0, } } };
  g_autoptr(GVariant) options = NULL;
  g_autofree gchar *trusted_keys_name = NULL;
  g_autoptr(GFile) remote_trusted_keys = NULL;
  g_autoptr(GFileInputStream) gpg_stream = NULL;
  g_autoptr(GError) local_error = NULL;

  usb_repo_path = g_file_get_child (usb_path, "eos-update");
  usb_repo = ostree_repo_new (usb_repo_path);
  if (!ostree_repo_create (usb_repo,
                           OSTREE_REPO_MODE_ARCHIVE_Z2,
                           cancellable,
                           error))
    return FALSE;

  if (!ostree_repo_remote_get_url (repo,
                                   refspec->remote,
                                   &url,
                                   error))
    return FALSE;

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&builder, "{s@v}",
                         "branches",
                         g_variant_new_variant (g_variant_new_strv (&refspec->ref, 1)));
  options = g_variant_ref_sink (g_variant_builder_end (&builder));
  // ostree remote add --repo=dir/eos-update --no-gpg-verify eos https://endless:upgtnFSxLoDfUJnt@ostree.endlessm.com/ostree/eosfree-i386 eos2/i386
  if (!ostree_repo_remote_add (usb_repo,
                               refspec->remote,
                               url,
                               options,
                               cancellable,
                               error))
    return FALSE;

  trusted_keys_name = g_strdup_printf ("%s.trustedkeys.gpg", refspec->remote);
  remote_trusted_keys = g_file_get_child (ostree_repo_get_path (repo),
                                          trusted_keys_name);
  gpg_stream = g_file_read (remote_trusted_keys,
                            cancellable,
                            &local_error);

  if (gpg_stream == NULL &&
      !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  if (gpg_stream != NULL &&
      !ostree_repo_remote_gpg_import (usb_repo,
                                      refspec->remote,
                                      G_INPUT_STREAM (gpg_stream),
                                      NULL, /* take all the keys from the stream */
                                      NULL, /* not interested in a number of the imported keys */
                                      cancellable,
                                      error))
        return FALSE;

  *out_usb_repo = g_steal_pointer (&usb_repo);
  return TRUE;
}

typedef GMainContext ScopedMainContext;

static ScopedMainContext *
scoped_main_context_new (void)
{
  GMainContext *context = g_main_context_new ();

  g_main_context_push_thread_default (context);

  return context;
}

static void
scoped_main_context_free (ScopedMainContext *context)
{
  g_main_context_pop_thread_default (context);
  g_main_context_unref (context);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ScopedMainContext, scoped_main_context_free)

#define EOS_TYPE_PULL_DATA eos_pull_data_get_type ()
EOS_DECLARE_REFCOUNTED (EosPullData,
                        eos_pull_data,
                        EOS,
                        PULL_DATA)

struct _EosPullData
{
  GObject parent_instance;
  GError *error;
  GMainLoop *loop;

  gchar *source_uri;
  EosRefspec *refspec;
  gchar *commit_id;
  OstreeAsyncProgress *progress;
};

static void
eos_pull_data_dispose_impl (EosPullData *pull_data)
{
  g_clear_object (&pull_data->progress);
  g_clear_object (&pull_data->refspec);
  g_clear_pointer (&pull_data->loop, g_main_loop_unref);
}

static void
eos_pull_data_finalize_impl (EosPullData *pull_data)
{
  g_clear_pointer (&pull_data->error, g_error_free);
  g_clear_pointer (&pull_data->source_uri, g_free);
  g_clear_pointer (&pull_data->commit_id, g_free);
}

EOS_DEFINE_REFCOUNTED (EOS_PULL_DATA,
                       EosPullData,
                       eos_pull_data,
                       eos_pull_data_dispose_impl,
                       eos_pull_data_finalize_impl)

static EosPullData *
eos_pull_data_new (GMainLoop *loop,
                   const gchar *source_uri,
                   EosRefspec *refspec,
                   const gchar *commit_id,
                   OstreeAsyncProgress *progress)
{
  EosPullData *pull_data = g_object_new (EOS_TYPE_PULL_DATA, NULL);

  pull_data->loop = g_main_loop_ref (loop);
  pull_data->source_uri = g_strdup (source_uri);
  pull_data->refspec = g_object_ref (refspec);
  pull_data->commit_id = g_strdup (commit_id);
  pull_data->progress = g_object_ref (progress);

  return pull_data;
}

static void
pull_ready (GObject *object,
            GAsyncResult *res,
            gpointer pull_data_ptr)
{
  g_autoptr(EosPullData) pull_data = EOS_PULL_DATA (pull_data_ptr);

  g_task_propagate_boolean (G_TASK (res), &pull_data->error);
  g_main_loop_quit (pull_data->loop);
}

static void
run_pull_task_func (GTask *task,
                    gpointer repo_ptr,
                    gpointer pull_data_ptr,
                    GCancellable *cancellable)
{
  EosPullData *pull_data = EOS_PULL_DATA (pull_data_ptr);
  OstreeRepo *repo = OSTREE_REPO (repo_ptr);
  g_auto(GVariantBuilder) builder = { { { 0, } } };
  g_autoptr(GVariant) options = NULL;
  g_autoptr(GError) local_error = NULL;
  const gchar *refs[] = { pull_data->refspec->ref };
  const gchar *override_commit_ids[] = { pull_data->commit_id };
  g_autoptr(ScopedMainContext) context = scoped_main_context_new ();
  OstreeRepoPullFlags pull_flags = OSTREE_REPO_PULL_FLAGS_MIRROR;

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&builder,
                         "{s@v}",
                         "override-url",
                         g_variant_new_variant (g_variant_new_string (pull_data->source_uri)));
  g_variant_builder_add (&builder,
                         "{s@v}",
                         "refs",
                         g_variant_new_variant (g_variant_new_strv (refs, 1)));
  g_variant_builder_add (&builder,
                         "{s@v}",
                         "override-commit-ids",
                         g_variant_new_variant (g_variant_new_strv (override_commit_ids, 1)));
  g_variant_builder_add (&builder,
                         "{s@v}",
                         "depth",
                         g_variant_new_variant (g_variant_new_int32 (0)));
  g_variant_builder_add (&builder,
                         "{s@v}",
                         "flags",
                         g_variant_new_variant (g_variant_new_int32 (pull_flags)));
  options = g_variant_ref_sink (g_variant_builder_end (&builder));

  if (!ostree_repo_pull_with_options (repo,
                                      pull_data->refspec->remote,
                                      options,
                                      pull_data->progress,
                                      cancellable,
                                      &local_error))
    {
      g_task_return_error (task, g_steal_pointer (&local_error));
      return;
    }

  g_task_return_boolean (task, TRUE);
}

static void
run_ostree_pull_in_thread (OstreeRepo *repo,
                           GCancellable *cancellable,
                           EosPullData *pull_data)
{
  g_autoptr(GTask) task = g_task_new (repo,
                                      cancellable,
                                      pull_ready,
                                      g_object_ref (pull_data));

  g_task_set_task_data (task, g_object_ref (pull_data), g_object_unref);
  g_task_run_in_thread (task, run_pull_task_func);
}

static gboolean
do_pull (OstreeRepo *source_repo,
         OstreeRepo *target_repo,
         EosRefspec *refspec,
         const gchar *commit_id,
         OstreeAsyncProgress *progress,
         GCancellable *cancellable,
         GError **error)
{
  g_autoptr(ScopedMainContext) context = scoped_main_context_new ();
  g_autoptr(GMainLoop) loop = NULL;
  g_autofree gchar *source_uri = NULL;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(EosPullData) pull_data = NULL;

  source_uri = g_file_get_uri (ostree_repo_get_path (source_repo));

  loop = g_main_loop_new (context, FALSE);
  pull_data = eos_pull_data_new (loop,
                                 source_uri,
                                 refspec,
                                 commit_id,
                                 progress);
  run_ostree_pull_in_thread (target_repo,
                             cancellable,
                             pull_data);

  g_main_loop_run (loop);

  if (pull_data->error != NULL)
    {
      g_propagate_error (error, g_steal_pointer (&pull_data->error));
      return FALSE;
    }

  return TRUE;
}

/* Copy eos-summary{,.sig} to summary{,.sig} within the repository if the
 * latter do not already exist. This allows the standard ostree tools to use
 * the repository, without needing to know about our eos-summary extension. */
static gboolean
mirror_summary (OstreeRepo    *repo,
                GCancellable  *cancellable,
                GError       **error)
{
  GFile *repo_path;
  g_autoptr(GFile) _extensions_path = NULL, extensions_path = NULL;
  g_autoptr(GFile) source_summary = NULL, destination_summary = NULL;
  g_autoptr(GFile) source_sig = NULL, destination_sig = NULL;
  g_autoptr(GError) local_error = NULL;

  repo_path = ostree_repo_get_path (repo);
  _extensions_path = g_file_get_child (repo_path, "extensions");
  extensions_path = g_file_get_child (_extensions_path, "eos");

  /* Summary file. */
  source_summary = g_file_get_child (extensions_path, "eos-summary");
  destination_summary = g_file_get_child (repo_path, "summary");

  g_file_copy (source_summary, destination_summary,
               G_FILE_COPY_NONE, cancellable, NULL, NULL, &local_error);
  if (local_error != NULL &&
      !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) &&
      !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_EXISTS))
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }
  g_clear_error (&local_error);

  /* Signature file. */
  source_sig = g_file_get_child (extensions_path, "eos-summary.sig");
  destination_sig = g_file_get_child (repo_path, "summary.sig");

  g_file_copy (source_sig, destination_sig,
               G_FILE_COPY_NONE, cancellable, NULL, NULL, &local_error);
  if (local_error != NULL &&
      !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) &&
      !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_EXISTS))
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }
  g_clear_error (&local_error);

  return TRUE;
}

static gboolean
eos_updater_prepare_volume_internal (OstreeRepo *repo,
                                     const gchar *refspec_str,
                                     const gchar *commit_id,
                                     GFile *usb_path,
                                     OstreeAsyncProgress *progress,
                                     GCancellable *cancellable,
                                     GError **error)
{
  g_autoptr(EosRefspec) refspec = eos_refspec_new (refspec_str, error);
  g_autoptr(OstreeRepo) usb_repo = NULL;
  g_autoptr(EosExtensions) extensions = NULL;

  if (refspec == NULL)
    return FALSE;

  if (!ensure_coherency (repo,
                         refspec,
                         commit_id,
                         error))
    return FALSE;

  if (!create_usb_repo (repo,
                        refspec,
                        usb_path,
                        cancellable,
                        &usb_repo,
                        error))
    return FALSE;

  if (!do_pull (repo,
                usb_repo,
                refspec,
                commit_id,
                progress,
                cancellable,
                error))
    return FALSE;

  extensions = eos_extensions_new_from_repo (repo,
                                             cancellable,
                                             error);
  if (extensions == NULL)
    return FALSE;

  if (!eos_extensions_save (extensions,
                            usb_repo,
                            cancellable,
                            error))
    return FALSE;

  if (!mirror_summary (usb_repo, cancellable, error))
    return FALSE;

  return TRUE;
}

/**
 * eos_updater_prepare_volume_from_sysroot:
 * @sysroot: An #OstreeSysroot
 * @usb_path: A path to the volume
 * @progress: An #OstreeAsyncProgress for tracking the progress of preparations
 * @cancellable: (nullable): A #GCancellable
 * @error: A location for an error
 *
 * Prepares an update from the booted deployment of the @sysroot.
 *
 * Returns: Whether the preparations succeeded
 */
gboolean
eos_updater_prepare_volume_from_sysroot (OstreeSysroot *sysroot,
                                         GFile *usb_path,
                                         OstreeAsyncProgress *progress,
                                         GCancellable *cancellable,
                                         GError **error)
{
  g_autoptr(OstreeRepo) repo = NULL;
  OstreeDeployment *booted_deployment;
  GKeyFile *origin;
  g_autofree gchar *refspec = NULL;
  const gchar *commit_id;

  g_return_val_if_fail (OSTREE_IS_SYSROOT (sysroot), FALSE);
  g_return_val_if_fail (G_IS_FILE (usb_path), FALSE);
  g_return_val_if_fail (progress == NULL || OSTREE_IS_ASYNC_PROGRESS (progress), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable),
                        FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!ostree_sysroot_get_repo (sysroot,
                                &repo,
                                cancellable,
                                error))
    return FALSE;

  booted_deployment = eos_updater_get_booted_deployment_from_loaded_sysroot (sysroot,
                                                                             error);
  if (booted_deployment == NULL)
    return FALSE;

  origin = ostree_deployment_get_origin (booted_deployment);
  refspec = g_key_file_get_string (origin, "origin", "refspec", error);
  if (refspec == NULL)
    return FALSE;

  commit_id = ostree_deployment_get_csum (booted_deployment);
  return eos_updater_prepare_volume_internal (repo,
                                              refspec,
                                              commit_id,
                                              usb_path,
                                              progress,
                                              cancellable,
                                              error);
}

/**
 * eos_updater_prepare_volume:
 * @repo: A repo
 * @refspec: A refspec in @repo
 * @commit_id: A commit id (checksum IOW) in @refspec
 * @usb_path: A path to the volume
 * @progress: An #OstreeAsyncProgress for tracking the progress of preparations
 * @cancellable: (nullable): A #GCancellable
 * @error: A location for an error
 *
 * Prepares an update from the booted deployment of the @sysroot.
 *
 * Returns: Whether the preparations succeeded
 */
gboolean
eos_updater_prepare_volume (OstreeRepo *repo,
                            const gchar *refspec,
                            const gchar *commit_id,
                            GFile *usb_path,
                            OstreeAsyncProgress *progress,
                            GCancellable *cancellable,
                            GError **error)
{
  g_return_val_if_fail (OSTREE_IS_REPO (repo), FALSE);
  g_return_val_if_fail (refspec != NULL, FALSE);
  g_return_val_if_fail (commit_id != NULL, FALSE);
  g_return_val_if_fail (G_IS_FILE (usb_path), FALSE);
  g_return_val_if_fail (progress == NULL || OSTREE_IS_ASYNC_PROGRESS (progress), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable),
                        FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return eos_updater_prepare_volume_internal (repo,
                                              refspec,
                                              commit_id,
                                              usb_path,
                                              progress,
                                              cancellable,
                                              error);
}
