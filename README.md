# Waveshare multi-mode firmware

Firmware pro displej [Waveshare ESP32-S3-Touch-LCD-2.1](https://www.waveshare.com/esp32-s3-touch-lcd-2.1.htm), který v jednom zařízení kombinuje:

- [waveshare-hodiny](https://github.com/misch2/waveshare-hodiny-fork) – hodiny, počasí, Home Assistant a konfiguraci;
- [MeteoPlaneRadar](https://github.com/misch2/MeteoPlaneRadar) – meteoradar, předpověď počasí a radar letadel.

Mezi obrazovkami se přepíná vodorovným swipem. Svislý swipe mění rozsah radaru nebo radaru letadel. Nastavení je dostupné přes společné webové rozhraní zařízení.

## Původ a aktualizace zdrojových komponent

Firmware používá dva připnuté git submoduly. Jejich přesné forkové revize,
poslední začleněné upstream revize a URL jsou v
[`UPSTREAMS.json`](UPSTREAMS.json). Tento soubor je zdrojem pravdy pro
diagnostiku zařízení; `scripts/Test-UpstreamProvenance.ps1` ověřuje, že jeho
`forkPin` odpovídá skutečnému gitlinku v tomto repozitáři.

| Komponenta | Fork pin | Začleněný upstream |
| --- | --- | --- |
| MeteoPlaneRadar | `dd77fefd33d6adfa9498a745299e54004cea5694` | `792ef8d05b0900a81e0f49697b8e72220a89f4a7` |
| waveshare-hodiny | `e1a66810aba21504cf14c239022620e595430f83` | `9537a76932fc9269b2a22a5fb90a62785897c680` |

Aktualizujte vždy jen jeden submodul: načtěte jeho `upstream`, vytvořte z
`fork/main` větev `sync/upstream-YYYY-MM-DD`, proveďte explicitní merge
upstream větve, otestujte samostatný projekt a teprve potom ji slučte do
`fork/main`. Následně v tomto repozitáři připněte nový commit submodulu,
upravte pouze potřebné adaptéry a proveďte host testy, build i fyzický smoke
test. Nepoužívejte `git submodule update --remote` ani rebase zveřejněného
`fork/main`: první krok obchází kontrolovaný merge a druhý mění historické
gitlinky vydaných firmware verzí.

Po každé změně pinu aktualizujte `UPSTREAMS.json`, spusťte kontrolní skript a
do záznamu vydání přidejte oba rozsahy SHA (`old → new`). Hostitelská stránka
a `/api/diagnostics` zobrazují tytéž hodnoty přímo v zařízení.

## Nahrání firmwaru

1. Stáhněte z [Releases](../../releases) soubor s názvem končícím na `-factory.bin`.
2. Připojte displej k počítači datovým USB-C kabelem.
3. V desktopovém prohlížeči Chrome nebo Edge otevřete [ESP32 Flasher](https://esp32flasher.chiptron.cz/).
4. Vyberte `ESP32-S3`, stažený soubor `-factory.bin` a spusťte nahrání.

Soubor `-ota.bin` je určený pouze pro ruční aktualizaci z webového rozhraní již běžícího firmwaru.
