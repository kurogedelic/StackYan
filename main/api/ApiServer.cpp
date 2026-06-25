#include "ApiServer.h"

#include <cstring>
#include <string>
#include <vector>

#include "StorageLayout.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"

namespace stackyan {
namespace {
constexpr const char* kTag = "stackyan_api";

const char* kIndexHtml = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>StackYan Tool Test</title>
  <style>
    body { font-family: system-ui, sans-serif; margin: 16px; background: #f7f7f7; color: #222; }
    h1 { font-size: 22px; margin: 0 0 12px; }
    .tool, .face-panel { background: white; border: 1px solid #ddd; padding: 12px; margin: 10px 0; border-radius: 6px; }
    .name { font-weight: 700; }
    textarea { width: 100%; min-height: 90px; font-family: ui-monospace, monospace; box-sizing: border-box; }
    button, select, input { margin: 4px 4px 4px 0; padding: 8px 12px; }
    pre { background: #111; color: #eee; padding: 10px; overflow: auto; }
    .danger { color: #b00020; font-weight: 700; }
  </style>
</head>
<body>
  <h1>StackYan Tool Test</h1>
  <p>Local, unauthenticated development UI. Use only on a trusted LAN.</p>
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
  <div id="tools"></div>
  <script>
    const expressions = ["Neutral","Happy","Joy","Angry","Sad","Curious","Surprised","Shy","Thinking","Wink","Talking","Love","Panic","Proud","Sigh","Mischief","Cold","Sleepy"];
    const vowels = ["Off","Closed","A","I","U","E","O"];
    function fillSelect(id, values) {
      const el = document.getElementById(id);
      for (const value of values) el.add(new Option(value, value));
    }
    async function invokeToolRaw(name, args) {
      const res = await fetch("/api/invoke", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify({tool: name, args})
      });
      return await res.json();
    }
    async function invokeTool(name, args) {
      const json = await invokeToolRaw(name, args);
      await refreshFaceState();
      return json;
    }
    async function refreshFaceState() {
      const json = await invokeToolRaw("face.getState", {});
      document.getElementById("face-state").textContent = JSON.stringify(json, null, 2);
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
    async function runTool(name) {
      const input = document.getElementById("input-" + name).value;
      const out = document.getElementById("out-" + name);
      try {
        const json = await invokeToolRaw(name, JSON.parse(input || "{}"));
        out.textContent = JSON.stringify(json, null, 2);
        await refreshFaceState();
      } catch (e) {
        out.textContent = String(e);
      }
    }
    async function load() {
      fillSelect("face-expression", expressions);
      fillSelect("face-vowel", vowels);
      document.getElementById("status").textContent = "Loading...";
      const status = await (await fetch("/api/status")).json();
      document.getElementById("status").innerHTML = "<pre>" + JSON.stringify(status, null, 2) + "</pre>";
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
    }
    load();
  </script>
</body>
</html>
)HTML";

esp_err_t sendJson(httpd_req_t* req, cJSON* root, int status = 200) {
    char statusText[32];
    std::snprintf(statusText, sizeof(statusText), "%d", status);
    httpd_resp_set_status(req, statusText);
    httpd_resp_set_type(req, "application/json");
    char* json = cJSON_PrintUnformatted(root);
    esp_err_t err = httpd_resp_sendstr(req, json ? json : "{}");
    if (json) cJSON_free(json);
    cJSON_Delete(root);
    return err;
}

cJSON* errorJson(const char* code, const char* message) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON* err = cJSON_AddObjectToObject(root, "error");
    cJSON_AddStringToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", message);
    return root;
}

esp_err_t readRequestJson(httpd_req_t* req, cJSON** out) {
    std::vector<char> body(req->content_len + 1, 0);
    size_t received = 0;
    while (received < static_cast<size_t>(req->content_len)) {
        int r = httpd_req_recv(req, body.data() + received, req->content_len - received);
        if (r <= 0) return ESP_FAIL;
        received += r;
    }

    if (received == 0) {
        *out = cJSON_CreateObject();
        return ESP_OK;
    }

    *out = cJSON_Parse(body.data());
    return *out ? ESP_OK : ESP_ERR_INVALID_ARG;
}

std::string suffixAfter(const char* uri, const char* prefix) {
    if (!uri || !prefix) return {};
    const size_t n = std::strlen(prefix);
    if (std::strncmp(uri, prefix, n) != 0) return {};
    return std::string(uri + n);
}

}  // namespace

ApiServer::ApiServer(stackchu::Hal& hal, ToolRegistry& registry, EventBus& events)
    : hal_(hal), registry_(registry), events_(events) {}

bool ApiServer::begin() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;

    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(kTag, "http server start failed");
        return false;
    }

    httpd_uri_t index = {.uri = "/", .method = HTTP_GET, .handler = handleIndex, .user_ctx = this};
    httpd_uri_t status = {.uri = "/api/status", .method = HTTP_GET, .handler = handleStatus, .user_ctx = this};
    httpd_uri_t caps = {.uri = "/api/capabilities", .method = HTTP_GET, .handler = handleCapabilities, .user_ctx = this};
    httpd_uri_t events = {.uri = "/api/events", .method = HTTP_GET, .handler = handleEvents, .user_ctx = this};
    httpd_uri_t invoke = {.uri = "/api/invoke", .method = HTTP_POST, .handler = handleInvoke, .user_ctx = this};
    httpd_uri_t tools = {.uri = "/api/tools", .method = HTTP_GET, .handler = handleTools, .user_ctx = this};
    httpd_uri_t toolGet = {.uri = "/api/tools/*", .method = HTTP_GET, .handler = handleToolGet, .user_ctx = this};
    httpd_uri_t toolPost = {.uri = "/api/tools/*", .method = HTTP_POST, .handler = handleToolPost, .user_ctx = this};

    httpd_register_uri_handler(server_, &index);
    httpd_register_uri_handler(server_, &status);
    httpd_register_uri_handler(server_, &caps);
    httpd_register_uri_handler(server_, &events);
    httpd_register_uri_handler(server_, &invoke);
    httpd_register_uri_handler(server_, &tools);
    httpd_register_uri_handler(server_, &toolGet);
    httpd_register_uri_handler(server_, &toolPost);

    ESP_LOGI(kTag, "http server ready on port 80");
    return true;
}

esp_err_t ApiServer::handleIndex(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, kIndexHtml);
}

esp_err_t ApiServer::handleStatus(httpd_req_t* req) {
    return static_cast<ApiServer*>(req->user_ctx)->sendStatus(req);
}

esp_err_t ApiServer::handleCapabilities(httpd_req_t* req) {
    return static_cast<ApiServer*>(req->user_ctx)->sendCapabilities(req);
}

esp_err_t ApiServer::handleEvents(httpd_req_t* req) {
    return static_cast<ApiServer*>(req->user_ctx)->sendEvents(req);
}

esp_err_t ApiServer::handleInvoke(httpd_req_t* req) {
    return static_cast<ApiServer*>(req->user_ctx)->invokeRpc(req);
}

esp_err_t ApiServer::handleTools(httpd_req_t* req) {
    return static_cast<ApiServer*>(req->user_ctx)->sendTools(req);
}

esp_err_t ApiServer::handleToolGet(httpd_req_t* req) {
    auto* self = static_cast<ApiServer*>(req->user_ctx);
    const std::string name = suffixAfter(req->uri, "/api/tools/");
    return self->sendTool(req, name.c_str());
}

esp_err_t ApiServer::handleToolPost(httpd_req_t* req) {
    auto* self = static_cast<ApiServer*>(req->user_ctx);
    const std::string name = suffixAfter(req->uri, "/api/tools/");
    return self->invokeTool(req, name.c_str());
}

esp_err_t ApiServer::sendStatus(httpd_req_t* req) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", "StackYan");
    cJSON_AddStringToObject(root, "hostname", "stackyan.local");
    cJSON_AddStringToObject(root, "version", "0.1.0");
    cJSON_AddNumberToObject(root, "uptime_ms", esp_timer_get_time() / 1000);
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "tool_count", registry_.count());
    cJSON* storage = cJSON_AddObjectToObject(root, "storage");
    cJSON_AddBoolToObject(storage, "mounted", hal_.storage().isMounted());
    cJSON_AddStringToObject(storage, "root", hal_.storage().root());
    cJSON_AddNumberToObject(storage, "free_bytes", hal_.storage().freeSpace());
    return sendJson(req, root);
}

esp_err_t ApiServer::sendCapabilities(httpd_req_t* req) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "role", "physical_tool_server");
    cJSON_AddBoolToObject(root, "agent_centered", false);
    cJSON_AddStringToObject(root, "tool_discovery", "/api/tools");
    cJSON_AddStringToObject(root, "tool_invoke", "/api/invoke");
    cJSON_AddStringToObject(root, "tool_invoke_compat", "/api/tools/{name}");
    cJSON_AddStringToObject(root, "events", "/api/events");
    cJSON_AddBoolToObject(root, "face_tools", true);
    cJSON_AddBoolToObject(root, "workflow_tools", true);
    cJSON_AddBoolToObject(root, "avatar_renderer_attached", false);
    cJSON* disabled = cJSON_AddArrayToObject(root, "not_implemented_v1");
    cJSON_AddItemToArray(disabled, cJSON_CreateString("llm"));
    cJSON_AddItemToArray(disabled, cJSON_CreateString("xiaozhi"));
    cJSON_AddItemToArray(disabled, cJSON_CreateString("tts"));
    cJSON_AddItemToArray(disabled, cJSON_CreateString("stt"));
    cJSON_AddItemToArray(disabled, cJSON_CreateString("avatar_renderer"));
    cJSON_AddItemToArray(disabled, cJSON_CreateString("workflow_conditionals"));
    return sendJson(req, root);
}

esp_err_t ApiServer::sendEvents(httpd_req_t* req) {
    return sendJson(req, events_.toJson());
}

esp_err_t ApiServer::sendTools(httpd_req_t* req) {
    return sendJson(req, registry_.toJson());
}

esp_err_t ApiServer::sendTool(httpd_req_t* req, const char* name) {
    const ITool* tool = registry_.find(name ? name : "");
    if (!tool) return sendJson(req, errorJson("NOT_FOUND", "tool not found"), 404);
    return sendJson(req, registry_.toolToJson(*tool));
}

esp_err_t ApiServer::invokeTool(httpd_req_t* req, const char* name) {
    cJSON* args = nullptr;
    const esp_err_t readErr = readRequestJson(req, &args);
    if (readErr == ESP_ERR_INVALID_ARG) return sendJson(req, errorJson("BAD_JSON", "request body must be a JSON object"), 400);
    if (readErr != ESP_OK) return sendJson(req, errorJson("BAD_REQUEST", "failed to read request body"), 400);
    return invokeByName(req, name, args);
}

esp_err_t ApiServer::invokeRpc(httpd_req_t* req) {
    cJSON* body = nullptr;
    const esp_err_t readErr = readRequestJson(req, &body);
    if (readErr == ESP_ERR_INVALID_ARG) return sendJson(req, errorJson("BAD_JSON", "request body must be a JSON object"), 400);
    if (readErr != ESP_OK) return sendJson(req, errorJson("BAD_REQUEST", "failed to read request body"), 400);

    const cJSON* toolJson = cJSON_GetObjectItemCaseSensitive(body, "tool");
    if (!cJSON_IsString(toolJson) || !toolJson->valuestring || !toolJson->valuestring[0]) {
        cJSON_Delete(body);
        return sendJson(req, errorJson("INVALID_ARGUMENT", "tool is required"), 400);
    }

    const cJSON* argsJson = cJSON_GetObjectItemCaseSensitive(body, "args");
    cJSON* args = argsJson ? cJSON_Duplicate(argsJson, true) : cJSON_CreateObject();
    const std::string toolName = toolJson->valuestring;
    cJSON_Delete(body);
    return invokeByName(req, toolName.c_str(), args);
}

esp_err_t ApiServer::invokeByName(httpd_req_t* req, const char* name, cJSON* args) {
    ITool* tool = registry_.find(name ? name : "");
    if (!tool) {
        if (args) cJSON_Delete(args);
        return sendJson(req, errorJson("NOT_FOUND", "tool not found"), 404);
    }

    const int64_t start = esp_timer_get_time();
    ToolResult result = tool->invoke(args);
    const int elapsedMs = static_cast<int>((esp_timer_get_time() - start) / 1000);
    cJSON_Delete(args);

    ESP_LOGI(kTag, "tool %s ok=%s elapsed=%dms", tool->name(), result.ok ? "true" : "false", elapsedMs);
    appendToolLog(hal_.storage(), tool->name(), result.ok, elapsedMs);

    cJSON* eventPayload = cJSON_CreateObject();
    cJSON_AddStringToObject(eventPayload, "tool", tool->name());
    cJSON_AddBoolToObject(eventPayload, "ok", result.ok);
    cJSON_AddNumberToObject(eventPayload, "elapsed_ms", elapsedMs);
    events_.publish("tool.invoked", "ToolRegistry", eventPayload);
    cJSON_Delete(eventPayload);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", result.ok);
    cJSON_AddStringToObject(root, "tool", tool->name());
    cJSON_AddNumberToObject(root, "elapsed_ms", elapsedMs);
    if (result.ok) {
        cJSON_AddItemToObject(root, "result", cJSON_Duplicate(result.result, true));
    } else {
        cJSON* err = cJSON_AddObjectToObject(root, "error");
        cJSON_AddStringToObject(err, "code", result.errorCode.c_str());
        cJSON_AddStringToObject(err, "message", result.errorMessage.c_str());
    }
    return sendJson(req, root, result.ok ? 200 : 400);
}

}  // namespace stackyan
