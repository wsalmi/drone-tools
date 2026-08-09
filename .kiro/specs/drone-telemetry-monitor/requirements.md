# Requirements Document

## Introduction

Firmware para a plataforma M5 Stack Cardputer ADV (ESP32-S3) que realiza análise de telemetria e monitoramento passivo de drones em tempo real. O sistema utiliza múltiplos módulos de RF (LoRa SX1262, NRF24, RTL-SDR V3c) e GPS (ATGM 336H) para identificar aeronaves não tripuladas nas proximidades, interceptar dados de telemetria, localizar o piloto e classificar os protocolos de comunicação utilizados.

## Glossary

- **Firmware**: Software embarcado que executa no M5 Cardputer ADV
- **Sistema_Monitor**: O firmware principal de monitoramento de telemetria de drones
- **Módulo_LoRa**: Módulo Cap LoRa baseado no chip SX1262 para recepção de sinais LoRa
- **Módulo_NRF24**: Módulo NRF24L01+ para monitoramento de protocolos em 2.4 GHz (substitui o Módulo_LoRa quando conectado)
- **Módulo_GPS**: Módulo GPS ATGM 336H para geolocalização do dispositivo monitor
- **Módulo_SDR**: RTL-SDR V3c para recepção de banda larga de RF
- **Aeronave_Detectada**: Um drone ou VANT identificado pelo sistema
- **Telemetria**: Dados transmitidos por uma aeronave contendo informações de voo (posição, altitude, velocidade, bateria, etc.)
- **Piloto_Remoto**: Operador da aeronave detectada, cuja localização pode ser inferida a partir de dados de telemetria
- **Protocolo_Comunicação**: Tipo de enlace de comunicação utilizado entre drone e piloto (ELRS, DJI, WiFi, MAVLink, etc.)
- **RemoteID**: Protocolo regulatório de identificação remota transmitido por drones via WiFi ou Bluetooth
- **ELRS**: ExpressLRS — protocolo de controle de rádio de código aberto em 900 MHz e 2.4 GHz
- **MAVLink**: Protocolo de comunicação utilizado por controladores de voo como ArduPilot e PX4
- **Display_Interface**: Tela LCD integrada do M5 Cardputer ADV para apresentação de dados

## Requirements

### Requisito 1: Detecção de Aeronaves via RemoteID

**User Story:** Como operador do sistema, eu quero detectar drones que transmitem RemoteID via WiFi e Bluetooth, para que eu possa identificar aeronaves nas proximidades.

#### Acceptance Criteria

1. QUANDO um beacon RemoteID WiFi (NAN ou Beacon frame) é recebido, O Sistema_Monitor SHALL decodificar o pacote conforme ASTM F3411 e registrar o identificador da Aeronave_Detectada. IF a aeronave já estiver registrada (mesmo UAS ID), THEN O Sistema_Monitor SHALL atualizar os dados existentes em vez de criar registro duplicado.
2. QUANDO um beacon RemoteID Bluetooth (BLE 4/5 Legacy Advertisement) é recebido, O Sistema_Monitor SHALL decodificar o pacote conforme ASTM F3411 e registrar o identificador da Aeronave_Detectada. IF a aeronave já estiver registrada (mesmo UAS ID), THEN O Sistema_Monitor SHALL atualizar os dados existentes em vez de criar registro duplicado.
3. QUANDO uma Aeronave_Detectada é registrada ou atualizada, O Sistema_Monitor SHALL exibir na Display_Interface o identificador, posição e altitude da aeronave em no máximo 500 ms. IF posição ou altitude não estiverem presentes no pacote RemoteID, THEN O Sistema_Monitor SHALL exibir "N/D" nos campos ausentes.
4. IF um pacote RemoteID recebido falhar na validação de integridade (CRC inválido, campos obrigatórios ausentes conforme ASTM F3411, ou tamanho de pacote fora da especificação), THEN O Sistema_Monitor SHALL descartar o pacote e incrementar o contador de erros visível na Display_Interface.
5. WHILE o modo de detecção RemoteID está ativo, O Sistema_Monitor SHALL alternar a escuta entre WiFi e Bluetooth de forma contínua, completando um ciclo completo de varredura em no máximo 3 segundos.

### Requisito 2: Monitoramento de Protocolos 2.4 GHz via NRF24

**User Story:** Como operador do sistema, eu quero monitorar o espectro de 2.4 GHz com o módulo NRF24, para que eu possa detectar comunicações ELRS e outros protocolos nessa frequência.

#### Acceptance Criteria

1. WHEN o Módulo_NRF24 é conectado, THE Sistema_Monitor SHALL detectar o módulo via SPI e desativar o Módulo_LoRa em no máximo 2 segundos
2. WHILE o Módulo_NRF24 está ativo, THE Sistema_Monitor SHALL realizar varredura nos 126 canais da faixa 2.4 GHz (2400 a 2525 MHz, espaçamento de 1 MHz) em intervalos de no máximo 100 ms por canal
3. WHEN um pacote ELRS é identificado no espectro 2.4 GHz, THE Sistema_Monitor SHALL decodificar o cabeçalho e registrar a frequência (em MHz), potência de sinal (RSSI em dBm) e identificador do enlace
4. WHEN um pacote de protocolo DJI (OcuSync/O3) é detectado na faixa 2.4 GHz, THE Sistema_Monitor SHALL registrar a frequência (em MHz), RSSI (em dBm) e classificar como protocolo DJI
5. IF o Módulo_NRF24 não responder dentro de 3 segundos durante a inicialização, THEN O Sistema_Monitor SHALL exibir uma mensagem de erro na Display_Interface e operar sem monitoramento 2.4 GHz
6. IF o Módulo_NRF24 parar de responder durante operação normal (timeout de 1 segundo sem resposta SPI), THEN O Sistema_Monitor SHALL exibir alerta de falha na Display_Interface, reativar o Módulo_LoRa e registrar o evento no log

### Requisito 3: Monitoramento LoRa via SX1262

**User Story:** Como operador do sistema, eu quero monitorar comunicações LoRa em 900 MHz, para que eu possa detectar drones que utilizam ELRS 900 MHz ou outros protocolos LoRa.

#### Acceptance Criteria

1. WHILE o Módulo_LoRa está ativo e o Módulo_NRF24 não está conectado, THE Sistema_Monitor SHALL realizar varredura cíclica nas frequências do plano de frequências ELRS 900 MHz (862–928 MHz), permanecendo em cada frequência por no máximo 50 ms antes de avançar para a próxima
2. WHEN um pacote ELRS 900 MHz é recebido pelo Módulo_LoRa, THE Sistema_Monitor SHALL decodificar o cabeçalho e registrar no log do cartão SD e exibir na Display_Interface: frequência em MHz, RSSI em dBm, SNR em dB e identificador do enlace
3. WHEN um pacote LoRa não identificado como ELRS é recebido, THE Sistema_Monitor SHALL registrar no log do cartão SD os parâmetros de modulação (SF, BW, CR) e o payload em formato hexadecimal limitado aos primeiros 256 bytes
4. IF o Módulo_LoRa não responder dentro de 3 segundos durante a inicialização, THEN O Sistema_Monitor SHALL exibir uma mensagem de erro na Display_Interface e operar sem monitoramento LoRa
5. IF o Módulo_LoRa parar de responder durante a operação, THEN O Sistema_Monitor SHALL tentar reinicializar o módulo até 3 vezes com intervalo de 2 segundos e, em caso de falha persistente, exibir erro na Display_Interface e desativar o monitoramento LoRa

### Requisito 4: Recepção de Banda Larga via RTL-SDR

**User Story:** Como operador do sistema, eu quero utilizar o RTL-SDR V3c para recepção de banda larga, para que eu possa identificar sinais de telemetria em frequências diversas.

#### Acceptance Criteria

1. WHEN o Módulo_SDR é inicializado, THE Sistema_Monitor SHALL configurar a frequência central e largura de banda de amostragem (máximo 2.4 MHz) conforme o modo de operação selecionado pelo operador no menu de Configurações
2. WHILE o Módulo_SDR está ativo, THE Sistema_Monitor SHALL realizar análise espectral e apresentar um gráfico de waterfall na Display_Interface atualizado a cada 500 ms
3. WHEN um sinal com potência acima de 10 dB do piso de ruído é detectado pelo Módulo_SDR em frequência correspondente a um Protocolo_Comunicação conhecido (ELRS 900 MHz, DJI, MAVLink), THE Sistema_Monitor SHALL registrar a frequência central, largura de banda e potência do sinal
4. THE Sistema_Monitor SHALL suportar varredura nas faixas de 24 MHz a 1766 MHz através do Módulo_SDR
5. IF o Módulo_SDR não for detectado na porta USB em até 5 segundos após a inicialização, THEN O Sistema_Monitor SHALL exibir aviso de ausência do módulo SDR na Display_Interface e operar sem funcionalidade SDR
6. WHEN um sinal é detectado pelo Módulo_SDR em frequência que não corresponde a nenhum Protocolo_Comunicação conhecido, THE Sistema_Monitor SHALL registrar a frequência, potência e largura de banda do sinal e classificá-lo como "Não Classificado"

### Requisito 5: Geolocalização do Dispositivo Monitor

**User Story:** Como operador do sistema, eu quero que o sistema registre minha própria posição GPS, para que eu possa calcular distâncias e direções relativas às aeronaves detectadas.

#### Acceptance Criteria

1. WHEN o Módulo_GPS obtém fix de satélites com no mínimo 4 satélites e HDOP inferior a 5.0, THE Sistema_Monitor SHALL registrar latitude, longitude e altitude do dispositivo com precisão de até 2.5 metros CEP
2. WHILE o Módulo_GPS possui fix válido (mínimo 4 satélites e HDOP inferior a 5.0), THE Sistema_Monitor SHALL atualizar a posição do dispositivo a cada 1 segundo
3. WHEN uma Aeronave_Detectada possui coordenadas com idade inferior a 10 segundos, THE Sistema_Monitor SHALL calcular e exibir a distância em metros (para distâncias até 1000 m) ou quilômetros (acima de 1000 m) e azimute em graus (0°–359°) relativos à posição do dispositivo
4. IF o Módulo_GPS não obtiver fix em 60 segundos após inicialização, THEN O Sistema_Monitor SHALL exibir aviso de "GPS sem fix" na Display_Interface e continuar operação sem dados de posição própria
5. IF o Módulo_GPS perder o fix válido durante operação (satélites abaixo de 4 ou HDOP acima de 5.0 por mais de 5 segundos), THEN O Sistema_Monitor SHALL exibir indicador de "GPS degradado" na Display_Interface e manter a última posição válida com indicação de idade dos dados

### Requisito 6: Identificação da Localização do Piloto

**User Story:** Como operador do sistema, eu quero identificar a localização provável do piloto remoto, para que eu possa determinar de onde a aeronave está sendo controlada.

#### Acceptance Criteria

1. WHEN dados de RemoteID contêm a posição do operador (Operator Location), THE Sistema_Monitor SHALL exibir a posição do Piloto_Remoto na Display_Interface com indicação de distância em metros e direção em graus de azimute relativos à posição do dispositivo monitor
2. WHEN dados de telemetria MAVLink contêm coordenadas de Home Point, THE Sistema_Monitor SHALL inferir a posição do Piloto_Remoto como a posição de Home Point e exibir na Display_Interface com indicação de distância em metros e direção em graus de azimute relativos à posição do dispositivo monitor
3. WHEN no mínimo 3 leituras de RSSI do enlace de controle são coletadas em posições GPS com separação mínima de 10 metros entre si, THE Sistema_Monitor SHALL estimar a direção do Piloto_Remoto por triangulação de sinal e exibir na Display_Interface
4. WHEN a posição do Piloto_Remoto é determinada, THE Sistema_Monitor SHALL indicar o nível de confiança como "confirmado" se a fonte é Operator Location de RemoteID, "estimado" se a fonte é Home Point MAVLink ou triangulação RSSI, ou "desconhecido" se nenhuma fonte de posição está disponível
5. IF nenhuma fonte de dados de posição do Piloto_Remoto estiver disponível para uma Aeronave_Detectada (sem Operator Location, sem Home Point, e menos de 3 leituras RSSI), THEN THE Sistema_Monitor SHALL exibir indicação de "Posição do piloto desconhecida" na Display_Interface para a aeronave correspondente

### Requisito 7: Classificação de Protocolo de Comunicação

**User Story:** Como operador do sistema, eu quero identificar o tipo de protocolo de comunicação utilizado por cada drone detectado, para que eu possa entender a tecnologia empregada.

#### Acceptance Criteria

1. WHEN uma Aeronave_Detectada é registrada, THE Sistema_Monitor SHALL classificar o Protocolo_Comunicação como um dos tipos: ELRS, DJI, WiFi, MAVLink, Crossfire, FrSky, ou Desconhecido em no máximo 3 segundos após o registro
2. THE Sistema_Monitor SHALL determinar o Protocolo_Comunicação com base em assinaturas de cabeçalho de pacote, frequência de operação e padrão de modulação, utilizando correspondência contra a tabela de assinaturas carregada
3. WHEN o Protocolo_Comunicação é classificado, THE Sistema_Monitor SHALL exibir o tipo e o nível de confiança da classificação (alto, baixo) na Display_Interface junto com a Aeronave_Detectada correspondente
4. THE Sistema_Monitor SHALL manter uma tabela de assinaturas de protocolos atualizável via arquivo de configuração no cartão SD, carregada durante a inicialização do sistema
5. IF o arquivo de configuração de assinaturas estiver ausente ou malformado no cartão SD, THEN O Sistema_Monitor SHALL utilizar a tabela de assinaturas padrão embarcada no firmware e exibir aviso na Display_Interface
6. IF nenhuma assinatura da tabela corresponder aos dados recebidos de uma Aeronave_Detectada, THEN O Sistema_Monitor SHALL classificar o Protocolo_Comunicação como "Desconhecido" e registrar os parâmetros observados (frequência, modulação, cabeçalho) no log

### Requisito 8: Interceptação e Decodificação de Telemetria

**User Story:** Como operador do sistema, eu quero decodificar dados de telemetria interceptados, para que eu possa visualizar informações de voo das aeronaves detectadas.

#### Acceptance Criteria

1. WHEN pacotes de telemetria MAVLink (v1 ou v2) são interceptados, THE Sistema_Monitor SHALL decodificar e exibir na Display_Interface: posição GPS (lat/lon em graus decimais), altitude (metros acima do nível do mar), velocidade (m/s), nível de bateria (percentual) e modo de voo, associando os dados à Aeronave_Detectada correspondente
2. WHEN pacotes de telemetria ELRS são interceptados, THE Sistema_Monitor SHALL decodificar e exibir na Display_Interface os campos de telemetria presentes no payload: RSSI (dBm), LQ (percentual), bateria (tensão em V) e posição GPS (lat/lon em graus decimais, quando o campo GPS está presente no pacote), associando os dados à Aeronave_Detectada correspondente
3. WHEN pacotes de telemetria DJI são interceptados, THE Sistema_Monitor SHALL extrair e exibir na Display_Interface as informações decodificáveis do formato proprietário: posição (lat/lon em graus decimais), altitude (metros) e velocidade (m/s), associando os dados à Aeronave_Detectada correspondente
4. THE Sistema_Monitor SHALL armazenar todos os dados de telemetria decodificados em log no cartão SD com timestamp UTC (resolução de milissegundos), identificador da Aeronave_Detectada e posição GPS do monitor
5. IF um pacote de telemetria não puder ser decodificado, THEN O Sistema_Monitor SHALL registrar no log o payload bruto em hexadecimal (limitado aos primeiros 256 bytes), o protocolo de origem identificado e o timestamp UTC
6. IF um pacote de telemetria é recebido mas não pode ser associado a nenhuma Aeronave_Detectada existente, THEN O Sistema_Monitor SHALL criar um novo registro de Aeronave_Detectada com protocolo identificado e associar a telemetria ao novo registro

### Requisito 9: Interface de Usuário e Navegação

**User Story:** Como operador do sistema, eu quero uma interface clara e navegável no display do Cardputer, para que eu possa operar o sistema em campo de forma eficiente.

#### Acceptance Criteria

1. THE Sistema_Monitor SHALL apresentar um menu principal com opções: Scanner, Mapa, Lista de Aeronaves, Spectrum Analyzer, Configurações e Log
2. WHEN o operador pressiona uma tecla de navegação, THE Sistema_Monitor SHALL responder em no máximo 200 ms com atualização da Display_Interface
3. WHILE o modo Scanner está ativo, THE Display_Interface SHALL apresentar uma lista paginada de aeronaves detectadas (máximo 5 itens por página) exibindo para cada entrada: RSSI em dBm, protocolo, distância em metros e direção em graus
4. WHILE o modo Mapa está ativo, THE Display_Interface SHALL apresentar uma vista 2D com ícone do operador centralizado e ícones posicionais de cada Aeronave_Detectada e Piloto_Remoto, com escala ajustável e indicação de distância em metros
5. WHILE dados de telemetria estão sendo recebidos, THE Display_Interface SHALL atualizar as informações da tela ativa a cada 500 ms
6. IF nenhuma Aeronave_Detectada está registrada, THEN THE Display_Interface SHALL exibir mensagem indicando ausência de aeronaves detectadas nos modos Scanner, Mapa e Lista de Aeronaves
7. WHEN o número de aeronaves detectadas excede 5 no modo Scanner, THE Sistema_Monitor SHALL permitir navegação entre páginas da lista via teclas de seta do teclado

### Requisito 10: Gerenciamento de Módulos de Hardware

**User Story:** Como operador do sistema, eu quero que o firmware gerencie automaticamente os módulos de hardware disponíveis, para que eu possa conectar e desconectar módulos sem reiniciar o sistema.

#### Acceptance Criteria

1. WHEN o Sistema_Monitor é inicializado, THE Sistema_Monitor SHALL verificar cada módulo de hardware (Módulo_LoRa, Módulo_NRF24, Módulo_GPS, Módulo_SDR) com timeout de 500 ms por módulo e reportar o status (ativo, inativo, erro) na Display_Interface
2. WHEN o Módulo_NRF24 é conectado durante operação, THE Sistema_Monitor SHALL desativar o Módulo_LoRa e ativar o Módulo_NRF24 em no máximo 2 segundos
3. IF a ativação do Módulo_NRF24 falhar após o Módulo_LoRa ter sido desativado, THEN THE Sistema_Monitor SHALL reativar o Módulo_LoRa em no máximo 2 segundos e exibir indicador de erro para o Módulo_NRF24 na Display_Interface
4. WHEN o Módulo_NRF24 é desconectado durante operação, THE Sistema_Monitor SHALL reativar o Módulo_LoRa automaticamente em no máximo 2 segundos
5. THE Sistema_Monitor SHALL verificar a presença de módulos conectados ao barramento SPI a cada 500 ms para detectar eventos de conexão e desconexão em tempo de execução
6. THE Sistema_Monitor SHALL apresentar um indicador de status de cada módulo (ativo, inativo, erro) na barra superior da Display_Interface, atualizado em no máximo 1 segundo após mudança de estado

### Requisito 11: Registro e Exportação de Dados

**User Story:** Como operador do sistema, eu quero que todos os dados coletados sejam registrados e exportáveis, para que eu possa analisar as informações posteriormente.

#### Acceptance Criteria

1. THE Sistema_Monitor SHALL registrar todas as aeronaves detectadas, telemetria e eventos em arquivos de log no cartão SD em formato CSV, criando um novo arquivo por sessão de operação e rotacionando para um novo arquivo ao atingir 10 MB por arquivo
2. WHEN o operador seleciona "Exportar" no menu, THE Sistema_Monitor SHALL gerar um arquivo KML contendo todas as posições de aeronaves e pilotos registradas na sessão atual, exibir indicador de progresso na Display_Interface, e confirmar a conclusão em no máximo 30 segundos
3. THE Sistema_Monitor SHALL incluir em cada registro de log: timestamp UTC, posição GPS do monitor (latitude, longitude, altitude), identificador da aeronave, protocolo classificado, RSSI em dBm, e campos de telemetria decodificados conforme o protocolo (posição, altitude, velocidade, bateria quando disponíveis no pacote)
4. IF o cartão SD estiver cheio ou ausente, THEN O Sistema_Monitor SHALL exibir aviso na Display_Interface e manter os dados mais recentes em buffer na memória RAM (últimos 100 registros)
5. WHEN o cartão SD é reinserido ou espaço torna-se disponível após condição de SD cheio ou ausente, THE Sistema_Monitor SHALL gravar os registros do buffer no cartão SD e retomar o registro normal

### Requisito 12: Análise Espectral e Detecção de Interferência

**User Story:** Como operador do sistema, eu quero visualizar o espectro de RF nas faixas relevantes, para que eu possa identificar fontes de interferência e sinais não classificados.

#### Acceptance Criteria

1. WHILE o modo Spectrum Analyzer está ativo, THE Sistema_Monitor SHALL apresentar um gráfico de potência por frequência na Display_Interface atualizado a cada 200 ms
2. THE Sistema_Monitor SHALL permitir configuração da faixa de frequência (24–1766 MHz), largura de banda de resolução (10 kHz–1 MHz) e ganho (0–49.6 dB) através do menu de Configurações
3. WHEN um pico de sinal acima do limiar configurável (padrão: -60 dBm) é detectado fora das frequências conhecidas de drone, THE Sistema_Monitor SHALL marcar o sinal como "Não Classificado" e registrar no log a frequência, potência, largura de banda e timestamp
4. WHILE o modo Spectrum Analyzer está ativo, THE Sistema_Monitor SHALL sobrepor marcadores de frequência para protocolos conhecidos (ELRS 900, ELRS 2.4, DJI, WiFi) no gráfico espectral
5. IF o Módulo_SDR não estiver disponível quando o modo Spectrum Analyzer é selecionado, THEN THE Sistema_Monitor SHALL exibir mensagem de erro na Display_Interface indicando que o módulo SDR é necessário para esta funcionalidade

### Requisito 13: Alertas e Notificações

**User Story:** Como operador do sistema, eu quero receber alertas quando novas aeronaves são detectadas ou quando eventos relevantes ocorrem, para que eu possa reagir rapidamente.

#### Acceptance Criteria

1. WHEN uma nova Aeronave_Detectada é registrada pela primeira vez, THE Sistema_Monitor SHALL emitir alerta sonoro (buzzer) por 1 segundo e exibir notificação na Display_Interface por 3 segundos contendo o identificador da aeronave e o protocolo detectado
2. WHEN uma Aeronave_Detectada se aproxima a menos de 500 metros do operador, THE Sistema_Monitor SHALL emitir alerta sonoro (buzzer) com padrão distinto do alerta de nova detecção e exibir notificação de proximidade na Display_Interface contendo o identificador da aeronave e a distância atual, repetindo o alerta a cada 10 segundos enquanto a aeronave permanecer abaixo de 500 metros
3. WHEN uma Aeronave_Detectada deixa de transmitir por mais de 30 segundos, THE Sistema_Monitor SHALL marcar a aeronave como "Fora de Alcance" na lista e registrar o evento no log
4. WHERE o operador configurou alertas silenciosos, THE Sistema_Monitor SHALL suprimir todos os alertas sonoros (buzzer) de detecção e proximidade e utilizar apenas notificação visual na Display_Interface
5. IF o Módulo_GPS não possuir fix válido quando uma Aeronave_Detectada reporta posição, THEN THE Sistema_Monitor SHALL suprimir o alerta de proximidade e exibir indicação de "Distância indisponível" junto à aeronave na Display_Interface
6. WHEN uma Aeronave_Detectada marcada como "Fora de Alcance" volta a transmitir, THE Sistema_Monitor SHALL remover a marcação "Fora de Alcance", atualizar o status para ativo na lista e emitir alerta de nova detecção conforme critério 1
