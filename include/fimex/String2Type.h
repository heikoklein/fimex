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

#ifndef FIMEX_STRING2TYPE_H_
#define FIMEX_STRING2TYPE_H_

#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace MetNoFimex {

// based on https://stackoverflow.com/a/1055563
template <typename T>
struct TypeNameTraits;
#define TYPENAMETRAITS2(X, Y) template <> struct TypeNameTraits<X> \
    { static std::string name() { return Y; } };
#define TYPENAMETRAITS1(X) TYPENAMETRAITS2(X, #X)
TYPENAMETRAITS1(char);
TYPENAMETRAITS1(short);
TYPENAMETRAITS1(int);
TYPENAMETRAITS1(long);
TYPENAMETRAITS1(long long);
TYPENAMETRAITS1(unsigned char);
TYPENAMETRAITS1(unsigned int);
TYPENAMETRAITS1(unsigned short);
TYPENAMETRAITS1(unsigned long);
TYPENAMETRAITS1(unsigned long long);
TYPENAMETRAITS1(double);
TYPENAMETRAITS1(float);
#undef TYPENAMETRAITS1
#undef TYPENAMETRAITS2

class string2type_error : public std::runtime_error {
public:
    string2type_error(const std::string& t, const std::string& tn, const std::string& info = std::string())
        : std::runtime_error("could not parse all of '" + t + "' as " + tn + info)
        , text(t), type_name(tn) {}
    const std::string text, type_name;
};

template <typename T, typename R = T>
T string2type(const std::string& s)
{
    T retVal;
    bool ok = !s.empty();

    if (std::is_arithmetic<T>() && ok) {
        if (std::is_floating_point<T>() && s == "nan") {
            return std::numeric_limits<T>::quiet_NaN();
        }
        const char c = s[0];
        ok = (c == '-') || (std::is_floating_point<T>() && c == '.') || std::isdigit(c);
    }
    if (ok) {
        std::istringstream buffer(s);
        buffer >> retVal;
        ok = buffer.eof() && !buffer.fail();
    }
    if (!ok)
        throw string2type_error(s, TypeNameTraits<R>::name());
    return retVal;
}

//! convert char to digits, not bytes
template <>
inline char string2type(const std::string& s)
{
    return static_cast<char>(string2type<int, char>(s));
}

//! convert unsigned char to digits, not bytes
template <>
inline unsigned char string2type(const std::string& s)
{
    return static_cast<unsigned char>(string2type<unsigned int, unsigned char>(s));
}

//! no conversion for std::string
template <>
inline std::string string2type<std::string>(const std::string& s)
{
    return s;
}

//! recognize on,true,1 as true and off,false,0 as false
template <>
bool string2type<bool>(const std::string& s);

template <typename T, typename R = T>
std::vector<T> strings2types(const std::vector<std::string>& values)
{
    std::vector<T> t;
    t.reserve(values.size());
    for (const std::string& v : values)
        t.push_back(string2type<T, R>(v));
    return t;
}

} // namespace MetNoFimex

#endif // FIMEX_STRING2TYPE_H_
