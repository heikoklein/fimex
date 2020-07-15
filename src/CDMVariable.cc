/*
 * Fimex
 *
 * (C) Copyright 2008-2020, met.no
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

#include "fimex/CDMVariable.h"
#include "fimex/CDMAttribute.h"
#include "fimex/CDMException.h"
#include "fimex/Data.h"

#include <algorithm>
#include <ostream>

namespace MetNoFimex {

// static
CDMVariable::SpatialVectorDirection CDMVariable::vectorDirectionFromString(const std::string& vd)
{
    if (vd.find("x") != std::string::npos)
        return CDMVariable::SPATIAL_VECTOR_X;
    else if (vd.find("y") != std::string::npos)
        return CDMVariable::SPATIAL_VECTOR_Y;
    else if (vd.find("lon") != std::string::npos)
        return CDMVariable::SPATIAL_VECTOR_LON;
    else if (vd.find("lat") != std::string::npos)
        return CDMVariable::SPATIAL_VECTOR_LAT;
    else
        return CDMVariable::SPATIAL_VECTOR_NONE;
}

CDMVariable::CDMVariable(std::string name, CDMDataType datatype, std::vector<std::string> shape)
    : name(name)
    , datatype(datatype)
    , shape(shape)
    , spatialVectorDirection(SPATIAL_VECTOR_NONE)
{
}

CDMVariable::~CDMVariable()
{
}

bool CDMVariable::checkDimension(const std::string& dimension) const
{
    const std::vector<std::string>& shape = getShape();
    return (std::find(shape.begin(), shape.end(), dimension) != shape.end());
}

void CDMVariable::setAsSpatialVector(const std::string& counterpart, SpatialVectorDirection direction)
{
    spatialVectorCounterpart = counterpart;
    spatialVectorDirection = direction;
}

void CDMVariable::setDataType(CDMDataType type)
{
    datatype = type;
    if (data && datatype != data->getDataType())
        data = DataPtr();
}

void CDMVariable::setData(DataPtr d)
{
    if (d && datatype != d->getDataType()) {
        throw CDMException("variable '" + getName() + "' with datatype " + datatype2string(datatype) + " cannot store data with datatype " +
                           datatype2string(d->getDataType()));
    }
    data = d;
}

void CDMVariable::shapeToXMLStream(std::ostream& out) const
{
    if (shape.empty())
        return;

    out << "shape=\"";
    bool first = true;
    for (unsigned int i = 0; i < shape.size(); i++) {
        if (!first)
            out << ' ';
        out << shape[i];
    }
    out << "\" ";
}

void CDMVariable::toXMLStream(std::ostream& out, const std::vector<CDMAttribute>& attrs) const
{
    CDMDataType dt = getDataType();
    out << "<variable name=\"" << getName() << "\" type=\"" << datatype2string(dt) << "\" ";
    shapeToXMLStream(out);
    if (!attrs.empty()) {
        out << ">" << std::endl;
        for (const auto& att : attrs) {
            att.toXMLStream(out, "  ");
        }
        out << "</variable>" << std::endl;
    } else {
        out << "/>" << std::endl;
    }
}

void CDMVariable::toXMLStream(std::ostream& out) const
{
    toXMLStream(out, std::vector<CDMAttribute>());
}

} // namespace MetNoFimex
