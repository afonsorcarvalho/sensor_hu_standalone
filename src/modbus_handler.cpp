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
KalmanState kalmanStates[MAX_DEVICES][MAX_REGISTERS_PER_DEVICE];

void preTransmission() {
    digitalWrite(RS485_DE_RE_PIN, HIGH); // Habilita transmissão
}

void postTransmission() {
    digitalWrite(RS485_DE_RE_PIN, LOW); // Habilita recepção
}

void setupModbus(uint32_t baudRate) {
    // Se baudRate não foi especificado, usa o da configuração
    if (baudRate == 0) {
        baudRate = config.baudRate;
    }
    
    // Se já está configurado com o mesmo baud rate, não precisa reconfigurar
    if (currentBaudRate == baudRate && Serial2) {
        return;
    }
    
    // Se já estava configurado, fecha a serial anterior
    if (Serial2) {
        Serial2.end();
        delay(100);
    }
    
    // Configuração do Serial para RS485
    // Para ESP32-S3, Serial2 pode usar pinos customizados
    Serial2.begin(baudRate, MODBUS_SERIAL_CONFIG, RS485_RX_PIN, RS485_TX_PIN);
    
    // Configura pino DE/RE (Driver Enable / Receiver Enable) para controle RS485
    pinMode(RS485_DE_RE_PIN, OUTPUT);
    digitalWrite(RS485_DE_RE_PIN, LOW); // Inicia em modo recepção
    
    // Inicializa ModbusMaster
    // O endereço do escravo será configurado dinamicamente nas leituras
    node.begin(1, Serial2); // Endereço temporário, será alterado por dispositivo
    node.preTransmission(preTransmission);
    node.postTransmission(postTransmission);
    
    currentBaudRate = baudRate;
    
    Serial.println("Modbus RTU configurado");
    Serial.print("Baud rate: ");
    Serial.println(baudRate);
    Serial.print("Pinos - TX: ");
    Serial.print(RS485_TX_PIN);
    Serial.print(", RX: ");
    Serial.print(RS485_RX_PIN);
    Serial.print(", DE/RE: ");
    Serial.println(RS485_DE_RE_PIN);
    
    // Log no console WebSocket
    String logMsg = "[Modbus] Configurado - Baud Rate: " + String(baudRate) + 
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
        if (!config.devices[i].enabled) {
            continue;
        }
        
        uint8_t slaveAddr = config.devices[i].slaveAddress;
        
        // Configura o endereço do escravo para este dispositivo
        node.begin(slaveAddr, Serial2);
        
        for (int j = 0; j < config.devices[i].registerCount; j++) {
            // Pula registros de saída (não lemos, apenas escrevemos)
            if (config.devices[i].registers[j].isOutput) {
                continue;
            }
            
            uint16_t regAddr = config.devices[i].registers[j].address;
            uint8_t result;
            
            // Lê registro Modbus
            if (config.devices[i].registers[j].isInput) {
                // Holding Register (0x03) - Leitura/escrita
                result = node.readHoldingRegisters(regAddr, 1);
            } else {
                // Input Register (0x04) - Apenas leitura
                result = node.readInputRegisters(regAddr, 1);
            }
            
            // Verifica se a leitura foi bem-sucedida
            if (result == node.ku8MBSuccess) {
                // Armazena valor raw do Modbus
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
            // Apenas escreve em registros marcados como saída e que não sejam somente leitura
            if (!config.devices[i].registers[j].isOutput || config.devices[i].registers[j].readOnly) {
                continue;
            }
            
            uint16_t regAddr = config.devices[i].registers[j].address;
            uint16_t value = config.devices[i].registers[j].value;
            
            // Escreve no registro Modbus (Holding Register)
            // Apenas Holding Registers podem ser escritos
            uint8_t result = node.writeSingleRegister(regAddr, value);
            
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

