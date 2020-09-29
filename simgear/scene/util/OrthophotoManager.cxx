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

    OrthophotoBounds::OrthophotoBounds() {
        minLon = 180.0;
        maxLon = -180.0;
        minLat = 90.0;
        maxLat = -90.0;
    }
    
    void OrthophotoBounds::expandToInclude(const double lon, const double lat) {
        if (lon < minLon)
            minLon = lon;
        if (lon > maxLon)
            maxLon = lon;
        if (lat < minLat)
            minLat = lat;
        if (lat > maxLat)
            maxLat = lat;
    }

    Orthophoto::Orthophoto(const ImageRef& image, const OrthophotoBounds& bbox) {
        init(image, bbox);
    }

    Orthophoto::Orthophoto(ImageRefCollection2d& images, const OrthophotoBounds& bbox) {
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

    void Orthophoto::init(const ImageRef& image, const OrthophotoBounds& bbox) {
        _texture = new osg::Texture2D(image);
        _texture->setWrap(osg::Texture::WrapParameter::WRAP_S, osg::Texture::WrapMode::CLAMP_TO_EDGE);
        _texture->setWrap(osg::Texture::WrapParameter::WRAP_T, osg::Texture::WrapMode::CLAMP_TO_EDGE);
        _texture->setWrap(osg::Texture::WrapParameter::WRAP_R, osg::Texture::WrapMode::CLAMP_TO_EDGE);
        _texture->setMaxAnisotropy(SGSceneFeatures::instance()->getTextureFilter());
        _bbox = bbox;
    }

    OrthophotoManager* OrthophotoManager::instance() {
        return SingletonRefPtr<OrthophotoManager>::instance();
    }

    void OrthophotoManager::addSceneryPath(const SGPath& path) {
        for (const auto& existingPath : _sceneryPaths) {
            if (path == existingPath) {
                return;
            }
        }
        _sceneryPaths.push_front(path);
    }

    void OrthophotoManager::clearSceneryPaths() {
        _sceneryPaths.clear();
    }

    void augmentBoundingBox(OrthophotoBounds& bbox, const SGBucket& new_bucket) {
        double center_lon = new_bucket.get_center_lon();
        double center_lat = new_bucket.get_center_lat();
        double width = new_bucket.get_width();
        double height = new_bucket.get_height();

        double left = center_lon - width / 2;
        double right = center_lon + width / 2;
        double bottom = center_lat - height / 2;
        double top = center_lat + height / 2;

        bbox.expandToInclude(left, bottom);
        bbox.expandToInclude(right, top);
    }

    ImageRef OrthophotoManager::getBucketImage(const SGBucket& bucket) {
        long index = bucket.gen_index();

        ImageRef& image = _bucketImages[index];

        if (!image) {
            const std::string bucketPath = bucket.gen_base_path();

            for (const auto& sceneryPath : _sceneryPaths) {
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

    osg::ref_ptr<Orthophoto> OrthophotoManager::getOrthophoto(const SGBucket& bucket) {
        ImageRef image = getBucketImage(bucket);

        if (!image)
            return nullptr;

        OrthophotoBounds bbox;
        augmentBoundingBox(bbox, bucket);
        
        return new Orthophoto(image, bbox);
    }

    osg::ref_ptr<Orthophoto> OrthophotoManager::getOrthophoto(const std::vector<SGVec3d>& nodes, const SGVec3d& center) {
        
        OrthophotoBounds desired_bbox;
        
        for (const auto& node : nodes) {
          const SGGeod node_geod = SGGeod::fromCart(node + center);
          const double lon_deg = node_geod.getLongitudeDeg();
          const double lat_deg = node_geod.getLatitudeDeg();

          desired_bbox.expandToInclude(lon_deg, lat_deg);
        }
        
        double eps = SG_EPSILON * SGD_RADIANS_TO_DEGREES;
        desired_bbox.minLon += eps;
        desired_bbox.minLat += eps;
        desired_bbox.maxLon -= eps;
        desired_bbox.maxLat -= eps;

        SGBucket bottom_left_bucket(SGGeod::fromDeg(desired_bbox.minLon, desired_bbox.minLat));
        const double bucket_width = bottom_left_bucket.get_width();
        ImageRef bottom_left_image = getBucketImage(bottom_left_bucket);

        if (!bottom_left_image)
            return nullptr;
        
        OrthophotoBounds actual_bbox;
        augmentBoundingBox(actual_bbox, bottom_left_bucket);

        // Simplest case - we already have the full orthophoto
        if (actual_bbox.maxLon > desired_bbox.maxLon && actual_bbox.maxLat > desired_bbox.maxLat)
            return new Orthophoto(bottom_left_image, actual_bbox);
        

        // More complex case - we create a composite orthophoto


        int bk_width = 1;
        int bk_height = 1;

        ImageRefCollection2d images;
        
        ImageRefVec first_row_images;
        first_row_images.push_back(bottom_left_image);
        SGBucket current_bucket = bottom_left_bucket;
        while (actual_bbox.maxLon < desired_bbox.maxLon) {
            current_bucket = current_bucket.sibling(1, 0);
            ImageRef new_image = getBucketImage(current_bucket);
            first_row_images.push_back(new_image);
            augmentBoundingBox(actual_bbox, current_bucket);
            bk_width++;
        }
        images.push_back(first_row_images);

        current_bucket = bottom_left_bucket;
        while (actual_bbox.maxLat < desired_bbox.maxLat) {
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
