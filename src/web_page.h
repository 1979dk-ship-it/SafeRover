#pragma once

// The dashboard, as one document. Kept out of main.cpp because sixty lines of
// markup in the middle of the control code make the control code harder to
// read, and because the web module this project is heading for wants the page
// as a file of its own anyway.
//
// HTML, CSS and script together on purpose. Every separate file a browser has
// to fetch is another HTTP request, and this server answers one request per
// handleClient() call - so splitting them would slow the page down and give the
// loop more work, for a document of about 1.5 kB. Serving the parts separately
// starts paying off when the page grows enough to be worth caching, which is
// what the filesystem partition is being kept for.
//
// A raw string literal rather than an escaped one: HTML is full of quotes, and
// a backslash before every attribute value would bury the markup. The text is
// taken verbatim up to the closing delimiter.
//
// No PROGMEM. That macro belongs to AVR, where flash and RAM are separate
// address spaces and a stored string has to be read with special instructions.
// The ESP32 maps flash into the address space, so a const array already lives
// there and PROGMEM expands to nothing.
//
// %POLL_MS% is filled in when the page is served, so the interval exists once,
// as a named constant in main.cpp, rather than being written down twice.
const char PAGE_HTML[] = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SafeRover</title>
<style>
body{font-family:monospace;font-size:1.4rem;margin:1.5rem}
td{padding:.4rem 1rem}
td:last-child{font-weight:bold;text-align:right}
</style>
</head>
<body>
<h3>SafeRover - live</h3>
<table>
<tr><td>distance</td><td id="d">...</td></tr>
<tr><td>line L</td><td id="l">...</td></tr>
<tr><td>line R</td><td id="r">...</td></tr>
<tr><td>button</td><td id="b">...</td></tr>
</table>
<script>
async function poll() {
  try {
    const s = await (await fetch('/data')).json();

    // Strict equality, so no reading and a reading of zero can never be taken
    // for one another. A number here would say an obstacle is touching the
    // sensor, which is the worst thing this page could get wrong.
    document.getElementById('d').textContent =
      s.distance === null ? '-- no echo' : s.distance.toFixed(1) + ' cm';

    // textContent, not innerHTML: the values are written as text and never
    // interpreted as markup.
    document.getElementById('l').textContent = s.lineLeft;
    document.getElementById('r').textContent = s.lineRight;
    document.getElementById('b').textContent = s.button ? 'pressed' : 'released';
  } catch (e) {
    // One dropped poll is not worth blanking the screen for. The next one is
    // along shortly and will put the numbers back.
  }
}

setInterval(poll, %POLL_MS%);

// Once immediately, so the page shows real values on load instead of the
// placeholders sitting there until the first interval elapses.
poll();
</script>
</body>
</html>
)rawliteral";
