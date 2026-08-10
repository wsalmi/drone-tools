# Implementation Plan: Drone Telemetry Monitor

## Overview

Plano de implementação do firmware drone-telemetry-monitor para M5 Stack Cardputer ADV (ESP32-S3) usando ESP-IDF v5.x em C/C++. A implementação segue uma abordagem bottom-up: primeiro a camada HAL (drivers de hardware), depois a camada de domínio (modelos de dados), seguida pelos serviços de aplicação (decodificação, classificação, geolocalização) e finalmente a camada de UI e integração. Testes de propriedade executam em host (x86) com mocks da HAL.

## Tasks

- [x] 1. Configuração do projeto e infraestrutura base
  - [x] 1.1 Criar estrutura do projeto ESP-IDF e CMakeLists
    - Criar diretório do projeto com estrutura ESP-IDF padrão (main/, components/, test/)
    - Configurar CMakeLists.txt principal e sdkconfig.defaults para ESP32-S3
    - Configurar componentes: hal/, services/, domain/, ui/, common/
    - Criar common/error_codes.h com todos os códigos de erro customizados
    - Criar common/hal_common.h com tipos base (hal_status_t, hal_module_state_t, hal_rf_interface_t)
    - _Requirements: 10.1_

  - [x] 1.2 Configurar framework de testes host (Unity + Theft PBT)
    - Configurar CMakeLists para compilação x86 dos testes em test/host/
    - Integrar Unity (framework de testes unitários para C)
    - Integrar Theft (biblioteca PBT para C) com configuração de 100 iterações mínimas
    - Criar diretório test/mocks/ com stubs iniciais da HAL
    - Criar diretório test/host/generators/ para geradores PBT
    - _Requirements: N/A (infraestrutura de testes)_

  - [x] 1.3 Implementar módulo de configuração (config parser)
    - Criar domain/config_store.h e domain/config_store.c
    - Implementar parser JSON para config.json do SD Card (usando cJSON da ESP-IDF)
    - Definir struct de configuração global com valores padrão embarcados
    - Implementar validação de ranges para campos numéricos (frequências, intervalos, limiares)
    - Retornar configuração padrão se arquivo ausente ou malformado
    - _Requirements: 7.4, 7.5, 12.2_

  - [ ]* 1.4 Escrever teste de propriedade para validação de configuração de espectro
    - **Property 19: Validação de configuração de espectro**
    - Gerar configurações com frequência central, bandwidth e ganho em ranges arbitrários
    - Verificar que valores dentro de [24 MHz, 1766 MHz], [10 kHz, 1 MHz], [0 dB, 49.6 dB] são aceitos
    - Verificar que valores fora dos ranges são rejeitados com erro
    - **Validates: Requirements 12.2, 4.4**

  - [ ]* 1.5 Escrever teste de propriedade para resiliência a tabela de assinaturas malformada
    - **Property 9: Resiliência a tabela de assinaturas malformada**
    - Gerar strings arbitrárias que não são CSV válido (campos ausentes, tipos errados, vazio, encoding corrompido)
    - Verificar que o parse retorna erro sem crash
    - Verificar que o sistema utiliza a tabela padrão embarcada como fallback
    - **Validates: Requirements 7.5**

- [x] 2. Camada HAL — Drivers de Hardware
  - [x] 2.1 Implementar HAL LoRa SX1262
    - Criar hal/hal_lora.h com interface conforme design (lora_config_t, lora_packet_t)
    - Criar hal/hal_lora.c com driver SPI para SX1262
    - Implementar init, set_frequency, start_rx, get_packet, get_status, deinit
    - Implementar lógica de retry (3 tentativas, 2s intervalo) na inicialização
    - _Requirements: 3.1, 3.2, 3.4, 3.5_

  - [x] 2.2 Implementar HAL NRF24
    - Criar hal/hal_nrf24.h com interface conforme design (nrf24_config_t, nrf24_packet_t, nrf24_spectrum_t)
    - Criar hal/hal_nrf24.c com driver SPI para NRF24L01+
    - Implementar init, scan_spectrum (126 canais), listen_channel, is_present, get_status, deinit
    - Implementar detecção de presença via poll SPI
    - _Requirements: 2.1, 2.2, 2.5, 10.5_

  - [x] 2.3 Implementar HAL RTL-SDR
    - Criar hal/hal_sdr.h com interface conforme design (sdr_config_t, sdr_iq_buffer_t, sdr_spectrum_t)
    - Criar hal/hal_sdr.c com driver USB Host OTG para RTL-SDR V3c
    - Implementar init, set_frequency, set_sample_rate, read_iq, compute_spectrum, get_status, deinit
    - Implementar FFT para cálculo de espectro a partir de amostras I/Q
    - _Requirements: 4.1, 4.2, 4.4, 4.5_

  - [x] 2.4 Implementar HAL GPS ATGM336H
    - Criar hal/hal_gps.h com interface conforme design (gps_position_t)
    - Criar hal/hal_gps.c com driver UART para ATGM 336H
    - Implementar parser NMEA (GGA, RMC) para extração de posição
    - Implementar lógica de fix_valid (satélites ≥ 4 AND hdop < 5.0)
    - Preservar última posição válida quando fix é perdido
    - _Requirements: 5.1, 5.2, 5.4, 5.5_

  - [x] 2.5 Implementar HAL WiFi Scanner e BLE Scanner
    - Criar hal/hal_wifi_scanner.h e hal/hal_wifi_scanner.c
    - Implementar escuta de WiFi NAN e Beacon frames para RemoteID
    - Criar hal/hal_ble_scanner.h e hal/hal_ble_scanner.c
    - Implementar escuta de BLE Legacy Advertisement para RemoteID
    - Implementar ciclo de alternância WiFi/BLE conforme temporização configurável
    - _Requirements: 1.1, 1.2, 1.5_

  - [x] 2.6 Implementar HAL Display, SD Card e Buzzer
    - Criar hal/hal_display.h e hal/hal_display.c (ST7789V2 via SPI, 240×135 px)
    - Criar hal/hal_sd.h e hal/hal_sd.c (microSD via SPI/SDMMC)
    - Criar hal/hal_buzzer.h e hal/hal_buzzer.c (NS4150B amplifier)
    - Implementar operações básicas: clear, draw_text, draw_rect, draw_pixel para display
    - Implementar operações de arquivo: open, write, read, close, get_free_space para SD
    - Implementar play_tone com duração e frequência configuráveis para buzzer
    - _Requirements: 9.2, 11.1, 13.1_

  - [x] 2.7 Implementar Hardware Manager (máquina de estados de hot-swap)
    - Criar hal/hw_manager.h e hal/hw_manager.c
    - Implementar máquina de estados: Initializing → LoRa_Active / NRF24_Active / Error
    - Implementar transições de hot-swap (LoRa↔NRF24) com rollback em caso de falha
    - Implementar poll periódico (500ms) para detecção de conexão/desconexão de módulos
    - Implementar retry com backoff para módulos em erro (3 tentativas, 2s intervalo)
    - _Requirements: 10.1, 10.2, 10.3, 10.4, 10.5_

  - [ ]* 2.8 Escrever teste de propriedade para validação de fix GPS
    - **Property 10: Validação de fix GPS**
    - Gerar dados GPS com satélites e HDOP arbitrários
    - Verificar que fix_valid == true ⟺ (satellites_used ≥ 4 AND hdop < 5.0)
    - Verificar que quando fix transiciona true→false, última posição válida é preservada
    - **Validates: Requirements 5.1, 5.5**

  - [ ]* 2.9 Escrever teste de propriedade para retry LoRa e rollback hot-swap
    - **Property 18: Mecanismo de retry do LoRa com política definida**
    - Gerar sequências de falha/sucesso no módulo LoRa
    - Verificar que no máximo 3 tentativas são feitas com intervalo de 2s
    - Verificar que após 3ª falha, monitoramento LoRa é desativado
    - **Validates: Requirements 3.5**

  - [ ]* 2.10 Escrever teste de propriedade para rollback de hot-swap
    - **Property 22: Rollback de máquina de estados no hot-swap**
    - Gerar cenários onde NRF24 falha ao ativar após LoRa ser desativado
    - Verificar que o sistema retorna ao estado LoRa_Active com LoRa re-inicializado
    - **Validates: Requirements 10.3**

- [x] 3. Checkpoint — Verificar compilação e testes base
  - Ensure all tests pass, ask the user if questions arise.

- [x] 4. Camada de Domínio — Modelos de Dados
  - [x] 4.1 Implementar Aircraft Registry
    - Criar domain/aircraft_registry.h e domain/aircraft_registry.c conforme design
    - Implementar struct aircraft_entry_t e aircraft_registry_t com mutex
    - Implementar registry_init, registry_find_or_create, registry_find
    - Implementar registry_update_status (timeout 30s → OUT_OF_RANGE)
    - Implementar registry_get_active_count
    - Implementar política de substituição de slots (mais antigo OUT_OF_RANGE) quando MAX_AIRCRAFT (32) atingido
    - _Requirements: 1.1, 1.2, 8.6, 13.3, 13.6_

  - [ ]* 4.2 Escrever teste de propriedade para unicidade no Aircraft Registry
    - **Property 21: Unicidade no Aircraft Registry**
    - Gerar sequências de detecções com IDs repetidos e únicos
    - Verificar que o registry contém exatamente uma entrada por ID único
    - Verificar que dados correspondem à detecção mais recente para cada ID
    - **Validates: Requirements 1.1, 1.2, 8.6**

  - [ ]* 4.3 Escrever teste de propriedade para transição de status por timeout
    - **Property 17: Transição de status de aeronave por timeout**
    - Gerar sequências temporais com timestamps variados
    - Verificar que current_time - last_seen > 30000ms → OUT_OF_RANGE
    - Verificar que novo pacote recebido → transição para ACTIVE
    - **Validates: Requirements 13.3, 13.6**

  - [x] 4.4 Implementar Protocol Signatures DB
    - Criar domain/protocol_signatures.h e domain/protocol_signatures.c
    - Definir struct protocol_signature_t conforme design
    - Implementar parser CSV para signatures.csv do SD Card
    - Implementar tabela padrão embarcada (ELRS, MAVLink, DJI, Crossfire, FrSky)
    - Implementar função de busca por correspondência header + frequência
    - _Requirements: 7.1, 7.2, 7.4, 7.5_

  - [ ]* 4.5 Escrever teste de propriedade para round-trip da tabela de assinaturas
    - **Property 8: Round-trip da tabela de assinaturas de protocolo**
    - Gerar tabelas com 1–100 entradas válidas (header_pattern, mask, frequências, modulação)
    - Serializar para CSV e fazer parse de volta
    - Verificar que tabela resultante é equivalente à original
    - **Validates: Requirements 7.4**

- [x] 5. Serviços de Decodificação — RemoteID, MAVLink, ELRS
  - [x] 5.1 Implementar decodificador RemoteID (WiFi NAN/Beacon + BLE)
    - Criar services/remoteid_decoder.h e services/remoteid_decoder.c
    - Implementar decodificação conforme ASTM F3411 para WiFi NAN e Beacon frames
    - Implementar decodificação conforme ASTM F3411 para BLE Legacy Advertisement
    - Extrair: UAS ID, posição (lat/lon), altitude, Operator Location (quando presente)
    - Implementar validação: CRC, campos obrigatórios, tamanho de pacote
    - Rejeitar pacotes inválidos com retorno de erro sem alterar o registry
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 6.1_

  - [ ]* 5.2 Escrever teste de propriedade para decodificação RemoteID round-trip
    - **Property 1: Decodificação RemoteID round-trip**
    - Implementar gerador gen_remoteid.c para pacotes RemoteID válidos conforme ASTM F3411
    - Verificar que decodificação extrai corretamente UAS ID, posição, altitude, Operator Location
    - Verificar que re-codificação produz pacote equivalente ao original nos campos decodificados
    - **Validates: Requirements 1.1, 1.2, 6.1**

  - [ ]* 5.3 Escrever teste de propriedade para rejeição de pacotes RemoteID inválidos
    - **Property 2: Rejeição de pacotes RemoteID inválidos**
    - Gerar pacotes com CRC inválido, campos obrigatórios ausentes, tamanho fora da especificação
    - Verificar que decodificação retorna erro
    - Verificar que registro permanece inalterado (contagem de aeronaves não muda)
    - **Validates: Requirements 1.4**

  - [x] 5.4 Implementar decodificador MAVLink v1/v2
    - Criar services/mavlink_decoder.h e services/mavlink_decoder.c
    - Implementar decodificação MAVLink v1 (STX=0xFE) e v2 (STX=0xFD)
    - Decodificar mensagens: GLOBAL_POSITION_INT, BATTERY_STATUS, HEARTBEAT, HOME_POSITION
    - Aplicar conversões de unidade: lat/lon int32 → graus decimais (/1e7), altitude mm → metros
    - Preencher decoded_telemetry_t com campos presentes e flags has_*
    - _Requirements: 8.1, 6.2_

  - [ ]* 5.5 Escrever teste de propriedade para decodificação MAVLink
    - **Property 4: Decodificação de telemetria MAVLink**
    - Implementar gerador gen_mavlink.c para pacotes MAVLink v1/v2 válidos
    - Gerar pacotes com GLOBAL_POSITION_INT, BATTERY_STATUS, HEARTBEAT, HOME_POSITION
    - Verificar que campos numéricos são extraídos com valores iguais aos codificados
    - Verificar conversões de unidade (lat/lon /1e7, altitude mm→m)
    - **Validates: Requirements 8.1, 6.2**

  - [x] 5.6 Implementar decodificador ELRS (telemetria CRSF)
    - Criar services/elrs_decoder.h e services/elrs_decoder.c
    - Implementar decodificação de payload CRSF (Crossfire Serial Protocol)
    - Extrair campos: RSSI, LQ, bateria (tensão), GPS (quando presente)
    - Marcar campos ausentes com has_* = false
    - Validar valores dentro das faixas válidas do protocolo
    - _Requirements: 8.2_

  - [ ]* 5.7 Escrever teste de propriedade para decodificação ELRS
    - **Property 5: Decodificação de telemetria ELRS**
    - Implementar gerador gen_elrs.c para pacotes ELRS com campos opcionais
    - Verificar que campos presentes são extraídos com valores em faixas válidas
    - Verificar que campos ausentes são marcados como indisponíveis (has_* = false)
    - **Validates: Requirements 8.2**

- [x] 6. Checkpoint — Verificar decodificadores e testes de propriedade
  - Ensure all tests pass, ask the user if questions arise.

- [x] 7. Serviços — Classificação, Geolocalização e Piloto
  - [x] 7.1 Implementar Protocol Classifier
    - Criar services/protocol_classifier.h e services/protocol_classifier.c conforme design
    - Implementar classifier_init (carregar tabela do SD ou usar embarcada)
    - Implementar classifier_classify: correspondência header_pattern com header_mask na faixa de frequência
    - Retornar protocolo e nível de confiança (HIGH se match exato em header+freq, LOW se apenas header)
    - Retornar PROTOCOL_UNKNOWN quando nenhuma assinatura corresponde
    - _Requirements: 7.1, 7.2, 7.3, 7.6_

  - [ ]* 7.2 Escrever teste de propriedade para classificação de protocolo
    - **Property 3: Classificação de protocolo contra tabela de assinaturas**
    - Gerar pacotes com cabeçalhos que correspondem a assinaturas da tabela
    - Verificar que classificador retorna protocolo correto para matches
    - Verificar que retorna PROTOCOL_UNKNOWN para cabeçalhos sem correspondência
    - **Validates: Requirements 7.1, 7.2, 7.6, 2.3, 2.4, 4.3, 4.6, 12.3**

  - [x] 7.3 Implementar Geolocation Service
    - Criar services/geolocation_service.h e services/geolocation_service.c conforme design
    - Implementar geo_service_init (integrar com HAL GPS)
    - Implementar geo_calculate_relative usando fórmula de Haversine para distância e bearing
    - Implementar geo_get_monitor_position e geo_has_valid_fix
    - Garantir simetria: distância(A,B) == distância(B,A) e distância(A,A) == 0
    - _Requirements: 5.3, 6.1, 6.2, 6.3_

  - [ ]* 7.4 Escrever teste de propriedade para cálculo de distância e azimute
    - **Property 6: Cálculo de distância e azimute geográfico**
    - Implementar gerador gen_gps.c para coordenadas GPS válidas (lat ∈ [-90,90], lon ∈ [-180,180])
    - Verificar: distância ≥ 0, azimute ∈ [0, 360)
    - Verificar simetria: distância(A,B) == distância(B,A)
    - Verificar: pontos idênticos → distância == 0
    - **Validates: Requirements 5.3, 6.1, 6.2, 6.3**

  - [x] 7.5 Implementar Pilot Locator
    - Criar services/pilot_locator.h e services/pilot_locator.c conforme design
    - Implementar pilot_locator_update: recebe dados de telemetria e atualiza posição do piloto
    - Implementar pilot_locator_get_position: retorna posição e confiança
    - Fontes de posição: Operator Location (CONFIRMED), Home Point MAVLink (ESTIMATED), triangulação RSSI (ESTIMATED)
    - Retornar UNKNOWN quando nenhuma fonte disponível
    - _Requirements: 6.1, 6.2, 6.3, 6.4, 6.5_

  - [ ]* 7.6 Escrever teste de propriedade para mapeamento de confiança do piloto
    - **Property 11: Mapeamento de confiança de posição do piloto**
    - Gerar cenários com diferentes fontes de posição (Operator Location, Home Point, RSSI, nenhuma)
    - Verificar: CONFIRMED ⟺ Operator Location de RemoteID
    - Verificar: ESTIMATED ⟺ Home Point MAVLink ou triangulação RSSI
    - Verificar: UNKNOWN ⟺ nenhuma fonte disponível
    - **Validates: Requirements 6.4, 6.5**

- [x] 8. Serviços — Detection Service e Telemetry Decoder
  - [x] 8.1 Implementar Detection Service
    - Criar services/detection_service.h e services/detection_service.c conforme design
    - Implementar detection_service_init: registra módulos disponíveis
    - Implementar detection_service_start: inicia tasks de detecção para cada módulo
    - Unificar pacotes de WiFi, BLE, LoRa, NRF24, SDR no formato raw_detection_t
    - Enfileirar raw_detection_t via FreeRTOS Queue (64 itens, drop silencioso quando cheia)
    - _Requirements: 1.1, 1.2, 1.5, 2.1, 3.1, 4.3_

  - [x] 8.2 Implementar Telemetry Decoder (orchestrador de decodificação)
    - Criar services/telemetry_decoder.h e services/telemetry_decoder.c conforme design
    - Implementar telemetry_decoder_init: registra decodificadores (RemoteID, MAVLink, ELRS)
    - Implementar telemetry_decode: dispatch para decodificador correto baseado em classificação
    - Integrar com Protocol Classifier para determinar decodificador apropriado
    - Atualizar Aircraft Registry com telemetria decodificada
    - Criar novo registro se aeronave não existe (req 8.6)
    - _Requirements: 8.1, 8.2, 8.3, 8.5, 8.6_

- [x] 9. Serviços — Data Logger, Spectrum Analyzer e Alert Engine
  - [x] 9.1 Implementar Data Logger
    - Criar services/data_logger.h e services/data_logger.c
    - Implementar formato CSV conforme design (timestamp_utc, monitor_lat/lon/alt, aircraft_id, protocol, rssi, ...)
    - Implementar buffer circular em RAM (100 registros) para quando SD indisponível
    - Implementar rotação de arquivo por tamanho (novo arquivo ao atingir 10 MB)
    - Implementar flush do buffer RAM quando SD torna-se disponível
    - Implementar geração KML com placemarks para posições de aeronaves e pilotos
    - _Requirements: 11.1, 11.2, 11.3, 11.4, 11.5_

  - [ ]* 9.2 Escrever teste de propriedade para registro CSV round-trip
    - **Property 13: Registro CSV round-trip**
    - Gerar entradas de log com timestamp, posição GPS, ID de aeronave, protocolo, RSSI e telemetria
    - Serializar para CSV e fazer parse de volta
    - Verificar que entrada resultante é equivalente à original (preservando todos os campos)
    - **Validates: Requirements 11.1, 11.3, 8.4**

  - [ ]* 9.3 Escrever teste de propriedade para buffer circular RAM
    - **Property 14: Buffer circular RAM com capacidade fixa**
    - Gerar sequências de N registros (N > 100)
    - Verificar que buffer contém exatamente 100 registros
    - Verificar que são os 100 mais recentes na ordem de inserção
    - **Validates: Requirements 11.4**

  - [ ]* 9.4 Escrever teste de propriedade para rotação de arquivo de log
    - **Property 15: Rotação de arquivo de log por tamanho**
    - Gerar sequências de escritas com tamanhos variados
    - Verificar que ao exceder 10 MB, novo arquivo é criado
    - Verificar que arquivo anterior tem tamanho ≤ 10 MB + tamanho do último registro
    - **Validates: Requirements 11.1**

  - [ ]* 9.5 Escrever teste de propriedade para formatação hexadecimal com truncamento
    - **Property 7: Formatação hexadecimal com truncamento**
    - Gerar payloads de bytes com tamanhos arbitrários
    - Verificar que string hex tem exatamente min(payload_len, 256) × 2 caracteres
    - Verificar que decodificar a string produz os primeiros min(payload_len, 256) bytes originais
    - **Validates: Requirements 3.3, 8.5**

  - [ ]* 9.6 Escrever teste de propriedade para geração KML
    - **Property 20: Geração KML preserva todas as posições**
    - Gerar sessões com N posições de aeronaves e pilotos
    - Verificar que KML contém exatamente N placemarks
    - Verificar que cada placemark contém coordenadas correspondentes ao registro original
    - **Validates: Requirements 11.2**

  - [x] 9.7 Implementar Spectrum Analyzer Service
    - Criar services/spectrum_analyzer.h e services/spectrum_analyzer.c
    - Implementar integração com HAL SDR para leitura contínua de espectro
    - Implementar detecção de picos acima de limiar configurável (padrão -60 dBm)
    - Implementar sobreposição de marcadores de frequência para protocolos conhecidos
    - Classificar sinais fora de frequências conhecidas como "Não Classificado"
    - _Requirements: 12.1, 12.2, 12.3, 12.4_

  - [x] 9.8 Implementar Alert Engine
    - Criar services/alert_engine.h e services/alert_engine.c
    - Implementar alerta de nova detecção: buzzer 1s + notificação visual 3s
    - Implementar alerta de proximidade: buzzer padrão distinto + notificação com distância (repeat 10s)
    - Implementar condição: alerta de proximidade somente se GPS fix válido
    - Implementar modo silencioso: suprime buzzer, mantém notificação visual
    - _Requirements: 13.1, 13.2, 13.4, 13.5_

  - [ ]* 9.9 Escrever teste de propriedade para alerta de proximidade condicionado a GPS
    - **Property 16: Alerta de proximidade condicionado a GPS fix**
    - Gerar cenários com aeronaves em distâncias variadas, GPS fix válido/inválido, alertas habilitados/desabilitados
    - Verificar: alerta emitido ⟺ (distância < limiar) AND (GPS fix válido) AND (alertas habilitados)
    - **Validates: Requirements 13.2, 13.4, 13.5**

- [x] 10. Checkpoint — Verificar serviços e propriedades de domínio
  - Ensure all tests pass, ask the user if questions arise.

- [x] 11. Camada de UI — Interface de Usuário
  - [x] 11.1 Implementar UI Manager e modelo de estado
    - Criar ui/ui_manager.h e ui/ui_manager.c
    - Implementar ui_state_t conforme design (current_screen, página, seleção, escala, notificação)
    - Implementar navegação entre telas via teclado (latência ≤ 200ms)
    - Implementar barra de status superior (módulos, contagem aeronaves, GPS, SD)
    - Implementar sistema de notificações com timeout (3s para notificações normais)
    - _Requirements: 9.1, 9.2, 9.5, 9.6, 10.6_

  - [x] 11.2 Implementar tela Scanner (lista paginada de aeronaves)
    - Criar ui/screen_scanner.h e ui/screen_scanner.c
    - Exibir lista paginada (5 itens/página) com: RSSI, protocolo, distância, direção
    - Implementar navegação entre páginas via teclas de seta
    - Exibir mensagem "Nenhuma aeronave detectada" quando lista vazia
    - Atualizar informações a cada 500ms
    - _Requirements: 9.3, 9.6, 9.7_

  - [ ]* 11.3 Escrever teste de propriedade para paginação da lista de aeronaves
    - **Property 12: Paginação da lista de aeronaves**
    - Gerar listas de N aeronaves (N ≥ 0) com tamanho de página 5
    - Verificar: páginas == ceil(N/5) quando N > 0, 0 quando N == 0
    - Verificar: cada página ≤ 5 itens, união de todas = N aeronaves sem duplicatas
    - **Validates: Requirements 9.3, 9.7**

  - [x] 11.4 Implementar tela Mapa (vista 2D com posições)
    - Criar ui/screen_map.h e ui/screen_map.c
    - Renderizar vista 2D no display 240×135 px com operador centralizado
    - Exibir ícones posicionais de aeronaves e pilotos detectados
    - Implementar escala ajustável (zoom in/out via teclado)
    - Exibir indicação de distância em metros
    - _Requirements: 9.4_

  - [x] 11.5 Implementar telas Spectrum, Configurações e Log
    - Criar ui/screen_spectrum.h e ui/screen_spectrum.c (waterfall display, marcadores)
    - Criar ui/screen_settings.h e ui/screen_settings.c (configuração de alertas, espectro, scan)
    - Criar ui/screen_log.h e ui/screen_log.c (exibição dos últimos registros, opção exportar KML)
    - Criar ui/screen_menu.h e ui/screen_menu.c (menu principal com opções de navegação)
    - _Requirements: 9.1, 11.2, 12.1, 12.4_

- [x] 12. Integração — Tasks FreeRTOS e Pipeline Completo
  - [x] 12.1 Criar tasks FreeRTOS e distribuir entre cores
    - Criar main/task_manager.h e main/task_manager.c
    - Core 0 (PRO_CPU): Task WiFi/BLE Scanner (prio 5), Task RF Monitor LoRa/NRF24 (prio 6), Task SDR Receiver (prio 4)
    - Core 1 (APP_CPU): Task UI Render (prio 7), Task Decoder Pipeline (prio 5), Task GPS Reader (prio 3), Task Logger (prio 2)
    - Configurar stack sizes (4096–8192 bytes por task) e watchdog
    - Implementar monitoramento de stack overflow via uxTaskGetStackHighWaterMark
    - _Requirements: 1.5, 9.2, 9.5_

  - [x] 12.2 Implementar pipeline de dados com queues inter-task
    - Criar queues FreeRTOS para comunicação RF→Decoder (64 itens)
    - Criar queue Decoder→Logger para registro assíncrono
    - Criar eventos Decoder→UI para atualização da interface
    - Implementar shared state GPS (mutex protegido) acessível pelo Decoder
    - Implementar política de drop silencioso quando queue está cheia
    - _Requirements: 1.3, 8.4, 9.5, 11.1_

  - [x] 12.3 Implementar app_main e sequência de inicialização
    - Criar main/main.c com app_main()
    - Sequência: init barramentos SPI/UART/USB → init HAL modules → init domain → init services → start tasks
    - Integrar Hardware Manager para detecção e ativação de módulos na inicialização
    - Carregar configuração do SD Card (config.json + signatures.csv)
    - Exibir status de inicialização na Display_Interface com timeout por módulo (500ms)
    - _Requirements: 10.1, 7.4, 7.5_

- [x] 13. Checkpoint Final — Compilação completa e verificação
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marcadas com `*` são opcionais e podem ser puladas para um MVP mais rápido
- Cada task referencia requirements específicos para rastreabilidade
- Checkpoints garantem validação incremental a cada etapa crítica
- Testes de propriedade (PBT) executam em host x86 usando mocks da HAL — sem dependência de hardware real
- Testes unitários complementam PBTs com exemplos concretos e edge cases de hardware
- A linguagem de implementação é C (ESP-IDF v5.x) conforme definido no documento de design
- Framework de testes: Unity para unitários, Theft para PBT
- O módulo LoRa e NRF24 são mutuamente exclusivos no barramento SPI3 (hot-swap em runtime)
- O RTL-SDR opera via USB Host OTG independentemente do barramento SPI

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1"] },
    { "id": 1, "tasks": ["1.2", "1.3"] },
    { "id": 2, "tasks": ["1.4", "1.5", "2.1", "2.2", "2.3", "2.4", "2.5", "2.6"] },
    { "id": 3, "tasks": ["2.7", "2.8", "4.1", "4.4"] },
    { "id": 4, "tasks": ["2.9", "2.10", "4.2", "4.3", "4.5"] },
    { "id": 5, "tasks": ["5.1", "5.4", "5.6", "7.1", "7.3"] },
    { "id": 6, "tasks": ["5.2", "5.3", "5.5", "5.7", "7.2", "7.4", "7.5"] },
    { "id": 7, "tasks": ["7.6", "8.1", "8.2"] },
    { "id": 8, "tasks": ["9.1", "9.7", "9.8"] },
    { "id": 9, "tasks": ["9.2", "9.3", "9.4", "9.5", "9.6", "9.9"] },
    { "id": 10, "tasks": ["11.1"] },
    { "id": 11, "tasks": ["11.2", "11.4", "11.5"] },
    { "id": 12, "tasks": ["11.3", "12.1"] },
    { "id": 13, "tasks": ["12.2"] },
    { "id": 14, "tasks": ["12.3"] }
  ]
}
```
