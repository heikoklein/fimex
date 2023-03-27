/*
 * Fimex
 *
 * (C) Copyright 2008-2022, met.no
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

#include "fimex/CDMExtractor.h"

#include "fimex/CDM.h"
#include "fimex/CDMDataType.h"
#include "fimex/CDMException.h"
#include "fimex/CDMReaderUtils.h"
#include "fimex/Data.h"
#include "fimex/Logger.h"
#include "fimex/SliceBuilder.h"
#include "fimex/StringUtils.h"
#include "fimex/TimeUnit.h"
#include "fimex/Type2String.h"
#include "fimex/Units.h"
#include "fimex/coordSys/CoordinateSystem.h"

#include "CDMMergeUtils.h"

#include <algorithm>
#include <cassert>
#include <functional>
#include <iterator>
#include <numeric>
#include <set>
#include <vector>

namespace MetNoFimex
{

static Logger_p logger(getLogger("fimex.CDMExtractor"));

CDMExtractor::CDMExtractor(CDMReader_p datareader)
: dataReader_(datareader)
{
    *cdm_ = dataReader_->getCDM();
}

CDMExtractor::~CDMExtractor()
{
}

// join SliceBuilder request and build a big dataPtr
// the slices need to come in a logical order of the data
DataPtr joinSlices(CDMReader_p reader, std::string varName, const std::vector<SliceBuilder>& slices)
{
    const CDMVariable& var = reader->getCDM().getVariable(varName);
    // handle trivial cases
    if (slices.size() == 0)
        return createData(var.getDataType(), 0, 0.);
    if (slices.size() == 1)
        return reader->getDataSlice(varName, slices.front());

    // calculate total slice size
    std::vector<std::size_t> sliceSizes(slices.size());
    size_t totalSize = 0;
    for (size_t i = 0; i < slices.size(); i++) {
        const SliceBuilder& sb = slices[i];
        const std::vector<std::size_t>& dimSizes = sb.getDimensionSizes();
        sliceSizes[i] = std::accumulate(dimSizes.begin(), dimSizes.end(), 1ul, std::multiplies<size_t>());
        totalSize += sliceSizes[i];
    }

    DataPtr retData = createData(var.getDataType(), totalSize, reader->getCDM().getFillValue(varName));

    // add the data
    size_t dataPos = 0;
    for (size_t i = 0; i < slices.size(); i++) {
        if (DataPtr sliceData = reader->getDataSlice(varName, slices[i])) {
            assert(sliceData->size() == sliceSizes[i]);
            if (sliceData->size() > 0)
                retData->setValues(dataPos, *sliceData);
        }
        dataPos += sliceSizes[i];
    }
    return retData;
}

DataPtr CDMExtractor::getDataSlice_(const std::string& varName, const SliceBuilder& sb)
{
    std::vector<SliceBuilder> slices;
    // translate slice-variable size where dimensions have been transformed, (via data.slice)
    const CDM& orgCDM = dataReader_->getCDM();
    slices.push_back(SliceBuilder(orgCDM, varName));

    const std::vector<std::string> dims = slices.front().getDimensionNames(); // make a copy, we might replace slices[0] in the following loop

    // loop over variables dimensions and see which to reduce
    // revert the dimensions to match joining, unlimit is last in slicebuilder, but slowest changing
    for (const std::string& dimName : dims) {
        const CDMDimension& dim = orgCDM.getDimension(dimName);
        DimSlicesMap::iterator foundDim = dimSlices_.find(dim.getName());
        size_t sbStart, sbSize;
        sb.getStartAndSize(dimName, sbStart, sbSize);
        if (foundDim == dimSlices_.end()) {
            // handle pure sb changes
            for (SliceBuilder& sliceb : slices)
                sliceb.setStartAndSize(dim.getName(), sbStart, sbSize);
        } else if (foundDim->second.empty()) {
            for (SliceBuilder& sliceb : slices)
                sliceb.setStartAndSize(dim.getName(), sbStart, 0);
        } else {
            // handle slices in chunks
            const std::vector<std::size_t>& positions = foundDim->second;
            assert(positions.size() > sbStart);
            assert(positions.size() - sbStart >= sbSize);
            if (slices.size() <= 1) {
                // make chunks as large as possible for efficiency
                std::vector < std::pair<size_t, size_t> > chunks; // start,size
                size_t start = positions.at(sbStart), last = start;
                for (size_t i = sbStart + 1; i < sbStart + sbSize; ++i) {
                    const size_t positions_i = positions.at(i);
                    if (positions_i == last + 1) {
                        // make a larger continuous chunk
                        last = positions_i;
                    } else {
                        chunks.push_back(std::make_pair(start, last - start + 1));
                        start = last = positions_i;
                    }
                }
                chunks.push_back(std::make_pair(start, last - start + 1)); // chunks with start and size
                // create one slice for each chunk
                std::vector<SliceBuilder> newSlices;
                for (std::vector<std::pair<size_t, size_t> >::iterator chunksIt =
                        chunks.begin(); chunksIt != chunks.end(); ++chunksIt) {
                    for (SliceBuilder& sliceb : slices) {
                        SliceBuilder lsb = sliceb; // make a copy
                        lsb.setStartAndSize(dim.getName(), chunksIt->first, chunksIt->second);
                        newSlices.push_back(lsb);
                    }
                }
                slices = std::move(newSlices);
            } else {
                // chunks already splitted up, just add the relevant slices one by one
                std::vector<SliceBuilder> newSlices;
                for (size_t i = sbStart; i < sbStart + sbSize; ++i) {
                    for (SliceBuilder& sliceb : slices) {
                        SliceBuilder lsb = sliceb; // make a copy
                        lsb.setStartAndSize(dim.getName(), positions.at(i), 1);
                        newSlices.push_back(lsb);
                    }
                }
                slices = std::move(newSlices);
            }
        }
    }
    // read
    DataPtr data = joinSlices(dataReader_, varName, slices);
    return data;
}


DataPtr CDMExtractor::getDataSlice(const std::string& varName, size_t unLimDimPos)
{
    const CDMVariable& variable = cdm_->getVariable(varName);
    if (variable.hasData()) {
        // remove dimension makes sure that variables with dimensions requiring slicing
        // don't have in local in memory data, so return the memory data is save here
        return getDataSliceFromMemory(variable, unLimDimPos);
    }
    DataPtr data;
    if (dimSlices_.empty()) {
        // simple read
        data = dataReader_->getDataSlice(varName, unLimDimPos);
    } else {
        // translate unlimdim to SliceBuilder, fetch slices and join
        SliceBuilder sb(getCDM(), varName);
        const std::vector<std::string>& dimNames = sb.getDimensionNames();
        for (const std::string& dimName : dimNames) {
            const CDMDimension& dim = cdm_->getDimension(dimName);
            if (dim.isUnlimited()) {
                sb.setStartAndSize(dimName, unLimDimPos, 1);
            }
        }
        data = getDataSlice_(varName, sb);
     }
    // TODO: translate datatype where required
    return data;
}

DataPtr CDMExtractor::getDataSlice(const std::string& varName, const SliceBuilder& sb)
{
    const CDMVariable& variable = cdm_->getVariable(varName);
    if (variable.hasData()) {
        LOG4FIMEX(logger, Logger::DEBUG, "fetching data from memory");
        DataPtr data = variable.getData();
        if (data->size() == 0) {
            return data;
        } else {
            return data->slice(sb.getMaxDimensionSizes(), sb.getDimensionStartPositions(), sb.getDimensionSizes());
        }
    }

    if (dimSlices_.empty()) {
        // no further slicing of dimensions
        return dataReader_->getDataSlice(varName, sb);
    }
    return getDataSlice_(varName, sb);
}

void CDMExtractor::removeVariable(const std::string& variable)
{
    LOG4FIMEX(logger, Logger::DEBUG, "removing variable "<< variable);
    cdm_->removeVariable(variable);
}

void CDMExtractor::selectVariables(std::set<std::string> variables, bool addAuxiliaryVariables)
{
    using namespace std;
    if (addAuxiliaryVariables) {
        addAuxiliary(variables, getCDM(), listCoordinateSystems(this->dataReader_));
    }

    const CDM::VarVec& allVars = getCDM().getVariables();
    std::set<string> allVarNames;
    std::transform(allVars.begin(), allVars.end(), std::inserter(allVarNames, allVarNames.begin()), std::mem_fn(&CDMVariable::getName));

    // find the variables in one list, but not in the other
    set<string> difference;
    set_difference(allVarNames.begin(),
                   allVarNames.end(),
                   variables.begin(),
                   variables.end(),
                   inserter(difference, difference.begin()));

    // remove all unnecessary variables
    for_each(difference.begin(), difference.end(), [&](const std::string& r) { removeVariable(r); });

    // find variables selected, but not existing
    if (logger->isEnabledFor(Logger::WARN)) {
        set<string> missing;
        set_difference(variables.begin(), variables.end(), allVarNames.begin(), allVarNames.end(), inserter(missing, missing.begin()));
        for (const std::string& m : missing)
            LOG4FIMEX(logger, Logger::WARN, "selected variable '" << m << "' does not exist, ignoring");
    }
}

void CDMExtractor::reduceDimension(const std::string& dimName, const std::set<std::size_t>& slices)
{
    CDMDimension& dim = cdm_->getDimension(dimName);
    for (size_t sz : slices) {
        if (sz > dim.getLength())
            throw CDMException("can't select slice of dimension '" + dimName + "': " + type2string(sz) + " out of bounds: " + type2string(dim.getLength()));
    }

    // keep track of changes
    dim.setLength(slices.size());
    dimSlices_[dimName] = std::vector<size_t>(slices.begin(), slices.end());
    LOG4FIMEX(logger, Logger::DEBUG, "reducing dimension '" << dimName << "' to: " << join(slices.begin(), slices.end(), ","));

    // removing all data containing this dimension, just to be sure it's read from the dataReader_
    for (const CDMVariable& v : cdm_->getVariables()) {
        if (v.checkDimension(dim.getName())) {
            cdm_->getVariable(v.getName()).setData(DataPtr()); // v is const, need to get non-const variable
        }
    }
}

void CDMExtractor::reduceDimension(const std::string& dimName, size_t start, size_t length)
{
    std::set<std::size_t> useSlices;
    for (std::size_t i = 0; i < length; i++) {
        useSlices.insert(start+i);
    }
    reduceDimension(dimName, useSlices);
}

void CDMExtractor::reduceDimensionStartEnd(const std::string& dimName, size_t start, long long end)
{
    size_t length = 0;
    if (end > 0) {
        length = end - start + 1;
    } else {
        CDMDimension& dim = cdm_->getDimension(dimName);
        length = dim.getLength();
        length -= start;
        length += end;
    }
    reduceDimension(dimName, start, length);
}

void CDMExtractor::reduceAxes(const std::vector<CoordinateAxis::AxisType>& types, const std::string& aUnits, double startVal, double endVal)
{
    using namespace std;
    LOG4FIMEX(logger, Logger::DEBUG, "reduceAxes of "<< aUnits << "(" << startVal << "," << endVal <<")");

    Units units;
    CoordinateSystem_cp_v coordsys = listCoordinateSystems(dataReader_);
    const CDM& cdm = dataReader_->getCDM();
    CoordinateAxis_cp_v vAxes;
    for (CoordinateSystem_cp_v::const_iterator cs = coordsys.begin(); cs != coordsys.end(); ++cs) {
        for (vector<CoordinateAxis::AxisType>::const_iterator vType = types.begin(); vType != types.end(); ++vType) {
            CoordinateAxis_cp vAxis = (*cs)->findAxisOfType(*vType);
            if (vAxis.get() != 0) {
                string vaUnits = cdm.getUnits(vAxis->getName());
                if (units.areConvertible(vaUnits, aUnits)) {
                    vAxes.push_back(vAxis);
                }
            }
        }
    }

    set<string> usedDimensions;
    for (CoordinateAxis_cp_v::const_iterator va = vAxes.begin(); va != vAxes.end(); ++va) {
        const vector<string>& shape = (*va)->getShape();
        if (shape.size() != 1) {
            LOG4FIMEX(logger, Logger::WARN, "cannot reduce axis '" << (*va)->getName() << "': axis is not 1-dim");
        } else if (usedDimensions.find(shape[0]) == usedDimensions.end()) {
            // set usedDimensions to not process dimension again
            usedDimensions.insert(shape[0]);
            DataPtr vData = dataReader_->getScaledData((*va)->getName());
            if (vData->size() > 0) {
                auto vArray = vData->asDouble();
                // calculate everything in the original unit
                string vaUnits = cdm.getUnits((*va)->getName());
                double offset,slope;
                units.convert(aUnits, vaUnits, slope, offset);
                double roundingDelta = 1e-5;
                if (vData->size() > 1 && vArray[0] != vArray[1]) {
                    // make a relative rounding delta
                    roundingDelta = .01 * fabs(vArray[0] - vArray[1]);
                }
                double startValX = startVal*slope + offset - roundingDelta;
                double endValX = endVal*slope + offset + roundingDelta;
                LOG4FIMEX(logger, Logger::DEBUG, "reduceAxes of " << (*va)->getName() << " after unit-conversion (slope,offset="<<slope<<","<<offset<<"): ("<< startValX << ","<< endValX<<")");

                // find start and end time in time-axis
                // make sure data is growing
                bool isReverse = false;
                if ((vData->size() > 1) && (vArray[0] > vArray[1])) {
                    isReverse = true;
                    reverse(&vArray[0], &vArray[0] + vData->size());
                }

                // vArray assumed to be monotonic growing
                double* lower = lower_bound(&vArray[0], &vArray[0] + vData->size(), startValX); // val included
                double* upper = upper_bound(&vArray[0], &vArray[0] + vData->size(), endValX);   // val excluded

                if (upper == (&vArray[0] + vData->size())) {
                    LOG4FIMEX(logger, Logger::DEBUG, "reduceAxes found lower,upper ("<< *lower << ",end)");
                } else {
                    LOG4FIMEX(logger, Logger::DEBUG, "reduceAxes found lower,upper ("<< *lower << ","<< *upper <<")");
                }


                // reduce dimension according to these points (name, startPos, size)
                long startPos = distance(&vArray[0], lower);
                long endPos = distance(&vArray[0], upper);
                size_t size = std::max(endPos - startPos, 0l);
                if (isReverse) {
                    startPos = vData->size() - size - startPos;
                    LOG4FIMEX(logger, Logger::DEBUG, "reduceAxis on reverse data, new (start,size) = ("<<startPos<<","<<size<<")" );
                    // reverse data back for possible later usage
                    reverse(&vArray[0], &vArray[0] + vData->size());
                }
                if (dimSlices_.find(shape[0]) == dimSlices_.end()) {
                    LOG4FIMEX(logger, Logger::DEBUG, "reducing axes-dimension "<< shape[0] << " from: " << startPos << " size: " << size);
                    reduceDimension(shape[0], startPos, size);
                } else {
                    LOG4FIMEX(logger, Logger::DEBUG, "not reducing axes-dimension "<< shape[0] << ": already done earlier");
                }

            }
        }
    }
}

void CDMExtractor::reduceTime(const FimexTime& startTime, const FimexTime& endTime)
{
    const std::string unit = "seconds since 1970-01-01 00:00:00";
    const TimeUnit tu(unit);
    const double startVal = tu.fimexTime2unitTime(startTime, std::numeric_limits<double>::lowest());
    const double endVal = tu.fimexTime2unitTime(endTime, std::numeric_limits<double>::max());
    reduceAxes({CoordinateAxis::Time}, unit, startVal, endVal);
}

void CDMExtractor::reduceVerticalAxis(const std::string& units, double startVal, double endVal)
{
    std::vector<CoordinateAxis::AxisType> types;
    types.push_back(CoordinateAxis::GeoZ);
    types.push_back(CoordinateAxis::Height);
    types.push_back(CoordinateAxis::Depth);
    types.push_back(CoordinateAxis::Pressure);
    reduceAxes(types, units, startVal, endVal);
}

void CDMExtractor::reduceLatLonBoundingBox(double south, double north, double west, double east)
{
    using namespace std;
    // check input
    if (south > north) throw CDMException("reduceLatLonBoundingBox south > north: "+type2string(south) + ">" +type2string(north));

    if (south < -90. || south > 90) throw CDMException("reduceLatLonBoundingBox south outside domain: " + type2string(south));
    if (north < -90. || north > 90) throw CDMException("reduceLatLonBoundingBox north outside domain: " + type2string(north));
    if (west < -180. || west > 180) throw CDMException("reduceLatLonBoundingBox west outside domain: " + type2string(west));
    if (east < -180. || east > 180) throw CDMException("reduceLatLonBoundingBox east outside domain: " + type2string(east));

    const bool wrap180 = (west > east);

    // find coordinate-systems
    CoordinateSystem_cp_v coordsys = listCoordinateSystems(dataReader_);
    set<string> convertedAxes;
    for (CoordinateSystem_cp_v::const_iterator ics = coordsys.begin(); ics != coordsys.end(); ++ics) {
        CoordinateSystem_cp cs = *ics;

        if (!cs->isSimpleSpatialGridded())
            continue;

        if (!cs->hasProjection())
            continue;

        const string& xAxisName = cs->getGeoXAxis()->getName();
        const string& yAxisName = cs->getGeoYAxis()->getName();
        // check if the axes have already been processed by another cs
        if (convertedAxes.find(xAxisName) != convertedAxes.end()
                || convertedAxes.find(yAxisName) != convertedAxes.end())
            continue;

        const vector<string>& shapeX = cs->getGeoXAxis()->getShape();
        const vector<string>& shapeY = cs->getGeoYAxis()->getShape();
        if (shapeX.size() != 1 || shapeY.size() != 1) {
            LOG4FIMEX(logger, Logger::WARN, "cannot reduce x/y axis: axis is not 1-dim");
            continue;
        }

        DataPtr xData = dataReader_->getScaledData(xAxisName);
        DataPtr yData = dataReader_->getScaledData(yAxisName);
        const size_t nx = xData->size(), ny = yData->size();
        if (nx == 0 || ny == 0)
            continue;

        // reproject grid to lon-lat
        auto xArray = xData->asDouble();
        auto yArray = yData->asDouble();
        vector<double> xLonVals(nx*ny), yLatVals(nx*ny);
        for (size_t ix = 0, i = 0; ix < nx; ++ix) {
            for (size_t iy = 0; iy < ny; ++iy, ++i) {
                xLonVals[i] = xArray[ix];
                yLatVals[i] = yArray[iy];
            }
        }
        cs->getProjection()->convertToLonLat(xLonVals, yLatVals);

        // keep x and y indices when grid point is inside bbox
        std::set<std::size_t> xSlices, ySlices;
        for (size_t ix = 0, i = 0; ix < nx; ++ix) {
            for (size_t iy = 0; iy < ny; ++iy, ++i) {
                const double lon = xLonVals[i], lat = yLatVals[i];
                if (lat < south || lat > north)
                    continue;
                if (wrap180 && lon > east && lon < west)
                    continue;
                else if (!wrap180 && (lon < west || lon > east))
                    continue;

                xSlices.insert(ix);
                ySlices.insert(iy);
            }
        }
        reduceDimension(xAxisName, xSlices);
        reduceDimension(yAxisName, ySlices);

        // avoid double conversion
        convertedAxes.insert(xAxisName);
        convertedAxes.insert(yAxisName);
    }
}

} // end of namespace
