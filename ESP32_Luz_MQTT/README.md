# Casa Conectada — HTML + ESP32 + EMQX Cloud

Este projeto permite controlar o **LED integrado da placa ESP32 (GPIO 2)** pela página `index.html`, usando o EMQX Cloud como broker MQTT.

## Comunicação padronizada

Os dois programas usam exatamente os mesmos tópicos:

| Direção | Tópico | Payload |
|---|---|---|
| Site → ESP32 | `senaihorizontina/sala/luz` | `ON` ou `OFF` |
| ESP32 → Site | `senaihorizontina/sala/luz/estado` | `ON` ou `OFF` |

O ESP32 publica o estado com `retain=true`, portanto o site recebe o último estado assim que assina o tópico.

## Endereços EMQX Serverless

- Host: `w8767ca9.ala.us-east-1.emqxsl.com`
- ESP32: MQTT com TLS, porta `8883`
- Navegador: WSS, porta `8084`, caminho `/mqtt`
- URL WSS: `wss://w8767ca9.ala.us-east-1.emqxsl.com:8084/mqtt`

Confirme host e portas no Overview do seu deployment EMQX. A documentação do Serverless informa 8883 para MQTT/TLS e 8084 para WebSocket/TLS.

## 1. Gravar o ESP32

Abra `ESP32_Luz_MQTT.ino` na Arduino IDE.

Instale pelo Library Manager:
- **PubSubClient** — Nick O'Leary
- **WiFiManager** — tzapu
- **ArduinoJson** — Benoit Blanchon

Selecione sua placa ESP32 e a porta COM correta. Abra o Monitor Serial em **115200 baud**.

O programa preserva os parâmetros MQTT que já estiverem salvos em `/config.json` na SPIFFS, usando os mesmos nomes de campos do firmware anterior (`device_name`, `mqtt_server`, `mqtt_port`, `mqtt_user`, `mqtt_pass`).

### Primeira configuração ou alteração do Wi-Fi

Na primeira inicialização, o ESP32 cria uma rede de configuração. O nome e a senha aparecem no Monitor Serial:

1. Conecte o celular/computador à rede `CASA-ESP32-XXXXXXXX`.
2. Abra o portal que aparece no celular ou navegue para `192.168.4.1`.
3. Escolha a rede Wi-Fi da casa.
4. Informe o host `w8767ca9.ala.us-east-1.emqxsl.com` e a porta `8883`.
5. Informe o usuário e a senha de autenticação MQTT usados no EMQX.
6. Salve.

Para abrir novamente o portal quando o ESP32 já tem Wi-Fi salvo, mantenha BOOT pressionado por 5 segundos **depois que a placa iniciou**. Isso limpa a configuração Wi-Fi salva e reinicia o portal. Não mantenha BOOT pressionado durante a energização.

## 2. Configurar o site

Abra o GitHub Pages ou o `index.html`:

1. Deixe o endereço WSS como `wss://w8767ca9.ala.us-east-1.emqxsl.com:8084/mqtt`.
2. Deixe a raiz como `senaihorizontina`.
3. Deixe o tópico de estado como `senaihorizontina/sala/luz/estado`.
4. Digite o mesmo usuário e senha MQTT configurados no EMQX/ESP32.
5. Deixe **Modo demonstração** desmarcado.
6. Clique em **Conectar MQTT**.
7. Clique em **Ligar luz** ou **Desligar luz**.

As credenciais digitadas no site não são salvas no HTML público do GitHub. Depois de atualizar ou reabrir a página, será necessário digitá-las novamente.

A biblioteca MQTT.js é carregada por CDN; o navegador precisa de acesso à internet para carregar a biblioteca e abrir o WebSocket.

## 3. LED integrado

O firmware aciona diretamente o GPIO 2. A configuração padrão considera:
- HIGH = LED ligado
- LOW = LED desligado

Se a variante da placa usar LED ativo em LOW, altere `LED_ATIVO_EM_LOW = true`. Algumas variantes ESP32 não têm LED integrado no GPIO 2; confira a identificação/esquema da placa.

## 4. TLS e segurança

O sketch usa `secureClient.setInsecure()` para facilitar o primeiro teste em laboratório. O tráfego é TLS, mas o cliente ESP32 não valida a identidade do servidor.

Para uso permanente, baixe a CA apropriada no Overview do deployment EMQX e substitua por `secureClient.setCACert(certificadoCA)`.

Não publique usuário ou senha do EMQX em arquivos públicos.

## 5. Diagnóstico

No Monitor Serial, depois de conectar, deve aparecer:

```text
[MQTT] Conectando ao EMQX... CONECTADO.
[MQTT] Inscrito em: senaihorizontina/sala/luz
```

Ao clicar em **Ligar luz** no site:

```text
[MQTT RX] senaihorizontina/sala/luz -> ON
[LED GPIO 2] LIGADO
[MQTT PUB] senaihorizontina/sala/luz/estado -> ON [OK - RETAIN]
```

Ao desligar, a mesma sequência aparece com `OFF`.

### Códigos de erro MQTT do ESP32

- `4`: usuário ou senha incorretos.
- `5`: cliente não autorizado.
- `-2`: falha de rede ou TLS.
- `-4`: timeout de conexão.

Se o site conecta mas não recebe o estado, confira se o ESP32 está inscrito em `senaihorizontina/sala/luz` e se ambos os clientes usam o mesmo deployment EMQX e as mesmas credenciais.
