# Constatações iniciais

## Repositório

- Projeto: https://github.com/wsalmi/drone-tools
- Branch clonado: `main`; repositório privado, 16 commits no estado inicial da reconstrução.
- Build principal: ESP-IDF para ESP32-S3, com componentes `common`, `domain`, `hw_hal`, `services` e `ui`.
- Especificação encontrada em `.kiro/specs/drone-telemetry-monitor/`: `requirements.md`, `design.md` e `tasks.md`.
- O README e o design originais ainda descrevem RTL-SDR via USB, NRF24 e SoftAP/servidor web embutido, mas o novo escopo determina a remoção do uso funcional de RTL-SDR, a preservação da USB para atualização de firmware e a substituição do SoftAP por uma interface web estática via Serial.

## Hardware oficial consultado

Fonte: https://docs.m5stack.com/en/cap/Cap_LoRa868

- O Cap LoRa868 é projetado para Cardputer-Adv/CardputerZero.
- Rádio: Stamp LoRa-1262 baseado em Semtech SX1262; SPI; faixa publicada de 868–923 MHz; modulações FSK, GFSK, MSK, GMSK, LoRa e OOK; sensibilidade LoRa publicada de até -147 dBm e potência TX de +20 dBm.
- GNSS: ATGM336H-6N baseado em AT6668; UART, padrão publicado de 115200 bps 8N1; NMEA 0183 4.1; GPS/QZSS/BD2/BD3/GAL/GLO; precisão publicada inferior a 1,5 m CEP50; taxa de atualização máxima de 10 Hz.
- Mapa de pinos documentado para Cardputer-Adv: LoRa RST=G3, IRQ=G4, BUSY=G6, SCK=G40, MOSI=G14, MISO=G39, NSS=G5; GPS RX=G13 e GPS TX=G15.
- A página informa que as conexões são fixas e que o Cardputer não expõe o EXT 14P exigido por algumas variantes; a implementação deve respeitar os pinos fixos do Cap.

## Implicações de escopo

- Caminho principal de detecção: Remote ID via Wi-Fi e BLE.
- Recurso RF adicional: SX1262 no Cap LoRa868, com monitoramento/sniff passivo e fallback operacional quando indisponível.
- RTL-SDR não deve ser inicializado, configurado ou usado para análise; a USB deve permanecer livre para DFU/flash/atualização.
- Não usar SoftAP. A integração web deve ser realizada pelo protocolo Serial USB/UART e por um site estático publicável no GitHub Pages.
- A interface do dispositivo deve privilegiar menus de campo e atalhos numéricos, sem retirar navegação por setas/escape.
