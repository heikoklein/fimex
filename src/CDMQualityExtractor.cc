/*
 * Fimex, CDMQualityExtractor.cc
 *
 * (C) Copyright 2009-2022, met.no
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
 *  Created on: May 25, 2009
 *      Author: Heiko Klein
 */

#include "fimex/CDMQualityExtractor.h"
#include "fimex/CDM.h"
#include "fimex/CDMFileReaderFactory.h"
#include "fimex/Data.h"
#include "fimex/Logger.h"
#include "fimex/String2Type.h"
#include "fimex/TokenizeDotted.h"
#include "fimex/XMLDoc.h"
#include "fimex/XMLUtils.h"
#include "fimex/mifi_constants.h"

#include <algorithm>
#include <regex>
#include <set>

using namespace std;

namespace MetNoFimex {

static Logger_p logger = getLogger("fimex.CDMQualityExtractor");

/**
 * find the variable which is quality-assured by the status-variable
 * @param cdm
 * @param statusVarName the status variable
 * @return the quality assured variables
 */
static vector<string> findCorrespondingVariables(const CDM& cdm, const string& statusVarName) {
    vector<string> correspondingVars;
    // the standard name of a status variable is: "corresponding_standard_name status_flag" as of CF-1.3
    CDMAttribute attr;
    if (cdm.getAttribute(statusVarName, "standard_name", attr)) {
        std::string standardName = attr.getData()->asString();
        vector<string> parts = tokenize(standardName, " ");
        if (parts.size() >=2) {
            string foreignStandardName = parts[0];
            vector<string> correspondingVars = cdm.findVariables("standard_name", foreignStandardName);
        } else {
            LOG4FIMEX(logger, Logger::WARN, "standard_name "+ standardName + "not suitabale for a status_flag in statusVariable "+ statusVarName);
        }
    } else {
        LOG4FIMEX(logger, Logger::INFO, "unable to find standard_name for status variable "+statusVarName + ": autoconfig not possible");
    }
    return correspondingVars;
}

static vector<double> getValidValues(const CDM& cdm, const string& statusVarName, const string& autoConfString, const string& statusAttrName) {
    vector<double> validVals;
    std::smatch matches;
    std::regex rgx("values=(.*)");
    if (std::regex_match(autoConfString, matches, rgx)) {
        validVals = tokenizeDotted<double>(matches[1], ",");
        if (validVals.size() == 0) {
            LOG4FIMEX(logger, Logger::WARN, "could not find values autoConfString: "+autoConfString);
        }
    } else {
        const CDMAttribute& attr = cdm.getAttribute(statusVarName, statusAttrName);
        auto validData = attr.getData()->asDouble();
        size_t validSize = attr.getData()->size();
        if (validSize > 0) {
            if (autoConfString == "all") {
                copy(&validData[0], &validData[validSize], back_inserter(validVals));
            } else if (autoConfString == "highest") {
                validVals[0] = validData[validSize-1];
            } else if (autoConfString == "lowest") {
                validVals[0] = validData[0];
            } else {
                LOG4FIMEX(logger, Logger::WARN, "unable to interpret autoConfString: "+autoConfString);
            }
        } else {
            LOG4FIMEX(logger, Logger::WARN, "could not find flag_values in status-variable: "+statusVarName);
        }
    }
    return validVals;
}

CDMQualityExtractor::CDMQualityExtractor(CDMReader_p dataReader, std::string autoConfString, std::string configFile)
: dataReader(dataReader)
{
    *cdm_.get() = dataReader->getCDM();
    const CDM& cdm = dataReader->getCDM();
    if (!autoConfString.empty()) {
        // precheck allowed autoConfStrings, in case no variable is found, but string
        // is wrong
        set<string> allowedString;
        allowedString.insert("all");
        allowedString.insert("highest");
        allowedString.insert("lowest");
        if (allowedString.find(autoConfString) == allowedString.end()) {
            throw CDMException("auto-configure-string '"+ autoConfString +"' unknown");
        }

        vector<string> vars = cdm.findVariables("flag_values",".*");
        for (vector<string>::iterator varIt = vars.begin(); varIt != vars.end(); ++varIt) {
            vector<string> correspondingVars = findCorrespondingVariables(cdm, *varIt);
            for (vector<string>::iterator cVarIt = correspondingVars.begin(); cVarIt != correspondingVars.end(); ++cVarIt) {
                statusVariable[*cVarIt] = *varIt;
                variableFill[*cVarIt] = cdm.getFillValue(*cVarIt);
                variableValues[*cVarIt] = getValidValues(cdm, *varIt, autoConfString, "flag_values");
            }
        }
        vars = cdm.findVariables("flag_masks", ".*");
        if (!vars.empty()) {
            // TODO: implement
            LOG4FIMEX(logger, Logger::INFO, "flag_masks not implemented yet");
        }
    }
    if (!configFile.empty()) {
        XMLDoc_p doc = std::make_shared<XMLDoc>(configFile);
        XPathNodeSet nodesQC(doc, "/cdmQualityConfig");
        if (nodesQC.size() != 1) {
            throw CDMException("config-file "+configFile+" does not contain cdmQualityConfig root element");
        }

        // loop over variables
        XPathNodeSet variables(doc, "/cdmQualityConfig/variable");
        for (auto node : variables) {
            string varName = getXmlProp(node, "name");
            string fillValStr = getXmlProp(node, "fillValue");
            XPathNodeSet statusVar(doc, "status_flag_variable", node);
            string statusVarName;
            string statusVarUse;
            string statusVarValues;
            string statusVarFile, statusVarType, statusVarConfig;
            if (statusVar.size() == 1) {
                auto statusNode = statusVar[0];
                statusVarName = getXmlProp(statusNode, "name");
                XPathNodeSet allowed(doc, "allowed_values", statusNode);
                if (allowed.size() == 1) {
                    statusVarUse = getXmlProp(allowed[0], "use");
                    auto valNode = allowed[0]->children;
                    if (valNode && (valNode->type == XML_TEXT_NODE)) {
                        statusVarValues = reinterpret_cast<char *>(valNode->content);
                    }
                }
                statusVarFile = getXmlProp(statusNode, "file");
                statusVarType = getXmlProp(statusNode, "type");
                statusVarConfig = getXmlProp(statusNode, "config");
            }
            if (statusVarName.empty())
                throw CDMException("could not find status_flag_variable for var: " + varName);
            if (statusVarFile.empty() && (!statusVarConfig.empty() || !statusVarType.empty()))
                throw CDMException("could not find status_flag_variable has type/config but no filename: " + varName);
            LOG4FIMEX(logger,Logger::DEBUG, "adding (variable,statusVar,use,vals): ("<<varName<<","<<statusVarName<<","<<statusVarUse<<","<<statusVarValues<<")");
            statusVariable[varName] = statusVarName;
            CDMReader_p sr = dataReader;
            if (!statusVarFile.empty()) {
                // status var from a different reader
                sr = CDMFileReaderFactory::create(statusVarType, statusVarFile, statusVarConfig);
                statusReaders[varName] = sr;
            }
            // simple check if shapes are compatible
            // TODO improve shape compatibility check
            LOG4FIMEX(logger,Logger::DEBUG, "getting shape for variable '" << varName << "'");
            vector<string> shapeVar;
            if (cdm.hasVariable(varName)) {
                shapeVar = cdm_->getVariable(varName).getShape();
            }
            LOG4FIMEX(logger,Logger::DEBUG, "getting shape for status variable '" << statusVarName << "'");
            vector<string> shapeStatus;
            if (sr->getCDM().hasVariable(statusVarName)) {
                shapeStatus = sr->getCDM().getVariable(statusVarName).getShape();
            }
            if (cdm.hasVariable(varName) && (shapeVar != shapeStatus)) {
                if (shapeVar.size() < shapeStatus.size())
                    throw CDMException("external status variable '" + statusVarName + "' has more dimensions than variable '" + varName + "'");
                for (size_t i=0; i<shapeStatus.size(); ++i) {
                    if (cdm.getDimension(shapeVar[i]).getLength() != sr->getCDM().getDimension(shapeStatus[i]).getLength())
                        throw CDMException("external status variable '" + statusVarName + "' and variable '"
                                           + varName + "' have different shapes, quality extract not implemented");
                }
                LOG4FIMEX(logger,Logger::INFO, "shapes of variable '" << varName << "' and statusVar '" << statusVarName
                          << "' are different but hopefully compatible");
            }
            variableFill[varName] = fillValStr.empty() ? cdm.getFillValue(varName) : string2type<double>(fillValStr);
            vector<double> statusVarVals = tokenizeDotted<double>(statusVarValues);
            if (!statusVarVals.empty()) {
                variableValues[varName] = statusVarVals;
            } else if (!statusVarUse.empty()) {
                variableFlags[varName] = statusVarUse;
            } else {
                throw CDMException("unable to quality-assure variable " + varName + ": no use or values given");
            }
        }
    }
}

static double findDefinedExtreme(double* begin, double* end, const double& (*minmax)(const double& a, const double& b))
{
    double extreme = MIFI_UNDEFINED_D;
    double* pos = begin;
    // forward to first defined element
    while ((pos != end) && (!std::isnan(*pos))) {
        ++pos;
    }
    if (pos == end) {
        extreme = MIFI_UNDEFINED_D;
    } else {
        extreme = *pos;
        while (pos != end) {
            if (!std::isnan(*pos)) {
                extreme = minmax(extreme, *pos);
            }
        }
    }
    return extreme;
}

DataPtr CDMQualityExtractor::getDataSlice(const std::string& varName, size_t unLimDimPos)
{
    // no change in cdm-data in CDMQualityExtractor, so no need to check for in-memory data

    DataPtr data = dataReader->getDataSlice(varName, unLimDimPos);
    // test if variable has quality assignment
    if (statusVariable.find(varName) != statusVariable.end()) {
        const string& statusVar = statusVariable[varName];
        DataPtr statusData;
        const std::map<std::string, CDMReader_p>::iterator sit = statusReaders.find(varName);
        CDMReader_p readerS = dataReader;
        if( sit != statusReaders.end() ) {
            readerS = sit->second;
            statusData = readerS->getDataSlice(statusVar, unLimDimPos);
            if (statusData->size() == 0) {
                statusData = readerS->getDataSlice(statusVar, 0); // get the default slice
            }
        } else if (statusVar == varName) {
            // reuse data in case of own status data
            statusData = data;
        } else {
            statusData = getDataSlice(statusVar, unLimDimPos); // reading statusData after applying QualityExtractor
            if (statusData->size() == 0) {
                statusData = getDataSlice(statusVar, 0); // get the default slice
            }
        }

        const CDM& cdmS = readerS->getCDM();
        const size_t sizeD = data->size(), sizeS = statusData->size();
        if (sizeD == 0 && sizeS == 0) {
            // special case: only undefined data
            const CDMVariable& var = cdm_->getVariable(varName);
            const vector<string>& shape = var.getShape();
            size_t length = 1;
            size_t sliceDims = cdm_->hasUnlimitedDim(var) ? shape.size()-1 : shape.size();
            for (size_t i = 0; i < sliceDims; ++i) {
                length *= cdm_->getDimension(shape.at(i)).getLength();
            }

            // return undefined data with new fill-value
            return createData(cdm_->getVariable(varName).getDataType(), length, variableFill[varName]);
        }
        const double sizeRatio = double(sizeD)/sizeS;
        if (sizeRatio == int(sizeRatio) && sizeRatio >= 1) {
            auto sd = statusData->asDouble();
            vector<double> useVals;
            if (variableValues.find(varName) != variableValues.end()) {
                useVals = variableValues[varName];
                sort(useVals.begin(), useVals.end());
            } else if (variableFlags.find(varName) != variableFlags.end()) {
                const string& flag = variableFlags[varName];
                double statusFill = cdmS.getFillValue(statusVar);
                double minFlag = MIFI_UNDEFINED_D;
                double maxFlag = MIFI_UNDEFINED_D;
                CDMAttribute attr;
                if (cdmS.getAttribute(statusVar, "valid_min", attr)) {
                    minFlag = attr.getData()->asDouble()[0];
                }
                if (cdmS.getAttribute(statusVar, "valid_max", attr)) {
                    maxFlag = attr.getData()->asDouble()[0];
                }
                if (cdmS.getAttribute(statusVar, "valid_range", attr)) {
                    minFlag = attr.getData()->asDouble()[0];
                    maxFlag = attr.getData()->asDouble()[1];
                }
                if (!std::isnan(minFlag)) {
                    double* sdIt = &sd[0];
                    while (sdIt != &sd[sizeS]) {
                        if (*sdIt < minFlag) {
                            *sdIt = MIFI_UNDEFINED_D;
                        }
                        sdIt++;
                    }
                }
                if (!std::isnan(maxFlag)) {
                    double* sdIt = &sd[0];
                    while (sdIt != &sd[sizeS]) {
                        if (*sdIt > maxFlag) {
                            *sdIt = MIFI_UNDEFINED_D;
                        }
                        sdIt++;
                    }
                }
                if (!std::isnan(statusFill)) {
                    double* sdIt = &sd[0];
                    while (sdIt != &sd[sizeS]) {
                        if (*sdIt == statusFill) {
                            *sdIt = MIFI_UNDEFINED_D;
                        }
                        sdIt++;
                    }
                }
                std::smatch match;
                if (flag == "all") {
                    // no more to do
                } else if (std::regex_match(flag, match, std::regex("max:(.+)"))) {
                    double max = string2type<double>(match[1]);
                    LOG4FIMEX(logger, Logger::DEBUG, "using max="<<max<<" for statusVar "<<statusVar<< " on var "<< varName);
                    double* sdIt = &sd[0];
                    while (sdIt != &sd[sizeS]) {
                        if (*sdIt > max) {
                            *sdIt = MIFI_UNDEFINED_D;
                        }
                        sdIt++;
                    }
                } else if (std::regex_match(flag, match, std::regex("min:(.+)"))) {
                    double min = string2type<double>(match[1]);
                    double* sdIt = &sd[0];
                    size_t count = 0;
                    while (sdIt != &sd[sizeS]) {
                        if (*sdIt < min) {
                            *sdIt = MIFI_UNDEFINED_D;
                            ++count;
                        }
                        sdIt++;
                    }
                    LOG4FIMEX(logger, Logger::DEBUG, "using min="<<min<<" for statusVar "<<statusVar<< " on var "<< varName << ": removed points: " << count);
                } else if (flag == "highest") {
                    double testVal = findDefinedExtreme(&sd[0], &sd[sizeS], &max<double>);
                    if (!std::isnan(testVal))
                        useVals.push_back(testVal);
                } else if (flag == "lowest") {
                    double testVal = findDefinedExtreme(&sd[0], &sd[sizeS], &min<double>);
                    if (!std::isnan(testVal))
                        useVals.push_back(testVal);
                } else {
                    throw CDMException("undefined quality-flag: "+flag+" for variable: "+varName);
                }
            }
            if (useVals.size() > 0) {
                // useVals are externally given flag-values or internally derived min/max
                double* sdIt = &sd[0];
                while (sdIt != &sd[sizeS]) {
                    if (!binary_search(useVals.begin(), useVals.end(), *sdIt)) {
                        *sdIt = MIFI_UNDEFINED_D;
                    }
                    sdIt++;
                }
            }
            double fillValue = variableFill[varName];
            for(size_t iD = 0; iD < sizeD; ) {
                double *sdIt = &sd[0];
                for (size_t iS = 0; iS < sizeS; ++iS, ++iD) {
                    if (std::isnan(*sdIt)) {
                        data->setValue(iD, fillValue);
                    }
                    ++sdIt;
                }
            }
        } else {
            LOG4FIMEX(logger, Logger::WARN, "incompatible size in data of variable and statusVariable at slice "<< unLimDimPos << ": "<<varName << ","<<statusVar<<": "<< data->size() << "<>" << statusData->size());
        }
    }
    return data;
}

}
