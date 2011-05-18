/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/*
 * Meego Netbook
 * Copyright © 2009, 2010, Intel Corporation.
 *
 * Author: Tomas Frydrych <tf@linux.intel.com>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _MNB_ALTTAB_ALTTAB
#define _MNB_ALTTAB_ALTTAB

#include <meta/screen.h>
#include <meta/display.h>
#include <meta/window.h>
#include <meta/keybindings.h>

G_BEGIN_DECLS

void
mnb_alttab_overlay_alt_tab_key_handler (MetaDisplay    *display,
                                        MetaScreen     *screen,
                                        MetaWindow     *window,
                                        XEvent         *event,
                                        MetaKeyBinding *binding,
                                        gpointer        data);

void
mnb_alttab_overlay_alt_tab_select_handler (MetaDisplay    *display,
                                           MetaScreen     *screen,
                                           MetaWindow     *window,
                                           XEvent         *event,
                                           MetaKeyBinding *binding,
                                           gpointer        data);


void
mnb_alttab_overlay_alt_tab_cancel_handler (MetaDisplay    *display,
                                           MetaScreen     *screen,
                                           MetaWindow     *window,
                                           XEvent         *event,
                                           MetaKeyBinding *binding,
                                           gpointer        data);

G_END_DECLS

#endif /* _MNB_ALTTAB_ALTTAB */

