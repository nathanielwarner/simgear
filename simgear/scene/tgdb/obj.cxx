// obj.cxx -- routines to handle loading scenery and building the plib
//            scene graph.
//
// Written by Curtis Olson, started October 1997.
//
// Copyright (C) 1997  Curtis L. Olson  - http://www.flightgear.org/~curt
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//
// $Id$


#ifdef HAVE_CONFIG_H
#  include <simgear_config.h>
#endif

#include <osgDB/FileNameUtils>
#include <osgDB/FileUtils>
#include <osgDB/ReadFile>
#include <osg/Texture2D>
#include <osg/TexEnv>

#include "obj.hxx"

#include <simgear/debug/logstream.hxx>
#include <simgear/io/sg_binobj.hxx>
#include <simgear/bucket/newbucket.hxx>
#include <simgear/scene/util/OrthophotoManager.hxx>

#include "SGTileGeometryBin.hxx"        // for original tile loading
#include "SGTileDetailsCallback.hxx"    // for tile details ( random objects, and lighting )


using namespace simgear;

osg::Node*
SGLoadBTG(const std::string& path, const simgear::SGReaderWriterOptions* options)
{
    SGBinObject tile;
    if (!tile.read_bin(path))
      return NULL;

    SGMaterialLibPtr matlib;
    osg::ref_ptr<SGMaterialCache> matcache;
    bool useVBOs = false;
    bool simplifyDistant = false;
    bool simplifyNear    = false;
    double ratio       = SG_SIMPLIFIER_RATIO;
    double maxLength   = SG_SIMPLIFIER_MAX_LENGTH;
    double maxError    = SG_SIMPLIFIER_MAX_ERROR;
    double object_range = SG_OBJECT_RANGE_ROUGH;
    double tile_min_expiry = SG_TILE_MIN_EXPIRY;
    bool usePhotoscenery = false;

    if (options) {
      matlib = options->getMaterialLib();
      useVBOs = (options->getPluginStringData("SimGear::USE_VBOS") == "ON");
      SGPropertyNode* propertyNode = options->getPropertyNode().get();

      // We control whether we simplify the nearby terrain and distant terrain separatey.
      // However, we don't allow only simplifying the near terrain!
      simplifyNear = propertyNode->getBoolValue("/sim/rendering/terrain/simplifier/enabled-near", simplifyNear);
      simplifyDistant = simplifyNear || propertyNode->getBoolValue("/sim/rendering/terrain/simplifier/enabled-far", simplifyDistant);
      ratio = propertyNode->getDoubleValue("/sim/rendering/terrain/simplifier/ratio", ratio);
      maxLength = propertyNode->getDoubleValue("/sim/rendering/terrain/simplifier/max-length", maxLength);
      maxError = propertyNode->getDoubleValue("/sim/rendering/terrain/simplifier/max-error", maxError);
      object_range = propertyNode->getDoubleValue("/sim/rendering/static-lod/rough", object_range);
      tile_min_expiry= propertyNode->getDoubleValue("/sim/rendering/plod-minimum-expiry-time-secs", tile_min_expiry);
      usePhotoscenery = propertyNode->getBoolValue("/sim/rendering/photoscenery/enabled", usePhotoscenery);
    }

    SGVec3d center = tile.get_gbs_center();
    SGGeod geodPos = SGGeod::fromCart(center);
    SGQuatd hlOr = SGQuatd::fromLonLat(geodPos)*SGQuatd::fromEulerDeg(0, 0, 180);
    if (matlib)
    	matcache = matlib->generateMatCache(geodPos);
      
    // Create bucket based on tile name to get the extent
    long index = strtol(osgDB::getSimpleFileName(osgDB::getNameLessExtension(path)).c_str(), NULL, 10);
    SGBucket b(index);
    double lon_min = b.get_center_lon() - 0.5 * b.get_width();
    double lat_max = b.get_center_lat() + 0.5 * b.get_height();

    // Overlay texture coordinates
    std::vector<SGVec2f> oc;

    // rotate the tiles so that the bounding boxes get nearly axis aligned.
    // this will help the collision tree's bounding boxes a bit ...
    std::vector<SGVec3d> nodes = tile.get_wgs84_nodes();
    for (unsigned i = 0; i < nodes.size(); ++i) {
      // Generate TexCoords for Overlay
      SGGeod node_deg = SGGeod::fromCart(nodes[i] + center);
      float x = (node_deg.getLongitudeDeg() - lon_min) / b.get_width();
      float y = (lat_max - node_deg.getLatitudeDeg()) / b.get_height();
      oc.push_back(SGVec2f(x, y));

      nodes[i] = hlOr.transform(nodes[i]);
    }
    tile.set_wgs84_nodes(nodes);
    tile.set_overlaycoords(oc);

    SGQuatf hlOrf(hlOr[0], hlOr[1], hlOr[2], hlOr[3]);
    std::vector<SGVec3f> normals = tile.get_normals();
    for (unsigned i = 0; i < normals.size(); ++i)
      normals[i] = hlOrf.transform(normals[i]);
    tile.set_normals(normals);

    // tile surface    
    osg::ref_ptr<SGTileGeometryBin> tileGeometryBin = new SGTileGeometryBin();

    if (!tileGeometryBin->insertSurfaceGeometry(tile, matcache))
      return NULL;

    osg::Node* node = tileGeometryBin->getSurfaceGeometry(matcache, useVBOs);

    if (node) {
      // Get base node stateset
      osg::StateSet *stateSet = node->getOrCreateStateSet();

      osg::ref_ptr<osg::Uniform> overlaySet = new osg::Uniform("overlaySet", 0);
      stateSet->addUniform(overlaySet, osg::StateAttribute::ON);

      if (usePhotoscenery) {
        // Add overlay texture (satellite imagery) if it exists

        osg::ref_ptr<osg::Image> satelliteOrthophoto;
        OrthophotoManager::instance()->getOrthophoto(index, satelliteOrthophoto);
        if (satelliteOrthophoto) {
          osg::ref_ptr<osg::Texture2D> orthophotoTexture = new osg::Texture2D(satelliteOrthophoto);
          orthophotoTexture->setBorderColor(osg::Vec4(0.0, 0.0, 0.0, 0.0));
          orthophotoTexture->setWrap(osg::Texture::WrapParameter::WRAP_S, osg::Texture::WrapMode::CLAMP_TO_BORDER);
          orthophotoTexture->setWrap(osg::Texture::WrapParameter::WRAP_T, osg::Texture::WrapMode::CLAMP_TO_BORDER);
          orthophotoTexture->setWrap(osg::Texture::WrapParameter::WRAP_R, osg::Texture::WrapMode::CLAMP_TO_BORDER);
          stateSet->setTextureAttributeAndModes(15, orthophotoTexture, osg::StateAttribute::ON);

          overlaySet->set(1);

          SG_LOG(SG_TERRAIN, SG_INFO, "  Adding overlay image for index " << index);
        }
      }
    }

    if (node && simplifyDistant) {
      osgUtil::Simplifier simplifier(ratio, maxError, maxLength);
      node->accept(simplifier);
    }

    // The toplevel transform for that tile.
    osg::MatrixTransform* transform = new osg::MatrixTransform;
    transform->setName(path);
    transform->setMatrix(osg::Matrix::rotate(toOsg(hlOr))*
                         osg::Matrix::translate(toOsg(center)));

    if (node) {
      // tile points
      SGTileDetailsCallback* tileDetailsCallback = new SGTileDetailsCallback;
      tileDetailsCallback->insertPtGeometry( tile, matcache );
    
      // PagedLOD for the random objects so we don't need to generate
      // them all on tile loading.
      osg::PagedLOD* pagedLOD = new osg::PagedLOD;
      pagedLOD->setCenterMode(osg::PagedLOD::USE_BOUNDING_SPHERE_CENTER);
      pagedLOD->setName("pagedObjectLOD");

      if (simplifyNear == simplifyDistant) {
        // Same terrain type is used for both near and far distances,
        // so add it to the main group.
        osg::Group* terrainGroup = new osg::Group;
        terrainGroup->setName("BTGTerrainGroup");
        terrainGroup->addChild(node);
        transform->addChild(terrainGroup);
      } else if (simplifyDistant) {
        // Simplified terrain is only used in the distance, the
        // call-back below will re-generate the closer version
        pagedLOD->addChild(node, 2*object_range + SG_TILE_RADIUS, FLT_MAX);
      }

      osg::ref_ptr<SGReaderWriterOptions> opt;
      opt = SGReaderWriterOptions::copyOrCreate(options);

      // we just need to know about the read file callback that itself holds the data
      tileDetailsCallback->_options = opt;
      tileDetailsCallback->_path = std::string(path);
      tileDetailsCallback->_loadterrain = ! (simplifyNear == simplifyDistant);
      tileDetailsCallback->_gbs_center = center;
      tileDetailsCallback->_rootNode = node;
      tileDetailsCallback->_randomSurfaceLightsComputed = false;
      tileDetailsCallback->_tileRandomObjectsComputed = false;
    
      osg::ref_ptr<osgDB::Options> callbackOptions = new osgDB::Options;
      callbackOptions->setObjectCacheHint(osgDB::Options::CACHE_ALL);
      callbackOptions->setReadFileCallback(tileDetailsCallback);
      pagedLOD->setDatabaseOptions(callbackOptions.get());

      // Ensure that the random objects aren't expired too quickly
      pagedLOD->setMinimumExpiryTime(pagedLOD->getNumChildren(), tile_min_expiry);
      pagedLOD->setFileName(pagedLOD->getNumChildren(), "Dummy filename for random objects callback");

      // LOD Range is 2x the object range plus the tile radius because we display some objects up to 2x the
      // range to reduce popping.
      pagedLOD->setRange(pagedLOD->getNumChildren(), 0,  2 *object_range + SG_TILE_RADIUS);
      transform->addChild(pagedLOD);
    }

    transform->setNodeMask( ~(simgear::CASTSHADOW_BIT | simgear::MODELLIGHT_BIT) );
    return transform;
}
