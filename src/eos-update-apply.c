#include <errno.h>
#include <libgsystem.h>
#include <gio/gio.h>
#include <glib.h>
#include <ostree.h>

const gchar *NEW_VERSION_PATH = "/ostree/apply-version";

static gboolean
apply (GCancellable *cancel,
       GError **error)
{
  gboolean ret = FALSE;
  g_autoptr(GFile) update_file = g_file_new_for_path (NEW_VERSION_PATH);
  gchar *update_id = NULL;
  gsize id_length;
  gint bootversion = 0;
  gint newbootver = 0;
  gs_unref_object OstreeDeployment *merge_deployment = NULL;
  gs_unref_object OstreeDeployment *new_deployment = NULL;
  GKeyFile *origin = NULL;
  gs_unref_object OstreeSysroot *sysroot = NULL;

  if (!g_file_load_contents (update_file, cancel, &update_id, &id_length, NULL, error))
    {
      if (g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        ret = TRUE;
      goto out;
    }

  if (id_length != 64)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Version in update version file not valid");
      goto out;
    }

  sysroot = ostree_sysroot_new_default ();
  /* The sysroot lock must be taken to prevent multiple processes (like this
   * and ostree admin upgrade) from deploying simultaneously, which will fail.
   * The lock will be unlocked automatically when sysroot is deallocated.
   */
  if (!ostree_sysroot_lock (sysroot, error))
    goto out;
  if (!ostree_sysroot_load (sysroot, cancel, error))
    goto out;

  bootversion = ostree_sysroot_get_bootversion (sysroot);
  merge_deployment = ostree_sysroot_get_merge_deployment (sysroot, NULL);
  origin = ostree_deployment_get_origin (merge_deployment);

  if (!ostree_sysroot_deploy_tree (sysroot,
                                   NULL,
                                   update_id,
                                   origin,
                                   merge_deployment,
                                   NULL,
                                   &new_deployment,
                                   cancel,
                                   error))
    goto out;

  if (!ostree_sysroot_simple_write_deployment (sysroot,
                                               NULL,
                                               new_deployment,
                                               merge_deployment,
                                               0,
                                               cancel,
                                               error))
    goto out;

  if (g_unlink(NEW_VERSION_PATH))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to delete update version file: %s",
                   g_strerror (errno));
      goto out;
    }

  ret = TRUE;
out:
  g_free(update_id);
  return ret;
}

gint
main (gint argc, gchar *argv[])
{
  GError *error = NULL;
  guint id = 0;

  g_set_prgname (argv[0]);

  apply(NULL, &error);

  if (error != NULL)
    {
      int is_tty = isatty (1);
      const char *prefix = "";
      const char *suffix = "";
      if (is_tty)
        {
          prefix = "\x1b[31m\x1b[1m"; /* red, bold */
          suffix = "\x1b[22m\x1b[0m"; /* bold off, color reset */
        }
      g_printerr ("%serror: %s%s\n", prefix, suffix, error->message);
      g_error_free (error);
    }

  return 0;
}
