// ============================================================================
// Angry Birds - PhysX + OpenGL (GLFW)
//
// Gra 2D oparta na silniku PhysX 3D z zablokowanymi stopniami swobody
// (ruch w płaszczyźnie XY, obrót wokół osi Z). Cała fizyka (ruch, kolizje,
// zawiasy, silniki) jest liczona przez PhysX; OpenGL służy wyłącznie do
// rysowania.
//
// Struktura pliku:
//   1. Config / GameData  - stałe i dane statyczne
//   2. Physics            - opakowanie PhysX (scena, ciała, kontakty, zawiasy)
//   3. Levels             - budowa konstrukcji poziomu
//   4. Graphics / Text / Ui / Input - warstwa prezentacji i wejścia
//   5. Game               - logika rozgrywki
//   6. main               - pętla główna
// ============================================================================

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



// Wszystkie parametry liczbowe gry w jednym miejscu (okno, świat,
// grawitacja, krok fizyki, proca, progi czasowe, ustawienia solvera).
// Jednostki: metry, sekundy, kilogramy, radiany.
class Config {
public:

    // --- Okno i świat gry ---
    static constexpr int WindowW = 1280, WindowH = 720;
    // Logiczny rozmiar świata [m]; niezależny od rozdzielczości okna.
    static constexpr float ViewW = 32.f, ViewH = 18.f;

    static constexpr float Pi = 3.14159265f;

    // --- Symulacja ---
    static constexpr float Gravity = 9.81f;
    // Stały krok fizyki (60 Hz), niezależny od liczby klatek renderowania.
    static constexpr float Dt = 1.f / 60.f;

    // --- Proca ---
    // Punkt zaczepienia procy w świecie.
    static constexpr float AnchorX = 5.f;
    static constexpr float AnchorY = 3.2f;
    // Maksymalny naciąg [m]; dłuższy wektor jest przycinany.
    static constexpr float MaxPull = 3.f;
    // Minimalny naciąg [m]; krótszy anuluje strzał (ptak nie jest tracony).
    static constexpr float MinPull = 0.3f;
    // Sztywność sprężyny procy [N/m]; wyznacza prędkość wystrzału (v = x*sqrt(k/m)).
    static constexpr float SlingStiffness = 80.f;
    // Promień [m], w którym kliknięcie przy procy rozpoczyna przeciąganie.
    static constexpr float GrabRadius = 1.5f;
    // Czas przeładowania procy po strzale [s].
    static constexpr float ReloadDelay = 1.5f;

    // --- Kolejka ptaków (ikony na ekranie) ---
    // Współrzędna X pierwszej ikony; kolejne leżą na lewo od niej.
    static constexpr float QueueStartX = 3.6f;
    // Odstęp między ikonami [m].
    static constexpr float QueueSpacing = 0.65f;
    // Skala ikon względem rzeczywistego promienia ptaka.
    static constexpr float QueueScale = 0.6f;

    // --- Parametry brył ---
    // Jedyne sztuczne tłumienie w symulacji: tłumienie obrotu [1/s] dla wszystkich ciał.
    static constexpr float AngularDamping = 0.1f;
    // Gęstości [kg/m^3]; masa wynika z objętości kuli.
    static constexpr float BirdDensity = 3.f;
    static constexpr float PigDensity = 1.f;

    // --- Obrażenia i warunki końca gry ---
    // Czas stabilizacji konstrukcji po starcie poziomu [s]; obrażenia są wtedy wyłączone.
    static constexpr float SettleTime = 1.5f;
    // Skumulowany impuls kontaktu [N*s], powyżej którego świnia ginie.
    static constexpr float PigDeathImpulse = 3.f;
    // Maksymalny czas życia ptaka [s]; po nim ptak jest usuwany.
    static constexpr float BirdLifetime = 12.f;
    // Opóźnienie ekranu wygranej po zabiciu ostatniej świni [s].
    static constexpr float WinDelay = 0.8f;
    // Opóźnienie ekranu przegranej po wyhamowaniu ostatniego ptaka [s].
    static constexpr float LoseDelay = 3.f;
    // Progi uznania ptaka za nieruchomego: prędkość liniowa [m/s] i kątowa [rad/s].
    static constexpr float BirdRestSpeed = 0.5f;
    static constexpr float BirdRestAngular = 1.0f;
    // Twardy limit czasu od ostatniego strzału [s]; po nim gra ogłasza przegraną.
    static constexpr float LoseHardTimeout = BirdLifetime + 2.f;

    // --- Zdolności ptaków ---
    // Wybuch (czarny ptak): promień rażenia [m] i maksymalny impuls w epicentrum [N*s]
    // (maleje liniowo z odległością).
    static constexpr float BombRadius = 6.f;
    static constexpr float BombImpulse = 20.f;
    // Czas wyświetlania błysku po wybuchu [s].
    static constexpr float FlashTime = 0.4f;
    // Żółty ptak: mnożnik prędkości po aktywacji zdolności.
    static constexpr float YellowBoost = 2.f;
    // Niebieski ptak: kąt rozwidlenia bocznych ptaków przy podziale na trzy [rad].
    static constexpr float SplitAngle = 0.25f;

    // --- Solver PhysX ---
    // Iteracje pozycyjne (stabilność stosów i zawiasów) oraz prędkościowe (stabilność kolizji).
    static constexpr int SolverPos = 12;
    static constexpr int SolverVel = 4;
};


struct Color { float r, g, b, a = 1.f; };

// Definicja typu ptaka: promień kuli i kolor ciała.
struct BirdDef { float radius; Color color; };
// Definicja materiału konstrukcji: gęstość, impuls niszczący, tarcie, sprężystość, kolor.
struct MatDef { float density, breakImpulse, friction, restitution; Color color; };

// Typ obiektu w świecie gry (decyduje o zachowaniu i sposobie rysowania).
enum Type { BLOCK, PIG, BIRD, BALL };
// Materiały konstrukcyjne.
enum Mat { WOOD, STONE, GLASS, METAL, ALU, NumMats };
// Rodzaje ptaków (każdy ma inną zdolność specjalną).
enum Kind { RED, YELLOW, BLUE, BLACK };
// Aktualny ekran gry.
enum State { MENU, PLAYING, WON, LOST };

//  katalog stałych danych gry (kolory, definicje ptaków i materiałów)
// oraz deklaracje struktur pomocniczych współdzielonych przez klasy logiki.
class GameData {
public:
    // Uproszczona poza obiektu dla renderera (x, y, kąt).
    struct Pose;
    // Łączy aktora PhysX z danymi gry (typ, materiał, rozmiar, stan).
    struct Body;
    // Efekt wizualny wybuchu (pozycja i czas życia).
    struct Flash;
    // Migawka bloku w chwili rozpadu (pozycja, prędkości, wymiary, materiał).
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

    // Próg zniszczenia dla materiałów, które nie powinny pękać.
    static constexpr float Unbreakable = 1e30f;
    // Definicje ptaków, indeksowane wartością Kind.
    static const BirdDef Birds[4];
    // Definicje materiałów, indeksowane wartością Mat.
    static const MatDef Mats[NumMats];
};
// Ptaki: promień kuli i kolor (kolejność zgodna z enum Kind).
const BirdDef GameData::Birds[4] = {
    {0.50f, {0.85f, 0.15f, 0.15f}}, // czerwony
    {0.42f, {0.97f, 0.85f, 0.10f}}, // żółty
    {0.35f, {0.30f, 0.60f, 0.95f}}, // niebieski
    {0.60f, {0.10f, 0.10f, 0.10f}}, // czarny
};
// Materiały: gęstość, próg zniszczenia, tarcie, sprężystość, kolor (kolejność zgodna z enum Mat).
const MatDef GameData::Mats[NumMats] = {
    {1.0f, 9.0f, 0.65f, 0.15f, {0.65f, 0.45f, 0.25f}},  // drewno
    {2.5f, GameData::Unbreakable, 0.70f, 0.05f, {0.55f, 0.55f, 0.60f}}, // kamień (kula wahadła, domino)
    {0.5f, 4.0f, 0.55f, 0.10f, {0.70f, 0.90f, 0.95f}}, // szkło
    {7.8f, GameData::Unbreakable, 0.40f, 0.20f, {0.35f, 0.38f, 0.45f}}, // metal (ogniwa wahadła)
    {2.7f, GameData::Unbreakable, 0.40f, 0.20f, {0.75f, 0.78f, 0.85f}}, // aluminium (siedzisko, wiatrak)
};

// Wyciąga kąt obrotu wokół osi Z z kwaternionu PhysX.
inline float angleZ(const PxQuat& q) { return 2.f * std::atan2(q.z, q.w); }

struct GameData::Pose { float x, y, angle; };

// obiekt gry powiązany z aktorem PhysX. 
struct GameData::Body {
    PxRigidDynamic* actor = nullptr;

    Type type = BLOCK;
    Mat mat = WOOD;
    Kind kind = RED;
    int id = 0;

    // Połowy wymiarów (bloki) lub promień (kule).
    PxVec2 half{ 0, 0 };
    float radius = 0;

    // Suma impulsów kontaktowych zebranych w bieżącym kroku [N*s] - podstawa obrażeń.
    // Zerowana po każdym kroku w applyDamageRules().
    float impulse = 0;
    // Wiek ciała [s]; służy do usuwania starych ptaków.
    float age = 0;

    bool abilityUsed = false;
    bool dead = false;
    // Fragment powstały z rozpadu bloku (nie rozpada się ponownie, rysowany ciemniej).
    bool fragment = false;
    // Wyłącza rozpad; ustawiane przy przesuniętym środku masy (patrz Physics::setCom).
    bool noShatter = false;

    // Długość cięgien huśtawki 
    float armLen = 0;

    // Stan sprzed kroku fizyki. 
    PxVec3 prevPos{ 0, 0, 0 };
    float prevAngle = 0.f;
    PxVec3 preCom{ 0, 0, 0 };
    PxVec3 preV{ 0, 0, 0 };
    PxVec3 preW{ 0, 0, 0 };

    // Zapisuje stan ciała tuż przed krokiem fizyki.
    void saveState() {
        PxTransform t = actor->getGlobalPose();
        prevPos = t.p;
        prevAngle = angleZ(t.q);
        preCom = t.transform(actor->getCMassLocalPose().p);
        preV = actor->getLinearVelocity();
        preW = actor->getAngularVelocity();
    }

    // Prędkość punktu p ciała ze stanu sprzed kroku
    // Używana do oceny siły uderzenia w punkcie kontaktu.
    PxVec3 velocityAt(const PxVec3& p) const { return preV + preW.cross(p - preCom); }

    // Poza interpolowana między stanem sprzed kroku (alpha = 0) a bieżącym (alpha = 1).
    // Różnica kątów jest normalizowana, aby uniknąć skoku przy przejściu przez ±pi.
    Pose pose(float alpha) const {
        PxTransform t = actor->getGlobalPose();
        float da = std::remainder(angleZ(t.q) - prevAngle, 2.f * Config::Pi);
        return { prevPos.x + (t.p.x - prevPos.x) * alpha, prevPos.y + (t.p.y - prevPos.y) * alpha,
                prevAngle + da * alpha };
    }
};

using Pose = GameData::Pose;
using Body = GameData::Body;

struct GameData::Flash { float x, y, t; };

//  dane potrzebne do rozpadu bloku. Zapisywane przed usunięciem
// oryginału, aby fragmenty odziedziczyły jego ruch.
struct GameData::Fracture {
    PxVec3 pos;     // położenie środka bloku
    PxVec3 v;       // prędkość liniowa środka masy
    PxVec3 omega;   // prędkość kątowa
    PxVec2 half;    // połowy wymiarów
    float angle;    // kąt obrotu (fragmenty muszą być tak samo obrócone)
    Mat mat;        // materiał: decyduje o gęstości i podziale na fragmenty
};
using Flash = GameData::Flash;
using Fracture = GameData::Fracture;



//  opakowanie PhysX - inicjalizacja sceny, tworzenie ciał i materiałów,
// krok symulacji, odbiór kontaktów, zawiasy i silniki, rozpad bloków
// oraz zwalnianie zasobów.
class Physics {
public:
    // Grupy filtrowania kolizji (używane w filterShader).
    // Ptaki nie kolidują ze sobą (uproszczenie rozgrywki).
    static constexpr PxU32 BirdGroup = 1;
    // Elementy tylko wizualne (np. cięgna huśtawki) - nie kolidują z niczym.
    static constexpr PxU32 GhostGroup = 2;

    PxDefaultAllocator allocator;
    PxDefaultErrorCallback errorCallback;
    PxFoundation* foundation = nullptr;
    PxPhysics* physics = nullptr;
    PxDefaultCpuDispatcher* dispatcher = nullptr;
    PxScene* scene = nullptr;

    PxMaterial* creatureMat = nullptr;     // materiał ptaków, świń i granic planszy
    PxMaterial* matPx[NumMats] = {};       // materiały konstrukcji

    PxRigidStatic* ground = nullptr;
    PxRigidStatic* leftWall = nullptr;
    PxRigidStatic* rightWall = nullptr;

    std::vector<Body*> bodies;
    int nextId = 1;

    // Element tła bez kolizji (pozycja, wymiary, kolor).
    struct Deco { float x, y, hx, hy; Color c; };
    std::vector<Deco> decos;

    // Punkty zawieszenia rysowane jako osie obrotu.
    std::vector<PxVec2> pivots;

    // Shader filtrowania: decyduje, które pary kształtów kolidują.
    // Pary z GhostGroup oraz ptak-ptak są pomijane. Pozostałe dostają zwykłe
    // kontakty z powiadomieniami o punktach i impulsach oraz detekcję CCD.
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

    // Zwraca Body powiązane z aktorem (przez userData) albo nullptr dla ciał
    // statycznych (podłoże, ściany).
    static Body* bodyOf(PxActor* a) {
        return (a && a->userData) ? static_cast<Body*>(a->userData) : nullptr;
    }

    // Minimalna prędkość względna w punkcie kontaktu [m/s]; poniżej niej kontakt
    // nie liczy się do obrażeń (sam nacisk spoczywających bloków nic nie niszczy).
    static constexpr float MinImpactSpeed = 0.5f;

    // Prędkość względna ciał a i b w punkcie p, liczona ze stanu SPRZED kroku.
    // Ciało statyczne ma prędkość zero (przekazujemy nullptr).
    static PxVec3 relativeVelocityAt(const Body* a, const Body* b, const PxVec3& p) {
        PxVec3 v0 = a ? a->velocityAt(p) : PxVec3(0.f);
        PxVec3 v1 = b ? b->velocityAt(p) : PxVec3(0.f);
        return v0 - v1;
    }

    // ContactCallback: model obrażeń. Dla każdego punktu kontaktu sprawdza
    // prędkość względną; jeśli przekracza MinImpactSpeed, impuls kontaktu jest
    // dodawany do obu ciał (Body::impulse). Progi zniszczenia są później
    // porównywane z tą sumą w Game::applyDamageRules().
    class ContactCallback : public PxSimulationEventCallback {
    public:
        void onContact(const PxContactPairHeader& header, const PxContactPair* pairs, PxU32 count) override {
            if (!header.actors[0] || !header.actors[1])
                return;
            Body* a = Physics::bodyOf(header.actors[0]);
            Body* b = Physics::bodyOf(header.actors[1]);
            for (PxU32 i = 0; i < count; ++i) {
                // Pomijamy pary, których kształt został już usunięty.
                if (pairs[i].flags &
                    (PxContactPairFlag::eREMOVED_SHAPE_0 | PxContactPairFlag::eREMOVED_SHAPE_1))
                    continue;

                PxContactPairPoint points[16];
                PxU32 n = pairs[i].extractContacts(points, 16);
                if (n == 0)
                    continue;

                float sum = 0.f;
                for (PxU32 j = 0; j < n; ++j) {
                    PxVec3 vrel = Physics::relativeVelocityAt(a, b, points[j].position);
                    if (vrel.magnitude() < Physics::MinImpactSpeed)
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

    // Tworzy fundament, silnik, dyspozytor, scenę i materiały PhysX
    // oraz nieruchome granice planszy (podłoże, ściany). Zwraca false przy błędzie.
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
        desc.gravity = PxVec3(0.f, -Config::Gravity, 0.f);
        dispatcher = PxDefaultCpuDispatcherCreate(2);
        if (!dispatcher) {
            std::fprintf(stderr, "PhysX: PxDefaultCpuDispatcherCreate failed\n");
            return false;
        }
        desc.cpuDispatcher = dispatcher;
        desc.filterShader = Physics::filterShader;
        desc.simulationEventCallback = &contactCallback;
        // CCD zapobiega "tunelowaniu" szybkich obiektów przez cienkie przeszkody.
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
        // Tarcie dynamiczne 
        for (int i = 0; i < NumMats; ++i)
            matPx[i] = physics->createMaterial(GameData::Mats[i].friction, GameData::Mats[i].friction * 0.85f, GameData::Mats[i].restitution);

        ground = PxCreateStatic(*physics, PxTransform(PxVec3(16.f, -0.5f, 0.f)),
            PxBoxGeometry(20.f, 0.5f, 2.f), *creatureMat);
        scene->addActor(*ground);
        // Ściany sięgają bardzo wysoko, aby ptak wystrzelony stromo nie wyleciał poza planszę.
        leftWall = PxCreateStatic(*physics, PxTransform(PxVec3(0.1f, 20.f, 0.f)),
            PxBoxGeometry(0.2f, 20.5f, 2.f), *creatureMat);
        rightWall = PxCreateStatic(*physics, PxTransform(PxVec3(Config::ViewW - 0.1f, 20.f, 0.f)),
            PxBoxGeometry(0.2f, 20.5f, 2.f), *creatureMat);
        scene->addActor(*leftWall);
        scene->addActor(*rightWall);
        return true;
    }

    // Zwalnia aktora PhysX razem z danymi gry (Body i aktor wskazują na siebie nawzajem).
    void releaseBody(Body* b) {
        b->actor->release();
        delete b;
    }

    // Usuwa wszystkie dynamiczne ciała i dekoracje (przed ponowną budową poziomu).
    void clear() {
        for (Body* b : bodies)
            releaseBody(b);
        bodies.clear();
        decos.clear();
        pivots.clear();
    }

    // Zwalnia zasoby PhysX w kolejności odwrotnej do inicjalizacji.
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

    // Jeden stały krok symulacji: zapis stanu ciał (do interpolacji i obliczeń
    // uderzeń), symulacja, pobranie wyników wraz z callbackami kontaktów.
    void step(float dt) {
        for (Body* b : bodies) b->saveState();
        scene->simulate(dt);
        scene->fetchResults(true);
    }

    // Wspólna finalizacja nowego ciała: nadaje id, blokuje stopnie swobody
    // (zostaje ruch w XY i obrót wokół Z - model 2D), ustawia tłumienie
    // i iteracje solvera, łączy aktora z Body i rejestruje go w scenie.
    Body* finish(Body* b, PxRigidDynamic* actor) {
        b->actor = actor;
        b->id = nextId++;
        actor->setRigidDynamicLockFlags(PxRigidDynamicLockFlag::eLOCK_LINEAR_Z |
            PxRigidDynamicLockFlag::eLOCK_ANGULAR_X |
            PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y);
        actor->setAngularDamping(Config::AngularDamping);
        actor->setSolverIterationCounts(Config::SolverPos, Config::SolverVel);
        actor->userData = b;
        scene->addActor(*actor);
        b->saveState();
        bodies.push_back(b);
        return b;
    }

    // Tworzy prostokątny blok (połowy wymiarów hx, hy) z materiału mat, obrócony o angle.
    // Fragmenty po rozpadzie dostają CCD, bo mogą szybko się rozlatywać.
    Body* addBox(float x, float y, float hx, float hy, Mat mat, float angle = 0.f, bool fragment = false) {
        auto* b = new Body;
        b->type = BLOCK;
        b->mat = mat;
        b->half = PxVec2(hx, hy);
        b->fragment = fragment;
        PxTransform pose(PxVec3(x, y, 0.f), PxQuat(angle, PxVec3(0.f, 0.f, 1.f)));
        PxRigidDynamic* actor = PxCreateDynamic(*physics, pose, PxBoxGeometry(hx, hy, 0.5f),
            *matPx[mat], GameData::Mats[mat].density);
        if (fragment)
            actor->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
        return finish(b, actor);
    }

    // Tworzy huśtawkę: siedzisko (prostokąt) i dwa cięgna jako dodatkowe kształty
    // tego samego aktora, oznaczone GhostGroup (bez kolizji). Cięgna mają masę,
    // więc środek masy huśtawki leży wyżej niż siedzisko, a masa i moment
    // bezwładności są przeliczane po dodaniu kształtów.
    Body* addSwing(float x, float y, float hx, float hy, float armLen, Mat mat) {
        auto* b = new Body;
        b->type = BLOCK;
        b->mat = mat;
        b->half = PxVec2(hx, hy);
        b->armLen = armLen;
        PxRigidDynamic* actor = PxCreateDynamic(*physics, PxTransform(PxVec3(x, y, 0.f)),
            PxBoxGeometry(hx, hy, 0.5f), *matPx[mat], GameData::Mats[mat].density);

        // Cięgno biegnie od krawędzi siedziska do punktu zawieszenia (skośnie).
        const float L = std::sqrt(hx * hx + armLen * armLen);
        const float phi = std::atan2(hx, armLen);
        for (int side = -1; side <= 1; side += 2) {
            PxShape* arm = PxRigidActorExt::createExclusiveShape(*actor, PxBoxGeometry(0.03f, L * 0.5f, 0.5f),
                *matPx[mat]);
            arm->setLocalPose(PxTransform(PxVec3(side * hx * 0.5f, armLen * 0.5f, 0.f),
                PxQuat(side * phi, PxVec3(0.f, 0.f, 1.f))));
            arm->setSimulationFilterData(PxFilterData(GhostGroup, 0, 0, 0));
        }
        PxRigidBodyExt::updateMassAndInertia(*actor, GameData::Mats[mat].density);
        // Huśtawka nie może zasypiać, bo zawias do świata utrzymuje ją w ciągłym ruchu.
        actor->setSleepThreshold(0.f);
        return finish(b, actor);
    }

    // Tworzy kulę o promieniu radius i gęstości density (ptak, świnia albo kula wahadła).
    // Ptaki dostają BirdGroup (brak kolizji z innymi ptakami) oraz CCD,
    // żeby szybki ptak nie przeniknął przez cienką przeszkodę.
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

    // Przesuwa lokalny środek masy o dy w pionie i przelicza moment bezwładności.
    // Wyższy środek masy obniża stabilność bryły (domino, kolumna z balastem).
    // Ciało dostaje noShatter, bo fragmenty z przesuniętym środkiem masy
    // zachowywałyby się nienaturalnie.
    void setCom(Body* b, float dy) {
        PxVec3 com(0.f, dy, 0.f);
        PxRigidBodyExt::updateMassAndInertia(*b->actor, GameData::Mats[b->mat].density, &com);
        b->noShatter = true;
    }

    // Wyszukuje ciało po identyfikatorze (np. ostatnio wystrzelony ptak).
    Body* find(int id) {
        for (Body* b : bodies) if (b->id == id) return b;
        return nullptr;
    }

    // Usuwa ze sceny i z rejestru ciała oznaczone jako dead.
    // Wywoływana po kroku symulacji, aby nie modyfikować listy w trakcie iteracji.
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

    void addDeco(float x, float y, float hx, float hy, Color c) {
        decos.push_back({ x, y, hx, hy, c });
    }

    // Zapisuje migawkę bloku potrzebną do rozpadu (przed jego usunięciem).
    Fracture makeFracture(const Body& b) {
        PxTransform t = b.actor->getGlobalPose();
        return { t.p, b.actor->getLinearVelocity(), b.actor->getAngularVelocity(),
                b.half, angleZ(t.q), b.mat };
    }

    // Rozbija blok na fragmenty: szkło na siatkę 2x2, bloki wysokie na 1x3, pozostałe na 3x1.
    // Środek każdego fragmentu jest obracany o kąt bloku. Prędkość liniowa fragmentu
    // to v + omega x r, dzięki czemu zachowany jest ruch obrotowy całego bloku.
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

    // Obrót, który ustawia oś zawiasu PhysX (domyślnie X) wzdłuż osi Z świata,
    // czyli osi obrotu gry 2D.
    PxQuat axisZ() { return PxQuat(-0.5f * Config::Pi, PxVec3(0.f, 1.f, 0.f)); }

    // Przelicza ramkę ze świata na układ lokalny aktora
    // (dla a == nullptr, czyli zawiasu do świata, ramka pozostaje światowa).
    PxTransform frameOn(PxRigidActor* a, const PxTransform& world) {
        return a ? a->getGlobalPose().transformInv(world) : world;
    }

    // Tworzy zawias obrotowy między ciałami a i b w punkcie p.
    // a == nullptr oznacza zawias do świata (wahadło, huśtawka, wiatrak).
    PxRevoluteJoint* hinge(Body* a, Body* b, PxVec3 p) {
        PxTransform w(p, axisZ());
        PxRigidActor* A = a ? static_cast<PxRigidActor*>(a->actor) : nullptr;
        return PxRevoluteJointCreate(*physics, A, frameOn(A, w), b->actor, frameOn(b->actor, w));
    }

    // Zawias z napędem
    PxRevoluteJoint* motor(Body* b, PxVec3 p, float omega, float maxTorque) {
        PxRevoluteJoint* j = hinge(nullptr, b, p);
        j->setDriveVelocity(omega);
        j->setDriveForceLimit(maxTorque);
        j->setRevoluteJointFlag(PxRevoluteJointFlag::eDRIVE_ENABLED, true);
        return j;
    }
};

// Definicje wymagane w C++ dla statycznych składowych constexpr (przed C++17).
constexpr PxU32 Physics::BirdGroup;
constexpr PxU32 Physics::GhostGroup;
constexpr float Physics::MinImpactSpeed;

// Wspólna instancja systemu fizyki.
Physics physics;


// Levels: buduje konstrukcje planszy z ciał fizycznych. Metody są statyczne,
// bo klasa nie przechowuje stanu - tylko tworzy ciała w globalnej instancji physics.
class Levels {
public:
    // Wieża z floors pięter: dolne podpory szklane, wyższe drewniane.
    // Świnie stoją na piętrach 1 i 3 oraz na szczycie.
    static void buildTower(float cx, int floors) {
        constexpr float hw = 1.5f;
        for (int k = 0; k < floors; ++k) {
            float y0 = k * 2.3f;
            Mat pm = (k == 0) ? GLASS : WOOD;
            physics.addBox(cx - hw, y0 + 0.9f, 0.3f, 0.9f, pm);
            physics.addBox(cx + hw, y0 + 0.9f, 0.3f, 0.9f, pm);
            physics.addBox(cx, y0 + 2.05f, 1.9f, 0.25f, WOOD);
            if (k == 1 || k == 3)
                physics.addBall(cx, y0 + 0.5f, 0.5f, Config::PigDensity, PIG);
        }
        physics.addBall(cx, floors * 2.3f + 0.5f, 0.5f, Config::PigDensity, PIG);
    }

    // Domek: dwie podpory (materiał pillarMat), drewniany dach i dwie świnie
    // (pod dachem i na dachu).
    static void buildHut(float cx, Mat pillarMat) {
        physics.addBox(cx - 0.9f, 0.6f, 0.2f, 0.6f, pillarMat);
        physics.addBox(cx + 0.9f, 0.6f, 0.2f, 0.6f, pillarMat);
        physics.addBox(cx, 1.35f, 1.3f, 0.15f, WOOD);
        physics.addBall(cx, 0.5f, 0.5f, Config::PigDensity, PIG);
        physics.addBall(cx, 1.5f + 0.5f, 0.5f, Config::PigDensity, PIG);
    }

    // Wiatrak: aluminiowa belka na zawiasie z silnikiem (1.2 rad/s, maks. moment 100 N*m).
    // Belka zamiata wysokości ok. 3.8-6.6 m, więc strzały płaskie są odbijane
    // i trzeba celować łukiem.
    static void buildWindmill(float cx, float cy, float halfLen) {
        constexpr float AngularSpeed = 1.2f;
        constexpr float MaxTorque = 100.f;
        Body* arm = physics.addBox(cx, cy, halfLen, 0.1f, ALU);
        arm->actor->setSleepThreshold(0.f);
        physics.motor(arm, PxVec3(cx, cy, 0.f), AngularSpeed, MaxTorque);
        physics.pivots.push_back(PxVec2(cx, cy));
        physics.addDeco(cx, cy * 0.5f, 0.12f, cy * 0.5f, GameData::Frame);
    }

    // Rząd n kamiennych kostek domina co spacing. setCom podnosi środek masy
    // każdej kostki o ComShift, co zmniejsza stabilność i ułatwia reakcję łańcuchową.
    static void buildDominoes(float x0, int n, float spacing) {
        constexpr float HalfH = 0.7f;
        constexpr float ComShift = 0.5f * HalfH;
        for (int i = 0; i < n; ++i) {
            Body* b = physics.addBox(x0 + i * spacing, HalfH, 0.12f, HalfH, STONE);
            physics.setCom(b, ComShift);
        }
    }

    // Wahadło: links metalowych ogniw połączonych zawiasami (pierwsze zawieszone
    // do świata), zakończone kamienną kulą o promieniu ballR.
    static void buildPendulum(float ax, float ay, int links, float ballR) {
        const float L = 0.7f;
        Body* prev = nullptr;
        for (int i = 0; i < links; ++i) {
            Body* link = physics.addBox(ax, ay - 0.5f * L - i * L, 0.09f, 0.5f * L, METAL);
            physics.hinge(prev, link, PxVec3(ax, ay - i * L, 0.f));
            prev = link;
        }
        Body* ball = physics.addBall(ax, ay - links * L - ballR, ballR, GameData::Mats[STONE].density, BALL, STONE);
        physics.hinge(prev, ball, PxVec3(ax, ay - links * L, 0.f));
        physics.pivots.push_back(PxVec2(ax, ay));
        physics.addDeco(ax, (ay + Config::ViewH) * 0.5f, 0.08f, (Config::ViewH - ay) * 0.5f, GameData::Frame);
    }

    // Huśtawka: siedzisko zawieszone zawiasem w punkcie (cx, topY),
    // dwie dekoracje ramy i świnia na siedzisku.
    static void buildSwing(float cx, float topY, float len) {
        physics.addDeco(cx, topY + 0.15f, 2.0f, 0.15f, GameData::Frame);
        physics.addDeco(cx, (topY + 0.3f + Config::ViewH) * 0.5f, 0.15f,
            (Config::ViewH - topY - 0.3f) * 0.5f, GameData::Frame);

        Body* seat = physics.addSwing(cx, topY - len, 1.2f, 0.15f, len, ALU);
        physics.hinge(nullptr, seat, PxVec3(cx, topY, 0.f));
        physics.pivots.push_back(PxVec2(cx, topY));
        physics.addBall(cx, topY - len + 0.15f + 0.5f, 0.5f, Config::PigDensity, PIG);
    }

    // Drewniana kolumna z podniesionym środkiem masy (łatwo się przewraca)
    // i świnią na szczycie.
    static void buildTopHeavyColumn(float x) {
        Body* col = physics.addBox(x, 1.5f, 0.4f, 1.5f, WOOD);
        physics.setCom(col, 0.5f);
        physics.addBall(x, 3.5f, 0.5f, Config::PigDensity, PIG);
    }

    // Układ poziomu od lewej do prawej.
    static void build() {
        buildHut(11.7f, GLASS);
        buildWindmill(14.0f, 8.2f, 1.4f);
        buildDominoes(17.2f, 5, 0.8f);
        buildPendulum(19.4f, 7.6f, 4, 0.6f);
        buildTower(23.0f, 4);
        buildSwing(27.2f, 10.0f, 4.0f);
        buildHut(27.2f, WOOD);
        buildTopHeavyColumn(30.5f);
    }

    // Kolejność ptaków dostępnych w poziomie (10 sztuk).
    static std::vector<Kind> birdQueue() {
        return { RED, BLUE, YELLOW, BLACK, RED, YELLOW, BLACK, BLUE, RED, BLACK };
    }
};



// Graphics: rysowanie prymitywów 2D  oraz mapowanie
// współrzędnych świata gry na piksele okna z zachowaniem proporcji (letterbox).
class Graphics {
public:
    // Rozmiar bufora ramki.
    int fbW = Config::WindowW;
    int fbH = Config::WindowH;
    // Viewport z zachowaną proporcją świata (piksele).
    int vpX = 0;
    int vpY = 0;
    int vpW = Config::WindowW;
    int vpH = Config::WindowH;
    // Viewport jako ułamki okna; służą do przeliczania pozycji myszy na współrzędne świata.
    float fracX = 0.f;
    float fracY = 0.f;
    float fracW = 1.f;
    float fracH = 1.f;

    // Dopasowuje viewport do proporcji świata 32:18 (pasy po bokach lub u góry i dołu)
    // i zapisuje jego położenie.
    void layout(GLFWwindow* win) {
        glfwGetFramebufferSize(win, &fbW, &fbH);
        if (fbW <= 0 || fbH <= 0) return;
        const float aspect = Config::ViewW / Config::ViewH;
        vpW = fbW; vpH = (int)(fbW / aspect);
        if (vpH > fbH) { vpH = fbH; vpW = (int)(fbH * aspect); }
        vpX = (fbW - vpW) / 2; vpY = (fbH - vpH) / 2;
        fracX = (float)vpX / fbW; fracY = (float)vpY / fbH;
        fracW = (float)vpW / fbW; fracH = (float)vpH / fbH;
    }

    // Włącza mieszanie alfa (przezroczystość efektów i tekstu).
    void init() {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    // Czyści ekran (czarne pasy poza viewportem, niebo wewnątrz) i ustawia
    // projekcję ortograficzną (0..ViewW, 0..ViewH): od tej chwili rysujemy
    // we współrzędnych świata gry.
    void beginFrame(GLFWwindow* win) {
        (void)win;
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glViewport(vpX, vpY, vpW, vpH);
        glEnable(GL_SCISSOR_TEST);
        glScissor(vpX, vpY, vpW, vpH);
        glClearColor(GameData::Sky.r, GameData::Sky.g, GameData::Sky.b, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, Config::ViewW, 0.0, Config::ViewH, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }

    // Prostokąt o środku (x, y), połowach wymiarów hx, hy i obrocie angle [rad] wokół Z.
    void quad(float x, float y, float hx, float hy, float angle, Color c) {
        glColor4f(c.r, c.g, c.b, c.a);
        glPushMatrix();
        glTranslatef(x, y, 0.f);
        glRotatef(angle * 180.f / Config::Pi, 0.f, 0.f, 1.f);
        glScalef(hx, hy, 1.f);
        glBegin(GL_QUADS);
        glVertex2f(-1.f, -1.f); glVertex2f(1.f, -1.f);
        glVertex2f(1.f, 1.f);   glVertex2f(-1.f, 1.f);
        glEnd();
        glPopMatrix();
    }

    // Koło 
    void circle(float x, float y, float radius, Color c) {
        constexpr int segments = 40;
        glColor4f(c.r, c.g, c.b, c.a);
        glPushMatrix();
        glTranslatef(x, y, 0.f);
        glScalef(radius, radius, 1.f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(0.f, 0.f);
        for (int i = 0; i <= segments; ++i) {
            float t = 2.f * Config::Pi * i / segments;
            glVertex2f(std::cos(t), std::sin(t));
        }
        glEnd();
        glPopMatrix();
    }

    // Odcinek między a i b narysowany jako cienki obrócony prostokąt.
    void line(PxVec2 a, PxVec2 b, float thickness, Color c) {
        PxVec2 d = b - a;
        quad((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, d.magnitude() * 0.5f, thickness,
            std::atan2(d.y, d.x), c);
    }
};

Graphics graphics;

// ładuje czcionkę TTF do atlasu glifów (stb_truetype) i rysuje
// napisy jako teksturowane czworokąty w siatce pikseli, dzięki czemu czcionka
// pikselowa pozostaje ostra przy każdej rozdzielczości.
class TextRenderer {
public:
    // Plik czcionki musi leżeć w katalogu roboczym programu.
    const char* fontPath = "PressStart2P-Regular.ttf";
    const int atlasSize = 512;
    const float fontPx = 8.f;

    // Zakres znaków Unicode (pierwszy kod i liczba znaków).
    struct Range { int first, count; };
    // Pakowane zakresy: ASCII, Latin-1 i Latin Extended-A (polskie znaki).
    const Range ranges[3] = { {0x20, 95}, {0xA0, 96}, {0x100, 128} };
    const int numRanges = 3;

    stbtt_packedchar glyphs[3][128];
    GLuint texture = 0;
    bool ready = false;

    // Zaokrągla do całkowitego piksela (ostrość czcionki pikselowej).
    float snap(float v) { return std::floor(v + 0.5f); }

    // Wczytuje cały plik do bufora. Zwraca false, gdy pliku brak lub jest pusty.
    bool readFile(const char* path, std::vector<unsigned char>& out) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return false;
        std::streamsize size = file.tellg();
        if (size <= 0) return false;
        out.resize((size_t)size);
        file.seekg(0, std::ios::beg);
        return (bool)file.read(reinterpret_cast<char*>(out.data()), size);
    }

    // Pakuje glify do atlasu alfa, tworzy teksturę OpenGL i ustawia flagę ready.
    // Brak czcionki nie blokuje gry - nie są wtedy rysowane tylko napisy.
    void init() {
        std::vector<unsigned char> ttf;
        if (!readFile(fontPath, ttf)) {
            std::fprintf(stderr, "Brak pliku czcionki: %s\n", fontPath);
            return;
        }
        std::vector<unsigned char> alpha(atlasSize * atlasSize);
        stbtt_pack_context pc;
        if (!stbtt_PackBegin(&pc, alpha.data(), atlasSize, atlasSize, 0, 1, nullptr))
            return;
        stbtt_PackSetOversampling(&pc, 1, 1);
        for (int i = 0; i < numRanges; ++i)
            if (!stbtt_PackFontRange(&pc, ttf.data(), 0, STBTT_POINT_SIZE(fontPx),
                ranges[i].first, ranges[i].count, glyphs[i]))
                std::fprintf(stderr, "Atlas pełny: zakres 0x%X\n", ranges[i].first);
        stbtt_PackEnd(&pc);
        // Atlas jednokanałowy (alfa) rozszerzamy do RGBA z białym kolorem;
        // faktyczny kolor tekstu nadaje glColor4f.
        std::vector<unsigned char> rgba(atlasSize * atlasSize * 4, 255);
        for (int i = 0; i < atlasSize * atlasSize; ++i)
            rgba[i * 4 + 3] = alpha[i];
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlasSize, atlasSize, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        ready = true;
    }

    // Dekoduje jeden znak UTF-8 
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
        for (int i = 0; i < numRanges; ++i)
            if (cp >= ranges[i].first && cp < ranges[i].first + ranges[i].count)
                return &glyphs[i][cp - ranges[i].first];
        return cp == '?' ? nullptr : glyph('?');
    }

    // Przelicza rozmiar tekstu ze świata na całkowitą krotność pikseli czcionki
    int scale(float size) {
        float pixelsPerUnit = (float)graphics.vpW / Config::ViewW;
        return std::max(1, (int)std::lround(5.5f * size * pixelsPerUnit / fontPx));
    }

    // Szerokość napisu  (do wyśrodkowania).
    float width(const char* s, float size) {
        int sc = scale(size);
        float px = 0.f;
        for (const char* p = s; *p;)
            if (const stbtt_packedchar* g = glyph(decode(p)))
                px += snap(g->xadvance * sc);
        return px / graphics.vpW * Config::ViewW;
    }

    // Rysuje napis w punkcie (x, y) świata. 
    void draw(const char* s, float x, float y, float size, Color c) {
        if (!ready) return;
        int sc = scale(size);
        float px = std::floor(x / Config::ViewW * graphics.vpW);
        float py = std::floor((1.f - y / Config::ViewH) * graphics.vpH);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0.0, graphics.vpW, graphics.vpH, 0.0, -1.0, 1.0);
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
            float u0 = g->x0 / (float)atlasSize, u1 = g->x1 / (float)atlasSize;
            float v0 = g->y0 / (float)atlasSize, v1 = g->y1 / (float)atlasSize;
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

    // Rysuje napis wyśrodkowany w punkcie (cx, cy); używane przez przyciski i tytuły.
    void drawCentered(const char* s, float cx, float cy, float size, Color c) {
        float baselineShift = 3.5f * scale(size) * (Config::ViewH / (float)graphics.vpH);
        draw(s, cx - width(s, size) * 0.5f, cy - baselineShift, size, c);
    }
};

TextRenderer text;

// przycisk interfejsu (środek, wymiary, etykieta).
struct Button {
    float x;
    float y;
    float w;
    float h;
    const char* label;

    // Czy punkt m (kursor we współrzędnych świata) leży wewnątrz przycisku.
    bool contains(PxVec2 m) const {
        return std::fabs(m.x - x) < w * 0.5f && std::fabs(m.y - y) < h * 0.5f;
    }

    // Rysuje cień, ramkę, tło i etykietę; hover rozjaśnia przycisk.
    void draw(bool hover) const {
        float k = hover ? 1.15f : 1.f;
        graphics.quad(x + 0.12f, y - 0.12f, w * 0.5f, h * 0.5f, 0.f, { 0.f, 0.f, 0.f, 0.35f });
        graphics.quad(x, y, w * 0.5f, h * 0.5f, 0.f, { 0.15f, 0.10f, 0.05f });
        graphics.quad(x, y, w * 0.5f - 0.1f, h * 0.5f - 0.1f, 0.f,
            { std::min(1.f, 0.90f * k), std::min(1.f, 0.55f * k), std::min(1.f, 0.10f * k) });
        text.drawCentered(label, x, y, 0.16f, GameData::White);
    }
};

// przyciski menu głównego i ekranów końcowych.
class Ui {
public:
    const Button Play{ 16.f, 8.f, 6.f, 1.8f, "PLAY" };
    const Button Quit{ 16.f, 5.5f, 6.f, 1.8f, "QUIT" };
    const Button Retry{ 12.8f, 6.5f, 6.f, 1.8f, "RETRY" };
    const Button MenuRight{ 19.2f, 6.5f, 6.f, 1.8f, "MENU" };
};

Ui ui;

// stan myszy i klawiatury.
class Input {
public:
    PxVec2 mouse;          // pozycja kursora we współrzędnych świata
    bool lmb = false;      // lewy przycisk myszy wciśnięty
    bool clicked = false;  // lewy przycisk dopiero co wciśnięty (zbocze)
    bool escPressed = false;
    bool rPressed = false;

    // Odczytuje stan wejścia raz na klatkę.
    void poll(GLFWwindow* win) {
        double mx, my;
        int ww, wh;
        glfwGetCursorPos(win, &mx, &my);
        glfwGetWindowSize(win, &ww, &wh);
        float nx = (float)(mx / ww), ny = 1.f - (float)(my / wh);
        mouse = PxVec2((nx - graphics.fracX) / graphics.fracW * Config::ViewW,
            (ny - graphics.fracY) / graphics.fracH * Config::ViewH);

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
    bool escPrev = false;
    bool rPrev = false;
};


// obsługa wejścia, celowanie i strzał, zdolności ptaków, symulacja
// ze stałym krokiem, obrażenia, warunki końca gry oraz rysowanie sceny.
class Game {
public:
    // Ustawiane przez Escape w menu lub przycisk QUIT; kończy pętlę główną.
    bool quit = false;

    // Aktualizacja klatki: najpierw wejście, potem fizyka.
    void update(const Input& in, double frameDt) {
        handleInput(in);
        simulate(frameDt);
    }

    // Rysuje scenę warstwami: tło, obiekty fizyczne, efekty, HUD, nakładka końcowa.
    void draw() const {
        graphics.quad(16.f, -0.5f, 20.f, 0.5f, 0.f, GameData::Ground);
        if (state == MENU) { drawMenu(); return; }

        // Współczynnik interpolacji: jaka część kroku fizyki upłynęła od ostatniego kroku.
        const float alpha = (float)(accumulator / Config::Dt);
        for (const Physics::Deco& d : physics.decos)
            graphics.quad(d.x, d.y, d.hx, d.hy, 0.f, d.c);
        drawSling();
        for (const Body* b : physics.bodies)
            drawBody(*b, alpha);
        for (const PxVec2& p : physics.pivots) {
            graphics.circle(p.x, p.y, 0.18f, GameData::Black);
            graphics.circle(p.x, p.y, 0.07f, GameData::White);
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

    // Kolejka ptaków poziomu i indeks następnego do wystrzelenia.
    std::vector<Kind> queue;
    size_t birdIdx = 0;
    // Id ostatnio wystrzelonego ptaka (cel aktywacji zdolności).
    int lastBirdId = 0;
    std::vector<Flash> flashes;

    // Stan celowania: czy ptak jest przeciągany i gdzie znajduje się w procy.
    bool dragging = false;
    PxVec2 dragPos;

    // Czas klatek, który nie został jeszcze zużyty na kroki fizyki.
    double accumulator = 0.0;
    // Czas od startu poziomu (do fazy stabilizacji).
    float levelTime = 0.f;
    // Czas od ostatniego strzału (przeładowanie, warunek przegranej).
    float sinceLaunch = 10.f;
    // Liczniki opóźnień ekranów wygranej i przegranej.
    float winTimer = 0.f;
    float loseTimer = 0.f;

    void clearWorld() {
        physics.clear();
        flashes.clear();
    }

    // Buduje poziom od nowa, zeruje liczniki i przełącza grę w tryb PLAYING.
    void startLevel() {
        clearWorld();
        Levels::build();
        queue = Levels::birdQueue();
        birdIdx = 0;
        lastBirdId = 0;
        dragging = false;
        dragPos = PxVec2(Config::AnchorX, Config::AnchorY);
        levelTime = 1.0f;
        sinceLaunch = 10.f;
        winTimer = 0.f;
        loseTimer = 0.f;
        accumulator = 0.0;
        state = PLAYING;
    }

    void toMenu() {
        clearWorld();
        dragging = false;
        state = MENU;
    }

    // Escape (wyjście lub powrót do menu),
    // R (restart poziomu), kliknięcia przycisków.
    void handleInput(const Input& in) {
        mouse = in.mouse;
        if (state == MENU) {
            if (in.escPressed || (in.clicked && ui.Quit.contains(mouse)))
                quit = true;
            else if (in.clicked && ui.Play.contains(mouse))
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
            if (in.clicked && ui.Retry.contains(mouse)) startLevel();
            else if (in.clicked && ui.MenuRight.contains(mouse)) toMenu();
            break;
        default: break;
        }
    }

    // Czy można teraz celować: ptak czeka w kolejce, proca jest przeładowana,
    // a konstrukcja zakończyła stabilizację.
    bool canAim() const {
        return birdIdx < queue.size() && sinceLaunch > Config::ReloadDelay &&
            levelTime > Config::SettleTime;
    }

    // Celowanie i strzał. Kliknięcie przy procy rozpoczyna przeciąganie,
    // kliknięcie gdzie indziej aktywuje zdolność ostatniego ptaka.
    // Naciąg jest przycinany do MaxPull; puszczenie przycisku strzela,
    // chyba że naciąg jest mniejszy od MinPull (wtedy strzał jest anulowany).
    void handleAiming(const Input& in) {
        const PxVec2 anchor(Config::AnchorX, Config::AnchorY);
        if (in.clicked && !dragging) {
            if (canAim() && (mouse - anchor).magnitude() < Config::GrabRadius)
                dragging = true;
            else
                useAbility();
        }
        if (!dragging) return;

        PxVec2 pull = mouse - anchor;
        float len = pull.magnitude();
        if (len > Config::MaxPull) pull *= Config::MaxPull / len;
        dragPos = anchor + pull;
        // Ptak nie może zostać naciągnięty poniżej poziomu gruntu.
        dragPos.y = std::max(dragPos.y, GameData::Birds[queue[birdIdx]].radius);

        if (!in.lmb) {
            if ((dragPos - anchor).magnitude() < Config::MinPull)
                dragging = false;
            else
                launch();
        }
    }

    // Prędkość początkowa ptaka 
    // Kierunek wyznacza wektor od punktu naciągu do zaczepu procy.
    // Masa liczona jest z gęstości i objętości kuli 
    PxVec2 launchVelocity(Kind k) const {
        float r = GameData::Birds[k].radius;
        float mass = Config::BirdDensity * (4.f / 3.f) * Config::Pi * r * r * r;
        const PxVec2 anchor(Config::AnchorX, Config::AnchorY);
        return (anchor - dragPos) * std::sqrt(Config::SlingStiffness / mass);
    }

    // Tworzy ptaka w punkcie naciągu, nadaje mu prędkość 
    // zapamiętuje jego id (dla zdolności) i rozpoczyna odliczanie przeładowania.
    void launch() {
        Kind k = queue[birdIdx++];
        Body* bird = physics.addBall(dragPos.x, dragPos.y, GameData::Birds[k].radius, Config::BirdDensity, BIRD);
        bird->kind = k;
        PxVec2 v = launchVelocity(k);
        bird->actor->setLinearVelocity(PxVec3(v.x, v.y, 0.f));
        lastBirdId = bird->id;
        sinceLaunch = 0.f;
        dragging = false;
    }

    // Aktywuje zdolność ostatnio wystrzelonego ptaka (jednokrotnie):
    // żółty przyspiesza, niebieski dzieli się na trzy, czarny wybucha.
    void useAbility() {
        Body* b = physics.find(lastBirdId);
        if (!b || b->abilityUsed) return;
        b->abilityUsed = true;
        switch (b->kind) {
        case YELLOW:
            b->actor->setLinearVelocity(b->actor->getLinearVelocity() * Config::YellowBoost);
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

    // Podział niebieskiego ptaka na trzy. Promień każdego ptaka maleje
    // więc jego masa to 1/3 oryginału, a suma pędów jest zachowana.
    // Dwa nowe ptaki dostają przeciwne składowe prędkości prostopadłej do toru,
    // które się znoszą (pęd całkowity pozostaje bez zmian).
    void split(Body& b) {
        PxVec3 p = b.actor->getGlobalPose().p;
        PxVec3 v = b.actor->getLinearVelocity();
        float r = b.radius / std::cbrt(3.f);

        PxShape* shape = nullptr;
        b.actor->getShapes(&shape, 1);
        if (shape)
            shape->setGeometry(PxSphereGeometry(r));
        b.radius = r;
        PxRigidBodyExt::updateMassAndInertia(*b.actor, Config::BirdDensity);

        float vLen = v.magnitude();
        PxVec3 perp = vLen > 0.01f ? PxVec3(-v.y, v.x, 0.f) / vLen : PxVec3(0.f, 1.f, 0.f);
        float dv = vLen * std::tan(Config::SplitAngle);

        for (int side = -1; side <= 1; side += 2) {
            Body* n = physics.addBall(p.x, p.y, r, Config::BirdDensity, BIRD);
            n->kind = b.kind;
            n->abilityUsed = true;
            n->actor->setLinearVelocity(v + perp * (dv * side));
        }
    }

    // Wybuch czarnego ptaka. 
    // cięższe ciała są odrzucane slabiej
    // Impuls wybuchu trafia też do licznika
    // obrażeń, dzięki czemu wybuch może niszczyć bloki i świnie.
    // Sam ptak jest usuwany, a w miejscu wybuchu pojawia się błysk.
    void explode(Body& bomb) {
        PxVec3 p = bomb.actor->getGlobalPose().p;
        for (Body* o : physics.bodies) {
            if (o == &bomb || o->dead) continue;

            PxVec3 d = o->actor->getGlobalPose().p - p;
            d.z = 0.f;
            float dist = d.magnitude();
            if (dist >= Config::BombRadius) continue;

            float J = Config::BombImpulse * (1.f - dist / Config::BombRadius);
            PxVec3 dir = dist > 0.001f ? d / dist : PxVec3(0.f, 1.f, 0.f);
            o->actor->addForce(dir * J, PxForceMode::eIMPULSE);
            o->impulse += J;
        }
        flashes.push_back({ p.x, p.y, 0.f });
        bomb.dead = true;
    }

    // Pętla ze stałym krokiem
    void simulate(double frameDt) {
        if (state == MENU) { accumulator = 0.0; return; }
        accumulator += frameDt;
        while (accumulator >= Config::Dt) {
            physics.step(Config::Dt);
            levelTime += Config::Dt;
            sinceLaunch += Config::Dt;
            applyDamageRules();
            physics.removeDead();
            updateFlashes();
            checkEnd();
            accumulator -= Config::Dt;
        }
    }

    // Model obrażeń: porównuje impuls zebrany w kroku z progami.
    // Świnia ginie powyżej PigDeathImpulse, blok rozpada się powyżej breakImpulse
    // swojego materiału (fragmenty i ciała noShatter po prostu znikają).
    // W fazie stabilizacji obrażenia są wyłączone. Usuwane są też ptaki starsze
    // niż BirdLifetime oraz ciała, które opuściły planszę. Na końcu licznik impulsu
    // jest zerowany; rozpady wykonywane są po pętli, żeby nie modyfikować listy ciał
    // podczas iteracji.
    void applyDamageRules() {
        const bool settling = levelTime < Config::SettleTime;
        std::vector<Fracture> toBreak;

        for (Body* b : physics.bodies) {
            b->age += Config::Dt;

            if (!settling) {
                if (b->type == PIG && b->impulse > Config::PigDeathImpulse) {
                    b->dead = true;
                }
                else if (b->type == BLOCK && b->impulse > GameData::Mats[b->mat].breakImpulse) {
                    b->dead = true;
                    if (!b->fragment && !b->noShatter)
                        toBreak.push_back(physics.makeFracture(*b));
                }
            }

            if (b->type == BIRD && b->age > Config::BirdLifetime)
                b->dead = true;

            PxVec3 p = b->actor->getGlobalPose().p;
            if (p.y < -6.f || p.x > 50.f || p.x < -10.f)
                b->dead = true;

            b->impulse = 0.f;
        }

        for (const Fracture& f : toBreak)
            physics.shatter(f);
    }

    // Postęp animacji błysków
    void updateFlashes() {
        for (Flash& f : flashes) f.t += Config::Dt;
        flashes.erase(std::remove_if(flashes.begin(), flashes.end(),
            [](const Flash& f) { return f.t > Config::FlashTime; }),
            flashes.end());
    }

    int pigsAlive() const {
        int pigs = 0;
        for (const Body* b : physics.bodies)
            if (b->type == PIG && !b->dead) ++pigs;
        return pigs;
    }

    // Czy jakikolwiek ptak jeszcze się porusza 
    // Przegrana jest ogłaszana dopiero, gdy wszystkie ptaki się zatrzymają.
    bool birdInFlight() const {
        for (const Body* b : physics.bodies) {
            if (b->type != BIRD || b->dead) continue;
            if (b->actor->getLinearVelocity().magnitude() > Config::BirdRestSpeed) return true;
            if (b->actor->getAngularVelocity().magnitude() > Config::BirdRestAngular) return true;
        }
        return false;
    }

    // Warunki końca gry.
    // Wygrana: brak żywych świń 
    // Przegrana: kolejka ptaków pusta i żaden ptak się nie rusza 
    // albo upłynął twardy limit LoseHardTimeout od ostatniego strzału
    // (zabezpieczenie przed ptakiem, który nigdy całkiem nie znieruchomieje).
    void checkEnd() {
        if (state != PLAYING) return;
        if (pigsAlive() == 0) {
            winTimer += Config::Dt;
            if (winTimer > Config::WinDelay) state = WON;
            return;
        }
        winTimer = 0.f;
        if (birdIdx >= queue.size()) {
            if (sinceLaunch > Config::LoseHardTimeout) { state = LOST; return; }
            if (!birdInFlight()) {
                loseTimer += Config::Dt;
                if (loseTimer > Config::LoseDelay) state = LOST;
            }
            else loseTimer = 0.f;
        }
        else loseTimer = 0.f;
    }

    // --- Rysowanie ---

    // Ptak obrócony o angle: ciało, oko i źrenica obracane razem z ciałem.
    static void drawBirdRotated(Kind k, float x, float y, float r, float angle) {
        float c = std::cos(angle), s = std::sin(angle);
        graphics.circle(x, y, r, GameData::Birds[k].color);
        float ex = x + r * (0.35f * c - 0.30f * s), ey = y + r * (0.35f * s + 0.30f * c);
        graphics.circle(ex, ey, r * 0.28f, GameData::White);
        graphics.circle(ex + r * 0.05f * c, ey + r * 0.05f * s, r * 0.13f, GameData::Black);
    }

    static void drawBird(Kind k, float x, float y, float r) {
        drawBirdRotated(k, x, y, r, 0.f);
    }

    static void drawPig(float x, float y, float r, float angle) {
        float c = std::cos(angle), s = std::sin(angle);
        graphics.circle(x, y, r, GameData::Pig);
        graphics.circle(x - 0.2f * c, y - 0.2f * s, r * 0.18f, GameData::White);
        graphics.circle(x + 0.2f * c, y + 0.2f * s, r * 0.18f, GameData::White);
    }

    // Kula wahadła z ciemniejszym znacznikiem, który pokazuje jej obrót.
    static void drawBall(const Body& b, float x, float y, float a) {
        Color c = GameData::Mats[b.mat].color;
        graphics.circle(x, y, b.radius, c);
        graphics.circle(x + 0.5f * b.radius * std::cos(a), y + 0.5f * b.radius * std::sin(a),
            b.radius * 0.22f, { c.r * 0.5f, c.g * 0.5f, c.b * 0.5f });
    }

    // Rysuje ciało według typu, w pozie interpolowanej współczynnikiem alpha.
    static void drawBody(const Body& b, float alpha) {
        Pose p = b.pose(alpha);
        switch (b.type) {
        case BLOCK: {
            Color c = GameData::Mats[b.mat].color;
            if (b.fragment) { c.r *= 0.75f; c.g *= 0.75f; c.b *= 0.75f; }
            graphics.quad(p.x, p.y, b.half.x, b.half.y, p.angle, c);
            if (b.armLen > 0.f) {
                float cs = std::cos(p.angle), sn = std::sin(p.angle);
                PxVec2 top(p.x - b.armLen * sn, p.y + b.armLen * cs);
                for (int side = -1; side <= 1; side += 2) {
                    float lx = side * b.half.x;
                    graphics.line(PxVec2(p.x + lx * cs, p.y + lx * sn), top, 0.04f, GameData::SlingBand);
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
        graphics.quad(Config::AnchorX, 1.5f, 0.2f, 1.7f, 0.f, GameData::Sling);
    }

    // Błyski wybuchów: rozszerzające się, zanikające koła.
    void drawExplosions() const {
        for (const Flash& f : flashes) {
            float k = f.t / Config::FlashTime;
            Color c = GameData::Explosion;
            c.a = 0.6f * (1.f - k);
            graphics.circle(f.x, f.y, Config::BombRadius * k, c);
        }
    }

    // Ptak na procy. Podczas przeciągania rysuje cięciwę i przewidywany tor lotu
    // (punkty wg wzoru rzutu ukośnego, zanikające z czasem); gdy proca jest
    // gotowa, rysuje ptaka w spoczynku.
    void drawActiveBird() const {
        if (state != PLAYING || birdIdx >= queue.size()) return;
        Kind k = queue[birdIdx];
        const PxVec2 left(Config::AnchorX - 0.2f, Config::AnchorY);
        const PxVec2 right(Config::AnchorX + 0.2f, Config::AnchorY);

        if (dragging) {
            graphics.line(left, dragPos, 0.06f, GameData::SlingBand);
            graphics.line(right, dragPos, 0.06f, GameData::SlingBand);
            drawBird(k, dragPos.x, dragPos.y, GameData::Birds[k].radius);

            PxVec2 v = launchVelocity(k);
            for (int i = 1; i <= 28; ++i) {
                float t = i * 0.1f;
                float a = 1.f - i / 32.f;
                graphics.circle(dragPos.x + v.x * t,
                    dragPos.y + v.y * t - 0.5f * Config::Gravity * t * t,
                    0.08f, { 1.f, 1.f, 1.f, a });
            }
        }
        else if (sinceLaunch > Config::ReloadDelay) {
            PxVec2 at(Config::AnchorX, Config::AnchorY);
            graphics.line(left, at, 0.06f, GameData::SlingBand);
            graphics.line(right, at, 0.06f, GameData::SlingBand);
            drawBird(k, at.x, at.y, GameData::Birds[k].radius);
        }
    }

    // Pozostałe ptaki jako małe ikony w lewym dolnym rogu.
    void drawQueue() const {
        if (state != PLAYING) return;
        for (size_t i = birdIdx + 1, n = 0; i < queue.size(); ++i, ++n) {
            Kind k = queue[i];
            float r = GameData::Birds[k].radius * Config::QueueScale;
            float x = Config::QueueStartX - (float)n * Config::QueueSpacing;
            if (x < 0.7f) break;
            drawBird(k, x, r, r);
        }
    }

    // Licznik pozostałych ptaków i świń.
    void drawHud() const {
        if (state != PLAYING) return;
        char buf[96];
        int birdsLeft = (int)(queue.size() - birdIdx);
        std::snprintf(buf, sizeof buf, "BIRDS %d  PIGS %d", birdsLeft, pigsAlive());
        text.draw(buf, 0.5f, 16.6f, 0.14f, GameData::Hud);
    }

    void drawMenu() const {
        text.drawCentered("ANGRY BIRDS", 16.f + 0.15f, 13.5f - 0.15f, 0.3f, GameData::Black);
        text.drawCentered("ANGRY BIRDS", 16.f, 13.5f, 0.3f, { 0.9f, 0.25f, 0.15f });
        for (int i = 0; i < 4; ++i)
            drawBird((Kind)i, 10.f + i * 4.f, 11.f, GameData::Birds[i].radius * 1.3f);
        ui.Play.draw(ui.Play.contains(mouse));
        ui.Quit.draw(ui.Quit.contains(mouse));
    }

    // Przyciemnienie sceny, napis wyniku i przyciski RETRY / MENU.
    void drawOverlay() const {
        graphics.quad(16.f, 9.f, 16.f, 9.f, 0.f, { 0.f, 0.f, 0.f, 0.45f });
        auto button = [&](const Button& b) { b.draw(b.contains(mouse)); };
        if (state == WON)
            text.drawCentered("YOU WIN", 16.f, 11.f, 0.3f, { 1.f, 0.95f, 0.3f });
        else if (state == LOST)
            text.drawCentered("GAME OVER", 16.f, 11.f, 0.25f, { 1.f, 0.3f, 0.3f });
        button(ui.Retry);
        button(ui.MenuRight);
    }
};



int main() {
    glfwSetErrorCallback([](int code, const char* msg) {
        std::fprintf(stderr, "GLFW error %d: %s\n", code, msg);
        });
    if (!glfwInit())
        return -1;

   
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    GLFWwindow* window = glfwCreateWindow(Config::WindowW, Config::WindowH,
        "Angry Birds - PhysX + OpenGL", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    graphics.layout(window);
    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    graphics.init();
    text.init();
    if (!physics.init()) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    Game game;
    Input input;
    double prevTime = glfwGetTime();
    while (!glfwWindowShouldClose(window) && !game.quit) {
        glfwPollEvents();
        graphics.layout(window);
        input.poll(window);

        
        double now = glfwGetTime();
        game.update(input, std::min(now - prevTime, 0.1));
        prevTime = now;

        graphics.beginFrame(window);
        game.draw();
        glfwSwapBuffers(window);
    }

    physics.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}