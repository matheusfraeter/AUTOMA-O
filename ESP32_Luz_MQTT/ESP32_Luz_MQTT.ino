/*
  CASA CONECTADA - ESP32 + EMQX CLOUD
  Firmware sincronizado com index.html.

  TOPICOS MQTT:
    COMANDO (HTML -> ESP32): senaihorizontina/sala/luz
    ESTADO  (ESP32 -> HTML): senaihorizontina/sala/luz/estado

  PAYLOADS:
    Comando: ON / OFF
    Estado:  ON / OFF

  LED:
    LED integrado no GPIO 2 (conforme a placa original do projeto).

  BIBLIOTECAS:
    - PubSubClient (Nick O'Leary)
    - WiFiManager (tzapu)
    - ArduinoJson (Benoit Blanchon)

  IMPORTANTE:
    net.setInsecure() e usado para facilitar o primeiro teste em aula.
    Para uso definitivo, baixe a CA do deployment EMQX e use setCACert().
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// -----------------------------------------------------------------------------
// EMQX Cloud - valores padrao
// -----------------------------------------------------------------------------

char device_name[24] = "ESP32-SALA";
char mqtt_server[96] = "w8767ca9.ala.us-east-1.emqxsl.com";
char mqtt_port[6] = "8883";
char mqtt_user[48] = "";
char mqtt_pass[64] = "";

// -----------------------------------------------------------------------------
// Topicos fixos - nao alterar somente no ESP32 ou somente no HTML
// -----------------------------------------------------------------------------

const char TOPIC_COMANDO[] = "senaihorizontina/sala/luz";
const char TOPIC_ESTADO[]  = "senaihorizontina/sala/luz/estado";

// -----------------------------------------------------------------------------
// LED integrado
// -----------------------------------------------------------------------------

const uint8_t LED_PIN = 2;

// Em muitas placas ESP32 DevKit, HIGH acende e LOW apaga.
// Se o LED integrado da sua placa for ativo em LOW, altere para true.
const bool LED_ATIVO_EM_LOW = false;

bool ledLigado = false;
bool shouldSaveConfig = false;

unsigned long ultimaTentativaMQTT = 0;
unsigned long ultimaTentativaWiFi = 0;
unsigned long inicioPressaoBoot = 0;
bool resetWiFiSolicitado = false;

WiFiClientSecure secureClient;
PubSubClient mqtt(secureClient);
WiFiManager wifiManager;

// -----------------------------------------------------------------------------
// LED e publicacao de estado
// -----------------------------------------------------------------------------

void aplicarEstadoLED() {
  const uint8_t nivelLigado = LED_ATIVO_EM_LOW ? LOW : HIGH;
  const uint8_t nivelDesligado = LED_ATIVO_EM_LOW ? HIGH : LOW;

  digitalWrite(LED_PIN, ledLigado ? nivelLigado : nivelDesligado);
}

void publicarEstado() {
  if (!mqtt.connected()) {
    return;
  }

  const char* payload = ledLigado ? "ON" : "OFF";
  const bool enviado = mqtt.publish(TOPIC_ESTADO, payload, true);

  Serial.print("[MQTT PUB] ");
  Serial.print(TOPIC_ESTADO);
  Serial.print(" -> ");
  Serial.print(payload);
  Serial.println(enviado ? " [OK - RETAIN]" : " [ERRO]");
}

// -----------------------------------------------------------------------------
// Persistencia das configuracoes MQTT
// -----------------------------------------------------------------------------

void copiarCampo(JsonDocument& json, const char* chave,
                 char* destino, size_t tamanho) {
  const char* valor = json[chave];

  if (valor != nullptr && valor[0] != 0) {
    strlcpy(destino, valor, tamanho);
  }
}

void carregarConfiguracaoMQTT() {
  if (!SPIFFS.begin(true)) {
    Serial.println("[SPIFFS] Erro ao montar sistema de arquivos.");
    return;
  }

  if (!SPIFFS.exists("/config.json")) {
    Serial.println("[SPIFFS] Sem configuracao anterior; usando padroes.");
    return;
  }

  File arquivo = SPIFFS.open("/config.json", "r");
  if (!arquivo) {
    Serial.println("[SPIFFS] Nao foi possivel abrir config.json.");
    return;
  }

  DynamicJsonDocument json(1536);
  DeserializationError erro = deserializeJson(json, arquivo);
  arquivo.close();

  if (erro) {
    Serial.print("[SPIFFS] JSON invalido: ");
    Serial.println(erro.c_str());
    return;
  }

  copiarCampo(json, "device_name", device_name, sizeof(device_name));
  copiarCampo(json, "mqtt_server", mqtt_server, sizeof(mqtt_server));
  copiarCampo(json, "mqtt_port", mqtt_port, sizeof(mqtt_port));
  copiarCampo(json, "mqtt_user", mqtt_user, sizeof(mqtt_user));
  copiarCampo(json, "mqtt_pass", mqtt_pass, sizeof(mqtt_pass));

  Serial.println("[SPIFFS] Configuracao MQTT carregada.");
}

void salvarConfiguracaoMQTT() {
  DynamicJsonDocument json(1536);

  json["device_name"] = device_name;
  json["mqtt_server"] = mqtt_server;
  json["mqtt_port"] = mqtt_port;
  json["mqtt_user"] = mqtt_user;
  json["mqtt_pass"] = mqtt_pass;

  File arquivo = SPIFFS.open("/config.json", "w");
  if (!arquivo) {
    Serial.println("[SPIFFS] Erro ao salvar config.json.");
    return;
  }

  const size_t bytes = serializeJson(json, arquivo);
  arquivo.close();

  Serial.print("[SPIFFS] Configuracao salva. Bytes: ");
  Serial.println(bytes);
}

void saveConfigCallback() {
  shouldSaveConfig = true;
  Serial.println("[CONFIG] Alteracoes MQTT detectadas.");
}

// -----------------------------------------------------------------------------
// Portal de configuracao Wi-Fi e MQTT
// -----------------------------------------------------------------------------

void configurarWiFi() {
  carregarConfiguracaoMQTT();

  WiFiManagerParameter campoNome(
      "devicename", "Nome do dispositivo",
      device_name, sizeof(device_name)
  );

  WiFiManagerParameter campoServidor(
      "server", "Servidor MQTT / EMQX",
      mqtt_server, sizeof(mqtt_server)
  );

  WiFiManagerParameter campoPorta(
      "port", "Porta MQTT TLS",
      mqtt_port, sizeof(mqtt_port)
  );

  WiFiManagerParameter campoUsuario(
      "user", "Usuario MQTT / EMQX",
      mqtt_user, sizeof(mqtt_user)
  );

  WiFiManagerParameter campoSenha(
      "pass", "Senha MQTT / EMQX",
      mqtt_pass, sizeof(mqtt_pass),
      "type=\"password\""
  );

  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.addParameter(&campoNome);
  wifiManager.addParameter(&campoServidor);
  wifiManager.addParameter(&campoPorta);
  wifiManager.addParameter(&campoUsuario);
  wifiManager.addParameter(&campoSenha);

  wifiManager.setConfigPortalTimeout(180);
  wifiManager.setConnectTimeout(25);
  wifiManager.setMinimumSignalQuality(15);

  // Nome e senha individuais para o portal de cada placa.
  const uint64_t mac = ESP.getEfuseMac();
  char sufixo[9];
  snprintf(
      sufixo,
      sizeof(sufixo),
      "%08lX",
      static_cast<unsigned long>(mac & 0xFFFFFFFF)
  );

  const String nomePortal = String("CASA-ESP32-") + sufixo;
  const String senhaPortal = String("CFG-") + sufixo;

  Serial.println();
  Serial.println("[WIFI] Portal de configuracao:");
  Serial.print("[WIFI] SSID: ");
  Serial.println(nomePortal);
  Serial.print("[WIFI] Senha: ");
  Serial.println(senhaPortal);

  if (!wifiManager.autoConnect(nomePortal.c_str(), senhaPortal.c_str())) {
    Serial.println("[WIFI] Falha ao conectar. Reiniciando ESP32.");
    delay(1000);
    ESP.restart();
  }

  // Copia os parametros que foram apresentados no portal.
  strlcpy(device_name, campoNome.getValue(), sizeof(device_name));
  strlcpy(mqtt_server, campoServidor.getValue(), sizeof(mqtt_server));
  strlcpy(mqtt_port, campoPorta.getValue(), sizeof(mqtt_port));
  strlcpy(mqtt_user, campoUsuario.getValue(), sizeof(mqtt_user));
  strlcpy(mqtt_pass, campoSenha.getValue(), sizeof(mqtt_pass));

  if (shouldSaveConfig) {
    salvarConfiguracaoMQTT();
    shouldSaveConfig = false;
  }

  Serial.println("[WIFI] Conectado.");
  Serial.print("[WIFI] IP: ");
  Serial.println(WiFi.localIP());

  Serial.print("[MQTT] Host: ");
  Serial.println(mqtt_server);
  Serial.print("[MQTT] Porta TLS: ");
  Serial.println(mqtt_port);
  Serial.print("[MQTT] Usuario configurado: ");
  Serial.println(strlen(mqtt_user) > 0 ? "SIM" : "NAO");

  Serial.print("[MQTT] Topico de comando: ");
  Serial.println(TOPIC_COMANDO);
  Serial.print("[MQTT] Topico de estado: ");
  Serial.println(TOPIC_ESTADO);
}

// -----------------------------------------------------------------------------
// Callback MQTT - recebe ON/OFF e controla o LED
// -----------------------------------------------------------------------------

void callbackMQTT(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, TOPIC_COMANDO) != 0) {
    Serial.print("[MQTT] Topico ignorado: ");
    Serial.println(topic);
    return;
  }

  String mensagem;
  mensagem.reserve(length + 1);

  for (unsigned int i = 0; i < length; i++) {
    mensagem += static_cast<char>(payload[i]);
  }

  mensagem.trim();
  mensagem.toUpperCase();

  Serial.print("[MQTT RX] ");
  Serial.print(topic);
  Serial.print(" -> ");
  Serial.println(mensagem);

  if (mensagem == "ON") {
    ledLigado = true;
  } else if (mensagem == "OFF") {
    ledLigado = false;
  } else {
    Serial.println("[MQTT] Payload invalido. Use ON ou OFF.");
    return;
  }

  aplicarEstadoLED();

  Serial.print("[LED GPIO 2] ");
  Serial.println(ledLigado ? "LIGADO" : "DESLIGADO");

  // O site recebe ON ou OFF simples no topico de estado.
  publicarEstado();
}

// -----------------------------------------------------------------------------
// Diagnostico de falhas MQTT
// -----------------------------------------------------------------------------

void imprimirErroMQTT(int estado) {
  Serial.print("[MQTT] Falha. Codigo ");
  Serial.print(estado);
  Serial.print(": ");

  switch (estado) {
    case -4: Serial.println("timeout"); break;
    case -3: Serial.println("conexao perdida"); break;
    case -2: Serial.println("falha de rede ou TLS"); break;
    case -1: Serial.println("desconectado"); break;
    case  1: Serial.println("protocolo incorreto"); break;
    case  2: Serial.println("Client ID rejeitado"); break;
    case  3: Serial.println("broker indisponivel"); break;
    case  4: Serial.println("usuario ou senha incorretos"); break;
    case  5: Serial.println("cliente nao autorizado"); break;
    default: Serial.println("erro nao identificado"); break;
  }
}

// -----------------------------------------------------------------------------
// Reconexao MQTT nao bloqueante
// -----------------------------------------------------------------------------

void conectarMQTT() {
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (millis() - ultimaTentativaMQTT < 4000) {
    return;
  }

  ultimaTentativaMQTT = millis();

  const String clientId = String(device_name) + "-" +
                          String(random(0xFFFF), HEX);

  Serial.print("[MQTT] Conectando ao EMQX... ");

  bool conectado = false;

  if (strlen(mqtt_user) > 0) {
    conectado = mqtt.connect(
        clientId.c_str(),
        mqtt_user,
        mqtt_pass
    );
  } else {
    conectado = mqtt.connect(clientId.c_str());
  }

  if (!conectado) {
    imprimirErroMQTT(mqtt.state());
    return;
  }

  Serial.println("CONECTADO.");

  if (mqtt.subscribe(TOPIC_COMANDO, 1)) {
    Serial.print("[MQTT] Inscrito em: ");
    Serial.println(TOPIC_COMANDO);
  } else {
    Serial.println("[MQTT] ERRO ao assinar topico de comando.");
  }

  // Sincroniza o painel com o estado atual do LED.
  publicarEstado();
}

// -----------------------------------------------------------------------------
// Botao BOOT - segurar por 5 segundos depois da inicializacao para resetar Wi-Fi
// Nao segurar BOOT ao energizar a placa.
// -----------------------------------------------------------------------------

void verificarBotaoBoot() {
  if (digitalRead(0) == LOW) {
    if (inicioPressaoBoot == 0) {
      inicioPressaoBoot = millis();
    }

    if (!resetWiFiSolicitado && millis() - inicioPressaoBoot >= 5000) {
      resetWiFiSolicitado = true;

      Serial.println("[WIFI] Apagando configuracao Wi-Fi salva.");
      wifiManager.resetSettings();
      delay(500);
      ESP.restart();
    }
  } else {
    inicioPressaoBoot = 0;
    resetWiFiSolicitado = false;
  }
}

// -----------------------------------------------------------------------------
// Arduino
// -----------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(400);

  pinMode(LED_PIN, OUTPUT);
  ledLigado = false;
  aplicarEstadoLED();

  pinMode(0, INPUT_PULLUP);
  randomSeed(static_cast<uint32_t>(ESP.getEfuseMac()));

  configurarWiFi();

  // TLS para o listener Serverless do EMQX na porta 8883.
  // Para producao, troque por secureClient.setCACert(CA_DO_EMQX).
  secureClient.setInsecure();

  mqtt.setServer(mqtt_server, atoi(mqtt_port));
  mqtt.setCallback(callbackMQTT);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(5);
  mqtt.setBufferSize(512);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" CASA CONECTADA - ESP32 PRONTO");
  Serial.println(" LED integrado: GPIO 2");
  Serial.print(" COMANDO: ");
  Serial.println(TOPIC_COMANDO);
  Serial.print(" ESTADO:  ");
  Serial.println(TOPIC_ESTADO);
  Serial.println("========================================");
}

void loop() {
  verificarBotaoBoot();

  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - ultimaTentativaWiFi >= 8000) {
      ultimaTentativaWiFi = millis();
      Serial.println("[WIFI] Desconectado. Tentando reconectar...");
      WiFi.reconnect();
    }
    return;
  }

  if (!mqtt.connected()) {
    conectarMQTT();
  } else {
    mqtt.loop();
  }
}
