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
    .module,.tools{background:#111d22;border:1px solid #28414a;border-radius:16px;padding:20px}
    .module h2,.tools h2{margin:0 0 8px;font-size:1.2rem}.state{font-size:.85rem;color:#75d6ee}
    a,button{display:inline-block;margin-top:16px;padding:10px 16px;border:0;border-radius:10px;background:#167ca0;color:white;text-decoration:none;font:inherit;font-weight:650;cursor:pointer}
    button.secondary{background:#285866}button.danger{background:#9d4b4b}button:disabled{opacity:.45;cursor:not-allowed}
    .tools{margin-top:24px}.tools p{margin:8px 0}.actions{display:flex;flex-wrap:wrap;gap:10px}.actions button{margin-top:8px}
    input[type=file]{display:block;margin-top:12px;max-width:100%}
    pre{max-height:260px;overflow:auto;padding:14px;background:#081014;border:1px solid #28414a;border-radius:10px;white-space:pre-wrap;word-break:break-word;color:#bce9f5}
    #restoreMessage{min-height:1.5em;color:#bce9f5}
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
    <section class="module">
      <div class="state">Aktivní modul</div>
      <h2>MeteoPlaneRadar</h2>
      <p>Radar, letadla, předpověď a nastavení zdrojů meteorologických dat.</p>
      <a href="/meteo/">Otevřít nastavení MeteoPlaneRadar</a>
    </section>
  </main>
  <section class="tools">
    <div class="state">Hostitel zařízení</div>
    <h2>Stav, diagnostika a záloha</h2>
    <p>Export obsahuje nastavení hostitele a modulů bez hesel, Wi-Fi údajů a tokenů Home Assistant.</p>
    <div class="actions">
      <button id="statusButton" class="secondary" type="button">Načíst stav</button>
      <button id="diagnosticsButton" class="secondary" type="button">Načíst diagnostiku</button>
      <button id="downloadButton" type="button">Stáhnout zálohu</button>
    </div>
    <pre id="hostOutput" hidden></pre>
    <p>Obnova je úplná náhrada nastavení hostitele a modulů. Vyber JSON zálohu a potvrď ji až po kontrole.</p>
    <input id="backupFile" type="file" accept="application/json,.json">
    <button id="restoreButton" class="danger" type="button" disabled>Obnovit vybranou zálohu</button>
    <p id="restoreMessage" role="status"></p>
  </section>
  <script>
    (() => {
      const output = document.getElementById('hostOutput');
      const backupFile = document.getElementById('backupFile');
      const restoreButton = document.getElementById('restoreButton');
      const restoreMessage = document.getElementById('restoreMessage');

      function redirectIfUnauthorized(response) {
        if (response.status === 401 || response.status === 423) {
          window.location.href = '/clock/';
          return true;
        }
        return false;
      }

      async function readJson(response) {
        if (redirectIfUnauthorized(response)) throw new Error('Přesměrování na přihlášení.');
        const text = await response.text();
        let data;
        try { data = text ? JSON.parse(text) : {}; }
        catch (_) { throw new Error('Server vrátil neplatný JSON.'); }
        if (!response.ok || data.ok === false) {
          throw new Error(data.message || data.error || ('HTTP ' + response.status));
        }
        return data;
      }

      function showData(data) {
        output.hidden = false;
        output.textContent = JSON.stringify(data, null, 2);
      }

      async function showEndpoint(path) {
        try { showData(await readJson(await fetch(path, {cache:'no-store'}))); }
        catch (error) { output.hidden = false; output.textContent = error.message; }
      }

      document.getElementById('statusButton').addEventListener('click', () => showEndpoint('/api/status'));
      document.getElementById('diagnosticsButton').addEventListener('click', () => showEndpoint('/api/diagnostics'));

      document.getElementById('downloadButton').addEventListener('click', async () => {
        try {
          const response = await fetch('/api/config/export', {cache:'no-store'});
          if (redirectIfUnauthorized(response)) return;
          if (!response.ok) throw new Error('Export se nepodařilo načíst (HTTP ' + response.status + ').');
          const blob = await response.blob();
          const url = URL.createObjectURL(blob);
          const link = document.createElement('a');
          link.href = url;
          link.download = 'waveshare-multi-mode-backup.json';
          document.body.appendChild(link);
          link.click();
          link.remove();
          URL.revokeObjectURL(url);
          restoreMessage.textContent = 'Záloha byla stažena.';
        } catch (error) { restoreMessage.textContent = error.message; }
      });

      backupFile.addEventListener('change', () => {
        restoreButton.disabled = !backupFile.files || backupFile.files.length === 0;
        restoreMessage.textContent = '';
      });

      restoreButton.addEventListener('click', async () => {
        if (!backupFile.files || backupFile.files.length === 0) return;
        if (!window.confirm('Obnova nahradí aktuální nastavení hostitele i modulů. Pokračovat?')) return;
        restoreButton.disabled = true;
        restoreMessage.textContent = 'Obnova probíhá…';
        try {
          const file = backupFile.files[0];
          if (file.size > 16384) throw new Error('Záloha je příliš velká.');
          const body = await file.text();
          const response = await fetch('/api/config/import', {
            method: 'POST',
            headers: {'Content-Type':'application/json'},
            body
          });
          const data = await readJson(response);
          restoreMessage.textContent = data.message || 'Konfigurace obnovena.';
          backupFile.value = '';
        } catch (error) {
          restoreMessage.textContent = error.message;
          restoreButton.disabled = false;
        }
      });
    })();
  </script>
</body>
</html>)HTML";

}  // namespace web_host
