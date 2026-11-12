// Angry Birds - jeden rozbudowany poziom (PhysX + OpenGL).
// Fizyka 2D realizowana w 3D: wszystkie ciała mają zablokowany ruch wzdłuż Z
// oraz obroty wokół X i Y (ruch w płaszczyźnie XY, obrót tylko wokół Z).
// Poziom demonstruje: grawitację, kolizje, tarcie, pęd, moment obrotowy,
// jointy (zawias, wahadło), środek ciężkości, napęd silnikowy i zniszczenia.
//
// Zasady spójności fizycznej użyte w całym kodzie:
//  - masa każdego ciała = gęstość materiału * objętość (nigdzie nie ustawiamy masy "z ręki"),
//  - obrażenia = impuls kontaktowy [N*s] zgłoszony przez PhysX (wybuch: impuls J),
//  - rozpad zachowuje pęd całkowity; moment pędu jest przybliżony (fragmenty dzielą
//    bryłę na skończoną liczbę kawałków, więc część orbitalna L "ginie" w podziale -
//    dokładne zachowanie L wymagałoby nadania fragmentom dodatkowych prędkości kątowych
//    lub kompensacji orbitalnej). Ciała z przesuniętym COM są oznaczone jako nierozpadowe
//    (setCom), bo podział wokół środka geometrycznego złamałby i tak przybliżone L.
//  - podział ptaka BLUE zachowuje pęd (3 równe masy, symetryczne odchylenie boczne),
//  - jedno tłumienie obrotów dla wszystkich ciał, brak innych "sztucznych" tłumień,
//  - progi obrażeń są kalibrowane dla stałego kroku Dt = 1/60 s.
//  - ptaki nie kolidują ze sobą (uproszczenie rozgrywki, nie fizyka).

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <PxPhysicsAPI.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace physx;

// =============================================================
// Konfiguracja gry
// =============================================================
namespace cfg {
    constexpr int WindowW = 1280, WindowH = 720;
    constexpr float ViewW = 32.f, ViewH = 18.f;
    constexpr float Pi = 3.14159265f;
    constexpr float Gravity = 9.81f;
    constexpr float Dt = 1.f / 60.f;

    const PxVec2 Anchor(5.f, 3.2f);
    constexpr float MaxPull = 3.f;
    constexpr float SlingStiffness = 80.f;
    constexpr float GrabRadius = 1.5f;
    constexpr float ReloadDelay = 1.5f;
    constexpr float QueueStartX = 3.6f;
    constexpr float QueueSpacing = 0.65f;
    constexpr float QueueScale = 0.6f;

    constexpr float AngularDamping = 0.1f;
    constexpr float BirdDensity = 3.f;
    constexpr float PigDensity = 1.f;

    constexpr float SettleTime = 1.5f;
    constexpr float PigDeathImpulse = 3.f;
    constexpr float BirdLifetime = 12.f;
    constexpr float WinDelay = 0.8f;
    constexpr float LoseDelay = 3.f;
    constexpr float BirdRestSpeed = 0.5f;   // próg prędkości liniowej
    constexpr float BirdRestAngular = 1.0f; // próg prędkości kątowej [rad/s]
    constexpr float LoseHardTimeout = BirdLifetime + 2.f;

    constexpr float BombRadius = 6.f;
    constexpr float BombImpulse = 20.f;
    constexpr float FlashTime = 0.4f;

    constexpr float YellowBoost = 2.f;
    constexpr float SplitAngle = 0.25f;

    constexpr int SolverPos = 12, SolverVel = 4;
}

struct Color { float r, g, b, a = 1.f; };

namespace color {
    constexpr Color White{ 1.f, 1.f, 1.f };
    constexpr Color Black{ 0.f, 0.f, 0.f };
    constexpr Color Sky{ 0.53f, 0.81f, 0.92f };
    constexpr Color Ground{ 0.35f, 0.65f, 0.25f };
    constexpr Color Sling{ 0.40f, 0.25f, 0.10f };
    constexpr Color SlingBand{ 0.20f, 0.10f, 0.05f };
    constexpr Color Pig{ 0.30f, 0.75f, 0.30f };
    constexpr Color Explosion{ 1.f, 0.6f, 0.1f };
    constexpr Color Hud{ 0.1f, 0.1f, 0.1f };
    constexpr Color Frame{ 0.30f, 0.22f, 0.15f };
}

enum Type { BLOCK, PIG, BIRD, BALL };
enum Mat { WOOD, STONE, GLASS, METAL, ALU, NumMats };
enum Kind { RED, YELLOW, BLUE, BLACK };
enum State { MENU, PLAYING, WON, LOST };

struct BirdDef { float radius; Color color; };
const BirdDef Birds[] = {
    {0.50f, {0.85f, 0.15f, 0.15f}},
    {0.42f, {0.97f, 0.85f, 0.10f}},
    {0.35f, {0.30f, 0.60f, 0.95f}},
    {0.60f, {0.10f, 0.10f, 0.10f}},
};

constexpr float Unbreakable = 1e30f;

struct MatDef { float density, breakImpulse, friction, restitution; Color color; };
const MatDef Mats[NumMats] = {
    {0.8f, 9.0f, 0.65f, 0.15f, {0.65f, 0.45f, 0.25f}},
    {2.5f, Unbreakable, 0.70f, 0.05f, {0.55f, 0.55f, 0.60f}},
    {2.5f, 4.0f, 0.55f, 0.10f, {0.70f, 0.90f, 0.95f}},
    {7.8f, Unbreakable, 0.40f, 0.20f, {0.35f, 0.38f, 0.45f}},
    {2.7f, Unbreakable, 0.40f, 0.20f, {0.75f, 0.78f, 0.85f}},
};

inline float angleZ(const PxQuat& q) { return 2.f * std::atan2(q.z, q.w); }
struct Pose { float x, y, angle; };

struct Body {
    PxRigidDynamic* actor = nullptr;
    Type type = BLOCK;
    Mat mat = WOOD;
    Kind kind = RED;
    int id = 0;
    PxVec2 half{ 0, 0 };
    float radius = 0;
    float impulse = 0;
    float age = 0;
    bool abilityUsed = false;
    bool dead = false;
    bool fragment = false;
    bool noShatter = false;
    float armLen = 0;
    float mass = 0.f;

    PxVec3 prevPos{ 0, 0, 0 };
    float prevAngle = 0.f;

    void savePose() {
        PxTransform t = actor->getGlobalPose();
        prevPos = t.p;
        prevAngle = angleZ(t.q);
    }
    Pose pose(float alpha) const {
        PxTransform t = actor->getGlobalPose();
        float da = std::remainder(angleZ(t.q) - prevAngle, 2.f * cfg::Pi);
        return { prevPos.x + (t.p.x - prevPos.x) * alpha, prevPos.y + (t.p.y - prevPos.y) * alpha,
                prevAngle + da * alpha };
    }
};

struct Flash { float x, y, t; };
struct Fracture { PxVec3 pos, v, omega; PxVec2 half; float angle; Mat mat; };

// =============================================================
// Warstwa fizyki
// =============================================================
namespace phys {
    constexpr PxU32 BirdGroup = 1;
    constexpr PxU32 GhostGroup = 2;
    PxDefaultAllocator allocator;
    PxDefaultErrorCallback errorCallback;
    PxFoundation* foundation = nullptr;
    PxPhysics* physics = nullptr;
    PxDefaultCpuDispatcher* dispatcher = nullptr;
    PxScene* scene = nullptr;
    PxMaterial* creatureMat = nullptr;
    PxMaterial* matPx[NumMats] = {};
    PxRigidStatic* ground = nullptr;
    PxRigidStatic* leftWall = nullptr;
    PxRigidStatic* rightWall = nullptr;

    std::vector<Body*> bodies;
    int nextId = 1;

    struct Deco { float x, y, hx, hy; Color c; PxRigidStatic* actor; };
    std::vector<Deco> decos;
    std::vector<PxVec2> pivots;

    PxFilterFlags filterShader(PxFilterObjectAttributes, PxFilterData fd0, PxFilterObjectAttributes,
        PxFilterData fd1, PxPairFlags& pairFlags, const void*, PxU32) {
        if (fd0.word0 == GhostGroup || fd1.word0 == GhostGroup)
            return PxFilterFlag::eSUPPRESS;
        if (fd0.word0 == BirdGroup && fd1.word0 == BirdGroup)
            return PxFilterFlag::eSUPPRESS;
        pairFlags = PxPairFlag::eCONTACT_DEFAULT |
            PxPairFlag::eNOTIFY_TOUCH_FOUND |
            PxPairFlag::eNOTIFY_TOUCH_PERSISTS |
            PxPairFlag::eNOTIFY_CONTACT_POINTS |
            PxPairFlag::eDETECT_CCD_CONTACT;
        return PxFilterFlag::eDEFAULT;
    }

    Body* bodyOf(PxActor* a) {
        return (a && a->userData) ? static_cast<Body*>(a->userData) : nullptr;
    }

    constexpr float MinImpactSpeed = 0.5f;

    // Prędkość względna w punkcie kontaktu: v_rel = (v0 + w0 x r0) - (v1 + w1 x r1).
    PxVec3 relativeVelocityAt(PxActor* a, PxActor* b, const PxVec3& p) {
        auto dyn = [](PxActor* x) -> PxRigidDynamic* {
            return (x && x->is<PxRigidDynamic>()) ? static_cast<PxRigidDynamic*>(x) : nullptr;
            };
        PxRigidDynamic* d0 = dyn(a), * d1 = dyn(b);
        PxVec3 v0 = d0 ? d0->getLinearVelocity() : PxVec3(0);
        PxVec3 v1 = d1 ? d1->getLinearVelocity() : PxVec3(0);
        PxVec3 w0 = d0 ? d0->getAngularVelocity() : PxVec3(0);
        PxVec3 w1 = d1 ? d1->getAngularVelocity() : PxVec3(0);
        PxVec3 c0 = d0 ? d0->getGlobalPose().p : PxVec3(0);
        PxVec3 c1 = d1 ? d1->getGlobalPose().p : PxVec3(0);
        return (v0 + w0.cross(p - c0)) - (v1 + w1.cross(p - c1));
    }

    class ContactCallback : public PxSimulationEventCallback {
    public:
        void onContact(const PxContactPairHeader& header, const PxContactPair* pairs, PxU32 count) override {
            if (!header.actors[0] || !header.actors[1])
                return;
            Body* a = bodyOf(header.actors[0]);
            Body* b = bodyOf(header.actors[1]);
            for (PxU32 i = 0; i < count; ++i) {
                if (pairs[i].flags &
                    (PxContactPairFlag::eREMOVED_SHAPE_0 | PxContactPairFlag::eREMOVED_SHAPE_1))
                    continue;

                PxContactPairPoint points[16];
                PxU32 n = pairs[i].extractContacts(points, 16);
                if (n == 0)
                    continue;

                float sum = 0.f;
                for (PxU32 j = 0; j < n; ++j) {
                    PxVec3 vrel = relativeVelocityAt(header.actors[0], header.actors[1], points[j].position);
                    if (vrel.magnitude() < MinImpactSpeed)
                        continue;
                    sum += points[j].impulse.magnitude();
                }
                if (a) a->impulse += sum;
                if (b) b->impulse += sum;
            }
        }
        void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
        void onWake(PxActor**, PxU32) override {}
        void onSleep(PxActor**, PxU32) override {}
        void onTrigger(PxTriggerPair*, PxU32) override {}
        void onAdvance(const PxRigidBody* const*, const PxTransform*, PxU32) override {}
    } contactCallback;

    void init() {
        foundation = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errorCallback);
        physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, PxTolerancesScale(), true, nullptr);
        PxInitExtensions(*physics, nullptr);

        PxSceneDesc desc(physics->getTolerancesScale());
        desc.gravity = PxVec3(0.f, -cfg::Gravity, 0.f);
        dispatcher = PxDefaultCpuDispatcherCreate(2);
        desc.cpuDispatcher = dispatcher;
        desc.filterShader = filterShader;
        desc.simulationEventCallback = &contactCallback;
        desc.flags |= PxSceneFlag::eENABLE_CCD;
#if PX_PHYSICS_VERSION_MAJOR == 4
        desc.flags |= PxSceneFlag::eENABLE_STABILIZATION;
#endif
        scene = physics->createScene(desc);

        creatureMat = physics->createMaterial(0.6f, 0.5f, 0.15f);
        for (int i = 0; i < NumMats; ++i)
            matPx[i] = physics->createMaterial(Mats[i].friction, Mats[i].friction * 0.85f, Mats[i].restitution);

        ground = PxCreateStatic(*physics, PxTransform(PxVec3(16.f, -0.5f, 0.f)),
            PxBoxGeometry(20.f, 0.5f, 2.f), *creatureMat);
        scene->addActor(*ground);
        leftWall = PxCreateStatic(*physics, PxTransform(PxVec3(0.1f, 8.f, 0.f)),
            PxBoxGeometry(0.2f, 8.5f, 2.f), *creatureMat);
        rightWall = PxCreateStatic(*physics, PxTransform(PxVec3(cfg::ViewW - 0.1f, 8.f, 0.f)),
            PxBoxGeometry(0.2f, 8.5f, 2.f), *creatureMat);
        scene->addActor(*leftWall);
        scene->addActor(*rightWall);
    }

    void releaseBody(Body* b) {
        b->actor->release();
        delete b;
    }

    void clear() {
        for (Body* b : bodies)
            releaseBody(b);
        bodies.clear();
        for (Deco& d : decos)
            if (d.actor) d.actor->release();
        decos.clear();
        pivots.clear();
    }

    void shutdown() {
        clear();
        leftWall->release();
        rightWall->release();
        ground->release();
        for (int i = 0; i < NumMats; ++i) matPx[i]->release();
        creatureMat->release();
        scene->release();
        dispatcher->release();
        PxCloseExtensions();
        physics->release();
        foundation->release();
    }

    void step(float dt) {
        for (Body* b : bodies) b->savePose();
        scene->simulate(dt);
        scene->fetchResults(true);
    }

    Body* finish(Body* b, PxRigidDynamic* actor) {
        b->actor = actor;
        b->id = nextId++;
        actor->setRigidDynamicLockFlags(PxRigidDynamicLockFlag::eLOCK_LINEAR_Z |
            PxRigidDynamicLockFlag::eLOCK_ANGULAR_X |
            PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y);
        actor->setAngularDamping(cfg::AngularDamping);
        actor->setSolverIterationCounts(cfg::SolverPos, cfg::SolverVel);
        actor->userData = b;
        b->mass = actor->getMass();
        scene->addActor(*actor);
        b->savePose();
        bodies.push_back(b);
        return b;
    }

    Body* addBox(float x, float y, float hx, float hy, Mat mat, float angle = 0.f, bool fragment = false) {
        auto* b = new Body;
        b->type = BLOCK;
        b->mat = mat;
        b->half = PxVec2(hx, hy);
        b->fragment = fragment;
        PxTransform pose(PxVec3(x, y, 0.f), PxQuat(angle, PxVec3(0.f, 0.f, 1.f)));
        PxRigidDynamic* actor = PxCreateDynamic(*physics, pose, PxBoxGeometry(hx, hy, 0.5f),
            *matPx[mat], Mats[mat].density);
        if (fragment)
            actor->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
        return finish(b, actor);
    }

    Body* addSwing(float x, float y, float hx, float hy, float armLen, Mat mat) {
        auto* b = new Body;
        b->type = BLOCK;
        b->mat = mat;
        b->half = PxVec2(hx, hy);
        b->armLen = armLen;
        PxRigidDynamic* actor = PxCreateDynamic(*physics, PxTransform(PxVec3(x, y, 0.f)),
            PxBoxGeometry(hx, hy, 0.5f), *matPx[mat], Mats[mat].density);

        const float L = std::sqrt(hx * hx + armLen * armLen);
        const float phi = std::atan2(hx, armLen);
        for (int side = -1; side <= 1; side += 2) {
            PxShape* arm = PxRigidActorExt::createExclusiveShape(*actor, PxBoxGeometry(0.03f, L * 0.5f, 0.5f),
                *matPx[mat]);
            arm->setLocalPose(PxTransform(PxVec3(side * hx * 0.5f, armLen * 0.5f, 0.f),
                PxQuat(side * phi, PxVec3(0.f, 0.f, 1.f))));
            arm->setSimulationFilterData(PxFilterData(GhostGroup, 0, 0, 0));
        }
        PxRigidBodyExt::updateMassAndInertia(*actor, Mats[mat].density);
        actor->setSleepThreshold(0.f);
        return finish(b, actor);
    }

    Body* addBall(float x, float y, float radius, float density, Type type, Mat mat = STONE) {
        auto* b = new Body;
        b->type = type;
        b->radius = radius;
        b->mat = mat;
        PxMaterial* m = (type == BALL) ? matPx[mat] : creatureMat;
        PxRigidDynamic* actor = PxCreateDynamic(*physics, PxTransform(PxVec3(x, y, 0.f)),
            PxSphereGeometry(radius), *m, density);
        if (type == BIRD) {
            PxShape* shape = nullptr;
            actor->getShapes(&shape, 1);
            if (shape)
                shape->setSimulationFilterData(PxFilterData(BirdGroup, 0, 0, 0));
            actor->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
        }
        return finish(b, actor);
    }

    void setCom(Body* b, float dy) {
        PxVec3 com(0.f, dy, 0.f);
        PxRigidBodyExt::updateMassAndInertia(*b->actor, Mats[b->mat].density, &com);
        b->noShatter = true;
        b->mass = b->actor->getMass();
    }

    Body* find(int id) {
        for (Body* b : bodies) if (b->id == id) return b;
        return nullptr;
    }

    // Bezpieczne usuwanie: erase w pętli, bez efektów ubocznych w predykacie remove_if.
    void removeDead() {
        for (auto it = bodies.begin(); it != bodies.end();) {
            Body* b = *it;
            if (b->dead) {
                releaseBody(b);
                it = bodies.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    void addDeco(float x, float y, float hx, float hy, Color c, bool solid) {
        PxRigidStatic* a = nullptr;
        if (solid) {
            a = PxCreateStatic(*physics, PxTransform(PxVec3(x, y, 0.f)), PxBoxGeometry(hx, hy, 2.f), *creatureMat);
            scene->addActor(*a);
        }
        decos.push_back({ x, y, hx, hy, c, a });
    }

    Fracture makeFracture(const Body& b) {
        PxTransform t = b.actor->getGlobalPose();
        return { t.p, b.actor->getLinearVelocity(), b.actor->getAngularVelocity(),
                b.half, angleZ(t.q), b.mat };
    }

    // Rozpad: pęd całkowity zachowany (v_i = v + omega x r_i, suma m_i r_i = 0).
    // Moment pędu przybliżony - fragmenty dzielą bryłę na skończoną siatkę, więc
    // część orbitalna Sum m_i r_i x (omega x r_i) nie trafia do ich tensora bezwładności.
    // Dla ciał z przesuniętym COM (setCom) rozpad jest wyłączony - patrz noShatter.
    void shatter(const Fracture& f) {
        int nx, ny;
        if (f.mat == GLASS) { nx = 2; ny = 2; }
        else if (f.half.y > f.half.x) { nx = 1; ny = 3; }
        else { nx = 3; ny = 1; }
        const float hx = f.half.x / nx, hy = f.half.y / ny;
        const float c = std::cos(f.angle), s = std::sin(f.angle);

        for (int ix = 0; ix < nx; ++ix)
            for (int iy = 0; iy < ny; ++iy) {
                float lx = -f.half.x + hx * (2 * ix + 1);
                float ly = -f.half.y + hy * (2 * iy + 1);
                PxVec3 center(f.pos.x + lx * c - ly * s, f.pos.y + lx * s + ly * c, 0.f);
                Body* n = addBox(center.x, center.y, hx, hy, f.mat, f.angle, true);
                n->actor->setLinearVelocity(f.v + f.omega.cross(center - f.pos));
                n->actor->setAngularVelocity(f.omega);
            }
    }

    PxQuat axisZ() { return PxQuat(-0.5f * cfg::Pi, PxVec3(0.f, 1.f, 0.f)); }
    PxTransform frameOn(PxRigidActor* a, const PxTransform& world) {
        return a ? a->getGlobalPose().transformInv(world) : world;
    }

    PxRevoluteJoint* hinge(Body* a, Body* b, PxVec3 p) {
        PxTransform w(p, axisZ());
        PxRigidActor* A = a ? static_cast<PxRigidActor*>(a->actor) : nullptr;
        return PxRevoluteJointCreate(*physics, A, frameOn(A, w), b->actor, frameOn(b->actor, w));
    }
} // namespace phys

// =============================================================
// Budowa poziomu
// =============================================================
namespace levels {
    void buildTower(float cx, int floors) {
        constexpr float hw = 1.5f;
        for (int k = 0; k < floors; ++k) {
            float y0 = k * 2.3f;
            Mat pm = (k == 0) ? GLASS : WOOD;
            phys::addBox(cx - hw, y0 + 0.9f, 0.3f, 0.9f, pm);
            phys::addBox(cx + hw, y0 + 0.9f, 0.3f, 0.9f, pm);
            phys::addBox(cx, y0 + 2.05f, 1.9f, 0.25f, WOOD);
            if (k == 1 || k == 3)
                phys::addBall(cx, y0 + 0.5f, 0.5f, cfg::PigDensity, PIG);
        }
        phys::addBall(cx, floors * 2.3f + 0.5f, 0.5f, cfg::PigDensity, PIG);
    }

    void buildHut(float cx, Mat pillarMat) {
        phys::addBox(cx - 0.9f, 0.6f, 0.2f, 0.6f, pillarMat);
        phys::addBox(cx + 0.9f, 0.6f, 0.2f, 0.6f, pillarMat);
        phys::addBox(cx, 1.35f, 1.3f, 0.15f, WOOD);
        phys::addBall(cx, 0.5f, 0.5f, cfg::PigDensity, PIG);
        phys::addBall(cx, 1.5f + 0.5f, 0.5f, cfg::PigDensity, PIG);
    }

    void buildDominoes(float x0, int n, float spacing) {
        for (int i = 0; i < n; ++i)
            phys::addBox(x0 + i * spacing, 0.7f, 0.12f, 0.7f, WOOD);
    }

    void buildPendulum(float ax, float ay, int links, float ballR) {
        const float L = 0.7f;
        Body* prev = nullptr;
        for (int i = 0; i < links; ++i) {
            Body* link = phys::addBox(ax, ay - 0.5f * L - i * L, 0.09f, 0.5f * L, METAL);
            phys::hinge(prev, link, PxVec3(ax, ay - i * L, 0.f));
            prev = link;
        }
        Body* ball = phys::addBall(ax, ay - links * L - ballR, ballR, Mats[STONE].density, BALL, STONE);
        phys::hinge(prev, ball, PxVec3(ax, ay - links * L, 0.f));
        phys::pivots.push_back(PxVec2(ax, ay));
        phys::addDeco(ax, (ay + cfg::ViewH) * 0.5f, 0.08f, (cfg::ViewH - ay) * 0.5f, color::Frame, false);
    }

    void buildSwing(float cx, float topY, float len) {
        phys::addDeco(cx, topY + 0.15f, 2.0f, 0.15f, color::Frame, false);
        phys::addDeco(cx, (topY + 0.3f + cfg::ViewH) * 0.5f, 0.15f,
            (cfg::ViewH - topY - 0.3f) * 0.5f, color::Frame, false);

        Body* seat = phys::addSwing(cx, topY - len, 1.2f, 0.15f, len, ALU);
        phys::hinge(nullptr, seat, PxVec3(cx, topY, 0.f));
        phys::pivots.push_back(PxVec2(cx, topY));
        phys::addBall(cx, topY - len + 0.15f + 0.5f, 0.5f, cfg::PigDensity, PIG);
    }

    void buildTopHeavyColumn(float x) {
        Body* col = phys::addBox(x, 1.5f, 0.4f, 1.5f, WOOD);
        phys::setCom(col, 0.5f);
        phys::addBall(x, 3.5f, 0.5f, cfg::PigDensity, PIG);
    }

    std::vector<Kind> build() {
        buildHut(11.7f, GLASS);              // szklany domek
        buildDominoes(17.2f, 5, 0.8f);       // rząd pięciu kostek domina
        buildPendulum(19.4f, 7.6f, 4, 0.6f); // wahadło z kulą
        buildTower(23.0f, 4);                // czteropiętrowa wieża
        buildSwing(27.2f, 10.0f, 4.0f);      // huśtawka ze świnią
        buildHut(27.2f, WOOD);               // drewniany domek
        buildTopHeavyColumn(30.5f);          // niestabilny słup z balastem

        return { RED, BLUE, YELLOW, BLACK, RED, YELLOW, BLACK, BLUE, RED, BLACK };
    }
} // namespace levels

// =============================================================
// Grafika 2D
// =============================================================
namespace gfx {
    int fbW = cfg::WindowW, fbH = cfg::WindowH;
    int vpX = 0, vpY = 0, vpW = cfg::WindowW, vpH = cfg::WindowH;
    float fracX = 0.f, fracY = 0.f, fracW = 1.f, fracH = 1.f;

    void layout(GLFWwindow* win) {
        glfwGetFramebufferSize(win, &fbW, &fbH);
        if (fbW <= 0 || fbH <= 0) return;
        const float aspect = cfg::ViewW / cfg::ViewH;
        vpW = fbW; vpH = (int)(fbW / aspect);
        if (vpH > fbH) { vpH = fbH; vpW = (int)(fbH * aspect); }
        vpX = (fbW - vpW) / 2; vpY = (fbH - vpH) / 2;
        fracX = (float)vpX / fbW; fracY = (float)vpY / fbH;
        fracW = (float)vpW / fbW; fracH = (float)vpH / fbH;
    }

    void init() {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    void beginFrame(GLFWwindow* win) {
        (void)win;
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glViewport(vpX, vpY, vpW, vpH);
        glEnable(GL_SCISSOR_TEST);
        glScissor(vpX, vpY, vpW, vpH);
        glClearColor(color::Sky.r, color::Sky.g, color::Sky.b, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, cfg::ViewW, 0.0, cfg::ViewH, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }

    void quad(float x, float y, float hx, float hy, float angle, Color c) {
        glColor4f(c.r, c.g, c.b, c.a);
        glPushMatrix();
        glTranslatef(x, y, 0.f);
        glRotatef(angle * 180.f / cfg::Pi, 0.f, 0.f, 1.f);
        glScalef(hx, hy, 1.f);
        glBegin(GL_QUADS);
        glVertex2f(-1.f, -1.f); glVertex2f(1.f, -1.f);
        glVertex2f(1.f, 1.f);   glVertex2f(-1.f, 1.f);
        glEnd();
        glPopMatrix();
    }

    void circle(float x, float y, float radius, Color c) {
        constexpr int segments = 40;
        glColor4f(c.r, c.g, c.b, c.a);
        glPushMatrix();
        glTranslatef(x, y, 0.f);
        glScalef(radius, radius, 1.f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(0.f, 0.f);
        for (int i = 0; i <= segments; ++i) {
            float t = 2.f * cfg::Pi * i / segments;
            glVertex2f(std::cos(t), std::sin(t));
        }
        glEnd();
        glPopMatrix();
    }

    void line(PxVec2 a, PxVec2 b, float thickness, Color c) {
        PxVec2 d = b - a;
        quad((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, d.magnitude() * 0.5f, thickness,
            std::atan2(d.y, d.x), c);
    }
} // namespace gfx

// =============================================================
// Tekst
// =============================================================
namespace text {
    constexpr char FontPath[] = "PressStart2P-Regular.ttf";
    constexpr int AtlasSize = 512;
    constexpr float FontPx = 8.f;
    struct Range { int first, count; };
    constexpr Range Ranges[] = { {0x20, 95}, {0xA0, 96}, {0x100, 128} };
    constexpr int NumRanges = 3;
    stbtt_packedchar glyphs[NumRanges][128];
    GLuint texture = 0;
    bool ready = false;

    float snap(float v) { return std::floor(v + 0.5f); }

    bool readFile(const char* path, std::vector<unsigned char>& out) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return false;
        std::streamsize size = file.tellg();
        if (size <= 0) return false;
        out.resize((size_t)size);
        file.seekg(0, std::ios::beg);
        return (bool)file.read(reinterpret_cast<char*>(out.data()), size);
    }

    void init() {
        std::vector<unsigned char> ttf;
        if (!readFile(FontPath, ttf)) {
            std::fprintf(stderr, "Brak pliku czcionki: %s\n", FontPath);
            return;
        }
        std::vector<unsigned char> alpha(AtlasSize * AtlasSize);
        stbtt_pack_context pc;
        if (!stbtt_PackBegin(&pc, alpha.data(), AtlasSize, AtlasSize, 0, 1, nullptr))
            return;
        stbtt_PackSetOversampling(&pc, 1, 1);
        for (int i = 0; i < NumRanges; ++i)
            if (!stbtt_PackFontRange(&pc, ttf.data(), 0, STBTT_POINT_SIZE(FontPx),
                Ranges[i].first, Ranges[i].count, glyphs[i]))
                std::fprintf(stderr, "Atlas pełny: zakres 0x%X\n", Ranges[i].first);
        stbtt_PackEnd(&pc);
        std::vector<unsigned char> rgba(AtlasSize * AtlasSize * 4, 255);
        for (int i = 0; i < AtlasSize * AtlasSize; ++i)
            rgba[i * 4 + 3] = alpha[i];
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, AtlasSize, AtlasSize, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        ready = true;
    }

    int decode(const char*& s) {
        unsigned char c = (unsigned char)*s++;
        if (c < 0x80) return c;
        if ((c >> 5) == 6 && *s)
            return ((c & 0x1F) << 6) | ((unsigned char)*s++ & 0x3F);
        if ((c >> 4) == 14 && s[0] && s[1]) {
            int cp = ((c & 0x0F) << 12) | (((unsigned char)s[0] & 0x3F) << 6) |
                ((unsigned char)s[1] & 0x3F);
            s += 2;
            return cp;
        }
        return '?';
    }

    const stbtt_packedchar* glyph(int cp) {
        for (int i = 0; i < NumRanges; ++i)
            if (cp >= Ranges[i].first && cp < Ranges[i].first + Ranges[i].count)
                return &glyphs[i][cp - Ranges[i].first];
        return cp == '?' ? nullptr : glyph('?');
    }

    int scale(float size) {
        float pixelsPerUnit = (float)gfx::vpW / cfg::ViewW;
        return std::max(1, (int)std::lround(5.5f * size * pixelsPerUnit / FontPx));
    }

    float width(const char* s, float size) {
        int sc = scale(size);
        float px = 0.f;
        for (const char* p = s; *p;)
            if (const stbtt_packedchar* g = glyph(decode(p)))
                px += snap(g->xadvance * sc);
        return px / gfx::vpW * cfg::ViewW;
    }

    void draw(const char* s, float x, float y, float size, Color c) {
        if (!ready) return;
        int sc = scale(size);
        float px = std::floor(x / cfg::ViewW * gfx::vpW);
        float py = std::floor((1.f - y / cfg::ViewH) * gfx::vpH);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0.0, gfx::vpW, gfx::vpH, 0.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture);
        glColor4f(c.r, c.g, c.b, c.a);
        glBegin(GL_QUADS);
        for (const char* p = s; *p;) {
            const stbtt_packedchar* g = glyph(decode(p));
            if (!g) continue;
            float x0 = px + snap(g->xoff * sc), x1 = px + snap(g->xoff2 * sc);
            float y0 = py + snap(g->yoff * sc), y1 = py + snap(g->yoff2 * sc);
            float u0 = g->x0 / (float)AtlasSize, u1 = g->x1 / (float)AtlasSize;
            float v0 = g->y0 / (float)AtlasSize, v1 = g->y1 / (float)AtlasSize;
            glTexCoord2f(u0, v0); glVertex2f(x0, y0);
            glTexCoord2f(u1, v0); glVertex2f(x1, y0);
            glTexCoord2f(u1, v1); glVertex2f(x1, y1);
            glTexCoord2f(u0, v1); glVertex2f(x0, y1);
            px += snap(g->xadvance * sc);
        }
        glEnd();
        glDisable(GL_TEXTURE_2D);
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
    }

    void drawCentered(const char* s, float cx, float cy, float size, Color c) {
        float baselineShift = 3.5f * scale(size) * (cfg::ViewH / (float)gfx::vpH);
        draw(s, cx - width(s, size) * 0.5f, cy - baselineShift, size, c);
    }
} // namespace text

// =============================================================
// UI
// =============================================================
struct Button {
    float x, y, w, h;
    const char* label;
    bool contains(PxVec2 m) const {
        return std::fabs(m.x - x) < w * 0.5f && std::fabs(m.y - y) < h * 0.5f;
    }
    void draw(bool hover) const {
        float k = hover ? 1.15f : 1.f;
        gfx::quad(x + 0.12f, y - 0.12f, w * 0.5f, h * 0.5f, 0.f, { 0.f, 0.f, 0.f, 0.35f });
        gfx::quad(x, y, w * 0.5f, h * 0.5f, 0.f, { 0.15f, 0.10f, 0.05f });
        gfx::quad(x, y, w * 0.5f - 0.1f, h * 0.5f - 0.1f, 0.f,
            { std::min(1.f, 0.90f * k), std::min(1.f, 0.55f * k), std::min(1.f, 0.10f * k) });
        text::drawCentered(label, x, y, 0.16f, color::White);
    }
};

namespace ui {
    const Button Play{ 16.f, 8.f, 6.f, 1.8f, "PLAY" };
    const Button Quit{ 16.f, 5.5f, 6.f, 1.8f, "QUIT" };
    const Button Retry{ 12.8f, 6.5f, 6.f, 1.8f, "RETRY" };
    const Button MenuRight{ 19.2f, 6.5f, 6.f, 1.8f, "MENU" };
}

// =============================================================
// Wejście
// =============================================================
struct Input {
    PxVec2 mouse;
    bool lmb = false;
    bool clicked = false;
    bool escPressed = false, rPressed = false;

    void poll(GLFWwindow* win) {
        double mx, my;
        int ww, wh;
        glfwGetCursorPos(win, &mx, &my);
        glfwGetWindowSize(win, &ww, &wh);
        float nx = (float)(mx / ww), ny = 1.f - (float)(my / wh);
        mouse = PxVec2((nx - gfx::fracX) / gfx::fracW * cfg::ViewW,
            (ny - gfx::fracY) / gfx::fracH * cfg::ViewH);

        bool l = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool e = glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        bool r = glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS;
        clicked = l && !lmb;
        escPressed = e && !escPrev;
        rPressed = r && !rPrev;
        lmb = l;
        escPrev = e;
        rPrev = r;
    }

private:
    bool escPrev = false, rPrev = false;
};

// =============================================================
// Klasa gry
// =============================================================
class Game {
public:
    bool quit = false;

    void update(const Input& in, double frameDt) {
        mouse = in.mouse;
        handleInput(in);
        simulate(frameDt);
    }

    void draw() const {
        gfx::quad(16.f, -0.5f, 20.f, 0.5f, 0.f, color::Ground);
        if (state == MENU) { drawMenu(); return; }

        const float alpha = (float)(accumulator / cfg::Dt);
        for (const phys::Deco& d : phys::decos)
            gfx::quad(d.x, d.y, d.hx, d.hy, 0.f, d.c);
        drawSling();
        for (const Body* b : phys::bodies)
            drawBody(*b, alpha);
        for (const PxVec2& p : phys::pivots) {
            gfx::circle(p.x, p.y, 0.18f, color::Black);
            gfx::circle(p.x, p.y, 0.07f, color::White);
        }
        drawExplosions();
        drawActiveBird();
        drawQueue();
        drawHud();
        if (state != PLAYING) drawOverlay();
    }

private:
    State state = MENU;
    PxVec2 mouse;
    std::vector<Kind> queue;
    size_t birdIdx = 0;
    int lastBirdId = 0;
    std::vector<Flash> flashes;
    bool dragging = false;
    PxVec2 dragPos = cfg::Anchor;
    double accumulator = 0.0;
    float levelTime = 0.f;
    float sinceLaunch = 10.f;
    float winTimer = 0.f;
    float loseTimer = 0.f;
    float totalPlayTime = 0.f;
    bool wasSettling = false; // pole klasy zamiast static w applyDamageRules

    void clearWorld() {
        phys::clear();
        flashes.clear();
    }

    void startLevel() {
        clearWorld();
        queue = levels::build();
        birdIdx = 0;
        lastBirdId = 0;
        dragging = false;
        levelTime = 0.f;
        sinceLaunch = 10.f;
        winTimer = 0.f;
        loseTimer = 0.f;
        totalPlayTime = 0.f;
        accumulator = 0.0;
        wasSettling = false; // jawny reset przy starcie poziomu
        state = PLAYING;
    }

    void toMenu() {
        clearWorld();
        dragging = false;
        state = MENU;
    }

    void handleInput(const Input& in) {
        if (state == MENU) {
            if (in.escPressed || (in.clicked && ui::Quit.contains(mouse)))
                quit = true;
            else if (in.clicked && ui::Play.contains(mouse))
                startLevel();
            return;
        }
        if (in.escPressed) { toMenu(); return; }
        switch (state) {
        case PLAYING:
            if (in.rPressed) startLevel();
            else handleAiming(in);
            break;
        case WON:
        case LOST:
            if (in.clicked && ui::Retry.contains(mouse)) startLevel();
            else if (in.clicked && ui::MenuRight.contains(mouse)) toMenu();
            break;
        default: break;
        }
    }

    bool canAim() const {
        return birdIdx < queue.size() && sinceLaunch > cfg::ReloadDelay &&
            levelTime > cfg::SettleTime;
    }

    void handleAiming(const Input& in) {
        if (in.clicked && !dragging) {
            if ((mouse - cfg::Anchor).magnitude() < cfg::GrabRadius) {
                if (canAim()) dragging = true;
            }
            else {
                useAbility();
            }
        }
        if (!dragging) return;

        PxVec2 pull = mouse - cfg::Anchor;
        float len = pull.magnitude();
        if (len > cfg::MaxPull) pull *= cfg::MaxPull / len;
        dragPos = cfg::Anchor + pull;
        dragPos.y = std::max(dragPos.y, Birds[queue[birdIdx]].radius);

        if (!in.lmb) launch();
    }

    // v = x * sqrt(k/m); m bierzemy z definicji (jedna kula o zadanej gęstości),
    // żeby nie tworzyć ptaka tylko po to, by odczytać masę.
    PxVec2 launchVelocity(Kind k) const {
        float r = Birds[k].radius;
        float mass = cfg::BirdDensity * (4.f / 3.f) * cfg::Pi * r * r * r;
        return (cfg::Anchor - dragPos) * std::sqrt(cfg::SlingStiffness / mass);
    }

    void launch() {
        Kind k = queue[birdIdx++];
        Body* bird = phys::addBall(dragPos.x, dragPos.y, Birds[k].radius, cfg::BirdDensity, BIRD);
        bird->kind = k;
        PxVec2 v = launchVelocity(k);
        bird->actor->setLinearVelocity(PxVec3(v.x, v.y, 0.f));
        lastBirdId = bird->id;
        sinceLaunch = 0.f;
        dragging = false;
    }

    void useAbility() {
        Body* b = phys::find(lastBirdId);
        if (!b || b->abilityUsed) return;
        b->abilityUsed = true;
        switch (b->kind) {
        case YELLOW:
            b->actor->setLinearVelocity(b->actor->getLinearVelocity() * cfg::YellowBoost);
            break;
        case BLUE:
            split(*b);
            break;
        case BLACK:
            explode(*b);
            break;
        default: break;
        }
    }

    // Podział BLUE: pęd zachowany (3 równe masy, symetryczne odchylenie boczne).
    void split(Body& b) {
        PxVec3 p = b.actor->getGlobalPose().p;
        PxVec3 v = b.actor->getLinearVelocity();
        float r = b.radius / std::cbrt(3.f);

        PxShape* shape = nullptr;
        b.actor->getShapes(&shape, 1);
        if (shape)
            shape->setGeometry(PxSphereGeometry(r));
        b.radius = r;
        PxRigidBodyExt::updateMassAndInertia(*b.actor, cfg::BirdDensity);
        b.mass = b.actor->getMass();

        float vLen = v.magnitude();
        PxVec3 perp = vLen > 0.01f ? PxVec3(-v.y, v.x, 0.f) / vLen : PxVec3(0.f, 1.f, 0.f);
        float dv = vLen * std::tan(cfg::SplitAngle);

        for (int side = -1; side <= 1; side += 2) {
            Body* n = phys::addBall(p.x, p.y, r, cfg::BirdDensity, BIRD);
            n->kind = b.kind;
            n->abilityUsed = true;
            n->actor->setLinearVelocity(v + perp * (dv * side));
        }
    }

    // Wybuch: impuls J malejący liniowo z odległością. Cięższe ciała odrzucane słabiej.
    void explode(Body& bomb) {
        PxVec3 p = bomb.actor->getGlobalPose().p;
        for (Body* o : phys::bodies) {
            if (o == &bomb || o->dead) continue;

            PxVec3 d = o->actor->getGlobalPose().p - p;
            d.z = 0.f;
            float dist = d.magnitude();
            if (dist >= cfg::BombRadius) continue;

            float J = cfg::BombImpulse * (1.f - dist / cfg::BombRadius);
            PxVec3 dir = dist > 0.001f ? d / dist : PxVec3(0.f, 1.f, 0.f);
            o->actor->addForce(dir * J, PxForceMode::eIMPULSE);
            o->impulse += J;
        }
        flashes.push_back({ p.x, p.y, 0.f });
        bomb.dead = true;
    }

    void simulate(double frameDt) {
        if (state == MENU) { accumulator = 0.0; return; }
        accumulator += frameDt;
        while (accumulator >= cfg::Dt) {
            phys::step(cfg::Dt);
            levelTime += cfg::Dt;
            sinceLaunch += cfg::Dt;
            totalPlayTime += cfg::Dt;
            applyDamageRules();
            phys::removeDead();
            updateFlashes();
            checkEnd();
            accumulator -= cfg::Dt;
        }
    }

    // Reguły zniszczeń. Faza "settling" wyłącza obrażenia, ale nie akumulację impulsów -
    // przy przejściu settling -> !settling zerujemy impulse, żeby śmieci z fazy
    // osiadania nie wywołały fałszywego pęknięcia w pierwszym "dorosłym" kroku.
    void applyDamageRules() {
        const bool settling = levelTime < cfg::SettleTime;
        if (!settling && wasSettling) {
            for (Body* b : phys::bodies) b->impulse = 0.f;
        }
        wasSettling = settling;

        std::vector<Fracture> toBreak;

        for (Body* b : phys::bodies) {
            b->age += cfg::Dt;

            if (!settling) {
                if (b->type == PIG && b->impulse > cfg::PigDeathImpulse) {
                    b->dead = true;
                }
                else if (b->type == BLOCK && b->impulse > Mats[b->mat].breakImpulse) {
                    b->dead = true;
                    if (!b->fragment && !b->noShatter)
                        toBreak.push_back(phys::makeFracture(*b));
                }
            }

            if (b->type == BIRD && b->age > cfg::BirdLifetime)
                b->dead = true;

            PxVec3 p = b->actor->getGlobalPose().p;
            if (p.y < -6.f || p.x > 50.f || p.x < -10.f)
                b->dead = true;

            b->impulse = 0.f;
        }

        for (const Fracture& f : toBreak)
            phys::shatter(f);
    }

    void updateFlashes() {
        for (Flash& f : flashes) f.t += cfg::Dt;
        flashes.erase(std::remove_if(flashes.begin(), flashes.end(),
            [](const Flash& f) { return f.t > cfg::FlashTime; }),
            flashes.end());
    }

    int pigsAlive() const {
        int pigs = 0;
        for (const Body* b : phys::bodies)
            if (b->type == PIG && !b->dead) ++pigs;
        return pigs;
    }

    // Ptak "w locie" także wtedy, gdy wiruje w miejscu (np. utknął na zawiasie).
    bool birdInFlight() const {
        for (const Body* b : phys::bodies) {
            if (b->type != BIRD || b->dead) continue;
            if (b->actor->getLinearVelocity().magnitude() > cfg::BirdRestSpeed) return true;
            if (b->actor->getAngularVelocity().magnitude() > cfg::BirdRestAngular) return true;
        }
        return false;
    }

    void checkEnd() {
        if (state != PLAYING) return;
        if (pigsAlive() == 0) {
            winTimer += cfg::Dt;
            if (winTimer > cfg::WinDelay) state = WON;
            return;
        }
        winTimer = 0.f;
        if (birdIdx >= queue.size()) {
            if (sinceLaunch > cfg::LoseHardTimeout) { state = LOST; return; }
            if (!birdInFlight()) {
                loseTimer += cfg::Dt;
                if (loseTimer > cfg::LoseDelay) state = LOST;
            }
            else loseTimer = 0.f;
        }
        else loseTimer = 0.f;
    }

    // ---------------------------------------------------------
    // Rysowanie
    // ---------------------------------------------------------
    static void drawBirdRotated(Kind k, float x, float y, float r, float angle) {
        float c = std::cos(angle), s = std::sin(angle);
        gfx::circle(x, y, r, Birds[k].color);
        float ex = x + r * (0.35f * c - 0.30f * s), ey = y + r * (0.35f * s + 0.30f * c);
        gfx::circle(ex, ey, r * 0.28f, color::White);
        gfx::circle(ex + r * 0.05f * c, ey + r * 0.05f * s, r * 0.13f, color::Black);
    }
    static void drawBird(Kind k, float x, float y, float r) {
        drawBirdRotated(k, x, y, r, 0.f);
    }
    static void drawPig(float x, float y, float r, float angle) {
        float c = std::cos(angle), s = std::sin(angle);
        gfx::circle(x, y, r, color::Pig);
        gfx::circle(x - 0.2f * c, y - 0.2f * s, r * 0.18f, color::White);
        gfx::circle(x + 0.2f * c, y + 0.2f * s, r * 0.18f, color::White);
    }
    static void drawBall(const Body& b, float x, float y, float a) {
        Color c = Mats[b.mat].color;
        gfx::circle(x, y, b.radius, c);
        gfx::circle(x + 0.5f * b.radius * std::cos(a), y + 0.5f * b.radius * std::sin(a),
            b.radius * 0.22f, { c.r * 0.5f, c.g * 0.5f, c.b * 0.5f });
    }
    static void drawBody(const Body& b, float alpha) {
        Pose p = b.pose(alpha);
        switch (b.type) {
        case BLOCK: {
            Color c = Mats[b.mat].color;
            if (b.fragment) { c.r *= 0.75f; c.g *= 0.75f; c.b *= 0.75f; }
            gfx::quad(p.x, p.y, b.half.x, b.half.y, p.angle, c);
            if (b.armLen > 0.f) {
                float cs = std::cos(p.angle), sn = std::sin(p.angle);
                PxVec2 top(p.x - b.armLen * sn, p.y + b.armLen * cs);
                for (int side = -1; side <= 1; side += 2) {
                    float lx = side * b.half.x;
                    gfx::line(PxVec2(p.x + lx * cs, p.y + lx * sn), top, 0.04f, color::SlingBand);
                }
            }
            break;
        }
        case PIG:  drawPig(p.x, p.y, b.radius, p.angle); break;
        case BIRD: drawBirdRotated(b.kind, p.x, p.y, b.radius, p.angle); break;
        case BALL: drawBall(b, p.x, p.y, p.angle); break;
        }
    }

    static void drawSling() {
        gfx::quad(cfg::Anchor.x, 1.5f, 0.2f, 1.7f, 0.f, color::Sling);
    }

    void drawExplosions() const {
        for (const Flash& f : flashes) {
            float k = f.t / cfg::FlashTime;
            Color c = color::Explosion;
            c.a = 0.6f * (1.f - k);
            gfx::circle(f.x, f.y, cfg::BombRadius * k, c);
        }
    }

    // Rysuje procę, ptaka i tor lotu. Gdy ptak jest załadowany (nie przeciągany),
    // rysujemy też cięciwę w spoczynku - między końcami widełek a ptakiem.
    void drawActiveBird() const {
        if (state != PLAYING || birdIdx >= queue.size()) return;
        Kind k = queue[birdIdx];
        const PxVec2 left(cfg::Anchor.x - 0.2f, cfg::Anchor.y);
        const PxVec2 right(cfg::Anchor.x + 0.2f, cfg::Anchor.y);

        if (dragging) {
            gfx::line(left, dragPos, 0.06f, color::SlingBand);
            gfx::line(right, dragPos, 0.06f, color::SlingBand);
            drawBird(k, dragPos.x, dragPos.y, Birds[k].radius);

            PxVec2 v = launchVelocity(k);
            for (int i = 1; i <= 28; ++i) {
                float t = i * 0.1f;
                float a = 1.f - i / 32.f;
                gfx::circle(dragPos.x + v.x * t,
                    dragPos.y + v.y * t - 0.5f * cfg::Gravity * t * t,
                    0.08f, { 1.f, 1.f, 1.f, a });
            }
        }
        else if (sinceLaunch > cfg::ReloadDelay) {
            PxVec2 at(cfg::Anchor.x, cfg::Anchor.y);
            gfx::line(left, at, 0.06f, color::SlingBand);
            gfx::line(right, at, 0.06f, color::SlingBand);
            drawBird(k, at.x, at.y, Birds[k].radius);
        }
    }

    void drawQueue() const {
        if (state != PLAYING) return;
        for (size_t i = birdIdx + 1, n = 0; i < queue.size(); ++i, ++n) {
            Kind k = queue[i];
            float r = Birds[k].radius * cfg::QueueScale;
            float x = cfg::QueueStartX - (float)n * cfg::QueueSpacing;
            if (x < 0.7f) break;
            drawBird(k, x, r, r);
        }
    }

    void drawHud() const {
        if (state != PLAYING) return;
        char buf[96];
        int birdsLeft = (int)(queue.size() - birdIdx);
        std::snprintf(buf, sizeof buf, "BIRDS %d  PIGS %d", birdsLeft, pigsAlive());
        text::draw(buf, 0.5f, 16.6f, 0.14f, color::Hud);
    }

    void drawMenu() const {
        text::drawCentered("ANGRY BIRDS", 16.f + 0.15f, 13.5f - 0.15f, 0.3f, color::Black);
        text::drawCentered("ANGRY BIRDS", 16.f, 13.5f, 0.3f, { 0.9f, 0.25f, 0.15f });
        for (int i = 0; i < 4; ++i)
            drawBird((Kind)i, 10.f + i * 4.f, 11.f, Birds[i].radius * 1.3f);
        ui::Play.draw(ui::Play.contains(mouse));
        ui::Quit.draw(ui::Quit.contains(mouse));
    }

    void drawOverlay() const {
        gfx::quad(16.f, 9.f, 16.f, 9.f, 0.f, { 0.f, 0.f, 0.f, 0.45f });
        auto button = [&](const Button& b) { b.draw(b.contains(mouse)); };
        if (state == WON)
            text::drawCentered("YOU WIN", 16.f, 11.f, 0.3f, { 1.f, 0.95f, 0.3f });
        else if (state == LOST)
            text::drawCentered("GAME OVER", 16.f, 11.f, 0.25f, { 1.f, 0.3f, 0.3f });
        button(ui::Retry);
        button(ui::MenuRight);
    }
};

// =============================================================
// Punkt wejścia
// =============================================================
int main() {
    glfwSetErrorCallback([](int code, const char* msg) {
        std::fprintf(stderr, "GLFW error %d: %s\n", code, msg);
        });
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    GLFWwindow* win = glfwCreateWindow(cfg::WindowW, cfg::WindowH,
        "Angry Birds - PhysX + OpenGL", nullptr, nullptr);
    if (!win) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    gfx::layout(win);
    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) return -1;

    gfx::init();
    text::init();
    phys::init();

    Game game;
    Input input;
    double prev = glfwGetTime();
    while (!glfwWindowShouldClose(win) && !game.quit) {
        glfwPollEvents();
        gfx::layout(win);
        input.poll(win);
        double now = glfwGetTime();
        game.update(input, std::min(now - prev, 0.1));
        prev = now;
        gfx::beginFrame(win);
        game.draw();
        glfwSwapBuffers(win);
    }

    phys::shutdown();
    glfwTerminate();
    return 0;
}