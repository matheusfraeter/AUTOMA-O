# Integração do painel HTML com o ESP32 original

Esta atualização foi preparada para funcionar **sem alterar nem regravar o firmware original do ESP32**.

## Configuração no HTML

- Broker WSS: `wss://w8767ca9.ala.us-east-1.emqxsl.com:8084/mqtt`
- Raiz do comando: `senaihorizontina`
- Tópico PUBLISHER de retorno: `senai/esp32/estado` (padrão do sketch original)

O site publica `ON` ou `OFF` em:

```text
senaihorizontina/sala/luz
```

O sketch original confirma a execução publicando no tópico PUBLISHER:

```text
senaihorizontina/sala/luz=ON
senaihorizontina/sala/luz=OFF
```

O HTML escuta esse tópico de retorno e extrai o estado da mensagem.

## Única configuração necessária no ESP32 (sem modificar código)

O sketch original tem como valor padrão de SUBSCRIBER `senai/#`, que não corresponde ao comando acima. Abra o portal WiFiManager já utilizado pelo seu ESP32 e ajuste somente o campo:

- SUBSCRIBER: `senaihorizontina/#`

Mantenha o PUBLISHER como `senai/esp32/estado`, ou informe no campo correspondente do HTML o valor que já estiver salvo no ESP32.

Isso altera apenas a configuração salva pelo WiFiManager; não exige editar, compilar ou gravar o sketch.

## Credenciais

Digite no site o usuário e a senha cadastrados no EMQX. Não coloque credenciais no arquivo HTML público.

## Hardware

O sketch original já contém `digitalWrite(PIN_SALA_LUZ, on)` com `PIN_SALA_LUZ = 2`. A placa precisa ter o LED integrado ligado ao GPIO 2 e com polaridade compatível (HIGH acende).

A página mantém ar-condicionado, televisão e ventilador indisponíveis no modo hardware porque o ESP32 original enviado controla somente o LED da sala.

## Segurança

O firmware original utiliza `WiFiClientSecure::setInsecure()`. Para um projeto definitivo, substitua a validação insegura do certificado pela validação da CA do broker. Não compartilhe usuário e senha MQTT.
