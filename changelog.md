# Changelog

## [1.4.0] - 2026-07-29

### Changed
- Konstruktor už jen ukládá parametry (bus, adresa, scl_hz) — nesahá na I2C
  sběrnici a nikdy neabortuje aplikaci
- `begin()` (nová metoda, `esp_err_t`) — zaregistruje zařízení na sběrnici a
  provede počáteční zápis. Idempotentní. Pokud zařízení na adrese neodpovídá,
  vrátí chybu, ale aplikace běží dál — chová se jako odpojený senzor
- Privátní I2C funnely (`_write`/`_read`) vrací `esp_err_t` a mají guard
  `_dev == nullptr` — pokrývá všechny veřejné metody jedním místem; před
  úspěšným `begin()` (nebo po výpadku zařízení) vrací fail-safe hodnoty
  (cache / false) bez pokusu o I2C komunikaci
- Mutex nyní staticky alokovaný (`xSemaphoreCreateMutexStatic` +
  `StaticSemaphore_t` člen) místo `xSemaphoreCreateMutex()` — nemůže selhat
  kvůli OOM heapu, žádná alokace na heapu, deterministické chování
- `begin()` nyní drží mutex po celou dobu registrace zařízení a
  počátečního zápisu — odstraněn race na `_dev`/`_data` při souběžném
  volání `begin()` (např. hot-plug retry) s ostatními metodami
- Komentáře a log/error texty v kódu přepsány do angličtiny (obecná
  konvence pro AP_ knihovny)
- Licence změněna z UNLICENSED na MIT
- `getCache()` a `readPin(pin, fromCache=true)` teď čtou `_data` pod mutexem
  (dřív bez zámku, nekonzistentní s deklarovaným thread-safe kontraktem třídy —
  nalezeno při reálném HW testu 2026-07-28)
- Zápis (`_write`) se teď ověřuje okamžitým zpětným čtením výstupních pinů
  (vstupní piny se z porovnání vynechávají — odráží reálnou vnější úroveň,
  ne to, co se do nich zapsalo) — neshoda se bere jako neúspěšný pokus a
  zopakuje se, chrání proti tiché bitové korupci na zašuměné sběrnici
- Logování při trvalé poruše (`_write`/`_read`) je teď utlumené — plné
  detaily jen pro prvních pár selhání, pak jen periodická připomínka, aby
  dlouhý nehlídaný výpadek (odpojené zařízení na dny) nezaplavil log

### Added
- ESP-IDF balení: `CMakeLists.txt`, `idf_component.yml`
- `LICENSE` (MIT)
- Volitelný výstupní parametr `bool *stale` u `readAll()`/`readPin()` —
  signalizuje, že vrácená hodnota je cache po I2C chybě (nebo timeoutu
  mutexu), ne čerstvé čtení. Odvozeno přímo z výsledku probíhající I2C
  transakce, žádná extra komunikace navíc (na rozdíl od volání `isOnline()`
  zvlášť)
- Volitelný výstupní parametr `bool *ok` u `writeAll()`/`writePin()` —
  potvrzuje, že zařízení zápis skutečně přijalo (nebo že se zápis
  přeskočil, protože cache už odpovídala). Bez toho volající (např. příkaz
  motoru přes I2C expandér) nemá šanci poznat tichý neúspěch zápisu
- Oba parametry mají default `nullptr` — nemění chování ani signaturu
  stávajících volání
- Líný, rate-limited retry registrace (`begin()`) uvnitř každé veřejné
  metody, pokud zařízení ještě není zaregistrované (`_dev == nullptr`) —
  nejvýš jednou za ~2s, ať se sběrnice nemlátí zbytečně při dlouhodobě
  odpojeném zařízení. Explicitní `begin()` zůstává vždy okamžitý (nikdy
  netlumený) - konzument tak už nemusí sám dokola volat `begin()` z
  vlastního časovače, stačí normálně používat ostatní metody
- `addReconnectListener(Listener&)` / `addDisconnectListener(Listener&)` —
  posluchači na přechod offline/online, odvození ze stejných volání, která
  aplikace dělá běžně (žádná I2C transakce navíc). Volaní po uvolnění
  mutexu, smí bezpečně volat další metody na stejné instanci. Odstraňuje
  potřebu, aby si konzument sám hlídal "bylo poslední čtení stale, je teď
  fresh" - typicky se používá k opětovnému nastavení pin mode/výstupů po
  reconnectu, protože čip po výpadku vlastního napájení zapomene svůj
  stav. Podporuje libovolný počet posluchačů přes intrusive singly-linked
  list (`Listener` uzel vlastní volající, žádná heap alokace v knihovně,
  žádný pevný strop) - pro případ, že jednu instanci sdílí víc nezávislých
  tasků nad stejným chipem (např. jeden řídí výstupy, druhý čte vstupy s
  jinou periodou), aby si navzájem nepřepsaly registraci

### Fixed
- `_read()` psal přímo do bufferu se seedovanou cache hodnotou - při
  selhávající/rušené transakci (např. brownout na sběrnici při výpadku
  napájení) mohl ISR stihnout zapsat částečný/pokažený bajt ještě předtím,
  než driver ohlásí chybu, což tiše poškodilo jinak platnou cache. Teď se
  čte do scratch bufferu a do `data`/`_data` se commitne jen při potvrzeném
  `ESP_OK` - fail-safe cache je garantovaně netknutá i při neúspěchu
  (nalezeno při reálném HW testu reconnectu 2026-07-29, projevovalo se jako
  falešný `FAULT_UNEXPECTED_MOVEMENT` u AP_ServoValve během výpadku PCF)

## [1.3.0] - 2026-04-16

### Added
- Thread-safe — FreeRTOS mutex chrání všechny I2C operace
- Retry logika — 3 pokusy s 2ms pauzou při I2C chybě, každý neúspěch zalogován
- `isOnline()` — probe dostupnosti zařízení bez změny cache
- Volitelný parametr `scl_hz` v konstruktoru (výchozí 100 kHz)
- Destruktor — uvolní `_dev` handle a mutex
- Kopírování zakázáno (`= delete`) — prevence sdílení handles
- Inicializační zápis v konstruktoru — čip je od startu ve známém stavu
- Validace čísla pinu (0–7) s logováním chyby
- Log tag obsahuje adresu zařízení (`PCF8574@0x20`) pro snadnou identifikaci

### Fixed
- `writePin()` — odstraněn zbytečný `readAll()` před zápisem (extra I2C transakce,
  při chybě čtení přepisoval `_data` nulami a mohl shodit ostatní výstupní piny)

## [1.2.1] - 2026-02-20

### Changed
- `writePin()` vraci `bool` - true pokud byl zapis proveden, false pokud byl preskocen

## [1.2.0] - 2026-02-20

### Changed
- `writePin()` preskoci I2C transakce pokud se stav pinu nezmenil (porovnani s cache pred readAll)

## [1.1.0] - 2026-02-18

### Added
- `setPinMode(uint8_t mask)` - nastaveni smeru pinu maskou (1=input, 0=output)
- `setPinMode(uint8_t pin, bool input)` - nastaveni smeru jednoho pinu
- Vstupni piny se pri kazdem `writeAll()` a `writePin()` automaticky drzi na HIGH

## [1.0.0] - 2026-02-13

### Added
- Inicializace PCF8574/PCF8574A pres ESP-IDF `i2c_master` API
- Cteni vsech 8 pinu najednou `readAll()`
- Cteni jednoho pinu `readPin(pin, fromCache)`
- Zapis vsech 8 pinu najednou `writeAll(value)`
- Zapis jednoho pinu `writePin(pin, state)` s read-modify-write
- Cache posledniho stavu `getCache()`
