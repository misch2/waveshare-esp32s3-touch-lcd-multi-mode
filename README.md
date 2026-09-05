# Waveshare multi-mode firmware

## Zásadní otázka

Mít na 2.1" Waveshare displeji [hodiny od CooLajze](https://github.com/CooLajz/waveshare-hodiny), nebo [MeteoPlaneRadar](https://github.com/petus/MeteoPlaneRadar) od Chiptrona?

<img width="338" height="208" alt="image" src="https://github.com/user-attachments/assets/5aac2ea1-cf0a-4df5-9316-896c49fcc6b7" />

proto tento firmware kombinuje obojí.

<img width="335" height="191" alt="image" src="https://github.com/user-attachments/assets/a337099d-68ca-49b2-9e3d-8a1089ce34d4" />

## Co firmware obsahuje

| Komponenta | Verze | Obsah |
| --- | --- | --- |
| [waveshare-hodiny](https://github.com/CooLajz/waveshare-hodiny) | `v1.7.2` | digitální a analogové hodiny, počasí, Home Assistant, TMEP a konfigurace |
| [MeteoPlaneRadar](https://github.com/petus/MeteoPlaneRadar) | `v0.6.4` | meteoradar, předpověď počasí a radar letadel |

Mezi obrazovkami se přepíná vodorovným swipem. Svislý swipe mění rozsah radaru nebo radaru letadel. Nastavení je dostupné přes společné webové rozhraní na http://waveshare-hodiny.local/.

## Nahrání firmwaru

1. Stáhněte z [Releases](../../releases) soubor s názvem končícím na `-factory.bin`.
2. Připojte displej k počítači datovým USB-C kabelem.
3. V desktopovém prohlížeči Chrome nebo Edge otevřete [ESP32 Flasher](https://esp32flasher.chiptron.cz/).
4. Vyberte `ESP32-S3`, stažený soubor `-factory.bin` a spusťte nahrání.

Soubor `-ota.bin` je určený pouze pro ruční aktualizaci z webového rozhraní již běžícího firmwaru.

## Krabička

Zkusil jsem případně navrhnout i alternativní krabičku s USBčkem vzadu místo na boku:

- [Printables](https://www.printables.com/model/1819205-enclosure-for-waveshare-esp32-s3-touch-lcd-21)

<img width="1600" height="900" alt="image" src="https://github.com/user-attachments/assets/358b3ec2-7ede-432d-a367-0d933ba56047" />
