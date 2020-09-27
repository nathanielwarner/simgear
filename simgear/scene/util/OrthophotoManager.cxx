// OrthophotoManager.cxx -- manages satellite orthophotos
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

#include "OrthophotoManager.hxx"

namespace simgear {
    Orthophoto::Orthophoto(osg::ref_ptr<osg::Image>& image, SGRect<double> bbox) {
        _texture = new osg::Texture2D(image);
        _texture->setWrap(osg::Texture::WrapParameter::WRAP_S, osg::Texture::WrapMode::CLAMP_TO_EDGE);
        _texture->setWrap(osg::Texture::WrapParameter::WRAP_T, osg::Texture::WrapMode::CLAMP_TO_EDGE);
        _texture->setWrap(osg::Texture::WrapParameter::WRAP_R, osg::Texture::WrapMode::CLAMP_TO_EDGE);
        _texture->setMaxAnisotropy(SGSceneFeatures::instance()->getTextureFilter());
        _bbox = bbox;
    }

    osg::ref_ptr<osg::Texture2D> Orthophoto::getTexture() {
        return _texture;
    }

    SGRect<double> Orthophoto::getBbox() {
        return _bbox;
    }

    OrthophotoManager* OrthophotoManager::instance() {
        return SingletonRefPtr<OrthophotoManager>::instance();
    }

    void OrthophotoManager::addSceneryPath(const SGPath path) {
        for (SGPath existingPath : _sceneryPaths) {
            if (path == existingPath) {
                return;
            }
        }
        _sceneryPaths.push_front(path);
    }

    void OrthophotoManager::clearSceneryPaths() {
        _sceneryPaths.clear();
    }

    void augmentBoundingBox(SGRect<double>& bbox, SGBucket& newBucket) {
        double center_lon = newBucket.get_center_lon();
        double center_lat = newBucket.get_center_lat();
        double width = newBucket.get_width();
        double height = newBucket.get_height();
        double left = center_lon - width / 2;
        double right = center_lon + width / 2;
        double bottom = center_lat - height / 2;
        double top = center_lat + height / 2;

        if (bbox.l() > left)
            bbox.setLeft(left);
        if (bbox.r() < right)
            bbox.setRight(right);
        if (bbox.b() > bottom)
            bbox.setBottom(bottom);
        if (bbox.t() < top)
            bbox.setTop(top);
    }

    osg::ref_ptr<osg::Image> OrthophotoManager::getBucketImage(SGBucket bucket) {
        long index = bucket.gen_index();

        osg::ref_ptr<osg::Image>& image = _bucketImages[index];

        if (!image) {
            std::string bucketPath = bucket.gen_base_path();

            for (SGPath sceneryPath : _sceneryPaths) {
                SGPath path = sceneryPath / "Orthophotos" / bucketPath / std::to_string(index);
                path.concat(".png");
                if (path.exists()) {
                    image = osgDB::readRefImageFile(path.str());
                    break;
                }
            }
        }

        return image;
    }

    osg::ref_ptr<Orthophoto> OrthophotoManager::getOrthophoto(SGRect<double> desired_bbox) {
        
        SGRect<double> actual_bbox;
        actual_bbox.setLeft(180.0);
        actual_bbox.setBottom(90.0);
        actual_bbox.setRight(-180.0);
        actual_bbox.setTop(-90.0);

        double eps = SG_EPSILON * SGD_RADIANS_TO_DEGREES;
        desired_bbox.setLeft(desired_bbox.l() + eps);
        desired_bbox.setBottom(desired_bbox.b() + eps);
        desired_bbox.setRight(desired_bbox.r() - eps);
        desired_bbox.setTop(desired_bbox.t() - eps);

        SGBucket bottom_left_bucket(SGGeod::fromDeg(desired_bbox.l(), desired_bbox.b()));
        osg::ref_ptr<osg::Image> bottom_left_image = getBucketImage(bottom_left_bucket);

        if (!bottom_left_image)
            return nullptr;
        
        augmentBoundingBox(actual_bbox, bottom_left_bucket);

        // Simplest case - we already have the full orthophoto
        if (actual_bbox.r() > desired_bbox.r() && actual_bbox.t() > desired_bbox.t())
            return new Orthophoto(bottom_left_image, actual_bbox);
        

        // More complex case - we need to create a composite orthophoto
        
        /*osg::ref_ptr<osg::Image> image = new osg::Image();
        int new_width = bottom_left_image->s() * (desired_bbox.width() / actual_bbox.width());        // wrong wrong wrong
        int new_height = bottom_left_image->t() * (desired_bbox.height() / actual_bbox.height());
        image->allocateImage(new_width, new_height, bottom_left_image->r(), bottom_left_image->getPixelFormat(), 
                             bottom_left_image->getDataType(), bottom_left_image->getPacking());
        image->copySubImage(0, 0, 0, bottom_left_image);
        return new Orthophoto(image, desired_bbox); // not done!*/
        return nullptr;
    }
}
