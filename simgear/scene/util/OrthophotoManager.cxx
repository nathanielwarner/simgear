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

    void OrthophotoBounds::absorb(const OrthophotoBounds& bounds) {
        expandToInclude(bounds.minLon, bounds.minLat);
        expandToInclude(bounds.maxLon, bounds.maxLat);
    }

    Orthophoto::Orthophoto(const ImageRef& image, const OrthophotoBounds& bbox) {
        _image = image;
        _bbox = bbox;
    }

    Orthophoto::Orthophoto(const std::vector<osg::ref_ptr<Orthophoto>>& orthophotos) {
        for (const auto& orthophoto : orthophotos) {
            _bbox.absorb(orthophoto->getBbox());
        }

        const osg::ref_ptr<Orthophoto>& some_orthophoto = orthophotos[0];
        const ImageRef& some_image = some_orthophoto->_image;
        const OrthophotoBounds& some_bbox = some_orthophoto->getBbox();
        const double degs_to_pixels = some_image->s() / (some_bbox.maxLon - some_bbox.minLon);
        
        const int total_width = degs_to_pixels * (_bbox.maxLon - _bbox.minLon);
        const int total_height = degs_to_pixels * (_bbox.maxLat - _bbox.minLat);

        const int depth = some_image->r();
        GLenum pixel_format = some_image->getPixelFormat();
        GLenum data_type = some_image->getDataType();
        int packing = some_image->getPacking();

        _image = new osg::Image();
        _image->allocateImage(total_width, total_height, depth, pixel_format, data_type, packing);

        for (const auto& orthophoto : orthophotos) {
            ImageRef sub_image = orthophoto->_image;
            const OrthophotoBounds& bounds = orthophoto->getBbox();
            const int width = degs_to_pixels * (bounds.maxLon - bounds.minLon);
            const int height = degs_to_pixels * (bounds.maxLat - bounds.minLat);
            sub_image->scaleImage(width, height, depth);
            const int s_offset = degs_to_pixels * (bounds.minLon - _bbox.minLon);
            const int t_offset = degs_to_pixels * (_bbox.maxLat - bounds.maxLat);
            
            _image->copySubImage(s_offset, t_offset, 0, sub_image);
        }
    }

    osg::ref_ptr<osg::Texture2D> Orthophoto::getTexture() {
        osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D(_image);
        texture->setWrap(osg::Texture::WrapParameter::WRAP_S, osg::Texture::WrapMode::CLAMP_TO_EDGE);
        texture->setWrap(osg::Texture::WrapParameter::WRAP_T, osg::Texture::WrapMode::CLAMP_TO_EDGE);
        texture->setWrap(osg::Texture::WrapParameter::WRAP_R, osg::Texture::WrapMode::CLAMP_TO_EDGE);
        texture->setMaxAnisotropy(SGSceneFeatures::instance()->getTextureFilter());
        return texture;
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
        
        std::unordered_map<long, bool> orthophotos_attempted;
        std::vector<osg::ref_ptr<Orthophoto>> orthophotos;

        for (const auto& node : nodes) {
            const SGGeod node_geod = SGGeod::fromCart(node + center);
            const SGBucket bucket(node_geod);
            bool& orthophoto_attempted = orthophotos_attempted[bucket.gen_index()];
            if (!orthophoto_attempted) {
                osg::ref_ptr<Orthophoto> orthophoto = getOrthophoto(bucket);
                if (orthophoto) {
                    orthophotos.push_back(orthophoto);
                }
                orthophoto_attempted = true;
            }
        }

        if (orthophotos.size() == 0) {
            return nullptr;
        }

        return new Orthophoto(orthophotos);
    }
}
