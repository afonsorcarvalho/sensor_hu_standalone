/**
 * @file modbus_handler.cpp
 * @brief Implementação das funções Modbus RTU
 */

#include "modbus_handler.h"
#include "console.h"
#include <HardwareSerial.h>

// Variáveis globais
ModbusMaster node;
uint32_t currentBaudRate = 0;
uint32_t currentSerialConfig = 0;
KalmanState kalmanStates[MAX_DEVICES][MAX_REGISTERS_PER_DEVICE];


uint32_t buildSerialConfig(uint8_t dataBits, uint8_t parity, uint8_t stopBits) {
    // Normaliza entradas para valores suportados
    uint8_t normalizedDataBits = (dataBits == 7) ? 7 : 8;
    uint8_t normalizedStopBits = (stopBits == 2) ? 2 : 1;
    uint8_t normalizedParity = (parity == MODBUS_PARITY_EVEN || parity == MODBUS_PARITY_ODD) 
        ? parity 
        : MODBUS_PARITY_NONE;
    
    // Mapeia para constantes do Arduino Serial
    if (normalizedDataBits == 7) {
        if (normalizedParity == MODBUS_PARITY_EVEN && normalizedStopBits == 1) return SERIAL_7E1;
        if (normalizedParity == MODBUS_PARITY_EVEN && normalizedStopBits == 2) return SERIAL_7E2;
        if (normalizedParity == MODBUS_PARITY_ODD && normalizedStopBits == 1) return SERIAL_7O1;
        if (normalizedParity == MODBUS_PARITY_ODD && normalizedStopBits == 2) return SERIAL_7O2;
        if (normalizedParity == MODBUS_PARITY_NONE && normalizedStopBits == 2) return SERIAL_7N2;
        return SERIAL_7N1;
    }
    
    if (normalizedParity == MODBUS_PARITY_EVEN && normalizedStopBits == 1) return SERIAL_8E1;
    if (normalizedParity == MODBUS_PARITY_EVEN && normalizedStopBits == 2) return SERIAL_8E2;
    if (normalizedParity == MODBUS_PARITY_ODD && normalizedStopBits == 1) return SERIAL_8O1;
    if (normalizedParity == MODBUS_PARITY_ODD && normalizedStopBits == 2) return SERIAL_8O2;
    if (normalizedParity == MODBUS_PARITY_NONE && normalizedStopBits == 2) return SERIAL_8N2;
    return SERIAL_8N1;
}

void preTransmission() {
    digitalWrite(RS485_DE_RE_PIN, HIGH); // Habilita transmissão
}

void postTransmission() {
    digitalWrite(RS485_DE_RE_PIN, LOW); // Habilita recepção
}

void setupModbus(uint32_t baudRate, uint32_t serialConfig) {
    // Se baudRate não foi especificado, usa o da configuração
    if (baudRate == 0) {
        baudRate = config.baudRate;
    }
    
    // Se serialConfig não foi especificado, usa o da configuração atual
    if (serialConfig == 0) {
        serialConfig = buildSerialConfig(config.dataBits, config.parity, config.stopBits);
    }
    
    // Se já está configurado com os mesmos parâmetros, não precisa reconfigurar
    if (currentBaudRate == baudRate && currentSerialConfig == serialConfig && Serial2) {
        return;
    }
    
    // Se já estava configurado, fecha a serial anterior
    if (Serial2) {
        Serial2.end();
        delay(100);
    }
    
    // Configuração do Serial para RS485
    // Para ESP32-S3, Serial2 pode usar pinos customizados
    Serial2.begin(baudRate, serialConfig, RS485_RX_PIN, RS485_TX_PIN);
    
    // CRÍTICO: Configura timeout de resposta na serial usando o valor configurado pelo usuário
    // Isso permite ajustar para dispositivos lentos (100-200ms) ou rápidos (50ms ou menos)
    // A biblioteca ModbusMaster usa as funções Serial.read() internamente, então este timeout é respeitado
    uint16_t timeout = config.timeout;
    if (timeout < 10) timeout = 10;  // Mínimo 10ms
    if (timeout > 1000) timeout = 1000;  // Máximo 1000ms
    Serial2.setTimeout(timeout);
    
    // Configura pino DE/RE (Driver Enable / Receiver Enable) para controle RS485
    pinMode(RS485_DE_RE_PIN, OUTPUT);
    digitalWrite(RS485_DE_RE_PIN, LOW); // Inicia em modo recepção
    
    // Inicializa ModbusMaster
    // O endereço do escravo será configurado dinamicamente nas leituras
    node.begin(1, Serial2); // Endereço temporário, será alterado por dispositivo
    node.preTransmission(preTransmission);
    node.postTransmission(postTransmission);
    
    currentBaudRate = baudRate;
    currentSerialConfig = serialConfig;
    
    Serial.println("Modbus RTU configurado");
    Serial.print("Baud rate: ");
    Serial.println(baudRate);
    Serial.print("Config Serial: 0x");
    Serial.println(serialConfig, HEX);
    Serial.print("Pinos - TX: ");
    Serial.print(RS485_TX_PIN);
    Serial.print(", RX: ");
    Serial.print(RS485_RX_PIN);
    Serial.print(", DE/RE: ");
    Serial.println(RS485_DE_RE_PIN);
    
    // Log no console WebSocket
    String logMsg = "[Modbus] Configurado - Baud Rate: " + String(baudRate) + 
                   ", Paridade: " + String(config.parity) + 
                   ", Stop Bits: " + String(config.stopBits) + 
                   ", TX: GPIO" + String(RS485_TX_PIN) + 
                   ", RX: GPIO" + String(RS485_RX_PIN) + 
                   ", DE/RE: GPIO" + String(RS485_DE_RE_PIN) + "\r\n";
    consolePrint(logMsg);
}

void readAllDevices() {
    
    static unsigned long lastReadTime = 0;
    unsigned long currentTime = millis();
    
    // Mostra separador a cada ciclo de leitura (a cada ~1 segundo)
    if (currentTime - lastReadTime >= 900) { // 900ms para evitar spam, mas mostrar antes do próximo ciclo
        consolePrint("--- Leitura Modbus ---\r\n");
        lastReadTime = currentTime;
    }
    
    for (int i = 0; i < config.deviceCount; i++) {
        // CRÍTICO: Yield permite que o webserver e outras tarefas executem
        // Evita que a leitura Modbus trave todo o sistema
        yield();
        
        if (!config.devices[i].enabled) {
            continue;
        }
        
        uint8_t slaveAddr = config.devices[i].slaveAddress;
        
        // Configura o endereço do escravo para este dispositivo
        node.begin(slaveAddr, Serial2);
        
        for (int j = 0; j < config.devices[i].registerCount; j++) {
            // CRÍTICO: Yield antes de cada leitura para manter webserver responsivo
            yield();
            
            // Determina se deve ler baseado no registerType
            uint8_t registerType = config.devices[i].registers[j].registerType;
            bool shouldRead = (registerType == 0 || registerType == 2); // Leitura ou Leitura e Escrita
            
            // Compatibilidade: se registerType não estiver definido, usa campos antigos
            if (registerType == 0 && config.devices[i].registers[j].isOutput) {
                shouldRead = false; // Se é somente escrita, não lê
            } else if (registerType == 0 && config.devices[i].registers[j].isOutput == false && 
                       config.devices[i].registers[j].readOnly == false && 
                       config.devices[i].registers[j].isInput == true) {
                shouldRead = true; // Leitura e Escrita
            }
            
            // Pula registros que não devem ser lidos
            if (!shouldRead) {
                continue;
            }
            
            uint16_t regAddr = config.devices[i].registers[j].address;
            uint8_t registerCount = config.devices[i].registers[j].registerCount;
            if (registerCount == 0) registerCount = 1; // Garante mínimo de 1
            
            uint8_t result;
            
            // Lê registro Modbus usando função apropriada baseada no tipo
            // Padrão Modbus:
            // - Input Registers (0x04): somente leitura (registerType == 0)
            // - Holding Registers (0x03): leitura/escrita (registerType == 2)
            // Compatibilidade: se registerType não definido, usa isInput
            if (registerType == 0 || (!config.devices[i].registers[j].isInput && registerType == 0)) {
                // Input Register (0x04) - somente leitura, pode ler múltiplos registros
                result = node.readInputRegisters(regAddr, registerCount);
            } else {
                // Holding Register (0x03) - leitura/escrita, pode ler múltiplos registros
                result = node.readHoldingRegisters(regAddr, registerCount);
            }
            
            // Verifica se a leitura foi bem-sucedida
            if (result == node.ku8MBSuccess) {
                // Armazena valor raw do Modbus (primeiro registrador)
                // Para múltiplos registros, armazena apenas o primeiro (compatibilidade)
                uint16_t rawValueModbus = node.getResponseBuffer(0);
                
                // Aplica filtro de Kalman se habilitado
                float rawValue = (float)rawValueModbus;
                if (config.devices[i].registers[j].kalmanEnabled) {
                    // Aplica filtro de Kalman ao valor raw com parâmetros configuráveis
                    float kalmanQ = config.devices[i].registers[j].kalmanQ;
                    float kalmanR = config.devices[i].registers[j].kalmanR;
                    rawValue = kalmanFilter(&kalmanStates[i][j], rawValue, kalmanQ, kalmanR);
                    // Arredonda para uint16_t após filtro
                    rawValueModbus = (uint16_t)round(rawValue);
                } else {
                    // Se filtro foi desabilitado, reseta o estado
                    if (kalmanStates[i][j].initialized) {
                        kalmanReset(&kalmanStates[i][j]);
                    }
                }
                
                // Armazena valor (raw ou filtrado) no registro
                config.devices[i].registers[j].value = rawValueModbus;
                
                // Calcula valor processado (com gain e offset)
                float processedValue = (rawValue * config.devices[i].registers[j].gain) + config.devices[i].registers[j].offset;
                
                // Mostra no console
                String varName = strlen(config.devices[i].registers[j].variableName) > 0 
                    ? String(config.devices[i].registers[j].variableName) 
                    : "sem_nome";
                
                String msg = "[Modbus] Dev " + String(slaveAddr) + 
                            " Reg " + String(regAddr) + 
                            " (" + varName + "): " + 
                            String(processedValue, 2) + 
                            " (raw: " + String(config.devices[i].registers[j].value) + ")\r\n";
                // consolePrint já imprime no Serial e no WebSocket, não precisa duplicar
                consolePrint(msg);
            } else {
                // Em caso de erro, mantém o valor anterior ou zera
                String varName = strlen(config.devices[i].registers[j].variableName) > 0 
                    ? String(config.devices[i].registers[j].variableName) 
                    : "sem_nome";
                
                // Obtém descrição do erro
                String errorDesc = "Erro desconhecido";
                switch(result) {
                    case 0x01: errorDesc = "Funcao ilegal"; break;
                    case 0x02: errorDesc = "Endereco de dados ilegal"; break;
                    case 0x03: errorDesc = "Valor de dados ilegal"; break;
                    case 0x04: errorDesc = "Falha no dispositivo escravo"; break;
                    case 0xE1: errorDesc = "Timeout"; break;
                    case 0xE2: errorDesc = "Resposta invalida"; break;
                    case 0xE3: errorDesc = "Checksum invalido"; break;
                    case 0xE4: errorDesc = "Excecao Modbus"; break;
                    default: errorDesc = "Codigo: 0x" + String(result, HEX); break;
                }
                
                String msg = "[Modbus ERRO] Dev " + String(slaveAddr) + 
                            " Reg " + String(regAddr) + 
                            " (" + varName + "): " + errorDesc + "\r\n";
                // consolePrint já imprime no Serial e no WebSocket, não precisa duplicar
                consolePrint(msg);
            }
            
            delay(50); // Delay para garantir resposta antes da próxima leitura
        }
    }
}

void writeOutputRegisters() {
    
    for (int i = 0; i < config.deviceCount; i++) {
        if (!config.devices[i].enabled) {
            continue;
        }
        
        uint8_t slaveAddr = config.devices[i].slaveAddress;
        
        // Configura o endereço do escravo para este dispositivo
        node.begin(slaveAddr, Serial2);
        
        for (int j = 0; j < config.devices[i].registerCount; j++) {
            // CRÍTICO: Yield antes de cada escrita para manter webserver responsivo
            yield();
            
            // Determina se deve escrever baseado no registerType
            uint8_t registerType = config.devices[i].registers[j].registerType;
            bool shouldWrite = (registerType == 1 || registerType == 2); // Escrita ou Leitura e Escrita
            
            // Compatibilidade: se registerType não estiver definido, usa campos antigos
            if (registerType == 0) {
                if (config.devices[i].registers[j].isOutput && !config.devices[i].registers[j].readOnly) {
                    shouldWrite = true;
                } else {
                    shouldWrite = false;
                }
            }
            
            // Pula registros que não devem ser escritos
            if (!shouldWrite) {
                continue;
            }
            
            uint16_t regAddr = config.devices[i].registers[j].address;
            uint8_t registerCount = config.devices[i].registers[j].registerCount;
            if (registerCount == 0) registerCount = 1; // Garante mínimo de 1
            
            uint8_t result;
            
            // Determina função Modbus apropriada baseada na quantidade de registros
            // Padrão Modbus: 0x06 para 1 registrador, 0x10 para múltiplos
            if (registerCount == 1) {
                // Write Single Register (0x06)
                uint16_t value = config.devices[i].registers[j].value;
                result = node.writeSingleRegister(regAddr, value);
            } else {
                // Write Multiple Registers (0x10)
                // Prepara buffer com valores (usa valor atual repetido para todos os registros)
                // TODO: Em implementação futura, pode usar array de valores
                node.setTransmitBuffer(0, config.devices[i].registers[j].value);
                for (uint8_t k = 1; k < registerCount && k < 125; k++) {
                    node.setTransmitBuffer(k, config.devices[i].registers[j].value);
                }
                result = node.writeMultipleRegisters(regAddr, registerCount);
            }
            
            if (result != node.ku8MBSuccess) {
                Serial.print("Erro ao escrever dispositivo ");
                Serial.print(slaveAddr);
                Serial.print(" registro ");
                Serial.print(regAddr);
                Serial.print(" - Código: ");
                Serial.println(result);
            }
            
            delay(50); // Delay para garantir escrita antes da próxima operação
        }
    }
}

