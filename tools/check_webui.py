"""Check the real web UI against a running TC002 firmware HTTP endpoint."""
import argparse
from pathlib import Path
from playwright.sync_api import sync_playwright

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("url")
p.add_argument("--chrome", default="/usr/bin/google-chrome")
p.add_argument("--screenshot", default="dist/webui.png")
a = p.parse_args()
with sync_playwright() as pw:
    browser = pw.chromium.launch(executable_path=a.chrome, headless=True,
                                 args=["--no-sandbox"])
    page = browser.new_page(viewport={"width": 1360, "height": 1000})
    errors = []
    page.on("pageerror", lambda error: errors.append(str(error)))
    # The dashboard polls continuously, so networkidle never occurs.
    page.goto(a.url, wait_until="domcontentloaded")
    page.wait_for_function("document.querySelector('#screen')?.width === 520 && "
                           "document.querySelector('#screen')?.height === 160")
    # Dimensions are now correct before the first poll. Wait for real data too,
    # so the screenshot verifies rendered content instead of the loading shell.
    page.wait_for_function("S.stats && document.querySelector('#screen').getContext('2d')"
                           ".getImageData(0,0,520,160).data.some((v,i)=>i%4!==3 && v!==0)")
    assert page.locator("#screen").evaluate("e => e.style.aspectRatio") == "52 / 16"
    assert not errors, errors
    Path(a.screenshot).parent.mkdir(parents=True, exist_ok=True)
    page.screenshot(path=a.screenshot, full_page=True)
    page.goto(a.url+'/#/system',wait_until='domcontentloaded')
    page.locator('#sec-matrix').wait_for()
    assert '52 × 16 = 832' in page.locator('#sec-matrix').inner_text()
    assert page.locator('#sec-gpio').count()==0
    page.goto(a.url+'/#/icons',wait_until='domcontentloaded')
    page.get_by_text('16×16 icons or images',exact=False).wait_for()
    assert '52×16' in page.locator('.drop').inner_text()
    page.goto(a.url+'/#/editor',wait_until='domcontentloaded')
    page.locator('#piskelFrame').wait_for()
    assert 'sizes=16x16,52x16,8x8,32x8' in page.locator('#piskelFrame').get_attribute('src')
    with page.expect_response(lambda r: r.url.endswith('/api/v1/audio/mp3')) as mp3:
        page.goto(a.url+'/#/audio', wait_until='domcontentloaded')
    assert mp3.value.status == 200
    assert isinstance(mp3.value.json()['files'], list)
    page.locator('input[type=file][accept=".mp3,audio/mpeg"]').wait_for(state='attached')
    assert not errors, errors
    print("Web UI loads without JavaScript errors; preview is 52 × 16 pixels.")
    browser.close()
