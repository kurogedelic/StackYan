#pragma once

namespace stackyan::sim {

inline constexpr const char* kSimIndexHtml = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>StackYan mac_simulator</title>
  <style>
    body { font-family: system-ui, sans-serif; margin: 16px; background: #f7f7f7; color: #222; }
    h1 { font-size: 22px; margin: 0 0 12px; }
    h2 { font-size: 16px; margin: 10px 0; }
    .tool, .state, .face-panel { background: white; border: 1px solid #ddd; padding: 12px; margin: 10px 0; border-radius: 6px; }
    .name { font-weight: 700; }
    textarea { width: 100%; min-height: 90px; font-family: ui-monospace, monospace; box-sizing: border-box; }
    button, select, input { margin: 4px 4px 4px 0; padding: 7px 10px; }
    pre { background: #111; color: #eee; padding: 10px; overflow: auto; }
    .danger { color: #b00020; font-weight: 700; }
    .leds { display: flex; flex-wrap: wrap; gap: 6px; margin: 8px 0; }
    .led { width: 22px; height: 22px; border-radius: 50%; border: 1px solid #777; }
    .meters { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 8px; }
    .meter { background: #f2f2f2; padding: 8px; border-radius: 4px; }
  </style>
</head>
<body>
  <h1>StackYan mac_simulator</h1>
  <p>Local simulator UI. All actions call <code>POST /api/invoke</code> through ToolRegistry.</p>

  <section class="face-panel">
    <h2>Face Tools</h2>
    <div>
      <select id="face-expression"></select>
      <button onclick="setFaceExpression()">Set Expression</button>
      <select id="face-vowel"></select>
      <button onclick="setFaceVowel()">Set Vowel</button>
    </div>
    <div>
      <input id="face-line1" value="StackYan" placeholder="caption line 1">
      <input id="face-line2" value="Tool Server" placeholder="caption line 2">
      <button onclick="setFaceCaption()">Set Caption</button>
      <button onclick="invokeTool('face.clearCaption', {})">Clear Caption</button>
      <button onclick="invokeTool('face.reset', {})">Reset</button>
    </div>
    <pre id="face-state"></pre>
  </section>

  <div id="status"></div>
  <div id="sim-state" class="state"></div>
  <div id="tools"></div>
  <h2>Events</h2>
  <pre id="events"></pre>

  <script>
    const expressions = ["Neutral","Happy","Joy","Angry","Sad","Curious","Surprised","Shy","Thinking","Wink","Talking","Love","Panic","Proud","Sigh","Mischief","Cold","Sleepy"];
    const vowels = ["Off","Closed","A","I","U","E","O"];
    function fillSelect(id, values) {
      const el = document.getElementById(id);
      for (const value of values) el.add(new Option(value, value));
    }
    async function invokeTool(name, args) {
      const res = await fetch("/api/invoke", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify({tool: name, args})
      });
      const json = await res.json();
      await refreshFaceState();
      await refreshEvents();
      return json;
    }
    async function refreshFaceState() {
      const json = await invokeToolRaw("face.getState", {});
      document.getElementById("face-state").textContent = JSON.stringify(json, null, 2);
    }
    async function invokeToolRaw(name, args) {
      const res = await fetch("/api/invoke", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify({tool: name, args})
      });
      return await res.json();
    }
    async function setFaceExpression() {
      await invokeTool("face.setExpression", {expression: document.getElementById("face-expression").value});
    }
    async function setFaceVowel() {
      await invokeTool("face.setVowel", {vowel: document.getElementById("face-vowel").value});
    }
    async function setFaceCaption() {
      await invokeTool("face.setCaption", {line1: document.getElementById("face-line1").value, line2: document.getElementById("face-line2").value});
    }
    function sample(schema) {
      const p = (schema && schema.properties) || {};
      const out = {};
      for (const k of Object.keys(p)) {
        if (p[k].default !== undefined) out[k] = p[k].default;
        else if (p[k].enum) out[k] = p[k].enum[0];
        else if (p[k].type === "integer") out[k] = p[k].minimum || 0;
        else if (p[k].type === "number") out[k] = p[k].minimum || 0;
        else if (p[k].type === "boolean") out[k] = false;
        else out[k] = "";
      }
      return out;
    }
    async function refreshEvents() {
      const events = await (await fetch("/api/events")).json();
      document.getElementById("events").textContent = JSON.stringify(events, null, 2);
    }
    function renderSimState(state) {
      const leds = (state.rgb.leds || []).map(c => `<span class="led" title="${c}" style="background:${c}"></span>`).join("");
      document.getElementById("sim-state").innerHTML = `
        <h2>Simulator State</h2>
        <div>RGB brightness: ${state.rgb.brightness.toFixed(3)}</div>
        <div class="leds">${leds}</div>
        <div class="meters">
          <div class="meter">Servo H: ${state.servo.horizontal.toFixed(1)} deg<br>Servo V: ${state.servo.vertical.toFixed(1)} deg</div>
          <div class="meter">Motion ax/ay/az: ${state.motion.ax.toFixed(2)}, ${state.motion.ay.toFixed(2)}, ${state.motion.az.toFixed(2)}</div>
          <div class="meter">Power: ${state.power.level}% ${state.power.battery_voltage.toFixed(2)}V</div>
          <div class="meter">Storage: ${state.storage.root}</div>
        </div>`;
    }
    async function refreshSimState() {
      const state = await (await fetch("/api/sim/state")).json();
      renderSimState(state);
    }
    async function runTool(name) {
      const input = document.getElementById("input-" + name).value;
      const out = document.getElementById("out-" + name);
      try {
        const json = await invokeToolRaw(name, JSON.parse(input || "{}"));
        out.textContent = JSON.stringify(json, null, 2);
        await refreshSimState();
        await refreshFaceState();
        await refreshEvents();
      } catch (e) {
        out.textContent = String(e);
      }
    }
    async function load() {
      fillSelect("face-expression", expressions);
      fillSelect("face-vowel", vowels);
      const status = await (await fetch("/api/status")).json();
      document.getElementById("status").innerHTML = "<pre>" + JSON.stringify(status, null, 2) + "</pre>";
      await refreshSimState();
      await refreshFaceState();
      const data = await (await fetch("/api/tools")).json();
      const root = document.getElementById("tools");
      root.innerHTML = "";
      for (const tool of data.tools) {
        const div = document.createElement("div");
        div.className = "tool";
        const initial = JSON.stringify(sample(tool.parameters), null, 2);
        div.innerHTML = `<div class="name">${tool.title || tool.name} <code>${tool.name}</code> ${tool.dangerous ? '<span class="danger">dangerous</span>' : ''}</div>
          <p>${tool.description || ""}</p>
          <details><summary>schema</summary><pre>${JSON.stringify(tool.parameters, null, 2)}</pre></details>
          <textarea id="input-${tool.name}">${initial}</textarea>
          <button onclick="runTool('${tool.name}')">Run</button>
          <pre id="out-${tool.name}"></pre>`;
        root.appendChild(div);
      }
      refreshEvents();
      setInterval(refreshSimState, 1500);
      setInterval(refreshEvents, 3000);
    }
    load();
  </script>
</body>
</html>
)HTML";

}  // namespace stackyan::sim
