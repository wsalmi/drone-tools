# Arquitetura de Reconstrução — Drone Telemetry Monitor

## 1. Objetivo operacional

Esta reconstrução mantém o **M5 Cardputer ADV** como console autônomo de campo e altera o foco de RF para **Remote ID por Wi‑Fi e BLE**, usando o **SX1262 do M5 Cap LoRa868** como monitor passivo complementar. A USB-C não é utilizada por receptores externos, SoftAP ou host USB; ela permanece disponível para log serial, flash e atualização de firmware.

O firmware continua processando eventos internamente e a interface Web apenas espelha e controla o dispositivo via Serial. Não há endpoint HTTP embarcado, página hospedada no dispositivo ou dependência de rede local.

| Área | Decisão | Critério de aceite |
|---|---|---|
| Detecção primária | Wi‑Fi Remote ID e BLE Remote ID | Fontes executam sem depender de LoRa, SD ou GPS. |
| Detecção complementar | SX1262 do Cap LoRa868 em modo passivo | Rádio é opcional, inicializa por SPI e seu estado é visível à UI. |
| USB | Canal de atualização/console | Nenhum código de RTL-SDR, USB Host ou SoftAP é iniciado. |
| Campo | Atalhos `1–7`, setas, Enter e Esc | A numeração visível corresponde à navegação efetiva. |
| Painel externo | Web Serial no navegador | Todas as ações operacionais têm comandos equivalentes no protocolo Serial. |
| Desenvolvimento | Emulador determinístico | Cenários reproduzíveis exercitam o mesmo modelo de telemetria do painel. |

## 2. Hardware e barramentos

O Cap LoRa868 combina um SX1262 ligado por SPI e um GNSS por UART, com conexões fixas para Cardputer ADV. A configuração de firmware usará `SCK=GPIO40`, `MOSI=GPIO14`, `MISO=GPIO39`, `NSS=GPIO5`, `RST=GPIO3`, `IRQ=GPIO4`, `BUSY=GPIO6`, e GNSS em `RX=GPIO13` / `TX=GPIO15` a 115200 bps 8N1.[1]

> O rádio é operado como observador: nenhuma rotina de transmissão é iniciada pelo Monitor.

## 3. Fluxo de dados

```text
Wi‑Fi RID ─┐
BLE RID ───┼──> Detection Service ──> Decoder ──> Aircraft Registry ──> UI local
SX1262 ────┘                                                    │
                                                                ├──> Data Logger (SD)
                                                                └──> Serial Bridge (JSON Lines)
Emulador ──────────────────────────────────────────────────────┘
```

Cada mensagem de telemetria é derivada do `Aircraft Registry`, que é a fonte única de verdade. O simulador escreve nesse mesmo registro por meio de uma API controlada; não injeta dados diretamente na tela nem no site. Assim, o painel local, a ponte Serial, o log e o emulador observam o mesmo estado.

## 4. Interface local de campo

| Tecla | Ação | Tela / efeito |
|---:|---|---|
| `1` | Varredura | Lista de aeronaves e fontes de recepção. |
| `2` | Radar | Visão relativa de alvos e rumo. |
| `3` | Mapa | Posição, distância e operador quando disponível. |
| `4` | Modos | Wi‑Fi, BLE, SX1262, GNSS, alerta, Serial e simulação. |
| `5` | Configuração | Parâmetros persistentes sem opções de SDR. |
| `6` | Registros | Histórico e exportação por SD quando presente. |
| `7` | Estado | Diagnóstico dos módulos e do enlace Serial. |

Setas fazem seleção, Enter alterna ou confirma e Esc/Menu volta ao menu principal. A navegação numérica tem precedência quando estiver no menu principal, reduzindo a quantidade de passos sob pressão operacional.

## 5. Protocolo Serial: DTM-1

O protocolo usa **JSON Lines UTF-8** a 115200 bps no console USB Serial/JTAG do ESP32-S3. Cada objeto ocupa uma linha e não contém dados binários. Linhas inválidas retornam um `error`; comandos são idempotentes quando possível.

### 5.1 Envelopes

| Direção | Exemplo | Finalidade |
|---|---|---|
| Dispositivo → painel | `{"type":"hello","protocol":"DTM-1","device":"Cardputer ADV"}` | Descoberta e compatibilidade. |
| Painel → dispositivo | `{"type":"command","id":"42","cmd":"snapshot"}` | Solicitação de ação. |
| Dispositivo → painel | `{"type":"ack","id":"42","ok":true}` | Confirmação de comando. |
| Dispositivo → painel | `{"type":"snapshot","modules":{},"aircraft":[]}` | Estado completo inicial ou solicitado. |
| Dispositivo → painel | `{"type":"telemetry","aircraft":{...}}` | Atualização incremental de aeronave. |
| Dispositivo → painel | `{"type":"status","modules":{},"simulation":false}` | Estado de módulos, bateria e ponte. |
| Dispositivo → painel | `{"type":"error","id":"42","code":"BAD_COMMAND"}` | Erro tratável pelo operador. |

### 5.2 Comandos obrigatórios

| Comando | Argumentos | Comportamento |
|---|---|---|
| `snapshot` | nenhum | Publica estado completo. |
| `navigate` | `screen` | Navega a mesma tela disponível no Cardputer. |
| `toggle` | `module`, `enabled` | Ajusta módulo permitido: `wifi`, `ble`, `lora`, `gps`, `alerts`, `serial`, `simulation`. |
| `simulation.start` | `scenario` opcional | Ativa cenário determinístico de três aeronaves. |
| `simulation.stop` | nenhum | Interrompe atualizações simuladas sem apagar logs. |
| `simulation.step` | `ticks` opcional | Avança a simulação de modo determinístico para testes. |
| `log.export` | nenhum | Solicita exportação via fluxo Serial quando SD estiver disponível. |
| `key` | `key` | Aciona um comando equivalente a uma tecla local, útil para integração e regressão. |

`toggle` não aceita `sdr`, `rtl`, `softap`, `webserver` ou qualquer ação de transmissão. O firmware responde com `UNSUPPORTED_COMMAND` para esses valores.

## 6. Emulador e estratégia de teste

O painel estático contém dois transportes intercambiáveis. O `WebSerialTransport` é usado no navegador compatível quando o operador seleciona uma porta. O `EmulatorTransport` implementa localmente o protocolo DTM-1 e gera os mesmos `hello`, `status`, `snapshot`, `telemetry`, `ack` e `error` do firmware.

O cenário padrão possui três aeronaves com trajetórias orbitais determinísticas e fontes Wi‑Fi, BLE e LoRa. A repetibilidade permite testar a UI, filtros, alertas e comandos mesmo sem hardware. A suíte host do firmware cobre serialização, parse, rejeição de comandos proibidos e transições de simulação.

## 7. Exclusões deliberadas

O anterior `RTL-SDR`, USB Host, Spectrum Analyzer de 24–1766 MHz, NRF24 e servidor SoftAP não fazem parte da entrega de firmware. Os arquivos podem permanecer como histórico até sua remoção completa da árvore, mas não entram no manifesto de compilação, inicialização, menus, comandos ou configuração.

## Referências

[1] [M5Stack — Cap LoRa868 para Cardputer ADV](https://docs.m5stack.com/en/cap/Cap_LoRa868)
