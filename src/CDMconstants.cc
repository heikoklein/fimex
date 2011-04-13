/*
 * Fimex, CDMconstants.c
 *
 * (C) Copyright 2010, met.no
 *
 * Project Info:  https://wiki.met.no/fimex/start
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 *
 *  Created on: May 5, 2010
 *      Author: Heiko Klein
 */

#include "fimex/CDMconstants.h"
#include "../config.h"
#include <vector>
#include <iterator>
#include <algorithm>

// remember to upgrade this function together with
// with the MIFI_FILETYPE_*
std::vector<std::string> names_;
static void addName_(int pos, const std::string& name)
{
    if (pos >= names_.size()) {
        names_.resize(pos+1);
    }
    names_.at(pos) = name;
}
static std::vector<std::string> getFiletypeNames_()
{
    if (names_.size() == 0) {
        addName_(MIFI_FILETYPE_FELT,   "felt");
        addName_(MIFI_FILETYPE_NETCDF, "netcdf");
        addName_(MIFI_FILETYPE_NCML,   "ncml");
        addName_(MIFI_FILETYPE_GRIB,   "grib");
        addName_(MIFI_FILETYPE_WDB,    "wdb");
    }
    return names_;
}

const char* mifi_get_filetype_name(int filetype)
{
    std::vector<std::string> names = getFiletypeNames_();
    if (filetype >= 0 && filetype < names.size()) {
        return names.at(filetype).c_str();
    }
    return "";
}

int mifi_get_filetype(const char* name)
{
    std::vector<std::string> names = getFiletypeNames_();
    std::vector<std::string>::iterator namePos = std::find(names.begin(), names.end(), name);
    if (namePos == names.end()) {
        return MIFI_FILETYPE_UNKNOWN;
    } else {
        return std::distance(names.begin(), namePos);
    }
}
const char* fimexVersion()
{
    return VERSION;
}

int fimexHas(int fileType)
{
    switch (fileType) {
    case MIFI_FILETYPE_FELT:
#ifdef HAVE_FELT
    return 1;
#else
    return 0;
#endif
    case MIFI_FILETYPE_NETCDF:
#ifdef MIFI_HAVE_NETCDF
    return 1;
#else
    return 0;
#endif
    case MIFI_FILETYPE_NCML: return 1;
    case MIFI_FILETYPE_GRIB:
#ifdef HAVE_GRIBAPI_H
    return 1;
#else
    return 0;
#endif
    case MIFI_FILETYPE_WDB:
#ifdef HAVE_LIBPQ
    return 1;
#else
    return 0;
#endif
    default: return 0;
    }
}

// the functions below are deprecated and will be removed soon
int fimexHasNetcdf()
{
#ifdef MIFI_HAVE_NETCDF
    return 1;
#else
    return 0;
#endif
}
int fimexHasGribApi()
{
#ifdef HAVE_GRIBAPI_H
    return 1;
#else
    return 0;
#endif
}
int fimexHasFelt()
{
#ifdef HAVE_FELT
    return 1;
#else
    return 0;
#endif

}