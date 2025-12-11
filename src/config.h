/**
 * @file config.h
 * @brief Definições de estruturas de dados e constantes do sistema
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ==================== DEFINIÇÕES ====================
#define AP_SSID "ESP32-Modbus-Config"
#define AP_PASSWORD "12345678"
#define WEB_SERVER_PORT 80
#define MODBUS_SERIAL_BAUD 9600
#define MODBUS_SERIAL_CONFIG SERIAL_8N1
#define MAX_DEVICES 10
#define MAX_REGISTERS_PER_DEVICE 20
#define CALCULATION_INTERVAL_MS 1000

// Pinos RS485 (ajustar conforme hardware)
// Pinout ESP32-S3-RS485-CAN (Waveshare):
// GPIO17: RS485 TX
// GPIO18: RS485 RX
// GPIO21: RS485 EN (Enable/Driver Enable)
#define RS485_TX_PIN 17
#define RS485_RX_PIN 18
#define RS485_DE_RE_PIN 21  // Driver Enable / Receiver Enable

// ==================== ESTRUTURAS DE DADOS ====================

/**
 * @struct ModbusRegister
 * @brief Estrutura para armazenar informações de um registro Modbus
 */
struct ModbusRegister {
    uint16_t address;        // Endereço do registro
    uint16_t value;          // Valor atual lido (raw do Modbus)
    bool isInput;            // true = Holding Register (0x03), false = Input Register (0x04)
    bool isOutput;           // true = registro de saída (para escrever resultados)
    bool readOnly;           // true = somente leitura, false = leitura e escrita
    char variableName[32];   // Nome da variável para uso em cálculos
    float gain;              // Ganho aplicado ao valor antes de atribuir à variável (padrão: 1.0)
    float offset;            // Offset aplicado ao valor antes de atribuir à variável (padrão: 0.0)
    bool kalmanEnabled;      // true = filtro de Kalman habilitado para este registro
    float kalmanQ;           // Process noise (ruído do processo) - padrão: 0.01
    float kalmanR;           // Measurement noise (ruído da medição) - padrão: 0.1
};

/**
 * @struct ModbusDevice
 * @brief Estrutura para armazenar informações de um dispositivo Modbus
 */
struct ModbusDevice {
    uint8_t slaveAddress;    // Endereço Modbus do dispositivo
    bool enabled;            // Dispositivo habilitado
    uint8_t registerCount;   // Quantidade de registros configurados
    char deviceName[32];     // Nome do dispositivo
    ModbusRegister registers[MAX_REGISTERS_PER_DEVICE];
};

/**
 * @struct MQTTConfig
 * @brief Estrutura para configuração MQTT
 */
struct MQTTConfig {
    bool enabled;           // MQTT habilitado
    char server[64];        // Servidor MQTT
    uint16_t port;          // Porta MQTT
    char user[32];          // Usuário MQTT
    char password[32];      // Senha MQTT
    char topic[64];         // Tópico base
    uint16_t interval;      // Intervalo de publicação (segundos)
};

/**
 * @struct WiFiConfig
 * @brief Estrutura para configuração WiFi
 */
struct WiFiConfig {
    char mode[4];           // "ap" ou "sta"
    char apSSID[32];        // SSID do Access Point
    char apPassword[32];    // Senha do Access Point
    char staSSID[32];       // SSID da rede WiFi (modo STA)
    char staPassword[32];   // Senha da rede WiFi (modo STA)
};

/**
 * @struct RTCConfig
 * @brief Estrutura para configuração RTC
 */
struct RTCConfig {
    bool enabled;           // RTC habilitado
    int8_t timezone;       // Fuso horário (UTC offset)
    char ntpServer[64];     // Servidor NTP
    bool ntpEnabled;       // Atualizar via NTP
    uint32_t epochTime;    // Timestamp Unix (epoch) da última sincronização
    uint32_t bootTime;     // millis() no momento da última sincronização
};

/**
 * @struct SystemConfig
 * @brief Estrutura para configuração do sistema
 */
struct SystemConfig {
    uint32_t baudRate;       // Velocidade de comunicação RS485 (baud rate)
    uint8_t deviceCount;     // Quantidade de dispositivos configurados
    ModbusDevice devices[MAX_DEVICES];
    MQTTConfig mqtt;         // Configuração MQTT
    WiFiConfig wifi;         // Configuração WiFi
    RTCConfig rtc;           // Configuração RTC
    char calculationCode[1024];  // Código Python/expressão para cálculos
};

// ==================== VARIÁVEIS GLOBAIS EXTERNAS ====================
// Declaradas em config.cpp
extern struct SystemConfig config;

#endif // CONFIG_H

