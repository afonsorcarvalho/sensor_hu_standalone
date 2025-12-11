/**
 * @file main.cpp
 * @brief Sistema Modbus RTU Master para ESP32-S3-RS485-CAN
 * 
 * Este programa implementa um mestre Modbus RTU que:
 * - Opera como Access Point WiFi para configuração
 * - Possui interface web para configurar dispositivos e registros
 * - Lê registros de múltiplos dispositivos Modbus
 * - Executa cálculos customizados a cada 1 segundo
 * - Escreve resultados em registros de saída
 */

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "config_storage.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "modbus_handler.h"
#include "calculations.h"
#include "rtc_manager.h"
#include "console.h"
#include "kalman_filter.h"

// Variáveis globais
unsigned long lastCalculationTime = 0;
bool littleFSStatus = false;  // Status de inicialização do LittleFS

// ==================== SETUP E LOOP ====================

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n=== Sistema Modbus RTU Master ESP32-S3 ===");
    
    // Inicializa LittleFS
    littleFSStatus = initLittleFS();
    if (!littleFSStatus) {
        Serial.println("AVISO: LittleFS nao inicializado. Interface web pode nao funcionar.");
    }
    
    // Carrega configuração salva
    loadConfig();
    
    // Configura WiFi baseado na configuração salva
    bool connected = false;
    if (strcmp(config.wifi.mode, "sta") == 0 && strlen(config.wifi.staSSID) > 0) {
        // Tenta conectar no modo STA
        connected = setupWiFiSTA();
    }
    
    // Se não conectou no STA, usa modo AP
    if (!connected) {
        setupWiFiAP();
    }
    
    // Configura servidor web
    setupWebServer();
    
    // Configura Modbus RTU (usa baud rate da configuração)
    setupModbus(config.baudRate);
    
    // Inicializa estados do filtro de Kalman para todos os registros
    for (int i = 0; i < config.deviceCount; i++) {
        for (int j = 0; j < config.devices[i].registerCount; j++) {
            kalmanReset(&kalmanStates[i][j]);
        }
    }
    
    // Log de inicialização no console web (após WebSocket estar pronto)
    if (!littleFSStatus) {
        consolePrint("[Sistema] AVISO: LittleFS nao inicializado. Interface web pode nao funcionar.\r\n");
    } else {
        consolePrint("[Sistema] LittleFS inicializado com sucesso\r\n");
    }
    
    // Log de configuração carregada no console web
    consolePrint("[Sistema] Configuracao carregada: " + String(config.deviceCount) + " dispositivos\r\n");
    
    // Log de WiFi no console web
    if (WiFi.status() == WL_CONNECTED) {
        String wifiLog = "[WiFi] Modo: Station (STA)\r\n";
        wifiLog += "[WiFi] IP: " + WiFi.localIP().toString() + "\r\n";
        wifiLog += "[WiFi] Acesse: http://" + WiFi.localIP().toString() + "\r\n";
        consolePrint(wifiLog);
    } else {
        const char* apSSID = (strlen(config.wifi.apSSID) > 0) ? config.wifi.apSSID : AP_SSID;
        String wifiLog = "[WiFi] Modo: Access Point (AP)\r\n";
        wifiLog += "[WiFi] SSID: " + String(apSSID) + "\r\n";
        wifiLog += "[WiFi] IP: " + WiFi.softAPIP().toString() + "\r\n";
        wifiLog += "[WiFi] Acesse: http://" + WiFi.softAPIP().toString() + "\r\n";
        consolePrint(wifiLog);
    }
    
    // Inicializa RTC
    if (config.rtc.enabled) {
        if (config.rtc.ntpEnabled && WiFi.status() == WL_CONNECTED) {
            // Tenta sincronizar com NTP
            Serial.println("Tentando sincronizar NTP...");
            consolePrint("[RTC] Tentando sincronizar NTP...\r\n");
            syncNTP();
        } else if (config.rtc.epochTime > 0) {
            // Usa data/hora salva anteriormente
            rtcInitialized = true;
            config.rtc.bootTime = millis();  // Atualiza bootTime para continuar contando
            Serial.println("RTC inicializado com data/hora salva");
            
            char dateStr[11];
            char timeStr[9];
            formatDateTime(getCurrentEpochTime(), dateStr, timeStr, config.rtc.timezone);
            Serial.print("Hora atual: ");
            Serial.print(dateStr);
            Serial.print(" ");
            Serial.println(timeStr);
            
            String rtcLog = "[RTC] RTC inicializado com data/hora salva\r\n";
            rtcLog += "[RTC] Hora atual: " + String(dateStr) + " " + String(timeStr) + "\r\n";
            consolePrint(rtcLog);
        } else {
            Serial.println("RTC habilitado mas nao inicializado (configure data/hora manualmente ou conecte WiFi para NTP)");
            String logMsg = "[RTC] RTC habilitado mas nao inicializado. Configure data/hora manualmente ou conecte WiFi para NTP.\r\n";
            consolePrint(logMsg);
        }
    } else {
        consolePrint("[RTC] RTC desabilitado\r\n");
    }
    
    Serial.println("Sistema inicializado!");
    consolePrint("[Sistema] Sistema inicializado com sucesso!\r\n");
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Modo: Station (STA)");
        Serial.println("IP: " + WiFi.localIP().toString());
        Serial.println("Acesse: http://" + WiFi.localIP().toString());
    } else {
        const char* apSSID = (strlen(config.wifi.apSSID) > 0) ? config.wifi.apSSID : AP_SSID;
        Serial.println("Modo: Access Point (AP)");
        Serial.println("Conecte-se ao WiFi: " + String(apSSID));
        Serial.println("Acesse: http://" + WiFi.softAPIP().toString());
    }
    Serial.println("Console WebSocket disponivel na porta 81");
    
    // Mensagem final no console web
    consolePrint("=== Sistema inicializado com sucesso! ===\r\n");
    consolePrint("Digite 'help' para ver comandos disponiveis.\r\n");
}

void loop() {
    // AsyncWebServer e AsyncWebSocket processam requisições automaticamente
    // Não é necessário chamar loop() ou handleClient()
    
    // Tenta sincronizar NTP periodicamente (se habilitado e conectado)
    if (config.rtc.enabled && config.rtc.ntpEnabled && WiFi.status() == WL_CONNECTED) {
        unsigned long currentMillis = millis();
        if (currentMillis - lastNtpSync > NTP_SYNC_INTERVAL) {
            syncNTP();
        }
    }
    
    // Se RTC não está inicializado mas está habilitado e WiFi conectado, tenta sincronizar
    if (config.rtc.enabled && config.rtc.ntpEnabled && !rtcInitialized && WiFi.status() == WL_CONNECTED) {
        if (millis() - lastNtpSync > 30000) {  // Tenta a cada 30 segundos se não inicializado
            syncNTP();
        }
    }
    
    // Executa cálculos a cada 1 segundo
    unsigned long currentTime = millis();
    if (currentTime - lastCalculationTime >= CALCULATION_INTERVAL_MS) {
        lastCalculationTime = currentTime;
        
        // Lê todos os dispositivos
        readAllDevices();
        
        // Realiza cálculos customizados
        performCalculations();
        
        // Escreve resultados nos registros de saída
        writeOutputRegisters();
        
        Serial.println("Ciclo de leitura/cálculo/escrita executado");
    }
    
    delay(10);
}
