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

#include <unordered_map>

#include <osg/Referenced>
#include <osg/Image>
#include <osg/Texture2D>
#include <osgDB/ReaderWriter>
#include <osgDB/ReadFile>
#include <simgear/misc/sg_path.hxx>
#include <simgear/bucket/newbucket.hxx>
#include "SGSceneFeatures.hxx"
#include "OsgSingleton.hxx"

namespace simgear {

    using ImageRef = osg::ref_ptr<osg::Image>;
    using ImageRefVec = std::vector<ImageRef>;
    using ImageRefCollection2d = std::vector<ImageRefVec>;

    struct OrthophotoBounds {
        double minLon = 180.0;
        double maxLon = -180.0;
        double minLat = 90.0;
        double maxLat = -90.0;

        static OrthophotoBounds fromBucket(const SGBucket& bucket);

        void expandToInclude(const double lon, const double lat);
        void absorb(const OrthophotoBounds& bounds);
    };

    class Orthophoto : public osg::Referenced {
    private:
        ImageRef _image;
        OrthophotoBounds _bbox;
    public:
        Orthophoto(const ImageRef& image, const OrthophotoBounds& bbox);
        Orthophoto(const std::vector<osg::ref_ptr<Orthophoto>>& orthophotos);
        osg::ref_ptr<osg::Texture2D> getTexture();
        OrthophotoBounds getBbox() const { return _bbox; };
    };

    class OrthophotoManager : public osg::Referenced {
    private:
        std::deque<SGPath> _sceneryPaths;
        std::unordered_map<long, ImageRef> _bucketImages;
        ImageRef getBucketImage(const SGBucket& bucket);
    public:
        static OrthophotoManager* instance();
        void addSceneryPath(const SGPath& path);
        void clearSceneryPaths();
        osg::ref_ptr<Orthophoto> getOrthophoto(const SGBucket& bucket);
        osg::ref_ptr<Orthophoto> getOrthophoto(const std::vector<SGVec3d>& nodes, const SGVec3d& center);
    };
}

#endif