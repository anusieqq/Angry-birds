# Dokumentacja projektu Angry Birds

## 1. O projekcie

Program jest prostą grą typu *Angry Birds*. Gracz naciąga procę i wystrzeliwuje ptaki w stronę konstrukcji. Celem jest zniszczenie wszystkich świń. W świecie gry znajdują się między innymi bloki z różnych materiałów, domino, wieża, wiatrak z napędem, wahadło i huśtawka.

Projekt jest napisany w C++. Cała logika znajduje się w jednym pliku `AngryBirds.cpp`. PhysX odpowiada za symulację ruchu i zderzeń, GLFW za okno i wejście, OpenGL za rysowanie, a `stb_truetype` za przygotowanie tekstu z pliku czcionki.

Kod używa również GLAD do udostępnienia funkcji OpenGL. Konkretne wersje PhysX, GLFW i wymagany standard C++ wynikają z konfiguracji projektu i użytego zestawu bibliotek; nie należy ich wywodzić wyłącznie z tego pliku. Czcionka `PressStart2P-Regular.ttf` jest otwierana względem katalogu roboczego. Jeśli pliku brakuje, renderer zgłasza błąd, a tekst nie jest wyświetlany.

## 2. Organizacja programu

Kod jest podzielony na klasy, ale zapisany w jednym pliku. Kolejność klas pomaga oddzielić odpowiedzialności:

- **`Config`** — przechowuje stałe gry: wymiary świata i okna, grawitację, krok symulacji, parametry procy, progi oraz ustawienia solvera.
- **`GameData`** — przechowuje kolory, definicje ptaków i materiałów oraz struktury danych obiektów.
- **`Physics`** — inicjalizuje PhysX, tworzy ciała i materiały, prowadzi symulację, odbiera kontakty, obsługuje zawiasy, impulsy i rozpad bloków.
- **`Levels`** — składa elementy planszy z obiektów fizycznych.
- **`Graphics`** — rysuje prostokąty, koła i linie w świecie 2D.
- **`TextRenderer`** — ładuje czcionkę i wyświetla napisy.
- **`Button`** i **`Ui`** — opisują przyciski i ich położenie.
- **`Input`** — odczytuje mysz i klawiaturę oraz zamienia współrzędne kursora na współrzędne świata gry.
- **`Game`** — łączy wejście, zasady rozgrywki, symulację i rysowanie.
- **`main()`** — inicjalizuje biblioteki i uruchamia główną pętlę programu.

### Struktury danych obiektów

`Body` łączy aktora PhysX z informacjami potrzebnymi grze i grafice: typem obiektu, materiałem, promieniem lub rozmiarem bloku, stanem zniszczenia, zebranym impulsem oraz danymi ptaka. Wskaźnik `actor` prowadzi do ciała symulowanego przez PhysX, a `userData` aktora wskazuje z powrotem na odpowiadający mu `Body`.

`Pose` jest uproszczoną pozą do rysowania: pozycją X/Y i kątem. `Fracture` przechowuje stan bloku w chwili rozpadu. `Flash` przechowuje pozycję i czas życia animacji wybuchu.

## 3. Fizyka — szczegółowe omówienie

### 3.0. Przepływ fizyki w jednej klatce

Główna pętla odczytuje wejście, wywołuje `Game::update()`, a następnie rysuje scenę. Aktualizacja gry najpierw obsługuje sterowanie, a potem przekazuje czas do symulacji. Akumulator czasu może uruchomić zero, jeden lub kilka stałych kroków fizyki:

```text
Game::update
├─ handleInput
└─ simulate(frameDt)
   └─ while accumulator >= Dt
      ├─ Physics::step(Dt)
      │  ├─ Body::saveState()       zapis pozycji i prędkości sprzed kroku
      │  ├─ scene->simulate(Dt)     obliczenia PhysX i kontakty
      │  └─ scene->fetchResults()
      ├─ applyDamageRules()         obrażenia i zaplanowanie rozpadu
      ├─ Physics::removeDead()      usunięcie oznaczonych ciał
      ├─ updateFlashes()
      ├─ checkEnd()
      └─ accumulator -= Dt
```

Ważna jest kolejność: stan prędkości do oceny kontaktu jest zapisywany przed symulacją. Ciała do zniszczenia są oznaczane, a usuwane po zakończeniu kroku, poza callbackiem kontaktów.

### 3.1. Fizyka 2D zrealizowana w PhysX 3D

PhysX symuluje trójwymiarowe ciała. Gra wykorzystuje tylko płaszczyznę XY, dlatego każde dynamiczne ciało ma zablokowany ruch wzdłuż Z oraz obroty wokół osi X i Y. Pozostaje ruch w poziomie i pionie oraz obrót wokół Z. Dzięki temu obliczenia kolizji i dynamiki wykonuje PhysX, ale gracz widzi i obsługuje płaski poziom.



Świat ma grawitację skierowaną w dół osi Y o wartości `9.81`. Podłoże i boczne ściany są ciałami statycznymi — nie poruszają się pod wpływem zderzeń. Ciała ptaków, świń i elementów konstrukcji są dynamiczne. Wymiary świata można interpretować w metrach, ale kod nie wykonuje konwersji jednostek ani nie narzuca skali fizycznej.

### 3.2. Masa, gęstość i bezwładność

Dynamiczne bloki i kule są tworzone z podaną gęstością. PhysX wyznacza z kształtu i gęstości masę oraz moment bezwładności. W kodzie ptaki mają gęstość `3`, świnie `1`; materiały konstrukcyjne mają własne gęstości. W praktyce oznacza to, że ciężar wynika z rozmiaru i gęstości obiektu, a nie z przypadkowej wartości wpisanej oddzielnie dla każdego elementu.

Wartości gęstości zapisane w kodzie to: ptaki `3`, świnie `1`, drewno `0.8`, kamień i szkło `2.5`, metal `7.8`, aluminium `2.7`. Bloki są prostopadłościanami o głębokości 1 jednostki, a kule są pełnymi kulami 3D; ruch poza płaszczyzną jest zablokowany, ale masa i bezwładność nadal pochodzą z geometrii 3D.

Moment bezwładności opisuje, jak trudno zmienić ruch obrotowy ciała. Zależy od masy i rozmieszczenia tej masy względem środka masy. Ma to znaczenie np. dla domino, kolumny z balastem i huśtawki.

### 3.3. Materiały, tarcie i sprężystość

Tabela `GameData::Mats` opisuje materiały: drewno, kamień, szkło, metal i aluminium. Każdy wpis zawiera gęstość, próg zniszczenia, tarcie, sprężystość oraz kolor. PhysX otrzymuje z tych danych materiał kolizyjny: współczynnik tarcia statycznego, tarcia dynamicznego i restytucji (sprężystości zderzenia). Tarcie dynamiczne w kodzie wynosi `0.85` tarcia statycznego.

| Materiał | Gęstość | Próg zniszczenia | Tarcie statyczne | Restytucja |
|---|---:|---:|---:|---:|
| Drewno | 0.8 | 9 | 0.65 | 0.15 |
| Kamień | 2.5 | niezniszczalny w regułach gry | 0.70 | 0.05 |
| Szkło | 2.5 | 4 | 0.55 | 0.10 |
| Metal | 7.8 | niezniszczalny w regułach gry | 0.40 | 0.20 |
| Aluminium | 2.7 | niezniszczalny w regułach gry | 0.40 | 0.20 |

W praktyce tarcie ogranicza ślizganie się obiektów po sobie, a restytucja określa, jak dużo prędkości normalnej obiektów pozostaje po zderzeniu. Bloki ze szkła i drewna mają skończony próg zniszczenia. Kamień, metal i aluminium korzystają z bardzo wysokiej wartości oznaczającej, że w logice gry nie powinny pękać.

### 3.4. Stały krok symulacji

Fizyka jest aktualizowana stałym krokiem `Dt = 1/60 s`, niezależnie od liczby klatek renderowania. Program dodaje czas klatki do akumulatora, a następnie wykonuje tyle kroków PhysX, ile potrzeba, aby nadrobić czas. Maksymalny czas pojedynczej klatki przekazany z pętli jest ograniczony do `0.1 s`.

> „Stały krok sprawia, że obliczenia fizyki nie zmieniają się zależnie od szybkości komputera. Grafika może renderować z różną liczbą FPS, ale silnik dostaje stałe porcje czasu”.

Przed każdym krokiem `Body::saveState()` zapamiętuje poprzednią pozycję, kąt, środek masy i prędkości. Po symulacji renderer interpoluje pozycję pomiędzy stanem poprzednim i aktualnym. Dzięki temu ruch wygląda płynniej, kiedy częstotliwość rysowania klatek nie pokrywa się z 60 Hz fizyki.

### 3.5. Kolizje i filtrowanie

`filterShader` decyduje, które pary kształtów biorą udział w kolizji. Ptaki są oznaczone grupą `BirdGroup`, a dodatkowe kształty cięgien huśtawki — grupą `GhostGroup`. Ptaki nie zderzają się między sobą, a cięgna są wyłączone z kolizji. Pozostałe pary korzystają ze standardowych kontaktów PhysX i zgłaszają punkty kontaktu oraz ich impulsy.

Cięgna są kształtami przypiętymi do tego samego dynamicznego aktora co siedzisko. Wpływają na rozkład masy i bezwładność tego ciała, lecz filtr `GhostGroup` wyłącza je z kontaktów kolizyjnych.

Ptaki i fragmenty bloków mają włączone CCD (ciągłe wykrywanie kolizji). To pomaga ograniczyć przenikanie przez cienkie przeszkody przy szybkich ruchach. Scena także ma włączoną flagę CCD. Solver dostaje podwyższone liczby iteracji pozycyjnych i prędkościowych, co wspiera stabilność stosów i połączeń.

### 3.6. Zbieranie impulsu i obrażenia

Po kroku PhysX przekazuje dane kontaktowe do `ContactCallback::onContact`, w tym impulsy wyliczone przez solver. Dla każdego punktu kontaktu kod szacuje prędkość względną ciał w tym punkcie na podstawie stanu sprzed kroku. Prędkość punktu uwzględnia ruch liniowy środka masy i ruch obrotowy:

```text
v_p = v_COM + ω × (p - COM)
```

Kontakty, dla których długość prędkości względnej jest mniejsza niż `MinImpactSpeed = 0.5`, są pomijane przy naliczaniu obrażeń. Kod porównuje długość całego wektora prędkości względnej, a nie wyłącznie jego składową normalną do powierzchni kontaktu. Impulsy PhysX z pozostałych punktów są sumowane i zapisywane w obu uczestniczących obiektach. To filtr ograniczający obrażenia od łagodnych kontaktów; nie jest to osobny model siły uderzenia.

Po kroku symulacji `applyDamageRules()` porównuje zebrany impuls z progami materiałów. Świnia ginie powyżej `PigDeathImpulse = 3`. Blok rozpada się, gdy jego impuls przekroczy `breakImpulse` danego materiału. Impulsy są zerowane po obsłużeniu każdego kroku, więc nie sumują się bez końca między odległymi zderzeniami.

**Uwaga o osiadaniu planszy:** kod ma próg `SettleTime = 1.5 s`, przed którym obrażenia miały być wyłączone, ale `Game::startLevel()` ustawia `levelTime` od razu na `1.5 s`. W aktualnym przepływie czas osiadania jest więc pominięty i reguły obrażeń mogą działać od pierwszego kroku symulacji. Próg prędkości odrzuca część łagodnych kontaktów, ale nie gwarantuje, że wszystkie początkowe kontakty zostaną zignorowane.

### 3.7. Rozpad bloków i zachowanie ruchu

Przed usunięciem pękającego bloku program zapisuje jego pozycję, kąt, prędkość liniową, prędkość kątową, wymiary i materiał. Blok jest dzielony na mniejsze prostokątne fragmenty: szkło na siatkę 2×2, pionowe elementy na 1×3, a pozostałe na 3×1.

Każdy fragment dostaje prędkość wynikającą z ruchu środka masy oraz obrotu oryginalnego bloku:

```text
v_i = v + ω × r_i
```

Wszystkie fragmenty otrzymują również pierwotną prędkość kątową. Wymiary dzielenia sprawiają, że suma objętości (a więc mas przy tej samej gęstości) odpowiada objętości bloku przed pęknięciem. W idealnym, symetrycznym podziale wzory zachowują pęd liniowy i moment pędu:

```text
Σ m_i v_i = m v,     gdy Σ m_i r_i = 0
I_bloku = Σ (I_i + m_i |r_i|²)
```

To własność zastosowanych wzorów i założeń geometrycznych, a nie gwarancja identycznego wyniku numerycznego po dalszej symulacji PhysX. Fragmenty mają flagę, która uniemożliwia ich kolejne rozpadanie się. Ciała z celowo przesuniętym środkiem masy także nie są rozbijane przez tę regułę.

### 3.8. Środek masy i niestabilne elementy

`setCom()` ustawia lokalne przesunięcie środka masy i ponownie wylicza masę oraz tensor bezwładności z gęstości materiału. W poziomie jest używany dla kostek domina i kolumny. Zmiana położenia środka masy zmienia momenty sił działające przy podparciu, a więc również równowagę i sposób przewracania się elementu. Nie zmienia ona grawitacji. Obiekty z przesuniętym środkiem masy otrzymują też flagę `noShatter`, która wyłącza ich rozpad.

### 3.9. Zawiasy, wahadło i silnik

`hinge()` tworzy przegub obrotowy PhysX. Może łączyć dwa ciała lub jedno ciało ze światem. Punkt przegubu jest podany w świecie, a następnie przeliczany do lokalnych układów aktorów. Oś przegubu ustawiana jest tak, aby dozwolony obrót odbywał się w płaszczyźnie gry wokół Z.

Wahadło składa się z metalowych ogniw połączonych zawiasami i zakończonych kulą. Huśtawka to pojedyncze dynamiczne ciało zawieszone zawiasem; jej siedzisko i dwa cięgna są kształtami jednego aktora. Cięgna mają materiał i masę, ale są oznaczone jako niekolizyjne. Wiatrak wykorzystuje zawias z napędem: silnik dąży do zadanej prędkości kątowej `1.2 rad/s`, ograniczonej maksymalnym momentem `100 N·m`.

### 3.10. Proca i zdolności ptaków

Pozycja ptaka podczas celowania jest ograniczana maksymalnym naciągiem. W kodzie maksymalny naciąg wynosi `3`, a strzał krótszy niż `0.3` jednostki jest anulowany. Prędkość początkowa jest wyliczana z uproszczonego modelu sprężyny:

```text
v = (położenie zaczepu - położenie ptaka) × √(k / m)
```

Wzór wynika z przybliżenia, że energia sprężystości `½kx²` zamienia się w energię kinetyczną `½mv²`. Masa ptaka jest obliczana z gęstości i objętości kuli. Jest to model gry, a nie sprężyna symulowana przez PhysX: zakłada natychmiastową zamianę energii, bez strat. Nadaje strzałowi kierunek przeciwny do naciągnięcia i skaluje prędkość z długością naciągu oraz masą ptaka.

Ptaki specjalne mają umiejętności:

- **Żółty** — podwaja aktualną prędkość liniową.
- **Niebieski** — zmniejsza promień oryginału i tworzy dwa dodatkowe ptaki. Przy tej samej gęstości promień jest zmniejszany przez `∛3`, więc masa każdej z trzech kul wynosi jedną trzecią masy początkowej w modelu geometrycznym. Dwie nowe kule otrzymują przeciwne składowe prędkości bocznej, które się znoszą; ich składowe prędkości wzdłuż kierunku lotu pozostają takie same jak przed podziałem.
- **Czarny** — wywołuje wybuch. Impuls działa promieniowo i maleje liniowo z odległością do `BombRadius`. Dla ciał o większej masie ten sam impuls daje mniejszą zmianę prędkości. Sam ptak jest oznaczany do usunięcia.
- **Czerwony** — nie ma dodatkowej zdolności.

### 3.11. Usuwanie obiektów i koniec rozgrywki

Obiekty przeznaczone do usunięcia są najpierw oznaczane flagą `dead`, a fizycznie usuwane z aktora PhysX i wektora dopiero po kroku symulacji. Unika to modyfikowania listy ciał w trakcie callbacków i iteracji fizyki. Ptaki mają ograniczony czas życia, a obiekty, które wypadną daleko poza planszę, są usuwane.

Wygrana następuje, kiedy nie ma żywych świń i minie `0.8 s`. Po wystrzeleniu ostatniego ptaka gra czeka, aż ruch wszystkich ptaków spadnie poniżej progów (prędkość liniowa `0.5`, kątowa `1 rad/s`) przez `3 s`; niezależny limit przegranej wynosi `14 s` od ostatniego strzału.

## 4. Budowa poziomu

Metody klasy `Levels` tworzą powtarzalne elementy z bloków i kul:

- `buildHut()` — domek z dwóch podpór, półki i świń; materiał podpór jest parametrem.
- `buildWindmill()` — obracająca się aluminiowa belka na silniku; słupek narysowany za osią jest dekoracją.
- `buildDominoes()` — rząd kamiennych kostek z podniesionym środkiem masy.
- `buildPendulum()` — łańcuch metalowych ogniw zawieszony na przegubach i zakończony kulą.
- `buildTower()` — wielopiętrowa wieża; dolne podpory są szklane, a wybrane piętra zawierają świnie.
- `buildSwing()` — huśtawka na zawiasie z dodatkową świnią.
- `buildTopHeavyColumn()` — drewniana kolumna z przesuniętym środkiem masy i świnią na górze.

`build()` ustawia te elementy od lewej do prawej w świecie o rozmiarze `32 × 18`. Osobna metoda `birdQueue()` zwraca kolejność ptaków dostępnych podczas poziomu.

## 5. Grafika, tekst i interfejs

Grafika jest dwuwymiarowa. `Graphics::layout()` dopasowuje viewport do proporcji świata, zachowując proporcje obrazu przy zmianie rozmiaru okna. `beginFrame()` ustawia projekcję ortograficzną. Prostokąty rysowane są jako obrócone quady, koła jako wielokąty z 40 segmentami, a linie jako cienkie prostokąty.

`TextRenderer` pakuje glify czcionki z pliku `PressStart2P-Regular.ttf` do atlasu 512×512 przy użyciu `stb_truetype`, a następnie wyświetla etykiety menu i HUD. Obsługiwane zakresy znaków obejmują ASCII, Latin-1 oraz część Latin Extended-A. Plik czcionki jest otwierany względem katalogu roboczego; jeśli go brakuje, tekst nie jest rysowany, ale gra może działać dalej.

Interfejs ma ekran menu, licznik ptaków i świń, przyciski ponowienia i powrotu do menu oraz nakładkę z wynikiem. Obsługiwane klawisze to Escape (powrót do menu) i R (restart poziomu podczas rozgrywki). Mysz służy do klikania, przeciągania ptaka na procy i uruchamiania zdolności.

## 6. Główna pętla programu

`main()` wykonuje kolejno następujące zadania:

1. Ustawia callback błędów GLFW i inicjalizuje GLFW.
2. Tworzy okno oraz kontekst OpenGL.
3. Inicjalizuje renderer, tekst i scenę PhysX.
4. W każdej klatce pobiera zdarzenia, dopasowuje viewport i odczytuje wejście.
5. Aktualizuje logikę gry i wykonuje stałe kroki fizyki.
6. Rysuje scenę i prezentuje klatkę.
7. Po wyjściu zwalnia obiekty PhysX, okno i GLFW.

Aktualizacja logiki jest oddzielona od rysowania. Dzięki temu renderowanie nie steruje bezpośrednio krokiem fizyki.

## 7. Ważne parametry do zapamiętania

| Parametr | Wartość | Znaczenie |
|---|---:|---|
| Rozmiar świata | `32 × 18` | Współrzędne używane przez grę i renderer |
| Grawitacja | `9.81 m/s²` w dół | Przyspieszenie w osi Y |
| Krok symulacji | `1/60 s` | Stały krok PhysX |
| Czas osiadania | `1.5 s` | Wartość progu; w aktualnym kodzie jest już zaliczona przy starcie poziomu |
| Minimalna prędkość kontaktu | `0.5` jednostki/s | Próg długości prędkości względnej używany przy naliczaniu obrażeń |
| Impuls śmierci świni | `3 N·s` | Próg zniszczenia świni |
| Promień wybuchu | `6` | Maksymalny zasięg wybuchu |
| Czas życia ptaka | `12 s` | Limit usuwający długo istniejącego ptaka |

Wartości są zapisane w `Config`, aby można je było znaleźć i zmieniać w jednym miejscu. Kod używa wartości zgodnych z konwencją SI dla grawitacji, prędkości, impulsu i momentu, jeśli przyjmiemy, że jednostka świata odpowiada metrowi. Nie ma osobnego przelicznika pikseli na metry ani kontroli jednostek — jest to interpretacja przyjętej skali, a nie mechanizm wymuszany przez program.

## 8. Uproszczenia i ograniczenia modelu

To fizyka gry, więc część zjawisk jest celowo uproszczona:

- Świat jest planarny, mimo że silnik działa w 3D.
- Siła procy jest zamieniana na prędkość początkową wzorem energetycznym; sama proca nie jest symulowana jako sprężyna w PhysX.
- Wybuch stosuje bezpośredni impuls do ciał w promieniu; kod nie sprawdza, czy między wybuchem a ciałem znajduje się przeszkoda.
- Zdolność żółtego ptaka podwaja prędkość zamiast modelować dodatkową siłę przez pewien czas.
- Punkty wizualne, takie jak słup wiatraka i część zawieszeń, są dekoracjami bez kolizji.
- Wartości progów obrażeń i parametrów materiałów są dobrane do rozgrywki; ich spójność jest ważniejsza od odwzorowania konkretnego materiału laboratoryjnego.

Na obronie warto nazywać te elementy **uproszczeniami modelu**, a nie twierdzić, że każdy efekt jest pełną symulacją rzeczywistego urządzenia.

## 9. Krótka wypowiedź podsumowująca

> „Najważniejszą część projektu stanowi połączenie logiki gry z PhysX. Ciała dynamiczne mają kształty, gęstości, materiały i ograniczone stopnie swobody, aby fizyka 3D zachowywała się jak gra 2D. Symulację wykonuję w stałym kroku. Zdarzenia kontaktu dostarczają impulsów, na podstawie których gra decyduje o zniszczeniu świń i bloków. Pękający blok zastępuję fragmentami, którym nadaję prędkości wynikające z ruchu obrotowego i liniowego oryginału. Poziom pokazuje różne zjawiska — zderzenia, tarcie, przewracanie, wahadło, zawias, napęd i przesunięty środek masy — a pozostałe klasy odpowiadają za sterowanie, grafikę, tekst i przebieg rozgrywki”.

## 10. Jak przygotować prezentację

Przed obroną warto umieć wskazać w `AngryBirds.cpp`:

1. `Config` — parametry fizyki i stały krok.
2. `Physics::init()` i `Physics::finish()` — tworzenie sceny i konfigurację ciał.
3. `ContactCallback::onContact()` oraz `Game::applyDamageRules()` — drogę od kontaktu do obrażeń.
4. `Physics::shatter()` — tworzenie fragmentów i nadawanie im ruchu.
5. `Physics::hinge()` i `Physics::motor()` — wahadło, huśtawkę i wiatrak.
6. `Game::launchVelocity()` — model strzału z procy.
7. `Game::simulate()` — stały krok fizyki i kolejność aktualizacji.

Przy omawianiu efektów dobrze pokazać konkretny element poziomu: domino dla środka masy, wahadło dla zawiasów, wiatrak dla napędu oraz pękający blok dla impulsu i rozpadu.

### Pytania, które mogą paść na obronie

- **Dlaczego zapisujesz prędkość przed krokiem PhysX?** Żeby próg kontaktu oceniać na podstawie ruchu sprzed rozwiązania zderzenia. Impuls kontaktowy pochodzi z danych PhysX.
- **Czy próg kontaktu sprawdza siłę zderzenia wzdłuż normalnej?** Nie. Kod porównuje długość całej prędkości względnej w punkcie kontaktu; następnie sumuje impulsy kontaktowe.
- **Czy proca jest symulowana fizycznie?** Nie jako osobne ciało lub sprężyna. Prędkość startowa wynika z uproszczonego bilansu energii.
- **Czy wybuch uwzględnia ściany między bombą a celem?** Nie. Impuls dostają ciała w promieniu, bez testu zasłonięcia.
- **Czy poziom ma czas na osiadanie przed naliczaniem obrażeń?** Stała `SettleTime` istnieje, ale `startLevel()` ustawia `levelTime` od razu na `1.5 s`, więc w aktualnej wersji ten okres jest pominięty.

## 11. Skrót kryteriów oceniania

Poniższe zestawienie odnosi wymagania z kryteriów do funkcji obecnych w projekcie. Nie jest obietnicą konkretnej oceny — prowadzący ocenia również stabilność działania, poprawność symulacji i to, czy student potrafi wyjaśnić zastosowane rozwiązania.

- **3.0 — podstawowa fizyka:** projekt ma dynamiczne ciała PhysX, grawitację, kolizje, statyczne podłoże i ściany oraz wpływ gracza przez naciąg i strzał z procy.
- **3.5 — właściwości fizyczne:** obiekty mają masę wynikającą z gęstości i geometrii; materiały różnią się tarciem, sprężystością i odpornością na zniszczenie. Zderzenia przekazują impulsy.
- **4.0 — złożone oddziaływania:** poziom zawiera wieloelementowe budowle, domino i niestabilne elementy z przesuniętym środkiem masy. Przewracanie oraz reakcje łańcuchowe wynikają z symulacji.
- **4.5 — mechanizmy i połączenia:** w grze są zawiasy, wahadło z połączonych ogniw, huśtawka oraz wiatrak napędzany silnikiem PhysX.
- **5.0 — zaawansowana, stabilna symulacja:** projekt ma rozbudowane konstrukcje, destrukcję bloków na fragmenty, reakcje wielu ciał oraz układ przegubów i napęd. Taki zakres odpowiada przykładowym rozwiązaniom na tę ocenę, ale jej uzyskanie zależy szczególnie od stabilności symulacji, poprawnego działania podczas prezentacji i obrony decyzji fizycznych.

**W skrócie:** projekt zawiera mechaniki odpowiadające kryteriom od `3.0` do przykładów wymienionych przy `5.0`. Najważniejsze przed oceną jest sprawdzić działanie poziomu i przygotować wyjaśnienie ograniczeń modelu, w tym pominiętego czasu osiadania konstrukcji.
