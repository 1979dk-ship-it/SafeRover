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
// what the filesystem  partition is being kept for.
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

/* A cross, so a thumb finds a direction by position instead of by reading.
   Stop sits in the middle, which is the shortest reach from anywhere. */
.pad{display:grid;grid-template-columns:repeat(3,5rem);
     grid-template-rows:repeat(3,5rem);gap:.5rem;margin:1.5rem 0}
.pad button{font-size:1.6rem;font-family:monospace;

/* Without these a long press selects text or starts a scroll, and the
   system takes the gesture over - which fires pointercancel instead of
   pointerup. Better to stop that happening than to handle it. */
             touch-action:none;user-select:none;-webkit-user-select:none}
#fwd{grid-area:1/2}#left{grid-area:2/1}#stop{grid-area:2/2}
#right{grid-area:2/3}#back{grid-area:3/2}
#stop{font-weight:bold}
#speed{width:100%;height:2.5rem}
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

<div class="pad">
<button id="fwd">&#94;</button>
<button id="left">&lt;</button>
<button id="stop">STOP</button>
<button id="right">&gt;</button>
<button id="back">v</button>
</div>

<label>speed <span id="sv">%START_PERCENT%</span>%</label>
<input type="range" id="speed"
       min="%MIN_PERCENT%" max="100" value="%START_PERCENT%">

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


// ---- driving ----

// The whole of the operator's intent, in one variable. Buttons write to it, the
// sender reads it, and the two never call each other. One variable rather than a
// flag per direction, because directions contradict: with four flags there is a
// state where forward and back are both set, and something has to decide which
// wins. Here that state cannot be expressed.
let currentDir = 'stop';

const speedEl = document.getElementById('speed');
const svEl = document.getElementById('sv');

// 'input' fires on every movement of the handle. 'change' would only fire when
// it is let go, and the number beside it has to follow the thumb.
speedEl.addEventListener('input', () => {
  svEl.textContent = speedEl.value;
});

// Reports the current intent. Not "do this once" but "this is what I want now",
// which is why repeats and out-of-order arrivals are harmless.
//
// URLSearchParams builds dir=fwd&speed=47 and makes the browser set the
// form-urlencoded content type, which is exactly what server.arg() on the rover
// parses. Neither side needs any parsing code of its own.
async function sendCommand() {
  const body = new URLSearchParams({ dir: currentDir, speed: speedEl.value });

  try {
    await fetch('/drive', { method: 'POST', body });
  } catch (e) {
    // Deliberately empty. A dropped command is not worth reacting to: the next
    // one is along shortly. And if they all stop arriving, it is the rover's own
    // watchdog that stops it - nothing in this page is responsible for that.
  }
}

// Wires one direction button. Written once rather than four times: four copies
// of the same handler are four chances to leave 'left' where 'right' belongs.
//
// Each call keeps its own dir. The inner functions outlive hold() and still know
// which direction they were made for, because a function created inside another
// holds on to the scope it was created in.
function hold(id, dir) {
  const el = document.getElementById(id);

  el.addEventListener('pointerdown', () => {
    currentDir = dir;
    sendCommand();
  });

  // Three ways a press can end, and only the first is the ordinary one.
  // pointercancel fires when the system takes the gesture over - a scroll, an
  // edge swipe, an incoming call - and pointerleave when the finger slides off
  // the button without lifting. Sending one stop too many costs nothing; missing
  // one leaves the rover driving.
  ['pointerup', 'pointercancel', 'pointerleave'].forEach((ev) => {
    el.addEventListener(ev, () => {
      currentDir = 'stop';
      sendCommand();
    });
  });
}

hold('fwd', 'fwd');
hold('back', 'back');
hold('left', 'left');
hold('right', 'right');

// Stop is not a held control. It is pressed, and it means stop.
document.getElementById('stop').addEventListener('pointerdown', () => {
  currentDir = 'stop';
  sendCommand();
});

// The heartbeat, and only that. The presses above are what make the controls
// feel immediate; this exists to keep proving the channel is alive, which is the
// thing the rover's watchdog is really measuring. It runs whether or not a
// button is down, so a watchdog message means the page genuinely went away
// rather than that somebody let go of a button.
setInterval(sendCommand, %SEND_MS%);
</script>
</body>
</html>
)rawliteral";
