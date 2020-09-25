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

    void OrthophotoManager::getOrthophoto(long index, osg::ref_ptr<osg::Image>& orthophoto) {
        if (_orthophotos[index]) {
            orthophoto = _orthophotos[index];
        } else {
            SGBucket bucket(index);
            std::string bucketPath = bucket.gen_base_path();

            for (SGPath sceneryPath : _sceneryPaths) {
                SGPath path = sceneryPath / "Orthophotos" / bucketPath / std::to_string(index);
                path.concat(".png");
                if (path.exists()) {
                    _orthophotos[index] = osgDB::readRefImageFile(path.str());
                    orthophoto = _orthophotos[index];
                    break;
                }
            }
        }
    }
}
