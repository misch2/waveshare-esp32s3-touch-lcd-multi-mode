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
    progress{display:block;width:min(100%,420px);height:14px;margin-top:14px;accent-color:#168aae}
    pre{max-height:260px;overflow:auto;padding:14px;background:#081014;border:1px solid #28414a;border-radius:10px;white-space:pre-wrap;word-break:break-word;color:#bce9f5}
    #restoreMessage{min-height:1.5em;color:#bce9f5}
    .disabled{opacity:.65}.disabled .state{color:#9fb7c0}
    .versions{display:grid;gap:10px;margin-top:12px}.version{padding:12px;background:#081014;border-radius:10px}
    .version strong,.version code{display:block}.version code{margin-top:4px;color:#bce9f5;word-break:break-all}
    .version code a{margin:0;padding:0;background:none;color:inherit;text-decoration:underline;text-underline-offset:2px}
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
  <section class="tools" id="componentVersions" hidden>
    <div class="state">Původ zdrojových komponent</div>
    <h2>Verze komponent</h2>
    <p>Exactní fork pin a poslední upstream revize začleněná do tohoto firmware.</p>
    <div class="versions" id="componentVersionsList"></div>
  </section>
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
  <section class="tools">
    <div class="state">Ruční aktualizace</div>
    <h2>Nahrát nový firmware</h2>
    <p>Vyberte aplikační obraz s příponou <strong>.bin</strong>. Název souboru může být libovolný; obsah se ověří před instalací. Aktualizace proběhne až po potvrzení a zařízení se poté restartuje.</p>
    <input id="firmwareFile" type="file" accept=".bin,application/octet-stream">
    <button id="firmwareUploadButton" type="button" disabled>Nahrát firmware</button>
    <progress id="firmwareProgress" max="100" value="0" hidden></progress>
    <p id="firmwareMessage" role="status"></p>
  </section>
  <script>
    (() => {
      const output = document.getElementById('hostOutput');
      const backupFile = document.getElementById('backupFile');
      const restoreButton = document.getElementById('restoreButton');
      const restoreMessage = document.getElementById('restoreMessage');
      const firmwareFile = document.getElementById('firmwareFile');
      const firmwareUploadButton = document.getElementById('firmwareUploadButton');
      const firmwareProgress = document.getElementById('firmwareProgress');
      const firmwareMessage = document.getElementById('firmwareMessage');
      const componentVersions = document.getElementById('componentVersions');
      const componentVersionsList = document.getElementById('componentVersionsList');

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

      function shortSha(value) { return typeof value === 'string' ? value.slice(0, 12) : ''; }

      function commitLink(repositoryUrl, sha, label) {
        const link = document.createElement('a');
        link.textContent = label;
        if (typeof repositoryUrl === 'string' && repositoryUrl && typeof sha === 'string' && sha) {
          link.href = repositoryUrl.replace(/\/$/, '') + '/commit/' + encodeURIComponent(sha);
          link.target = '_blank';
          link.rel = 'noopener noreferrer';
        }
        return link;
      }

      function renderComponentVersions(data) {
        const components = data && data.componentProvenance;
        if (!Array.isArray(components) || components.length === 0) return;
        componentVersionsList.textContent = '';
        components.forEach((component) => {
          const item = document.createElement('div');
          item.className = 'version';
          const name = document.createElement('strong');
          name.textContent = component.displayName || component.id || 'Komponenta';
          const pin = document.createElement('code');
          pin.append('Fork pin: ', commitLink(component.forkUrl, component.forkPin, shortSha(component.forkPin)));
          const upstream = document.createElement('code');
          upstream.append('Upstream ' + (component.upstreamRef || '') + ': ');
          if (typeof component.upstreamTag === 'string' && component.upstreamTag) {
            upstream.append(component.upstreamTag, ' (');
          }
          upstream.append(commitLink(component.upstreamUrl, component.upstreamBase, shortSha(component.upstreamBase)));
          if (typeof component.upstreamTag === 'string' && component.upstreamTag) {
            upstream.append(')');
          }
          item.append(name, pin, upstream);
          componentVersionsList.appendChild(item);
        });
        componentVersions.hidden = false;
      }

      async function showEndpoint(path) {
        try { showData(await readJson(await fetch(path, {cache:'no-store'}))); }
        catch (error) { output.hidden = false; output.textContent = error.message; }
      }

      document.getElementById('statusButton').addEventListener('click', () => showEndpoint('/api/status'));
      document.getElementById('diagnosticsButton').addEventListener('click', () => showEndpoint('/api/diagnostics'));

      fetch('/api/diagnostics', {cache:'no-store'})
        .then(readJson).then(renderComponentVersions).catch(() => {});

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

      function firmwareFileName(file) {
        if (!file || !file.name) return '';
        return file.name.split(/[\\/]/).pop();
      }

      function validateFirmwareFile(file) {
        if (!file) return 'Nejprve vyberte soubor.';
        const name = firmwareFileName(file).toLowerCase();
        if (name.length <= 4 || !name.endsWith('.bin')) {
          return 'Vyberte soubor firmwaru s příponou .bin.';
        }
        if (file.size > 6291456) return 'Firmware je příliš velký pro OTA slot.';
        return '';
      }

      function setFirmwareBusy(busy) {
        firmwareFile.disabled = busy;
        firmwareUploadButton.disabled = busy || !firmwareFile.files ||
                                         firmwareFile.files.length === 0;
      }

      firmwareFile.addEventListener('change', () => {
        firmwareMessage.textContent = validateFirmwareFile(firmwareFile.files[0]);
        firmwareUploadButton.disabled = Boolean(firmwareMessage.textContent);
      });

      firmwareUploadButton.addEventListener('click', () => {
        const file = firmwareFile.files && firmwareFile.files[0];
        const validationError = validateFirmwareFile(file);
        if (validationError) {
          firmwareMessage.textContent = validationError;
          return;
        }
        if (!window.confirm('Nahrát firmware a restartovat zařízení?')) return;

        setFirmwareBusy(true);
        firmwareProgress.hidden = false;
        firmwareProgress.value = 0;
        firmwareMessage.textContent = 'Nahrávání probíhá…';
        const request = new XMLHttpRequest();
        request.open('POST', '/api/firmware/upload');
        request.upload.addEventListener('progress', (event) => {
          if (event.lengthComputable) {
            firmwareProgress.value = Math.round(event.loaded * 100 / event.total);
          }
        });
        request.onload = () => {
          if (request.status === 401 || request.status === 423) {
            window.location.href = '/clock/';
            return;
          }
          let data;
          try { data = request.responseText ? JSON.parse(request.responseText) : {}; }
          catch (_) { data = {ok:false, message:'Server vrátil neplatný JSON.'}; }
          if (request.status >= 200 && request.status < 300 && data.ok !== false) {
            firmwareProgress.value = 100;
            firmwareMessage.textContent = data.message ||
              'Firmware byl nahrán. Zařízení se restartuje…';
            setFirmwareBusy(true);
          } else {
            firmwareMessage.textContent = data.message ||
              ('Nahrání selhalo (HTTP ' + request.status + ').');
            setFirmwareBusy(false);
          }
        };
        request.onerror = () => {
          firmwareMessage.textContent = 'Nahrání firmwaru selhalo.';
          setFirmwareBusy(false);
        };
        const form = new FormData();
        form.append('firmware', file, file.name);
        request.send(form);
      });
    })();
  </script>
</body>
</html>)HTML";

}  // namespace web_host
