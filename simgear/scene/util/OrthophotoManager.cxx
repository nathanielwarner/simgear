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
    Orthophoto::Orthophoto(osg::ref_ptr<osg::Image>& image, SGRect<double>& bbox) {
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

    osg::ref_ptr<Orthophoto> OrthophotoManager::getOrthophoto(SGRect<double> rect) {
        
        SGRect<double> actual_bbox;
        actual_bbox.setLeft(180.0);
        actual_bbox.setBottom(90.0);
        actual_bbox.setRight(-180.0);
        actual_bbox.setTop(-90.0);

        double eps = SG_EPSILON * SGD_RADIANS_TO_DEGREES;
        rect.setLeft(rect.l() + eps);
        rect.setBottom(rect.b() + eps);
        rect.setRight(rect.r() - eps);
        rect.setTop(rect.t() - eps);

        // For now this works by identifying orthophotos by bucket index

        SGBucket bottom_left_bucket(SGGeod::fromDeg(rect.l(), rect.b()));
        augmentBoundingBox(actual_bbox, bottom_left_bucket);
        
        std::vector<SGBucket> buckets;
        buckets.push_back(bottom_left_bucket);

        if (buckets.size() == 1) {
            SGBucket bucket = buckets[0];
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

            if (image) {
                return new Orthophoto(image, actual_bbox);
            }
        }

        return nullptr;
        
    }
}
