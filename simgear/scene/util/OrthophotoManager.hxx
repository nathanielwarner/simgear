// OrthophotoManager.hxx -- manages satellite orthophotos
//
// Copyright (C) 2020  Nathaniel MacArthur-Warner nathanielwarner77@gmail.com
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Library General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Library General Public License for more details.
//
// You should have received a copy of the GNU Library General Public
// License along with this library; if not, write to the
// Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
// Boston, MA  02110-1301, USA.

#ifndef SG_SCENE_ORTHOPHOTO_MANAGER
#define SG_SCENE_ORTHOPHOTO_MANAGER

#include <osg/Referenced>
#include <osg/Image>
#include <osgDB/ReaderWriter>
#include <osgDB/ReadFile>
#include <simgear/misc/sg_path.hxx>
#include <simgear/bucket/newbucket.hxx>
#include "OsgSingleton.hxx"

namespace simgear {
    class OrthophotoManager : public osg::Referenced {
    private:
        std::deque<SGPath> sceneryPaths;
    public:
        static OrthophotoManager* instance();
        void addSceneryPath(const SGPath path);
        void clearSceneryPaths();
        void getOrthophoto(double min_lon, double max_lon, double min_lat, double max_lat, osg::ref_ptr<osg::Image>& orthophoto, 
                           double& ortho_min_lon, double& ortho_max_lon, double& ortho_min_lat, double& ortho_max_lat);
    };
}

#endif