// Angry Birds - jeden rozbudowany poziom (PhysX + OpenGL).
// Fizyka 2D realizowana w 3D: wszystkie ciała mają zablokowany ruch wzdłuż Z
// oraz obroty wokół X i Y (ruch w płaszczyźnie XY, obrót tylko wokół Z).
// Poziom demonstruje: grawitację, kolizje, tarcie, pęd, moment obrotowy,
// jointy (zawias, wahadło), środek ciężkości, napęd silnikowy i zniszczenia.
//
// Zasady spójności fizycznej użyte w całym kodzie:
//  - masa każdego ciała = gęstość materiału * objętość (nigdzie nie ustawiamy masy "z ręki"),
//  - obrażenia = impuls kontaktowy [N*s] zgłoszony przez PhysX (wybuch: impuls J),
//  - rozpad zachowuje pęd całkowity; moment pędu jest kompensowany przez nadanie
//    fragmentom dodatkowej prędkości kątowej (patrz Fizyka::shatter).
//  - ciała z przesuniętym COM są oznaczone jako nierozpadowe (setCom).
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


// Centralizuje parametry, które sterują rozmiarem okna i regułami gry.
class Konfiguracja {
public:
    // Rozmiar okna i logiczny rozmiar świata gry, niezależny od rozdzielczości ekranu.
    static constexpr int WindowW = 1280, WindowH = 720;
    static constexpr float ViewW = 32.f, ViewH = 18.f;
    static constexpr float Pi = 3.14159265f;
    // Grawitacja działa w dół osi Y; Dt jest stałym krokiem symulacji fizyki.
    static constexpr float Gravity = 9.81f;
    static constexpr float Dt = 1.f / 60.f;

    // Parametry procy określają punkt zaczepienia, maksymalny naciąg i moc strzału.
    static constexpr float AnchorX = 5.f;
    static constexpr float AnchorY = 3.2f;
    static constexpr float MaxPull = 3.f;
    static constexpr float SlingStiffness = 80.f;
    static constexpr float GrabRadius = 1.5f;
    static constexpr float ReloadDelay = 1.5f;
    static constexpr float QueueStartX = 3.6f;
    static constexpr float QueueSpacing = 0.65f;
    static constexpr float QueueScale = 0.6f;

    // Współczynniki masy wynikają z gęstości i objętości obiektu.
    static constexpr float AngularDamping = 0.1f; //tlumienie
    static constexpr float BirdDensity = 3.f;
    static constexpr float PigDensity = 1.f;

    // Czasy i progi potrzebne przy starcie poziomu oraz rozstrzyganiu wyniku.
    static constexpr float SettleTime = 1.5f;
    static constexpr float PigDeathImpulse = 3.f;
    static constexpr float BirdLifetime = 12.f;
    static constexpr float WinDelay = 0.8f;
    static constexpr float LoseDelay = 3.f;
    static constexpr float BirdRestSpeed = 0.5f;   // próg prędkości liniowej
    static constexpr float BirdRestAngular = 1.0f; // próg prędkości kątowej [rad/s]
    static constexpr float LoseHardTimeout = BirdLifetime + 2.f;

    // Wybuch przekazuje impuls obiektom w określonym zasięgu.
    static constexpr float BombRadius = 6.f;
    static constexpr float BombImpulse = 20.f;
    static constexpr float FlashTime = 0.4f;

    // Parametry umiejętności ptaków specjalnych.
    static constexpr float YellowBoost = 2.f;
    static constexpr float SplitAngle = 0.25f;

    // Iteracje solvera poprawiają stabilność stosów i połączeń.
    static constexpr int SolverPos = 12, SolverVel = 4;
};

// Kolor RGBA: kanały r, g, b określają barwę, a kanał a przezroczystość.
struct Color { float r, g, b, a = 1.f; };

// Zestawy parametrów używane przez typy ptaków i materiały konstrukcji.
struct BirdDef { float radius; Color color; };
struct MatDef { float density, breakImpulse, friction, restitution; Color color; };

// Typ obiektu wybiera jego zachowanie; materiał określa parametry fizyczne bloku.
enum Type { BLOCK, PIG, BIRD, BALL };
enum Mat { WOOD, STONE, GLASS, METAL, ALU, NumMats };
// Rodzaj ptaka wybiera wygląd i umiejętność; State opisuje aktualny ekran gry.
enum Kind { RED, YELLOW, BLUE, BLACK };
enum State { MENU, PLAYING, WON, LOST };

// Przechowuje katalog kolorów, ptaków i materiałów używany przez grę.
class Dane {
public:
    // Typy opisujące pozycję do rysowania, ciało fizyczne, wybuch i dane rozpadu.
    struct Pose;
    struct Body;
    struct Flash;
    struct Fracture;

    static constexpr Color White{ 1.f, 1.f, 1.f };
    static constexpr Color Black{ 0.f, 0.f, 0.f };
    static constexpr Color Sky{ 0.53f, 0.81f, 0.92f };
    static constexpr Color Ground{ 0.35f, 0.65f, 0.25f };
    static constexpr Color Sling{ 0.40f, 0.25f, 0.10f };
    static constexpr Color SlingBand{ 0.20f, 0.10f, 0.05f };
    static constexpr Color Pig{ 0.30f, 0.75f, 0.30f };
    static constexpr Color Explosion{ 1.f, 0.6f, 0.1f };
    static constexpr Color Hud{ 0.1f, 0.1f, 0.1f };
    static constexpr Color Frame{ 0.30f, 0.22f, 0.15f };
    static constexpr float Unbreakable = 1e30f;
    static const BirdDef Birds[4];
    static const MatDef Mats[NumMats];
};
//promien ,kolor
const BirdDef Dane::Birds[4] = {
    {0.50f, {0.85f, 0.15f, 0.15f}}, //czerwony
    {0.42f, {0.97f, 0.85f, 0.10f}}, //zolty
    {0.35f, {0.30f, 0.60f, 0.95f}}, //niebieski
    {0.60f, {0.10f, 0.10f, 0.10f}}, //czarny
};
//gestosc, wytrzymalosc, tarcie, sprezytosc, kolor
const MatDef Dane::Mats[NumMats] = {
    {0.8f, 9.0f, 0.65f, 0.15f, {0.65f, 0.45f, 0.25f}},  //drewno
    {2.5f, Dane::Unbreakable, 0.70f, 0.05f, {0.55f, 0.55f, 0.60f}}, //kamien (kula wahadla)
    {2.5f, 4.0f, 0.55f, 0.10f, {0.70f, 0.90f, 0.95f}}, //szklo
    {7.8f, Dane::Unbreakable, 0.40f, 0.20f, {0.35f, 0.38f, 0.45f}}, //metal (ogniwa wahadla)
    {2.7f, Dane::Unbreakable, 0.40f, 0.20f, {0.75f, 0.78f, 0.85f}}, //aluminium
};


inline float angleZ(const PxQuat& q) { return 2.f * std::atan2(q.z, q.w); }
struct Dane::Pose { float x, y, angle; };

// Łączy aktora PhysX z danymi potrzebnymi przez zasady gry i renderer.
struct Dane::Body {
    // Wskaźnik PhysX wykonuje symulację; pozostałe pola przechowują stan gry.
    PxRigidDynamic* actor = nullptr;
    Type type = BLOCK;
    Mat mat = WOOD;
    Kind kind = RED;
    int id = 0;
    PxVec2 half{ 0, 0 }; // Połowa szerokości i wysokości prostokątnego bloku.
    float radius = 0;
    float impulse = 0; // Suma impulsów kontaktowych zebranych w bieżącym kroku.
    float age = 0;
    bool abilityUsed = false;
    bool dead = false;       // Obiekt czeka na usunięcie po zakończeniu kroku fizyki.
    bool fragment = false;   // Fragment po rozpadzie nie rozpada się ponownie.
    bool noShatter = false;  // Wyłącza rozpad dla ciał o specjalnym środku masy.
    float armLen = 0;        // Długość widocznych cięgien, jeśli ciało jest huśtawką.
    float mass = 0.f;        // Aktualna masa odczytana z PhysX.

    PxVec3 prevPos{ 0, 0, 0 };
    float prevAngle = 0.f;

    void savePose() {
        // Zapamiętujemy poprzednią pozę, by renderować płynnie między krokami fizyki.
        PxTransform t = actor->getGlobalPose();
        prevPos = t.p;
        prevAngle = angleZ(t.q);
    }
    Pose pose(float alpha) const {
        // Interpolujemy pozycję i najkrótszą drogę obrotu do bieżącej pozycji PhysX.
        PxTransform t = actor->getGlobalPose();
        float da = std::remainder(angleZ(t.q) - prevAngle, 2.f * Konfiguracja::Pi);
        return { prevPos.x + (t.p.x - prevPos.x) * alpha, prevPos.y + (t.p.y - prevPos.y) * alpha,
                prevAngle + da * alpha };
    }
};

using Pose = Dane::Pose;
using Body = Dane::Body;

struct Dane::Flash { float x, y, t; };
struct Dane::Fracture {
    PxVec3 pos, v, omega;
    PxVec2 half;
    float angle;
    Mat mat;
    float density;
    float invInertiaZ;
};
using Flash = Dane::Flash;
using Fracture = Dane::Fracture;


class Fizyka {
public:
    // Zarządza sceną PhysX, ciałami, materiałami, kolizjami i zawiasami.
    // BirdGroup pomija kolizje między ptakami, GhostGroup wyłącza kolizje cięgien.
    static constexpr PxU32 BirdGroup = 1;
    static constexpr PxU32 GhostGroup = 2;
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

    std::vector<Body*> bodies; // Własność ciał dynamicznych utworzonych dla poziomu.
    int nextId = 1;            // Unikalny numer pozwala grze odnaleźć wystrzelonego ptaka.

    struct Deco { float x, y, hx, hy; Color c; PxRigidStatic* actor; };
    std::vector<Deco> decos;
    std::vector<PxVec2> pivots;

    // PhysX pyta tę funkcję, czy para kształtów ma brać udział w kolizji.
    static PxFilterFlags filterShader(PxFilterObjectAttributes, PxFilterData fd0, PxFilterObjectAttributes,
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

    // userData łączy aktora PhysX z odpowiadającym mu obiektem logiki gry.
    static Body* bodyOf(PxActor* a) {
        return (a && a->userData) ? static_cast<Body*>(a->userData) : nullptr;
    }

    static constexpr float MinImpactSpeed = 0.5f;

    // Prędkość względna w punkcie kontaktu: v_rel = (v0 + w0 x r0) - (v1 + w1 x r1).
    static PxVec3 relativeVelocityAt(PxActor* a, PxActor* b, const PxVec3& p) {
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
        // Zbiera impulsy z punktów kontaktu; gra wykorzystuje je do obrażeń i rozpadu.
        void onContact(const PxContactPairHeader& header, const PxContactPair* pairs, PxU32 count) override {
            if (!header.actors[0] || !header.actors[1])
                return;
            Body* a = Fizyka::bodyOf(header.actors[0]);
            Body* b = Fizyka::bodyOf(header.actors[1]);
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
                    PxVec3 vrel = Fizyka::relativeVelocityAt(header.actors[0], header.actors[1], points[j].position);
                    if (vrel.magnitude() < Fizyka::MinImpactSpeed)
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

    // Tworzy fundament, silnik, scenę, materiały i nieruchome granice planszy.
    bool init() {
        foundation = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errorCallback);
        if (!foundation) {
            std::fprintf(stderr, "PhysX: PxCreateFoundation failed\n");
            return false;
        }
        physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, PxTolerancesScale(), true, nullptr);
        if (!physics) {
            std::fprintf(stderr, "PhysX: PxCreatePhysics failed\n");
            return false;
        }
        PxInitExtensions(*physics, nullptr);

        PxSceneDesc desc(physics->getTolerancesScale());
        desc.gravity = PxVec3(0.f, -Konfiguracja::Gravity, 0.f);
        dispatcher = PxDefaultCpuDispatcherCreate(2);
        if (!dispatcher) {
            std::fprintf(stderr, "PhysX: PxDefaultCpuDispatcherCreate failed\n");
            return false;
        }
        desc.cpuDispatcher = dispatcher;
        desc.filterShader = Fizyka::filterShader;
        desc.simulationEventCallback = &contactCallback;
        desc.flags |= PxSceneFlag::eENABLE_CCD;
#if PX_PHYSICS_VERSION_MAJOR == 4
        desc.flags |= PxSceneFlag::eENABLE_STABILIZATION;
#endif
        scene = physics->createScene(desc);
        if (!scene) {
            std::fprintf(stderr, "PhysX: createScene failed\n");
            return false;
        }

        creatureMat = physics->createMaterial(0.6f, 0.5f, 0.15f);
        for (int i = 0; i < NumMats; ++i)
            matPx[i] = physics->createMaterial(Dane::Mats[i].friction, Dane::Mats[i].friction * 0.85f, Dane::Mats[i].restitution);

        ground = PxCreateStatic(*physics, PxTransform(PxVec3(16.f, -0.5f, 0.f)),
            PxBoxGeometry(20.f, 0.5f, 2.f), *creatureMat);
        scene->addActor(*ground);
        leftWall = PxCreateStatic(*physics, PxTransform(PxVec3(0.1f, 8.f, 0.f)),
            PxBoxGeometry(0.2f, 8.5f, 2.f), *creatureMat);
        rightWall = PxCreateStatic(*physics, PxTransform(PxVec3(Konfiguracja::ViewW - 0.1f, 8.f, 0.f)),
            PxBoxGeometry(0.2f, 8.5f, 2.f), *creatureMat);
        scene->addActor(*leftWall);
        scene->addActor(*rightWall);
        return true;
    }

    // Zwolnienie aktora PhysX i jego danych gry musi nastąpić razem.
    void releaseBody(Body* b) {
        b->actor->release();
        delete b;
    }

    // Czyści tylko obiekty poziomu, aby można było zbudować go ponownie.
    void clear() {
        for (Body* b : bodies)
            releaseBody(b);
        bodies.clear();
        for (Deco& d : decos)
            if (d.actor) d.actor->release();
        decos.clear();
        pivots.clear();
    }

    // Zwalnia wszystkie zasoby PhysX po zamknięciu pętli gry.
    void shutdown() {
        clear();
        if (leftWall) leftWall->release();
        if (rightWall) rightWall->release();
        if (ground) ground->release();
        for (int i = 0; i < NumMats; ++i) if (matPx[i]) matPx[i]->release();
        if (creatureMat) creatureMat->release();
        if (scene) scene->release();
        if (dispatcher) dispatcher->release();
        PxCloseExtensions();
        if (physics) physics->release();
        if (foundation) foundation->release();
    }

    // Wykonuje jeden stały krok PhysX i pobiera zakończone wyniki symulacji.
    void step(float dt) {
        for (Body* b : bodies) b->savePose();
        scene->simulate(dt);
        scene->fetchResults(true);
    }

    // Ustawia wspólne flagi ciała, dodaje je do sceny i zapisuje w rejestrze gry.
    Body* finish(Body* b, PxRigidDynamic* actor) {
        b->actor = actor;
        b->id = nextId++;
        actor->setRigidDynamicLockFlags(PxRigidDynamicLockFlag::eLOCK_LINEAR_Z |
            PxRigidDynamicLockFlag::eLOCK_ANGULAR_X |
            PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y);
        actor->setAngularDamping(Konfiguracja::AngularDamping);
        actor->setSolverIterationCounts(Konfiguracja::SolverPos, Konfiguracja::SolverVel);
        actor->userData = b;
        b->mass = actor->getMass();
        scene->addActor(*actor);
        b->savePose();
        bodies.push_back(b);
        return b;
    }

    // Tworzy prostokątny blok; hx i hy są jego połowami, a mat wybiera materiał.
    Body* addBox(float x, float y, float hx, float hy, Mat mat, float angle = 0.f, bool fragment = false) {
        auto* b = new Body;
        b->type = BLOCK;
        b->mat = mat;
        b->half = PxVec2(hx, hy);
        b->fragment = fragment;
        PxTransform pose(PxVec3(x, y, 0.f), PxQuat(angle, PxVec3(0.f, 0.f, 1.f)));
        PxRigidDynamic* actor = PxCreateDynamic(*physics, pose, PxBoxGeometry(hx, hy, 0.5f),
            *matPx[mat], Dane::Mats[mat].density);
        if (fragment)
            actor->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
        return finish(b, actor);
    }

    // Jedno ciało huśtawki zawiera siedzisko i dwa niekolizyjne cięgna.
    Body* addSwing(float x, float y, float hx, float hy, float armLen, Mat mat) {
        auto* b = new Body;
        b->type = BLOCK;
        b->mat = mat;
        b->half = PxVec2(hx, hy);
        b->armLen = armLen;
        PxRigidDynamic* actor = PxCreateDynamic(*physics, PxTransform(PxVec3(x, y, 0.f)),
            PxBoxGeometry(hx, hy, 0.5f), *matPx[mat], Dane::Mats[mat].density);

        const float L = std::sqrt(hx * hx + armLen * armLen);
        const float phi = std::atan2(hx, armLen);
        for (int side = -1; side <= 1; side += 2) {
            PxShape* arm = PxRigidActorExt::createExclusiveShape(*actor, PxBoxGeometry(0.03f, L * 0.5f, 0.5f),
                *matPx[mat]);
            arm->setLocalPose(PxTransform(PxVec3(side * hx * 0.5f, armLen * 0.5f, 0.f),
                PxQuat(side * phi, PxVec3(0.f, 0.f, 1.f))));
            arm->setSimulationFilterData(PxFilterData(GhostGroup, 0, 0, 0));
        }
        PxRigidBodyExt::updateMassAndInertia(*actor, Dane::Mats[mat].density);
        actor->setSleepThreshold(0.f);
        return finish(b, actor);
    }

    // Tworzy kulę dla ptaka, świnki albo kuli wahadła i ustawia filtrowanie ptaków.
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

    // Przesuwa środek masy w pionie i przelicza bezwładność bryły w PhysX.
    void setCom(Body* b, float dy) {
        PxVec3 com(0.f, dy, 0.f);
        PxRigidBodyExt::updateMassAndInertia(*b->actor, Dane::Mats[b->mat].density, &com);
        b->noShatter = true;
        b->mass = b->actor->getMass();
    }

    // Wyszukuje ciało po identyfikatorze zapamiętanym przy jego utworzeniu.
    Body* find(int id) {
        for (Body* b : bodies) if (b->id == id) return b;
        return nullptr;
    }

    // Bezpieczne usuwanie
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

    // Dodaje element rysowany; opcjonalnie tworzy dla niego nieruchomą kolizję.
    void addDeco(float x, float y, float hx, float hy, Color c, bool solid) {
        PxRigidStatic* a = nullptr;
        if (solid) {
            a = PxCreateStatic(*physics, PxTransform(PxVec3(x, y, 0.f)), PxBoxGeometry(hx, hy, 2.f), *creatureMat);
            scene->addActor(*a);
        }
        decos.push_back({ x, y, hx, hy, c, a });
    }

    // Kopiuje pozę i prędkości bloku, by można go było bezpiecznie rozbić później.
    Fracture makeFracture(const Body& b) {
        PxTransform t = b.actor->getGlobalPose();
        PxVec3 inertia = b.actor->getMassSpaceInertiaTensor();
        return { t.p, b.actor->getLinearVelocity(), b.actor->getAngularVelocity(),
                b.half, angleZ(t.q), b.mat,
                Dane::Mats[b.mat].density,
                (inertia.z > 1e-9f) ? 1.f / inertia.z : 0.f };
    }

    // Rozpad: pęd całkowity zachowany (v_i = v + omega x r_i, suma m_i r_i = 0).
    // Moment pędu jest kompensowany przez dodatkową prędkość kątową fragmentów.
    // Dla ciał z przesuniętym COM (setCom) rozpad jest wyłączony - patrz noShatter.
    // Dzieli blok na siatkę fragmentów, obraca ich środki i zachowuje prędkości.
    void shatter(const Fracture& f) {
        int nx, ny;
        if (f.mat == GLASS) { nx = 2; ny = 2; }
        else if (f.half.y > f.half.x) { nx = 1; ny = 3; }
        else { nx = 3; ny = 1; }
        const float hx = f.half.x / nx, hy = f.half.y / ny;
        const float c = std::cos(f.angle), s = std::sin(f.angle);

        const float L0 = (1.f / std::max(f.invInertiaZ, 1e-12f)) * f.omega.z;

        float mSum = 0.f;
        float mrsq = 0.f;
        for (int ix = 0; ix < nx; ++ix)
            for (int iy = 0; iy < ny; ++iy) {
                float lx = -f.half.x + hx * (2 * ix + 1);
                float ly = -f.half.y + hy * (2 * iy + 1);
                float mi = f.density * (2.f * hx) * (2.f * hy);
                mSum += mi;
                mrsq += mi * (lx * lx + ly * ly);
            }
        const float missingL = L0 - mrsq * f.omega.z;
        const float Ifrag = mSum * (hx * hx + hy * hy) / 3.f;
        const float dOmega = (Ifrag > 1e-9f) ? missingL / Ifrag : 0.f;

        for (int ix = 0; ix < nx; ++ix)
            for (int iy = 0; iy < ny; ++iy) {
                float lx = -f.half.x + hx * (2 * ix + 1);
                float ly = -f.half.y + hy * (2 * iy + 1);
                PxVec3 center(f.pos.x + lx * c - ly * s, f.pos.y + lx * s + ly * c, 0.f);
                Body* n = addBox(center.x, center.y, hx, hy, f.mat, f.angle, true);
                n->actor->setLinearVelocity(f.v + f.omega.cross(center - f.pos));
                n->actor->setAngularVelocity(PxVec3(0.f, 0.f, f.omega.z + dOmega));
            }
    }

    // Ustawia oś zawiasu wzdłuż osi Z świata, czyli osi obrotu gry 2D.
    PxQuat axisZ() { return PxQuat(-0.5f * Konfiguracja::Pi, PxVec3(0.f, 1.f, 0.f)); }
    PxTransform frameOn(PxRigidActor* a, const PxTransform& world) {
        return a ? a->getGlobalPose().transformInv(world) : world;
    }

    // Łączy dwa ciała zawiasem w punkcie p; nullptr oznacza zakotwiczenie do świata.
    PxRevoluteJoint* hinge(Body* a, Body* b, PxVec3 p) {
        PxTransform w(p, axisZ());
        PxRigidActor* A = a ? static_cast<PxRigidActor*>(a->actor) : nullptr;
        return PxRevoluteJointCreate(*physics, A, frameOn(A, w), b->actor, frameOn(b->actor, w));
    }
};

constexpr PxU32 Fizyka::BirdGroup;
constexpr PxU32 Fizyka::GhostGroup;
constexpr float Fizyka::MinImpactSpeed;

Fizyka fizyka;


// Buduje konstrukcje planszy. Metody są statyczne, bo klasa nie przechowuje stanu.
class Poziomy {
public:
    // Wieża ma szklane podpory na dole i świnki na wybranych piętrach.
    static void buildTower(float cx, int floors) {
        constexpr float hw = 1.5f;
        for (int k = 0; k < floors; ++k) {
            // Piętro tworzą dwa słupy i półka; dolne słupy są z kruchego szkła.
            float y0 = k * 2.3f;
            Mat pm = (k == 0) ? GLASS : WOOD;
            fizyka.addBox(cx - hw, y0 + 0.9f, 0.3f, 0.9f, pm);
            fizyka.addBox(cx + hw, y0 + 0.9f, 0.3f, 0.9f, pm);
            fizyka.addBox(cx, y0 + 2.05f, 1.9f, 0.25f, WOOD);
            if (k == 1 || k == 3)
                fizyka.addBall(cx, y0 + 0.5f, 0.5f, Konfiguracja::PigDensity, PIG);
        }
        fizyka.addBall(cx, floors * 2.3f + 0.5f, 0.5f, Konfiguracja::PigDensity, PIG);
    }

    // Domek składa się z dwóch podpór, dachu i dwóch świnek.
    static void buildHut(float cx, Mat pillarMat) {
        fizyka.addBox(cx - 0.9f, 0.6f, 0.2f, 0.6f, pillarMat);
        fizyka.addBox(cx + 0.9f, 0.6f, 0.2f, 0.6f, pillarMat);
        fizyka.addBox(cx, 1.35f, 1.3f, 0.15f, WOOD);
        fizyka.addBall(cx, 0.5f, 0.5f, Konfiguracja::PigDensity, PIG);
        fizyka.addBall(cx, 1.5f + 0.5f, 0.5f, Konfiguracja::PigDensity, PIG);
    }

    // Ustawia rząd drewnianych kostek, które przewracają się łańcuchowo.
    static void buildDominoes(float x0, int n, float spacing) {
        for (int i = 0; i < n; ++i)
            fizyka.addBox(x0 + i * spacing, 0.7f, 0.12f, 0.7f, WOOD);
    }

    // Łączy metalowe ogniwa zawiasami i kończy je ciężką kulą.
    static void buildPendulum(float ax, float ay, int links, float ballR) {
        const float L = 0.7f;
        Body* prev = nullptr;
        for (int i = 0; i < links; ++i) {
            // Każde kolejne ogniwo łączy się zawiasem z poprzednim.
            Body* link = fizyka.addBox(ax, ay - 0.5f * L - i * L, 0.09f, 0.5f * L, METAL);
            fizyka.hinge(prev, link, PxVec3(ax, ay - i * L, 0.f));
            prev = link;
        }
        Body* ball = fizyka.addBall(ax, ay - links * L - ballR, ballR, Dane::Mats[STONE].density, BALL, STONE);
        fizyka.hinge(prev, ball, PxVec3(ax, ay - links * L, 0.f));
        fizyka.pivots.push_back(PxVec2(ax, ay));
        fizyka.addDeco(ax, (ay + Konfiguracja::ViewH) * 0.5f, 0.08f, (Konfiguracja::ViewH - ay) * 0.5f, Dane::Frame, false);
    }

    // Buduje huśtawkę z ruchomym siedziskiem, zawieszeniem i świnką.
    static void buildSwing(float cx, float topY, float len) {
        fizyka.addDeco(cx, topY + 0.15f, 2.0f, 0.15f, Dane::Frame, false);
        fizyka.addDeco(cx, (topY + 0.3f + Konfiguracja::ViewH) * 0.5f, 0.15f,
            (Konfiguracja::ViewH - topY - 0.3f) * 0.5f, Dane::Frame, false);

        Body* seat = fizyka.addSwing(cx, topY - len, 1.2f, 0.15f, len, ALU);
        fizyka.hinge(nullptr, seat, PxVec3(cx, topY, 0.f));
        fizyka.pivots.push_back(PxVec2(cx, topY));
        fizyka.addBall(cx, topY - len + 0.15f + 0.5f, 0.5f, Konfiguracja::PigDensity, PIG);
    }

    // Umieszcza środek masy wysoko, aby kolumna była niestabilna.
    static void buildTopHeavyColumn(float x) {
        Body* col = fizyka.addBox(x, 1.5f, 0.4f, 1.5f, WOOD);
        fizyka.setCom(col, 0.5f);
        fizyka.addBall(x, 3.5f, 0.5f, Konfiguracja::PigDensity, PIG);
    }

    // Składa wszystkie elementy poziomu.
    static void build() {
        // Elementy układamy od lewej do prawej.
        buildHut(11.7f, GLASS);              // szklany domek
        buildDominoes(17.2f, 5, 0.8f);       // rząd pięciu kostek domina
        buildPendulum(19.4f, 7.6f, 4, 0.6f); // wahadło z kulą
        buildTower(23.0f, 4);                // czteropiętrowa wieża
        buildSwing(27.2f, 10.0f, 4.0f);      // huśtawka ze świnią
        buildHut(27.2f, WOOD);               // drewniany domek
        buildTopHeavyColumn(30.5f);          // niestabilny słup z balastem
    }

    // Zwraca kolejność ptaków dla poziomu.
    static std::vector<Kind> birdQueue() {
        return { RED, BLUE, YELLOW, BLACK, RED, YELLOW, BLACK, BLUE, RED, BLACK };
    }
};


class Grafika {
public:
    // Rysuje kształty 2D i przechowuje wymiary bieżącego obszaru widoku.
    int fbW = Konfiguracja::WindowW, fbH = Konfiguracja::WindowH;
    int vpX = 0, vpY = 0, vpW = Konfiguracja::WindowW, vpH = Konfiguracja::WindowH;
    float fracX = 0.f, fracY = 0.f, fracW = 1.f, fracH = 1.f;

    // Dopasowuje stałe proporcje świata do okna i zapisuje obszar gry w pikselach.
    void layout(GLFWwindow* win) {
        glfwGetFramebufferSize(win, &fbW, &fbH);
        if (fbW <= 0 || fbH <= 0) return;
        const float aspect = Konfiguracja::ViewW / Konfiguracja::ViewH;
        vpW = fbW; vpH = (int)(fbW / aspect);
        if (vpH > fbH) { vpH = fbH; vpW = (int)(fbH * aspect); }
        vpX = (fbW - vpW) / 2; vpY = (fbH - vpH) / 2;
        fracX = (float)vpX / fbW; fracY = (float)vpY / fbH;
        fracW = (float)vpW / fbW; fracH = (float)vpH / fbH;
    }

    // Włącza przezroczyste mieszanie kolorów używane m.in. przez efekty i tekst.
    void init() {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    // Czyści ekran, ustawia viewport i mapuje współrzędne świata na widok.
    void beginFrame(GLFWwindow* win) {
        (void)win;
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glViewport(vpX, vpY, vpW, vpH);
        glEnable(GL_SCISSOR_TEST);
        glScissor(vpX, vpY, vpW, vpH);
        glClearColor(Dane::Sky.r, Dane::Sky.g, Dane::Sky.b, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, Konfiguracja::ViewW, 0.0, Konfiguracja::ViewH, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }

    // Rysuje prostokąt o środku (x,y), połówkach rozmiaru hx/hy i obrocie angle.
    void quad(float x, float y, float hx, float hy, float angle, Color c) {
        glColor4f(c.r, c.g, c.b, c.a);
        glPushMatrix();
        glTranslatef(x, y, 0.f);
        glRotatef(angle * 180.f / Konfiguracja::Pi, 0.f, 0.f, 1.f);
        glScalef(hx, hy, 1.f);
        glBegin(GL_QUADS);
        glVertex2f(-1.f, -1.f); glVertex2f(1.f, -1.f);
        glVertex2f(1.f, 1.f);   glVertex2f(-1.f, 1.f);
        glEnd();
        glPopMatrix();
    }

    // Aproksymuje koło wielokątem, aby działało także w starym OpenGL 2.1.
    void circle(float x, float y, float radius, Color c) {
        constexpr int segments = 40;
        glColor4f(c.r, c.g, c.b, c.a);
        glPushMatrix();
        glTranslatef(x, y, 0.f);
        glScalef(radius, radius, 1.f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(0.f, 0.f);
        for (int i = 0; i <= segments; ++i) {
            float t = 2.f * Konfiguracja::Pi * i / segments;
            glVertex2f(std::cos(t), std::sin(t));
        }
        glEnd();
        glPopMatrix();
    }

    // Rysuje odcinek jako wąski, obrócony prostokąt między punktami a i b.
    void line(PxVec2 a, PxVec2 b, float thickness, Color c) {
        PxVec2 d = b - a;
        quad((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, d.magnitude() * 0.5f, thickness,
            std::atan2(d.y, d.x), c);
    }
};

Grafika grafika;


class Tekst {
public:
    // Ładuje czcionkę do atlasu tekstury i rysuje napisy na ekranie.
    const char* FontPath = "PressStart2P-Regular.ttf";
    const int AtlasSize = 512;
    const float FontPx = 8.f;
    struct Range { int first, count; };
    const Range Ranges[3] = { {0x20, 95}, {0xA0, 96}, {0x100, 128} };
    const int NumRanges = 3;
    stbtt_packedchar glyphs[3][128];
    GLuint texture = 0;
    bool ready = false;

    // Zaokrągla współrzędne pikseli, aby pikselowa czcionka była ostra.
    float snap(float v) { return std::floor(v + 0.5f); }

    // Wczytuje cały plik czcionki do pamięci; false oznacza brak lub pusty plik.
    bool readFile(const char* path, std::vector<unsigned char>& out) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return false;
        std::streamsize size = file.tellg();
        if (size <= 0) return false;
        out.resize((size_t)size);
        file.seekg(0, std::ios::beg);
        return (bool)file.read(reinterpret_cast<char*>(out.data()), size);
    }

    // Pakuje znaki do atlasu alfa, tworzy teksturę OpenGL i oznacza tekst jako gotowy.
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

    // Odczytuje z UTF-8 jeden znak i zwraca jego kod Unicode.
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

    // Wyszukuje znak w jednym z zapakowanych zakresów; brakujący znak zastępuje '?'.
    const stbtt_packedchar* glyph(int cp) {
        for (int i = 0; i < NumRanges; ++i)
            if (cp >= Ranges[i].first && cp < Ranges[i].first + Ranges[i].count)
                return &glyphs[i][cp - Ranges[i].first];
        return cp == '?' ? nullptr : glyph('?');
    }

    // Przelicza rozmiar tekstu z jednostek świata na całkowitą liczbę pikseli.
    int scale(float size) {
        float pixelsPerUnit = (float)grafika.vpW / Konfiguracja::ViewW;
        return std::max(1, (int)std::lround(5.5f * size * pixelsPerUnit / FontPx));
    }

    // Oblicza szerokość napisu, aby można go było dokładnie wyśrodkować.
    float width(const char* s, float size) {
        int sc = scale(size);
        float px = 0.f;
        for (const char* p = s; *p;)
            if (const stbtt_packedchar* g = glyph(decode(p)))
                px += snap(g->xadvance * sc);
        return px / grafika.vpW * Konfiguracja::ViewW;
    }

    // Rysuje znaki jako teksturowane czworokąty w bieżącym viewportcie.
    void draw(const char* s, float x, float y, float size, Color c) {
        if (!ready) return;
        int sc = scale(size);
        float px = std::floor(x / Konfiguracja::ViewW * grafika.vpW);
        float py = std::floor((1.f - y / Konfiguracja::ViewH) * grafika.vpH);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0.0, grafika.vpW, grafika.vpH, 0.0, -1.0, 1.0);
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

    // Ustawia początek napisu tak, by jego środek wypadł w punkcie (cx, cy).
    void drawCentered(const char* s, float cx, float cy, float size, Color c) {
        float baselineShift = 3.5f * scale(size) * (Konfiguracja::ViewH / (float)grafika.vpH);
        draw(s, cx - width(s, size) * 0.5f, cy - baselineShift, size, c);
    }
};

Tekst tekst;

//UI
struct Button {
    // Środek, rozmiar i etykieta pojedynczego przycisku w jednostkach świata.
    float x, y, w, h;
    const char* label;
    // Punkt kursora jest podany w tych samych współrzędnych co pozycja przycisku.
    bool contains(PxVec2 m) const {
        return std::fabs(m.x - x) < w * 0.5f && std::fabs(m.y - y) < h * 0.5f;
    }
    // Rysuje cień, obramowanie i etykietę; hover lekko rozjaśnia przycisk.
    void draw(bool hover) const {
        float k = hover ? 1.15f : 1.f;
        grafika.quad(x + 0.12f, y - 0.12f, w * 0.5f, h * 0.5f, 0.f, { 0.f, 0.f, 0.f, 0.35f });
        grafika.quad(x, y, w * 0.5f, h * 0.5f, 0.f, { 0.15f, 0.10f, 0.05f });
        grafika.quad(x, y, w * 0.5f - 0.1f, h * 0.5f - 0.1f, 0.f,
            { std::min(1.f, 0.90f * k), std::min(1.f, 0.55f * k), std::min(1.f, 0.10f * k) });
        tekst.drawCentered(label, x, y, 0.16f, Dane::White);
    }
};

class Interfejs {
public:
    // Przyciski menu i ekranów końcowych są zebrane w jednym miejscu.
    const Button Play{ 16.f, 8.f, 6.f, 1.8f, "PLAY" };
    const Button Quit{ 16.f, 5.5f, 6.f, 1.8f, "QUIT" };
    const Button Retry{ 12.8f, 6.5f, 6.f, 1.8f, "RETRY" };
    const Button MenuRight{ 19.2f, 6.5f, 6.f, 1.8f, "MENU" };
};

Interfejs interfejs;


class Wejscie {
public:
    // Odczytuje mysz i klawiaturę, a także wykrywa nowe naciśnięcia.
    PxVec2 mouse;
    bool lmb = false;
    bool clicked = false;
    bool escPressed = false, rPressed = false;

    // Zamienia położenie kursora z pikseli okna na współrzędne świata gry.
    void poll(GLFWwindow* win) {
        double mx, my;
        int ww, wh;
        glfwGetCursorPos(win, &mx, &my);
        glfwGetWindowSize(win, &ww, &wh);
        float nx = (float)(mx / ww), ny = 1.f - (float)(my / wh);
        mouse = PxVec2((nx - grafika.fracX) / grafika.fracW * Konfiguracja::ViewW,
            (ny - grafika.fracY) / grafika.fracH * Konfiguracja::ViewH);

        bool l = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool e = glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        bool r = glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS;
        // Porównanie z poprzednią klatką wykrywa samo naciśnięcie, nie przytrzymanie.
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


class Gra {
public:
    // Łączy wejście, symulację, warunki końca gry i wyświetlanie sceny.
    bool quit = false;

    // Aktualizacja sterowania i symulacji jest osobna od rysowania klatki.
    // Najpierw zapamiętuje mysz, potem obsługuje zdarzenia i aktualizuje fizykę.
    void update(const Wejscie& in, double frameDt) {
        handleInput(in);
        simulate(frameDt);
    }

    void draw() const {
        // Scenę rysujemy warstwami: tło, obiekty fizyczne, efekty i interfejs.
        grafika.quad(16.f, -0.5f, 20.f, 0.5f, 0.f, Dane::Ground);
        if (state == MENU) { drawMenu(); return; }

        const float alpha = (float)(accumulator / Konfiguracja::Dt);
        for (const Fizyka::Deco& d : fizyka.decos)
            grafika.quad(d.x, d.y, d.hx, d.hy, 0.f, d.c);
        drawSling();
        for (const Body* b : fizyka.bodies)
            drawBody(*b, alpha);
        for (const PxVec2& p : fizyka.pivots) {
            grafika.circle(p.x, p.y, 0.18f, Dane::Black);
            grafika.circle(p.x, p.y, 0.07f, Dane::White);
        }
        drawExplosions();
        drawActiveBird();
        drawQueue();
        drawHud();
        if (state != PLAYING) drawOverlay();
    }

private:
    // Stan rozgrywki należy do tej instancji i jest resetowany przy starcie poziomu.
    State state = MENU;                 // Bieżący ekran: menu, gra, wygrana lub przegrana.
    PxVec2 mouse;                       // Pozycja kursora w jednostkach świata.
    std::vector<Kind> queue;            // Rodzaje ptaków dostępnych w tej rozgrywce.
    size_t birdIdx = 0;                 // Indeks ptaka, który będzie wystrzelony jako następny.
    int lastBirdId = 0;                 // Id ostatniego ptaka, którego zdolność może być aktywowana.
    std::vector<Flash> flashes;         // Wybuchy, które są nadal widoczne na ekranie.
    bool dragging = false;              // Czy gracz przeciąga aktualnego ptaka na procy.
    PxVec2 dragPos;                     // Aktualna pozycja ptaka podczas celowania.
    double accumulator = 0.0;           // Czas oczekujący na wykonanie kolejnego stałego kroku fizyki.
    float levelTime = 0.f;              // Czas od rozpoczęcia aktualnego poziomu.
    float sinceLaunch = 10.f;           // Czas od ostatniego strzału, używany przy przeładowaniu procy.
    float winTimer = 0.f;               // Opóźnienie przed pokazaniem zwycięstwa.
    float loseTimer = 0.f;              // Opóźnienie przed pokazaniem przegranej.
    float totalPlayTime = 0.f;           // Łączny czas aktywnej symulacji poziomu.
    bool wasSettling = false;

    // Usuwa ciała poziomu i czyści efekty, przygotowując świat do ponownego użycia.
    void clearWorld() {
        fizyka.clear();
        flashes.clear();
    }

    // Resetuje liczniki i stan sterowania, buduje poziom i rozpoczyna grę.
    void startLevel() {
        clearWorld();
        Poziomy::build();
        queue = Poziomy::birdQueue();
        birdIdx = 0;
        lastBirdId = 0;
        dragging = false;
        dragPos = PxVec2(Konfiguracja::AnchorX, Konfiguracja::AnchorY);
        levelTime = 0.f;
        sinceLaunch = 10.f;
        winTimer = 0.f;
        loseTimer = 0.f;
        totalPlayTime = 0.f;
        accumulator = 0.0;
        wasSettling = false;
        state = PLAYING;
    }

    // Zatrzymuje bieżącą rozgrywkę i wraca do ekranu startowego.
    void toMenu() {
        clearWorld();
        dragging = false;
        state = MENU;
    }

    // Rozdziela sterowanie menu, rozgrywką oraz ekranami końcowymi.
    void handleInput(const Wejscie& in) {
        mouse = in.mouse;
        if (state == MENU) {
            if (in.escPressed || (in.clicked && interfejs.Quit.contains(mouse)))
                quit = true;
            else if (in.clicked && interfejs.Play.contains(mouse))
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
            if (in.clicked && interfejs.Retry.contains(mouse)) startLevel();
            else if (in.clicked && interfejs.MenuRight.contains(mouse)) toMenu();
            break;
        default: break;
        }
    }

    // Celowanie jest dostępne, gdy jest ptak, proca się przeładowała i konstrukcja osiadła.
    bool canAim() const {
        return birdIdx < queue.size() && sinceLaunch > Konfiguracja::ReloadDelay &&
            levelTime > Konfiguracja::SettleTime;
    }

    // Rozpoczyna przeciąganie przy procy, ogranicza naciąg i strzela po puszczeniu myszy.
    void handleAiming(const Wejscie& in) {
        const PxVec2 anchor(Konfiguracja::AnchorX, Konfiguracja::AnchorY);
        if (in.clicked && !dragging) {
            if ((mouse - anchor).magnitude() < Konfiguracja::GrabRadius) {
                if (canAim()) dragging = true;
            }
            else {
                useAbility();
            }
        }
        if (!dragging) return;

        PxVec2 pull = mouse - anchor;
        float len = pull.magnitude();
        if (len > Konfiguracja::MaxPull) pull *= Konfiguracja::MaxPull / len;
        dragPos = anchor + pull;
        dragPos.y = std::max(dragPos.y, Dane::Birds[queue[birdIdx]].radius);

        if (!in.lmb) launch();
    }

    // v = x * sqrt(k/m); m bierzemy z definicji (jedna kula o zadanej gęstości),
    // żeby nie tworzyć ptaka tylko po to, by odczytać masę.
    PxVec2 launchVelocity(Kind k) const {
        float r = Dane::Birds[k].radius;
        float mass = Konfiguracja::BirdDensity * (4.f / 3.f) * Konfiguracja::Pi * r * r * r;
        const PxVec2 anchor(Konfiguracja::AnchorX, Konfiguracja::AnchorY);
        return (anchor - dragPos) * std::sqrt(Konfiguracja::SlingStiffness / mass);
    }

    // Tworzy ciało ptaka, nadaje mu prędkość z modelu sprężyny i zapisuje jego identyfikator.
    void launch() {
        Kind k = queue[birdIdx++];
        Body* bird = fizyka.addBall(dragPos.x, dragPos.y, Dane::Birds[k].radius, Konfiguracja::BirdDensity, BIRD);
        bird->kind = k;
        PxVec2 v = launchVelocity(k);
        bird->actor->setLinearVelocity(PxVec3(v.x, v.y, 0.f));
        lastBirdId = bird->id;
        sinceLaunch = 0.f;
        dragging = false;
    }

    // Uruchamia zdolność ostatniego ptaka najwyżej raz, zależnie od jego rodzaju.
    void useAbility() {
        Body* b = fizyka.find(lastBirdId);
        if (!b || b->abilityUsed) return;
        b->abilityUsed = true;
        switch (b->kind) {
        case YELLOW:
            b->actor->setLinearVelocity(b->actor->getLinearVelocity() * Konfiguracja::YellowBoost);
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
        // Zachowujemy pozycję i prędkość oryginału przed zmianą jego rozmiaru.
        PxVec3 p = b.actor->getGlobalPose().p;
        PxVec3 v = b.actor->getLinearVelocity();
        // Promień dzielimy przez pierwiastek sześcienny z 3, więc masa każdej kuli
        // przy tej samej gęstości jest równa jednej trzeciej masy początkowej.
        float r = b.radius / std::cbrt(3.f);

        PxShape* shape = nullptr;
        b.actor->getShapes(&shape, 1);
        if (shape)
            shape->setGeometry(PxSphereGeometry(r));
        b.radius = r;
        PxRigidBodyExt::updateMassAndInertia(*b.actor, Konfiguracja::BirdDensity);
        b.mass = b.actor->getMass();

        float vLen = v.magnitude();
        PxVec3 perp = vLen > 0.01f ? PxVec3(-v.y, v.x, 0.f) / vLen : PxVec3(0.f, 1.f, 0.f);
        // Dwie nowe kule dostają przeciwne składowe boczne; ich pędy się znoszą.
        float dv = vLen * std::tan(Konfiguracja::SplitAngle);

        for (int side = -1; side <= 1; side += 2) {
            Body* n = fizyka.addBall(p.x, p.y, r, Konfiguracja::BirdDensity, BIRD);
            n->kind = b.kind;
            n->abilityUsed = true;
            n->actor->setLinearVelocity(v + perp * (dv * side));
        }
    }

    // Wybuch: impuls J malejący liniowo z odległością. Cięższe ciała odrzucane słabiej.
    void explode(Body& bomb) {
        // Dla każdego ciała w zasięgu przykładamy impuls od środka eksplozji.
        PxVec3 p = bomb.actor->getGlobalPose().p;
        for (Body* o : fizyka.bodies) {
            if (o == &bomb || o->dead) continue;

            PxVec3 d = o->actor->getGlobalPose().p - p;
            d.z = 0.f;
            float dist = d.magnitude();
            if (dist >= Konfiguracja::BombRadius) continue;

            float J = Konfiguracja::BombImpulse * (1.f - dist / Konfiguracja::BombRadius);
            PxVec3 dir = dist > 0.001f ? d / dist : PxVec3(0.f, 1.f, 0.f);
            o->actor->addForce(dir * J, PxForceMode::eIMPULSE);
            o->impulse += J;
        }
        flashes.push_back({ p.x, p.y, 0.f });
        bomb.dead = true;
    }

    // Zamienia czas klatki na stałe kroki PhysX, by fizyka nie zależała od FPS.
    void simulate(double frameDt) {
        if (state == MENU) { accumulator = 0.0; return; }
        accumulator += frameDt;
        while (accumulator >= Konfiguracja::Dt) {
            fizyka.step(Konfiguracja::Dt);
            levelTime += Konfiguracja::Dt;
            sinceLaunch += Konfiguracja::Dt;
            totalPlayTime += Konfiguracja::Dt;
            applyDamageRules();
            fizyka.removeDead();
            updateFlashes();
            checkEnd();
            accumulator -= Konfiguracja::Dt;
        }
    }

    // Reguły zniszczeń. Faza "settling" wyłącza obrażenia, ale nie akumulację impulsów -
    // przy przejściu settling -> !settling zerujemy impulse, żeby śmieci z fazy
    // osiadania nie wywołały fałszywego pęknięcia w pierwszym "dorosłym" kroku.
    // Stosuje progi obrażeń i rozbija zapisane bloki po iteracji po ciałach.
    void applyDamageRules() {
        const bool settling = levelTime < Konfiguracja::SettleTime;
        if (!settling && wasSettling) {
            for (Body* b : fizyka.bodies) b->impulse = 0.f;
        }
        wasSettling = settling;

        std::vector<Fracture> toBreak;

        for (Body* b : fizyka.bodies) {
            b->age += Konfiguracja::Dt;

            if (!settling) {
                if (b->type == PIG && b->impulse > Konfiguracja::PigDeathImpulse) {
                    b->dead = true;
                }
                else if (b->type == BLOCK && b->impulse > Dane::Mats[b->mat].breakImpulse) {
                    b->dead = true;
                    if (!b->fragment && !b->noShatter)
                        toBreak.push_back(fizyka.makeFracture(*b));
                }
            }

            if (b->type == BIRD && b->age > Konfiguracja::BirdLifetime)
                b->dead = true;

            PxVec3 p = b->actor->getGlobalPose().p;
            if (p.y < -6.f || p.x > 50.f || p.x < -10.f)
                b->dead = true;

            b->impulse = 0.f;
        }

        for (const Fracture& f : toBreak)
            fizyka.shatter(f);
    }

    // Zwiększa wiek efektów wybuchu i usuwa te, których animacja już się skończyła.
    void updateFlashes() {
        for (Flash& f : flashes) f.t += Konfiguracja::Dt;
        flashes.erase(std::remove_if(flashes.begin(), flashes.end(),
            [](const Flash& f) { return f.t > Konfiguracja::FlashTime; }),
            flashes.end());
    }

    // Liczy świnki, które nie zostały oznaczone do usunięcia.
    int pigsAlive() const {
        int pigs = 0;
        for (const Body* b : fizyka.bodies)
            if (b->type == PIG && !b->dead) ++pigs;
        return pigs;
    }

    // Ptak "w locie" także wtedy, gdy wiruje w miejscu (np. utknął na zawiasie).
    bool birdInFlight() const {
        for (const Body* b : fizyka.bodies) {
            if (b->type != BIRD || b->dead) continue;
            if (b->actor->getLinearVelocity().magnitude() > Konfiguracja::BirdRestSpeed) return true;
            if (b->actor->getAngularVelocity().magnitude() > Konfiguracja::BirdRestAngular) return true;
        }
        return false;
    }

    // Wygrana wymaga braku świnek; przegrana czeka na koniec lotu i reakcji konstrukcji.
    void checkEnd() {
        if (state != PLAYING) return;
        if (pigsAlive() == 0) {
            winTimer += Konfiguracja::Dt;
            if (winTimer > Konfiguracja::WinDelay) state = WON;
            return;
        }
        winTimer = 0.f;
        if (birdIdx >= queue.size()) {
            if (sinceLaunch > Konfiguracja::LoseHardTimeout) { state = LOST; return; }
            if (!birdInFlight()) {
                loseTimer += Konfiguracja::Dt;
                if (loseTimer > Konfiguracja::LoseDelay) state = LOST;
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
        grafika.circle(x, y, r, Dane::Birds[k].color);
        float ex = x + r * (0.35f * c - 0.30f * s), ey = y + r * (0.35f * s + 0.30f * c);
        grafika.circle(ex, ey, r * 0.28f, Dane::White);
        grafika.circle(ex + r * 0.05f * c, ey + r * 0.05f * s, r * 0.13f, Dane::Black);
    }
    static void drawBird(Kind k, float x, float y, float r) {
        drawBirdRotated(k, x, y, r, 0.f);
    }
    static void drawPig(float x, float y, float r, float angle) {
        float c = std::cos(angle), s = std::sin(angle);
        grafika.circle(x, y, r, Dane::Pig);
        grafika.circle(x - 0.2f * c, y - 0.2f * s, r * 0.18f, Dane::White);
        grafika.circle(x + 0.2f * c, y + 0.2f * s, r * 0.18f, Dane::White);
    }
    static void drawBall(const Body& b, float x, float y, float a) {
        Color c = Dane::Mats[b.mat].color;
        grafika.circle(x, y, b.radius, c);
        grafika.circle(x + 0.5f * b.radius * std::cos(a), y + 0.5f * b.radius * std::sin(a),
            b.radius * 0.22f, { c.r * 0.5f, c.g * 0.5f, c.b * 0.5f });
    }
    static void drawBody(const Body& b, float alpha) {
        // Wybiera kształt i kolor na podstawie typu obiektu oraz jego aktualnej pozy.
        Pose p = b.pose(alpha);
        switch (b.type) {
        case BLOCK: {
            Color c = Dane::Mats[b.mat].color;
            if (b.fragment) { c.r *= 0.75f; c.g *= 0.75f; c.b *= 0.75f; }
            grafika.quad(p.x, p.y, b.half.x, b.half.y, p.angle, c);
            if (b.armLen > 0.f) {
                float cs = std::cos(p.angle), sn = std::sin(p.angle);
                PxVec2 top(p.x - b.armLen * sn, p.y + b.armLen * cs);
                for (int side = -1; side <= 1; side += 2) {
                    float lx = side * b.half.x;
                    grafika.line(PxVec2(p.x + lx * cs, p.y + lx * sn), top, 0.04f, Dane::SlingBand);
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
        grafika.quad(Konfiguracja::AnchorX, 1.5f, 0.2f, 1.7f, 0.f, Dane::Sling);
    }

    void drawExplosions() const {
        for (const Flash& f : flashes) {
            float k = f.t / Konfiguracja::FlashTime;
            Color c = Dane::Explosion;
            c.a = 0.6f * (1.f - k);
            grafika.circle(f.x, f.y, Konfiguracja::BombRadius * k, c);
        }
    }

    // Rysuje procę, ptaka i tor lotu. Gdy ptak jest załadowany (nie przeciągany),
    // rysujemy też cięciwę w spoczynku - między końcami widełek a ptakiem.
    void drawActiveBird() const {
        if (state != PLAYING || birdIdx >= queue.size()) return;
        Kind k = queue[birdIdx];
        const PxVec2 left(Konfiguracja::AnchorX - 0.2f, Konfiguracja::AnchorY);
        const PxVec2 right(Konfiguracja::AnchorX + 0.2f, Konfiguracja::AnchorY);

        if (dragging) {
            grafika.line(left, dragPos, 0.06f, Dane::SlingBand);
            grafika.line(right, dragPos, 0.06f, Dane::SlingBand);
            drawBird(k, dragPos.x, dragPos.y, Dane::Birds[k].radius);

            PxVec2 v = launchVelocity(k);
            for (int i = 1; i <= 28; ++i) {
                float t = i * 0.1f;
                float a = 1.f - i / 32.f;
                grafika.circle(dragPos.x + v.x * t,
                    dragPos.y + v.y * t - 0.5f * Konfiguracja::Gravity * t * t,
                    0.08f, { 1.f, 1.f, 1.f, a });
            }
        }
        else if (sinceLaunch > Konfiguracja::ReloadDelay) {
            PxVec2 at(Konfiguracja::AnchorX, Konfiguracja::AnchorY);
            grafika.line(left, at, 0.06f, Dane::SlingBand);
            grafika.line(right, at, 0.06f, Dane::SlingBand);
            drawBird(k, at.x, at.y, Dane::Birds[k].radius);
        }
    }

    void drawQueue() const {
        // Pokazuje pozostałe ptaki małymi ikonami obok procy.
        if (state != PLAYING) return;
        for (size_t i = birdIdx + 1, n = 0; i < queue.size(); ++i, ++n) {
            Kind k = queue[i];
            float r = Dane::Birds[k].radius * Konfiguracja::QueueScale;
            float x = Konfiguracja::QueueStartX - (float)n * Konfiguracja::QueueSpacing;
            if (x < 0.7f) break;
            drawBird(k, x, r, r);
        }
    }

    void drawHud() const {
        // HUD pokazuje aktualną liczbę niewystrzelonych ptaków i żywych świnek.
        if (state != PLAYING) return;
        char buf[96];
        int birdsLeft = (int)(queue.size() - birdIdx);
        std::snprintf(buf, sizeof buf, "BIRDS %d  PIGS %d", birdsLeft, pigsAlive());
        tekst.draw(buf, 0.5f, 16.6f, 0.14f, Dane::Hud);
    }

    void drawMenu() const {
        // Menu startowe zawiera tytuł, przykładowe ptaki i przyciski.
        tekst.drawCentered("ANGRY BIRDS", 16.f + 0.15f, 13.5f - 0.15f, 0.3f, Dane::Black);
        tekst.drawCentered("ANGRY BIRDS", 16.f, 13.5f, 0.3f, { 0.9f, 0.25f, 0.15f });
        for (int i = 0; i < 4; ++i)
            drawBird((Kind)i, 10.f + i * 4.f, 11.f, Dane::Birds[i].radius * 1.3f);
        interfejs.Play.draw(interfejs.Play.contains(mouse));
        interfejs.Quit.draw(interfejs.Quit.contains(mouse));
    }

    void drawOverlay() const {
        // Przyciemnienie tła podsumowuje wynik i udostępnia ponowienie lub powrót do menu.
        grafika.quad(16.f, 9.f, 16.f, 9.f, 0.f, { 0.f, 0.f, 0.f, 0.45f });
        auto button = [&](const Button& b) { b.draw(b.contains(mouse)); };
        if (state == WON)
            tekst.drawCentered("YOU WIN", 16.f, 11.f, 0.3f, { 1.f, 0.95f, 0.3f });
        else if (state == LOST)
            tekst.drawCentered("GAME OVER", 16.f, 11.f, 0.25f, { 1.f, 0.3f, 0.3f });
        button(interfejs.Retry);
        button(interfejs.MenuRight);
    }
};

// Punkt wejścia inicjalizuje biblioteki, uruchamia pętlę i zwalnia zasoby.
int main() {
    // Callback wypisuje komunikaty GLFW, np. błąd utworzenia okna.
    glfwSetErrorCallback([](int code, const char* msg) {
        std::fprintf(stderr, "GLFW error %d: %s\n", code, msg);
        });
    if (!glfwInit())
        return -1;

    // Wybierz kontekst zgodny z używanymi funkcjami OpenGL 2.1.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    GLFWwindow* window = glfwCreateWindow(Konfiguracja::WindowW, Konfiguracja::WindowH,
        "Angry Birds - PhysX + OpenGL", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    // Ustaw kontekst OpenGL okna i przygotuj funkcje renderujące.
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    grafika.layout(window);
    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // Kolejność inicjalizacji: renderer, atlas tekstu, a na końcu scena fizyczna.
    grafika.init();
    tekst.init();
    if (!fizyka.init()) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // Gra i wejście przechowują stan między kolejnymi klatkami.
    Gra gra;
    Wejscie wejscie;
    double poprzedniCzas = glfwGetTime();
    while (!glfwWindowShouldClose(window) && !gra.quit) {
        // Zbierz zdarzenia, przelicz wejście i zaktualizuj świat.
        glfwPollEvents();
        grafika.layout(window);
        wejscie.poll(window);

        double aktualnyCzas = glfwGetTime();
        gra.update(wejscie, std::min(aktualnyCzas - poprzedniCzas, 0.1));
        poprzedniCzas = aktualnyCzas;

        // Narysuj zaktualizowany stan i pokaż gotową klatkę w oknie.
        grafika.beginFrame(window);
        gra.draw();
        glfwSwapBuffers(window);
    }

    // Zwalniaj zasoby w odwrotnej kolejności niż ich inicjalizacja.
    fizyka.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}