// Local simulated-device tests. No hardware IO or external network requests.
const assert=require('node:assert/strict'),fs=require('node:fs'),http=require('node:http'),path=require('node:path');
let chromium;try{({chromium}=require('../.pio/web-test/node_modules/playwright'));}catch{({chromium}=require('../../QC_ESP32/.pio/web-test/node_modules/playwright'));}
const root=path.resolve(__dirname,'..');
const version=fs.readFileSync(path.join(root,'src/app_config.cpp'),'utf8').match(/FIRMWARE_VERSION\s*=\s*"([^"]+)"/)[1];
const meters=Array.from({length:12},(_,i)=>({ch:i+1,gpio:[7,4,5,6,8,15,9,10,11,12,13,14][i],enabled:i!==3,name:'电表'+String(i+1).padStart(2,'0'),initial_kwh:100+i*25.5,imp_kwh:1600}));
let config={dev_name:'12路电表采集',mq_brk:'192.168.1.10',mq_port:1883,mq_cid:'DianBiao_1C29042F77D8',mq_user:'meter-user',mq_pass:'Mock@123',mq_pub:'dianbiao/meter/{MAC}',mq_sub:'dianbiao/meter/{MAC}',hb_s:60,rep_s:60,ntp1:'ntp.aliyun.com',ntp2:'ntp.ntsc.ac.cn',tz:'CST-8',sta_ip:'',sta_gw:'',sta_sn:'',sta_dns:'',ap_ssid:'',ap_pass:'Hotspot@123',meters};
let status={dev_mac:'1C:29:04:2F:77:D8',firmware:version,device_name:'12路电表采集',uptime:'1d 02:03:04',uptime_ms:93784000,free_heap:200000,ota_state:'idle',ap:{ssid:'DianBiao_2F77D8',clients:1},wifi:{connected:true,ssid:'Factory',ip:'192.168.1.66'},mqtt:{configured:true,connected:true,broker:'192.168.1.10'},ntp:{ok:true,time:'2026-11-07 08:03:00'},meters:meters.map((m,i)=>({...m,pulses:123456+i,energy_kwh:177.16+i*25.5}))};
let offline=false,saveFailure=false,configFailure=false,statusDelay=0,statusActive=0,statusMax=0,statusRequests=0,documents=0,scanPolls=0,joinPolls=0,joinFailure=false,joinRequest=null;
const writes=[],errors=[];
const wait=ms=>new Promise(r=>setTimeout(r,ms));
const server=http.createServer(async(req,res)=>{
 const url=new URL(req.url,'http://localhost');if(url.pathname==='/favicon.ico'){res.statusCode=204;res.end();return;}let body='';for await(const chunk of req)body+=chunk;
 const send=(code,d)=>{res.statusCode=code;res.setHeader('Content-Type','application/json; charset=utf-8');res.end(JSON.stringify(d));};
 if(!url.pathname.startsWith('/api/')){documents++;res.setHeader('Content-Type','text/html; charset=utf-8');res.end(fs.readFileSync(path.join(root,'web/index.html')));return;}
 if(url.pathname==='/api/status'){statusActive++;statusMax=Math.max(statusMax,statusActive);statusRequests++;await wait(statusDelay);send(offline?503:200,offline?{error:'offline'}:status);statusActive--;return;}
 if(url.pathname==='/api/config'&&req.method==='GET'){send(configFailure?503:200,configFailure?{error:'no_memory'}:config);return;}
 if(url.pathname==='/api/wifi/scan'){
  if(url.searchParams.has('start'))scanPolls=0;
  send(200,++scanPolls===1?{scanning:true}:{scanning:false,networks:[{ssid:'Factory',rssi:-42,secure:true,connected:true},{ssid:'Guest',rssi:-70,secure:false},{ssid:'Factory',rssi:-80,secure:true},{ssid:'<img src=x>',rssi:-60,secure:true}]});return;
 }
 if(url.pathname==='/api/wifi/status'){
  joinPolls++;send(200,joinFailure?{state:'failed',error:'wifi_auth_failed'}:{state:joinPolls<3?'connecting':'connected',connected:true,ssid:joinRequest.ssid,ip:'192.168.1.77'});return;
 }
 if(req.method==='POST'){
  const d=JSON.parse(body);writes.push({path:url.pathname,body:d});
  if(saveFailure){send(409,{error:'device_busy'});return;}
  if(url.pathname==='/api/meters'){if(d.meters)config.meters=d.meters;if(d.zero_channel)status.meters[d.zero_channel-1].pulses=0;}
  if(url.pathname==='/api/config')Object.assign(config,d);
  if(url.pathname==='/api/wifi/connect'){joinRequest=d;joinPolls=0;}
  send(['/api/ota','/api/wifi/connect'].includes(url.pathname)?202:200,{success:true,state:'queued'});return;
 }
 send(404,{error:'not_found'});
});
(async()=>{
 await new Promise(r=>server.listen(0,'127.0.0.1',r));const origin='http://127.0.0.1:'+server.address().port;
 const browser=await chromium.launch({channel:'msedge',headless:true});
 try{
  const page=await browser.newPage({viewport:{width:1440,height:1100}});page.on('pageerror',e=>errors.push(e.message));page.on('dialog',d=>d.accept());
  await page.goto(origin);await page.waitForFunction(()=>document.querySelector('#m_name0').value==='电表01');
  assert.equal(await page.locator('#meters>.card').count(),12);assert.equal(await page.locator('#meterConfig>.card').count(),12);
  assert.equal(await page.locator('#energySummary>tr').count(),3);
  for(const row of await page.locator('#energySummary>tr').all())assert.equal(await row.locator('td').count(),4);
  assert.equal(await page.locator('#energy1 .summary-name').textContent(),'电表01');
  assert.equal(await page.locator('#energy1 .summary-value').textContent(),'177.160');
  assert.equal(await page.locator('.power').count(),0);assert(!(await page.locator('#overview').textContent()).includes('当前功率'));
  await page.evaluate(()=>{
   const original=state;render({...original,meters:original.meters.map(m=>m.ch===1?{...m,name:'<img src=x>',energy_kwh:123456789.125}:m)});
   if(document.querySelector('#energy1 img')||document.querySelector('#energy1 .summary-name').textContent!=='<img src=x>'||document.querySelector('#energy1 .summary-value').textContent!=='123456789.125')throw Error('Summary live update or text escaping failed');
   for(const value of ['85858.003','85858.008','85858.013','85858.000','85859.000','2.000','0.000']){
    const m=JSON.parse('{"energy_kwh":'+value+'}');render({...original,meters:original.meters.map(row=>row.ch===1?{...row,...m}:row)});
    if(document.querySelector('#energy1 .summary-value').textContent!==value||document.querySelector('#meter1 .energy-value').textContent!==value)throw Error('Web display differs from published energy: '+value);
   }
   render(original);
  });
  assert.equal(documents,1);assert.equal(scanPolls,0,'no scan until requested');
  await page.evaluate(()=>{window.firstCard=document.querySelector('#meter1');window.firstInput=document.querySelector('#m_name0');});
  await page.locator('[data-tab=config]').click();await page.fill('#m_name0','未保存');await page.focus('#m_name0');
  await page.evaluate(()=>{document.querySelector('#m_name0').setSelectionRange(1,2);render(state);});
  assert.equal(await page.inputValue('#m_name0'),'未保存');assert(await page.evaluate(()=>document.activeElement===firstInput&&firstInput.selectionStart===1&&firstInput===document.querySelector('#m_name0')&&firstCard===document.querySelector('#meter1')));
  for(const tab of ['mqtt','wifi','ota','overview','config'])await page.locator('[data-tab='+tab+']').click();
  assert.equal(documents,1);assert.equal(scanPolls,0);assert.equal(await page.inputValue('#m_name0'),'未保存');
  const mutations=await page.evaluate(async()=>{const records=[];const o=new MutationObserver(rs=>records.push(...rs));o.observe(document.querySelector('#overview'),{subtree:true,childList:true,characterData:true});for(let i=0;i<30;i++)render(state);await Promise.resolve();o.disconnect();return records.length;});
  assert.equal(mutations,0,'unchanged data must not rebuild card content');
  statusDelay=900;const docBefore=documents;
  await page.evaluate(()=>{poll();poll();poll();});await page.waitForTimeout(100);
  await page.locator('[data-tab=mqtt]').click();assert(await page.locator('#settingsForm').isVisible());assert(statusActive>0,'tab switch works while request is pending');assert.equal(documents,docBefore);
  await page.waitForTimeout(1000);statusDelay=0;assert.equal(statusMax,1,'status polls never overlap');
  await page.locator('[data-tab=config]').click();await page.fill('#m_name0','车间总表');await page.fill('#m_init0','123.456');await page.fill('#m_imp0','3200');await page.uncheck('[name=m_en1]');
  await page.locator('#meterForm button.primary').click();await page.waitForFunction(()=>document.querySelector('#meterDraft').textContent==='配置已保存');
  let sent=writes.at(-1);assert.equal(sent.path,'/api/meters');assert.equal(sent.body.meters.length,12);assert.deepEqual(sent.body.meters[0],{ch:1,enabled:true,name:'车间总表',initial_kwh:123.456,imp_kwh:3200});assert.equal(sent.body.meters[1].enabled,false);
  await page.locator('[data-zero="3"]').click();await page.waitForTimeout(100);assert.equal(writes.at(-1).body.zero_channel,3);assert.equal(writes.at(-1).body.meters.length,12);
  saveFailure=true;await page.fill('#m_init0','888');await page.locator('#meterForm button.primary').click();await page.waitForFunction(()=>document.querySelector('#toast').textContent.includes('其他操作'));assert.equal(await page.inputValue('#m_init0'),'888');assert(await page.locator('#meterForm button.primary').isEnabled());saveFailure=false;
  await page.locator('[data-tab=mqtt]').click();
  for(const name of ['mq_pass','ap_pass']){assert.equal(await page.locator('#'+name).getAttribute('type'),'password');await page.locator('[data-password-target='+name+']').check();assert.equal(await page.locator('#'+name).getAttribute('type'),'text');}
  await page.fill('#mq_port','1884');await page.fill('#ap_pass','NewHotspot@123');await page.locator('#settingsForm button.primary').click();await page.waitForFunction(()=>document.querySelector('#settingsDraft').textContent.includes('配置已保存'));
  assert.deepEqual(writes.at(-1),{path:'/api/config',body:{mq_port:1884,ap_pass:'NewHotspot@123'}});
  await page.locator('[data-tab=wifi]').click();await page.locator('#scan').click();await page.waitForSelector('#networks .wifi-network');assert.equal(await page.locator('#networks .wifi-network').count(),3);assert.equal(await page.locator('#networks img').count(),0);
  await page.locator('#networks .wifi-network').filter({hasText:'Guest'}).locator('button').click();assert(!(await page.locator('#joinPasswordField').isVisible()));await page.locator('#closeJoin').click();
  await page.locator('#manualWifi').click();await page.fill('#joinSsid','车间 & WiFi');await page.fill('#joinPassword','Wifi@123&+');await page.locator('#joinConnect').click();await page.waitForTimeout(1300);assert((await page.locator('#joinState').textContent()).includes('正在连接'),'old connected association must not count as success');await page.waitForFunction(()=>document.querySelector('#joinState').textContent.includes('连接成功'));
  assert.deepEqual(writes.at(-1),{path:'/api/wifi/connect',body:{ssid:'车间 & WiFi',password:'Wifi@123&+'}});await page.locator('#closeJoin').click();
  joinFailure=true;await page.locator('#manualWifi').click();await page.fill('#joinSsid','Factory');await page.fill('#joinPassword','wrongpass');await page.locator('#joinConnect').click();await page.waitForFunction(()=>document.querySelector('#joinState').textContent.includes('检查密码'));assert(await page.locator('#joinConnect').isEnabled());await page.locator('#closeJoin').click();joinFailure=false;
  await page.locator('[data-tab=ota]').click();await page.fill('#url','http://192.168.1.10/firmware.bin?v=1&x=2');await page.fill('#md5','0123456789abcdef0123456789abcdef');await page.fill('#size','1080000');await page.locator('#otaForm button.primary').click();await page.waitForFunction(()=>document.querySelector('#otaState').textContent.includes('升级已提交'));
  assert.deepEqual(writes.at(-1),{path:'/api/ota',body:{url:'http://192.168.1.10/firmware.bin?v=1&x=2',md5:'0123456789abcdef0123456789abcdef',size:1080000}});
  offline=true;await page.evaluate(()=>poll());await page.waitForFunction(()=>!document.querySelector('#statusError').hidden);assert(await page.evaluate(()=>firstCard===document.querySelector('#meter1')));offline=false;await page.evaluate(()=>poll());await page.waitForFunction(()=>document.querySelector('#statusError').hidden);
  await page.evaluate(()=>{window.hiddenForTest=true;Object.defineProperty(document,'hidden',{configurable:true,get(){return window.hiddenForTest}});document.dispatchEvent(new Event('visibilitychange'));});
  const requestBefore=statusRequests;await page.evaluate(()=>poll());await page.waitForTimeout(1200);assert.equal(statusRequests,requestBefore,'background page pauses polling');await page.evaluate(()=>{hiddenForTest=false;document.dispatchEvent(new Event('visibilitychange'));});
  for(const width of [320,360,390,620,1000,1440]){
   await page.setViewportSize({width,height:1000});for(const tab of ['overview','config','wifi','mqtt','ota']){await page.locator('[data-tab='+tab+']').click();assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),tab+' at '+width);}
   await page.locator('[data-tab=overview]').click();
   const cells=await page.locator('#energySummary tr:first-child td').evaluateAll(es=>es.map(e=>({x:e.getBoundingClientRect().x,y:e.getBoundingClientRect().y})));
   assert(cells.every((c,i)=>c.y===cells[0].y && (!i||c.x>cells[i-1].x)),'four columns at '+width);
   const nameBox=await page.locator('#energy1 .summary-name').boundingBox(),valueBox=await page.locator('#energy1 .summary-energy').boundingBox();assert(nameBox.y+nameBox.height<=valueBox.y,'name above energy');
   await page.locator('.meter-details>summary').click();assert(await page.locator('#meter1').isVisible());assert(!(await page.locator('#meter1').textContent()).includes('当前功率'));await page.locator('.meter-details>summary').click();
  }
  assert.equal(documents,1,'all operations and tab switches use one HTML load');
  await page.evaluate(async()=>{await loadConfig();document.querySelectorAll('[data-password-target]').forEach(e=>{e.checked=false;document.getElementById(e.dataset.passwordTarget).type='password';});renderNetworks([{ssid:'Factory',rssi:-42,secure:true,connected:true},{ssid:'Guest',rssi:-70,secure:false},{ssid:'Workshop_2F',rssi:-60,secure:true}]);document.querySelector('#toast').hidden=true;});
  fs.mkdirSync(path.join(root,'docs'),{recursive:true});
  for(const [tab,name]of [['overview','overview'],['config','config'],['mqtt','mqtt'],['wifi','wifi'],['ota','ota_web']]){
   await page.locator('[data-tab='+tab+']').click();await page.setViewportSize({width:1440,height:1100});await page.screenshot({path:path.join(root,'docs/'+name+'-desktop-'+version+'.png'),fullPage:true});await page.setViewportSize({width:390,height:1000});await page.screenshot({path:path.join(root,'docs/'+name+'-mobile-'+version+'.png'),fullPage:true});
  }
  configFailure=true;await page.reload();await page.waitForFunction(()=>!document.querySelector('#configError').hidden);assert(await page.locator('#m_name0').isDisabled());configFailure=false;await page.evaluate(()=>poll());await page.waitForFunction(()=>!document.querySelector('#m_name0').disabled);
  assert.deepEqual(errors,[]);
  console.log('PASS SPA: one document load, local tabs during delayed API, stable DOM/focus/drafts, zero card text mutations for unchanged data, single in-flight poll, background pause, offline/config recovery, 12-channel save/zero, settings patches, Wi-Fi/OTA JSON workflows, all tabs at 320..1440px.');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1}).finally(()=>server.close());
