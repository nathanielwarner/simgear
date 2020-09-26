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
    OrthophotoManager* OrthophotoManager::instance() {
        return SingletonRefPtr<OrthophotoManager>::instance();
    }

    void OrthophotoManager::addSceneryPath(const SGPath path) {
        for (SGPath existingPath : sceneryPaths) {
            if (path == existingPath) {
                return;
            }
        }
        sceneryPaths.push_front(path);
    }

    void OrthophotoManager::clearSceneryPaths() {
        sceneryPaths.clear();
    }

    void OrthophotoManager::getOrthophoto(double min_lon, double max_lon, double min_lat, double max_lat, osg::ref_ptr<osg::Image>& orthophoto, 
                                          double& ortho_min_lon, double& ortho_max_lon, double& ortho_min_lat, double& ortho_max_lat) {

        // Need to shrink the input bounding box by epsilon so we don't incorrectly end up with too many buckets
        double eps = SG_EPSILON * SGD_RADIANS_TO_DEGREES;
        min_lon += eps;
        max_lon -= eps;
        min_lat += eps;
        max_lat -= eps;
        
        std::vector<SGBucket> buckets;
        SGGeod min_geod = SGGeod::fromDeg(min_lon, min_lat);
        sgGetBuckets(min_geod, min_geod, buckets);

        if (buckets.size() == 1) {
            SGBucket bucket = buckets[0];
            std::string bucketPath = bucket.gen_base_path();
            long index = bucket.gen_index();

            double center_lon = bucket.get_center_lon();
            double center_lat = bucket.get_center_lat();
            double width = bucket.get_width();
            double height = bucket.get_height();

            ortho_min_lon = center_lon - width;
            ortho_max_lon = center_lon + width;
            ortho_min_lat = center_lat - height;
            ortho_max_lat = center_lat + height;

            for (SGPath sceneryPath : sceneryPaths) {
                SGPath path = sceneryPath / "Orthophotos" / bucketPath / std::to_string(index);
                path.concat(".png");
                if (path.exists()) {
                    orthophoto = osgDB::readRefImageFile(path.str());
                    break;
                }
            }
        }
        
    }
}
