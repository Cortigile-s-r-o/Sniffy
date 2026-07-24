# Uživatelský manuál Sniffy

> Tento manuál vychází z dostupného kódu, UI a dokumentace v repozitářích Sniffy k 24. 5. 2026.
>
> Rozsah: pokrývá desktopovou aplikaci Sniffy, připojení zařízení, přihlášení, firmware, session, měřicí a generační moduly a volitelnou pokročilou automatizaci přes Agent Bridge a sniffy-mcp.

![I am Sniffy the Squirrel, and I will be your companion](assets/images/low_poly_squirrel.png){.manual-scale--90}

## Právní doložka a autorská práva

Produkt vytváří, spravuje a poskytuje firma Cortigile, s.r.o.

© 2026 Cortigile, s.r.o. Všechna práva vyhrazena.
Žádná část tohoto dokumentu nesmí být reprodukována bez souhlasu autorů. Názvy produktů nebo technologií zmíněných v textu (např. STM32, Windows, Linux) mohou být chráněnými ochrannými známkami příslušných vlastníků.

**Zřeknutí se odpovědnosti:** Tento software a související firmware nástroje jsou poskytovány "tak jak jsou", bez jakýchkoliv záruk. Autoři nenesou odpovědnost za případné škody způsobené na vašem hardwaru, připojených zařízeních, měřených obvodech nebo počítači v důsledku použití těchto nástrojů. Práce s měřicí technikou a neizolovaným rozhraním vyžaduje odbornou obezřetnost uživatele.

## Důležitá bezpečnostní upozornění

Tento manuál popisuje práci se zařízením, které přímo interaguje s vnějšími elektronickými a elektrickými obvody:

- **Galvanické oddělení:** Standardní vývojové desky STM32 Nucleo připojené přes USB *nemají* měřicí obvody galvanicky oddělené od počítače. Chybné zapojení sond nebo zavlečení cizího přepětí na vstupy může zničit mikrokontrolér, USB port nebo celý počítač.
- **Limity napětí a zátěže:** Ujistěte se, že nepřekračujete maximální povolené napětí na měřicích vstupních pinech (zpravidla max. 3.3V, nebo do 5V u tolerantních pinů podle dokumentace konkrétního MCU). Dbejte také na dodržování maximálního proudového odběru na použitých propojovacích kabelech a pinech při generování a spínání napětí (Voltage source, Arb. generator atd.).
- **Zkraty a přetížení:** Vyvarujte se zkratům při propojování generujících výstupů se vstupy nebo měřeným napájením. Pokud si nejste vědomi potenciálu obvodu, piny a sondy nejprve odpojte. 

Před použitím vždy ověřte rozvržení pinů a specifikace příslušné Nucleo desky v první záložce modulu *Device*.

## 1. Co je Sniffy

Sniffy je desktopová aplikace pro STM32 Nucleo desky, která z připojeného hardware dělá laboratorní přístroj s více moduly. Podle nahraného firmware a schopností konkrétní desky umí zejména:

- rozpoznat připojenou desku a zobrazit její parametry,
- pracovat jako osciloskop,
- měřit frekvenci, periodu a časové intervaly,
- měřit napětí a logovat je do souboru,
- generovat synchronní PWM,
- generovat analogové průběhy,
- generovat digitální patterny včetně UART, SPI a I2C scénářů,
- nastavovat analogové DC výstupy,
- ukládat a obnovovat pracovní session,
- ovládat aplikaci i z externího AI/skriptovacího klienta.

Ne všechny funkce musí být aktivní na každé desce. Aplikace si při připojení čte konfiguraci zařízení a podle ní zpřístupní pouze kompatibilní moduly a parametry.

### Jak je manuál napsaný

Tento manuál je záměrně psaný prakticky:

- názvy tlačítek a stránek odpovídají tomu, co uživatel opravdu vidí v aplikaci,
- nejdřív vysvětluje běžný pracovní postup a teprve potom doplňuje pokročilejší detaily,
- u důležitých míst počítá se screenshoty,
- tam, kde se možnosti liší podle konkrétní desky, to říká otevřeně místo obecných slibů.

### Pro koho je určen

Tento manuál popisuje pouze funkce a případy použití spojené se zařízením Sniffy. Nejde o obecný výukový manuál laboratorních přístrojů a předpokládá zkušenější uživatele.

Záměrně proto nerozebírá úplné základy, například obecnou teorii režimů spouštění osciloskopu nebo chování periferií MCU. Sniffy navíc běží na různých jednočipových MCU, takže některé funkce jsou z principu kompromisem mezi schopnostmi přístroje a omezeným výkonem konkrétního hardware.

### Použité pojmy

- deska: fyzická STM32 Nucleo deska připojená k počítači,
- modul: jedna funkční část aplikace, například Oscilloscope nebo Voltmeter,
- session: uložené rozložení oken a nastavení modulů,
- pinout: grafické zobrazení pinů desky a jejich funkcí,
- demo mode: omezený režim, kdy zařízení neběží v plně ověřeném stavu.

## 2. Co budete potřebovat

### Hardware

- kompatibilní STM32 Nucleo desku s nahraným Sniffy firmware,
- USB připojení k PC,
- podle použitého modulu i připojený měřený nebo generovaný obvod.

### Software

- Windows nebo Linux,
- nainstalovanou aplikaci Sniffy,
- na Windows ST-Link ovladač, pokud ještě není v systému,
- pro firmware a licencované workflow také internetové připojení a uživatelský účet na sniffy.cz.

### Ovladače a oprávnění

- Windows: pokud jste na PC někdy používali STM32CubeIDE, Keil nebo jiné ST nástroje, ST-Link ovladač už pravděpodobně máte.
- Windows: pokud desku aplikace nenajde, doinstalujte STSW-LINK009 ze stránek ST.
- Linux: pro přístup bez sudo musí být nastavená udev pravidla pro ST-Link.

### Přehled desek v tomto buildu

V aplikaci a jejích konfiguračních podkladech jsou aktuálně připravené minimálně tyto varianty desek:

- Nucleo-C562RE,
- Nucleo-F303RE,
- Nucleo-F446RE,
- Nucleo-G474RE,
- Nucleo-L476RG.

To ještě automaticky neznamená, že každá z těchto desek bude mít ve všech sestaveních stejné moduly nebo stejné limity. O tom vždy rozhoduje až skutečně nahraný firmware a konfigurace, kterou zařízení po připojení předá aplikaci.

## 3. Instalace a první spuštění

### Windows

1. Stáhněte aktuální instalátor aplikace.
2. Dokončete instalaci standardním průvodcem.
3. Připojte Nucleo desku přes USB.
4. Spusťte Sniffy.

### Linux

1. Nainstalujte distribuční balíček aplikace.
2. Zajistěte, že má váš uživatel přístup k USB debug zařízením (doporučené jednorázové nastavení):

  ```bash
  sudo usermod -aG plugdev $USER
  ```

  Poté se odhlaste a znovu přihlaste.

3. Ověřte přístup k ST-Link bez sudo.

  ```bash
  st-info --probe
  ```

  Pokud `st-info` chybí, doinstalujte jej:

  ```bash
  sudo apt install stlink-tools
  ```

  Pokud se deska stále nezobrazuje, po přihlášení ji odpojte a znovu připojte.
4. Připojte desku a spusťte Sniffy.

### Co se stane po startu aplikace

- aplikace inicializuje témata a hlavní okno,
- po spuštění začne vyhledávat dostupná zařízení,
- pokud je k dispozici pouze jedno vhodné zařízení, může se k němu připojit automaticky,
- zobrazí levý panel modulů,
- připraví dialog přihlášení a nastavení,
- založí DeviceMediator, který obstarává sken zařízení, otevření komunikace a načtení modulů,
- podle dostupných desek nabídne připojení,
- při připojení může nabídnout obnovení poslední session pro konkrétní zařízení.

Ruční vyhledávání lze kdykoli spustit tlačítkem Scan v modulu Device.

![První spuštění aplikace s modulem Device, dostupnými zařízeními a tlačítky Connect a Scan](assets/images/fresh_connect_view.png)

## 4. Orientace v hlavním okně

Hlavní okno je rozdělené na dvě hlavní oblasti.

### Levý panel

- obsahuje tlačítka jednotlivých modulů,
- lze přepínat mezi širokým a úzkým režimem,
- dole zobrazuje verzi aplikace,
- obsahuje stav přihlášení,
- obsahuje patičku s tlačítky pro zúžení menu a otevření nastavení.

### Pravá pracovní oblast

- zobrazuje jednotlivé moduly v dock widgetech,
- po otevření modulu se v ní zobrazí jeho konkrétní UI,
- rozvržení lze přesouvat a ukládat do session,
- při změnách nebo chybách se zde mohou objevit popup hlášky.

### Důležité chování UI

- moduly se nenačítají napevno, ale podle schopností zařízení,
- některé moduly mohou být kvůli konfliktu pinů blokované,
- po flashování firmware nebo změně přihlášení může aplikace zařízení sama znovu otevřít,
- po změně tématu se může zobrazit upozornění, že je vhodný restart aplikace.

### Seznam modulů a jejich stavy

Každý firmware Sniffy nabízí několik funkcí podobných běžným laboratorním přístrojům. V aplikaci jsou tyto funkce reprezentované jako moduly. Seznam dostupných modulů se zobrazí po úspěšném připojení a může se lišit podle desky, MCU a nahraného firmware.

Kliknutím na položku v levém seznamu modul otevřete nebo přepnete jeho stav. Typicky se můžete setkat s těmito stavy:

- OFF (prázdné - bez ikony): modul je zastavený a není aktivní; kliknutím jej otevřete a spustíte.
- ON ![](../graphics/common/status_play.png){.manual-icon-inline}: modul je aktivní, běží a jeho okno je zobrazeno.
- ON HIDN ![](../graphics/common/status_play_hidden.png){.manual-icon-inline}: modul je aktivní a běží, ale jeho okno je skryté.
- IDLE ![](../graphics/common/status_pause.png){.manual-icon-inline}: modul je aktivní, ale momentálně nic nevykonává.
- IDLE HIDN ![](../graphics/common/status_pause_hidden.png){.manual-icon-inline}: modul je aktivní, ale momentálně nic nevykonává a jeho okno je skryté.
- LOCK ![](../graphics/common/status_locked.png){.manual-icon-inline}: modul nelze otevřít, protože potřebné prostředky používá jiný modul.
- PEND (animovaný kroužek): čekání na událost: modul čeká na dokončení nebo potvrzení nějaké akce.

Stav LOCK se typicky týká sdílených prostředků, například časovačů, ADC nebo sdílené paměti.

![Levé menu s dostupnými moduly a jejich aktuálními stavy](assets/images/modules_list_menu.png){.manual-image--panel .manual-scale--85}

### Okna modulů

Každý modul se otevírá v novém dokovatelném okně. Rozvržení si tak můžete upravit uvnitř hlavního okna, nebo si jednotlivé moduly odpojit jako samostatná okna. Dokování a oddokování funguje stejně jako u běžných desktopových aplikací ve Windows, včetně ovládání přes pravou horní část záhlaví okna.

![Rozvržení s více dokovanými okny modulů uvnitř hlavního okna](assets/images/dock_windows.png)

![Stejné rozvržení s oddokovanými okny modulů](assets/images/undock_windows.png)

### Ovládací prvky a zadávání hodnot

GUI používá malou sadu sjednocených ovládacích prvků, které se opakují napříč moduly. Tlačítka, záložky a běžné přepínače jsou záměrně navržené jednoduše, proto stojí za popis hlavně specifičtější vstupní prvky.

<div class="manual-image-stack">
  <table class="manual-image-grid">
    <tr>
      <td><img class="manual-scale--75 manual-image manual-image--standard" src="assets/images/combo_control_return.png" alt="Výběrový ovladač pro změnu hodnoty" /></td>
      <td><img class="manual-scale--75 manual-image manual-image--standard" src="assets/images/combo_control.png" alt="Výběrový ovladač pro změnu hodnoty" /></td>
    </tr>
  </table>
</div>

#### Textový nebo číselný vstup

V některých polích lze zapisovat text nebo čísla přímo. Desetinné hodnoty přijímají lokální oddělovač i tečku, takže české i anglické zadání funguje stejně. Čísla lze zapisovat i se zkrácenými násobnými jednotkami, například 9.9k, 9k9 a 9900 mohou představovat stejnou platnou hodnotu.

![Textový vstup s přímým zadáním hodnot a jednotek](assets/images/textbox_entry_units.png){.manual-scale--60}

#### Ciferníkový vstup

Ciferníkový vstup je jedním z nejpoužívanějších ovladačů ve Sniffy. Existují dvě základní varianty: jedna pracuje s pevnými možnostmi a druhá dovoluje nastavit libovolnou číselnou hodnotu.

- tlačítka: zvyšují nebo snižují hodnotu,
- výběr: přepíná základní jednotku nebo volí hodnotu z předdefinovaných možností,
- textová hodnota: u číselného ciferníku ji lze přepsat přímo,
- ciferník: lze jej ovládat kolečkem myši pro rychlé jemné změny nebo tažením myši pro větší skoky.

Při tažení myší se změna hodnoty odvíjí od směru a vzdálenosti od diagonální osy, takže jde o rychlý způsob, jak se dostat na hrubý rozsah a pak hodnotu doladit.

![Ciferníkový ovladač s interakcí myší](assets/images/rotary_knob_control.png){.manual-scale--50}

#### Společné ovládání grafů

Některé moduly používají společný interaktivní graf, takže se stejné drobné ovladače opakují například v Oscilloscope, FFT pohledu, Sync PWM nebo v generátorech.

- v pravém horním rohu grafu je malé kolečko pro nastavení výraznosti mřížky; každým kliknutím se opacity gridu zvýší o další krok a po dosažení 100 % se dalším kliknutím vrátí na 0 %, takže mřížku lze úplně skrýt nebo naopak zobrazit co nejvýrazněji,
- v pravém dolním rohu grafu je ovladač tloušťky datových křivek; klikáním se cyklicky přepínají tři šířky data series: úzká, středně široká a široká.

## 5. Připojení zařízení

### Postup připojení

1. Připojte desku přes USB.
2. Otevřete modul Device.
3. Stiskněte Scan, pokud se deska neobjeví automaticky.
4. V seznamu vyberte nalezené zařízení.
5. Stiskněte Connect.

### Co aplikace dělá po připojení

- otevře komunikaci s deskou,
- provede reset zařízení do známého stavu,
- pošle do zařízení identitu uživatele a token, pokud je uživatel přihlášený,
- čeká na potvrzení tokenu nebo demo režimu,
- načte konfiguraci zařízení a připravené moduly,
- případně nabídne obnovu poslední session pro danou desku.

### Demo režim

Pokud firmware vrátí potvrzení v demo režimu, aplikace zobrazí hlášku Running in demo mode. To je důležité hlavně v situacích, kdy:

- zařízení nemá platné licenční ověření,
- token nebyl přijat,
- některé funkce mají být omezené nebo neaktivní.

### Modul Device

Modul Device je první a klíčový modul. Zobrazuje:

- seznam dostupných zařízení,
- tlačítka Connect a Scan,
- informace o zařízení, firmware a protokolu,
- mapování pinů pro aktivní moduly,
- grafiku desky a její pinout.

### Přepínání obrázku desky a pinoutu

V modulu Device lze pravým kliknutím na obrázek desky přepnout z běžného pohledu do pinoutu. Stejným pravým kliknutím v pinoutu se vrátíte zpět na obrázek desky. Pokud má aplikace k dispozici vektorový pinout, zobrazí ho přímo s aktivními funkcemi pinů podle zapnutých modulů.

Ve vektorovém pinoutu lze pohled přiblížit nebo oddálit kolečkem myši. Podržením levého tlačítka myši a tažením se pinout posouvá. Dvojklik obnoví výchozí oddálený pohled, ve kterém je pinout přizpůsobený velikosti panelu.

To je praktické hlavně při ověřování konfliktů mezi moduly a při zapojování sond, generátorů nebo digitálních linek.

### Jak poznat, že je zařízení opravdu připravené k práci

Za běžného stavu platí, že je zařízení připravené k práci, když:

- v modulu Device vidíte konkrétní název desky místo prázdného nebo skenovacího stavu,
- seznam modulů odpovídá připojenému hardware,
- Device zobrazuje specifikace a pinout místo výchozího obrázku bez zařízení,
- aplikace po připojení nehází opakovaně chybová hlášení,
- u chráněných workflow máte platný login, nebo aspoň vědomě pracujete v demo mode.

### Jak číst názvy pinů v aplikaci

Názvy pinů, které vidíte v modulu Device a v popisech jednotlivých modulů, se nenačítají natvrdo z textu manuálu, ale přicházejí z konfigurace a specifikace zařízení.

Prakticky to znamená:

- stejný modul může mít na různých deskách jiné piny,
- počet kanálů se může lišit podle desky,
- pin označený pomlčkou nebo chybějící položkou obvykle znamená, že daný výstup nebo vstup pro aktuální kombinaci není dostupný,
- nejbezpečnější je vždy řídit se tím, co ukazuje aktuální pinout v modulu Device, ne starším screenshotem z jiné desky.

![Modul Device po připojení s pinoutem a routováním aktivních funkcí](assets/images/pinout_routing.png)

## 6. Přihlášení a licence

Přihlášení řeší samostatný login dialog a serverová autentizace na sniffy.cz. Desktopová aplikace nepracuje s heslem přímo; místo toho spouští přihlášení v prohlížeči.

### Jak login funguje z pohledu uživatele

1. Klikněte na stav přihlášení vlevo dole.
2. Zadejte e-mail.
3. Klikněte na Check Login.
4. Pokud ještě nejste ověření, aplikace otevře výchozí prohlížeč a přesměruje vás na přihlášení na sniffy.cz.
5. Po úspěšném přihlášení aplikace sama průběžně kontroluje stav a uloží token.

### Co dialog zobrazuje

- textové pole pro e-mail,
- odkaz pro registraci na sniffy.cz,
- stavovou zprávu,
- tlačítko Check Login,
- tlačítko Close,
- při platném loginu také tlačítko Logout.

### Platnost tokenu

- token je vydaný na 1 týden,
- aplikace ukládá jeho platnost lokálně,
- při vypršení platnosti nebo při neplatném session bude firmware update nebo jiná chráněná akce požadovat nové přihlášení.

### Kdy je login důležitý

- při stahování kompatibilního firmware ze serveru,
- při obnovování session navázaných na licencované zařízení,
- při handshaku mezi aplikací a firmware, pokud zařízení vyžaduje ověření.

![Login dialog pro neznámého nebo dosud neověřeného uživatele](assets/images/login_unknown_user.png){.manual-scale--80}

![Login dialog pro již přihlášeného uživatele s uloženým tokenem](assets/images/login_logged_in.png){.manual-scale--80}

## 7. Nastavení aplikace

Dialog nastavení má čtyři stránky: General, Appearance, Firmware a About.

### 7.1 General

V sekci General najdete správu session.

#### Restore session after startup

Možnosti jsou tři:

- No: session se po připojení automaticky neobnovuje,
- Always ask: aplikace se zeptá, zda obnovit poslední session pro konkrétní zařízení,
- Yes: session se obnovuje automaticky.

#### Smart session layout and geometry

Možnosti:

- Off: při načtení session se obnoví i uložená pozice a rozměry okna,
- On: aplikace se pokusí zachovat aktuální umístění okna a použít jen vhodnou velikost a rozvržení.

#### Session

Tlačítka:

- Save: uloží aktuální session do JSON souboru,
- Load: načte dříve uloženou JSON session.

### 7.2 Appearance

Sekce Appearance umožňuje výběr motivu aplikace. V aktuálním kódu jsou připravené tyto motivy:

- Dark,
- Light,
- Dawn,
- Greenfield,
- MS-DOS,
- Ember.

Po změně tématu může aplikace upozornit, že je vhodný restart, aby se nový vzhled projevil konzistentně.

### 7.3 Firmware

Sekce Firmware obsahuje:

- tlačítko Install / Flash,
- progress bar,
- log průběhu operace,
- tlačítko Mass Erase.

Volba zdroje firmware je v interní implementaci přítomná, ale v aktuálním UI je skrytá. Z pohledu uživatele tedy firmware update funguje jako jedna inteligentní akce: aplikace sama rozhodne, zda použije kompatibilní lokální cache, nebo stáhne nový firmware.

### 7.4 About

Sekce About obsahuje:

- verzi aplikace,
- odkaz na přehled open-source notices,
- tlačítko Third-party licenses.

![Dialog Settings na stránce General se správou session](assets/images/settings_dialog_general.png){.manual-scale--80}

![Dialog Settings na stránce Appearance se seznamem témat](assets/images/settings_dialog_appearance.png){.manual-scale--80}

## 8. Aktualizace firmware

Firmware update je navržený tak, aby byl pro uživatele co nejjednodušší, ale zároveň bezpečný vůči nekompatibilním verzím.

### Typický postup

1. Přihlaste se.
2. Otevřete Settings.
3. Přejděte na Firmware.
4. Klikněte na Install / Flash.
5. Sledujte progress bar a log.
6. Po dokončení nechte aplikaci zařízení znovu otevřít nebo desku znovu připojte.

### Co aplikace kontroluje na pozadí

- připojení ke ST-Link,
- MCU UID a typ MCU,
- dostupnost manifestu nejnovějšího firmware,
- kompatibilitu firmware s verzí desktop aplikace,
- kompatibilitu protokolu,
- platnost uživatelského loginu,
- dostupnost lokálně cachované kompatibilní binárky.

### Důležité vlastnosti

- firmware se cacheuje per MCU UID,
- binárka je svázaná s konkrétním MCU UID,
- kompatibilní lokální cache může být použita místo nového stahování,
- nekompatibilní nebo chybějící metadata firmware aplikace odmítne,
- pokud není uživatel přihlášený, firmware update skončí výzvou k loginu.

### Mass Erase

Mass Erase:

- smaže firmware z zařízení,
- používá ST-Link mimo běžnou komunikační cestu,
- po dokončení zařízení odpojí z běžné aplikace,
- je vhodné používat jen tehdy, když opravdu potřebujete čistý stav.

> Pozor: Mass Erase je destruktivní operace. Po jejím dokončení nebude na desce funkční Sniffy firmware, dokud ho znovu nenahrajete.

![Dialog Settings na stránce Firmware s flashováním a logem průběhu](assets/images/settings_dialog_flash.png){.manual-scale--80}

## 9. Přehled modulů

| Modul | Účel | Typické použití |
| --- | --- | --- |
| Device | informace o desce a pinoutu | výběr zařízení, kontrola pinů |
| Oscilloscope | akvizice analogových signálů | měření průběhu, trigger, FFT |
| Counter | přesné měření frekvence a časů | HF, LF, ratio, intervaly |
| Voltmeter | měření napětí | sledování DC/AC hodnot, logování |
| Sync PWM | synchronní PWM výstupy | řízení motorů, časově svázané PWM |
| Arb. generator | analogové průběhy | sinus, pila, obdélník, vlastní data |
| PWM generator | PWM generování přes variantu generátoru | PWM s doplňkovými parametry |
| Pattern gen. | digitální patterny a protokoly | UART, SPI, I2C, kódování |
| Voltage source | DC napěťové výstupy | reference, bias, testování |

### Ne každá deska nabízí stejné moduly a limity

Přehled modulů výše je záměrně obecný. V praxi se mezi deskami liší zejména:

- počet dostupných kanálů,
- konkrétní piny přiřazené jednotlivým funkcím,
- maximální frekvence,
- velikost paměti pro akvizici nebo generování,
- dostupnost speciálních režimů.

Proto má smysl před první prací s novou deskou vždy otevřít modul Device a projít si:

- počet kanálů,
- seznam pinů,
- maximální hodnoty,
- případné omezení nebo konflikty mezi moduly.

## 10. Modul Oscilloscope

Oscilloscope je nejbohatší modul v aplikaci. Je rozdělený do pěti záložek: Set, Meas, Cursors, Math a Adv.

### 10.1 Záložka Set

V záložce Set nastavíte:

- časovou základnu,
- pretrigger,
- aktivní kanály CH1 až CH4,
- trigger mode: Auto, Normal, Single, Stop,
- trigger edge: rising nebo falling,
- trigger channel,
- trigger level,
- memory policy,
- vertikální měřítko a posun pro vybraný kanál,
- práci s matematickým kanálem.

### 10.2 Záložka Meas

Záložka Meas přidává měření nad daty. K dispozici jsou alespoň tyto veličiny:

- RMS,
- RMS-AC,
- Mean,
- Min,
- Max,
- Peak-to-peak,
- Frequency,
- Period,
- Duty,
- High,
- Low,
- Phase,
- Clear all.

### 10.3 Záložka Cursors

Umožňuje pracovat s kurzory v časové a amplitudové oblasti. K dispozici jsou:

- horizontální kurzory A a B,
- vertikální kurzory A a B,
- kurzory i pro FFT pohled,
- volba typu kurzorů a přiřazeného kanálu.

### 10.4 Záložka Math

Math umožňuje dva typy práce:

- symbolický výpočet nad kanály,
- FFT.

Ve FFT části lze nastavovat:

- vstupní kanál,
- typ okna,
- způsob zobrazení,
- délku FFT,
- vertikální škálování.

### 10.5 Záložka Adv

V Advanced části je možné nastavovat:

- rozlišení,
- vlastní sampling frequency,
- vlastní data length,
- pracovní režim včetně XY zobrazení,
- volbu os X a Y v XY módu.

### Typický pracovní postup

1. Otevřete Oscilloscope.
2. Zapněte požadované kanály.
3. Nastavte time base a trigger.
4. Sledujte průběh na grafu.
5. Přidejte měření nebo FFT podle potřeby.

### Na co si dát pozor

- ne všechny kombinace kanálů a délek paměti jsou vhodné pro FFT,
- chybný matematický výraz vede k neplatnému math trace,
- XY mód dává smysl jen se dvěma validními kanály,
- rozsah a rychlost akvizice závisí na hardware.

![Oscilloscope v běžném režimu s aktivními kanály a záložkou Set](assets/images/scope_welcome_view.png)

![Oscilloscope v pokročilém použití s rozšířenými matematickými funkcemi](assets/images/scope_math_extension.png)

## 11. Modul Counter

Counter je rozdělený do čtyř měřicích režimů: HF, LF, Ratio a Intervals.

### HF

Použijte pro vysokofrekvenční měření. Typicky nastavujete:

- měřenou veličinu Frequency nebo Period,
- averaging nebo chování chybového zobrazení,
- gate time.

### LF

Použijte pro pomalejší signály. K dispozici bývá:

- volba kanálu,
- Frequency nebo Period,
- počet vzorků,
- volba multiplier,
- zobrazení duty cycle.

### Ratio

Vhodné pro poměr dvou frekvencí nebo souvisejících vstupů. Důležitý je hlavně počet vzorků a stabilita referenčního vstupu.

### Intervals

Použijte pro měření času mezi dvěma událostmi. Nastavujete:

- pořadí událostí,
- hrany A a B,
- timeout.

### Typický pracovní postup

1. Otevřete Counter.
2. Vyberte režim.
3. Nastavte kanál nebo parametry měření.
4. Spusťte měření.
5. Sledujte hlavní hodnotu i historii.

### Poznámky

- dostupné maxima závisejí na boardu,
- intervalové měření má timeout až do hodinových hodnot,
- při špatné kvalitě signálu nebo nevhodném vstupu mohou být výsledky nestabilní.

![Modul Counter v HF režimu s hlavní hodnotou, historií a ovládacími prvky](assets/images/counter_hf_mode.png){.manual-scale--95}

## 12. Modul Voltmeter

Voltmeter je postavený na stejné akviziční základně jako osciloskop, a proto s ním sdílí prostředky. Vzorkovací frekvence ADC je 5 ksps, délka dat je 200 vzorků a vzorkovací doba kanálu je pevně nastavená na 40 ms. Zobrazená hodnota odpovídá průměru měřeného signálu.

- Je možné zobrazit min/max naměřené hodnoty nebo zvlnění napětí a jeho frekvenci.
- Vdd se vzorkuje jednou za N vzorků, aby bylo možné měření průběžně kalibrovat.
- S vyšším počtem vzorků pro průměrování se Vdd vzorkuje po delší dobu a výsledná přesnost bývá lepší.
- Vzorkování Vdd lze přeskočit zapnutím rychlého režimu Fast.

Voltmeter má dvě záložky: Control a Data log.

### Control

V Control části můžete:

- zapnout nebo vypnout jednotlivé kanály,
- přepnout Speed mezi Normal a Fast,
- nastavit averaging od 1 do 64 vzorků,
- zvolit způsob zobrazení doplňkových údajů: Min/Max, Ripple nebo None,
- sledovat aktuální Vcc,
- sledovat průběžný progress.

### Data log

V Data log části můžete:

- vybrat soubor pro záznam,
- vidět cestu k vybranému souboru,
- spustit a zastavit logování.

Záznam probíhá do standardního CSV souboru. Datalog obsahuje datum a čas pořízení vzorku, odpovídající Vdd a naměřené hodnoty jednotlivých kanálů.

### Co se ukládá

Log obsahuje na každém řádku alespoň:

- index vzorku,
- datum,
- čas s milisekundami,
- aktuální Vdd,
- hodnoty jednotlivých kanálů.

### Typický pracovní postup

1. Otevřete Voltmeter.
2. Zapněte potřebné kanály.
3. Nastavte averaging a režim zobrazení.
4. Pro logování přejděte do Data log.
5. Vyberte soubor a spusťte Start.

### Poznámky

- bez vybraného souboru logování nespustíte,
- při novém startu logování se soubor přepisuje,
- kvalita měření závisí na stabilitě reference a zapojení.

![Voltmeter s vícekanálovým měřením a zobrazením Vcc](assets/images/voltmeter_view.png){.manual-scale--80}

![Voltmeter během aktivního logování dat](assets/images/voltmeter_active_logging.png){.manual-scale--80}

<div class="manual-table-half" markdown="1">

| sample ID | date       | time         | Vdd   | Voltage CH1 | Voltage CH2 | Voltage CH3 | Voltage CH4 |
| --------- | ---------- | ------------ | ----- | ----------- | ----------- | ----------- | ----------- |
| 0         | 01.04.2021 | 11:39:59.611 | 3.323 | 0.0682914   | 0.0686606   | 0.209109    | 1.4729      |
| 1         | 01.04.2021 | 11:40:00.736 | 3.323 | 0.0718227   | 0.0728447   | 0.222619    | 1.57114     |
| 2         | 01.04.2021 | 11:40:01.541 | 3.323 | 0.0695935   | 0.06956     | 0.221218    | 1.57103     |
| 3         | 01.04.2021 | 11:40:01.874 | 3.324 | 0.0687527   | 0.0712899   | 0.223016    | 1.57208     |

<div class="manual-caption">Ukázka výsledného CSV datalogu z modulu Voltmeter</div>
</div>

## 13. Modul Sync PWM

Sync PWM slouží pro synchronní PWM výstupy na více kanálech.

### Hlavní funkce

- frekvence pro jednotlivé kanály,
- duty cycle,
- fáze,
- zapínání a vypínání kanálů,
- náhled průběhu v grafu,
- equidistant rozložení fází,
- podle varianty hardware také invertování nebo závislé kanály.

### Kdy se hodí

- synchronní vícekanálové PWM,
- časově svázané řídicí signály,
- experimenty s fázovým posunem.

### Na co si dát pozor

- některé desky mohou mít kanály navázané nebo sdílené,
- u závislých kanálů nemusí být všechny ovladače aktivní,
- skutečný rozsah frekvencí a přesnost určuje hardware.

![Modul Sync PWM s více kanály a náhledem průběhů](assets/images/syncpwm_view.png){.manual-scale--85}

## 14. Moduly Arb. generator a PWM generator

V aplikaci jsou dvě příbuzné generátorové varianty.

### Arb. generator

Slouží pro analogové nebo obecné průběhy.

#### Princip generování

Generátor využívá časovače, DMA a DAC k tomu, aby analogový signál generoval přímo hardwarově. Signál je v paměti reprezentován jako pole vzorků a při každém přetečení čítače se další vzorek odešle do DAC.

- Tento přístup nevyžaduje prakticky žádný procesorový čas MCU.
- DDS se zde nepoužívá, protože by spotřebovávalo CPU čas.
- DAC v MCU mají omezené možnosti z hlediska maximální rychlosti generování a výstupního proudu.
- Když se signál blíží napájecímu napětí nebo GND, může být zkreslený, protože výstupní buffer DAC neumí jít zcela k okrajům rozsahu.

#### Hlavní funkce

- tvar průběhu: sinus, pila, obdélník, arbitrary z datového souboru,
- frequency,
- amplitude,
- offset,
- phase,
- duty cycle pro obdélník,
- počet aktivních kanálů,
- memory modes: Best fit, Long, Custom,
- software sweep,
- synchronizace kanálů vůči CH1,
- načtení vlastního waveform souboru,
- progress během generování.

#### Graf signálu

Graf ukazuje, jak budou průběhy vypadat při generování přes DAC. Standardně se zobrazuje jedna perioda signálu, což je užitečné hlavně při porovnání tvaru, offsetu a amplitudy.

![Arbitrary generator s ovládáním průběhu a hlavním náhledem signálu](assets/images/arbgen_view.png){.manual-scale--85}

![Graf reálného DAC výstupu generátoru](assets/images/arbgen_real_dac_out.png){.manual-scale--85}

#### Délka paměti

Kvůli principu generování nelze při plné délce paměti vždy nastavit frekvenci úplně přesně, protože hodinový signál MCU se dělí celočíselně. Přesnější frekvenci lze často získat právě úpravou délky paměti.

Dostupné režimy jsou:

- Best Fit: délka bufferu se upraví tak, aby chyba nastavené frekvence byla co nejmenší; minimální délka bývá omezená na přibližně čtvrtinu maximálního bufferu,
- Long: použije se nejdelší možná délka paměti pro danou frekvenci,
- Custom: uživatel ručně určí počet vzorků, které se mají generovat.

#### Změna frekvence za běhu

Výstupní frekvenci lze měnit i během generování. V takovém případě se délka paměti uzamkne a mění se hlavně vzorkovací frekvence. Tím pádem je změna frekvence praktická, ale ne vždy zcela přesná.

Maximální frekvence je omezená maximální vzorkovací frekvencí DAC a délkou paměti:

$$
f_{max} = \frac{f_{s,DAC}}{n}
$$

Například při maximální vzorkovací frekvenci DAC 2 MSPS a délce paměti 1000 vzorků vychází maximální frekvence výstupního signálu přibližně 2 kHz.

#### Synchronizace frekvence a fáze

Pokud používáte více kanálů, generátor umí frekvenční synchronizaci s kanálem CH1. Podle nastavení GUI lze zároveň synchronizovat i fázi.

Při změně frekvence nebo při sweepu se může zejména ve vyšších frekvencích stát, že fázová synchronizace mírně ujede. Pokud k tomu dojde, obvykle pomůže synchronizaci příslušného kanálu vypnout a znovu zapnout.

#### Rozmítání frekvence

Sweep je implementovaný softwarově a generovací frekvence se aktualizuje přibližně každých 50 ms. Ovlivňuje primárně kanál CH1 a ostatní kanály se mohou připojit přes synchronizaci frekvence.

Parametry sweepu lze měnit i za běhu, ale stále platí omezení vycházející z maximální frekvence DAC a zvolené délky paměti.

#### Vstup z vlastního souboru

Při volbě typu Arbitrary umí GUI načíst soubory CSV nebo TXT a parser se snaží rozpoznat běžné varianty formátu automaticky.

Rozpoznává zejména:

- první řádek jako data nebo jako hlavičku,
- oddělovače hodnot, například čárku nebo středník,
- desetinnou čárku i desetinnou tečku,
- více kanálů s různou délkou,
- data vyjádřená v napětí nebo jako surové DAC hodnoty,
- časovou osu v prvním sloupci, pokud jsou hodnoty ekvidistantní.

Níže jsou čtyři praktické varianty, které odpovídají ukázkám v přiložených screenshotech i aktuální implementaci loaderu.

#### Příklad 1: časová osa + jeden kanál

<div class="manual-table-half" markdown="1">

| time | data |
| --- | --- |
| 0.01 | 0.2 |
| 0.02 | 0.5 |
| 0.03 | 1 |
| 0.04 | 1.5 |

</div>

Tento formát je vhodný pro jeden analogový kanál. Pokud je první sloupec ekvidistantní, aplikace z něj odvodí sample rate a do samotných dat už ho nezapočítá jako kanál.

#### Příklad 2: časová osa + dva kanály

<div class="manual-table-half" markdown="1">

| time | data | data2 |
| --- | --- | --- |
| 0,01 | 0,2 | 2 |
| 0,02 | 0,5 | 3 |
| 0.03 | 1 | 1 |
| 0.04 | 1.5 |  |
| 0.05 | 1.9 |  |

</div>

Tady jsou vidět dvě důležité vlastnosti parseru: umí kombinaci desetinné čárky i tečky a podporuje víc kanálů v jednom souboru. V praxi mohou mít jednotlivé kanály i různou délku.

#### Příklad 3: jeden sloupec surových DAC hodnot

<div class="manual-table-half" markdown="1">

| DAC |
| --- |
| 512 |
| 1024 |
| 2048 |
| 1536 |
| 1024 |

</div>

Pokud hodnoty výrazně přesahují běžný napěťový rozsah, loader je vyhodnotí jako surové DAC kódy a přepočítá je podle zvoleného rozlišení a výstupního rozsahu.

#### Příklad 4: jeden sloupec napěťových hodnot

<div class="manual-table-half" markdown="1">

| data |
| --- |
| 0,1 |
| 0.8 |
| 1.03 |
| 0.04 |

</div>

Nejjednodušší varianta pro jeden kanál bez hlavičky a bez časové osy. Opět funguje desetinná čárka i tečka, takže se hodí pro ručně psané krátké waveform soubory.

### PWM generator

PWM generator využívá příbuzný základ, ale orientuje se na PWM generování. V praxi může přidat nebo zvýraznit parametry jako PWM frequency a synchronizaci PWM parametrů.

### Typický pracovní postup

1. Otevřete Arb. generator nebo PWM generator.
2. Zvolte tvar signálu.
3. Nastavte frekvenci, amplitudu, offset a případně fázi.
4. U arbitrary módu načtěte vlastní soubor.
5. Spusťte generování a sledujte progress.

### Poznámky

- při běžícím generování nemusí jít měnit všechny parametry,
- vlastní průběh musí respektovat kapacitu bufferu,
- sweep a synchronizace jsou výkonné, ale závislé na konkrétním hardware.

![PWM generator se sweepem a specifickým nastavením PWM parametrů](assets/images/pwmgen_view.png)

## 15. Modul Pattern gen.

Pattern gen. je určený pro digitální patterny a protokolové scénáře. Jde o jeden z nejuniverzálnějších modulů, protože kombinuje běžné logické vzory i komunikační protokoly.

### Hlavní ovládací prvky

- výběr patternu,
- frekvence,
- délka nebo počet kanálů,
- reset patternu,
- generování a průběhový progress,
- speciální nastavení podle zvoleného patternu.

### Dostupné patterny

- User Defined,
- Counter Clock,
- Binary Code,
- Gray Code,
- Quadrature,
- PRBS,
- PWM,
- Line Code,
- 4B/5B,
- Johnson N-Phase,
- PDM,
- Parallel Bus,
- UART,
- SPI,
- I2C.

### Patterny a jejich typické parametry

#### User Defined

- ruční editace pattern gridu.

#### Quadrature

- volba sekvence A->B nebo B->A,
- vhodné pro enkodérové scénáře.

#### PRBS

- volba řádu pseudo-náhodné sekvence.

#### Line Code

Podporované typy z kódu zahrnují minimálně:

- NRZ-L,
- RZ,
- Manchester,
- Miller,
- BiPhaseMark,
- BiPhaseSpace.

#### UART

Typické volby:

- baud rate,
- počet datových bitů,
- parity,
- počet stop bitů,
- bit order,
- idle level,
- framing error,
- break.

#### SPI

Typické volby:

- SPI mode,
- word size,
- bit order,
- CS gating,
- pause between transfers.

#### I2C

Typické volby:

- clock frequency,
- address mode 7bit nebo 10bit,
- adresa,
- read/write,
- ACK,
- clock stretching,
- repeated start.

Při zvolení I2C patternu aplikace podle kódu přepíná příslušné piny do open-drain chování, a při opuštění I2C patternu je vrací zpět.

### Na co si dát pozor

- složitější patterny mohou významně měnit efektivní délku výstupu,
- protokoly UART, SPI a I2C mají své vlastní timing limity,
- dostupný počet linek závisí na desce a firmware.

![Pattern generator s výběrem paternu a panelem nastavení](assets/images/pattgen_pattern_list.png)

## 16. Modul Voltage source

Voltage source slouží k nastavování analogových DC úrovní na dostupných kanálech.

### Hlavní funkce

- nastavení výstupního napětí pro každý kanál,
- zapínání kanálů,
- přehledné zobrazení hodnoty a relativní úrovně,
- zobrazení aktuální reference Vcc.

### Typické použití

- biasování vstupů,
- jednoduchý referenční zdroj,
- testování analogové části obvodu.

### Omezení

- rozsah napětí určuje DAC a napájecí/reference desky,
- záporné napětí nepodporuje,
- výstup je přímo závislý na fyzickém zatížení výstupu.

![Voltage source s více kanály a ovládáním napěťových úrovní](assets/images/voltage_source_view.png){.manual-scale--80}

## 17. Konflikty modulů a pinů

Sniffy sleduje aktivní využití zdrojů a pinů. To má pro uživatele praktické dopady:

- některé moduly nelze používat současně,
- zapnutí jednoho modulu může dočasně zablokovat jiný,
- pinout v modulu Device pomáhá konflikt najít,
- při změně aktivních modulů se mapování pinů aktualizuje.

Pokud se modul nechová podle očekávání, vždy nejdřív zkontrolujte:

- zda už jiný modul neobsadil potřebný pin,
- zda je modul skutečně aktivní,
- zda je připojená správná deska a správný firmware.

> *POZN: Hardwarové kolize s výbavou desky*
>
> *Na vývojových deskách Nucleo bývá často z výroby osazena uživatelská LED dioda, která je pevně spojená s konkrétním pinem mikrokontroléru. Pokud je funkce aktivního modulu (například výstup Arb. generátoru) namapována právě na tento pin, připájená LED začne působit jako zátěž. Výsledným symptomem bývá znatelně oříznuté výstupní napětí na daném kanálu a fakt, že LED na desce svítí nebo poblikává. Pokud na takovém pinu potřebujete pracovat s plným napěťovým rozsahem nebo bez zkreslení, je nutné LED diodu (případně její předřadný rezistor či odpovídající propojku) z desky opatrně odpájet.*

## 18. Session a rozložení pracovního prostoru

### Co session obsahuje

Session ukládá:

- geometrii hlavního okna,
- stav dock widgetů,
- rozložení jednotlivých modulů,
- konfigurace modulů,
- stav aktivace modulů,
- stav levého menu.

### Automatické ukládání

Při ukončení práce s připojeným zařízením se session ukládá automaticky pod jménem zařízení do aplikační konfigurační složky.

### Ruční ukládání a načtení

V Settings -> General lze session ručně:

- uložit do JSON souboru,
- načíst z JSON souboru.

### Obnova po připojení

Po připojení zařízení může aplikace:

- neobnovit nic,
- zeptat se,
- obnovit session automaticky.

To se řídí volbou Restore session after startup.

### Kde aplikace ukládá lokální soubory

Sniffy používá standardní aplikační konfigurační adresář operačního systému. Do něj ukládá několik důležitých souborů:

- `settings.ini`: základní nastavení aplikace, například motiv, volbu obnovy session, e-mail, token a jeho platnost,
- `sessions/<název_zařízení>.json`: automaticky nebo ručně uložené session pro konkrétní desku,
- `<MCU_UID>.bin`: lokální cache firmware staženého pro konkrétní zařízení,
- `<MCU_UID>.manifest.json`: metadata k odpovídající cachované binárce.

Z uživatelského pohledu je dobré vědět:

- session jsou uložené per zařízení, takže dvě různé desky mohou mít rozdílné rozložení i konfigurace,
- firmware cache je svázaná s konkrétním MCU UID,
- přenesení těchto souborů mezi různými deskami nedává smysl,
- pokud řešíte rozbitou session nebo podivné staré rozložení, bývá nejrychlejší zkontrolovat/smazat právě tyto lokální soubory.

### Kdy dává smysl lokální soubory smazat nebo vyčistit

Ruční zásah do lokálních souborů má smysl hlavně v těchto situacích:

- session se načítá chybně po větší změně verze aplikace,
- chcete začít s čistým rozložením bez starých docků a pozic,
- testujete více variant firmware a nechcete používat starou cache,
- přecházíte na jinou pracovní konfiguraci a chcete si odstranit staré uživatelské stopy.

> Doporučení: pokud mažete session nebo firmware cache ručně, dělejte to vždy při zavřené aplikaci.

![Dialog s dotazem na obnovu uložené session](assets/images/session_restore_quest.png)

## 19. Řešení typických problémů

### Aplikace nevidí desku

Zkontrolujte:

- USB kabel a napájení,
- ST-Link ovladač,
- u Linuxu udev pravidla,
- že deska opravdu běží se Sniffy firmware.

### Aplikace hlásí demo mode

To znamená, že firmware nepotvrdil plný autentizovaný režim. Zkontrolujte login, token nebo licenční stav desky.

### Flash firmware selhává

Zkontrolujte:

- že jste přihlášení,
- že deska odpovídá očekávanému MCU,
- že používáte verzi Sniffy kompatibilní s firmware,
- že jiná aplikace nedrží ST-Link.

### Session se neobnovuje správně

Zkontrolujte:

- nastavení Restore session after startup,
- volbu Smart session layout and geometry,
- že máte skutečně uloženou session pro danou desku,
- zda nejde o výrazně starší session neodpovídající nové verzi aplikace.

### Některý modul je neaktivní nebo se hned zavře

Obvykle jde o jednu z těchto příčin:

- firmware daný modul nepodporuje,
- modul je v konfliktu s jiným aktivním modulem,
- zařízení je v omezeném režimu,
- došlo k problému v handshaku po připojení.

## 20. Pokročilé použití: Agent Bridge a sniffy-mcp

Tato kapitola je určená pro pokročilé uživatele, kteří chtějí aplikaci ovládat skripty, AI agentem nebo přes MCP.

### Co je Agent Bridge

Agent Bridge je lokální IPC vrstva uvnitř desktopové aplikace. Aplikace ji podle kódu spouští automaticky a zpřístupňuje funkce přes named pipe.

Vlastnosti:

- běží lokálně, neotevírá síťový port,
- používá JSON-RPC 2.0,
- změny provedené agentem se projeví v běžném GUI,
- umí ovládat zařízení, moduly i číst data.

### sniffy-mcp

Součástí projektu je i Python balíček sniffy-mcp, který slouží jako klient a MCP server.

#### Instalace publikované verze

```bash
pip install "sniffy-mcp[mcp] @ https://sniffy.cz/sniffy_mcp_latest.php?format=wheel"
```

```powershell
$meta = Invoke-RestMethod https://sniffy.cz/scripts/sniffy_mcp_latest.php?format=json
pip install "sniffy-mcp[mcp] @ $($meta.wheel_url)"
```

#### Lokální vývojová instalace

```bash
cd sniffy/tools/sniffy_mcp
pip install -e .
pip install -e ".[mcp]"
```

#### Co lze ovládat

- scan a connect zařízení,
- start a stop modulů,
- konfigurace osciloskopu,
- čtení scope dat a měření,
- ovládání Sync PWM,
- ovládání Counter,
- ovládání Arb/PWM generatoru,
- Pattern gen.,
- Voltage source,
- Voltmeter a jeho datalogger.

#### Kdy se to hodí

- automatické testy,
- AI asistované měření,
- opakovatelné sekvence konfigurace,
- vzdáleně řízené laboratorní workflow na jednom PC.

Pro úplný přehled metod a CLI příkladů použijte přibalené dokumentace:

- [agentbridge/README.md](../agentbridge/README.md)
- [tools/sniffy_mcp/README.md](../tools/sniffy_mcp/README.md)

## 21. Doporučený první pracovní scénář

Pokud chcete Sniffy rychle vyzkoušet bez zbytečného bloudění, použijte tento postup:

1. Připojte desku a ověřte její nalezení v modulu Device.
2. Zobrazte pinout a zkontrolujte, kam připojíte sondu nebo výstup.
3. Otevřete Oscilloscope a ověřte základní akvizici signálu.
4. Otevřete Voltmeter a zkontrolujte napětí na stejném nebo jiném kanálu.
5. Uložte session, aby se stejné rozvržení příště obnovilo.
6. Až potom začněte používat firmware update, Pattern gen. nebo AI automatizaci.

### Doporučené pracovní návyky

- Před prvním měřením na nové desce si vždy otevřete Device a zkontrolujte pinout.
- Před flashováním firmware zavřete jiné ST nástroje, které by mohly držet ST-Link.

## 22. Shrnutí

Sniffy je víceúčelové desktopové prostředí, které propojuje konfiguraci STM32 Nucleo desek, měření, generování signálů, firmware management a pokročilou automatizaci.

Pro běžného uživatele jsou nejdůležitější čtyři oblasti:

- spolehlivě připojit správnou desku,
- rozumět pinům a konfliktům modulů,
- používat session a firmware update bezpečně,
- vybrat správný modul pro konkrétní úlohu.
