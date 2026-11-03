/*
  CASA CONECTADA - ESP32 + EMQX Cloud
  Controle da luz da sala pelo painel index.html.

  Bibliotecas:
    - PubSubClient
    - WiFiManager
    - ArduinoJson

  Hardware:
    - GPIO 2: saída para LED de teste ou entrada de módulo de relé.
    - GPIO 0 (BOOT): manter pressionado por 5 s DEPOIS que o programa iniciar
      para apagar as credenciais Wi-Fi e reabrir o portal.
      Não mantenha BOOT pressionado durante a energização da placa.

  Segurança:
    - As credenciais MQTT não ficam gravadas neste arquivo.
    - Elas são informadas pelo portal WiFiManager e salvas localmente na SPIFFS.
    - net.setInsecure() é usado apenas para facilitar o teste em sala de aula.
      Em uma instalação definitiva, substitua pela validação do certificado CA.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// -----------------------------------------------------------------------------
// Configuração padrão do EMQX e dos tópicos compartilhados com o HTML
// -----------------------------------------------------------------------------

char device_name[24] = "ESP32-SALA";
char mqtt_server[96] = "w8767ca9.ala.us-east-1.emqxsl.com";
char mqtt_port[6] = "8883";

// Deixe as credenciais vazias no código.
// Configure-as pelo portal WiFiManager.
char mqtt_user[40] = "";
char mqtt_pass[64] = "";

char sub_topic_1[96] =
    "aula/automacao/residencia01/luz_sala/command";

char pub_topic_1[96] =
    "aula/automacao/residencia01/luz_sala/state";

// -----------------------------------------------------------------------------
// Pinos
// -----------------------------------------------------------------------------

// Usa o LED definido pela placa no pacote ESP32. Em placas DevKit comuns,
// LED_BUILTIN normalmente corresponde ao GPIO 2. O fallback mantém compatibilidade.
#ifndef LED_BUILTIN
  #define LED_BUILTIN 2
#endif
const uint8_t PIN_LED_INTEGRADO = LED_BUILTIN;
const uint8_t PIN_BOTAO_CFG = 0;

bool shouldSaveConfig = false;
bool estadoSala = false;

unsigned long ultimaTentativaMQTT = 0;
unsigned long inicioPressaoBotao = 0;
bool resetWiFiExecutado = false;

WiFiClientSecure net;
PubSubClient mqtt(net);
WiFiManager wifiManager;

// -----------------------------------------------------------------------------
// Utilitários
// -----------------------------------------------------------------------------

void copiarCampo(JsonDocument& json, const char* chave,
                 char* destino, size_t tamanho) {
  const char* valor = json[chave];

  if (valor && valor[0] != '\0') {
    strlcpy(destino, valor, tamanho);
  }
}

void saveConfigCallback() {
  Serial.println("[CONFIG] Alterações MQTT serão salvas.");
  shouldSaveConfig = true;
}

void publicarEstadoSala() {
  if (!mqtt.connected()) {
    return;
  }

  const char* estado = estadoSala ? "ON" : "OFF";

  // Retain=true: o navegador recebe o último estado ao se inscrever.
  bool enviado = mqtt.publish(pub_topic_1, estado, true);

  Serial.print("[ESTADO] ");
  Serial.print(pub_topic_1);
  Serial.print(" -> ");
  Serial.print(estado);
  Serial.println(enviado ? " [retido no broker]" : " [falha ao publicar]");
}

// -----------------------------------------------------------------------------
// Recebimento dos comandos enviados pelo HTML
// -----------------------------------------------------------------------------

void onMessage(char* topic, byte* payload, unsigned int length) {
  String topicoRecebido(topic);
  String mensagem;

  mensagem.reserve(length + 1);

  for (unsigned int i = 0; i < length; i++) {
    mensagem += static_cast<char>(payload[i]);
  }

  mensagem.trim();
  mensagem.toUpperCase();

  Serial.print("[MQTT RECEBIDO] ");
  Serial.print(topicoRecebido);
  Serial.print(" -> ");
  Serial.println(mensagem);

  // Este ESP32 controla somente a luz da sala.
  if (topicoRecebido != String(sub_topic_1)) {
    Serial.println("[IGNORADO] Tópico não configurado neste ESP32.");
    return;
  }

  if (mensagem == "ON") {
    estadoSala = true;
  } else if (mensagem == "OFF") {
    estadoSala = false;
  } else {
    Serial.println("[IGNORADO] Payload inválido. Use ON ou OFF.");
    return;
  }

  digitalWrite(PIN_LED_INTEGRADO, estadoSala ? HIGH : LOW);

  Serial.print("[LED INTEGRADO] Luz da sala: ");
  Serial.println(estadoSala ? "LIGADA" : "DESLIGADA");

  // Publica somente ON/OFF no tópico de estado, sem concatenar o tópico.
  publicarEstadoSala();
}

// -----------------------------------------------------------------------------
// Configuração Wi-Fi e portal de parâmetros MQTT
// -----------------------------------------------------------------------------

void setupWiFi() {
  Serial.println();
  Serial.println("[WIFI] Iniciando configuração...");

  if (!SPIFFS.begin(true)) {
    Serial.println("[ERRO] Não foi possível montar a SPIFFS.");
  } else {
    Serial.println("[SPIFFS] Sistema de arquivos montado.");

    if (SPIFFS.exists("/config.json")) {
      File arquivo = SPIFFS.open("/config.json", "r");

      if (arquivo) {
        DynamicJsonDocument json(1536);
        DeserializationError erro = deserializeJson(json, arquivo);

        if (!erro) {
          copiarCampo(json, "device_name", device_name, sizeof(device_name));
          copiarCampo(json, "mqtt_server", mqtt_server, sizeof(mqtt_server));
          copiarCampo(json, "mqtt_port", mqtt_port, sizeof(mqtt_port));
          copiarCampo(json, "mqtt_user", mqtt_user, sizeof(mqtt_user));
          copiarCampo(json, "mqtt_pass", mqtt_pass, sizeof(mqtt_pass));
          copiarCampo(json, "sub_topic_1", sub_topic_1, sizeof(sub_topic_1));
          copiarCampo(json, "pub_topic_1", pub_topic_1, sizeof(pub_topic_1));

          // Migra os tópicos padrão usados pela versão anterior do projeto.
          if (strcmp(sub_topic_1, "senai/#") == 0 ||
              strcmp(sub_topic_1, "senaihorizontina/#") == 0) {
            strlcpy(
                sub_topic_1,
                "aula/automacao/residencia01/luz_sala/command",
                sizeof(sub_topic_1)
            );
          }

          if (strcmp(pub_topic_1, "senai/esp32/estado") == 0) {
            strlcpy(
                pub_topic_1,
                "aula/automacao/residencia01/luz_sala/state",
                sizeof(pub_topic_1)
            );
          }

          Serial.println("[SPIFFS] Configuração MQTT carregada.");
        } else {
          Serial.print("[ERRO] JSON inválido: ");
          Serial.println(erro.c_str());
        }

        arquivo.close();
      }
    }
  }

  WiFiManagerParameter campoNome(
      "devicename", "Nome do dispositivo",
      device_name, sizeof(device_name)
  );

  WiFiManagerParameter campoServidor(
      "server", "Servidor MQTT (host)",
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

  WiFiManagerParameter campoInscricao(
      "subtopic", "Topico de comando (SUB)",
      sub_topic_1, sizeof(sub_topic_1)
  );

  WiFiManagerParameter campoPublicacao(
      "pubtopic", "Topico de estado (PUB)",
      pub_topic_1, sizeof(pub_topic_1)
  );

  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.addParameter(&campoNome);
  wifiManager.addParameter(&campoServidor);
  wifiManager.addParameter(&campoPorta);
  wifiManager.addParameter(&campoUsuario);
  wifiManager.addParameter(&campoSenha);
  wifiManager.addParameter(&campoInscricao);
  wifiManager.addParameter(&campoPublicacao);

  wifiManager.setMinimumSignalQuality(20);
  wifiManager.setTimeout(180);

  // Senha individual por placa para proteger o portal de configuração.
  char senhaPortal[20];
  uint32_t identificador = static_cast<uint32_t>(ESP.getEfuseMac());
  snprintf(senhaPortal, sizeof(senhaPortal), "CFG-%08lX",
           static_cast<unsigned long>(identificador));

  Serial.println("[WIFI] Conecte-se ao portal FRAETER-SETUP.");
  Serial.print("[WIFI] Senha do portal: ");
  Serial.println(senhaPortal);

  if (!wifiManager.autoConnect("FRAETER-SETUP", senhaPortal)) {
    Serial.println("[WIFI] Não foi possível conectar. Reiniciando...");
    delay(1000);
    ESP.restart();
  }

  // Copia os valores informados no portal para os buffers do programa.
  strlcpy(device_name, campoNome.getValue(), sizeof(device_name));
  strlcpy(mqtt_server, campoServidor.getValue(), sizeof(mqtt_server));
  strlcpy(mqtt_port, campoPorta.getValue(), sizeof(mqtt_port));
  strlcpy(mqtt_user, campoUsuario.getValue(), sizeof(mqtt_user));
  strlcpy(mqtt_pass, campoSenha.getValue(), sizeof(mqtt_pass));
  strlcpy(sub_topic_1, campoInscricao.getValue(), sizeof(sub_topic_1));
  strlcpy(pub_topic_1, campoPublicacao.getValue(), sizeof(pub_topic_1));

  if (shouldSaveConfig) {
    DynamicJsonDocument json(1536);

    json["device_name"] = device_name;
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_user"] = mqtt_user;
    json["mqtt_pass"] = mqtt_pass;
    json["sub_topic_1"] = sub_topic_1;
    json["pub_topic_1"] = pub_topic_1;

    File arquivo = SPIFFS.open("/config.json", "w");

    if (arquivo) {
      serializeJson(json, arquivo);
      arquivo.close();
      Serial.println("[SPIFFS] Configuração salva.");
    } else {
      Serial.println("[ERRO] Não foi possível salvar /config.json.");
    }
  }

  Serial.println("[WIFI] Conectado.");
  Serial.print("[WIFI] IP: ");
  Serial.println(WiFi.localIP());

  Serial.print("[MQTT] Servidor: ");
  Serial.println(mqtt_server);

  Serial.print("[MQTT] Porta: ");
  Serial.println(mqtt_port);

  Serial.print("[MQTT] SUB: ");
  Serial.println(sub_topic_1);

  Serial.print("[MQTT] PUB: ");
  Serial.println(pub_topic_1);
}

// -----------------------------------------------------------------------------
// Reconexão MQTT sem loop bloqueante
// -----------------------------------------------------------------------------

void conectarMQTT() {
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (millis() - ultimaTentativaMQTT < 3000) {
    return;
  }

  ultimaTentativaMQTT = millis();

  String clientId = String(device_name) + "_" +
                    String(random(0xffff), HEX);

  Serial.print("[MQTT] Tentando conectar... ");

  bool conectado = mqtt.connect(
      clientId.c_str(),
      mqtt_user,
      mqtt_pass
  );

  if (conectado) {
    Serial.println("conectado.");

    if (mqtt.subscribe(sub_topic_1, 1)) {
      Serial.print("[MQTT] Inscrito em: ");
      Serial.println(sub_topic_1);
    } else {
      Serial.println("[MQTT] Falha ao assinar o tópico de comando.");
    }

    // Publica o estado inicial/atual como mensagem retida.
    publicarEstadoSala();
  } else {
    Serial.print("[MQTT] Falha. Código: ");
    Serial.println(mqtt.state());
  }
}

// -----------------------------------------------------------------------------
// Botão BOOT: apagar Wi-Fi salvo após pressionar por 5 segundos em execução
// -----------------------------------------------------------------------------

void verificarBotaoConfiguracao() {
  if (digitalRead(PIN_BOTAO_CFG) == LOW) {
    if (inicioPressaoBotao == 0) {
      inicioPressaoBotao = millis();
    }

    if (!resetWiFiExecutado &&
        millis() - inicioPressaoBotao >= 5000) {
      resetWiFiExecutado = true;

      Serial.println("[WIFI] Apagando configuração Wi-Fi...");
      wifiManager.resetSettings();
      delay(500);
      ESP.restart();
    }
  } else {
    inicioPressaoBotao = 0;
    resetWiFiExecutado = false;
  }
}

// -----------------------------------------------------------------------------
// Arduino
// -----------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_LED_INTEGRADO, OUTPUT);
  digitalWrite(PIN_LED_INTEGRADO, LOW);
  estadoSala = false;

  pinMode(PIN_BOTAO_CFG, INPUT_PULLUP);

  setupWiFi();

  // Adequado para testes em sala de aula.
  // Para uso definitivo, instale o certificado CA do EMQX e use setCACert().
  net.setInsecure();

  mqtt.setServer(mqtt_server, atoi(mqtt_port));
  mqtt.setCallback(onMessage);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(5);

  randomSeed(static_cast<uint32_t>(ESP.getEfuseMac()));

  Serial.println("[SISTEMA] ESP32 pronto.");
}

void loop() {
  verificarBotaoConfiguracao();

  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long ultimaTentativaWiFi = 0;

    if (millis() - ultimaTentativaWiFi >= 5000) {
      ultimaTentativaWiFi = millis();
      Serial.println("[WIFI] Conexão perdida. Tentando reconectar...");
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