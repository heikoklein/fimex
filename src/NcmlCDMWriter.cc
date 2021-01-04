/*
 * Fimex
 *
 * (C) Copyright 2008, met.no
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
 */

#include "fimex/NcmlCDMWriter.h"

#include "fimex/CDM.h"
#include "fimex/Data.h"
#include "fimex/Logger.h"
#include "fimex/Type2String.h"

#include <fstream>

namespace MetNoFimex {

namespace {

Logger_p logger = getLogger("fimex.NcmlCDMWriter");

void shapeToXMLStream(std::ostream& out, const std::vector<std::string>& shape)
{
    if (shape.empty())
        return;

    out << " shape=\"";
    bool first = true;
    for (const auto& s : shape) {
        if (!first)
            out << ' ';
        first = false;
        out << s;
    }
    out << '"';
}

struct NcmlDataType
{
    const std::string name;
    bool is_unsigned;
    operator bool() const { return !name.empty(); }
};

const NcmlDataType ncml_BYTE = {"byte", false};
const NcmlDataType ncml_SHORT = {"short", false};
const NcmlDataType ncml_INT = {"int", false};
const NcmlDataType ncml_LONG = {"long", false};
const NcmlDataType ncml_UBYTE = {"byte", true};
const NcmlDataType ncml_USHORT = {"short", true};
const NcmlDataType ncml_UINT = {"int", true};
const NcmlDataType ncml_ULONG = {"long", true};
const NcmlDataType ncml_CHAR = {"char", false};
const NcmlDataType ncml_FLOAT = {"float", false};
const NcmlDataType ncml_DOUBLE = {"double", false};
const NcmlDataType ncml_STRING = {"string", false};
const NcmlDataType ncml_STRING_CAP = {"String", false};
const NcmlDataType ncml_NAT = {std::string(), false};

const NcmlDataType& datatype_cdm2ncml(CDMDataType dt)
{
    switch (dt) {
    case CDM_CHAR:
        return ncml_BYTE;
    case CDM_SHORT:
        return ncml_SHORT;
    case CDM_INT:
        return ncml_INT;
    case CDM_INT64:
        return ncml_LONG;
    case CDM_UCHAR:
        return ncml_UBYTE;
    case CDM_USHORT:
        return ncml_USHORT;
    case CDM_UINT:
        return ncml_UINT;
    case CDM_UINT64:
        return ncml_ULONG;
    case CDM_FLOAT:
        return ncml_FLOAT;
    case CDM_DOUBLE:
        return ncml_DOUBLE;
    case CDM_STRING:
        return ncml_STRING;
    default:
        return ncml_NAT;
    }
}

CDMDataType datatype_ncml2cdm(const NcmlDataType& dt)
{
    if (dt == ncml_NAT) {
        return CDM_NAT;
    } else if (dt == ncml_BYTE) {
        return CDM_CHAR;
    } else if (dt == ncml_SHORT) {
        return CDM_SHORT;
    } else if (dt == ncml_INT) {
        return CDM_INT;
    } else if (dt == ncml_LONG) {
        return CDM_INT64;
    } else if (dt == ncml_UBYTE) {
        return CDM_UCHAR;
    } else if (dt == ncml_USHORT) {
        return CDM_USHORT;
    } else if (dt == ncml_UINT) {
        return CDM_UINT;
    } else if (dt == ncml_ULONG) {
        return CDM_UINT64;
    } else if (dt == ncml_FLOAT) {
        return CDM_FLOAT;
    } else if (dt == ncml_DOUBLE) {
        return CDM_DOUBLE;
    } else if (dt == ncml_STRING || dt == ncml_STRING_CAP) {
        return CDM_STRING;
    } else {
        return CDM_NAT;
    }
}

} // namespace

NcmlCDMWriter::NcmlCDMWriter(const CDMReader_p cdmReader, const std::string& outputFile)
    : CDMWriter(cdmReader, outputFile)
{
    std::ofstream out(outputFile);
    write(out, true);
}

NcmlCDMWriter::NcmlCDMWriter(const CDMReader_p cdmReader, std::ostream& output, bool withData)
    : CDMWriter(cdmReader, "")
{
    write(output, withData);
}

NcmlCDMWriter::~NcmlCDMWriter() {}

void NcmlCDMWriter::write(std::ostream& out, bool withData)
{
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << std::endl
        << "<netcdf xmlns=\"http://www.unidata.ucar.edu/namespaces/netcdf/ncml-2.2\"" << std::endl
        << "        xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"" << std::endl
        << "        xsi:schemaLocation=\"http://www.unidata.ucar.edu/namespaces/netcdf/ncml-2.2 http://www.unidata.ucar.edu/schemas/netcdf/ncml-2.2.xsd\">"
        << std::endl;
    out << std::endl;

    const CDM& cdm = cdmReader->getCDM();
    for (const auto& dim : cdm.getDimensions()) {
        write(out, dim);
    }
    out << std::endl;

    for (const auto& att : cdm.getAttributes(cdm.globalAttributeNS())) {
        write(out, att, "");
    }

    const CDMDimension* unLimDim = cdm.getUnlimitedDim();
    const long long maxUnLim = (unLimDim ? unLimDim->getLength() : 0);
    for (const auto& var : cdm.getVariables()) {
        out << std::endl;
        const bool writeData = withData && (var.getDataType() != CDM_NAT);
        write(out, var, cdm.getAttributes(var.getName()), !writeData);
        if (writeData) {
            size_t dataSize = 1;
            for (const auto& d : var.getShape()) {
                const CDMDimension& dim = cdm.getDimension(d);
                if (!dim.isUnlimited())
                    dataSize *= dim.getLength();
            }

            std::string separator = " ";
            out << "<values";
            if (var.getDataType() == CDM_STRINGS) {
                separator = "---NEXT-NCML-STRING---";
                out << " separator=\"" << separator << "\" ";
            }
            out << ">";
            const bool has_unlimited = cdm.hasUnlimitedDim(var);
            for (long long unLimDimPos = -1; unLimDimPos < maxUnLim; ++unLimDimPos) {
                DataPtr data;
                if (unLimDimPos == -1 && !has_unlimited) {
                    data = cdmReader->getData(var.getName());
                } else if (unLimDimPos >= 0 && has_unlimited) {
                    data = cdmReader->getDataSlice(var.getName(), unLimDimPos);
                } else {
                    continue;
                }
                if (unLimDimPos > 0)
                    out << separator;
                if (data && data->size() > 0) {
                    data->toStream(out, separator);
                } else if (dataSize > 0) {
                    std::ostringstream fill;
                    createData(var.getDataType(), 1, cdm.getFillValue(var.getName()))->toStream(fill);
                    const std::string fillvalue = fill.str();

                    out << fillvalue;
                    for (size_t i = 1; i < dataSize; ++i) {
                        out << separator << fillvalue;
                    }
                }
            }
            out << "</values>" << std::endl;
            out << "</variable>" << std::endl; // otherwise closed by write(CDMVariable, ...)
        }
    }

    out << "</netcdf>" << std::endl;
}

// static
void NcmlCDMWriter::write(std::ostream& out, const CDMDimension& dim)
{
    out << "<dimension name=\"" << dim.getName() << "\" length=\"" << dim.getLength() << "\" ";
    if (dim.isUnlimited()) {
        out << "isUnlimited=\"true\" ";
    }
    out << "/>" << std::endl;
}

// static
void NcmlCDMWriter::write(std::ostream& out, const CDMAttribute& att, const std::string& indent)
{
    const NcmlDataType& dt = datatype_cdm2ncml(att.getDataType());
    out << indent << "<attribute name=\"" << att.getName() << "\" type=\"" << dt.name << "\" value=\"" << att.getStringValue() << "\" />" << std::endl;
}

// static
void NcmlCDMWriter::write(std::ostream& out, const CDMVariable& var, const std::vector<CDMAttribute>& attrs, bool closeXML)
{
    out << "<variable name=\"" << var.getName() << "\"";
    shapeToXMLStream(out, var.getShape());
    const NcmlDataType& dt = datatype_cdm2ncml(var.getDataType());
    bool opened = false;
    if (dt) {
        out << " type=\"" << dt.name << "\"";
        if (dt.is_unsigned) {
            out << ">\n  <attribute name=\"_Unsigned\" value=\"true\" />" << std::endl;
            opened = true;
        }
    } else {
        out << " type=\"int\">" << std::endl
            << "  <!-- cdm datatype '" << datatype2string(var.getDataType()) << "' not available in ncml, replaced with INT -->" << std::endl;
        opened = true;
    }
    if (!opened && (!attrs.empty() || !closeXML)) {
        out << ">" << std::endl;
        opened = true;
    }
    for (const auto& att : attrs) {
        write(out, att, "  ");
    }
    if (opened) {
        if (closeXML)
            out << "</variable>" << std::endl;
    } else {
        out << "/>" << std::endl;
    }
}

} // namespace MetNoFimex
