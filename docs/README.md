# Drone Telemetry Field Console — GitHub Pages

Este diretório é o pacote estático do painel operacional. Publique a pasta
`docs/` pelo GitHub Pages usando a origem **Deploy from a branch → main → /docs**.

## Conexão com o Cardputer

1. Grave o firmware no M5 Cardputer ADV e conecte-o ao computador por USB-C.
2. Abra o painel em Chrome ou Chromium por HTTPS (GitHub Pages atende esse requisito).
3. Selecione **Conectar Serial**, escolha a porta do dispositivo e autorize o acesso.
4. O painel envia e recebe NDJSON UTF-8 em `115200` baud usando o protocolo
   `drone-telemetry-serial/v1`.

O site não cria SoftAP, não carrega um servidor no dispositivo e não utiliza
rádio USB. A USB-C permanece disponível para atualização de firmware e para o
console Serial/JTAG. Sem o hardware conectado, o **Emulador de missão** fornece
tráfego determinístico com os cenários Campo, Esparso e Denso.

## Atalhos operacionais

| Tecla | Ação |
|---:|---|
| 1 | Varredura |
| 2 | Radar |
| 3 | Mapa |
| 4 | Modos |
| 5 | Configuração |
| 6 | Registros |
| 7 | Estado |

O conjunto de atalhos é espelhado pelo firmware para preservar a memória
operacional entre a tela do Cardputer e o painel conectado.
