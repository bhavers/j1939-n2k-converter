#pragma once

// The gateway's single, mobile-friendly settings page.
//   GET /        the page
//   GET /fuel    same page (old bookmark)
//   GET /update  same page (old bookmark)
// The page talks to the JSON API in fuel_web.h and to POST /update in ota_manager.h.

#include "esp_http_server.h"
#include "esp_log.h"

namespace WebUI {

static const char *TAG = "WEB_UI";

// The page is sent in three chunks so the firmware build time can be inserted
// between PAGE_HEAD and PAGE_TAIL.
static const char PAGE_HEAD[] = R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark">
<title>J1939-N2K Gateway</title>
<style>
*{box-sizing:border-box}
body{margin:0;background:#18181b;color:#f4f4f5;line-height:1.5;-webkit-text-size-adjust:100%;
font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
main{max-width:640px;margin:0 auto;padding:16px}
h1{font-size:20px;color:#38bdf8;margin:4px 0 0}
h2{font-size:16px;margin:0 0 12px}
.sub{font-size:13px;color:#a1a1aa;margin-bottom:16px}
.card{background:#27272a;border:1px solid #3f3f46;border-radius:12px;padding:16px;margin-bottom:16px}
.hint{font-size:13px;color:#a1a1aa;margin:12px 0 0}
.tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:10px}
.tiles .w{grid-column:1/-1}
.t{background:#18181b;border-radius:10px;padding:10px 12px}
.k{font-size:12px;color:#a1a1aa}
.v{font-size:20px;font-weight:600;overflow-wrap:anywhere}
.r{display:grid;grid-template-columns:1fr 1fr 44px;gap:8px;margin-bottom:8px;align-items:center}
.r.h{font-size:12px;color:#a1a1aa;margin-bottom:4px}
.f{display:grid;gap:4px;margin-bottom:10px;font-size:14px}
input{width:100%;min-height:44px;padding:0 10px;border-radius:8px;border:1px solid #52525b;
background:#18181b;color:#f4f4f5;font-size:16px}
input[type=file]{padding:9px 10px;height:auto}
button{min-height:44px;padding:0 16px;border:0;border-radius:10px;background:#0284c7;color:#fff;
font-size:15px;font-weight:600;cursor:pointer}
button.s{background:#3f3f46}
button:disabled{opacity:.45;cursor:default}
.r button{width:44px;padding:0;font-size:22px}
.btns{display:flex;flex-wrap:wrap;gap:8px;margin-top:12px}
.btns button{flex:1 1 140px}
.m{min-height:22px;margin-top:10px;font-size:14px;font-weight:500}
.ok{color:#4ade80}.er{color:#f87171}
progress{width:100%;height:14px;margin-top:12px;accent-color:#38bdf8}
.lamps{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:10px}
.lamp{display:flex;align-items:center;gap:10px;background:#18181b;border-radius:10px;padding:10px 12px;font-weight:600}
.lamp i{flex:none;width:18px;height:18px;border-radius:50%;background:#52525b}
.lamp small{display:block;font-size:12px;font-weight:400;color:#a1a1aa}
.lamp.g i{background:#22c55e}.lamp.a{background:#7f1d1d}.lamp.a i{background:#ef4444}
@media (max-width:360px){main{padding:10px}.card{padding:12px}}
</style></head><body><main>
<h1>J1939 &rarr; N2K Gateway</h1>
<div class="sub">Volvo Penta D1-30
<div>Engine Data J1939 (CAN1): <b id="sj" class="er">no connection</b></div>
<div>NMEA 2000 (CAN2): <b id="sn" class="er">no connection</b></div></div>

<section class="card"><h2>Live</h2>
<div class="lamps">
<div class="lamp" id="lo"><i></i><span>Oil pressure<small id="lot">no data</small></span></div>
<div class="lamp" id="lt"><i></i><span>Temperature<small id="ltt">no data</small></span></div>
</div>
<div class="tiles">
<div class="t"><div class="k">Engine speed</div><div class="v" id="rpm">-</div></div>
<div class="t"><div class="k">Coolant temperature</div><div class="v" id="cool">-</div></div>
<div class="t"><div class="k">Engine hours</div><div class="v" id="hrs">-</div></div>
<div class="t"><div class="k">Battery voltage</div><div class="v" id="bat">-</div></div>
<div class="t"><div class="k">Fuel rate</div><div class="v" id="rate">-</div></div>
<div class="t"><div class="k">Trip average</div><div class="v" id="avg">-</div></div>
<div class="t"><div class="k">Trip fuel</div><div class="v" id="tfv">-</div></div>
<div class="t"><div class="k">Trip engine time</div><div class="v" id="trv">-</div></div>
<div class="t"><div class="k">Engine off for</div><div class="v" id="off">-</div></div>
<div class="t w"><div class="k">GPS position (from NMEA 2000)</div><div class="v" id="gpos">no data</div></div>
<div class="t w"><div class="k">GPS time (UTC, from NMEA 2000)</div><div class="v" id="gtime">no data</div></div>
</div>
<p class="hint">Fuel figures are estimates from the table below, not measurements.</p>
</section>

<section class="card"><h2>Fuel table</h2>
<div class="r h"><span>Engine speed (rpm)</span><span>Fuel (L/h)</span><span></span></div>
<div id="rows"></div>
<div class="btns">
<button type="button" class="s" id="add">Add row</button>
<button type="button" id="savec">Save table</button>
<button type="button" class="s" id="defc">Restore default</button>
</div>
<div class="m" id="m1" role="status"></div>
<p class="hint">2 to 12 rows, rpm ascending. Below the first row the rate falls to 0 at 0 rpm; above the last row it stays constant.</p>
</section>

<section class="card"><h2>Trip</h2>
<div class="btns"><button type="button" id="rest">Reset now</button></div>
<div class="m" id="m2" role="status"></div>
<p class="hint">Zeroes the trip fuel and trip engine time immediately. The trip is stored in flash, so it survives a power cycle.</p>
<label class="f" style="margin-top:16px">Start a new trip when the engine has been off for (minutes)<input id="nt" type="number" inputmode="numeric" step="1" min="0" max="1440"></label>
<div class="btns"><button type="button" id="savent">Save</button></div>
<div class="m" id="m4" role="status"></div>
<p class="hint">After the engine has been off this long, the next engine start zeroes the trip and begins a new one. Until then the last trip stays visible. 0 = never start a new trip automatically. The off time is only counted while the gateway is powered; after a power-up it starts at 0.</p>
</section>

<section class="card"><h2>Firmware update</h2>
<label class="f">Firmware file (.bin)<input id="f" type="file" accept=".bin"></label>
<div class="btns"><button type="button" id="up">Upload and restart</button></div>
<progress id="p" value="0" max="100" hidden></progress>
<div class="m" id="m3" role="status"></div>
<p class="hint">Hardware: SmuqDev ESP32-C5 &middot; Build: <span id="build">)HTML";

static const char PAGE_TAIL[] = R"HTML(</span></p>
</section>
</main>
<script>
var $=function(i){return document.getElementById(i)};
function msg(id,t,ok){var m=$(id);m.textContent=t;m.className='m '+(ok?'ok':'er')}
function mk(tag,cls){var e=document.createElement(tag);if(cls)e.className=cls;return e}
function num(v,lab){var i=mk('input');i.type='number';i.inputMode='decimal';i.step='any';i.min='0';
i.value=v;i.setAttribute('aria-label',lab);return i}
function cnt(){$('add').disabled=$('rows').children.length>=12}
function row(r,l){var d=mk('div','r'),x=mk('button','s');
x.type='button';x.textContent='\u00d7';x.setAttribute('aria-label','Delete row');
x.onclick=function(){d.remove();cnt()};
d.appendChild(num(r,'Engine speed (rpm)'));d.appendChild(num(l,'Fuel (L/h)'));d.appendChild(x);
$('rows').appendChild(d);cnt()}
function fill(c){$('rows').textContent='';c.forEach(function(p){row(p[0],p[1])})}
function st(id,s){var e=$(id);e.textContent=s===true?'Connected':s===false?'no data':'no connection';e.className=s===true?'ok':'er'}
function conn(ok){if(!ok){st('sj',null);st('sn',null)}}
function post(u,b){return fetch(u,{method:'POST',headers:{'X-Gateway':'1'},body:b||''}).then(function(r){
return r.text().then(function(t){if(!r.ok)throw new Error(t||('HTTP '+r.status));return t})})}
function lamp(id,tid,have,alarm){$(id).className='lamp '+(!have?'':alarm?'a':'g');
$(tid).textContent=!have?'no data':alarm?'ALARM':'OK'}
function hm(s){var h=Math.floor(s/3600),m=Math.floor(s%3600/60);return h+' h '+m+' min'}
function dm(v,pos,neg,w){var t=Math.round(Math.abs(v)*60000),d=Math.floor(t/60000),m=(t%60000)/1000;
return ('00'+d).slice(-w)+'\u00b0'+(m<10?'0':'')+m.toFixed(3)+"' "+(v<0?neg:pos)}
function load(first){return fetch('/api/fuel',{cache:'no-store'}).then(function(r){
if(!r.ok)throw 0;return r.json()}).then(function(d){
st('sj',d.j1939_active);st('sn',d.n2k_active);
$('rpm').textContent=d.running?Math.round(d.rpm)+' rpm':(d.engine_data?'stopped':'no data');
$('cool').textContent=d.coolant_c===null?'-':d.coolant_c.toFixed(0)+' \u00b0C';
lamp('lo','lot',d.engine_data,d.oil_alarm);lamp('lt','ltt',d.engine_data,d.temp_alarm);
$('hrs').textContent=d.hours===null?'-':d.hours.toFixed(1)+' h';
$('bat').textContent=d.batt_v===null?'-':d.batt_v.toFixed(1)+' V';
$('rate').textContent=d.running?d.rate_lph.toFixed(1)+' L/h':'-';
$('avg').textContent=d.avg_lph===null?'-':d.avg_lph.toFixed(1)+' L/h';
$('tfv').textContent=d.trip_fuel_l.toFixed(1)+' L';
$('trv').textContent=hm(d.trip_run_s);
$('off').textContent=d.running?'running':hm(d.engine_off_min*60);
$('gpos').textContent=d.gps_lat===null?'no data':dm(d.gps_lat,'N','S',2)+'  '+dm(d.gps_lon,'E','W',3);
$('gtime').textContent=d.gps_time===null?'no data':d.gps_time;
if(first){fill(d.curve);$('nt').value=d.new_trip_min}
}).catch(function(){conn(false)})}
$('add').onclick=function(){row('','')};
$('savec').onclick=function(){var p=[],rs=$('rows').children;
for(var i=0;i<rs.length;i++){var x=rs[i].getElementsByTagName('input'),a=parseFloat(x[0].value),b=parseFloat(x[1].value);
if(!isFinite(a)||!isFinite(b)){msg('m1','Row '+(i+1)+' is incomplete',false);return}
p.push(a+':'+b)}
post('/api/fuel/curve',p.join(',')).then(function(){msg('m1','Table saved',true);load(true)})
.catch(function(e){msg('m1','Not saved: '+e.message,false)})};
$('defc').onclick=function(){if(!confirm('Restore the default fuel table?'))return;
post('/api/fuel/curve/reset').then(function(){msg('m1','Default table restored',true);load(true)})
.catch(function(e){msg('m1','Failed: '+e.message,false)})};
$('savent').onclick=function(){var v=$('nt').value.trim();
if(!/^\d+$/.test(v)||+v>1440){msg('m4','Enter whole minutes from 0 to 1440',false);return}
post('/api/fuel/newtrip',v).then(function(){msg('m4','Saved',true);load(true)})
.catch(function(e){msg('m4','Not saved: '+e.message,false)})};
$('rest').onclick=function(){if(!confirm('Reset the trip to zero now?'))return;
post('/api/fuel/trip/reset').then(function(){msg('m2','Trip reset',true);load(false)})
.catch(function(e){msg('m2','Failed: '+e.message,false)})};
function back(){var n=0;(function t(){n++;fetch('/api/fuel',{cache:'no-store'}).then(function(r){
if(!r.ok)throw 0;location.reload()}).catch(function(){
if(n<40)setTimeout(t,1500);else msg('m3','The gateway did not come back. Reload this page manually.',false)})})()}
$('up').onclick=function(){var f=$('f').files[0];
if(!f){msg('m3','Choose a firmware .bin file first',false);return}
if(!confirm('Upload '+f.name+' and restart the gateway?'))return;
var prg=$('p'),x=new XMLHttpRequest();prg.hidden=false;prg.value=0;$('up').disabled=true;msg('m3','Uploading...',true);
x.open('POST','/update');x.setRequestHeader('X-Gateway','1');
x.upload.onprogress=function(e){if(e.lengthComputable)prg.value=e.loaded/e.total*100};
x.onload=function(){if(x.status===200){msg('m3','Installed. Restarting...',true);setTimeout(back,4000)}
else{msg('m3','Failed: '+x.responseText,false);$('up').disabled=false}};
x.onerror=function(){msg('m3','Network error during upload',false);$('up').disabled=false};
x.send(f)};
load(true);
setInterval(function(){if(!document.hidden)load(false)},2000);
</script></body></html>)HTML";

static esp_err_t page_get(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (httpd_resp_send_chunk(req, PAGE_HEAD, HTTPD_RESP_USE_STRLEN) != ESP_OK ||
        httpd_resp_send_chunk(req, __DATE__ " " __TIME__, HTTPD_RESP_USE_STRLEN) != ESP_OK ||
        httpd_resp_send_chunk(req, PAGE_TAIL, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
        return ESP_FAIL;
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

// Registers the page on an already started HTTP server.
inline bool register_handlers(httpd_handle_t server) {
    if (!server) return false;
    const char *paths[] = {"/", "/fuel", "/update"};
    for (const char *path : paths) {
        httpd_uri_t u = {};
        u.uri = path;
        u.method = HTTP_GET;
        u.handler = page_get;
        if (httpd_register_uri_handler(server, &u) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register %s", path);
            return false;
        }
    }
    return true;
}

} // namespace WebUI
