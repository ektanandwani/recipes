/* gr-image.c:
 *
 * Copyright (C) 2016 Matthias Clasen <mclasen@redhat.com>
 *
 * Licensed under the GNU General Public License Version 3
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <libsoup/soup.h>

#include "gr-image.h"
#include "gr-utils.h"


struct _GrImage
{
        GObject parent_instance;
        char *path;

        SoupSession *session;
        SoupMessage *thumbnail_message;
        SoupMessage *image_message;
        GList *pending;
};

G_DEFINE_TYPE (GrImage, gr_image, G_TYPE_OBJECT)

static void
gr_image_finalize (GObject *object)
{
        GrImage *ri = GR_IMAGE (object);

        if (ri->thumbnail_message)
                soup_session_cancel_message (ri->session,
                                             ri->thumbnail_message,
                                             SOUP_STATUS_CANCELLED);
        g_clear_object (&ri->thumbnail_message);
        if (ri->image_message)
                soup_session_cancel_message (ri->session,
                                             ri->image_message,
                                             SOUP_STATUS_CANCELLED);
        g_clear_object (&ri->image_message);
        g_clear_object (&ri->session);
        g_free (ri->path);

        G_OBJECT_CLASS (gr_image_parent_class)->finalize (object);
}

static void
gr_image_class_init (GrImageClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gr_image_finalize;
}

static void
gr_image_init (GrImage *image)
{
}

GrImage *
gr_image_new (SoupSession *session,
              const char  *path)
{
        GrImage *image;

        image = g_object_new (GR_TYPE_IMAGE, NULL);
        image->session = g_object_ref (session);
        gr_image_set_path (image, path);

        return image;
}

void
gr_image_set_path (GrImage    *image,
                   const char *path)
{
        g_free (image->path);
        image->path = g_strdup (path);
}

const char *
gr_image_get_path (GrImage *image)
{
        return image->path;
}

GPtrArray *
gr_image_array_new (void)
{
        return g_ptr_array_new_with_free_func (g_object_unref);
}

static GdkPixbuf *
load_pixbuf (const char *path,
             int         width,
             int         height,
             gboolean    fit)
{
        GdkPixbuf *pixbuf;

        if (fit)
                pixbuf = load_pixbuf_fit_size (path, width, height, FALSE);
        else
                pixbuf = load_pixbuf_fill_size (path, width, height);

        return pixbuf;
}

typedef struct {
        GtkImage *image;
        int width;
        int height;
        gboolean fit;
        GCancellable *cancellable;
        GrImageCallback callback;
        gpointer data;
} TaskData;

static void
task_data_free (gpointer data)
{
        TaskData *td = data;

        g_clear_object (&td->cancellable);

        g_free (td);
}

static char *
get_image_url (const char *path)
{
        g_autofree char *basename = NULL;

        basename = g_path_get_basename (path);
        return g_strconcat ("http://mclasen.fedorapeople.org/recipes/images/", basename, NULL);
}

static char *
get_thumbnail_url (const char *path)
{
        g_autofree char *basename = NULL;

        basename = g_path_get_basename (path);
        return g_strconcat ("http://mclasen.fedorapeople.org/recipes/thumbnails/", basename, NULL);
}

static char *
get_image_cache_path (const char *path)
{
        char *filename;
        g_autofree char *cache_dir = NULL;
        g_autofree char *basename = NULL;

        basename = g_path_get_basename (path);
        filename = g_build_filename (g_get_user_cache_dir (), PACKAGE_NAME, "images", basename, NULL);
        cache_dir = g_path_get_dirname (filename);
        g_mkdir_with_parents (cache_dir, 0755);

        return filename;
}

static char *
get_thumbnail_cache_path (const char *path)
{
        char *filename;
        g_autofree char *cache_dir = NULL;
        g_autofree char *basename = NULL;

        basename = g_path_get_basename (path);
        filename = g_build_filename (g_get_user_cache_dir (), PACKAGE_NAME, "thumbnails", basename, NULL);
        cache_dir = g_path_get_dirname (filename);
        g_mkdir_with_parents (cache_dir, 0755);

        return filename;
}

static void
set_image (SoupSession *session,
           SoupMessage *msg,
           gpointer     data)
{
        GrImage *ri = data;
        g_autofree char *cache_path = NULL;
        GdkPixbuf *pixbuf;
        GList *l;

        l = ri->pending;
        while (l) {
                GList *next = l->next;
                TaskData *td = l->data;

                if (g_cancellable_is_cancelled (td->cancellable)) {
                        ri->pending = g_list_remove (ri->pending, td);
                        task_data_free (td);
                }
                l = next;
        }

        if (msg->status_code == SOUP_STATUS_CANCELLED || ri->session == NULL) {
                g_debug ("Message cancelled");
                goto error;
        }

        if (msg->status_code != SOUP_STATUS_OK) {
                g_debug ("Status not ok: %d", msg->status_code);
                goto out;
        }

        if (msg == ri->thumbnail_message) {
                if (ri->image_message == NULL) // already got the image, ignore the thumbnail
                        goto out;
                cache_path = get_thumbnail_cache_path (ri->path);
        }
        else {
                cache_path = get_image_cache_path (ri->path);
        }

        g_debug ("Saving image to %s", cache_path);
        if (!g_file_set_contents (cache_path, msg->response_body->data, msg->response_body->length, NULL)) {
                g_debug ("Saving image to %s failed", cache_path);
                goto out;
        }

        g_debug ("Loading image for %s", ri->path);

        for (l = ri->pending; l; l = l->next) {
                TaskData *td = l->data;

                if (msg == ri->thumbnail_message) {
                        g_autoptr(GdkPixbuf) tmp = NULL;

                        tmp = load_pixbuf (cache_path, 120, 120 * td->height / td->width, td->fit);

                        pixbuf = gdk_pixbuf_scale_simple (tmp, td->width, td->height, GDK_INTERP_BILINEAR);
                        pixbuf_blur (pixbuf, 5, 3);
                }
                else {
                        pixbuf = load_pixbuf (cache_path, td->width, td->height, td->fit);
                }
                if (pixbuf)
                        td->callback (ri, pixbuf, td->data);
                else {
                        g_message ("Failed to load %s", ri->path);
                        break;
                }
        }

out:
        if (msg == ri->thumbnail_message)
                g_clear_object (&ri->thumbnail_message);
        else
                g_clear_object (&ri->image_message);

        if (ri->thumbnail_message || ri->image_message)
                return;

error:
        g_list_free_full (ri->pending, task_data_free);
        ri->pending = NULL;
}

void
gr_image_load (GrImage         *ri,
               int              width,
               int              height,
               gboolean         fit,
               GCancellable    *cancellable,
               GrImageCallback  callback,
               gpointer         data)
{
        TaskData *td;
        g_autofree char *cache_path = NULL;
        g_autofree char *thumbnail_cache_path = NULL;
        g_autoptr(GdkPixbuf) pixbuf = NULL;

        if (ri->path[0] == '/') {
                pixbuf = load_pixbuf (ri->path, width, height, fit);
                if (pixbuf) {
                        g_debug ("Use local image for %s", ri->path);
                        callback (ri, pixbuf, data);
                        return;
                }
        }

        cache_path = get_image_cache_path (ri->path);
        pixbuf = load_pixbuf (cache_path, width, height, fit);

        if (pixbuf) {
                g_debug ("Use cached image for %s", ri->path);
                callback (ri, pixbuf, data);
                return;
        }

        td = g_new0 (TaskData, 1);
        td->width = width;
        td->height = height;
        td->fit = fit;
        td->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
        td->callback = callback;
        td->data = data;

        ri->pending = g_list_prepend (ri->pending, td);

        thumbnail_cache_path = get_thumbnail_cache_path (ri->path);
        pixbuf = load_pixbuf (thumbnail_cache_path, 120, 120 * height / width, fit);
        if (pixbuf) {
                g_autoptr(GdkPixbuf) blurred = NULL;
                g_debug ("Use cached thumbnail for %s", ri->path);

                blurred = gdk_pixbuf_scale_simple (pixbuf, width, height, GDK_INTERP_BILINEAR);
                pixbuf_blur (blurred, 5, 3);
                callback (ri, blurred, data);
        }
        else if (width > 240 && ri->thumbnail_message == NULL) {
                g_autofree char *url = NULL;
                g_autoptr(SoupURI) base_uri = NULL;

                url = get_thumbnail_url (ri->path);
                base_uri = soup_uri_new (url);
                ri->thumbnail_message = soup_message_new_from_uri (SOUP_METHOD_GET, base_uri);
                g_debug ("Load thumbnail for %s from %s", ri->path, url);
                soup_session_queue_message (ri->session, g_object_ref (ri->thumbnail_message), set_image, ri);
        }

        if (ri->image_message == NULL) {
                g_autofree char *url = NULL;
                g_autoptr(SoupURI) base_uri = NULL;

                url = get_image_url (ri->path);
                base_uri = soup_uri_new (url);
                ri->image_message = soup_message_new_from_uri (SOUP_METHOD_GET, base_uri);
                g_debug ("Load image for %s from %s", ri->path, url);
                soup_session_queue_message (ri->session, g_object_ref (ri->image_message), set_image, ri);
        }
}

void
gr_image_set_pixbuf (GrImage   *ri,
                     GdkPixbuf *pixbuf,
                     gpointer   data)
{
        gtk_image_set_from_pixbuf (GTK_IMAGE (data), pixbuf);
}

GdkPixbuf  *
gr_image_load_sync (GrImage   *ri,
                    int        width,
                    int        height,
                    gboolean   fit)
{
        GdkPixbuf *pixbuf;

        if (ri->path[0] == '/') {
                pixbuf = load_pixbuf (ri->path, width, height, fit);
        }
        else {
                g_autofree char *cache_path = NULL;

                cache_path = get_image_cache_path (ri->path);
                pixbuf = load_pixbuf (cache_path, width, height, fit);
        }

        if (!pixbuf) {
                g_autoptr(GtkIconInfo) info = NULL;

                info = gtk_icon_theme_lookup_icon (gtk_icon_theme_get_default (),
                                                   "org.gnome.Recipes",
                                                    256,
                                                    GTK_ICON_LOOKUP_FORCE_SIZE);
                pixbuf = load_pixbuf (gtk_icon_info_get_filename (info), width, height, fit);

        }

        return pixbuf;
}
