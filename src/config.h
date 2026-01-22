/**
 * @file config.h
 * @brief Definições de estruturas de dados e constantes do sistema
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
// CRÍTICO: usamos mutex (FreeRTOS) para proteger acesso concorrente ao config
// durante salvar/importar e durante o ciclo de leitura/cálculo/escrita.
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// ==================== DEFINIÇÕES ====================
#define AP_SSID "ESP32-Modbus-Config"
#define AP_PASSWORD "12345678"
#define WEB_SERVER_PORT 80
#define MODBUS_SERIAL_BAUD 9600
#define MODBUS_SERIAL_CONFIG SERIAL_8N1
// Paridade Modbus (configurável via interface web)
#define MODBUS_PARITY_NONE 0
#define MODBUS_PARITY_EVEN 1
#define MODBUS_PARITY_ODD 2
// Bits padrão do Modbus RTU (mantém compatibilidade com hardware)
#define MODBUS_DATA_BITS_DEFAULT 8
#define MODBUS_STOP_BITS_DEFAULT 1
#define MODBUS_START_BITS_DEFAULT 1
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
    bool isInput;            // true = Holding Register (0x03), false = Input Register (0x04) - DEPRECATED: usar registerType
    bool isOutput;           // true = registro de saída (para escrever resultados) - DEPRECATED: usar registerType
    bool readOnly;           // true = somente leitura, false = leitura e escrita - DEPRECATED: usar registerType
    char variableName[32];   // Nome da variável para uso em cálculos
    float gain;              // Ganho aplicado ao valor antes de atribuir à variável (padrão: 1.0)
    float offset;            // Offset aplicado ao valor antes de atribuir à variável (padrão: 0.0)
    bool kalmanEnabled;      // true = filtro de Kalman habilitado para este registro
    float kalmanQ;           // Process noise (ruído do processo) - padrão: 0.01
    float kalmanR;           // Measurement noise (ruído da medição) - padrão: 0.1
    bool generateGraph;      // true = incluir esta variável no gráfico em tempo real
    uint8_t writeFunction;   // Função Modbus para escrita: 0x06 (Write Single Register) ou 0x10 (Write Multiple Registers) - DEPRECATED: calculado automaticamente
    uint8_t writeRegisterCount; // Quantidade de registros para escrita - DEPRECATED: usar registerCount
    uint8_t registerType;    // 0 = Leitura, 1 = Escrita, 2 = Leitura e Escrita
    uint8_t registerCount;   // Quantidade de registradores para leitura/escrita (padrão: 1)
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
 * @struct WireGuardConfig
 * @brief Estrutura para configuração WireGuard VPN
 */
struct WireGuardConfig {
    bool enabled;              // WireGuard habilitado
    char privateKey[45];       // Chave privada do cliente (44 caracteres base64 + null)
    char publicKey[45];        // Chave pública do servidor (44 caracteres base64 + null)
    char serverAddress[64];    // IP ou domínio do servidor WireGuard
    uint16_t serverPort;       // Porta do servidor WireGuard (padrão: 51820)
    IPAddress localIP;         // IP local na rede VPN (ex: 10.0.0.2)
    IPAddress gatewayIP;       // IP do gateway/servidor na VPN (ex: 10.0.0.1)
    IPAddress subnetMask;      // Máscara de sub-rede (ex: 255.255.255.0)
};

/**
 * @struct SystemConfig
 * @brief Estrutura para configuração do sistema
 */
struct SystemConfig {
    uint32_t baudRate;       // Velocidade de comunicação RS485 (baud rate)
    uint8_t dataBits;        // Bits de dados (padrão: 8)
    uint8_t stopBits;        // Bits de parada (padrão: 1)
    uint8_t parity;          // Paridade (0=None, 1=Even, 2=Odd)
    uint8_t startBits;       // Bits de start (padrão: 1, fixo em UART)
    uint16_t timeout;        // Timeout de resposta Modbus em ms (padrão: 50)
    uint8_t deviceCount;     // Quantidade de dispositivos configurados
    ModbusDevice devices[MAX_DEVICES];
    MQTTConfig mqtt;         // Configuração MQTT
    WiFiConfig wifi;         // Configuração WiFi
    RTCConfig rtc;           // Configuração RTC
    WireGuardConfig wireguard; // Configuração WireGuard VPN
    char calculationCode[1024];  // Código Python/expressão para cálculos
};

// ==================== VARIÁVEIS GLOBAIS EXTERNAS ====================
// Declaradas em config.cpp
extern struct SystemConfig config;

// ==================== PROTEÇÃO DE CONCORRÊNCIA (CONFIG MUTEX) ====================
// Mutex recursivo para permitir lock aninhado (ex: handleSaveConfig() -> saveConfig()).
extern SemaphoreHandle_t g_configMutex;

// Flags globais para coordenar pausa do ciclo (evita WDT e concorrência durante save/import/reset)
extern volatile bool g_processingPaused;
extern volatile bool g_cycleInProgress;

/**
 * @brief Inicializa o mutex recursivo do config (chamar no setup antes do loop).
 */
void initConfigMutex();

/**
 * @brief Tenta adquirir o mutex do config.
 * @param timeoutTicks Timeout em ticks (use pdMS_TO_TICKS(ms)).
 * @return true se adquiriu, false caso contrário.
 */
bool lockConfig(TickType_t timeoutTicks = portMAX_DELAY);

/**
 * @brief Libera o mutex do config.
 */
void unlockConfig();

#endif // CONFIG_H

