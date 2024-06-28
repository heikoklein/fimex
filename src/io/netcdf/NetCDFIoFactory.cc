/*
  Fimex, src/NetCDFIoFactory.cc

  Copyright (C) 2019-2024 met.no

  Contact information:
  Norwegian Meteorological Institute
  Box 43 Blindern
  0313 OSLO
  NORWAY
  email: diana@met.no

  Project Info:  https://wiki.met.no/fimex/start

  This library is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
  License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
  USA.
*/

#include "NetCDFIoFactory.h"

#define MIFI_IO_READER_SUPPRESS_DEPRECATED
#include "NetCDF_CDMReader.h"
#include "NetCDF_CDMWriter.h"
#undef MIFI_IO_READER_SUPPRESS_DEPRECATED

#include "fimex/CDMException.h"
#include "fimex/FileUtils.h"
#include "fimex/IoPlugin.h"
#include "fimex/NcmlCDMReader.h"
#include "fimex/StringUtils.h"
#include "fimex/XMLInputString.h"

#include <regex>
#include <set>

namespace MetNoFimex {

namespace {

bool detectHDF5(const char* magic)
{
    const char* hdf5 = "\211HDF\r\n\032";
    for (int i = 0; i < 7; i++) {
        if (hdf5[i] != magic[i])
            return false;
    }
    return true;
}

bool detectNetCDF(const char* magic)
{
    const char* netcdf = "CDF\001";
    for (int i = 0; i < 4; i++) {
        if (netcdf[i] != magic[i])
            return false;
    }
    return true;
}

bool isNetCDF4Type(const std::string& type)
{
    return (type == "nc4");
}

bool isNetCDFType(const std::string& type)
{
    return (type == "nc" || type == "cdf" || type == "netcdf" || isNetCDF4Type(type));
}

bool isNetCDFZarrFile(const std::string& file)
{
    static const std::regex zarr_file("^file:.*#.*mode=.*zarr.*");
    std::smatch zarr_file_match;
    return std::regex_match(file, zarr_file_match, zarr_file);
}

} // namespace

size_t NetCDFIoFactory::matchMagicSize()
{
    return 7;
}

int NetCDFIoFactory::matchMagic(const char* magic, size_t count)
{
    if ((count >= 4 && detectHDF5(magic)) || (count >= 7 && detectNetCDF(magic)))
        return 1;
    return 0;
}

bool NetCDFIoFactory::matchFileType(const std::string& type, bool& is_rw)
{
    std::string nc;
    is_rw = false;
    static const std::regex re_type_rw("(\\w+)(\\+rw)?");
    std::smatch what;
    if (std::regex_match(type, what, re_type_rw)) {
        nc = what[1].str();
        is_rw = what[2].matched;
    }
    return isNetCDFType(nc) ? 1 : 0;
}

int NetCDFIoFactory::matchFileTypeName(const std::string& type)
{
    bool is_rw = false;
    return matchFileType(type, is_rw) ? 1 : 0;
}

int NetCDFIoFactory::matchFileName(const std::string& fileName)
{
    const std::string ext = getExtension(fileName);
    if (ext == "nc" || ext == "nc4")
        return 1;

    // match urls with schemes http(s) and dods, as these likely are OpenDAP
    static const std::set<std::string> url_schemes{"https", "http", "dods"};
    static const std::regex re_url_scheme("^(\\w+)://");
    std::smatch what;
    if (std::regex_search(fileName, what, re_url_scheme)) {
        if (url_schemes.count(what[1].str()))
            return 2;
    }

    if (isNetCDFZarrFile(fileName))
        return 1;

    return 0;
}

CDMReader_p NetCDFIoFactory::createReader(const std::string& fileTypeName, const std::string& fileName, const XMLInput& config,
                                          const std::vector<std::string>& args)
{
    bool is_rw = false;
    matchFileType(fileTypeName, is_rw);
    if (is_rw)
        return createReaderWriter(fileTypeName, fileName, config, args);

    CDMReader_p reader;
    // scanfiles by a glob
    const std::string globStr("glob:");
    if (starts_with(fileName, globStr)) {
        std::string glob = fileName.substr(globStr.size());
        std::vector<std::string> files;
        globFiles(files, glob);
        std::ostringstream ncml;
        ncml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << std::endl
             << "<netcdf xmlns=\"http://www.unidata.ucar.edu/namespaces/netcdf/ncml-2.2\">"
             << "<aggregation type=\"joinExisting\">";
        for (size_t i = 0; i < files.size(); ++i) {
            ncml << "<netcdf location=\"" << files.at(i) << "\" />";
        }
        ncml << "</aggregation></netcdf>";
        reader = std::make_shared<NcmlCDMReader>(XMLInputString(ncml.str()));
    } else {
        std::string file = fileName;

        // remove file: URL-prefix, except for zarr
        if (!isNetCDFZarrFile(file)) {
            file = std::regex_replace(file, std::regex("^file:"), "", std::regex_constants::format_first_only);
        }

        // java-netcdf allows dods: prefix for dods-files while netcdf-C requires http:
        file = std::regex_replace(file, std::regex("^dods:"), "http:", std::regex_constants::format_first_only);

        reader = std::make_shared<NetCDF_CDMReader>(file, false);
    }
    if (!config.isEmpty())
        reader = std::make_shared<NcmlCDMReader>(reader, config);
    return reader;
}

CDMReaderWriter_p NetCDFIoFactory::createReaderWriter(const std::string&, const std::string& fileName, const XMLInput& config, const std::vector<std::string>&)
{
    if (!config.isEmpty())
        throw CDMException("Cannot open writeable NetCDF file with Ncml config: " + config.id());
    return std::make_shared<NetCDF_CDMReader>(fileName, true);
}

void NetCDFIoFactory::createWriter(CDMReader_p input, const std::string& fileTypeName, const std::string& fileName, const XMLInput& config)
{
    const int version = isNetCDF4Type(fileTypeName) ? 4 : 3;
    NetCDF_CDMWriter(input, fileName, config, version);
}

} // namespace MetNoFimex

DEFINE_IO_PLUGIN("netcdf", MetNoFimex::NetCDFIoFactory)
