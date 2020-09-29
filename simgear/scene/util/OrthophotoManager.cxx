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
    Orthophoto::Orthophoto(ImageRef& image, SGRectd bbox) {
        init(image, bbox);
    }

    Orthophoto::Orthophoto(ImageRefCollection2d& images, SGRectd bbox) {
        ImageRef& bottom_left_image = images[0][0];
        int bk_height = images.size();
        int bk_width = images[0].size();
        int single_height = bottom_left_image->t();
        int single_width = bottom_left_image->s();
        int px_width = bk_width * single_width;
        int px_height = bk_height * single_height;
        int px_depth = bottom_left_image->r();
        GLenum pixel_format = bottom_left_image->getPixelFormat();
        GLenum data_type = bottom_left_image->getDataType();
        int packing = bottom_left_image->getPacking();

        ImageRef image = new osg::Image();
        image->allocateImage(px_width, px_height, px_depth, pixel_format, data_type, packing);

        for (unsigned int vertical = 0; vertical < images.size(); vertical++) {
            for (unsigned int horiz = 0; horiz < images[vertical].size(); horiz++) {
                ImageRef& single_image = images[vertical][horiz];
                if (single_image) {
                    single_image->scaleImage(single_width, single_height, bottom_left_image->r());
                    image->copySubImage(horiz * single_width, (bk_height - 1 - vertical) * single_height, 0, single_image);
                }
            }
        }

        init(image, bbox);
    }

    void Orthophoto::init(ImageRef& image, SGRectd bbox) {
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

    SGRectd Orthophoto::getBbox() {
        return _bbox;
    }

    OrthophotoManager* OrthophotoManager::instance() {
        return SingletonRefPtr<OrthophotoManager>::instance();
    }

    void OrthophotoManager::addSceneryPath(const SGPath path) {
        for (const SGPath& existingPath : _sceneryPaths) {
            if (path == existingPath) {
                return;
            }
        }
        _sceneryPaths.push_front(path);
    }

    void OrthophotoManager::clearSceneryPaths() {
        _sceneryPaths.clear();
    }

    void augmentBoundingBox(SGRectd& bbox, SGBucket& new_bucket) {
        double center_lon = new_bucket.get_center_lon();
        double center_lat = new_bucket.get_center_lat();
        double width = new_bucket.get_width();
        double height = new_bucket.get_height();

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

    ImageRef OrthophotoManager::getBucketImage(SGBucket bucket) {
        long index = bucket.gen_index();

        ImageRef& image = _bucketImages[index];

        if (!image) {
            const std::string bucketPath = bucket.gen_base_path();

            for (const SGPath& sceneryPath : _sceneryPaths) {
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

    SGRectd OrthophotoManager::initBoundingBox() {
        SGRectd bbox;
        bbox.setLeft(180.0);
        bbox.setBottom(90.0);
        bbox.setRight(-180.0);
        bbox.setTop(-90.0);
        return bbox;
    }

    osg::ref_ptr<Orthophoto> OrthophotoManager::getOrthophoto(long bucket_index) {
        SGBucket bucket(bucket_index);
        ImageRef image = getBucketImage(bucket);

        if (!image)
            return nullptr;

        SGRectd bbox = initBoundingBox();
        augmentBoundingBox(bbox, bucket);
        
        return new Orthophoto(image, bbox);
    }

    osg::ref_ptr<Orthophoto> OrthophotoManager::getOrthophoto(SGRectd desired_bbox) {
        
        double eps = SG_EPSILON * SGD_RADIANS_TO_DEGREES;
        desired_bbox.setLeft(desired_bbox.l() + eps);
        desired_bbox.setBottom(desired_bbox.b() + eps);
        desired_bbox.setRight(desired_bbox.r() - eps);
        desired_bbox.setTop(desired_bbox.t() - eps);

        SGBucket bottom_left_bucket(SGGeod::fromDeg(desired_bbox.l(), desired_bbox.b()));
        const double bucket_width = bottom_left_bucket.get_width();
        ImageRef bottom_left_image = getBucketImage(bottom_left_bucket);

        if (!bottom_left_image)
            return nullptr;
        
        SGRectd actual_bbox = initBoundingBox();
        augmentBoundingBox(actual_bbox, bottom_left_bucket);

        // Simplest case - we already have the full orthophoto
        if (actual_bbox.r() > desired_bbox.r() && actual_bbox.t() > desired_bbox.t())
            return new Orthophoto(bottom_left_image, actual_bbox);
        

        // More complex case - we create a composite orthophoto


        int bk_width = 1;
        int bk_height = 1;

        ImageRefCollection2d images;
        
        ImageRefVec first_row_images;
        first_row_images.push_back(bottom_left_image);
        SGBucket current_bucket = bottom_left_bucket;
        while (actual_bbox.r() < desired_bbox.r()) {
            current_bucket = current_bucket.sibling(1, 0);
            ImageRef new_image = getBucketImage(current_bucket);
            first_row_images.push_back(new_image);
            augmentBoundingBox(actual_bbox, current_bucket);
            bk_width++;
        }
        images.push_back(first_row_images);

        current_bucket = bottom_left_bucket;
        while (actual_bbox.t() < desired_bbox.t()) {
            ImageRefVec row_images;
            current_bucket = current_bucket.sibling(0, 1);

            if (current_bucket.get_width() != bucket_width) {
                // We've crossed into territory that has different tile spans!
                // Currently, we don't handle this situation.
                return nullptr;
            }

            for (int i = 0; i < bk_width; i++) {
                SGBucket new_bucket = current_bucket.sibling(i, 0);
                ImageRef new_image = getBucketImage(new_bucket);
                row_images.push_back(new_image);
            }
            images.push_back(row_images);
            augmentBoundingBox(actual_bbox, current_bucket);
            bk_height++;
        }

        return new Orthophoto(images, actual_bbox);
    }
}
