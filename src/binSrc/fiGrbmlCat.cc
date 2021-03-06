/*
 * Fimex, fiGrbmlCat.cc
 *
 * (C) Copyright 2015, met.no
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
 *  Created on: Oct 31, 2015
 *      Author: heikok
 */

#include "fimex/XMLUtils.h"

#include <mi_programoptions.h>

#include <libxml/xmlreader.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <string>

using namespace std;
namespace po = miutil::program_options;

static void writeUsage(ostream& out, const po::option_set& options)
{
    out << "usage: fiGrbmlCat --outputFile=OUTFILE.grbml file1.grml [file2.grbml ...] [--inputFile=fileX.grbml] " << endl;
    out << endl;
    options.help(out);
}


int printNode(xmlTextReaderPtr reader, ostream& os) {
    bool isEmpty = xmlTextReaderIsEmptyElement(reader); // needs to be read before reading attributes!

    const xmlChar* nodeName = xmlTextReaderConstName(reader);
//    cerr << "printNode: " << nodeName << endl;
    os << "<" << nodeName;

    while (xmlTextReaderMoveToNextAttribute(reader) == 1) {
        MetNoFimex::XmlCharPtr name = xmlTextReaderName(reader);
        MetNoFimex::XmlCharPtr value = xmlTextReaderValue(reader);
        os << " " << name.to_cc() << "=\"" << value.to_cc() << "\"";
    }
    if (isEmpty) {
        os << " />";
        return 1;
    } else {
        os << ">";
    }

    int ret = xmlTextReaderRead(reader);
    // walk through nodes until end-element
    while (ret == 1) {
        int type = xmlTextReaderNodeType(reader);
//        cerr << "type: " << type << " " << xmlTextReaderConstName(reader) << " " << xmlTextReaderIsEmptyElement(reader) << endl;
        switch (type) {
        case XML_READER_TYPE_ELEMENT: printNode(reader, os);break;
        case XML_READER_TYPE_END_ELEMENT: {
                os << "</" << nodeName << ">";
                return ret;
        }
        default: break;
        }
        ret = xmlTextReaderRead(reader);
    }

    return ret;
}

static bool first = true;
void grbmlExtract(const string& fileName, ostream& os)
{
    xmlTextReaderPtr reader = xmlReaderForFile(fileName.c_str(), NULL, 0);
    if (reader != NULL) {
        std::shared_ptr<xmlTextReader> cleanupReader(reader, xmlFreeTextReader);
        const xmlChar* name;
        int ret = xmlTextReaderRead(reader);
        while (ret == 1) {
            //int depth = xmlTextReaderDepth(reader);
            int type = xmlTextReaderNodeType(reader);
            switch (type) {
            case XML_READER_TYPE_ELEMENT: {
                name = xmlTextReaderConstName(reader);
                if (name == NULL) name = reinterpret_cast<const xmlChar*>("");
                if (xmlStrEqual(name, reinterpret_cast<const xmlChar*>("gribFileIndex"))) {
                    if (first) {
                        MetNoFimex::XmlCharPtr url = xmlTextReaderGetAttribute(reader, reinterpret_cast<const xmlChar*>("url"));
                        os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << endl;
                        os << "<gribFileIndex url=\"" << url.to_cc() << "\" xmlns=\"http://www.met.no/schema/fimex/gribFileIndex\">" << endl;
                        first = false;
                    }
                } else if (xmlStrEqual(name, reinterpret_cast<const xmlChar*>("gribMessage"))) {
                    printNode(reader, os);
                }
                break;
            }
            case XML_READER_TYPE_END_ELEMENT: {
                name = xmlTextReaderConstName(reader);
                if (name == NULL) name = reinterpret_cast<const xmlChar*>("");
                if (xmlStrEqual(name, reinterpret_cast<const xmlChar*>("gribFileIndex"))) {
                    ret = 0;
                    continue; // leave while loop
                } else {
                    cerr << "unknown node: '" << name << "'" << endl;
                }
                break;
            }
            default: break; // only element nodes of interest
            }
            ret = xmlTextReaderRead(reader);
        }
        if (ret != 0) {
            cerr <<  fileName << ": failed to parse" << endl;
        }
    }
}

void extractToStream(std::ostream& out, const std::vector<std::string>& files)
{
    for (size_t i = 0; i < files.size(); ++i)
        grbmlExtract(files[i], out);
    if (!first)
        out << "</gribFileIndex>" << endl;
}

int main(int argc, char* args[])
{
    const po::option op_outputFile = po::option("outputFile", "output grbml").set_shortkey("o");
    const po::option op_inputFile = po::option("inputFile", "input grbml, possibly many").set_composing().set_shortkey("i");

    po::option_set options;
    options << op_outputFile << op_inputFile;

    // read the options
    po::string_v positional;
    po::value_set vm = po::parse_command_line(argc, args, options, positional);

    po::positional_args_consumer pac(vm, positional);
    while (!pac.done())
        pac >> op_inputFile;
    if (!vm.is_set(op_inputFile)) {
        options.dump(cerr, vm);
        pac.dump(cerr);
        return 1;
    }

    const vector<string>& files = vm.values(op_inputFile);
    const std::string& outputFile = vm.value(op_outputFile);
    if (outputFile != "-") {
        std::ofstream outputStream(outputFile, std::ios::binary);
        extractToStream(outputStream, files);
    } else {
        extractToStream(std::cout, files);
    }

    return 0;
}
