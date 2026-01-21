# Sistema Modbus RTU Master para ESP32-S3-RS485-CAN

Sistema completo de mestre Modbus RTU desenvolvido para o ESP32-S3-RS485-CAN da Waveshare, com interface web para configuração e sistema de cálculos customizados.

## Características

- **Modbus RTU Master**: Lê e escreve registros em múltiplos dispositivos Modbus
- **WiFi Access Point**: Modo AP para configuração sem necessidade de rede externa
- **Interface Web**: Interface HTML completa para configurar dispositivos e registros
- **Armazenamento Persistente**: Configurações salvas em EEPROM/Preferences
- **Cálculos Customizados**: Rotina executada a cada 1 segundo para processar valores lidos
- **Registros de Saída**: Sistema para escrever resultados de cálculos em dispositivos Modbus
- **Filtro de Kalman**: Suavização de valores com parâmetros configuráveis por registro
- **Scan Modbus**: Busca de dispositivos na rede RS485 via interface

## Hardware

- **Placa**: ESP32-S3-RS485-CAN (Waveshare)
- **Comunicação**: RS485 para Modbus RTU
- **Pinos RS485** (configuráveis no código):
  - TX: GPIO 17
  - RX: GPIO 18
  - DE/RE: GPIO 19

## Configuração

### Requisitos

- PlatformIO instalado
- Driver USB para ESP32-S3

### Instalação

1. Clone ou copie este projeto
2. Abra no PlatformIO
3. Compile e faça upload para a placa

### Configuração de Pinos

Se os pinos RS485 da sua placa forem diferentes, edite em `src/main.cpp`:

```cpp
#define RS485_TX_PIN 17
#define RS485_RX_PIN 18
#define RS485_DE_RE_PIN 19
```

## Uso

### Primeira Configuração

1. Após o upload, o ESP32 criará um Access Point WiFi:
   - **SSID**: `ESP32-Modbus-Config`
   - **Senha**: `12345678`

2. Conecte-se ao WiFi com seu dispositivo (celular, notebook, etc.)

3. Acesse no navegador: `http://192.168.4.1`

4. Na interface web:
   - Adicione dispositivos Modbus
   - Configure endereços Modbus (1-247)
   - Adicione registros para cada dispositivo
   - Defina quais registros são de entrada ou saída
   - Salve a configuração

### Configuração de Dispositivos

- **Endereço Modbus**: Endereço do escravo Modbus (1-247)
- **Habilitado**: Ativa/desativa leitura do dispositivo
- **Registros**:
  - **Endereço**: Endereço do registro Modbus
  - **Tipo**: 
    - Holding Register (0x03) - Leitura/escrita
    - Input Register (0x04) - Apenas leitura
  - **Saída**: Marque se este registro receberá resultados de cálculos

### Sistema de Cálculos

A função `performCalculations()` em `src/main.cpp` é executada a cada 1 segundo. Por padrão, ela:

1. Lê todos os registros configurados de todos os dispositivos
2. Realiza cálculos customizados (exemplo: soma de valores)
3. Escreve resultados nos registros marcados como saída

**Personalização**: Edite a função `performCalculations()` para implementar sua lógica específica.

## Estrutura do Código

- `src/main.cpp`: Código principal com todas as funcionalidades
- `platformio.ini`: Configuração do PlatformIO e dependências

### Principais Funções

- `setupWiFiAP()`: Configura Access Point WiFi
- `setupWebServer()`: Configura servidor web e rotas
- `setupModbus()`: Inicializa comunicação Modbus RTU
- `loadConfig()` / `saveConfig()`: Gerencia configuração persistente
- `readAllDevices()`: Lê todos os registros de todos os dispositivos
- `performCalculations()`: Executa cálculos customizados
- `writeOutputRegisters()`: Escreve resultados em registros de saída

## API REST

O servidor web expõe as seguintes rotas:

- `GET /`: Interface HTML de configuração
- `GET /api/config`: Retorna configuração atual (JSON)
- `POST /api/config`: Salva nova configuração (JSON)
- `GET /api/read`: Força leitura manual de todos os registros
- `POST /api/modbus/scan`: Busca dispositivos Modbus (endereços 1-255) com parâmetros seriais informados

## Documentação Adicional

- **[Documentação do Filtro de Kalman](DOCUMENTACAO_FILTRO_KALMAN.md)**: Guia completo sobre o filtro de Kalman, parâmetros Q e R, e como configurá-los
- **[Guia de Configuração](CONFIGURACAO.md)**: Detalhes sobre configuração de pinos e baud rate
- **[Explicação de Variáveis](EXPLICACAO_VARIAVEIS.md)**: Como usar variáveis nos cálculos

## Limites

- Máximo de dispositivos: 10
- Máximo de registros por dispositivo: 20
- Intervalo de cálculos: 1 segundo (configurável)

## Personalização

### Alterar Intervalo de Cálculos

Edite a constante em `src/main.cpp`:

```cpp
#define CALCULATION_INTERVAL_MS 1000  // 1 segundo
```

### Implementar Cálculos Customizados

Edite a função `performCalculations()` em `src/main.cpp`. Exemplos:

- Média de valores
- Mínimo/Máximo
- Cálculos de temperatura/umidade
- Lógica de controle (PID)
- Conversões de unidades

## Troubleshooting

### WiFi não aparece
- Verifique se o código foi compilado corretamente
- Reinicie a placa

### Modbus não lê
- Verifique conexões RS485 (A, B, GND)
- Confirme endereços Modbus dos dispositivos
- Verifique configuração de baud rate (padrão: 9600)
- Ajuste pinos RS485 se necessário

### Configuração não salva
- Verifique se há espaço suficiente na EEPROM
- Tente salvar novamente pela interface web

## Licença

Este projeto é fornecido como está, sem garantias.

