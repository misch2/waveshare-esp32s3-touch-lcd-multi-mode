#pragma once

#include <pgmspace.h>

namespace web_host {

const char HOST_WEB_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="cs">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Multi-mode displej</title>
  <style>
    :root{color-scheme:dark;font-family:system-ui,sans-serif;background:#091014;color:#e7f6fb}
    body{max-width:760px;margin:0 auto;padding:32px 20px}
    h1{font-size:1.8rem;margin:0 0 8px}p{color:#9fb7c0;line-height:1.5}
    .modules{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:16px;margin-top:28px}
    .module{background:#111d22;border:1px solid #28414a;border-radius:16px;padding:20px}
    .module h2{margin:0 0 8px;font-size:1.2rem}.state{font-size:.85rem;color:#75d6ee}
    a{display:inline-block;margin-top:16px;padding:10px 16px;border-radius:10px;background:#167ca0;color:white;text-decoration:none;font-weight:650}
    .disabled{opacity:.65}.disabled .state{color:#9fb7c0}
  </style>
</head>
<body>
  <h1>Multi-mode displej</h1>
  <p>Společný vstup pro obrazovky a jejich nastavení.</p>
  <main class="modules">
    <section class="module">
      <div class="state">Aktivní modul</div>
      <h2>Waveshare hodiny</h2>
      <p>Čas, Home Assistant, Open-Meteo a nastavení vzhledu.</p>
      <a href="/clock/">Otevřít nastavení hodin</a>
    </section>
    <section class="module disabled">
      <div class="state">Připravuje se</div>
      <h2>MeteoPlaneRadar</h2>
      <p>Konfigurace meteoradaru, letadel a předpovědi bude připojena v další etapě.</p>
    </section>
  </main>
</body>
</html>)HTML";

}  // namespace web_host
