/*

Copyright (c) 2013, Project OSRM, Dennis Luxen, others
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

Redistributions of source code must retain the above copyright notice, this list
of conditions and the following disclaimer.
Redistributions in binary form must reproduce the above copyright notice, this
list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef JSON_DESCRIPTOR_H_
#define JSON_DESCRIPTOR_H_

#include "BaseDescriptor.h"
#include "DescriptionFactory.h"
#include "../Algorithms/ObjectToBase64.h"
#include "../DataStructures/SegmentInformation.h"
#include "../DataStructures/TurnInstructions.h"
#include "../Util/Azimuth.h"
#include "../Util/StringUtil.h"

#include <boost/bind.hpp>
#include <boost/lambda/lambda.hpp>

#include <algorithm>

template<class DataFacadeT>
class JSONDescriptor : public BaseDescriptor<DataFacadeT> {
private:
    _DescriptorConfig config;
    DescriptionFactory descriptionFactory;
    DescriptionFactory alternateDescriptionFactory;
    FixedPointCoordinate current;
    unsigned numberOfEnteredRestrictedAreas;
    struct RoundAbout{
        RoundAbout() :
            startIndex(INT_MAX),
            nameID(INT_MAX),
            leaveAtExit(INT_MAX)
        {}
        int startIndex;
        int nameID;
        int leaveAtExit;
    } roundAbout;

    struct Segment {
        Segment() : nameID(-1), length(-1), position(-1) {}
        Segment(int n, int l, int p) : nameID(n), length(l), position(p) {}
        int nameID;
        int length;
        int position;
    };
    std::vector<Segment> shortestSegments, alternativeSegments;

    struct RouteNames {
        std::string shortestPathName1;
        std::string shortestPathName2;
        std::string alternativePathName1;
        std::string alternativePathName2;
    };

public:
    JSONDescriptor() : numberOfEnteredRestrictedAreas(0) {}
    void SetConfig(const _DescriptorConfig & c) { config = c; }

    void Run(
        http::Reply & reply,
        const RawRouteData &rawRoute,
        PhantomNodes &phantomNodes,
        const DataFacadeT * facade
    ) {

        WriteHeaderToOutput(reply.content);

        if(rawRoute.lengthOfShortestPath != INT_MAX) {
            descriptionFactory.SetStartSegment(phantomNodes.startPhantom);
            reply.content += "0,"
                    "\"status_message\": \"Found route between points\",";

            //Get all the coordinates for the computed route
            BOOST_FOREACH(const _PathData & pathData, rawRoute.computedShortestPath) {
                current = facade->GetCoordinateOfNode(pathData.node);
                descriptionFactory.AppendSegment(current, pathData );
            }
            descriptionFactory.SetEndSegment(phantomNodes.targetPhantom);
        } else {
            //We do not need to do much, if there is no route ;-)
            reply.content += "207,"
                    "\"status_message\": \"Cannot find route between points\",";
        }

        descriptionFactory.Run(facade, config.z);
        reply.content += "\"route_geometry\": ";
        if(config.geometry) {
            descriptionFactory.AppendEncodedPolylineString(reply.content, config.encodeGeometry);
        } else {
            reply.content += "[]";
        }

        reply.content += ","
                "\"route_instructions\": [";
        numberOfEnteredRestrictedAreas = 0;
        if(config.instructions) {
            BuildTextualDescription(descriptionFactory, reply, rawRoute.lengthOfShortestPath, facade, shortestSegments);
        } else {
            BOOST_FOREACH(const SegmentInformation & segment, descriptionFactory.pathDescription) {
                TurnInstruction currentInstruction = segment.turnInstruction & TurnInstructions.InverseAccessRestrictionFlag;
                numberOfEnteredRestrictedAreas += (currentInstruction != segment.turnInstruction);
            }
        }
        reply.content += "],";
        descriptionFactory.BuildRouteSummary(descriptionFactory.entireLength, rawRoute.lengthOfShortestPath - ( numberOfEnteredRestrictedAreas*TurnInstructions.AccessRestrictionPenalty));

        reply.content += "\"route_summary\":";
        reply.content += "{";
        reply.content += "\"total_distance\":";
        reply.content += descriptionFactory.summary.lengthString;
        reply.content += ","
                "\"total_time\":";
        reply.content += descriptionFactory.summary.durationString;
        reply.content += ","
                "\"start_point\":\"";
        reply.content += facade->GetEscapedNameForNameID(descriptionFactory.summary.startName);
        reply.content += "\","
                "\"end_point\":\"";
        reply.content += facade->GetEscapedNameForNameID(descriptionFactory.summary.destName);
        reply.content += "\"";
        reply.content += "}";
        reply.content +=",";

        //only one alternative route is computed at this time, so this is hardcoded

        if(rawRoute.lengthOfAlternativePath != INT_MAX) {
            alternateDescriptionFactory.SetStartSegment(phantomNodes.startPhantom);
            //Get all the coordinates for the computed route
            BOOST_FOREACH(const _PathData & pathData, rawRoute.computedAlternativePath) {
                current = facade->GetCoordinateOfNode(pathData.node);
                alternateDescriptionFactory.AppendSegment(current, pathData );
            }
            alternateDescriptionFactory.SetEndSegment(phantomNodes.targetPhantom);
        }
        alternateDescriptionFactory.Run(facade, config.z);

        //give an array of alternative routes
        reply.content += "\"alternative_geometries\": [";
        if(config.geometry && INT_MAX != rawRoute.lengthOfAlternativePath) {
            //Generate the linestrings for each alternative
            alternateDescriptionFactory.AppendEncodedPolylineString(reply.content, config.encodeGeometry);
        }
        reply.content += "],";
        reply.content += "\"alternative_instructions\":[";
        numberOfEnteredRestrictedAreas = 0;
        if(INT_MAX != rawRoute.lengthOfAlternativePath) {
            reply.content += "[";
            //Generate instructions for each alternative
            if(config.instructions) {
                BuildTextualDescription(
                    alternateDescriptionFactory,
                    reply,
                    rawRoute.lengthOfAlternativePath,
                    facade,
                    alternativeSegments
                );
            } else {
                BOOST_FOREACH(const SegmentInformation & segment, alternateDescriptionFactory.pathDescription) {
                	TurnInstruction currentInstruction = segment.turnInstruction & TurnInstructions.InverseAccessRestrictionFlag;
                    numberOfEnteredRestrictedAreas += (currentInstruction != segment.turnInstruction);
                }
            }
            reply.content += "]";
        }
        reply.content += "],";
        reply.content += "\"alternative_summaries\":[";
        if(INT_MAX != rawRoute.lengthOfAlternativePath) {
            //Generate route summary (length, duration) for each alternative
            alternateDescriptionFactory.BuildRouteSummary(alternateDescriptionFactory.entireLength, rawRoute.lengthOfAlternativePath - ( numberOfEnteredRestrictedAreas*TurnInstructions.AccessRestrictionPenalty));
            reply.content += "{";
            reply.content += "\"total_distance\":";
            reply.content += alternateDescriptionFactory.summary.lengthString;
            reply.content += ","
                    "\"total_time\":";
            reply.content += alternateDescriptionFactory.summary.durationString;
            reply.content += ","
                    "\"start_point\":\"";
            reply.content += facade->GetEscapedNameForNameID(descriptionFactory.summary.startName);
            reply.content += "\","
                    "\"end_point\":\"";
            reply.content += facade->GetEscapedNameForNameID(descriptionFactory.summary.destName);
            reply.content += "\"";
            reply.content += "}";
        }
        reply.content += "],";

        //Get Names for both routes
        RouteNames routeNames;
        GetRouteNames(shortestSegments, alternativeSegments, facade, routeNames);

        reply.content += "\"route_name\":[\"";
        reply.content += routeNames.shortestPathName1;
        reply.content += "\",\"";
        reply.content += routeNames.shortestPathName2;
        reply.content += "\"],"
                "\"alternative_names\":[";
        reply.content += "[\"";
        reply.content += routeNames.alternativePathName1;
        reply.content += "\",\"";
        reply.content += routeNames.alternativePathName2;
        reply.content += "\"]";
        reply.content += "],";
        //list all viapoints so that the client may display it
        reply.content += "\"via_points\":[";
        std::string tmp;
        if(config.geometry && INT_MAX != rawRoute.lengthOfShortestPath) {
            for(unsigned i = 0; i < rawRoute.segmentEndCoordinates.size(); ++i) {
                reply.content += "[";
                if(rawRoute.segmentEndCoordinates[i].startPhantom.location.isSet())
                    convertInternalReversedCoordinateToString(rawRoute.segmentEndCoordinates[i].startPhantom.location, tmp);
                else
                    convertInternalReversedCoordinateToString(rawRoute.rawViaNodeCoordinates[i], tmp);

                reply.content += tmp;
                reply.content += "],";
            }
            reply.content += "[";
            if(rawRoute.segmentEndCoordinates.back().startPhantom.location.isSet())
                convertInternalReversedCoordinateToString(rawRoute.segmentEndCoordinates.back().targetPhantom.location, tmp);
            else
                convertInternalReversedCoordinateToString(rawRoute.rawViaNodeCoordinates.back(), tmp);
            reply.content += tmp;
            reply.content += "]";
        }
        reply.content += "],";
        reply.content += "\"hint_data\": {";
        reply.content += "\"checksum\":";
        intToString(rawRoute.checkSum, tmp);
        reply.content += tmp;
        reply.content += ", \"locations\": [";

        std::string hint;
        for(unsigned i = 0; i < rawRoute.segmentEndCoordinates.size(); ++i) {
            reply.content += "\"";
            EncodeObjectToBase64(rawRoute.segmentEndCoordinates[i].startPhantom, hint);
            reply.content += hint;
            reply.content += "\", ";
        }
        EncodeObjectToBase64(rawRoute.segmentEndCoordinates.back().targetPhantom, hint);
        reply.content += "\"";
        reply.content += hint;
        reply.content += "\"]";
        reply.content += "},";
        reply.content += "\"transactionId\": \"OSRM Routing Engine JSON Descriptor (v0.3)\"";
        reply.content += "}";
    }

    // construct routes names
    void GetRouteNames(
        std::vector<Segment> & shortestSegments,
        std::vector<Segment> & alternativeSegments,
        const DataFacadeT * facade,
        RouteNames & routeNames
    ) {

        Segment shortestSegment1, shortestSegment2;
        Segment alternativeSegment1, alternativeSegment2;

        if(0 < shortestSegments.size()) {
            sort(shortestSegments.begin(), shortestSegments.end(), boost::bind(&Segment::length, _1) > boost::bind(&Segment::length, _2) );
            shortestSegment1 = shortestSegments[0];
            if(0 < alternativeSegments.size()) {
                sort(alternativeSegments.begin(), alternativeSegments.end(), boost::bind(&Segment::length, _1) > boost::bind(&Segment::length, _2) );
                alternativeSegment1 = alternativeSegments[0];
            }
            std::vector<Segment> shortestDifference(shortestSegments.size());
            std::vector<Segment> alternativeDifference(alternativeSegments.size());
            std::set_difference(shortestSegments.begin(), shortestSegments.end(), alternativeSegments.begin(), alternativeSegments.end(), shortestDifference.begin(), boost::bind(&Segment::nameID, _1) < boost::bind(&Segment::nameID, _2) );
            int size_of_difference = shortestDifference.size();
            if(0 < size_of_difference ) {
                int i = 0;
                while( i < size_of_difference && shortestDifference[i].nameID == shortestSegments[0].nameID) {
                    ++i;
                }
                if(i < size_of_difference ) {
                    shortestSegment2 = shortestDifference[i];
                }
            }

            std::set_difference(alternativeSegments.begin(), alternativeSegments.end(), shortestSegments.begin(), shortestSegments.end(), alternativeDifference.begin(), boost::bind(&Segment::nameID, _1) < boost::bind(&Segment::nameID, _2) );
            size_of_difference = alternativeDifference.size();
            if(0 < size_of_difference ) {
                int i = 0;
                while( i < size_of_difference && alternativeDifference[i].nameID == alternativeSegments[0].nameID) {
                    ++i;
                }
                if(i < size_of_difference ) {
                    alternativeSegment2 = alternativeDifference[i];
                }
            }
            if(shortestSegment1.position > shortestSegment2.position)
                std::swap(shortestSegment1, shortestSegment2);

            if(alternativeSegment1.position >  alternativeSegment2.position)
                std::swap(alternativeSegment1, alternativeSegment2);

            routeNames.shortestPathName1 = facade->GetEscapedNameForNameID(
                shortestSegment1.nameID
            );
            routeNames.shortestPathName2 = facade->GetEscapedNameForNameID(
                shortestSegment2.nameID
            );

            routeNames.alternativePathName1 = facade->GetEscapedNameForNameID(
                alternativeSegment1.nameID
            );
            routeNames.alternativePathName2 = facade->GetEscapedNameForNameID(
                alternativeSegment2.nameID
            );
        }
    }

    inline void WriteHeaderToOutput(std::string & output) {
        output += "{"
                "\"version\": 0.3,"
                "\"status\":";
    }

    //TODO: reorder parameters
    inline void BuildTextualDescription(
        DescriptionFactory & descriptionFactory,
        http::Reply & reply,
        const int lengthOfRoute,
        const DataFacadeT * facade,
        std::vector<Segment> & segmentVector
    ) {
        //Segment information has following format:
        //["instruction","streetname",length,position,time,"length","earth_direction",azimuth]
        //Example: ["Turn left","High Street",200,4,10,"200m","NE",22.5]
        //See also: http://developers.cloudmade.com/wiki/navengine/JSON_format
        unsigned prefixSumOfNecessarySegments = 0;
        roundAbout.leaveAtExit = 0;
        roundAbout.nameID = 0;
        std::string tmpDist, tmpLength, tmpDuration, tmpBearing, tmpInstruction;
        //Fetch data from Factory and generate a string from it.
        BOOST_FOREACH(const SegmentInformation & segment, descriptionFactory.pathDescription) {
        	TurnInstruction currentInstruction = segment.turnInstruction & TurnInstructions.InverseAccessRestrictionFlag;
            numberOfEnteredRestrictedAreas += (currentInstruction != segment.turnInstruction);
            if(TurnInstructions.TurnIsNecessary( currentInstruction) ) {
                if(TurnInstructions.EnterRoundAbout == currentInstruction) {
                    roundAbout.nameID = segment.nameID;
                    roundAbout.startIndex = prefixSumOfNecessarySegments;
                } else {
                    if(0 != prefixSumOfNecessarySegments){
                        reply.content += ",";
                    }
                    reply.content += "[\"";
                    if(TurnInstructions.LeaveRoundAbout == currentInstruction) {
                        intToString(TurnInstructions.EnterRoundAbout, tmpInstruction);
                        reply.content += tmpInstruction;
                        reply.content += "-";
                        intToString(roundAbout.leaveAtExit+1, tmpInstruction);
                        reply.content += tmpInstruction;
                        roundAbout.leaveAtExit = 0;
                    } else {
                        intToString(currentInstruction, tmpInstruction);
                        reply.content += tmpInstruction;
                    }


                    reply.content += "\",\"";
                    reply.content += facade->GetEscapedNameForNameID(segment.nameID);
                    reply.content += "\",";
                    intToString(segment.length, tmpDist);
                    reply.content += tmpDist;
                    reply.content += ",";
                    intToString(prefixSumOfNecessarySegments, tmpLength);
                    reply.content += tmpLength;
                    reply.content += ",";
                    intToString(segment.duration/10, tmpDuration);
                    reply.content += tmpDuration;
                    reply.content += ",\"";
                    intToString(segment.length, tmpLength);
                    reply.content += tmpLength;
                    reply.content += "m\",\"";
                    reply.content += Azimuth::Get(segment.bearing);
                    reply.content += "\",";
                    intToString(round(segment.bearing), tmpBearing);
                    reply.content += tmpBearing;
                    reply.content += "]";

                    segmentVector.push_back( Segment(segment.nameID, segment.length, segmentVector.size() ));
                }
            } else if(TurnInstructions.StayOnRoundAbout == currentInstruction) {
                ++roundAbout.leaveAtExit;
            }
            if(segment.necessary)
                ++prefixSumOfNecessarySegments;
        }
        if(INT_MAX != lengthOfRoute) {
            reply.content += ",[\"";
            intToString(TurnInstructions.ReachedYourDestination, tmpInstruction);
            reply.content += tmpInstruction;
            reply.content += "\",\"";
            reply.content += "\",";
            reply.content += "0";
            reply.content += ",";
            intToString(prefixSumOfNecessarySegments-1, tmpLength);
            reply.content += tmpLength;
            reply.content += ",";
            reply.content += "0";
            reply.content += ",\"";
            reply.content += "\",\"";
            reply.content += Azimuth::Get(0.0);
            reply.content += "\",";
            reply.content += "0.0";
            reply.content += "]";
        }
    }

};
#endif /* JSON_DESCRIPTOR_H_ */
