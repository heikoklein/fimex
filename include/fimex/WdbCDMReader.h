#ifndef WDBCDMREADER_H
#define WDBCDMREADER_H

#include "gridexer/GxWdbDataTypes.h"

// fimex
//
#include "fimex/CDMReader.h"
#include "fimex/CDMDimension.h"

// standard
//
#include <string>
#include <vector>

// boost
//
#include <boost/bimap.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

class GxWdbExplorer;

namespace MetNoFimex {

    class GxWdbCDMReader : public CDMReader
    {
    public:

        explicit GxWdbCDMReader(const std::string& source, const std::string& configfilename);

        void init() throw(CDMException);
        bool deinit();

        void setDbHost(const std::string& dbHost);
        void setDbName(const std::string& dbName);
        void setDbUser(const std::string& dbUser);
        void setDbPort(const unsigned int dbPort);

        std::string  dbHost() const;
        std::string  dbHost();
        std::string  dbName() const;
        std::string  dbName();
        std::string  dbUser() const;
        std::string  dbUser();
        unsigned int dbPort() const;
        unsigned int dbPort();
        std::string  wciUser() const;
        std::string  wciUser();
        std::string  connectString() const;
        std::string  connectString();

        void setWdbToCFNamesMap(const boost::bimap<std::string, std::string>& map);
        void addWdbToCFNames(const boost::bimap<std::string, std::string>& map);
        void addWdbNameToCFName(const std::string& wdbname, const std::string& cfname);

        void addWdbNameToFillValueMap(const boost::bimap<std::string, double>& map);
        void addWdbNameToFillValue(const std::string& wdbname, const double value);

        virtual boost::shared_ptr<Data> getDataSlice(const std::string& varName, size_t unLimDimPos) throw(CDMException);

    protected:
        boost::shared_ptr<GxWdbExplorer> wdbExplorer()  {
            return wdbExplorer_;
        }

        boost::shared_ptr<GxWdbExplorer> wdbExplorer() const {
            return wdbExplorer();
        }

        void setWdbExplorer(const boost::shared_ptr<GxWdbExplorer>& wdbExplorer) {
            wdbExplorer_ = wdbExplorer;
        }

        bool addDataProvider();
        bool addPlace();
        void addGlobalCDMAttributes();
        CDMDimension addTimeDimension();
        std::map<short, CDMDimension> addLevelDimensions();
        std::string getStandardNameForDimension(const std::string& name);
        // returning projName and coordinates for given place name
        boost::tuple<std::string, std::string> addProjection(const std::string& strplace);
        void addVariables(const std::string& projName, const std::string& coordinates, const CDMDimension& timeDim, const std::map<short, CDMDimension>& levelDims);

    private:
        std::string                         source_;
        std::string                         configFileName_;
        boost::shared_ptr<GxWdbExplorer>    wdbExplorer_;

        // data to build the reader on
        // todo: use smart pointers
        CDMDimension xDim;
        CDMDimension yDim;
        boost::bimap<std::string, std::string> wdbtocfnamesmap_;
        boost::bimap<std::string, double> wdbnametofillvaluemap_;
        std::vector<GxDataProviderRow> providers_; // we support only one ATM
        std::vector<GxPlaceRow> places_; // we support only one ATM
        std::vector<GxLevelParameterRow> levelparameters_;
        std::vector<GxValueParameterRow> valueparameters_;
        std::vector<GxValidTimeRow> validtimes_; // this should be UNLIMITED dimension
        std::vector<std::pair<boost::posix_time::ptime, boost::posix_time::ptime> > timeVec;
        std::map<std::string,  std::vector<std::pair<double, double> > > levelNamesToPairsMap;
    };

} // end namespace

#endif // WDBCDMREADER_H