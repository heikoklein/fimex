/*
 * Fimex, CDMTimeInterpolator.cpp
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
 *
 *  Created on: Dec 3, 2008
 *      Author: heikok
 */

#include "fimex/CDMTimeInterpolator.h"

#include "fimex/CDM.h"
#include "fimex/CDMException.h"
#include "fimex/Data.h"
#include "fimex/DataUtils.h"
#include "fimex/Logger.h"
#include "fimex/TimeSpec.h"
#include "fimex/Units.h"
#include "fimex/coordSys/CoordinateSystem.h"
#include "fimex/interpolation.h"

#include <algorithm>
#include <cassert>
#include <functional>
#include <set>
#include <utility>

namespace MetNoFimex {

using namespace std;

static Logger_p logger = getLogger("fimex.CDMTimeInterpolator");

// TODO: this function is mostly copied from CDM::getTimeAxis
//       CDM.h should provide a thread-safe version of getTimeAxis
static std::string getTimeAxis(const CoordinateSystem_cp_v& cs, const std::string& varName)
{
    // check if variable is its own axis (coord-axis don't have coordinate system)
    for (CoordinateSystem_cp c : cs) {
        CoordinateAxis_cp timeAxis = c->getTimeAxis();
        if (timeAxis && timeAxis->getName() == varName) {
            return varName;
        }
    }

    // search for coordinate system for varName
    if (CoordinateSystem_cp ccs = findCompleteCoordinateSystemFor(cs, varName)) {
        if (CoordinateAxis_cp axis = ccs->getTimeAxis())
            return axis->getName();
    }

    return std::string();
}

CDMTimeInterpolator::CDMTimeInterpolator(CDMReader_p dataReader)
   : dataReader_(dataReader)
{
    coordSystems_ = listCoordinateSystems(dataReader_);
    *cdm_ = dataReader_->getCDM();
    // removing all time-dependant data in cdm
    // just to be sure it's read from the dataReader_ or assigned in #changeTimeAxis
    for (const CDMVariable& var : cdm_->getVariables()) {
        const std::string timeDimName = getTimeAxis(coordSystems_, var.getName());
        if (!timeDimName.empty()) {
            cdm_->getVariable(var.getName()).setData(DataPtr());
        }
    }
}

CDMTimeInterpolator::~CDMTimeInterpolator()
{
}

DataPtr CDMTimeInterpolator::getDataSlice(const std::string& varName, size_t unLimDimPos)
{
    const std::string timeAxis = getTimeAxis(coordSystems_, varName);
    LOG4FIMEX(logger, Logger::DEBUG, "getting time-interpolated data-slice for " << varName << " with time-axis: " << timeAxis);
    if (timeAxis.empty() || (dataReaderTimesInNewUnits_.find(timeAxis)->second.size() == 0)) {
        // not time-axis or "changeTimeAxis" never called
        // no changes, simply forward
        return dataReader_->getDataSlice(varName, unLimDimPos);
    }

    const CDMVariable& variable = cdm_->getVariable(varName);
    if (variable.hasData()) {
        return getDataSliceFromMemory(variable, unLimDimPos);
    }

    // interpolate the data
    DataPtr data;

    // if unlimdim = time-axis, fetch all needed original slices
    const CDMDimension& timeDim = cdm_->getDimension(timeAxis);
    if (timeDim.isUnlimited()) {
        double currentTime = getDataSliceFromMemory(cdm_->getVariable(timeAxis), unLimDimPos)->asDouble()[0];
        // interpolate and return the time-slices
        pair<size_t, size_t> orgTimes = timeChangeMap_.find(timeAxis)->second.at(unLimDimPos);
        double d1Time = dataReaderTimesInNewUnits_.find(timeDim.getName())->second.at(orgTimes.first);
        double d2Time = dataReaderTimesInNewUnits_.find(timeDim.getName())->second.at(orgTimes.second);
        DataPtr d1 = dataReader_->getDataSlice(varName, orgTimes.first);
        DataPtr d2 = dataReader_->getDataSlice(varName, orgTimes.second);
        LOG4FIMEX(logger, Logger::DEBUG, "interpolation between " << d1Time << " and " << d2Time << " at " << currentTime);
        // convert if both slices are defined, otherwise, simply use the defined one or return undefined
        if (d1->size() == 0) {
            data = d2;
        } else if (d2->size() == 0) {
            data = d1;
        } else if (d1->size() == d2->size()) {
            auto out = make_shared_array<float>(d1->size());
            mifi_get_values_linear_weak_extrapol_f(d1->asFloat().get(), d2->asFloat().get(), out.get(), d1->size(), d1Time, d2Time, currentTime);
            data = createData(d1->size(), out);
        } else {
            throw CDMException("getDataSlice for " + varName + ": got slices with different size");
        }
    } else {
        // TODO
        // else get original slice, subslice the needed time-slices
        // interpolate and return the time-slices
        throw CDMException("TimeDimension != unlimited dimension not implemented yet in CDMTimeInterpolator");
    }
    return data;
}

void CDMTimeInterpolator::changeTimeAxis(const string& timeSpec)
{
    // changing time-axes
    const CDM& orgCDM = dataReader_->getCDM();
    const CDM::VarVec& vars = orgCDM.getVariables();
    set<string> changedTimes;
    for (CDM::VarVec::const_iterator varIt = vars.begin(); varIt != vars.end(); ++varIt) {
        // change all different time-axes
        const std::string timeDimName = orgCDM.getTimeAxis(varIt->getName());
        if (!timeDimName.empty() && changedTimes.find(timeDimName) == changedTimes.end()) {
            changedTimes.insert(timeDimName); // avoid double changes
            DataPtr times = dataReader_->getScaledData(timeDimName);
            string unit = cdm_->getUnits(timeDimName);
            const TimeUnit tu(unit);
            vector<FimexTime> oldTimes;
            auto oldTimesPtr = times->asDouble();
            size_t nEl = times->size();
            std::transform(oldTimesPtr.get(), oldTimesPtr.get() + nEl, std::back_inserter(oldTimes),
                           std::bind(std::mem_fn(&TimeUnit::unitTime2fimexTime), tu, std::placeholders::_1));
            const TimeSpec ts(timeSpec, oldTimes[0], oldTimes[nEl - 1]);

            // create mapping of new time value positions to old time values (per time-axis)
            const vector<FimexTime>& newTimes = ts.getTimeSteps();
            size_t newTimePos = 0;
            size_t lastPos = 0;
            vector<pair<size_t, size_t> > timeMapping(newTimes.size());
            for (vector<FimexTime>::const_iterator it = newTimes.begin(); it != newTimes.end(); ++it, ++newTimePos) {
                vector<FimexTime>::iterator olb = lower_bound(oldTimes.begin() + lastPos, oldTimes.end(), *it);
                size_t pos = distance(oldTimes.begin(), olb);
                size_t t1, t2;
                if (pos == oldTimes.size()) {
                    // extrapolation at the end, starting with last and previous element
                    pos--;
                }
                t2 = pos;
                if (pos != 0) {
                    t1 = pos-1;
                } else { // extrapolation at beginning
                    t1 = pos;
                    if ((pos+1) != oldTimes.size()) {
                        t2 = pos+1;
                    } else {// only one element, t1 = t2, implicit
                        assert(oldTimes.size() == 1);
                    }
                }
                lastPos = pos; // make search faster, we know that the next lower_bound_pos will be >= pos
                timeMapping[newTimePos] = make_pair(t1,t2);
            }
            timeChangeMap_[timeDimName] = timeMapping;


            // change cdm timeAxis values
            cdm_->addOrReplaceAttribute(timeDimName, CDMAttribute("units", ts.getUnitString()));
            auto timeData = make_shared_array<double>(newTimes.size());
            const TimeUnit newTU(ts.getUnitString());
            std::transform(newTimes.begin(), newTimes.end(), timeData.get(), [newTU](const FimexTime& ft) { return newTU.fimexTime2unitTime(ft); });
            auto& timeVar = cdm_->getVariable(timeDimName);
            timeVar.setDataType(CDM_DOUBLE);
            timeVar.setData(createData(newTimes.size(), timeData));
            cdm_->getDimension(timeDimName).setLength(newTimes.size());

            // store old times with new unit as oldTimesNewUnits-vector
            Units u;
            double slope, offset;
            u.convert(ts.getUnitString(), unit, slope, offset);
            dataReaderTimesInNewUnits_[timeDimName].clear();
            transform(oldTimesPtr.get(),
                      oldTimesPtr.get() + nEl,
                      back_inserter(dataReaderTimesInNewUnits_[timeDimName]),
                      ScaleValue<double,double>(MIFI_UNDEFINED_D, 1., 0., cdm_->getFillValue(timeDimName), slope, offset));
        }
    }
}

} // namespace MetNoFimex
