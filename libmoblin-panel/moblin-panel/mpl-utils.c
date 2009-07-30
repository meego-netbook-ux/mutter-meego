/*
 * Copyright (C) 2008 - 2009 Intel Corporation.
 *
 * Author: Rob Bradford <rob@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <glib/gi18n.h>

#include "mpl-utils.h"

/* Some code here is stolen from Tweet (C) Emmanuele Bassi */
gchar *
mpl_utils_format_time (GTimeVal *time_)
{
  GTimeVal now;
  struct tm tm_mtime;
  gchar *retval = NULL;
  GDate d1, d2;
  gint secs_diff, mins_diff, hours_diff, days_diff, months_diff, years_diff;

  g_return_val_if_fail (time_->tv_usec >= 0 && time_->tv_usec < G_USEC_PER_SEC, NULL);

  g_get_current_time (&now);

#ifdef HAVE_LOCALTIME_R
  localtime_r ((time_t *) &(time_->tv_sec), &tm_mtime);
#else
  {
    struct tm *ptm = localtime ((time_t *) &(time_->tv_sec));

    if (!ptm)
      {
        g_warning ("ptm != NULL failed");
        return NULL;
      }
    else
      memcpy ((void *) &tm_mtime, (void *) ptm, sizeof (struct tm));
  }
#endif /* HAVE_LOCALTIME_R */

  secs_diff = now.tv_sec - time_->tv_sec;
  if (secs_diff < 60)
    return g_strdup (_("Less than a minute ago"));

  mins_diff = secs_diff / 60;
  if (mins_diff < 60)
    return g_strdup (_("A few minutes ago"));

  hours_diff = mins_diff / 60;
  if (hours_diff < 3)
    return g_strdup (_("A couple of hours ago"));

  g_date_set_time_t (&d1, now.tv_sec);
  g_date_set_time_t (&d2, time_->tv_sec);
  days_diff = g_date_get_julian (&d1) - g_date_get_julian (&d2);

  if (days_diff == 0)
    return g_strdup (_("Earlier today"));

  if (days_diff == 1)
    return g_strdup (_("Yesterday"));

  if (days_diff < 7)
    {
      const gchar *format = NULL;
      gchar *locale_format = NULL;
      gchar buf[256];

      format = _("On %A"); /* day of the week */
      locale_format = g_locale_from_utf8 (format, -1, NULL, NULL, NULL);

      if (strftime (buf, sizeof (buf), locale_format, &tm_mtime) != 0)
        retval = g_locale_to_utf8 (buf, -1, NULL, NULL, NULL);
      else
        retval = g_strdup (_("Unknown"));

      g_free (locale_format);
      return retval;
    }

  if (days_diff < 14)
    return g_strdup (_("Last week"));

  if (days_diff < 21)
    return g_strdup (_("A couple of weeks ago"));

  months_diff = g_date_get_month (&d1) - g_date_get_month (&d2);
  years_diff = g_date_get_year (&d1) - g_date_get_year (&d2);

  if (years_diff == 0 && months_diff == 0)
    return g_strdup (_("This month"));

  if ((years_diff == 0 && months_diff == 1) ||
      (years_diff == 1 && months_diff == -11)) /* Now Jan., last used in Dec. */
    return g_strdup (_("Last month"));

  if (years_diff == 0)
    return g_strdup (_("This year"));

  if (years_diff == 1)
    return g_strdup (_("Last year"));

  return g_strdup (_("Ages ago"));
}

gchar *
mpl_utils_get_thumbnail_path (const gchar *uri)
{
  gchar *thumbnail_path;
  gchar *thumbnail_filename = NULL;
  gchar *csum;

  csum = g_compute_checksum_for_string (G_CHECKSUM_MD5, uri, -1);

  thumbnail_path = g_build_filename (g_get_home_dir (),
                                     ".bkl-thumbnails",
                                     csum,
                                     NULL);

  if (g_file_test (thumbnail_path, G_FILE_TEST_EXISTS))
  {
    g_free (csum);
    goto success;
  }

  g_free (thumbnail_path);

  thumbnail_filename = g_strconcat (csum, ".png", NULL);
  thumbnail_path = g_build_filename (g_get_home_dir (),
                                     ".thumbnails",
                                     "large",
                                     thumbnail_filename,
                                     NULL);

  g_free (csum);

  if (g_file_test (thumbnail_path, G_FILE_TEST_EXISTS))
  {
    goto success;
  } else {
    g_free (thumbnail_path);
    thumbnail_path = g_build_filename (g_get_home_dir (),
                                       ".thumbnails",
                                       "normal",
                                       thumbnail_filename,
                                       NULL);

    if (g_file_test (thumbnail_path, G_FILE_TEST_EXISTS))
    {
      goto success;
    }
  }

  g_free (thumbnail_filename);
  g_free (thumbnail_path);
  return NULL;

success:
  g_free (thumbnail_filename);
  return thumbnail_path;
}
