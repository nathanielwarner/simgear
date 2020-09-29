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
        double minLon;
        double maxLon;
        double minLat;
        double maxLat;

        static OrthophotoBounds fromBucket(SGBucket bucket);

        OrthophotoBounds();
        void expandToInclude(double lon, double lat);
    };

    class Orthophoto : public osg::Referenced {
    private:
        osg::ref_ptr<osg::Texture2D> _texture;
        OrthophotoBounds _bbox;
        void init(ImageRef& image, OrthophotoBounds bbox);
    public:
        Orthophoto(ImageRef& image, OrthophotoBounds bbox);
        Orthophoto(ImageRefCollection2d& images, OrthophotoBounds bbox);
        osg::ref_ptr<osg::Texture2D> getTexture();
        OrthophotoBounds getBbox();
    };

    class OrthophotoManager : public osg::Referenced {
    private:
        std::deque<SGPath> _sceneryPaths;
        std::unordered_map<long, ImageRef> _bucketImages;
        ImageRef getBucketImage(SGBucket bucket);
    public:
        static OrthophotoManager* instance();
        void addSceneryPath(const SGPath path);
        void clearSceneryPaths();
        osg::ref_ptr<Orthophoto> getOrthophoto(long bucket_index);
        osg::ref_ptr<Orthophoto> getOrthophoto(OrthophotoBounds desired_bbox);
    };
}

#endif