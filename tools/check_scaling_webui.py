"""Exercise icon uploads and live previews on a disposable host firmware instance.

Start build-host/awtrix-tc002 on port 18082 with an isolated data directory first.
Creates native-16.gif/native-52.gif test icons in that instance only.
"""
import json,time
from pathlib import Path
from playwright.sync_api import sync_playwright
with sync_playwright() as p:
 b=p.chromium.launch(executable_path='/usr/bin/google-chrome',args=['--no-sandbox'])
 page=b.new_page(viewport={'width':1360,'height':1000})
 errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
 page.goto('http://127.0.0.1:18082/#/icons')
 page.get_by_text('16×16 icons or images',exact=False).wait_for()
 for w,h in [(16,16),(52,16)]:
  result=page.evaluate('''async ([w,h])=>{
    const c=document.createElement('canvas');c.width=w;c.height=h;
    const g=c.getContext('2d');g.fillStyle='#ff0000';g.fillRect(0,0,w,h);
    g.fillStyle='#00ff00';g.fillRect(w-1,h-1,1,1);
    const blob=await new Promise(r=>c.toBlob(r,'image/png'));
    const cv=await iconAsGif(blob,'test.png');
    await uploadFile(cv.blob,'/ICONS','native-'+w+'.gif');
    await post('/api/v1/notifications',{icon:'native-'+w,text:'',hold:true,stack:false});
    return true;
  }''',[w,h])
  page.wait_for_function('''async ([w,h])=>{const s=await (await fetch('/api/v1/display/screen')).json();return s.pixels[(h-1)*52+w-1]===0x00ff00;}''',arg=[w,h])
  print('PNG upload -> GIF -> native display corner verified',w,h)
 # JPEG API fallback uses actual image dimensions, including inline JPEGs.
 page.evaluate('''async()=>{
  const c=document.createElement('canvas');c.width=52;c.height=16;
  const g=c.getContext('2d');g.fillStyle='#00ff00';g.fillRect(0,0,52,16);
  await post('/api/v1/notifications',{icon:c.toDataURL('image/jpeg',1).split(',')[1],text:'',hold:true,stack:false});
 }''')
 page.wait_for_function('''async()=>{const s=await (await fetch('/api/v1/display/screen')).json();return (s.pixels[831]&0xff00)>0xf000;}''')
 print('Native 52x16 JPEG bottom-right verified')
 rejected=page.evaluate('''async()=>{const c=document.createElement('canvas');c.width=52;c.height=17;
 const blob=await new Promise(r=>c.toBlob(r));try{await validateIcon(blob);return false;}catch(e){return e.message;}}''')
 assert '52×16' in rejected
 # Editor preview converts bitmap frames to the same scaled icon route.
 for w,h in [(8,8),(16,16),(32,8),(52,16)]:
  page.evaluate('''async ([w,h])=>await piskelLive({mode:'bitmap',w,h,dataBase64:btoa(String.fromCharCode(...Array.from({length:w*h*3},(_,i)=>i%3===2?255:0)))})''',[w,h])
  dw=min(52,w*2) if h<=8 else w
  page.wait_for_function('''async dw=>{const s=await (await fetch('/api/v1/display/screen')).json();return s.pixels[15*52+dw-1]===255;}''',arg=dw)
 print('Editor still previews match legacy and native image sizes')
 page.evaluate('piskelLiveOff()')
 page.goto('http://127.0.0.1:18082/#/editor')
 page.locator('#piskelFrame').wait_for()
 print('editor URL:',page.locator('#piskelFrame').get_attribute('src'))
 # Inspect the actual hosted editor, with a bounded load period.
 page.wait_for_function('piskelReady',timeout=20000)
 frame=page.frames[1]
 actual=frame.evaluate('({sizes:pskl.app.awtrixBridge.getSizes(),width:pskl.app.piskelController.getWidth(),height:pskl.app.piskelController.getHeight()})')
 assert actual=={'sizes':['16x16','52x16','8x8','32x8'],'width':16,'height':16},actual
 print('Hosted editor canvas and size presets verified:',actual)
 assert not errors,errors
 page.goto('http://127.0.0.1:18082/')
 page.wait_for_function("document.querySelector('#screen')?.style.aspectRatio==='52 / 16'")
 page.screenshot(path=str(Path(__file__).resolve().parents[1]/'dist/webui-scaling-local.png'))
 b.close()
