// particles.cxx - classes to manage particles
// started in 2008 by Tiago Gusm�o, using animation.hxx as reference
// Copyright (C) 2008 Tiago Gusm�o
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

#include <simgear_config.h>

#include "particles.hxx"


#include <mutex>

#include <simgear/misc/sg_path.hxx>
#include <simgear/props/props.hxx>
#include <simgear/props/props_io.hxx>
#include <simgear/scene/util/OsgMath.hxx>

#include <osgParticle/SmokeTrailEffect>
#include <osgParticle/FireEffect>
#include <osgParticle/ConnectedParticleSystem>
#include <osgParticle/MultiSegmentPlacer>
#include <osgParticle/SectorPlacer>
#include <osgParticle/ConstantRateCounter>
#include <osgParticle/ParticleSystemUpdater>
#include <osgParticle/ParticleSystem>
#include <osgParticle/FluidProgram>

#include <osg/Geode>
#include <osg/Group>
#include <osg/MatrixTransform>
#include <osg/Node>


#include <simgear/scene/model/animation.hxx>

namespace simgear
{

class ParticlesGlobalManager::ParticlesGlobalManagerPrivate : public osg::NodeCallback
{
public:
    ParticlesGlobalManagerPrivate() : _updater(new osgParticle::ParticleSystemUpdater),
                                      _commonGeode(new osg::Geode)
    {
    }

    void operator()(osg::Node* node, osg::NodeVisitor* nv) override
    {
        std::lock_guard<std::mutex> g(_lock);
        _enabled = !_enabledNode || _enabledNode->getBoolValue();

        if (!_enabled)
            return;

        const auto q = SGQuatd::fromLonLatDeg(_longitudeNode->getFloatValue(), _latitudeNode->getFloatValue());
        osg::Matrix om(toOsg(q));
        osg::Vec3 v(0, 0, 9.81);

        _gravity = om.preMult(v);

        // NOTE: THIS WIND COMPUTATION DOESN'T SEEM TO AFFECT PARTICLES
        // const osg::Vec3& zUpWind = _wind;
        // osg::Vec3 w(zUpWind.y(), zUpWind.x(), -zUpWind.z());
        // _localWind = om.preMult(w);
    }

    // only call this with the lock held!
    osg::Group* internalGetCommonRoot()
    {
        if (!_commonRoot.valid()) {
            SG_LOG(SG_PARTICLES, SG_DEBUG, "Particle common root called.");
            _commonRoot = new osg::Group;
            _commonRoot->setName("common particle system root");
            _commonGeode->setName("common particle system geode");
            _commonRoot->addChild(_commonGeode);
            _commonRoot->addChild(_updater);
            _commonRoot->setNodeMask(~simgear::MODELLIGHT_BIT);
        }
        return _commonRoot.get();
    }

    std::mutex _lock;
    bool _frozen = false;
    osg::ref_ptr<osgParticle::ParticleSystemUpdater> _updater;
    osg::ref_ptr<osg::Group> _commonRoot;
    osg::ref_ptr<osg::Geode> _commonGeode;
    osg::Vec3 _wind;
    bool _globalCallbackRegistered = false;
    bool _enabled = true;
    osg::Vec3 _gravity;
    //  osg::Vec3 _localWind;

    SGConstPropertyNode_ptr _enabledNode;
    SGConstPropertyNode_ptr _longitudeNode, _latitudeNode;
};

static std::mutex static_managerLock;
static std::unique_ptr<ParticlesGlobalManager> static_instance;

ParticlesGlobalManager* ParticlesGlobalManager::instance()
{
    std::lock_guard<std::mutex> g(static_managerLock);
    if (!static_instance) {
        static_instance.reset(new ParticlesGlobalManager);
    }

    return static_instance.get();
}

void ParticlesGlobalManager::clear()
{
    std::lock_guard<std::mutex> g(static_managerLock);
    static_instance.reset();
}

ParticlesGlobalManager::ParticlesGlobalManager() : d(new ParticlesGlobalManagerPrivate)
{
}

ParticlesGlobalManager::~ParticlesGlobalManager()
{
    if (d->_globalCallbackRegistered) {
        // is this actually necessary? possibly not
        d->_updater->setUpdateCallback(nullptr);
    }
}

bool ParticlesGlobalManager::isEnabled() const
{
    std::lock_guard<std::mutex> g(d->_lock);
    return d->_enabled;
}

bool ParticlesGlobalManager::isFrozen() const
{
    std::lock_guard<std::mutex> g(d->_lock);
    return d->_frozen;
}

osg::Vec3 ParticlesGlobalManager::getWindVector() const
{
    std::lock_guard<std::mutex> g(d->_lock);
    return d->_wind;
}

template <typename Object>
class PointerGuard{
public:
    Object* get() { return _ptr; }
    Object* operator () ()
    {
        if (!_ptr)
            _ptr = new Object;
        return _ptr;
    }
private:
    Object* _ptr = nullptr;
};

void transformParticles(osgParticle::ParticleSystem* particleSys,
                        const osg::Matrix& mat)
{
    const int numParticles = particleSys->numParticles();
    if (particleSys->areAllParticlesDead())
        return;
    for (int i = 0; i < numParticles; ++i) {
        osgParticle::Particle* P = particleSys->getParticle(i);
        if (!P->isAlive())
            continue;
        P->transformPositionVelocity(mat);
    }
}

void Particles::operator()(osg::Node* node, osg::NodeVisitor* nv)
{
    auto globalManager = ParticlesGlobalManager::instance();

    //SG_LOG(SG_PARTICLES, SG_ALERT, "callback!\n");
    particleSys->setFrozen(globalManager->isFrozen());

    using namespace osg;
    if (shooterValue)
        shooter->setInitialSpeedRange(shooterValue->getValue(),
                                      (shooterValue->getValue() + shooterExtraRange));
    if (counterValue)
        counter->setRateRange(counterValue->getValue(),
                              counterValue->getValue() + counterExtraRange);
    else if (counterCond)
        counter->setRateRange(counterStaticValue,
                              counterStaticValue + counterStaticExtraRange);
    if (!globalManager->isEnabled() || (counterCond && !counterCond->test()))
        counter->setRateRange(0, 0);
    bool colorchange = false;
    for (int i = 0; i < 8; ++i) {
        if (colorComponents[i]) {
            staticColorComponents[i] = colorComponents[i]->getValue();
            colorchange = true;
        }
    }
    if (colorchange)
        particleSys->getDefaultParticleTemplate().setColorRange(osgParticle::rangev4(Vec4(staticColorComponents[0], staticColorComponents[1], staticColorComponents[2], staticColorComponents[3]), Vec4(staticColorComponents[4], staticColorComponents[5], staticColorComponents[6], staticColorComponents[7])));
    if (startSizeValue)
        startSize = startSizeValue->getValue();
    if (endSizeValue)
        endSize = endSizeValue->getValue();
    if (startSizeValue || endSizeValue)
        particleSys->getDefaultParticleTemplate().setSizeRange(osgParticle::rangef(startSize, endSize));
    if (lifeValue)
        particleSys->getDefaultParticleTemplate().setLifeTime(lifeValue->getValue());

    if (particleFrame.valid()) {
        MatrixList mlist = node->getWorldMatrices();
        if (!mlist.empty()) {
            const Matrix& particleMat = particleFrame->getMatrix();
            Vec3d emitOrigin(mlist[0](3, 0), mlist[0](3, 1), mlist[0](3, 2));
            Vec3d displace = emitOrigin - Vec3d(particleMat(3, 0), particleMat(3, 1),
                                                particleMat(3, 2));
            if (displace * displace > 10000.0 * 10000.0) {
                // Make new frame for particle system, coincident with
                // the emitter frame, but oriented with local Z.
                SGGeod geod = SGGeod::fromCart(toSG(emitOrigin));
                Matrix newParticleMat = makeZUpFrame(geod);
                Matrix changeParticleFrame = particleMat * Matrix::inverse(newParticleMat);
                particleFrame->setMatrix(newParticleMat);
                transformParticles(particleSys.get(), changeParticleFrame);
            }
        }
    }
    if (program.valid() && useWind)
        program->setWind(globalManager->getWindVector());
}

void Particles::setupShooterSpeedData(const SGPropertyNode* configNode,
                                      SGPropertyNode* modelRoot)
{
    shooterValue = read_value(configNode, modelRoot, "-m",
                              -SGLimitsd::max(), SGLimitsd::max());
    if (!shooterValue) {
        SG_LOG(SG_GENERAL, SG_DEV_WARN, "Particles: shooter property error!\n");
    }
    shooterExtraRange = configNode->getFloatValue("extrarange", 0);
}

void Particles::setupCounterData(const SGPropertyNode* configNode,
                                 SGPropertyNode* modelRoot)
{
    counterValue = read_value(configNode, modelRoot, "-m",
                              -SGLimitsd::max(), SGLimitsd::max());
    if (!counterValue) {
        SG_LOG(SG_GENERAL, SG_DEV_WARN, "counter property error!\n");
    }
    counterExtraRange = configNode->getFloatValue("extrarange", 0);
}

void Particles::Particles::setupCounterCondition(const SGPropertyNode* configNode,
                                                 SGPropertyNode* modelRoot)
{
    counterCond = sgReadCondition(modelRoot, configNode);
}

void Particles::setupCounterCondition(float aCounterStaticValue,
                                      float aCounterStaticExtraRange)
{
    counterStaticValue = aCounterStaticValue;
    counterStaticExtraRange = aCounterStaticExtraRange;
}

void Particles::setupStartSizeData(const SGPropertyNode* configNode,
                                   SGPropertyNode* modelRoot)
{
    startSizeValue = read_value(configNode, modelRoot, "-m",
                                -SGLimitsd::max(), SGLimitsd::max());
    if (!startSizeValue) {
        SG_LOG(SG_GENERAL, SG_DEV_WARN, "Particles: startSizeValue error!\n");
    }
}

void Particles::setupEndSizeData(const SGPropertyNode* configNode,
                                 SGPropertyNode* modelRoot)
{
    endSizeValue = read_value(configNode, modelRoot, "-m",
                              -SGLimitsd::max(), SGLimitsd::max());
    if (!endSizeValue) {
        SG_LOG(SG_GENERAL, SG_DEV_WARN, "Particles: startSizeValue error!\n");
    }
}

void Particles::setupLifeData(const SGPropertyNode* configNode,
                              SGPropertyNode* modelRoot)
{
    lifeValue = read_value(configNode, modelRoot, "-m",
                           -SGLimitsd::max(), SGLimitsd::max());
    if (!lifeValue) {
        SG_LOG(SG_GENERAL, SG_DEV_WARN, "Particles: lifeValue error!\n");
    }
}

void Particles::setupColorComponent(const SGPropertyNode* configNode,
                                    SGPropertyNode* modelRoot, int color,
                                    int component)
{
    SGSharedPtr<SGExpressiond> colorValue = read_value(configNode, modelRoot, "-m",
                                                       -SGLimitsd::max(),
                                                       SGLimitsd::max());
    if (!colorValue) {
        SG_LOG(SG_GENERAL, SG_DEV_WARN, "Particles: color property error!\n");
    }
    colorComponents[(color * 4) + component] = colorValue;
    //number of color components = 4
}

void Particles::setupStaticColorComponent(float r1, float g1, float b1, float a1,
                                          float r2, float g2, float b2, float a2)
{
    staticColorComponents[0] = r1;
    staticColorComponents[1] = g1;
    staticColorComponents[2] = b1;
    staticColorComponents[3] = a1;
    staticColorComponents[4] = r2;
    staticColorComponents[5] = g2;
    staticColorComponents[6] = b2;
    staticColorComponents[7] = a2;
}

void ParticlesGlobalManager::setWindVector(const osg::Vec3& wind)
{
    std::lock_guard<std::mutex> g(d->_lock);
    d->_wind = wind;
}

void ParticlesGlobalManager::setWindFrom(const double from_deg, const double speed_kt)
{
    double map_rad = -from_deg * SG_DEGREES_TO_RADIANS;
    double speed_mps = speed_kt * SG_KT_TO_MPS;

    std::lock_guard<std::mutex> g(d->_lock);
    d->_wind[0] = cos(map_rad) * speed_mps;
    d->_wind[1] = sin(map_rad) * speed_mps;
    d->_wind[2] = 0.0;
}


osg::Group* ParticlesGlobalManager::getCommonRoot()
{
    std::lock_guard<std::mutex> g(d->_lock);
    return d->internalGetCommonRoot();
}

osg::ref_ptr<osg::Group> ParticlesGlobalManager::appendParticles(const SGPropertyNode* configNode, SGPropertyNode* modelRoot, const osgDB::Options* options)
{
    SG_LOG(SG_PARTICLES, SG_DEBUG,
           "Setting up a particle system." << std::boolalpha
        << "\n  Name: " << configNode->getStringValue("name", "")
        << "\n  Type: " << configNode->getStringValue("type", "point")
        << "\n  Attach: " << configNode->getStringValue("attach", "")
        << "\n  Texture: " << configNode->getStringValue("texture", "")
        << "\n  Emissive: " << configNode->getBoolValue("emissive")
        << "\n  Lighting: " << configNode->getBoolValue("lighting")
        << "\n  Align: " << configNode->getStringValue("align", "")
        << "\n  Placer: " << configNode->hasChild("placer")
        << "\n  Shooter: " << configNode->hasChild("shooter")
        << "\n  Particle: " << configNode->hasChild("particle")
        << "\n  Program: " << configNode->hasChild("program")
        << "\n    Fluid: " << configNode->getChild("program")->getStringValue("fluid", "air")
        << "\n    Gravity: " << configNode->getChild("program")->getBoolValue("gravity", true)
        << "\n    Wind: " << configNode->getChild("program")->getBoolValue("wind", true)
        << std::noboolalpha);

    osg::ref_ptr<osgParticle::ParticleSystem> particleSys;

    //create a generic particle system
    std::string type = configNode->getStringValue("type", "normal");
    if (type == "normal")
        particleSys = new osgParticle::ParticleSystem;
    else
        particleSys = new osgParticle::ConnectedParticleSystem;

    //may not be used depending on the configuration
    PointerGuard<Particles> callback;

    //contains counter, placer and shooter by default
    osgParticle::ModularEmitter* emitter = new osgParticle::ModularEmitter;

    emitter->setParticleSystem(particleSys);

    // Set up the alignment node ("stolen" from animation.cxx)
    // XXX Order of rotations is probably not correct.
    osg::ref_ptr<osg::MatrixTransform> align = new osg::MatrixTransform;
    osg::Matrix res_matrix;
    res_matrix.makeRotate(
        configNode->getFloatValue("offsets/pitch-deg", 0.0)*SG_DEGREES_TO_RADIANS,
        osg::Vec3(0, 1, 0),
        configNode->getFloatValue("offsets/roll-deg", 0.0)*SG_DEGREES_TO_RADIANS,
        osg::Vec3(1, 0, 0),
        configNode->getFloatValue("offsets/heading-deg", 0.0)*SG_DEGREES_TO_RADIANS,
        osg::Vec3(0, 0, 1));

    osg::Matrix tmat;
    tmat.makeTranslate(configNode->getFloatValue("offsets/x-m", 0.0),
                       configNode->getFloatValue("offsets/y-m", 0.0),
                       configNode->getFloatValue("offsets/z-m", 0.0));
    align->setMatrix(res_matrix * tmat);
    align->setName("particle align");

    align->addChild(emitter);

    //this name can be used in the XML animation as if it was a submodel
    std::string name = configNode->getStringValue("name", "");
    if (!name.empty())
        align->setName(name);
    std::string attach = configNode->getStringValue("attach", "world");
    if (attach == "local") { //local means attached to the model and not the world
        osg::Geode* g = new osg::Geode;
        align->addChild(g);
        g->addDrawable(particleSys);
    } else {
        callback()->particleFrame = new osg::MatrixTransform();
        osg::Geode* g = new osg::Geode;
        g->addDrawable(particleSys);
        callback()->particleFrame->addChild(g);
    }
    std::string textureFile;
    if (configNode->hasValue("texture")) {
        //SG_LOG(SG_PARTICLES, SG_ALERT,
        //       "requested:"<<configNode->getStringValue("texture","")<<"\n");
        textureFile= osgDB::findFileInPath(configNode->getStringValue("texture",
                                                                      ""),
                                           options->getDatabasePathList());
        //SG_LOG(SG_PARTICLES, SG_ALERT, "found:"<<textureFile<<"\n");

        //for(unsigned i = 0; i < options->getDatabasePathList().size(); ++i)
        //    SG_LOG(SG_PARTICLES, SG_ALERT,
        //           "opts:"<<options->getDatabasePathList()[i]<<"\n");
    }

    particleSys->setDefaultAttributes(textureFile,
                                      configNode->getBoolValue("emissive",
                                                               true),
                                      configNode->getBoolValue("lighting",
                                                               false));

    std::string alignstr = configNode->getStringValue("align", "billboard");

    if (alignstr == "fixed")
        particleSys->setParticleAlignment(osgParticle::ParticleSystem::FIXED);

    const SGPropertyNode* placernode = configNode->getChild("placer");

    if (placernode) {
        std::string emitterType = placernode->getStringValue("type", "point");

        if (emitterType == "sector") {
            osgParticle::SectorPlacer *splacer = new  osgParticle::SectorPlacer;
            float minRadius, maxRadius, minPhi, maxPhi;

            minRadius = placernode->getFloatValue("radius-min-m",0);
            maxRadius = placernode->getFloatValue("radius-max-m",1);
            minPhi = (placernode->getFloatValue("phi-min-deg",0)
                      * SG_DEGREES_TO_RADIANS);
            maxPhi = (placernode->getFloatValue("phi-max-deg",360.0f)
                      * SG_DEGREES_TO_RADIANS);

            splacer->setRadiusRange(minRadius, maxRadius);
            splacer->setPhiRange(minPhi, maxPhi);
            emitter->setPlacer(splacer);
        } else if (emitterType == "segments") {
            std::vector<SGPropertyNode_ptr> segments
                = placernode->getChildren("vertex");
            if (segments.size()>1) {
                osgParticle::MultiSegmentPlacer *msplacer
                    = new osgParticle::MultiSegmentPlacer();
                float x,y,z;

                for (unsigned i = 0; i < segments.size(); ++i) {
                    x = segments[i]->getFloatValue("x-m",0);
                    y = segments[i]->getFloatValue("y-m",0);
                    z = segments[i]->getFloatValue("z-m",0);
                    msplacer->addVertex(x, y, z);
                }
                emitter->setPlacer(msplacer);
            } else {
                SG_LOG(SG_PARTICLES, SG_ALERT,
                       "Detected particle system using segment(s) with less than 2 vertices\n");
            }
        } //else the default placer in ModularEmitter is used (PointPlacer)
    }

    const SGPropertyNode* shnode = configNode->getChild("shooter");

    if (shnode) {
        float minTheta, maxTheta, minPhi, maxPhi, speed, spread;

        minTheta = (shnode->getFloatValue("theta-min-deg",0)
                    * SG_DEGREES_TO_RADIANS);
        maxTheta = (shnode->getFloatValue("theta-max-deg",360.0f)
                    * SG_DEGREES_TO_RADIANS);
        minPhi = shnode->getFloatValue("phi-min-deg",0)* SG_DEGREES_TO_RADIANS;
        maxPhi = (shnode->getFloatValue("phi-max-deg",360.0f)
                  * SG_DEGREES_TO_RADIANS); 

        osgParticle::RadialShooter *shooter = new osgParticle::RadialShooter;
        emitter->setShooter(shooter);

        shooter->setThetaRange(minTheta, maxTheta);
        shooter->setPhiRange(minPhi, maxPhi);

        const SGPropertyNode* speednode = shnode->getChild("speed-mps");

        if (speednode) {
            if (speednode->hasValue("value")) {
                speed = speednode->getFloatValue("value",0);
                spread = speednode->getFloatValue("spread",0);
                shooter->setInitialSpeedRange(speed-spread, speed+spread);
            } else {
                callback()->setupShooterSpeedData(speednode, modelRoot);
            }
        }

        const SGPropertyNode* rotspeednode = shnode->getChild("rotation-speed");

        if (rotspeednode) {
            float x1,y1,z1,x2,y2,z2;
            x1 = rotspeednode->getFloatValue("x-min-deg-sec",0) * SG_DEGREES_TO_RADIANS;
            y1 = rotspeednode->getFloatValue("y-min-deg-sec",0) * SG_DEGREES_TO_RADIANS;
            z1 = rotspeednode->getFloatValue("z-min-deg-sec",0) * SG_DEGREES_TO_RADIANS;
            x2 = rotspeednode->getFloatValue("x-max-deg-sec",0) * SG_DEGREES_TO_RADIANS;
            y2 = rotspeednode->getFloatValue("y-max-deg-sec",0) * SG_DEGREES_TO_RADIANS;
            z2 = rotspeednode->getFloatValue("z-max-deg-sec",0) * SG_DEGREES_TO_RADIANS;
            shooter->setInitialRotationalSpeedRange(osg::Vec3f(x1,y1,z1), osg::Vec3f(x2,y2,z2));
        }
    } //else ModularEmitter uses the default RadialShooter


    const SGPropertyNode* conditionNode = configNode->getChild("condition");
    const SGPropertyNode* counternode = configNode->getChild("counter");

    if (conditionNode || counternode) {
        osgParticle::RandomRateCounter* counter
            = new osgParticle::RandomRateCounter;
        emitter->setCounter(counter);
        float pps = 0.0f, spread = 0.0f;

        if (counternode) {
            const SGPropertyNode* ppsnode = counternode->getChild("particles-per-sec");
            if (ppsnode) {
                if (ppsnode->hasValue("value")) {
                    pps = ppsnode->getFloatValue("value",0);
                    spread = ppsnode->getFloatValue("spread",0);
                    counter->setRateRange(pps-spread, pps+spread);
                } else {
                    callback()->setupCounterData(ppsnode, modelRoot);
                }
            }
        }

        if (conditionNode) {
            callback()->setupCounterCondition(conditionNode, modelRoot);
            callback()->setupCounterCondition(pps, spread);
        }
    } //TODO: else perhaps set higher values than default? 

    const SGPropertyNode* particlenode = configNode->getChild("particle");
    if (particlenode) {
        osgParticle::Particle &particle
            = particleSys->getDefaultParticleTemplate();
        float r1=0, g1=0, b1=0, a1=1, r2=0, g2=0, b2=0, a2=1;
        const SGPropertyNode* startcolornode
            = particlenode->getNode("start/color");
        if (startcolornode) {
            const SGPropertyNode* componentnode
                = startcolornode->getChild("red");
            if (componentnode) {
                if (componentnode->hasValue("value"))
                    r1 = componentnode->getFloatValue("value",0);
                else 
                    callback()->setupColorComponent(componentnode, modelRoot,
                                                    0, 0);
            }
            componentnode = startcolornode->getChild("green");
            if (componentnode) {
                if (componentnode->hasValue("value"))
                    g1 = componentnode->getFloatValue("value", 0);
                else
                    callback()->setupColorComponent(componentnode, modelRoot,
                                                    0, 1);
            }
            componentnode = startcolornode->getChild("blue");
            if (componentnode) {
                if (componentnode->hasValue("value"))
                    b1 = componentnode->getFloatValue("value",0);
                else
                    callback()->setupColorComponent(componentnode, modelRoot,
                                                    0, 2);
            }
            componentnode = startcolornode->getChild("alpha");
            if (componentnode) {
                if (componentnode->hasValue("value"))
                    a1 = componentnode->getFloatValue("value",0);
                else
                    callback()->setupColorComponent(componentnode, modelRoot,
                                                    0, 3);
            }
        }
        const SGPropertyNode* endcolornode = particlenode->getNode("end/color");
        if (endcolornode) {
            const SGPropertyNode* componentnode = endcolornode->getChild("red");

            if (componentnode) {
                if (componentnode->hasValue("value"))
                    r2 = componentnode->getFloatValue("value",0);
                else
                    callback()->setupColorComponent(componentnode, modelRoot,
                                                    1, 0);
            }
            componentnode = endcolornode->getChild("green");
            if (componentnode) {
                if (componentnode->hasValue("value"))
                    g2 = componentnode->getFloatValue("value",0);
                else
                    callback()->setupColorComponent(componentnode, modelRoot,
                                                    1, 1);
            }
            componentnode = endcolornode->getChild("blue");
            if (componentnode) {
                if (componentnode->hasValue("value"))
                    b2 = componentnode->getFloatValue("value",0);
                else
                    callback()->setupColorComponent(componentnode, modelRoot,
                                                    1, 2);
            }
            componentnode = endcolornode->getChild("alpha");
            if (componentnode) {
                if (componentnode->hasValue("value"))
                    a2 = componentnode->getFloatValue("value",0);
                else
                    callback()->setupColorComponent(componentnode, modelRoot,
                                                    1, 3);
            }
        }
        particle.setColorRange(osgParticle::rangev4(osg::Vec4(r1,g1,b1,a1),
                                                    osg::Vec4(r2,g2,b2,a2)));

        float startsize=1, endsize=0.1f;
        const SGPropertyNode* startsizenode = particlenode->getNode("start/size");
        if (startsizenode) {
            if (startsizenode->hasValue("value"))
                startsize = startsizenode->getFloatValue("value",0);
            else
                callback()->setupStartSizeData(startsizenode, modelRoot);
        }
        const SGPropertyNode* endsizenode = particlenode->getNode("end/size");
        if (endsizenode) {
            if (endsizenode->hasValue("value"))
                endsize = endsizenode->getFloatValue("value",0);
            else
                callback()->setupEndSizeData(endsizenode, modelRoot);
        }
        particle.setSizeRange(osgParticle::rangef(startsize, endsize));
        float life=5;
        const SGPropertyNode* lifenode = particlenode->getChild("life-sec");
        if (lifenode) {
            if (lifenode->hasValue("value"))
                life =  lifenode->getFloatValue("value",0);
            else
                callback()->setupLifeData(lifenode, modelRoot);
        }

        particle.setLifeTime(life);
        if (particlenode->hasValue("radius-m"))
            particle.setRadius(particlenode->getFloatValue("radius-m",0));
        if (particlenode->hasValue("mass-kg"))
            particle.setMass(particlenode->getFloatValue("mass-kg",0));
        if (callback.get()) {
            callback.get()->setupStaticColorComponent(r1, g1, b1, a1,
                                                      r2, g2, b2, a2);
            callback.get()->setupStaticSizeData(startsize, endsize);
        }
        //particle.setColorRange(osgParticle::rangev4( osg::Vec4(r1, g1, b1, a1), osg::Vec4(r2, g2, b2, a2)));
    }

    const SGPropertyNode* programnode = configNode->getChild("program");
    osgParticle::FluidProgram *program = new osgParticle::FluidProgram();

    if (programnode) {
        std::string fluid = programnode->getStringValue("fluid", "air");

        if (fluid=="air")
            program->setFluidToAir();
        else
            program->setFluidToWater();

        if (programnode->getBoolValue("gravity", true)) {
            program->setToGravity();
        } else
            program->setAcceleration(osg::Vec3(0,0,0));

        if (programnode->getBoolValue("wind", true))
            callback()->setupProgramWind(true);
        else
            program->setWind(osg::Vec3(0,0,0));

        align->addChild(program);

        program->setParticleSystem(particleSys);
    }

    if (callback.get()) {  //this means we want property-driven changes
        SG_LOG(SG_PARTICLES, SG_DEBUG, "Setting up particle system user data and callback.");
        //setup data and callback
        callback.get()->setGeneralData(dynamic_cast<osgParticle::RadialShooter*>(emitter->getShooter()),
                                       dynamic_cast<osgParticle::RandomRateCounter*>(emitter->getCounter()),
                                       particleSys, program);
        emitter->setUpdateCallback(callback.get());
    }

    // touch shared data now (and not before)

    {
        std::lock_guard<std::mutex> g(d->_lock);
        d->_updater->addParticleSystem(particleSys);

        if (attach != "local") {
            d->internalGetCommonRoot()->addChild(callback()->particleFrame);
        }

        if (!d->_globalCallbackRegistered) {
            SG_LOG(SG_PARTICLES, SG_INFO, "Registering global particles callback");
            d->_globalCallbackRegistered = true;
            d->_longitudeNode = modelRoot->getNode("/position/longitude-deg", true);
            d->_latitudeNode = modelRoot->getNode("/position/latitude-deg", true);
            d->_updater->setUpdateCallback(d.get());
        }
    }

    return align;
}

void ParticlesGlobalManager::setSwitchNode(const SGPropertyNode* n)
{
    std::lock_guard<std::mutex> g(d->_lock);
    d->_enabledNode = n;
}

void ParticlesGlobalManager::setFrozen(bool b)
{
    std::lock_guard<std::mutex> g(d->_lock);
    d->_frozen = b;
}

} // namespace simgear
