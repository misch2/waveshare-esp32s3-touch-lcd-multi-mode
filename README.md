# Waveshare multi-mode firmware

Firmware pro displej [Waveshare ESP32-S3-Touch-LCD-2.1](https://www.waveshare.com/esp32-s3-touch-lcd-2.1.htm), který v jednom zařízení kombinuje:

| Komponenta | Verze | Obsah |
| --- | --- | --- |
| [waveshare-hodiny](https://github.com/CooLajz/waveshare-hodiny) | `v1.5.5` | hodiny, počasí, Home Assistant a konfigurace |
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
