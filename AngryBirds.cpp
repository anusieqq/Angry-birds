// Angry Birds – wersja z komentarzami objaśniającymi strukturę programu, dane i funkcje.
// Komentarze opisują przeznaczenie elementów; nie zmieniają działania gry.

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
// Stałe sterujące grą. Dzięki zebraniu ich w jednym miejscu można zmieniać parametry rozgrywki bez szukania wartości w całym kodzie.
// Wymiary okna i świata, fizyka, proca, czasy oraz promień wybuchu są używane w dalszych modułach.
namespace cfg {
    constexpr int   WindowW = 1280, WindowH = 720; // Początkowa rozdzielczość okna w pikselach.
    constexpr float ViewW = 32.f, ViewH = 18.f; // Wirtualny rozmiar świata; niezależny od rozdzielczości monitora.
    constexpr float Pi = 3.14159265f; // Stała pi używana przy obliczeniach kątów.
    constexpr float Gravity = 9.81f; // Wartość przyspieszenia ziemskiego.
    constexpr float Dt = 1.f / 60.f; // Stały krok fizyki: 60 obliczeń na sekundę.
    constexpr int   NumLevels = 3; // Łączna liczba poziomów w grze.
    const PxVec2    Anchor(5.f, 3.2f); // Punkt zaczepienia procy w świecie gry.
    constexpr float MaxPull = 3.f; // Maksymalna odległość naciągnięcia procy.
    constexpr float LaunchPower = 6.5f; // Bazowa siła nadawana ptakowi podczas wystrzału.
    constexpr float GrabRadius = 1.5f; // Promień obszaru, w którym można chwycić ptaka na procy.
    constexpr float ReloadDelay = 1.5f; // Minimalny czas oczekiwania przed przygotowaniem kolejnego ptaka.
    constexpr float QueueStartX = 3.6f; // Pozycja pierwszego ptaka z kolejki.
    constexpr float QueueSpacing = 0.95f; // Odstęp między ikonami ptaków w kolejce.
    constexpr float SettleTime = 1.5f; // Czas, przez który konstrukcja może się ustabilizować bez naliczania obrażeń.
    constexpr float PigDeathImpulse = 5.f; // Próg impulsu powodujący zniszczenie świni.
    constexpr float PigBirdImpulse = 0.6f; // Próg kontaktu z ptakiem wystarczający do pokonania świni.
    constexpr float BirdLifetime = 12.f; // Maksymalny czas pozostawania ptaka w świecie.
    constexpr float WinDelay = 0.8f; // Opóźnienie przed wyświetleniem informacji o wygranej.
    constexpr float LoseDelay = 7.f; // Czas oczekiwania po ostatnim strzale przed uznaniem porażki.
    constexpr float BombRadius = 6.f; // Zasięg działania wybuchu czarnego ptaka.
    constexpr float FlashTime = 0.4f; // Czas wyświetlania animacji rozbłysku.
}
// Podstawowe typy danych opisujące wygląd i obiekty gry.
// Kolor w formacie RGBA: czerwony, zielony, niebieski i przezroczystość (alpha).
struct Color { float r, g, b, a = 1.f; };
// Nazwane kolory używane przez grafikę, aby nie powtarzać wartości RGB w kodzie.
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
}
// Rodzaj obiektu fizycznego: przeszkoda, świnia lub ptak.
enum Type { BLOCK, PIG, BIRD };
// Materiał bloku wpływa na jego wygląd, masę i odporność na uderzenia.
enum Mat { WOOD, STONE, GLASS };
// Odmiana ptaka. Indeks odpowiada wpisowi w tablicy Birds.
enum Kind { RED, YELLOW, BLUE, BLACK };
// Stany gry, na podstawie których wybierane są obsługa wejścia i ekran.
enum State { MENU, PLAYING, WON, LOST, FINISHED };
// Parametry danego ptaka: promień, gęstość (masa wynika z geometrii i gęstości), mnożnik siły oraz kolor.
struct BirdDef { float radius, density, power; Color color; };
// Dane czterech rodzajów ptaków; kolejność odpowiada wartościom enum Kind.
const BirdDef Birds[] = {
    { 0.50f, 3.0f, 1.00f, { 0.85f, 0.15f, 0.15f } },
    { 0.42f, 2.2f, 1.15f, { 0.97f, 0.85f, 0.10f } },
    { 0.35f, 2.0f, 1.10f, { 0.30f, 0.60f, 0.95f } },
    { 0.62f, 9.0f, 0.85f, { 0.10f, 0.10f, 0.10f } },
};
// Właściwości materiału bloku: gęstość, próg zniszczenia i kolor.
struct MatDef { float density, breakImpulse; Color color; };
// Parametry drewna, kamienia i szkła; kamień ma bardzo wysoki próg zniszczenia.
const MatDef Mats[] = {
    { 1.0f, 9.0f, { 0.65f, 0.45f, 0.25f } },
    { 3.0f, 1e9f, { 0.55f, 0.55f, 0.60f } },
    { 0.6f, 2.5f, { 0.70f, 0.90f, 0.95f } },
};
// Wspólne dane obiektu widocznego w świecie i symulowanego przez PhysX.
struct Body {
    PxRigidDynamic* actor = nullptr; // Wskaźnik na obiekt fizyczny utworzony przez PhysX.
    Type  type = BLOCK; // Określa, czy obiekt jest blokiem, świnią czy ptakiem.
    Mat   mat = WOOD; // Materiał bloku; używany do pobrania jego koloru i odporności.
    Kind  kind = RED; // Rodzaj ptaka, potrzebny do rysowania i uruchamiania umiejętności.
    int   id = 0; // Unikalny identyfikator pozwalający odnaleźć obiekt po wystrzeleniu.
    PxVec2 half{ 0, 0 }; // Połowy szerokości i wysokości prostokątnego bloku.
    float radius = 0; // Promień obiektu kulistego (ptaka lub świni).
    float impulse = 0; // Suma impulsów kontaktowych zebranych w kroku symulacji.
    float birdImpulse = 0; // Część impulsu pochodząca od zderzeń z ptakami.
    float age = 0; // Czas życia obiektu; służy m.in. do usuwania starych ptaków.
    bool  abilityUsed = false; // Zapobiega wielokrotnemu użyciu specjalnej umiejętności ptaka.
    bool  dead = false; // Oznacza obiekt przeznaczony do usunięcia po zakończeniu kroku fizyki.
};
// Krótkotrwały efekt wybuchu: pozycja środka (x, y) i czas od rozpoczęcia animacji.
struct Flash { float x, y, t; };
// Obsługa silnika PhysX: tworzenie sceny, obiektów, symulacja i sprzątanie pamięci.
namespace phys {
    constexpr PxU32 BirdGroup = 1;
    PxDefaultAllocator     allocator; // Alokator pamięci wymagany przez PhysX.
    PxDefaultErrorCallback errorCallback; // Odbiornik komunikatów i błędów PhysX.
    PxFoundation* foundation = nullptr; // Główny obiekt inicjalizacyjny biblioteki PhysX.
    PxPhysics* physics = nullptr; // Interfejs tworzenia materiałów i aktorów fizycznych.
    PxDefaultCpuDispatcher* dispatcher = nullptr; // Przydziela zadania symulacji do wątków CPU.
    PxScene* scene = nullptr; // Scena zawierająca wszystkie obiekty podlegające symulacji.
    PxMaterial* material = nullptr; // Wspólny materiał fizyczny z tarciem i sprężystością.
    PxRigidStatic* ground = nullptr; // Nieruchome podłoże, z którym zderzają się obiekty.
    PxRigidStatic* leftWall = nullptr; // Niewidzialna ściana blokująca obiekty przy lewej krawędzi planszy.
    PxRigidStatic* rightWall = nullptr; // Niewidzialna ściana blokująca obiekty przy prawej krawędzi planszy.
    std::vector<Body*> bodies; // Lista obiektów poziomu; przechowuje ich dane i wskaźniki PhysX.
    int nextId = 1; // Licznik kolejnych unikalnych identyfikatorów obiektów.
    // Ustawia zasady kolizji. Dwa ptaki nie zderzają się ze sobą, pozostałe kontakty są raportowane.
    PxFilterFlags filterShader(PxFilterObjectAttributes, PxFilterData fd0,
        PxFilterObjectAttributes, PxFilterData fd1,
        PxPairFlags& pairFlags, const void*, PxU32) {
        if (fd0.word0 == BirdGroup && fd1.word0 == BirdGroup)
            return PxFilterFlag::eSUPPRESS;
        pairFlags = PxPairFlag::eCONTACT_DEFAULT | PxPairFlag::eNOTIFY_TOUCH_FOUND |
            PxPairFlag::eNOTIFY_TOUCH_PERSISTS | PxPairFlag::eNOTIFY_CONTACT_POINTS |
            PxPairFlag::eDETECT_CCD_CONTACT;
        return PxFilterFlag::eDEFAULT;
    }
    // Odbiera informacje o zderzeniach i zapisuje siłę uderzenia w danych obiektów.
    class ContactCallback : public PxSimulationEventCallback {
    public:
        // Wywoływana przez PhysX po wykryciu kontaktu między obiektami.
        void onContact(const PxContactPairHeader& header, const PxContactPair* pairs, PxU32 count) override {
            for (PxU32 i = 0; i < count; ++i) {
                if (pairs[i].flags & (PxContactPairFlag::eREMOVED_SHAPE_0 | PxContactPairFlag::eREMOVED_SHAPE_1))
                    continue;
                PxContactPairPoint points[16];
                PxU32 n = pairs[i].extractContacts(points, 16);
                float sum = 0.f;
                for (PxU32 j = 0; j < n; ++j) sum += points[j].impulse.magnitude();
                for (int k = 0; k < 2; ++k) {
                    PxActor* a = header.actors[k];
                    PxActor* b = header.actors[1 - k];
                    if (!a || !a->userData) continue;
                    Body* self = static_cast<Body*>(a->userData);
                    self->impulse += sum;
                    if (b && b->userData && static_cast<Body*>(b->userData)->type == BIRD)
                        self->birdImpulse += sum;
                }
            }
        }
        void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
        void onWake(PxActor**, PxU32) override {}
        void onSleep(PxActor**, PxU32) override {}
        void onTrigger(PxTriggerPair*, PxU32) override {}
        void onAdvance(const PxRigidBody* const*, const PxTransform*, PxU32) override {}
    } contactCallback;
    // Pakuje znaki czcionki do atlasu tekstury OpenGL i przygotowuje dane do późniejszego rysowania.
    void init() {
        foundation = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errorCallback);
        physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, PxTolerancesScale(), true, nullptr);
        PxSceneDesc desc(physics->getTolerancesScale());
        desc.gravity = PxVec3(0.f, -cfg::Gravity, 0.f);
        dispatcher = PxDefaultCpuDispatcherCreate(2);
        desc.cpuDispatcher = dispatcher;
        desc.filterShader = filterShader;
        desc.simulationEventCallback = &contactCallback;
        desc.flags |= PxSceneFlag::eENABLE_CCD;
        scene = physics->createScene(desc);
        material = physics->createMaterial(0.6f, 0.5f, 0.15f);
        ground = PxCreateStatic(*physics, PxTransform(PxVec3(16.f, -0.5f, 0.f)),
            PxBoxGeometry(20.f, 0.5f, 2.f), *material);
        scene->addActor(*ground);

        // Ściany są niewidocznymi obiektami statycznymi. Zatrzymują świnie, bloki i ptaki,
        // zanim te wytoczą się poza obszar widoczny na ekranie.
        leftWall = PxCreateStatic(*physics, PxTransform(PxVec3(0.1f, 8.f, 0.f)),
            PxBoxGeometry(0.2f, 8.5f, 2.f), *material);
        rightWall = PxCreateStatic(*physics, PxTransform(PxVec3(cfg::ViewW - 0.1f, 8.f, 0.f)),
            PxBoxGeometry(0.2f, 8.5f, 2.f), *material);
        scene->addActor(*leftWall);
        scene->addActor(*rightWall);
    }
    // Usuwa wszystkie dynamiczne obiekty poziomu; wykorzystywane przy zmianie poziomu.
    void clear() {
        for (Body* b : bodies) { b->actor->release(); delete b; }
        bodies.clear();
    }
    // Zwalnia zasoby PhysX w odwrotnej kolejności do ich tworzenia.
    void shutdown() {
        clear();
        leftWall->release();
        rightWall->release();
        ground->release();
        material->release();
        scene->release();
        dispatcher->release();
        physics->release();
        foundation->release();
    }
    // Wykonuje jeden krok symulacji fizycznej o czasie dt i czeka na wynik.
    void step(float dt) {
        scene->simulate(dt);
        scene->fetchResults(true);
    }
    // Wspólna końcówka tworzenia obiektu: przypisuje ID, blokady osi, tłumienie i dodaje go do sceny.
    Body* finish(Body* b, PxRigidDynamic* actor, float angularDamping) {
        b->actor = actor;
        b->id = nextId++;
        actor->setRigidDynamicLockFlags(PxRigidDynamicLockFlag::eLOCK_LINEAR_Z |
            PxRigidDynamicLockFlag::eLOCK_ANGULAR_X |
            PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y);
        actor->setAngularDamping(angularDamping);
        actor->userData = b;
        scene->addActor(*actor);
        bodies.push_back(b);
        return b;
    }
    // Tworzy prostokątny blok w pozycji (x, y); hx i hy oznaczają połowy jego wymiarów.
    Body* addBox(float x, float y, float hx, float hy, Mat mat) {
        auto* b = new Body;
        b->type = BLOCK; b->mat = mat; b->half = PxVec2(hx, hy);
        return finish(b, PxCreateDynamic(*physics, PxTransform(PxVec3(x, y, 0.f)),
            PxBoxGeometry(hx, hy, 0.5f), *material, Mats[mat].density), 0.3f);
    }
    // Tworzy kulisty obiekt. Dla ptaka ustawia osobną grupę kolizji i ciągłe wykrywanie zderzeń (CCD).
    Body* addBall(float x, float y, float radius, float density, Type type) {
        auto* b = new Body;
        b->type = type; b->radius = radius;
        PxRigidDynamic* actor = PxCreateDynamic(*physics, PxTransform(PxVec3(x, y, 0.f)),
            PxSphereGeometry(radius), *material, density);
        if (type == BIRD) {
            PxShape* shape = nullptr;
            actor->getShapes(&shape, 1);
            if (shape) shape->setSimulationFilterData(PxFilterData(BirdGroup, 0, 0, 0));
            actor->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
        }
        return finish(b, actor, 0.4f);
    }
    // Wyszukuje obiekt po identyfikatorze, np. ostatnio wystrzelonego ptaka.
    Body* find(int id) {
        for (Body* b : bodies) if (b->id == id) return b;
        return nullptr;
    }
    // Fizycznie usuwa obiekty oznaczone jako martwe; zwalnia aktora PhysX i pamięć struktury Body.
    void removeDead() {
        bodies.erase(std::remove_if(bodies.begin(), bodies.end(), [](Body* b) {
            if (!b->dead) return false;
            b->actor->release();
            delete b;
            return true;
            }), bodies.end());
    }
}
// Definicje układów przeszkód i kolejności ptaków dla kolejnych poziomów.
namespace levels {
    // Buduje wieżę z pionowych podpór i poziomych desek. Opcjonalnie umieszcza świnie na co drugim piętrze.
    void buildTower(float cx, int floors, Mat pillar, Mat plank, bool pigEveryOtherFloor) {
        constexpr float halfWidth = 1.5f; // Odległość podpór wieży od jej środka.
        for (int k = 0; k < floors; ++k) {
            float y0 = k * 2.3f;
            phys::addBox(cx - halfWidth, y0 + 0.9f, 0.3f, 0.9f, pillar);
            phys::addBox(cx + halfWidth, y0 + 0.9f, 0.3f, 0.9f, pillar);
            phys::addBox(cx, y0 + 2.05f, 1.9f, 0.25f, plank);
            bool pig = pigEveryOtherFloor ? (k % 2 == 0) : (k == 0);
            if (pig) phys::addBall(cx, y0 + 0.5f, 0.5f, 1.f, PIG);
        }
        float top = floors * 2.3f;
        phys::addBall(cx, top + 0.5f, 0.5f, 1.f, PIG);
    }
    // Tworzy zawartość wskazanego poziomu i zwraca kolejkę ptaków dostępnych graczowi.
    std::vector<Kind> build(int n) {
        switch (n) {
        case 1:
            buildTower(22.f, 2, GLASS, WOOD, false);
            return { RED, RED, YELLOW };
        case 2:
            phys::addBox(16.5f, 0.8f, 0.5f, 0.8f, STONE);
            buildTower(19.5f, 3, WOOD, STONE, false);
            buildTower(25.5f, 2, WOOD, WOOD, false);
            return { RED, YELLOW, BLUE, BLACK };
        default:
            phys::addBox(15.5f, 0.8f, 0.5f, 0.8f, STONE);
            phys::addBox(15.5f, 2.4f, 0.5f, 0.8f, STONE);
            buildTower(18.5f, 2, WOOD, STONE, false);
            buildTower(23.5f, 4, WOOD, STONE, true);
            buildTower(28.5f, 3, WOOD, WOOD, false);
            return { BLUE, YELLOW, BLACK, RED, BLACK, BLUE };
        }
    }
}
// Proste funkcje rysujące 2D oparte na OpenGL; współrzędne odnoszą się do świata gry.
namespace gfx {
    int fbW = cfg::WindowW, fbH = cfg::WindowH; // Aktualny rozmiar bufora obrazu w pikselach.
    void init() {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    // Przygotowuje klatkę: odczytuje rozmiar okna, czyści ekran i ustawia rzutowanie świata gry.
    void beginFrame(GLFWwindow* win) {
        glfwGetFramebufferSize(win, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(color::Sky.r, color::Sky.g, color::Sky.b, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, cfg::ViewW, 0.0, cfg::ViewH, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }
    // Rysuje obrócony prostokąt o środku (x, y), połowach wymiarów hx/hy i podanym kolorze.
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
    // Rysuje koło jako wachlarz trójkątów; większa liczba segmentów daje gładszą krawędź.
    void circle(float x, float y, float radius, Color c) {
        constexpr int segments = 40; // Liczba odcinków przybliżających okrąg.
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
    // Rysuje grubą linię między punktami a i b, wykorzystując obrócony prostokąt.
    void line(PxVec2 a, PxVec2 b, float thickness, Color c) {
        PxVec2 d = b - a;
        quad((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, d.magnitude() * 0.5f, thickness,
            std::atan2(d.y, d.x), c);
    }
}
// Wczytywanie czcionki TrueType i rysowanie napisów przez atlas tekstury stb_truetype.
namespace text {
    constexpr char  FontPath[] = "PressStart2P-Regular.ttf"; // Nazwa pliku czcionki, który musi być dostępny obok programu.
    constexpr int   AtlasSize = 512; // Rozmiar tekstury, w której przechowywane są znaki czcionki.
    constexpr float FontPx = 8.f; // Bazowy rozmiar znaków podczas tworzenia atlasu.
    // Opis zakresu kodów Unicode: pierwszy kod i liczba kolejnych znaków.
    struct Range { int first, count; };
    constexpr Range Ranges[] = { {0x20, 95}, {0xA0, 96}, {0x100, 128} }; // Zakresy Unicode spakowane do atlasu, w tym znaki diakrytyczne.
    constexpr int NumRanges = 3; // Liczba zakresów znaków przechowywanych w atlasie.
    stbtt_packedchar glyphs[NumRanges][128]; // Metryki i współrzędne tekstury dla każdego znaku.
    GLuint texture = 0; // Identyfikator tekstury OpenGL zawierającej atlas znaków.
    bool   ready = false; // Informuje, czy czcionka została poprawnie przygotowana.
    // Zaokrągla współrzędną do najbliższej liczby całkowitej, ograniczając rozmycie pikselowej czcionki.
    float snap(float v) { return std::floor(v + 0.5f); }
    // Odczytuje plik binarny czcionki do wektora bajtów; zwraca false, jeśli pliku nie można odczytać.
    bool readFile(const char* path, std::vector<unsigned char>& out) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return false;
        std::streamsize size = file.tellg();
        if (size <= 0) return false;
        out.resize((size_t)size);
        file.seekg(0, std::ios::beg);
        return (bool)file.read(reinterpret_cast<char*>(out.data()), size);
    }
    // Wczytuje plik czcionki, tworzy atlas znaków i przesyła go do pamięci karty graficznej.
    void init() {
        std::vector<unsigned char> ttf;
        if (!readFile(FontPath, ttf)) {
            std::fprintf(stderr, "Brak pliku czcionki: %s\n", FontPath);
            return;
        }
        std::vector<unsigned char> alpha(AtlasSize * AtlasSize);
        stbtt_pack_context pc;
        if (!stbtt_PackBegin(&pc, alpha.data(), AtlasSize, AtlasSize, 0, 1, nullptr)) return;
        stbtt_PackSetOversampling(&pc, 1, 1);
        for (int i = 0; i < NumRanges; ++i)
            stbtt_PackFontRange(&pc, ttf.data(), 0, STBTT_POINT_SIZE(FontPx),
                Ranges[i].first, Ranges[i].count, glyphs[i]);
        stbtt_PackEnd(&pc);
        std::vector<unsigned char> rgba(AtlasSize * AtlasSize * 4, 255);
        for (int i = 0; i < AtlasSize * AtlasSize; ++i) rgba[i * 4 + 3] = alpha[i];
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, AtlasSize, AtlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        ready = true;
    }
    // Odczytuje jeden znak z tekstu UTF-8 i zwraca jego kod Unicode (obsługuje też polskie znaki).
    int decode(const char*& s) {
        unsigned char c = (unsigned char)*s++;
        if (c < 0x80) return c;
        if ((c >> 5) == 6 && *s)
            return ((c & 0x1F) << 6) | ((unsigned char)*s++ & 0x3F);
        if ((c >> 4) == 14 && s[0] && s[1]) {
            int cp = ((c & 0x0F) << 12) | (((unsigned char)s[0] & 0x3F) << 6) | ((unsigned char)s[1] & 0x3F);
            s += 2;
            return cp;
        }
        return '?';
    }
    // Zwraca dane graficzne znaku z atlasu; dla nieobsługiwanego znaku używa znaku zapytania.
    const stbtt_packedchar* glyph(int cp) {
        for (int i = 0; i < NumRanges; ++i)
            if (cp >= Ranges[i].first && cp < Ranges[i].first + Ranges[i].count)
                return &glyphs[i][cp - Ranges[i].first];
        return cp == '?' ? nullptr : glyph('?');
    }
    // Oblicza skalę znaków w pikselach na podstawie żądanego rozmiaru i szerokości okna.
    int scale(float size) {
        float pixelsPerUnit = (float)gfx::fbW / cfg::ViewW; // Przelicznik jednostek świata na piksele ekranu.
        return std::max(1, (int)std::lround(5.5f * size * pixelsPerUnit / FontPx));
    }
    // Oblicza szerokość napisu w jednostkach świata, aby można go było wyśrodkować.
    float width(const char* s, float size) {
        int sc = scale(size);
        float px = 0.f;
        for (const char* p = s; *p;)
            if (const stbtt_packedchar* g = glyph(decode(p))) px += snap(g->xadvance * sc);
        return px / gfx::fbW * cfg::ViewW;
    }
    // Rysuje napis od wskazanej pozycji, korzystając z tekstury atlasu znaków.
    void draw(const char* s, float x, float y, float size, Color c) {
        if (!ready) return;
        int sc = scale(size);
        float px = std::floor(x / cfg::ViewW * gfx::fbW);
        float py = std::floor((1.f - y / cfg::ViewH) * gfx::fbH);
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glOrtho(0.0, gfx::fbW, gfx::fbH, 0.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();
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
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
    }
    // Rysuje napis wyśrodkowany względem punktu (cx, cy).
    void drawCentered(const char* s, float cx, float cy, float size, Color c) {
        float baselineShift = 3.5f * scale(size) * (cfg::ViewH / (float)gfx::fbH);
        draw(s, cx - width(s, size) * 0.5f, cy - baselineShift, size, c);
    }
}
// Elementy interfejsu użytkownika, przede wszystkim przyciski menu i ekranów końcowych.
// Przycisk opisany środkiem, wymiarami i etykietą.
struct Button {
    float x, y, w, h;
    const char* label;
    // Sprawdza, czy pozycja myszy m znajduje się wewnątrz prostokąta przycisku.
    bool contains(PxVec2 m) const {
        return std::fabs(m.x - x) < w * 0.5f && std::fabs(m.y - y) < h * 0.5f;
    }
    // Rysuje przycisk; parametr hover informuje, czy kursor znajduje się nad nim.
    void draw(bool hover) const {
        float k = hover ? 1.15f : 1.f; // Powiększenie/jasność przycisku po najechaniu kursorem.
        gfx::quad(x + 0.12f, y - 0.12f, w * 0.5f, h * 0.5f, 0.f, { 0.f, 0.f, 0.f, 0.35f });
        gfx::quad(x, y, w * 0.5f, h * 0.5f, 0.f, { 0.15f, 0.10f, 0.05f });
        gfx::quad(x, y, w * 0.5f - 0.1f, h * 0.5f - 0.1f, 0.f,
            { std::min(1.f, 0.90f * k), std::min(1.f, 0.55f * k), std::min(1.f, 0.10f * k) });
        text::drawCentered(label, x, y, 0.16f, color::White);
    }
};
namespace ui {
    // Stałe definicje przycisków: pozycja środka, szerokość, wysokość i napis.
    const Button Play{ 16.f,   8.f,  6.f, 1.8f, "PLAY" };
    const Button Quit{ 16.f,   5.5f, 6.f, 1.8f, "QUIT" };
    const Button Next{ 16.f,   6.5f, 6.f, 1.8f, "NEXT" };
    const Button Retry{ 12.8f,  6.5f, 6.f, 1.8f, "RETRY" };
    const Button MenuLeft{ 19.2f,  6.5f, 6.f, 1.8f, "MENU" };
    const Button MenuCenter{ 16.f,   6.5f, 6.f, 1.8f, "MENU" };
}
// Odczyt klawiatury i myszy oraz przeliczenie pozycji kursora na współrzędne świata gry.
struct Input {
    PxVec2 mouse; // Pozycja kursora przeliczona na współrzędne świata gry.
    bool lmb = false; // Czy lewy przycisk myszy jest aktualnie wciśnięty.
    bool clicked = false; // True tylko w klatce, w której przycisk myszy został właśnie wciśnięty.
    bool escPressed = false, rPressed = false; // Jednorazowe zdarzenia naciśnięcia Escape i R.
    // Pobiera aktualny stan myszy i klawiszy. Wykrywa też pojedyncze naciśnięcie, a nie tylko przytrzymanie.
    void poll(GLFWwindow* win) {
        double mx, my; // Pozycja kursora w pikselach okna.
        int ww, wh; // Aktualna szerokość i wysokość okna.
        glfwGetCursorPos(win, &mx, &my);
        glfwGetWindowSize(win, &ww, &wh);
        mouse = PxVec2((float)(mx / ww) * cfg::ViewW, (1.f - (float)(my / wh)) * cfg::ViewH);
        // Odczyt stanów przycisków i klawiszy; porównanie z poprzednią klatką wykrywa nowe naciśnięcie.
        bool l = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool e = glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        bool r = glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS;
        clicked = l && !lmb;
        escPressed = e && !escPrev;
        rPressed = r && !rPrev;
        lmb = l; escPrev = e; rPrev = r;
    }
private:
    bool escPrev = false, rPrev = false; // Poprzedni stan klawiszy, potrzebny do wykrycia nowego naciśnięcia.
};
// Główna klasa łącząca sterowanie, fizykę, zasady rozgrywki i wyświetlanie.
class Game {
public:
    bool quit = false; // Ustawienie true kończy główną pętlę programu.
    // Aktualizuje grę raz na klatkę: wejście gracza, symulację fizyki i sprawdzenie końca poziomu.
    void update(const Input& in, double frameDt) {
        mouse = in.mouse;
        handleInput(in);
        simulate(frameDt);
        checkEnd(frameDt);
    }
    // Rysuje całą aktualną scenę w odpowiedniej kolejności: tło, obiekty, efekty i interfejs.
    void draw() const {
        gfx::quad(16.f, -0.5f, 20.f, 0.5f, 0.f, color::Ground);
        if (state == MENU) { drawMenu(); return; }
        drawSling();
        for (const Body* b : phys::bodies) drawBody(*b);
        drawExplosions();
        drawActiveBird();
        drawQueue();
        drawHud();
        if (state != PLAYING) drawOverlay();
    }
private:
    State state = MENU; // Aktualny stan rozgrywki.
    int   currentLevel = 1; // Numer aktualnie rozgrywanego poziomu.
    PxVec2 mouse; // Pozycja myszy wykorzystywana przez logikę klasy Game.
    std::vector<Kind> queue; // Kolejka ptaków, które gracz może wystrzelić na tym poziomie.
    size_t birdIdx = 0; // Indeks następnego ptaka w kolejce.
    int    lastBirdId = 0; // ID ostatnio wystrzelonego ptaka, potrzebne do aktywacji jego mocy.
    std::vector<Flash> flashes; // Aktywne animacje wybuchów.
    bool   dragging = false; // Czy gracz aktualnie przeciąga ptaka na procy.
    PxVec2 dragPos = cfg::Anchor; // Aktualna pozycja przeciąganego ptaka.
    double accumulator = 0.0; // Zbiera czas klatek, aby fizyka działała w stałych krokach czasowych.
    float  levelTime = 0.f, sinceLaunch = 10.f, winTimer = 0.f; // Czas poziomu, czas od strzału i opóźnienie potwierdzenia wygranej.
    // Czyści obiekty fizyczne i efekty, np. przed rozpoczęciem kolejnego poziomu.
    void clearWorld() {
        phys::clear();
        flashes.clear();
    }
    // Resetuje stan gry i buduje poziom n wraz z nową kolejką ptaków.
    void startLevel(int n) {
        clearWorld();
        currentLevel = n;
        queue = levels::build(n);
        birdIdx = 0; lastBirdId = 0;
        dragging = false;
        levelTime = 0.f; sinceLaunch = 10.f; winTimer = 0.f;
        state = PLAYING;
    }
    // Wraca do menu głównego i usuwa obiekty bieżącego poziomu.
    void toMenu() {
        clearWorld();
        dragging = false;
        state = MENU;
    }
    // Reaguje na wejście zależnie od stanu gry: menu, rozgrywki, wygranej lub przegranej.
    void handleInput(const Input& in) {
        if (state == MENU) {
            if (in.escPressed || (in.clicked && ui::Quit.contains(mouse))) quit = true;
            else if (in.clicked && ui::Play.contains(mouse)) startLevel(1);
            return;
        }
        if (in.escPressed) { toMenu(); return; }
        switch (state) {
        case PLAYING:
            if (in.rPressed) startLevel(currentLevel);
            else handleAiming(in);
            break;
        case WON:
            if (in.clicked && ui::Next.contains(mouse)) startLevel(currentLevel + 1);
            break;
        case LOST:
            if (in.clicked && ui::Retry.contains(mouse)) startLevel(currentLevel);
            else if (in.clicked && ui::MenuLeft.contains(mouse)) toMenu();
            break;
        case FINISHED:
            if (in.clicked && ui::MenuCenter.contains(mouse)) toMenu();
            break;
        default: break;
        }
    }
    // Sprawdza, czy jest jeszcze ptak do wystrzelenia i czy minął czas przeładowania procy.
    bool canAim() const {
        return birdIdx < queue.size() && sinceLaunch > cfg::ReloadDelay;
    }
    // Obsługuje chwytanie ptaka, ograniczenie naciągu procy i zwolnienie myszy powodujące strzał.
    void handleAiming(const Input& in) {
        if (in.clicked && !dragging) {
            if (canAim() && (mouse - cfg::Anchor).magnitude() < cfg::GrabRadius) dragging = true;
            else useAbility();
        }
        if (!dragging) return;
        PxVec2 pull = mouse - cfg::Anchor;
        float len = pull.magnitude(); // Długość wektora naciągnięcia procy.
        if (len > cfg::MaxPull) pull *= cfg::MaxPull / len;
        dragPos = cfg::Anchor + pull;
        if (!in.lmb) launch();
    }
    // Wyznacza prędkość startową ptaka z kierunku naciągnięcia procy i mocy danego typu.
    PxVec2 launchVelocity(Kind k) const {
        return (cfg::Anchor - dragPos) * (cfg::LaunchPower * Birds[k].power);
    }
    // Tworzy ptaka na procy, nadaje mu prędkość i zapamiętuje jego ID.
    void launch() {
        Kind k = queue[birdIdx++];
        const BirdDef& def = Birds[k];
        Body* bird = phys::addBall(dragPos.x, dragPos.y, def.radius, def.density, BIRD);
        bird->kind = k;
        PxVec2 v = launchVelocity(k);
        bird->actor->setLinearVelocity(PxVec3(v.x, v.y, 0.f));
        lastBirdId = bird->id;
        sinceLaunch = 0.f;
        dragging = false;
    }
    // Uruchamia specjalną zdolność ostatnio wystrzelonego ptaka, maksymalnie jeden raz.
    void useAbility() {
        Body* b = phys::find(lastBirdId);
        if (!b || b->abilityUsed) return;
        b->abilityUsed = true;
        switch (b->kind) {
        case YELLOW: b->actor->setLinearVelocity(b->actor->getLinearVelocity() * 2.f); break;
        case BLUE:   split(*b); break;
        case BLACK:  explode(*b); break;
        default: break;
        }
    }
    // Umiejętność niebieskiego ptaka: tworzy dwie dodatkowe kule lecące pod różnymi kątami.
    void split(const Body& b) {
        constexpr float angle = 0.3f, radius = 0.3f, density = 2.f;
        PxVec3 p = b.actor->getGlobalPose().p; // Aktualna pozycja ptaka w świecie fizyki.
        PxVec3 v = b.actor->getLinearVelocity(); // Aktualna prędkość ptaka; zachowywana przy rozdzieleniu.
        for (int side = -1; side <= 1; side += 2) {
            float c = std::cos(angle * side), s = std::sin(angle * side);
            Body* n = phys::addBall(p.x, p.y, radius, density, BIRD);
            n->kind = BLUE;
            n->abilityUsed = true;
            n->actor->setLinearVelocity(PxVec3(v.x * c - v.y * s, v.x * s + v.y * c, 0.f));
        }
    }
    // Umiejętność czarnego ptaka: odpycha pobliskie obiekty, eliminuje świnie i uszkadza bloki.
    void explode(Body& bomb) {
        constexpr float push = 14.f, blockDamage = 16.f;
        PxVec3 p = bomb.actor->getGlobalPose().p; // Środek wybuchu.
        for (Body* o : phys::bodies) {
            if (o == &bomb) continue;
            // Wektor od środka wybuchu do obiektu; jego długość określa odległość.
            PxVec3 d = o->actor->getGlobalPose().p - p;
            d.z = 0.f;
            float dist = d.magnitude();
            if (dist >= cfg::BombRadius) continue;
            float falloff = 1.f - dist / cfg::BombRadius; // Siła wybuchu maleje wraz z odległością od środka.
            PxVec3 dir = dist > 0.001f ? d / dist : PxVec3(0.f, 1.f, 0.f);
            o->actor->addForce(dir * (push * falloff), PxForceMode::eVELOCITY_CHANGE);
            if (o->type == PIG) o->dead = true;
            else o->impulse += blockDamage * falloff;
        }
        flashes.push_back({ p.x, p.y, 0.f });
        bomb.dead = true;
    }
    // Przelicza fizykę w stałych odstępach cfg::Dt niezależnie od chwilowej długości klatki.
    void simulate(double frameDt) {
        if (state == MENU) { accumulator = 0.0; return; }
        accumulator += frameDt;
        while (accumulator >= cfg::Dt) {
            phys::step(cfg::Dt);
            levelTime += cfg::Dt;
            sinceLaunch += cfg::Dt;
            applyDamageRules();
            phys::removeDead();
            updateFlashes();
            accumulator -= cfg::Dt;
        }
    }
    // Na podstawie impulsów zderzeń decyduje, czy świnie, bloki lub stare ptaki należy zniszczyć.
    void applyDamageRules() {
        for (Body* b : phys::bodies) {
            b->age += cfg::Dt;
            if (levelTime < cfg::SettleTime) { b->impulse = 0.f; b->birdImpulse = 0.f; }
            if (b->type == PIG && (b->impulse > cfg::PigDeathImpulse || b->birdImpulse > cfg::PigBirdImpulse))
                b->dead = true;
            if (b->type == BLOCK && b->impulse > Mats[b->mat].breakImpulse)
                b->dead = true;
            if (b->type == BIRD && b->age > cfg::BirdLifetime)
                b->dead = true;
            PxVec3 p = b->actor->getGlobalPose().p;
            if (p.y < -6.f || p.x > 50.f || p.x < -10.f) b->dead = true;
            b->impulse = 0.f;
            b->birdImpulse = 0.f;
        }
    }
    // Zwiększa czas animacji wybuchów i usuwa efekty, które już się zakończyły.
    void updateFlashes() {
        for (Flash& f : flashes) f.t += cfg::Dt;
        flashes.erase(std::remove_if(flashes.begin(), flashes.end(),
            [](const Flash& f) { return f.t > cfg::FlashTime; }), flashes.end());
    }
    // Sprawdza warunki zwycięstwa (brak świń) i porażki (brak ptaków w kolejce).
    void checkEnd(double frameDt) {
        if (state != PLAYING) return;
        int pigs = 0; // Liczba żywych świń pozostałych na poziomie.
        for (const Body* b : phys::bodies) if (b->type == PIG && !b->dead) ++pigs;
        if (pigs == 0) {
            winTimer += (float)frameDt;
            if (winTimer > cfg::WinDelay)
                state = (currentLevel >= cfg::NumLevels) ? FINISHED : WON;
        }
        else if (birdIdx >= queue.size() && sinceLaunch > cfg::LoseDelay) {
            state = LOST;
        }
    }
    // Zamienia obrót kwaternionowy PhysX na kąt wokół osi Z potrzebny do rysowania 2D.
    static float angleZ(const PxQuat& q) { return 2.f * std::atan2(q.z, q.w); }
    // Rysuje ptaka z okiem; obrót pozwala dopasować wygląd do orientacji obiektu fizycznego.
    static void drawBirdRotated(Kind k, float x, float y, float r, float angle) {
        float c = std::cos(angle), s = std::sin(angle);
        gfx::circle(x, y, r, Birds[k].color);
        float ex = x + r * (0.35f * c - 0.30f * s), ey = y + r * (0.35f * s + 0.30f * c);
        gfx::circle(ex, ey, r * 0.28f, color::White);
        gfx::circle(ex + r * 0.05f * c, ey + r * 0.05f * s, r * 0.13f, color::Black);
    }
    // Wygodna wersja rysowania ptaka bez obrotu.
    static void drawBird(Kind k, float x, float y, float r) {
        drawBirdRotated(k, x, y, r, 0.f);
    }
    // Rysuje świnię i jej oczy z uwzględnieniem obrotu obiektu.
    static void drawPig(float x, float y, float r, float angle) {
        float c = std::cos(angle), s = std::sin(angle);
        gfx::circle(x, y, r, color::Pig);
        gfx::circle(x - 0.2f * c, y - 0.2f * s, r * 0.18f, color::White);
        gfx::circle(x + 0.2f * c, y + 0.2f * s, r * 0.18f, color::White);
    }
    // Dobiera sposób rysowania na podstawie rodzaju obiektu i pobiera jego aktualną pozycję z PhysX.
    static void drawBody(const Body& b) {
        PxTransform t = b.actor->getGlobalPose();
        float a = angleZ(t.q);
        switch (b.type) {
        case BLOCK: gfx::quad(t.p.x, t.p.y, b.half.x, b.half.y, a, Mats[b.mat].color); break;
        case PIG:   drawPig(t.p.x, t.p.y, b.radius, a); break;
        case BIRD:  drawBirdRotated(b.kind, t.p.x, t.p.y, b.radius, a); break;
        }
    }
    // Rysuje pionową część procy.
    static void drawSling() {
        gfx::quad(cfg::Anchor.x, 1.5f, 0.2f, 1.7f, 0.f, color::Sling);
    }
    // Rysuje rozszerzające się i zanikające okręgi symbolizujące wybuchy.
    void drawExplosions() const {
        for (const Flash& f : flashes) {
            float k = f.t / cfg::FlashTime; // Postęp animacji od 0 do 1.
            Color c = color::Explosion;
            c.a = 0.6f * (1.f - k);
            gfx::circle(f.x, f.y, cfg::BombRadius * k, c);
        }
    }
    // Rysuje ptaka gotowego do strzału lub przeciąganego oraz przewidywaną trajektorię lotu.
    void drawActiveBird() const {
        if (state != PLAYING || birdIdx >= queue.size()) return;
        Kind k = queue[birdIdx];
        if (dragging) {
            const PxVec2 left(cfg::Anchor.x - 0.2f, cfg::Anchor.y);
            const PxVec2 right(cfg::Anchor.x + 0.2f, cfg::Anchor.y);
            gfx::line(left, dragPos, 0.06f, color::SlingBand);
            gfx::line(right, dragPos, 0.06f, color::SlingBand);
            drawBird(k, dragPos.x, dragPos.y, Birds[k].radius);
            PxVec2 v = launchVelocity(k);
            for (int i = 1; i <= 14; ++i) {
                float t = i * 0.12f;
                gfx::circle(dragPos.x + v.x * t,
                    dragPos.y + v.y * t - 0.5f * cfg::Gravity * t * t,
                    0.08f, color::White);
            }
        }
        else if (sinceLaunch > cfg::ReloadDelay) {
            drawBird(k, cfg::Anchor.x, cfg::Anchor.y, Birds[k].radius);
        }
    }
    // Pokazuje pozostałe ptaki w kolejce obok procy.
    void drawQueue() const {
        if (state != PLAYING) return;
        for (size_t i = birdIdx + 1, n = 0; i < queue.size(); ++i, ++n) {
            Kind k = queue[i];
            drawBird(k, cfg::QueueStartX - (float)n * cfg::QueueSpacing, Birds[k].radius, Birds[k].radius);
        }
    }
    // Wyświetla informacje HUD, obecnie numer poziomu.
    void drawHud() const {
        if (state != PLAYING) return;
        char buf[32]; // Bufor na tekstowy zapis numeru poziomu.
        std::snprintf(buf, sizeof buf, "LEVEL %d", currentLevel);
        text::draw(buf, 0.5f, 16.6f, 0.14f, color::Hud);
    }
    // Rysuje ekran startowy, tytuł gry, ptaki oraz przyciski Play i Quit.
    void drawMenu() const {
        text::drawCentered("ANGRY BIRDS", 16.f + 0.15f, 13.5f - 0.15f, 0.3f, color::Black);
        text::drawCentered("ANGRY BIRDS", 16.f, 13.5f, 0.3f, { 0.9f, 0.25f, 0.15f });
        for (int i = 0; i < 4; ++i)
            drawBird((Kind)i, 10.f + i * 4.f, 11.f, Birds[i].radius * 1.3f);
        ui::Play.draw(ui::Play.contains(mouse));
        ui::Quit.draw(ui::Quit.contains(mouse));
    }
    // Rysuje przyciemnioną nakładkę i komunikat/przyciski po wygranej lub przegranej.
    void drawOverlay() const {
        gfx::quad(16.f, 9.f, 16.f, 9.f, 0.f, { 0.f, 0.f, 0.f, 0.45f });
        auto button = [&](const Button& b) { b.draw(b.contains(mouse)); }; // Wspólna obsługa podświetlania przycisków nakładki.
        switch (state) {
        case WON:
            text::drawCentered("LEVEL CLEAR", 16.f, 11.f, 0.25f, { 1.f, 0.95f, 0.3f });
            button(ui::Next);
            break;
        case LOST:
            text::drawCentered("GAME OVER", 16.f, 11.f, 0.25f, { 1.f, 0.3f, 0.3f });
            button(ui::Retry);
            button(ui::MenuLeft);
            break;
        case FINISHED:
            text::drawCentered("YOU WIN", 16.f, 11.f, 0.3f, { 1.f, 0.95f, 0.3f });
            button(ui::MenuCenter);
            break;
        default: break;
        }
    }
};
// Punkt wejścia programu: inicjalizacja bibliotek, główna pętla gry i zwalnianie zasobów.
int main() {
    // Uruchomienie GLFW i utworzenie okna aplikacji.
    if (!glfwInit()) return -1; // Bez GLFW nie można utworzyć okna ani obsługiwać wejścia.
    GLFWwindow* win = glfwCreateWindow(cfg::WindowW, cfg::WindowH,
        "Angry Birds - PhysX + OpenGL", nullptr, nullptr);
    if (!win) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) return -1; // Ładuje funkcje OpenGL dostępne w sterowniku.
    gfx::init(); // Przygotowuje ustawienia renderowania.
    text::init(); // Wczytuje czcionkę i tworzy atlas znaków.
    phys::init(); // Inicjalizuje silnik fizyczny i podłoże.
    Game game;
    Input input;
    double prev = glfwGetTime(); // Czas poprzedniej klatki, potrzebny do obliczenia delta time.
    // Główna pętla: odczyt wejścia, aktualizacja logiki, rysowanie i prezentacja klatki.
    while (!glfwWindowShouldClose(win) && !game.quit) {
        glfwPollEvents();
        input.poll(win);
        double now = glfwGetTime(); // Aktualny czas; różnica względem prev to czas trwania klatki.
        game.update(input, std::min(now - prev, 0.1)); // Ogranicza duży skok czasu po przycięciu aplikacji.
        prev = now;
        gfx::beginFrame(win);
        game.draw();
        glfwSwapBuffers(win);
    }
    phys::shutdown(); // Zwalnia wszystkie zasoby silnika fizycznego przed zamknięciem programu.
    glfwTerminate();
    return 0;
}
