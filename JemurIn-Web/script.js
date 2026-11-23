// =====================================================
// SMART RAIN-AWARE DRYING RACK - MQTT WEB CLIENT
// EMQX Cloud: MQTT over WebSocket Secure (WSS), port 8084
// =====================================================

const MQTT_HOST = "zf2b171f.ala.asia-southeast1.emqxsl.com";
const MQTT_WSS_PORT = 8084;
const MQTT_PATH = "/mqtt";

const MQTT_USERNAME = "smartiot";
const MQTT_PASSWORD = "pintariot";

const TOPIC_DATA = "smartrain/data";
const TOPIC_STATUS = "smartrain/status";
const TOPIC_CONTROL = "smartrain/control";
const TOPIC_AVAIL = "smartrain/availability";

const brokerUrl = `wss://${MQTT_HOST}:${MQTT_WSS_PORT}${MQTT_PATH}`;

let client = null;
let currentMode = "AUTO";
let currentServo = "OPEN";
let currentFan = false;
let hasReceivedStatus = false;
let currentRain = null;      
let deviceOnline = null;     

const $ = (id) => document.getElementById(id);

const SERVO_LABEL = { OPEN: "Terbuka", CLOSED: "Tertutup" };

function setText(id, text) {
  const el = $(id);
  if (el) el.textContent = text;
}

function setPressed(id, active) {
  const el = $(id);
  el.classList.toggle("is-active", active);
  el.setAttribute("aria-pressed", String(active));
}

function setConnection(online, message) {
  const pill = $("connectionPill");
  pill.classList.toggle("online", online);
  pill.textContent = online
    ? "Terhubung"
    : /^reconnecting/i.test(message || "") ? "Menghubungkan…" : "Terputus";
  if (!online) deviceOnline = null;
  render();
}

function setNotice(message) {
  $("controlNotice").textContent = message;
}

function renderHero(connected) {
  let state, title, sub;
  const roofClosed = currentServo === "CLOSED";

  if (!connected) {
    state = "idle"; title = "Tidak terhubung"; sub = "Periksa koneksi internet.";
  } else if (deviceOnline === false) {
    state = "idle"; title = "Perangkat offline"; sub = "ESP32 tidak mengirim data.";
  } else if (currentRain === null) {
    state = "idle"; title = "Menunggu data"; sub = "Menunggu data dari ESP32.";
  } else if (currentRain && roofClosed) {
    state = "rain"; title = "Hujan terdeteksi"; sub = "Jemuran tertutup. Jemuran terlindungi.";
  } else if (currentRain) {
    state = "alert"; title = "Hujan, jemuran terbuka";
    sub = currentMode === "AUTO" ? "Menunggu jemuran menutup." : "Tutup jemuran lewat kontrol manual.";
  } else {
    state = "dry"; title = "Jemuran aman";
    sub = roofClosed ? "Tidak hujan. Jemuran tertutup." : "Tidak hujan. Jemuran terbuka.";
  }

  const hero = $("hero");
  hero.dataset.state = state;
  hero.dataset.roof = roofClosed ? "closed" : "open";
  setText("rainStatus", title);
  setText("heroSub", sub);
}

function render() {
  const manual = currentMode === "MANUAL";
  const connected = Boolean(client && client.connected);
  const canControl = manual && connected;

  renderHero(connected);

  setText("modeStatus", manual ? "Manual" : "Otomatis");
  setText("servoStatus", SERVO_LABEL[currentServo] ?? currentServo);
  setText("fanStatus", currentFan ? "Menyala" : "Mati");

  setPressed("autoButton", !manual);
  setPressed("manualButton", manual);
  setPressed("openRoofButton", currentServo === "OPEN");
  setPressed("closeRoofButton", currentServo === "CLOSED");
  setPressed("fanOnButton", currentFan);
  setPressed("fanOffButton", !currentFan);

  $("autoButton").disabled = !connected;
  $("manualButton").disabled = !connected;
  $("openRoofButton").disabled = !canControl;
  $("closeRoofButton").disabled = !canControl;
  $("fanOffButton").disabled = !canControl;
  $("fanOnButton").disabled = !canControl || currentServo !== "CLOSED";
}

// Nama lama dipertahankan agar pemanggil lain tetap bekerja.
function updateModeUI() { render(); }
function updateActuatorUI() { render(); }

function parseBoolean(value) {
  if (typeof value === "boolean") return value;
  if (typeof value === "string") return value.toUpperCase() === "ON" || value.toLowerCase() === "true";
  if (typeof value === "number") return value !== 0;
  return false;
}

function handleSensorData(data) {
  if (data.temperature !== undefined && Number.isFinite(Number(data.temperature))) {
    $("temperature").textContent = Number(data.temperature).toFixed(1);
  }

  if (data.humidity !== undefined && Number.isFinite(Number(data.humidity))) {
    $("humidity").textContent = Number(data.humidity).toFixed(1);
  }

  if (data.rain !== undefined) currentRain = parseBoolean(data.rain);

  const rawRain = data.rain_raw ?? data.rainRaw;
  if (rawRain !== undefined) $("rainRaw").textContent = rawRain;

  const roof = data.roof ?? data.servo;
  if (roof !== undefined) currentServo = String(roof).toUpperCase();

  if (data.fan !== undefined) currentFan = parseBoolean(data.fan);
  if (data.mode !== undefined) currentMode = String(data.mode).toUpperCase();

  $("lastUpdate").textContent = new Date().toLocaleTimeString("id-ID");
  render();
}

function handleStatusData(data) {
  if (data.mode) currentMode = String(data.mode).toUpperCase();
  if (data.servo) currentServo = String(data.servo).toUpperCase();
  if (data.roof) currentServo = String(data.roof).toUpperCase();
  if (data.fan !== undefined) currentFan = parseBoolean(data.fan);

  hasReceivedStatus = true;
  render();
}

function connectMQTT() {
  if (typeof mqtt === "undefined") {
    setConnection(false, "MQTT.js tidak termuat");
    setNotice("Periksa koneksi internet atau pemuatan library MQTT.js dari CDN.");
    console.error("MQTT.js tidak ditemukan. Periksa script MQTT.js pada index.html.");
    return;
  }

  if (!MQTT_USERNAME || !MQTT_PASSWORD || MQTT_PASSWORD === "ISI_PASSWORD_MQTT_WEBSITE") {
    setConnection(false, "Konfigurasi belum lengkap");
    setNotice("Isi MQTT_PASSWORD di script.js dengan password akun website EMQX.");
    console.warn("Konfigurasi kredensial MQTT website belum diisi.");
    return;
  }

  const clientId = "SmartRainWeb_" + Math.random().toString(16).slice(2, 10);

  client = mqtt.connect(brokerUrl, {
    clientId,
    username: MQTT_USERNAME,
    password: MQTT_PASSWORD,
    clean: true,
    connectTimeout: 10000,
    reconnectPeriod: 3000,
    keepalive: 60
  });

  client.on("connect", () => {
    console.log("MQTT connected:", brokerUrl);
    setConnection(true, "Connected");
    setNotice("Terhubung ke EMQX. Menunggu data dari ESP32...");

    client.subscribe([TOPIC_DATA, TOPIC_STATUS, TOPIC_AVAIL], { qos: 0 }, (error) => {
      if (error) {
        console.error("Subscribe gagal:", error);
        setNotice("Koneksi berhasil, tetapi subscribe gagal. Periksa Authorization/ACL EMQX.");
      } else {
        console.log("Subscribed:", TOPIC_DATA, TOPIC_STATUS, TOPIC_AVAIL);
      }
    });
    updateModeUI();
  });

  client.on("reconnect", () => {
    setConnection(false, "Reconnecting...");
    setNotice("Mencoba menghubungkan ulang ke EMQX...");
  });

  client.on("close", () => {
    setConnection(false, "Disconnected");
  });

  client.on("offline", () => {
    setConnection(false, "Offline");
  });

  client.on("error", (error) => {
    console.error("MQTT error:", error);
    setConnection(false, "Connection error");
    setNotice("Koneksi MQTT gagal. Periksa WSS, username/password, dan ACL EMQX.");
  });

  client.on("message", (topic, payload) => {
    const message = payload.toString();
    console.log("MQTT message:", topic, message);

    if (topic === TOPIC_AVAIL) {
      deviceOnline = message.trim().toLowerCase() === "online";
      render();
      return;
    }

    try {
      const data = JSON.parse(message);
      if (topic === TOPIC_DATA) handleSensorData(data);
      if (topic === TOPIC_STATUS) handleStatusData(data);
    } catch (error) {
      console.error("Payload JSON tidak valid:", error, message);
    }
  });
}

function publishControl(payload) {
  if (!client || !client.connected) {
    setNotice("Website belum terhubung ke MQTT. Periksa koneksi broker.");
    return;
  }

  const message = JSON.stringify(payload);
  client.publish(TOPIC_CONTROL, message, { qos: 0, retain: false }, (error) => {
    if (error) {
      console.error("Publish control gagal:", error);
      setNotice("Perintah gagal dikirim. Periksa izin publish pada ACL EMQX.");
      return;
    }

    console.log("Control published:", message);
    setNotice("Perintah dikirim. Menunggu pembaruan status dari ESP32...");
  });
}

function sendMode(mode) {
  if (mode === "AUTO") {
    publishControl({ mode: "AUTO" });
    return;
  }

  publishControl({
    mode: "MANUAL",
    servo: currentServo,
    fan: currentFan
  });
}

function sendManualState(servo, fan) {
  if (currentMode !== "MANUAL") {
    setNotice("Aktifkan mode MANUAL terlebih dahulu.");
    return;
  }

  if (fan && servo !== "CLOSED") {
    setNotice("Kipas hanya dapat dinyalakan ketika jemuran tertutup.");
    return;
  }

  publishControl({ mode: "MANUAL", servo, fan });
}

$("autoButton").addEventListener("click", () => sendMode("AUTO"));
$("manualButton").addEventListener("click", () => sendMode("MANUAL"));
$("openRoofButton").addEventListener("click", () => sendManualState("OPEN", false));
$("closeRoofButton").addEventListener("click", () => sendManualState("CLOSED", currentFan));
$("fanOnButton").addEventListener("click", () => sendManualState(currentServo, true));
$("fanOffButton").addEventListener("click", () => sendManualState(currentServo, false));

render();
connectMQTT();
