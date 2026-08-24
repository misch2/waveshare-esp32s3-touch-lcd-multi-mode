# Waveshare multi-mode firmware

Firmware pro displej [Waveshare ESP32-S3-Touch-LCD-2.1](https://www.waveshare.com/esp32-s3-touch-lcd-2.1.htm), který v jednom zařízení kombinuje:

- [waveshare-hodiny](https://github.com/misch2/waveshare-hodiny-fork) – hodiny, počasí, Home Assistant a konfiguraci;
- [MeteoPlaneRadar](https://github.com/misch2/MeteoPlaneRadar) – meteoradar, předpověď počasí a radar letadel.

Mezi obrazovkami se přepíná vodorovným swipem. Svislý swipe mění rozsah radaru nebo radaru letadel. Nastavení je dostupné přes společné webové rozhraní zařízení.

## Nahrání firmwaru

1. Stáhněte z [Releases](../../releases) soubor s názvem končícím na `-factory.bin`.
2. Připojte displej k počítači datovým USB-C kabelem.
3. V desktopovém prohlížeči Chrome nebo Edge otevřete [ESP32 Flasher](https://esp32flasher.chiptron.cz/).
4. Vyberte `ESP32-S3`, stažený soubor `-factory.bin` a spusťte nahrání.

Soubor `-ota.bin` je určený pouze pro ruční aktualizaci z webového rozhraní již běžícího firmwaru.
