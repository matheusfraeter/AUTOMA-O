# Atualização da automação residencial — ESP32 + EMQX

## O que foi preparado

- `aplicar_atualizacao_html.py`: atualiza o `index.html` existente, preservando o layout. Cria um backup antes de gravar.
- `ESP32_Luz_MQTT.ino`: firmware do ESP32 compatível com o painel HTML.

## 1. Atualizar o HTML

Coloque `aplicar_atualizacao_html.py` na mesma pasta do `index.html` e execute:

```powershell
python .\aplicar_atualizacao_html.py
```

O script cria `index.html.bak` e altera:
- endpoint do navegador para `wss://w8767ca9.ala.us-east-1.emqxsl.com:8084/mqtt`;
- tópico base para `aula/automacao/residencia01`;
- modo hardware como padrão;
- credenciais como campos digitados pelo usuário, sem colocá-las no arquivo público;
- cartões de ar-condicionado, TV e ventilador como não configurados no ESP32 atual.

## 2. Gravar o ESP32

Abra `ESP32_Luz_MQTT.ino` na Arduino IDE.

Instale as bibliotecas:
- PubSubClient
- WiFiManager
- ArduinoJson

Selecione sua placa ESP32 e faça a gravação.

Na primeira inicialização:
1. Abra o Monitor Serial em 115200 baud.
2. Procure a rede `FRAETER-SETUP`.
3. A senha do portal será exibida no Monitor Serial.
4. Entre no portal e configure o Wi-Fi e as credenciais de usuário/senha MQTT do EMQX.
5. Mantenha os tópicos:
   - SUB: `aula/automacao/residencia01/luz_sala/command`
   - PUB: `aula/automacao/residencia01/luz_sala/state`

O firmware usa MQTT com TLS na porta `8883`. O HTML usa WebSocket seguro (WSS) na porta `8084`, caminho `/mqtt`.

## 3. Configurar a página

Na página Casa Conectada:
1. Digite o mesmo usuário e senha configurados no EMQX.
2. Confirme o endereço WSS já preenchido.
3. Deixe desmarcada a opção de demonstração (modo hardware).
4. Clique em **Conectar MQTT**.
5. Use o botão da **Luz da sala**.

O ESP32 recebe `ON` ou `OFF` no tópico `.../luz_sala/command` e publica o estado confirmado no tópico `.../luz_sala/state`, com retain habilitado. Assim, a página também recupera o último estado publicado quando se conecta.

## 4. Pino e segurança elétrica

O firmware usa GPIO 2 como saída ativa em nível HIGH, adequado para o LED de teste usado no código original. Alguns módulos de relé são ativos em LOW; nesse caso, a lógica do pino precisa ser invertida.

Não ligue uma lâmpada da rede elétrica diretamente ao ESP32. Use um módulo de relé apropriado, isolado e dimensionado para a carga, com montagem supervisionada.

O firmware mantém `net.setInsecure()` para simplificar o primeiro teste TLS. Isso criptografa o tráfego, mas não valida a identidade do servidor. Para uso definitivo, instale o certificado CA do EMQX e substitua por `net.setCACert()`.

## 5. Publicar no GitHub

Depois de testar localmente, envie para o repositório:
- substitua `index.html` pelo arquivo atualizado;
- adicione `ESP32_Luz_MQTT.ino`;
- adicione este README, se desejar documentar o funcionamento.

Não publique usuário/senha do broker. As credenciais do HTML são digitadas na própria página e as do ESP32 são configuradas no portal WiFiManager, ficando armazenadas localmente na SPIFFS.