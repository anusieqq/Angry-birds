# Przewodnik po kodzie projektu Angry Birds

Ten plik jest indeksem po symbolach z `AngryBirds.cpp`: opisuje stałe, typy, klasy, pola, funkcje oraz najważniejsze zmienne lokalne. Przy każdym elemencie podano jego rolę, sposób działania i miejsce użycia. Opis dotyczy bieżącej wersji pliku źródłowego; jeśli kod zostanie później zmieniony, przewodnik również może wymagać aktualizacji.

## Jak czytać ten przewodnik

- **Pole** to zmienna przechowywana w obiekcie klasy lub struktury. Zachowuje wartość pomiędzy wywołaniami funkcji.
- **Zmienna lokalna** istnieje tylko podczas wykonywania danej funkcji i przechowuje jej tymczasowe obliczenia.
- **Funkcja/metoda** wykonuje operację. Metoda należy do klasy albo struktury.
- **`static`** przy metodzie oznacza, że można ją wywołać bez obiektu tej klasy. Przy stałej albo danych oznacza wspólną wartość klasy.
- **Wskaźnik** (np. `PxRigidDynamic*`) wskazuje na obiekt zarządzany przez PhysX. Wektor `bodies` przechowuje wskaźniki do ciał gry i odpowiada za ich rejestrację oraz usuwanie.

Nazwy PhysX, GLFW, OpenGL i `stb_truetype` są dostarczane przez użyte biblioteki. Tutaj opisuję je tylko tam, gdzie pomagają zrozumieć działanie własnego kodu.

## 1. Plik źródłowy i biblioteki

Na początku `AngryBirds.cpp` znajdują się dołączone nagłówki:

- `glad/gl.h` — deklaracje funkcji OpenGL ładowanych przez GLAD.
- `GLFW/glfw3.h` — okno, zdarzenia, mysz i klawiatura.
- `PxPhysicsAPI.h` — API PhysX.
- `stb_truetype.h` — pakowanie glifów czcionki do atlasu.
- `<algorithm>` — m.in. `std::min`, `std::max`, `std::remove_if`.
- `<cmath>` — funkcje matematyczne, np. `sqrt`, `sin`, `cos`, `atan2`, `cbrt`.
- `<cstdio>` — `std::fprintf` i `std::snprintf`.
- `<fstream>` — odczyt pliku czcionki.
- `<vector>` — dynamiczne listy ciał, dekoracji, efektów i ptaków.

`using namespace physx;` pozwala zapisywać nazwy typów PhysX bez prefiksu `physx::`.

## 2. Stałe gry: `Config`

`Config` zbiera parametry w jednym miejscu. Są to stałe, więc kod może z nich korzystać bez tworzenia instancji `Config`. Dzięki temu np. symulacja, wejście i rysowanie używają tych samych wymiarów świata.

| Stała | Znaczenie i użycie |
|---|---|
| `WindowW`, `WindowH` | Domyślny rozmiar okna w pikselach; wartości początkowe pól viewportu w `Graphics`. |
| `ViewW`, `ViewH` | Logiczny rozmiar świata `32 × 18`; używany przez projekcję grafiki, viewport i mapowanie myszy. |
| `Pi` | Przybliżenie liczby π do obliczeń kątów w grafice, geometrii i fizyce. |
| `Gravity` | Wartość grawitacji w dół; używana przy konfigurowaniu sceny PhysX. |
| `Dt` | Stały krok symulacji `1/60 s`; używany przez `Game::simulate()`. |
| `AnchorX`, `AnchorY` | Punkt zaczepienia procy; używany przy celowaniu, tworzeniu ptaka i rysowaniu procy. |
| `MaxPull`, `MinPull` | Górne ograniczenie naciągu i minimalny naciąg potrzebny do strzału; używane w `handleAiming()`. |
| `SlingStiffness` | Współczynnik `k` uproszczonego modelu sprężyny; używany w `launchVelocity()`. |
| `GrabRadius` | Odległość od zaczepu, w której kliknięcie rozpoczyna przeciąganie ptaka. |
| `ReloadDelay` | Czas po strzale, po którym proca może załadować następnego ptaka. |
| `QueueStartX`, `QueueSpacing`, `QueueScale` | Położenie, odstęp i rozmiar ikon oczekujących ptaków. |
| `AngularDamping` | Tłumienie obrotu nadawane każdemu dynamicznemu ciału w `Physics::finish()`. |
| `BirdDensity`, `PigDensity` | Gęstości używane przy tworzeniu ptaków, świń oraz obliczaniu masy ptaka do strzału. |
| `SettleTime` | Próg czasu używany przez `canAim()` i `applyDamageRules()`. `startLevel()` ustawia `levelTime` od razu na tę wartość, więc oczekiwanie jest pominięte. |
| `PigDeathImpulse` | Próg impulsu, po przekroczeniu którego świnia jest oznaczana do usunięcia. |
| `BirdLifetime` | Maksymalny wiek ptaka przed oznaczeniem go do usunięcia. |
| `WinDelay`, `LoseDelay` | Opóźnienia przed pokazaniem odpowiednio wygranej i przegranej. |
| `BirdRestSpeed`, `BirdRestAngular` | Progi prędkości liniowej i kątowej używane przy ocenie, czy ptaki nadal są w ruchu. |
| `LoseHardTimeout` | Maksymalny czas czekania po ostatnim strzale; zdefiniowany jako `BirdLifetime + 2`. |
| `BombRadius`, `BombImpulse` | Zasięg wybuchu i impuls w jego środku; używane przez `explode()`. |
| `FlashTime` | Czas widoczności efektu wybuchu. |
| `YellowBoost` | Mnożnik prędkości żółtego ptaka. |
| `SplitAngle` | Kąt używany do wyznaczenia bocznej składowej prędkości ptaków BLUE po podziale. |
| `SolverPos`, `SolverVel` | Liczby iteracji solvera PhysX ustawiane na ciałach w `Physics::finish()`. |

## 3. Typy podstawowe, wyliczenia i katalog danych

### 3.1. Struktury z danymi opisowymi

#### `Color`

Przechowuje kanały koloru `r`, `g`, `b` i przezroczystość `a`. Używana jest przez renderer, materiały, ptaki, dekoracje, efekty i interfejs. Domyślne `a = 1` oznacza pełną nieprzezroczystość.

#### `BirdDef`

- `radius` — promień kuli ptaka; trafia do geometrii PhysX, wyliczania masy i rysowania.
- `color` — podstawowy kolor ptaka; używany przy jego rysowaniu.

Tablica `GameData::Birds[4]` przechowuje promienie i kolory ptaków w kolejności `RED`, `YELLOW`, `BLUE`, `BLACK`.

#### `MatDef`

- `density` — gęstość przekazywana do PhysX przy tworzeniu ciał.
- `breakImpulse` — próg impulsu dla rozpadu bloku; `GameData::Unbreakable` oznacza, że reguła gry praktycznie nie pozwoli go przekroczyć.
- `friction` — bazowa wartość tarcia użyta do utworzenia materiału PhysX.
- `restitution` — sprężystość zderzenia przekazana do PhysX.
- `color` — kolor materiału na ekranie.

Tablica `GameData::Mats[NumMats]` zawiera wpisy dla drewna, kamienia, szkła, metalu i aluminium. Jej indeks jest wartością typu `Mat`.

### 3.2. Wyliczenia (`enum`)

- **`Type`** opisuje funkcję fizyczną i wizualną ciała: `BLOCK` — blok konstrukcji, `PIG` — świnia, `BIRD` — ptak, `BALL` — kula wahadła.
- **`Mat`** wybiera materiał: `WOOD`, `STONE`, `GLASS`, `METAL`, `ALU`. `NumMats` jest liczbą materiałów i rozmiarem tablicy materiałów.
- **`Kind`** określa gatunek/rodzaj ptaka: `RED`, `YELLOW`, `BLUE`, `BLACK`; decyduje o kolorze i umiejętności.
- **`State`** określa ekran gry: `MENU`, `PLAYING`, `WON`, `LOST`.

### 3.3. `GameData`

To katalog wspólnych danych statycznych. Nie tworzy obiektów fizycznych; udostępnia kolory, definicje ptaków i materiałów oraz deklaruje typy danych obiektów.

- Kolory `White`, `Black`, `Sky`, `Ground`, `Sling`, `SlingBand`, `Pig`, `Explosion`, `Hud`, `Frame` są używane w rendererze i elementach interfejsu.
- `Unbreakable` (`1e30`) to bardzo wysoki próg dla materiałów, które w logice gry nie pękają.
- `Birds` i `Mats` to opisane wyżej tablice definicji.
- Zagnieżdżone deklaracje `Pose`, `Body`, `Flash`, `Fracture` porządkują typy powiązane z obiektami gry; ich pełne definicje znajdują się dalej w pliku.

### 3.4. Funkcja `angleZ()` i `Pose`

`angleZ(const PxQuat& q)` wylicza kąt obrotu wokół Z z kwaternionu PhysX (`2 * atan2(q.z, q.w)`). Jest używana przy zapisie pozycji do interpolacji i przy tworzeniu migawki pękającego bloku.

`GameData::Pose` zawiera `x`, `y`, `angle`. To tylko uproszczona poza potrzebna rendererowi; nie zastępuje transformacji PhysX.

### 3.5. `Body` — dane jednego ciała gry

Każde dynamiczne ciało PhysX ma odpowiadający mu obiekt `Body`.

| Pole | Co przechowuje i gdzie jest używane |
|---|---|
| `actor` | Wskaźnik do `PxRigidDynamic`; funkcje fizyki pobierają z niego pozycję, prędkość i kształty. |
| `type` | Rodzaj obiektu; używany w regułach obrażeń, końca gry i w `drawBody()`. |
| `mat` | Materiał bloku/kuli; wybiera gęstość, kolor i próg rozpadu. |
| `kind` | Rodzaj ptaka; wybiera wygląd i zdolność. |
| `id` | Unikalny numer nadany w `Physics::finish()`; `Game` zapisuje go dla ostatniego wystrzelonego ptaka. |
| `half` | Połówki szerokości i wysokości prostokątnego kształtu; używane do rysowania i podziału bloku. |
| `radius` | Promień kuli; używany przez PhysX, renderer i zdolność BLUE. |
| `impulse` | Suma impulsów z kontaktów w bieżącym kroku; sprawdzana w `applyDamageRules()` i zerowana po sprawdzeniu. |
| `age` | Wiek ciała w sekundach; dla ptaków porównywany z `BirdLifetime`. |
| `abilityUsed` | Blokuje ponowne użycie zdolności tego ptaka. |
| `dead` | Oznacza obiekt do usunięcia po kroku symulacji. |
| `fragment` | Oznacza kawałek po rozpadzie; fragment nie rozpada się dalej. |
| `noShatter` | Wyłącza rozpad, m.in. dla ciał po zmianie środka masy. |
| `armLen` | Długość cięgien siedziska huśtawki; używana przy rysowaniu huśtawki. |
| `prevPos`, `prevAngle` | Pozycja i kąt z poprzedniego stanu fizyki; używane do płynnej interpolacji obrazu. |
| `preCom` | Położenie środka masy sprzed kroku; używane w prędkości punktu kontaktu. |
| `preV`, `preW` | Prędkość liniowa i kątowa sprzed kroku; używane do oszacowania względnej prędkości kontaktu. |

Metody `Body`:

- **`saveState()`** — pobiera transformację, środek masy i prędkości z aktora PhysX. Wywołuje ją `Physics::step()` przed `simulate()`, a `Physics::finish()` przy tworzeniu ciała.
- **`velocityAt(p)`** — oblicza prędkość punktu `p`: `preV + preW × (p - preCom)`. Wywołuje ją `Physics::relativeVelocityAt()` podczas filtrowania kontaktów.
- **`pose(alpha)`** — interpoluje pozycję i kąt między zapamiętanym stanem a bieżącą pozą PhysX. Używa jej `Game::drawBody()`.

### 3.6. Migawki i efekty: `Flash`, `Fracture`

- **`GameData::Flash`**: `x`, `y` to środek wybuchu, `t` to czas jego animacji. `Game::explode()` dodaje efekt, `updateFlashes()` aktualizuje i usuwa wygasłe, a `drawExplosions()` rysuje.
- **`GameData::Fracture`**: `pos` — pozycja bloku; `v` — jego prędkość liniowa; `omega` — prędkość kątowa; `half` — połówki wymiarów; `angle` — obrót; `mat` — materiał. `makeFracture()` wypełnia migawkę, a `shatter()` na jej podstawie tworzy kawałki.
- Aliasy `Body`, `Pose`, `Flash`, `Fracture` skracają zapisy typów `GameData::...` w reszcie pliku.

## 4. Fizyka: `Physics`

`Physics` jest właścicielem sceny PhysX i rejestru dynamicznych ciał. Globalny obiekt `physics` jest współdzielony przez `Levels` i `Game`.

### 4.1. Pola klasy

| Pole | Rola |
|---|---|
| `BirdGroup`, `GhostGroup` | Numery grup filtrujących ptaki oraz cięgna huśtawki. |
| `allocator`, `errorCallback` | Obiekty pomocnicze wymagane do utworzenia PhysX. |
| `foundation` | Podstawowy obiekt/infrastruktura PhysX. |
| `physics` | Fabryka sceny, ciał, materiałów i jointów. |
| `dispatcher` | Dyspozytor zadań CPU używany przez scenę. |
| `scene` | Świat symulacji, do którego trafiają aktorzy. |
| `creatureMat` | Materiał PhysX dla ptaków, świń i granic statycznych. |
| `matPx` | Materiały PhysX odpowiadające definicjom w `GameData::Mats`. |
| `ground`, `leftWall`, `rightWall` | Statyczne aktory podłoża i granic planszy. |
| `bodies` | Rejestr/wskaźnikowy właściciel danych `Body` utworzonych dla poziomu. |
| `nextId` | Kolejny identyfikator nadawany nowym ciałom. |
| `Deco` | Wewnętrzna struktura dekoracji bez kolizji: środek `x`, `y`, połówki rozmiaru `hx`, `hy`, kolor `c`. |
| `decos` | Lista dekoracji rysowanych w tle. |
| `pivots` | Punkty zawiasów rysowane jako małe kółka. |
| `contactCallback` | Instancja callbacku przekazywana scenie do odbierania kontaktów. |

### 4.2. Filtrowanie i callback kontaktów

- **`filterShader(...)`** — tłumi pary, w których występuje `GhostGroup`, oraz pary dwóch ptaków. Dla pozostałych ustawia domyślne kontakty, zgłoszenia punktów kontaktu i wykrywanie kontaktów CCD. Wywołuje go PhysX przy kwalifikowaniu par kształtów.
- **`bodyOf(actor)`** — zwraca `Body*` z `actor->userData`; dla aktora bez danych gry (np. ściany) zwraca `nullptr`.
- **`MinImpactSpeed`** — próg długości względnej prędkości; używany przez callback jako filtr obrażeń.
- **`relativeVelocityAt(a, b, p)`** — odejmuje prędkości punktów kontaktu obu ciał. Brak ciała dynamicznego (`nullptr`) oznacza prędkość zerową, np. dla statycznego podłoża.
- **`ContactCallback::onContact(...)`** — sprawdza aktorów i pary kontaktu, pomija usunięte kształty, pobiera do 16 punktów kontaktu, filtruje je według względnej prędkości i sumuje magnitudy impulsów. Sumę dodaje do `impulse` obu ciał dynamicznych. `applyDamageRules()` później interpretuje tę wartość.
- Pozostałe metody callbacku (`onConstraintBreak`, `onWake`, `onSleep`, `onTrigger`, `onAdvance`) mają puste ciała, bo projekt nie korzysta z tych zdarzeń.

### 4.3. Tworzenie, aktualizacja i zwalnianie fizyki

- **`init()`** — tworzy fundament i obiekt PhysX, inicjalizuje rozszerzenia, buduje opis sceny, ustawia grawitację, dyspozytor, filtr i callback, włącza CCD, tworzy materiały oraz statyczne podłoże i ściany. Zwraca `false`, gdy nie uda się utworzyć krytycznego zasobu.
- **`releaseBody(b)`** — zwalnia aktora PhysX, a potem usuwa jego powiązaną strukturę `Body`. Używana przez `clear()` i `removeDead()`.
- **`clear()`** — usuwa wszystkie dynamiczne ciała poziomu i czyści `bodies`, `decos`, `pivots`; używana przy restarcie poziomu i powrocie do menu.
- **`shutdown()`** — zwalnia ciała, ściany, podłoże, materiały, scenę, dyspozytor, rozszerzenia, obiekt PhysX i fundament. Wywołuje ją `main()` przed zamknięciem programu.
- **`step(dt)`** — dla każdego `Body` zapisuje poprzedni stan, wywołuje `scene->simulate(dt)` i czeka na wynik `fetchResults(true)`. Uruchamia ją `Game::simulate()` dla każdego stałego kroku.
- **`finish(b, actor)`** — wspólna końcówka tworzenia ciała: zapisuje aktora i `id`, blokuje Z oraz obroty X/Y, ustawia tłumienie i iteracje solvera, łączy `userData`, dodaje aktora do sceny, zapisuje początkowy stan i dopisuje ciało do `bodies`.
- **`removeDead()`** — usuwa z PhysX i wektora każde `Body` z `dead == true`. Wywoływana po regułach obrażeń.
- **`find(id)`** — liniowo przeszukuje `bodies` i zwraca ciało z podanym numerem albo `nullptr`. `Game::useAbility()` używa jej do odnalezienia ostatniego ptaka.

### 4.4. Fabryki ciał i właściwości

- **`addBox(x, y, hx, hy, mat, angle, fragment)`** — tworzy dynamiczne pudełko o podanych połówkach rozmiaru, obrocie i materiale. Włącza CCD dla fragmentu, po czym przekazuje ciało do `finish()`. Używana przez wszystkie konstrukcje, wiatrak i odłamki.
- **`addSwing(x, y, hx, hy, armLen, mat)`** — tworzy dynamiczne siedzisko oraz dwa dodatkowe kształty cięgien na tym samym aktorze. Cięgna dostają filtr `GhostGroup`; po ich dodaniu masa i bezwładność są przeliczane dla całego aktora. Używana przez `Levels::buildSwing()`.
- **`addBall(x, y, radius, density, type, mat)`** — tworzy dynamiczną kulę. Kule typu `BALL` korzystają z materiału `matPx`; ptaki i świnie z `creatureMat`. Ptaki dostają filtr `BirdGroup` i CCD. Używana przez budowę poziomu, strzał i podział BLUE.
- **`setCom(b, dy)`** — przesuwa lokalny środek masy o `dy`, przelicza masę i tensor bezwładności z gęstości materiału, ustawia `noShatter`. Wywołują ją domino i kolumna z balastem.
- **`addDeco(x, y, hx, hy, color)`** — dodaje prostokąt wyłącznie do listy rysowania. Nie tworzy aktora ani kolizji. Używana do ram i słupków tła.

### 4.5. Rozpad i jointy

- **`makeFracture(b)`** — odczytuje pozę i prędkości aktora, po czym zapisuje je wraz z wymiarami i materiałem w `Fracture`. Wywoływana, gdy blok przekroczy próg zniszczenia.
- **`shatter(f)`** — wybiera siatkę podziału (szkło 2×2, pionowy blok 1×3, pozostały 3×1), oblicza środek każdego fragmentu w obróconym układzie, tworzy fragmenty przez `addBox()` i nadaje im prędkości zgodne z ruchem bloku. Wywoływana po zebraniu pękających bloków w `applyDamageRules()`.
- **`axisZ()`** — zwraca orientację osi jointu używaną, by zawias działał w płaszczyźnie gry.
- **`frameOn(actor, world)`** — przelicza ramkę punktu jointu ze świata do układu lokalnego aktora; gdy aktora nie ma, ramka pozostaje światowa.
- **`hinge(a, b, p)`** — tworzy `PxRevoluteJoint` w punkcie `p`; `nullptr` po stronie `a` oznacza połączenie ciała `b` ze światem. Używana przez wahadło i huśtawkę.
- **`motor(b, p, omega, maxTorque)`** — tworzy zawias świata z `b`, ustawia docelową prędkość, limit napędu i włącza napęd. Używana przez wiatrak.

## 5. Budowa planszy: `Levels`

`Levels` nie przechowuje własnego stanu. Metody statyczne wywołują fabryki globalnej instancji `physics`.

- **`buildHut(cx, pillarMat)`** — tworzy domek: podpory, półkę/dach i dwie świnie. `cx` jest środkiem konstrukcji, `pillarMat` materiałem podpór.
- **`buildTower(cx, floors)`** — tworzy wieżę z pięter; `cx` to środek poziomy, `floors` liczba pięter. Dolne podpory są szklane, pozostałe drewniane, a świnie trafiają na wybrane poziomy i szczyt.
- **`buildWindmill(cx, cy, halfLen)`** — tworzy aluminiową belkę na środku `(cx, cy)`, zawiesza ją i uruchamia silnikiem. Dodaje też znacznik osi i dekoracyjny słup.
- **`buildDominoes(x0, n, spacing)`** — ustawia `n` kamiennych kostek od współrzędnej `x0` z odstępem `spacing`, po czym przesuwa środek masy każdej z nich.
- **`buildPendulum(ax, ay, links, ballR)`** — tworzy `links` metalowych ogniw pod punktem `(ax, ay)`, łączy je zawiasami i dodaje kamienną kulę promienia `ballR` na końcu.
- **`buildSwing(cx, topY, len)`** — dodaje dekoracyjną ramę, ruchome siedzisko o długości cięgien `len`, zawias w `(cx, topY)` i świnię na siedzisku.
- **`buildTopHeavyColumn(x)`** — tworzy drewniany słup w położeniu `x`, przesuwa jego środek masy i umieszcza świnię nad nim.
- **`build()`** — składa wszystkie elementy poziomu w określonej kolejności i współrzędnych. Wywołuje ją `Game::startLevel()`.
- **`birdQueue()`** — zwraca wektor dziesięciu rodzajów ptaków. `startLevel()` zapisuje go do `Game::queue`.

## 6. Rysowanie: `Graphics`

Globalna instancja `graphics` jest współdzielona przez `Game`, `Input`, `Button` i `TextRenderer`.

### Pola `Graphics`

- `fbW`, `fbH` — rozmiar bufora ramki w pikselach.
- `vpX`, `vpY`, `vpW`, `vpH` — położenie i rozmiar viewportu, czyli obszaru, w którym rysowany jest świat.
- `fracX`, `fracY`, `fracW`, `fracH` — udział viewportu w całym oknie; `Input::poll()` wykorzystuje je do poprawnego położenia myszy przy letterboxingu.

### Metody `Graphics`

- **`layout(win)`** — pobiera rozmiar framebufferu, dopasowuje viewport do proporcji świata i oblicza ułamki viewportu. Wywoływana co klatkę w `main()`.
- **`init()`** — włącza mieszanie alfa dla przezroczystych elementów.
- **`beginFrame(win)`** — czyści okno, ustawia viewport i ortograficzną projekcję od `(0,0)` do `(ViewW,ViewH)`. Parametr `win` pozostaje niewykorzystany; rozmiary zostały wcześniej odczytane w `layout()`. Metoda jest wywoływana przed `Game::draw()`.
- **`quad(x,y,hx,hy,angle,color)`** — rysuje obrócony prostokąt o środku `(x,y)` i połowach wymiarów `hx`, `hy`. Używana do bloków, tła, linii i przycisków.
- **`circle(x,y,radius,color)`** — rysuje koło jako 40-kąt. Używana dla ptaków, świń, kul, osi zawiasów i efektów.
- **`line(a,b,thickness,color)`** — zamienia odcinek między punktami `a`, `b` na cienki prostokąt. Używana m.in. do cięgien huśtawki i gumy procy.

## 7. Czcionka: `TextRenderer`

Globalna instancja `text` obsługuje wszystkie napisy. Wczytuje TTF, pakuje glify do tekstury i rysuje je jako teksturowane quady.

### Pola i typy

- `fontPath` — ścieżka względna do pliku `PressStart2P-Regular.ttf`.
- `atlasSize` — długość boku atlasu tekstury (512 px).
- `fontPx` — bazowy rozmiar glifów przy pakowaniu.
- `Range { first, count }` — opis zakresu kodów znaków Unicode.
- `ranges`, `numRanges` — trzy obsługiwane zakresy znaków i ich liczba.
- `glyphs` — dane metryczne i współrzędne glifów w atlasie.
- `texture` — identyfikator tekstury OpenGL.
- `ready` — informacja, czy atlas został utworzony; `draw()` nic nie robi, gdy ta flaga jest fałszywa.

### Metody

- **`snap(v)`** — zaokrągla współrzędną do piksela; używana do ograniczania rozmycia małej czcionki.
- **`readFile(path, out)`** — wczytuje binarnie plik czcionki do wektora bajtów. `init()` używa wyniku do spakowania glifów.
- **`init()`** — ładuje TTF, przygotowuje atlas alfa przez `stb_truetype`, buduje teksturę RGBA OpenGL i ustawia `ready`.
- **`decode(s)`** — odczytuje jeden znak UTF-8 i przesuwa wskaźnik `s` na kolejny znak. Obsługuje znaki jedno-, dwu- i trzybajtowe; pozostałe sekwencje zastępuje `?`. Używana podczas liczenia i rysowania tekstu.
- **`glyph(cp)`** — znajduje glif dla kodu punktowego; brakujący znak zastępuje znakiem `?`, jeśli jest dostępny.
- **`scale(size)`** — przelicza rozmiar tekstu ze skali świata na piksele zależnie od szerokości viewportu.
- **`width(s,size)`** — sumuje szerokości glifów i zwraca szerokość napisu w jednostkach świata. Używana do centrowania.
- **`draw(s,x,y,size,color)`** — rysuje tekst w zadanym położeniu, chwilowo ustawiając projekcję pikselową i teksturę atlasu.
- **`drawCentered(s,cx,cy,size,color)`** — oblicza początek napisu na podstawie jego szerokości i wywołuje `draw()`; używana w przyciskach i ekranach.

## 8. Interfejs: `Button` i `Ui`

### `Button`

Pojedynczy przycisk przechowuje:

- `x`, `y` — środek przycisku w świecie gry.
- `w`, `h` — jego szerokość i wysokość.
- `label` — tekst na przycisku.

Metody:

- **`contains(mouse)`** — sprawdza, czy punkt myszy leży w prostokącie przycisku. Używana przy kliknięciach i podświetleniu.
- **`draw(hover)`** — rysuje cień, obramowanie, tło i tekst; `hover` rozjaśnia tło.

### `Ui`

Globalne `ui` przechowuje gotowe przyciski:

- `Play` — rozpoczęcie gry, sprawdzany i rysowany w menu.
- `Quit` — wyjście z gry, sprawdzany i rysowany w menu.
- `Retry` — ponowne uruchomienie poziomu na ekranie końcowym.
- `MenuRight` — powrót do menu z ekranu końcowego.

## 9. Sterowanie: `Input`

`Input` rozdziela surowe zdarzenia GLFW od logiki gry.

### Pola

- `mouse` — pozycja kursora w jednostkach świata, a nie pikselach.
- `lmb` — stan lewego przycisku myszy w bieżącej klatce.
- `clicked` — jednorazowy sygnał nowego kliknięcia, nie ciągłe przytrzymanie.
- `escPressed`, `rPressed` — jednorazowe sygnały świeżego naciśnięcia klawiszy.
- `escPrev`, `rPrev` — prywatne stany klawiszy z poprzedniej klatki; pozwalają wykryć przejście z puszczonego do wciśniętego.

### `poll(win)`

Pobiera pozycję kursora i rozmiar okna, zamienia piksele na współrzędne świata z uwzględnieniem letterboxingu (`graphics.frac...`), odczytuje mysz i klawisze, po czym porównuje je ze stanem poprzedniej klatki. Wyniki są przekazywane do `Game::update()`.

Zmienne lokalne `mx`, `my` przechowują surową pozycję kursora, `ww`, `wh` — rozmiar okna, `nx`, `ny` — pozycję znormalizowaną, a `l`, `e`, `r` — aktualne stany wejść przed wykryciem nowych naciśnięć.

## 10. Logika rozgrywki: `Game`

Globalnych instancji `Game` nie ma — `main()` tworzy jeden lokalny obiekt `game`. `Game` przechowuje stan bieżącej rozgrywki i koordynuje fizykę, wejście oraz rysowanie.

### 10.1. Pola

- `quit` — prośba o zamknięcie pętli; ustawiana przez Escape w menu albo przycisk QUIT.
- `state` — wartość `State`, określa aktualny ekran.
- `mouse` — kopia pozycji myszy z `Input`, używana do sprawdzania przycisków.
- `queue` — kolejka rodzajów ptaków z `Levels::birdQueue()`.
- `birdIdx` — indeks następnego ptaka; równocześnie liczba już zużytych ptaków.
- `lastBirdId` — identyfikator ostatniego wystrzelonego ptaka; wykorzystywany przez `useAbility()`.
- `flashes` — aktywne wizualne efekty wybuchu.
- `dragging` — informacja, czy gracz aktualnie przeciąga ptaka.
- `dragPos` — bieżące położenie przeciąganego ptaka.
- `accumulator` — czas oczekujący na kolejny stały krok fizyki.
- `levelTime` — czas poziomu liczony krokami `Dt`; służy m.in. do sprawdzenia `SettleTime`.
- `sinceLaunch` — czas od strzału; steruje przeładowaniem procy i limitem przegranej.
- `winTimer`, `loseTimer` — liczniki opóźnień przed końcem gry.

### 10.2. Aktualizacja stanu i wejścia

- **`update(input, frameDt)`** — publiczny punkt aktualizacji: wywołuje `handleInput()`, a następnie `simulate()`.
- **`clearWorld()`** — czyści obiekty PhysX poziomu i efekty wybuchu.
- **`startLevel()`** — czyści świat, buduje poziom, pobiera kolejkę ptaków, resetuje liczniki i przechodzi do `PLAYING`. Ustawia `levelTime = 1.5`, co pomija oczekiwanie osiadania.
- **`toMenu()`** — czyści świat, kończy przeciąganie i przełącza ekran na `MENU`.
- **`handleInput(input)`** — wybiera reakcję zależnie od `state`: obsługuje menu, Escape, restart R, celowanie i przyciski Retry/Menu.
- **`canAim()`** — zwraca `true`, jeśli pozostał ptak, minął czas przeładowania i `levelTime` jest większy niż `SettleTime`.
- **`handleAiming(input)`** — kliknięcie przy zaczepie rozpoczyna przeciąganie; ruch ogranicza do `MaxPull`; puszczenie myszy uruchamia `launch()`, o ile naciąg przekracza `MinPull`. Kliknięcie, które nie rozpoczyna celowania, próbuje użyć zdolności.

### 10.3. Strzał i umiejętności

- **`launchVelocity(kind)`** — oblicza masę ptaka z gęstości i objętości kuli, a następnie prędkość z modelu `v = x√(k/m)`. Wynikiem jest wektor 2D skierowany od położenia naciągu do zaczepu.
- **`launch()`** — pobiera następny `Kind`, tworzy ciało ptaka, ustawia jego rodzaj i prędkość 3D (składowa Z równa zero), zapisuje `id`, resetuje przeładowanie i kończy przeciąganie.
- **`useAbility()`** — odnajduje ptaka po `lastBirdId`, sprawdza `abilityUsed` i wybiera zdolność według `kind`: żółty zwiększa prędkość, niebieski dzieli się, czarny wybucha.
- **`split(bird)`** — zapamiętuje pozycję i prędkość, zmniejsza promień istniejącej kuli, aktualizuje jej masę/bezwładność i tworzy dwie mniejsze kule z przeciwnymi składowymi bocznymi prędkości.
- **`explode(bomb)`** — przegląda ciała, oblicza odległość od wybuchu, pomija obiekty poza zasięgiem i przykłada radialny impuls malejący liniowo z odległością. Dodaje `Flash` i oznacza bombę do usunięcia. Nie wykonuje testu zasłonięcia przeszkodą.

Ważniejsze zmienne lokalne tych metod:

- W `launchVelocity()`: `r` to promień ptaka, `mass` jego masa, `anchor` punkt procy.
- W `handleAiming()`: `anchor` jest punktem zaczepienia, `pull` wektorem naciągu, `len` jego długością.
- W `launch()`: `k` to rodzaj pobranego ptaka, `bird` to utworzone ciało gry, `v` to wyliczona prędkość startowa.
- W `split()`: `p` i `v` to pozycja i prędkość przed zmianą, `r` nowy promień, `shape` kształt PhysX, `vLen` szybkość, `perp` jednostkowy kierunek boczny, `dv` boczna poprawka prędkości, `side` wybiera jej znak, `n` to nowo utworzone ciało.
- W `explode()`: `p` jest środkiem wybuchu, `d` wektorem do ciała, `dist` odległością, `J` wartością impulsu, `dir` kierunkiem impulsu.

### 10.4. Symulacja, zniszczenia i wynik

- **`simulate(frameDt)`** — dodaje czas klatki do `accumulator` i wykonuje tyle kroków `Dt`, ile potrzeba. W każdym kroku uruchamia fizykę, aktualizuje zegary, obrażenia, usuwanie obiektów, efekty i warunki końca. W menu zeruje akumulator.
- **`applyDamageRules()`** — sprawdza `settling`, zwiększa `age`, zabija świnie lub oznacza bloki do rozpadu na podstawie impulsu, usuwa stare/wypadające obiekty, zeruje impulsy i dopiero po pętli tworzy odłamki zapisane w `toBreak`.
- **`updateFlashes()`** — zwiększa czas `t` efektów i usuwa te, które przekroczyły `FlashTime`.
- **`pigsAlive()`** — liczy świnie bez flagi `dead`; używana do sprawdzania zwycięstwa i HUD.
- **`birdInFlight()`** — sprawdza prędkość liniową oraz kątową żywych ptaków; używana przy ocenie przegranej.
- **`checkEnd()`** — gdy nie ma świń, zwiększa `winTimer`; po `WinDelay` ustawia `WON`. Gdy wyczerpano ptaki, sprawdza twardy limit lub czekanie na zatrzymanie i ustawia `LOST`.

Zmienne lokalne:

- W `simulate()`: `frameDt` to czas klatki; `accumulator` rozdziela go na stałe kroki.
- W `applyDamageRules()`: `settling` oznacza fazę ochronną, `toBreak` zbiera migawki bloków, a `p` służy do wykrywania ciał poza planszą.
- W `pigsAlive()`: `pigs` jest licznikiem żywych świń.
- W `checkEnd()`: timery aktualizowane są o stały krok `Dt`, nie o zmienny czas renderowania.

### 10.5. Rysowanie elementów gry

- **`draw()`** — rysuje tło, dekoracje, procę, ciała, punkty zawiasów, wybuchy, aktywnego ptaka, kolejkę, HUD i ewentualny overlay. Przed rysowaniem ciał wylicza `alpha = accumulator / Dt` do interpolacji.
- **`drawBirdRotated(kind,x,y,r,angle)`** — rysuje koło ptaka, oko i źrenicę, obracając położenie detali. Używana dla ptaków w świecie.
- **`drawBird(kind,x,y,r)`** — wariant bez obrotu; używany dla ikon i ptaka na procy.
- **`drawPig(x,y,r,angle)`** — rysuje świnię i oczy.
- **`drawBall(body,x,y,angle)`** — rysuje kulę materiałową z małym znacznikiem, który wizualnie pokazuje obrót.
- **`drawBody(body,alpha)`** — pobiera interpolowaną pozę i wybiera rysowanie według `Type`. Dla bloku rysuje też cięgna huśtawki, jeśli `armLen > 0`.
- **`drawSling()`** — rysuje drewnianą podstawę procy.
- **`drawExplosions()`** — rysuje rozszerzające się i zanikające koła na podstawie wieku `Flash`.
- **`drawActiveBird()`** — rysuje gumę, ptaka podczas naciągania i przewidywany tor lotu; po przeładowaniu pokazuje ptaka oczekującego na procy.
- **`drawQueue()`** — rysuje pozostałe ptaki jako pomniejszone ikony.
- **`drawHud()`** — pokazuje liczbę pozostałych ptaków oraz żywych świń.
- **`drawMenu()`** — rysuje tytuł, ptaki przykładowe i przyciski PLAY/QUIT.
- **`drawOverlay()`** — przyciemnia ekran, pokazuje wynik i przyciski RETRY/MENU.

Zmienne lokalne rysowania mają znaczenie geometryczne: `alpha` to udział interpolacji; `c` i `s` to cosinus/sinus kąta; `ex`, `ey` to pozycja oka; `p` to interpolowana poza; `k` steruje zanikiem/rozjaśnieniem; `left`, `right`, `top` to punkty gumy i zawieszenia; `v` to przewidywana prędkość strzału; `t` to czas próbki trajektorii; `a` to przezroczystość punktu. Zmienne te istnieją tylko podczas danego rysowania.

## 11. Punkt wejścia: `main()`

`main()` inicjalizuje GLFW, tworzy okno i kontekst OpenGL, przygotowuje GLAD, renderer, tekst oraz PhysX, a następnie prowadzi pętlę programu. Tworzy lokalne obiekty `Game game` i `Input input`.

Zmienne lokalne:

- `window` — wskaźnik do okna GLFW; jest przekazywany rendererowi i wejściu, a na końcu niszczony.
- `prevTime` — czas poprzedniej klatki, używany do obliczenia czasu klatki.
- `now` — bieżący czas; różnica `now - prevTime` trafia do aktualizacji gry (ograniczona do `0.1 s`).
- Parametry lambdy callbacku: `code` — numer błędu GLFW, `msg` — tekst komunikatu.

W każdej iteracji `main()` pobiera zdarzenia, ustala layout, odczytuje wejście, wywołuje `game.update()`, rozpoczyna klatkę grafiki, wywołuje `game.draw()` i zamienia bufory. Pętla kończy się po zamknięciu okna albo ustawieniu `game.quit`. Na końcu `physics.shutdown()` zwalnia fizykę, a GLFW niszczy okno i kończy pracę.

## 12. Mapa zależności — od działania użytkownika do efektu

| Działanie / zdarzenie | Ścieżka w kodzie |
|---|---|
| Przeciągnięcie i puszczenie myszy | `Input::poll()` → `Game::handleInput()` → `handleAiming()` → `launchVelocity()` → `launch()` → `Physics::addBall()` |
| Zderzenie ptaka z konstrukcją | `Physics::step()` → callback PhysX → `ContactCallback::onContact()` → `Body::impulse` → `Game::applyDamageRules()` |
| Zniszczenie bloku | próg `MatDef::breakImpulse` → `makeFracture()` → oznaczenie `dead` → `shatter()` → `addBox()` dla fragmentów |
| Użycie zdolności BLUE | kliknięcie → `useAbility()` → `split()` → zmiana kształtu i dwa wywołania `addBall()` |
| Zwycięstwo | `pigsAlive() == 0` → timer `winTimer` → `state = WON` → `drawOverlay()` |
| Zmiana rozmiaru okna | `Graphics::layout()` → viewport i ułamki → `Input::poll()` mapuje kursor w tej samej skali |

## 13. Wskazówki do nauki kodu

Najłatwiej zrozumieć program, śledząc jedną pełną ścieżkę zamiast czytać wszystkie klasy naraz:

1. **Strzał:** zacznij od `Input::poll()`, potem przejdź przez `Game::handleAiming()`, `launchVelocity()` i `launch()`.
2. **Uderzenie:** sprawdź zapis stanu w `Physics::step()`, callback kontaktu, pole `Body::impulse` i `Game::applyDamageRules()`.
3. **Rozpad:** przejdź od `makeFracture()` do `shatter()` i tworzenia fragmentów w `addBox()`.
4. **Wahadło/wiatrak:** porównaj `hinge()` z `motor()` i sprawdź, jak wywołują je metody `Levels`.
5. **Klatka obrazu:** prześledź `main()` → `Game::draw()` → prymitywy klasy `Graphics` i napisy klasy `TextRenderer`.

Taki sposób czytania pokazuje, gdzie dane powstają, którędy przepływają i który fragment kodu wykorzystuje je do podjęcia decyzji.

## 14. Zmienne lokalne w najważniejszych metodach

Pola klas są opisane przy swoich klasach. Poniżej zebrano zmienne tworzone wewnątrz metod — istnieją tylko na czas wykonania danego fragmentu. Nazwy są zgodne z kodem.

### 14.1. Fizyka i budowanie poziomu

- **`Physics::relativeVelocityAt()`** — `v0`, `v1`: prędkości punktu kontaktu pierwszego i drugiego ciała; metoda zwraca ich różnicę.
- **`ContactCallback::onContact()`** — `a`, `b`: dane gry powiązane z aktorami; `i`: indeks pary kontaktu; `points`: bufor punktów kontaktowych; `n`: liczba pobranych punktów; `sum`: suma impulsu zaakceptowanych punktów; `j`: indeks punktu; `vrel`: prędkość względna w punkcie.
- **`Physics::init()`** — `desc`: konfiguracja tworzonej sceny PhysX; `i`: indeks materiału w pętli tworzącej `matPx`.
- **`Physics::finish()`** — parametry `b`, `actor`: struktura gry i aktor PhysX, które są łączone w jedno zarejestrowane ciało.
- **`Physics::addBox()`** — `b`: nowa struktura danych bloku; `pose`: transformacja początkowa z pozycją i obrotem; `actor`: utworzone dynamiczne ciało PhysX.
- **`Physics::addSwing()`** — `b`: dane huśtawki; `actor`: jej dynamiczny aktor; `L`: długość geometryczna cięgna; `phi`: kąt cięgna względem pionu; `side`: wybór lewego/prawego cięgna; `arm`: kształt cięgna dołączony do aktora.
- **`Physics::addBall()`** — `b`: dane kuli; `m`: wybrany materiał PhysX; `actor`: nowa kula; `shape`: kształt ptaka pobierany, by ustawić filtr kolizji.
- **`Physics::setCom()`** — `com`: lokalne przesunięcie środka masy w osi Y.
- **`Physics::find()`** — `b`: kolejne ciało sprawdzane w rejestrze.
- **`Physics::removeDead()`** — `it`: iterator po wektorze; `b`: ciało wskazywane przez iterator i ewentualnie zwalniane.
- **`Physics::makeFracture()`** — `t`: transformacja aktora, z której pobierana jest pozycja i kąt.
- **`Physics::shatter()`** — `nx`, `ny`: liczba fragmentów w poziomie i pionie; `hx`, `hy`: połówki wymiarów fragmentu; `c`, `s`: cosinus i sinus kąta bloku; `ix`, `iy`: indeksy fragmentu w siatce; `lx`, `ly`: jego lokalny środek względem bloku; `center`: środek w świecie; `n`: utworzone ciało-fragment.
- **`Physics::frameOn()`** — `a`: aktor, którego lokalna ramka jest obliczana; `world`: punkt i orientacja jointu w przestrzeni świata.
- **`Physics::hinge()`** — `w`: transformacja punktu zawiasu w świecie; `A`: aktor po stronie `a`, albo `nullptr`, gdy zawias jest zakotwiony w świecie.
- **`Physics::motor()`** — `j`: utworzony przegub, na którym ustawiane są parametry napędu.
- **`Levels::buildTower()`** — `hw`: połowa rozstawu podpór; `k`: numer piętra; `y0`: wysokość podstawy aktualnego piętra; `pm`: materiał podpór tego piętra.
- **`Levels::buildWindmill()`** — `AngularSpeed`, `MaxTorque`: lokalne stałe prędkości i momentu napędu; `arm`: utworzona belka wiatraka.
- **`Levels::buildDominoes()`** — `HalfH`: połowa wysokości kostki; `ComShift`: przesunięcie jej środka masy; `i`: numer kostki; `b`: nowa kostka.
- **`Levels::buildPendulum()`** — `L`: długość ogniwa; `prev`: poprzednie ogniwo, do którego dołączany jest następny element; `i`: numer ogniwa; `link`: nowo utworzone ogniwo; `ball`: kula końcowa.
- **`Levels::buildSwing()`** — `seat`: aktor siedziska przekazywany do utworzenia zawiasu.
- **`Levels::buildTopHeavyColumn()`** — `col`: utworzona kolumna, której środek masy jest następnie przesuwany.

### 14.2. Grafika, tekst i wejście

- **`Graphics::layout()`** — `aspect`: proporcja świata, według której obliczana jest szerokość albo wysokość viewportu.
- **`Graphics::circle()`** — `segments`: liczba boków przybliżającego koło wielokąta; `i`: indeks punktu obwodu; `t`: kąt bieżącego punktu.
- **`Graphics::line()`** — `d`: wektor od początku odcinka do końca; jego długość, środek i kąt wyznaczają prostokąt linii.
- **`TextRenderer::readFile()`** — `file`: strumień pliku; `size`: długość pliku w bajtach.
- **`TextRenderer::init()`** — `ttf`: zawartość pliku czcionki; `alpha`: bufor atlasu glifów; `pc`: kontekst pakowania `stb_truetype`; `i`: indeks zakresu/teksela; `rgba`: gotowe piksele tekstury z kanałem alfa.
- **`TextRenderer::decode()`** — `c`: pierwszy bajt znaku UTF-8; `cp`: złożony kod punktowy znaku wielobajtowego.
- **`TextRenderer::glyph()`** — `i`: indeks zakresu Unicode sprawdzanego w tablicy.
- **`TextRenderer::scale()`** — `pixelsPerUnit`: liczba pikseli viewportu przypadająca na jednostkę świata.
- **`TextRenderer::width()`** — `sc`: skala glifu w pikselach; `px`: zsumowana szerokość; `p`: wskaźnik przesuwający się po tekście; `g`: metryki aktualnego glifu.
- **`TextRenderer::draw()`** — `sc`: skala tekstu; `px`, `py`: kursor rysowania w pikselach; `p`: wskaźnik czytania tekstu; `g`: aktualny glif; `x0`, `x1`, `y0`, `y1`: narożniki quadów znaku; `u0`, `u1`, `v0`, `v1`: narożniki obszaru glifu w atlasie.
- **`TextRenderer::drawCentered()`** — `baselineShift`: przesunięcie linii bazowej potrzebne do optycznego wyśrodkowania napisu.
- **`Button::draw()`** — `k`: mnożnik jasności tła zależny od tego, czy kursor wskazuje przycisk.
- **`Input::poll()`** — `mx`, `my`: pikselowa pozycja kursora; `ww`, `wh`: rozmiar okna; `nx`, `ny`: znormalizowana pozycja po uwzględnieniu kierunku osi Y; `l`, `e`, `r`: bieżące stany myszy, Escape i R.

### 14.3. Rysowanie i stan gry

- **`Game::draw()`** — `alpha`: ułamek akumulatora względem kroku fizyki, używany do interpolacji renderowanych ciał; `d`: aktualna dekoracja; `b`: ciało; `p`: punkt zawiasu.
- **`drawBirdRotated()`** — `c`, `s`: cosinus i sinus obrotu; `ex`, `ey`: położenie oka ptaka.
- **`drawPig()`** — `c`, `s`: cosinus i sinus kąta, które obracają położenie oczu.
- **`drawBall()`** — `c`: kolor kuli z materiału; kąt wejściowy wyznacza pozycję znacznika obrotu.
- **`drawBody()`** — `p`: interpolowana poza; lokalne `c` to kolor materiału; `cs`, `sn` to cosinus i sinus obrotu siedziska; `top` to punkt zawieszenia; `side`, `lx` wyznaczają lewy i prawy koniec siedziska.
- **`drawExplosions()`** — `k`: ułamek czasu efektu, steruje promieniem i zanikiem; `c`: kopia koloru z ustawioną przezroczystością.
- **`drawActiveBird()`** — `k`: rodzaj aktualnego ptaka; `left`, `right`: końce gumy; `v`: przewidywana prędkość strzału; `i`, `t`: indeks i czas próbkowania toru; `a`: przezroczystość punktu trajektorii.
- **`drawQueue()`** — `i`: indeks ptaka w kolejce; `n`: indeks ikony; `k`: rodzaj ptaka; `r`: pomniejszony promień; `x`: położenie ikony.
- **`drawHud()`** — `buf`: tekstowy bufor HUD; `birdsLeft`: liczba niewystrzelonych ptaków.
- **`drawMenu()`** — `i`: indeks przykładowego ptaka na ekranie menu.
- **`drawOverlay()`** — `button`: lokalna funkcja-lambda rysująca przycisk ze stanem hover.
- **`Game::useAbility()`** — `b`: odnaleziony ostatnio wystrzelony ptak.
- **`Game::handleAiming()`** — `anchor`: punkt zaczepienia; `pull`: wektor od zaczepu do myszy; `len`: długość wektora naciągu.
- **`Game::applyDamageRules()`** — `settling`: informacja, czy naliczanie obrażeń powinno być wyłączone; `toBreak`: migawki bloków, które zostaną rozbite po zakończeniu przeglądania ciał; `b`: aktualnie sprawdzane ciało; `p`: jego pozycja; `f`: migawka przekazywana do rozpadu.
- **`Game::updateFlashes()`** — `f`: aktualny efekt, którego czas jest zwiększany.
- **`Game::pigsAlive()`** — `pigs`: licznik żywych świń; `b`: aktualnie sprawdzane ciało.
- **`Game::birdInFlight()`** — `b`: kolejny ptak sprawdzany pod kątem prędkości.
- **`Game::drawOverlay()`** — lokalna lambda `button` otrzymuje przycisk, sprawdza pozycję myszy i rysuje go ze stanem hover.

## 15. Wspólne obiekty i najczęściej przekazywane parametry

Na poziomie pliku tworzone są cztery współdzielone instancje:

- `physics` — jedyny zarządca sceny PhysX; używają go `Levels` i `Game`.
- `graphics` — renderer kształtów; używają go gra, przyciski, tekst i mapowanie myszy.
- `text` — renderer napisów; używają go `Button` i `Game`.
- `ui` — zestaw przycisków; sprawdza go `Game`, rysuje menu i ekran wyniku.

Najczęściej spotykane parametry geometryczne: `x`, `y` to współrzędne środka; `hx`, `hy` to **połówki** szerokości i wysokości; `radius` to promień; `angle` to obrót w radianach; `color` lub `c` to kolor; `mat` to materiał; `kind` to rodzaj ptaka. `p` często oznacza punkt lub pozycję, a `v` prędkość. Znaczenie zależy od funkcji i zostało doprecyzowane w jej opisie.
