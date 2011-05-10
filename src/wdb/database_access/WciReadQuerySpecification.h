/*
 fimex

 Copyright (C) 2011 met.no

 Contact information:
 Norwegian Meteorological Institute
 Box 43 Blindern
 0313 OSLO
 NORWAY
 E-mail: post@met.no

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 MA  02110-1301, USA
 */

#ifndef WCIREADQUERYSPECIFICATION_H_
#define WCIREADQUERYSPECIFICATION_H_

#include <boost/date_time/posix_time/ptime.hpp>
#include <set>
#include <string>

namespace MetNoFimex
{

namespace wdb
{

class WciReadQuerySpecification
{
public:
	WciReadQuerySpecification();

	std::string query() const;

	typedef boost::posix_time::ptime Time;

	void addDataProvider(const std::string & dataProvider);
	void setLocation(const std::string & location);
	void setReferenceTime(const Time & referenceTime);
	void setReferenceTime(const std::string & referenceTime);
	void addParameter(const std::string & parameter);
	void addDataVersion(int version);

	const std::set<std::string> * dataProvider() const { return dataProvider_.empty() ? 0 : & dataProvider_; };
	const std::string * location() const { return location_.empty() ? 0 : & location_; };
	const Time * referenceTime() const { return referenceTime_.is_not_a_date_time() ? 0 : & referenceTime_; };
	const std::set<std::string> * parameter() const { return parameter_.empty() ? 0 : & parameter_; };
	const std::set<int> * dataVersion() const { return dataVersion_.empty() ? 0 : & dataVersion_; };


private:
	std::set<std::string> dataProvider_;
	std::string location_;
	Time referenceTime_;
	std::set<std::string> parameter_;
	std::set<int> dataVersion_;
};

}

}

#endif /* WCIREADQUERYSPECIFICATION_H_ */
