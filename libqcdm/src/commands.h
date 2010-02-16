/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBQCDM_COMMANDS_H
#define LIBQCDM_COMMANDS_H

#include <glib.h>

#include "result.h"

gsize        qcdm_cmd_version_info_new    (char *buf,
                                           gsize len,
                                           GError **error);

#define QCDM_CMD_VERSION_INFO_ITEM_COMP_DATE "comp-date"
#define QCDM_CMD_VERSION_INFO_ITEM_COMP_TIME "comp-time"
#define QCDM_CMD_VERSION_INFO_ITEM_RELEASE_DATE "release-date"
#define QCDM_CMD_VERSION_INFO_ITEM_RELEASE_TIME "release-time"
#define QCDM_CMD_VERSION_INFO_ITEM_MODEL "model"

QCDMResult *qcdm_cmd_version_info_result (const char *buf,
                                          gsize len,
                                          GError **error);

#endif  /* LIBQCDM_COMMANDS_H */
