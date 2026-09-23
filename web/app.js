'use strict';
const $ = id => document.getElementById(id);
let device = null, requestBusy = false, otaBusy = false, revision = 0, savedTimer;
let scanAttempted = false;
const encoder = new TextEncoder();
const source = $('source');
const active = () => device && ['running','paused','cancelling','maintenance'].includes(device.job.state);
function showError(message) { $('alert').textContent = message; $('alert').hidden = !message; }
function keyboardStatusText(state) {
  if (!state) return 'Unknown';
  if (!state.enabled) return 'Not built';
  if (!state.connected) return 'Disconnected';
  return state.name ? 'Connected: ' + state.name : 'Connected (name unavailable)';
}
function memorySize(bytes) {
  return Number.isFinite(bytes) ? (bytes / 1048576).toLocaleString(undefined, {maximumFractionDigits:2}) + ' MiB' : '--';
}
function counts() {
  const bytes = encoder.encode(source.value).length;
  const lines = source.value ? source.value.replace(/\r\n?/g,'\n').split('\n').length - (source.value.endsWith('\n') ? 1 : 0) : 0;
  $('text-count').textContent = lines.toLocaleString() + ' lines · ' + bytes.toLocaleString() + ' bytes';
  updateControls();
}
function updateControls() {
  const lock = requestBusy || otaBusy || !device || active();
  $('send-text').disabled = $('validate').disabled = lock || !source.value || encoder.encode(source.value).length > (device?.maxBytes || 262144);
  source.disabled = !!(requestBusy || otaBusy || active());
  $('open-text').disabled = $('clear-text').disabled = source.disabled;
  $('overlay').disabled = lock;
  $('pause').disabled = requestBusy || !device || !['running','paused'].includes(device.job.state);
  $('pause').lastChild.textContent = device?.job.state === 'paused' ? 'Resume' : 'Pause';
  $('cancel').disabled = requestBusy || !device || !['running','paused'].includes(device.job.state);
  for (const id of ['scan','save-network','update']) $(id).disabled = !!(requestBusy || otaBusy || !device || active());
}
async function api(path, options = {}) {
  const headers = {'X-Astrocade-Token': device?.token || '', ...options.headers};
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 30000);
  try {
    const response = await fetch(path, {...options, headers, signal:controller.signal, cache:'no-store'});
    const data = await response.json();
    if (!response.ok) { const e = new Error(data.error || 'Request failed'); e.data = data; throw e; }
    return data;
  } finally { clearTimeout(timeout); }
}
function renderJob() {
  const job = device.job, percent = job.total ? Math.floor(100 * job.sent / job.total) : 0;
  $('job-state').textContent = job.state;
  $('sent').textContent = job.sent.toLocaleString();
  $('total').textContent = job.total.toLocaleString();
  $('progress').max = job.total || 1; $('progress').value = job.sent;
  $('percent').textContent = percent + '%';
  $('lines-sent').textContent = job.linesSent.toLocaleString() + ' / ' + job.lines.toLocaleString() + ' lines';
  const messages = {idle:'Ready',running:'Sending',paused:'Paused at a character boundary',cancelling:'Finishing current character',cancelled:'Cancelled; unsent characters discarded',complete:'All characters sent',maintenance:'Device maintenance'};
  $('job-note').textContent = messages[job.state] || job.state;
}
async function poll() {
  if (otaBusy) return;
  try {
    const fresh = await api('/api/status');
    const initial = !device; device = fresh;
    $('connection').textContent = fresh.preview ? 'Preview' : fresh.connected ? 'Online' : 'Setup Wi-Fi';
    $('connection').className = 'connection online';
    $('footer-state').textContent = fresh.preview ? 'Simulated device' : 'Device connected';
    $('down-ms').textContent = fresh.downMs + ' ms';
    $('gap-ms').textContent = fresh.gapMs + ' ms';
    $('line-ms').textContent = fresh.lineGapMs + ' ms';
    $('limit').textContent = (fresh.maxBytes / 1024) + ' KiB';
    $('network-state').textContent = fresh.connected ? 'Connected' : 'Setup';
    $('current-ssid').textContent = fresh.ssid || '--';
    $('ip').textContent = fresh.ip;
    $('ap-name').textContent = fresh.setup ? fresh.ap : 'Off';
    $('version').textContent = fresh.version;
    $('status-version').textContent = fresh.version || '--';
    $('build-type').textContent = fresh.buildType || '--';
    $('usb-status').textContent = keyboardStatusText(fresh.usb);
    $('ble-status').textContent = keyboardStatusText(fresh.ble);
    $('board-model').textContent = fresh.boardModel || '--';
    $('chip-model').textContent = fresh.chip || '--';
    $('device-memory').textContent = memorySize(fresh.flashBytes) + ' / ' + memorySize(fresh.psramBytes);
    $('ota-size').textContent = memorySize(fresh.otaMaxBytes);
    $('rgb-enabled').textContent = fresh.rgbEnabled === true ? 'Enabled' : fresh.rgbEnabled === false ? 'Disabled' : '--';
    $('build-inputs').textContent = fresh.usb?.enabled ? 'Astrocade - USB + BLE + Wi-Fi' : 'Astrocade - BLE + Wi-Fi';
    if (initial) {
      $('overlay').replaceChildren(...fresh.overlays.map(o => new Option(o.name, o.id)));
      $('overlay').value = fresh.overlay;
      if (fresh.setup && !location.hash) location.hash = '#network';
    }
    renderJob(); updateControls();
    maybeScan();
  } catch (e) {
    $('connection').textContent = 'Disconnected'; $('connection').className = 'connection offline';
    $('footer-state').textContent = 'Waiting for device';
    $('usb-status').textContent = $('ble-status').textContent = 'Unknown (device offline)';
    device = null; updateControls();
  }
}
function validationResult(data) {
  $('issues').replaceChildren();
  $('validation').hidden = !data.errors;
  if (data.errors) {
    $('validation-title').textContent = data.errors + ' unsupported character' + (data.errors === 1 ? '' : 's');
    for (const issue of data.issues || []) {
      const row = document.createElement('tr');
      const glyph = issue.codepoint >= 32 && issue.codepoint !== 127 ? String.fromCodePoint(issue.codepoint) + ' ' : '';
      for (const text of [issue.line,issue.column,glyph + 'U+' + issue.codepoint.toString(16).toUpperCase().padStart(4,'0')]) {
        const cell = document.createElement('td'); cell.textContent = text; row.append(cell);
      }
      $('issues').append(row);
    }
    $('validation-state').textContent = 'Validation failed';
  } else $('validation-state').textContent = 'Valid · ' + data.characters.toLocaleString() + ' characters';
}
async function submitText(send) {
  if (!device || requestBusy || active()) return;
  showError(''); requestBusy = true; updateControls();
  const current = revision;
  try {
    const data = await api(send ? '/api/send' : '/api/validate', {method:'POST',headers:{'Content-Type':'text/plain; charset=utf-8','X-Overlay':$('overlay').value},body:source.value});
    if (current === revision) validationResult(data);
    if (send && data.job) { device.job = data.job; renderJob(); }
  } catch (e) {
    if (e.data?.errors) validationResult(e.data);
    showError(e.name === 'AbortError' ? 'Response timed out. Check transfer status before sending again.' : e.message);
    if (send) await poll();
  } finally { requestBusy = false; updateControls(); }
}
source.addEventListener('input', () => {
  revision++; $('validation-state').textContent = 'Not validated'; $('validation').hidden = true; counts();
  clearTimeout(savedTimer);
  savedTimer = setTimeout(() => { try { localStorage.setItem('astrocade-draft',source.value); } catch {} }, 500);
});
$('validate').onclick = () => submitText(false);
$('send-text').onclick = () => submitText(true);
$('clear-text').onclick = () => { source.value = ''; source.dispatchEvent(new Event('input')); source.focus(); };
$('open-text').onclick = () => $('text-file').click();
$('text-file').onchange = async () => {
  const file = $('text-file').files[0]; if (!file) return;
  if (file.size > (device?.maxBytes || 262144)) return showError('File exceeds the upload limit.');
  source.value = await file.text(); source.dispatchEvent(new Event('input')); $('text-file').value = '';
};
for (const command of ['pause','cancel']) $(command).onclick = async () => {
  showError(''); requestBusy = true; updateControls();
  try {
    const op = command === 'pause' && device.job.state === 'paused' ? 'resume' : command;
    device.job = await api('/api/' + op, {method:'POST'}); renderJob();
  } catch(e) { showError(e.message); }
  finally { requestBusy = false; updateControls(); }
};
function key() { return $('network-key').value || $('firmware-key').value; }
for (const input of document.querySelectorAll('.device-key')) input.oninput = () => {
  for (const other of document.querySelectorAll('.device-key')) if (other !== input) other.value = input.value;
  try { sessionStorage.setItem('astrocade-key-v2',input.value); } catch {}
};
function selectedSsid() {
  return $('networks').value === 'manual' ? $('ssid').value : $('networks').selectedOptions[0]?.dataset.ssid || '';
}
$('networks').onchange = () => {
  const manual = $('networks').value === 'manual';
  $('manual-network').hidden = !manual;
  $('ssid').disabled = !manual;
  if (manual) $('ssid').focus();
};
function maybeScan() {
  if (location.hash === '#network' && device && !scanAttempted && !requestBusy && !otaBusy && !active() && key()) $('scan').click();
}
$('scan').onclick = async () => {
  if (!device || requestBusy || otaBusy || active()) return;
  scanAttempted = true;
  if (!key()) { $('network-key').focus(); return showError('Device key required. Check the serial monitor.'); }
  requestBusy = true; showError(''); updateControls();
  try {
    const result = await api('/api/networks',{headers:{'X-Admin-Key':key()}});
    const seen = new Set();
    const previous = selectedSsid() || device.ssid;
    const options = result.networks.filter(n => n.ssid && !seen.has(n.ssid) && seen.add(n.ssid))
      .sort((a,b) => b.rssi - a.rssi).map((n,i) => {
        const option = new Option(n.ssid + ' (' + n.rssi + ' dBm' + (n.secure ? '' : ', open') + ')',String(i));
        option.dataset.ssid = n.ssid;
        option.selected = n.ssid === previous;
        return option;
      });
    $('networks').replaceChildren(new Option('Select network',''), ...options, new Option('Hidden / other network','manual'));
    if (previous && !seen.has(previous)) { $('networks').value = 'manual'; $('ssid').value = previous; }
    $('networks').onchange();
    $('network-result').textContent = seen.size + ' networks found';
  } catch(e) { showError(e.message); } finally { requestBusy = false; updateControls(); }
};
$('network-form').onsubmit = async e => {
  e.preventDefault(); showError(''); requestBusy = true; updateControls();
  try {
    const ssid = selectedSsid();
    await api('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json','X-Admin-Key':key()},body:JSON.stringify({ssid,password:$('wifi-password').value})});
    $('wifi-password').value = ''; $('network-result').textContent = 'Saved. Connecting to ' + ssid + '…';
  } catch(e) { showError(e.message); } finally { requestBusy = false; updateControls(); }
};
$('firmware-form').onsubmit = e => {
  e.preventDefault();
  const file = $('firmware-file').files[0];
  if (!file || !device || active()) return;
  if (!device.otaMaxBytes || file.size > device.otaMaxBytes) return showError('Firmware exceeds this device\'s application slot (' + memorySize(device.otaMaxBytes) + ').');
  if (!confirm('Install ' + file.name + ' and restart the adapter?')) return;
  showError(''); otaBusy = true; updateControls();
  $('firmware-result').textContent = 'Uploading'; $('firmware-progress').value = 0;
  const xhr = new XMLHttpRequest();
  xhr.open('POST','/api/ota');
  xhr.timeout = 180000;
  xhr.setRequestHeader('X-Astrocade-Token',device.token);
  xhr.setRequestHeader('X-Admin-Key',key());
  xhr.setRequestHeader('Content-Type','application/octet-stream');
  xhr.upload.onprogress = event => {
    if (event.lengthComputable) $('firmware-progress').value = 100 * event.loaded / event.total;
  };
  const failed = message => { otaBusy = false; showError(message); $('firmware-result').textContent = 'Update not confirmed'; updateControls(); poll(); };
  xhr.onload = () => {
    let result; try { result = JSON.parse(xhr.responseText); } catch { return failed('Unexpected device response.'); }
    if (xhr.status >= 400) return failed(result.error || 'Update failed.');
    $('firmware-result').textContent = 'Firmware accepted. Restarting…';
    device = null;
    setTimeout(() => { otaBusy = false; poll(); }, 5000);
  };
  xhr.onerror = () => failed('Connection lost. Check the device before retrying.');
  xhr.ontimeout = () => failed('Update timed out. Check the device before retrying.');
  xhr.send(file);
};
function navigate() {
  const name = ['send','network','firmware'].includes(location.hash.slice(1)) ? location.hash.slice(1) : 'send';
  for (const section of document.querySelectorAll('.view')) section.hidden = section.id !== name;
  for (const tab of document.querySelectorAll('[data-tab]')) {
    if (tab.dataset.tab === name) tab.setAttribute('aria-current','page'); else tab.removeAttribute('aria-current');
  }
  maybeScan();
}
window.onhashchange = navigate;
try {
  source.value = localStorage.getItem('astrocade-draft') || '';
  for (const input of document.querySelectorAll('.device-key')) input.value = sessionStorage.getItem('astrocade-key-v2') || '123456';
} catch {}
const ctx = $('keypad').getContext('2d');
ctx.fillStyle = '#263d38'; ctx.fillRect(0,0,32,44);
for (let row = 0; row < 6; row++) for (let col = 0; col < 4; col++) {
  ctx.fillStyle = row === 5 ? ['#52ac79','#d66464','#5ca0cf','#e5bc56'][col] : '#e8eeeb';
  ctx.fillRect(3 + col * 7,3 + row * 7,5,5);
}
navigate(); counts(); poll();
setInterval(() => { if (!requestBusy && !otaBusy) poll(); },1500);
