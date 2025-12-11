/**
 * @file web_server.cpp
 * @brief Implementação dos handlers HTTP do servidor web
 */

#include "web_server.h"
#include "config.h"
#include "config_storage.h"
#include "modbus_handler.h"
#include "rtc_manager.h"
#include "console.h"
#include "expression_parser.h"
#include <ArduinoJson.h>
#include <ESP.h>
#include <WiFi.h>
#include <math.h>

// Variáveis globais
AsyncWebServer server(WEB_SERVER_PORT);
AsyncWebSocket* consoleWebSocket = nullptr;

bool initLittleFS() {
    if (!LittleFS.begin(true)) {
        Serial.println("Erro ao montar LittleFS");
        return false;
    }
    Serial.println("LittleFS montado com sucesso");
    return true;
}

void setupWebServer() {
    // Inicializa WebSocket do console ANTES das rotas
    Serial.println("Inicializando WebSocket do console...");
    consoleWebSocket = new AsyncWebSocket("/console");
    initConsoleWebSocket(consoleWebSocket);
    server.addHandler(consoleWebSocket);
    
    // Rota para página principal (interface de configuração)
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        handleRoot(request);
    });
    
    // Rota para obter configuração atual (JSON)
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request){
        handleGetConfig(request);
    });
    
    // Rota para salvar nova configuração (POST JSON)
    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
            handleSaveConfig(request, data, len);
        });
    
    // Rota para leitura manual de registros
    server.on("/api/read", HTTP_GET, [](AsyncWebServerRequest *request){
        handleReadRegisters(request);
    });
    
    // Rota para reboot do sistema
    server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request){
        handleReboot(request);
    });
    
    // Rota para obter horário atual do RTC
    server.on("/api/rtc/current", HTTP_GET, [](AsyncWebServerRequest *request){
        handleGetCurrentTime(request);
    });
    
    // Rota para definir data/hora manualmente
    server.on("/api/rtc/set", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
            handleSetTime(request, data, len);
        });
    
    // Rota para sincronizar NTP agora
    server.on("/api/rtc/sync", HTTP_POST, [](AsyncWebServerRequest *request){
        handleSyncNTP(request);
    });
    
    // Rota para exportar configurações
    server.on("/api/config/export", HTTP_GET, [](AsyncWebServerRequest *request){
        handleExportConfig(request);
    });
    
    // Rota para importar configurações
    server.on("/api/config/import", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
            handleImportConfig(request, data, len);
        });
    
    // Rota para scan de redes WiFi
    server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *request){
        handleWiFiScan(request);
    });
    
    // Rota para testar cálculo/expressão
    server.on("/api/calc/test", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
            handleTestCalculation(request, data, len);
        });
    
    // Rota para obter variáveis disponíveis
    server.on("/api/calc/variables", HTTP_GET, [](AsyncWebServerRequest *request){
        handleGetVariables(request);
    });
    
    // Rota para escrever valor de variável
    server.on("/api/variable/write", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
            handleWriteVariable(request, data, len);
        });
    
    // Inicia o servidor web
    server.begin();
    
    Serial.println("Servidor web iniciado na porta 80");
    Serial.println("Console WebSocket disponivel em /console");
    
    // Log no console web (após WebSocket estar inicializado)
    delay(100);  // Pequeno delay para garantir que WebSocket está pronto
    consolePrint("=== Sistema Modbus RTU Master ESP32-S3 ===\r\n");
    consolePrint("Servidor web iniciado na porta 80\r\n");
    consolePrint("Console WebSocket disponivel em /console\r\n");
}

void handleRoot(AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
}

void handleGetConfig(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(24576);  // 24KB para garantir espaço suficiente
    doc["baudRate"] = config.baudRate;
    doc["deviceCount"] = config.deviceCount;
    
    // Adiciona configuração MQTT
    JsonObject mqttObj = doc.createNestedObject("mqtt");
    mqttObj["enabled"] = config.mqtt.enabled;
    mqttObj["server"] = config.mqtt.server;
    mqttObj["port"] = config.mqtt.port;
    mqttObj["user"] = config.mqtt.user;
    mqttObj["password"] = config.mqtt.password;
    mqttObj["topic"] = config.mqtt.topic;
    mqttObj["interval"] = config.mqtt.interval;
    
    // Adiciona configuração WiFi
    JsonObject wifiObj = doc.createNestedObject("wifi");
    wifiObj["mode"] = String(config.wifi.mode);
    wifiObj["apSSID"] = String(config.wifi.apSSID);
    wifiObj["apPassword"] = String(config.wifi.apPassword);
    wifiObj["staSSID"] = String(config.wifi.staSSID);
    wifiObj["staPassword"] = String(config.wifi.staPassword);
    
    // Adiciona configuração RTC
    JsonObject rtcObj = doc.createNestedObject("rtc");
    rtcObj["enabled"] = config.rtc.enabled;
    rtcObj["timezone"] = config.rtc.timezone;
    rtcObj["ntpServer"] = config.rtc.ntpServer;
    rtcObj["ntpEnabled"] = config.rtc.ntpEnabled;
    rtcObj["epochTime"] = config.rtc.epochTime;
    
    // Adiciona código de cálculo
    doc["calculationCode"] = String(config.calculationCode);
    
    JsonArray devicesArray = doc.createNestedArray("devices");
    
    for (int i = 0; i < config.deviceCount; i++) {
        JsonObject deviceObj = devicesArray.createNestedObject();
        deviceObj["slaveAddress"] = config.devices[i].slaveAddress;
        deviceObj["enabled"] = config.devices[i].enabled;
        deviceObj["deviceName"] = String(config.devices[i].deviceName);
        
        JsonArray registersArray = deviceObj.createNestedArray("registers");
        
        // Salva todos os registros
        for (int j = 0; j < config.devices[i].registerCount; j++) {
            JsonObject regObj = registersArray.createNestedObject();
            regObj["address"] = config.devices[i].registers[j].address;
            regObj["isInput"] = config.devices[i].registers[j].isInput;
            regObj["isOutput"] = config.devices[i].registers[j].isOutput;
            regObj["readOnly"] = config.devices[i].registers[j].readOnly;
            regObj["value"] = config.devices[i].registers[j].value;
            regObj["variableName"] = String(config.devices[i].registers[j].variableName);
            regObj["gain"] = config.devices[i].registers[j].gain;
            regObj["offset"] = config.devices[i].registers[j].offset;
            regObj["kalmanEnabled"] = config.devices[i].registers[j].kalmanEnabled;
            regObj["kalmanQ"] = config.devices[i].registers[j].kalmanQ;
            regObj["kalmanR"] = config.devices[i].registers[j].kalmanR;
        }
        
        // IMPORTANTE: registerCount baseado no tamanho real do array
        deviceObj["registerCount"] = registersArray.size();
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleSaveConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
    if (data && len > 0) {
        String body = String((char*)data);
        
        // Tamanho aumentado para suportar muitos dispositivos e registros
        DynamicJsonDocument doc(24576);  // 24KB para garantir espaço suficiente
        DeserializationError error = deserializeJson(doc, body);
        
        if (error) {
            Serial.print("Erro ao deserializar JSON: ");
            Serial.println(error.c_str());
            Serial.print("Tamanho do body recebido: ");
            Serial.print(body.length());
            Serial.println(" bytes");
            request->send(400, "application/json", "{\"error\":\"JSON inválido\"}");
            return;
        }
        
        // Log para debug
        Serial.print("JSON recebido com sucesso. Tamanho: ");
        Serial.print(body.length());
        Serial.println(" bytes");
        
        // Atualiza configuração do sistema
        uint32_t newBaudRate = doc["baudRate"] | MODBUS_SERIAL_BAUD;
        config.baudRate = newBaudRate;
        
        // Se o baud rate mudou, reconfigura o Modbus
        if (newBaudRate != currentBaudRate) {
            setupModbus(newBaudRate);
        }
        
        // Atualiza configuração MQTT
        if (doc.containsKey("mqtt")) {
            JsonObject mqttObj = doc["mqtt"];
            config.mqtt.enabled = mqttObj["enabled"] | false;
            strncpy(config.mqtt.server, mqttObj["server"] | "", sizeof(config.mqtt.server) - 1);
            config.mqtt.port = mqttObj["port"] | 1883;
            strncpy(config.mqtt.user, mqttObj["user"] | "", sizeof(config.mqtt.user) - 1);
            strncpy(config.mqtt.password, mqttObj["password"] | "", sizeof(config.mqtt.password) - 1);
            strncpy(config.mqtt.topic, mqttObj["topic"] | "esp32/modbus", sizeof(config.mqtt.topic) - 1);
            config.mqtt.interval = mqttObj["interval"] | 60;
        }
        
        // Atualiza configuração WiFi
        if (doc.containsKey("wifi")) {
            JsonObject wifiObj = doc["wifi"];
            const char* modeStr = wifiObj["mode"] | "ap";
            strncpy(config.wifi.mode, modeStr, sizeof(config.wifi.mode) - 1);
            config.wifi.mode[sizeof(config.wifi.mode) - 1] = '\0';
            
            const char* apSSIDStr = wifiObj["apSSID"] | AP_SSID;
            strncpy(config.wifi.apSSID, apSSIDStr, sizeof(config.wifi.apSSID) - 1);
            config.wifi.apSSID[sizeof(config.wifi.apSSID) - 1] = '\0';
            
            const char* apPasswordStr = wifiObj["apPassword"] | AP_PASSWORD;
            strncpy(config.wifi.apPassword, apPasswordStr, sizeof(config.wifi.apPassword) - 1);
            config.wifi.apPassword[sizeof(config.wifi.apPassword) - 1] = '\0';
            
            const char* staSSIDStr = wifiObj["staSSID"] | "";
            strncpy(config.wifi.staSSID, staSSIDStr, sizeof(config.wifi.staSSID) - 1);
            config.wifi.staSSID[sizeof(config.wifi.staSSID) - 1] = '\0';
            
            const char* staPasswordStr = wifiObj["staPassword"] | "";
            strncpy(config.wifi.staPassword, staPasswordStr, sizeof(config.wifi.staPassword) - 1);
            config.wifi.staPassword[sizeof(config.wifi.staPassword) - 1] = '\0';
        }
        
        // Atualiza configuração RTC
        if (doc.containsKey("rtc")) {
            JsonObject rtcObj = doc["rtc"];
            config.rtc.enabled = rtcObj["enabled"] | false;
            config.rtc.timezone = rtcObj["timezone"] | -3;
            const char* ntpServerStr = rtcObj["ntpServer"] | "pool.ntp.org";
            strncpy(config.rtc.ntpServer, ntpServerStr, sizeof(config.rtc.ntpServer) - 1);
            config.rtc.ntpServer[sizeof(config.rtc.ntpServer) - 1] = '\0';
            config.rtc.ntpEnabled = rtcObj["ntpEnabled"] | true;
        }
        
        // Atualiza código de cálculo
        if (doc.containsKey("calculationCode")) {
            const char* code = doc["calculationCode"] | "";
            strncpy(config.calculationCode, code, sizeof(config.calculationCode) - 1);
            config.calculationCode[sizeof(config.calculationCode) - 1] = '\0';
        }
        
        config.deviceCount = doc["deviceCount"] | 0;
        if (config.deviceCount > MAX_DEVICES) {
            config.deviceCount = MAX_DEVICES;
        }
        
        // Verifica se há array de dispositivos
        if (!doc.containsKey("devices") || !doc["devices"].is<JsonArray>()) {
            Serial.println("ERRO: Array de dispositivos nao encontrado no JSON recebido");
            request->send(400, "application/json", "{\"error\":\"Array de dispositivos não encontrado\"}");
            return;
        }
        
        JsonArray devicesArray = doc["devices"].as<JsonArray>();
        int deviceCountFromJson = devicesArray.size();
        
        Serial.print("Dispositivos recebidos no JSON: ");
        Serial.println(deviceCountFromJson);
        
        for (int i = 0; i < config.deviceCount && i < MAX_DEVICES && i < deviceCountFromJson; i++) {
            JsonObject deviceObj = devicesArray[i];
            if (!deviceObj) {
                Serial.print("AVISO: Dispositivo ");
                Serial.print(i);
                Serial.println(" nao encontrado no JSON");
                continue;
            }
            
            config.devices[i].slaveAddress = deviceObj["slaveAddress"] | 1;
            config.devices[i].enabled = deviceObj["enabled"] | true;
            
            // Carrega nome do dispositivo
            const char* devName = deviceObj["deviceName"] | "";
            strncpy(config.devices[i].deviceName, devName, sizeof(config.devices[i].deviceName) - 1);
            config.devices[i].deviceName[sizeof(config.devices[i].deviceName) - 1] = '\0';
            
            // Verifica se há array de registros
            if (!deviceObj.containsKey("registers") || !deviceObj["registers"].is<JsonArray>()) {
                Serial.print("AVISO: Array de registros nao encontrado para dispositivo ");
                Serial.println(i);
                config.devices[i].registerCount = 0;
                continue;
            }
            
            JsonArray registersArray = deviceObj["registers"].as<JsonArray>();
            int registerCountFromJson = registersArray.size();
            
            // IMPORTANTE: registerCount deve ser baseado no tamanho real do array de registros
            config.devices[i].registerCount = registerCountFromJson;
            
            // Limita ao máximo permitido
            if (config.devices[i].registerCount > MAX_REGISTERS_PER_DEVICE) {
                config.devices[i].registerCount = MAX_REGISTERS_PER_DEVICE;
            }
            
            Serial.print("Processando dispositivo ");
            Serial.print(i);
            Serial.print(": ");
            Serial.print(config.devices[i].registerCount);
            Serial.println(" registros");
            
            for (int j = 0; j < config.devices[i].registerCount && j < registerCountFromJson; j++) {
                JsonObject regObj = registersArray[j];
                if (!regObj) {
                    Serial.print("AVISO: Registro ");
                    Serial.print(j);
                    Serial.print(" do dispositivo ");
                    Serial.print(i);
                    Serial.println(" nao encontrado no JSON");
                    continue;
                }
                
                config.devices[i].registers[j].address = regObj["address"] | 0;
                config.devices[i].registers[j].isInput = regObj["isInput"] | true;
                config.devices[i].registers[j].isOutput = regObj["isOutput"] | false;
                config.devices[i].registers[j].readOnly = regObj["readOnly"] | false;
                
                // IMPORTANTE: Inicializa valor com 0 mesmo sem leitura do dispositivo
                // Isso garante que a variável existe e pode ser usada nas expressões
                config.devices[i].registers[j].value = 0;
                
                // Carrega nome da variável
                const char* varName = regObj["variableName"] | "";
                strncpy(config.devices[i].registers[j].variableName, varName, sizeof(config.devices[i].registers[j].variableName) - 1);
                config.devices[i].registers[j].variableName[sizeof(config.devices[i].registers[j].variableName) - 1] = '\0';
                
                // Carrega ganho e offset (valores padrão se não especificados)
                config.devices[i].registers[j].gain = regObj["gain"] | 1.0f;
                config.devices[i].registers[j].offset = regObj["offset"] | 0.0f;
                
                // Carrega kalmanEnabled (padrão: false)
                config.devices[i].registers[j].kalmanEnabled = regObj["kalmanEnabled"] | false;
                
                // Carrega parâmetros do filtro de Kalman (valores padrão se não especificados)
                config.devices[i].registers[j].kalmanQ = regObj["kalmanQ"] | 0.01f;
                config.devices[i].registers[j].kalmanR = regObj["kalmanR"] | 0.1f;
                
                Serial.print("  Registro ");
                Serial.print(j);
                Serial.print(": endereco=");
                Serial.print(config.devices[i].registers[j].address);
                Serial.print(", variavel=");
                Serial.println(config.devices[i].registers[j].variableName);
            }
        }
        
        // Valida valores do Kalman antes de salvar
        for (int i = 0; i < config.deviceCount; i++) {
            for (int j = 0; j < config.devices[i].registerCount; j++) {
                // Valida kalmanQ
                if (isnan(config.devices[i].registers[j].kalmanQ) || isinf(config.devices[i].registers[j].kalmanQ) || config.devices[i].registers[j].kalmanQ <= 0.0f) {
                    config.devices[i].registers[j].kalmanQ = 0.01f;
                }
                // Valida kalmanR
                if (isnan(config.devices[i].registers[j].kalmanR) || isinf(config.devices[i].registers[j].kalmanR) || config.devices[i].registers[j].kalmanR <= 0.0f) {
                    config.devices[i].registers[j].kalmanR = 0.1f;
                }
            }
        }
        
        // Salva na EEPROM (memória não volátil)
        Serial.println("Salvando configuração...");
        consolePrint("[Acao] Botao 'Salvar Todas as Configuracoes' clicado\r\n");
        
        bool saveSuccess = saveConfig();
        
        if (saveSuccess) {
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        } else {
            Serial.println("ERRO: Falha ao salvar configuração");
            request->send(500, "application/json", "{\"error\":\"Erro ao salvar configuração\"}");
        }
    } else {
        request->send(400, "application/json", "{\"error\":\"Dados não fornecidos\"}");
    }
}

void handleReadRegisters(AsyncWebServerRequest *request) {
    readAllDevices();
    
    DynamicJsonDocument doc(4096);
    doc["status"] = "ok";
    doc["timestamp"] = millis();
    
    JsonArray devicesArray = doc.createNestedArray("devices");
    for (int i = 0; i < config.deviceCount; i++) {
        JsonObject deviceObj = devicesArray.createNestedObject();
        deviceObj["slaveAddress"] = config.devices[i].slaveAddress;
        
        JsonArray registersArray = deviceObj.createNestedArray("registers");
        for (int j = 0; j < config.devices[i].registerCount; j++) {
            JsonObject regObj = registersArray.createNestedObject();
            regObj["address"] = config.devices[i].registers[j].address;
            regObj["value"] = config.devices[i].registers[j].value;
        }
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleReboot(AsyncWebServerRequest *request) {
    request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Reiniciando em 2 segundos...\"}");
    
    Serial.println("Reboot solicitado via web interface");
    Serial.println("Reiniciando em 2 segundos...");
    
    delay(2000);
    ESP.restart();
}

void handleGetCurrentTime(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(512);
    
    uint32_t currentEpoch = getCurrentEpochTime();
    
    char timeStr[9] = "00:00:00";
    char dateStr[11] = "0000-00-00";
    
    if (currentEpoch > 0) {
        formatDateTime(currentEpoch, dateStr, timeStr, config.rtc.timezone);
    } else {
        // Se RTC não inicializado, mostra apenas uptime
        unsigned long currentMillis = millis();
        unsigned long seconds = currentMillis / 1000;
        unsigned long minutes = seconds / 60;
        unsigned long hours = minutes / 60;
        unsigned long days = hours / 24;
        
        unsigned long h = hours % 24;
        unsigned long m = minutes % 60;
        unsigned long s = seconds % 60;
        
        snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu:%02lu", h, m, s);
    }
    
    // Uptime em formato legível
    unsigned long currentMillis = millis();
    unsigned long seconds = currentMillis / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    unsigned long days = hours / 24;
    
    char uptimeStr[64];
    if (days > 0) {
        snprintf(uptimeStr, sizeof(uptimeStr), "%lud %02luh %02lum %02lus", days, hours % 24, minutes % 60, seconds % 60);
    } else if (hours > 0) {
        snprintf(uptimeStr, sizeof(uptimeStr), "%02luh %02lum %02lus", hours % 24, minutes % 60, seconds % 60);
    } else if (minutes > 0) {
        snprintf(uptimeStr, sizeof(uptimeStr), "%02lum %02lus", minutes % 60, seconds % 60);
    } else {
        snprintf(uptimeStr, sizeof(uptimeStr), "%02lus", seconds % 60);
    }
    
    doc["time"] = timeStr;
    doc["date"] = dateStr;
    doc["uptime"] = uptimeStr;
    doc["uptimeSeconds"] = seconds;
    doc["enabled"] = config.rtc.enabled;
    doc["timezone"] = config.rtc.timezone;
    doc["epochTime"] = currentEpoch;
    doc["initialized"] = (currentEpoch > 0);
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleSetTime(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
    if (data && len > 0) {
        String body = String((char*)data);
        
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, body);
        
        if (error) {
            request->send(400, "application/json", "{\"error\":\"JSON inválido\"}");
            return;
        }
        
        // Pode receber epochTime diretamente ou data/hora para converter
        uint32_t epochTime = 0;
        
        if (doc.containsKey("epochTime")) {
            epochTime = doc["epochTime"] | 0;
        } else if (doc.containsKey("date") && doc.containsKey("time")) {
            // Converte data/hora para epoch time
            String dateStr = doc["date"] | "";
            String timeStr = doc["time"] | "";
            
            if (dateStr.length() > 0 && timeStr.length() > 0) {
                // Formato: YYYY-MM-DD e HH:MM:SS
                int year, month, day, hour, min, sec;
                if (sscanf(dateStr.c_str(), "%d-%d-%d", &year, &month, &day) == 3 &&
                    sscanf(timeStr.c_str(), "%d:%d:%d", &hour, &min, &sec) == 3) {
                    
                    struct tm timeinfo;
                    timeinfo.tm_year = year - 1900;
                    timeinfo.tm_mon = month - 1;
                    timeinfo.tm_mday = day;
                    timeinfo.tm_hour = hour;
                    timeinfo.tm_min = min;
                    timeinfo.tm_sec = sec;
                    timeinfo.tm_isdst = 0;
                    
                    // Converte para epoch time (UTC)
                    epochTime = mktime(&timeinfo);
                    // Ajusta para UTC (remove fuso horário)
                    epochTime -= (config.rtc.timezone * 3600);
                }
            }
        }
        
        if (epochTime > 0) {
            config.rtc.epochTime = epochTime;
            config.rtc.bootTime = millis();
            
            // Salva na configuração
            (void)saveConfig(); // Ignora retorno neste contexto
            
            rtcInitialized = true;
            
            Serial.print("Data/hora configurada: ");
            Serial.println(epochTime);
            
            char dateStr[11];
            char timeStr[9];
            formatDateTime(epochTime, dateStr, timeStr, config.rtc.timezone);
            
            // Log no console web
            String logMsg = "[RTC] Data/hora configurada manualmente: " + String(dateStr) + " " + String(timeStr) + "\r\n";
            consolePrint(logMsg);
            
            // Log de ação no console
            consolePrint("[Acao] Botao 'Definir Data/Hora Manual' clicado\r\n");
            
            request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Data/hora configurada com sucesso\"}");
        } else {
            request->send(400, "application/json", "{\"error\":\"Data/hora inválida\"}");
        }
    } else {
        request->send(400, "application/json", "{\"error\":\"Dados não fornecidos\"}");
    }
}

void handleSyncNTP(AsyncWebServerRequest *request) {
    bool success = syncNTP();
    
    if (success) {
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"NTP sincronizado com sucesso\"}");
    } else {
        request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Falha ao sincronizar NTP\"}");
    }
}

void handleWiFiScan(AsyncWebServerRequest *request) {
    Serial.println("Iniciando scan de redes WiFi...");
    
    DynamicJsonDocument doc(4096);
    JsonArray networks = doc.createNestedArray("networks");
    
    try {
        // Salva o modo WiFi atual
        WiFiMode_t currentMode = WiFi.getMode();
        
        // Para fazer scan, precisa estar em modo AP_STA ou STA
        // Se estiver apenas em AP, muda temporariamente para AP_STA
        if (currentMode == WIFI_AP) {
            Serial.println("Mudando temporariamente para modo AP_STA para fazer scan...");
            WiFi.mode(WIFI_AP_STA);
            delay(200);  // Aguarda estabilização
            
            // Reconfigura o AP
            const char* apSSID = (strlen(config.wifi.apSSID) > 0) ? config.wifi.apSSID : AP_SSID;
            const char* apPassword = (strlen(config.wifi.apPassword) > 0) ? config.wifi.apPassword : AP_PASSWORD;
            WiFi.softAP(apSSID, apPassword);
            delay(200);
        }
        
        // Limpa scans anteriores
        WiFi.scanDelete();
        delay(100);
        
        // Inicia scan (modo assíncrono)
        Serial.println("Escaneando redes...");
        int scanResult = WiFi.scanNetworks(true, false);  // async=true, show_hidden=false
        
        if (scanResult == WIFI_SCAN_FAILED) {
            Serial.println("Erro ao iniciar scan WiFi");
            doc["status"] = "error";
            doc["message"] = "Falha ao iniciar scan WiFi";
            String response;
            serializeJson(doc, response);
            request->send(500, "application/json", response);
            return;
        }
        
        // Aguarda o scan completar (timeout de 15 segundos)
        unsigned long startTime = millis();
        int n = -1;
        while (n < 0 && (millis() - startTime) < 15000) {
            delay(200);
            n = WiFi.scanComplete();
        }
        
        if (n < 0) {
        Serial.println("Erro ao escanear redes WiFi");
        doc["status"] = "error";
        doc["message"] = "Falha ao escanear redes WiFi";
    } else if (n == 0) {
        Serial.println("Nenhuma rede encontrada");
        doc["status"] = "no_networks";
    } else {
        Serial.print(n);
        Serial.println(" redes encontradas");
        
        // Adiciona todas as redes encontradas
        for (int i = 0; i < n; ++i) {
            JsonObject network = networks.createNestedObject();
            network["ssid"] = WiFi.SSID(i);
            network["rssi"] = WiFi.RSSI(i);
            network["encryption"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "open" : "encrypted";
            
            // Calcula qualidade do sinal em porcentagem (0-100%)
            // RSSI típico: -100 (muito fraco) a -30 (muito forte)
            int quality = 2 * (WiFi.RSSI(i) + 100);
            if (quality > 100) quality = 100;
            if (quality < 0) quality = 0;
            network["quality"] = quality;
            
            // Descrição da qualidade
            String qualityDesc;
            if (quality >= 80) {
                qualityDesc = "Excelente";
            } else if (quality >= 60) {
                qualityDesc = "Boa";
            } else if (quality >= 40) {
                qualityDesc = "Regular";
            } else if (quality >= 20) {
                qualityDesc = "Fraca";
            } else {
                qualityDesc = "Muito Fraca";
            }
            network["qualityDesc"] = qualityDesc;
            
            // Canal
            network["channel"] = WiFi.channel(i);
        }
        
            doc["status"] = "success";
            doc["count"] = n;
        }
        
        // Restaura o modo WiFi original se foi alterado
        if (currentMode == WIFI_AP) {
            Serial.println("Restaurando modo AP...");
            WiFi.mode(WIFI_AP);
            delay(200);
            
            // Reconfigura o AP
            const char* apSSID = (strlen(config.wifi.apSSID) > 0) ? config.wifi.apSSID : AP_SSID;
            const char* apPassword = (strlen(config.wifi.apPassword) > 0) ? config.wifi.apPassword : AP_PASSWORD;
            WiFi.softAP(apSSID, apPassword);
        }
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
        
        // Limpa resultados do scan
        WiFi.scanDelete();
        
    } catch (...) {
        Serial.println("Erro inesperado no scan WiFi");
        doc["status"] = "error";
        doc["message"] = "Erro interno ao escanear redes";
        String response;
        serializeJson(doc, response);
        request->send(500, "application/json", response);
    }
}

void handleGetVariables(AsyncWebServerRequest *request) {
    Serial.println("GET /api/calc/variables - Obtendo variaveis disponiveis");
    
    DynamicJsonDocument doc(8192);
    
    // Cria estrutura bidimensional d[device][register]
    JsonArray devicesArray = doc.createNestedArray("devices");
    
    // Processa todos os dispositivos e registros
    for (int i = 0; i < config.deviceCount && i < MAX_DEVICES; i++) {
        JsonObject deviceObj = devicesArray.createNestedObject();
        deviceObj["deviceName"] = String(config.devices[i].deviceName);
        deviceObj["slaveAddress"] = config.devices[i].slaveAddress;
        
        JsonArray registersArray = deviceObj.createNestedArray("registers");
        
        for (int j = 0; j < config.devices[i].registerCount && j < MAX_REGISTERS_PER_DEVICE; j++) {
            JsonObject reg = registersArray.createNestedObject();
            
            reg["valueRaw"] = config.devices[i].registers[j].value;  // Valor raw do Modbus
            
            // Calcula valor processado (com gain e offset)
            float rawValue = (float)config.devices[i].registers[j].value;
            float processedValue = (rawValue * config.devices[i].registers[j].gain) + config.devices[i].registers[j].offset;
            reg["value"] = processedValue;  // Valor processado usado nas expressões
            
            reg["gain"] = config.devices[i].registers[j].gain;
            reg["offset"] = config.devices[i].registers[j].offset;
            reg["address"] = config.devices[i].registers[j].address;
            reg["enabled"] = config.devices[i].enabled;
            reg["isOutput"] = config.devices[i].registers[j].isOutput;
            reg["readOnly"] = config.devices[i].registers[j].readOnly;
            
            // Nome da variável (sempre inclui, mesmo se vazio)
            reg["variableName"] = String(config.devices[i].registers[j].variableName);
        }
    }
    
    // Adiciona informações sobre a estrutura
    doc["structure"] = "d[deviceIndex][registerIndex]";
    doc["deviceCount"] = config.deviceCount;
    
    String response;
    serializeJson(doc, response);
    
    Serial.print("Enviando resposta com ");
    Serial.print(config.deviceCount);
    Serial.println(" dispositivos");
    Serial.print("Tamanho da resposta: ");
    Serial.print(response.length());
    Serial.println(" bytes");
    
    request->send(200, "application/json", response);
}

void handleExportConfig(AsyncWebServerRequest *request) {
    // Retorna a configuração completa como JSON
    DynamicJsonDocument doc(24576);
    doc["baudRate"] = config.baudRate;
    doc["deviceCount"] = config.deviceCount;
    
    // Adiciona configuração MQTT
    JsonObject mqttObj = doc.createNestedObject("mqtt");
    mqttObj["enabled"] = config.mqtt.enabled;
    mqttObj["server"] = config.mqtt.server;
    mqttObj["port"] = config.mqtt.port;
    mqttObj["user"] = config.mqtt.user;
    mqttObj["password"] = config.mqtt.password;
    mqttObj["topic"] = config.mqtt.topic;
    mqttObj["interval"] = config.mqtt.interval;
    
    // Adiciona configuração WiFi
    JsonObject wifiObj = doc.createNestedObject("wifi");
    wifiObj["mode"] = String(config.wifi.mode);
    wifiObj["apSSID"] = String(config.wifi.apSSID);
    wifiObj["apPassword"] = String(config.wifi.apPassword);
    wifiObj["staSSID"] = String(config.wifi.staSSID);
    wifiObj["staPassword"] = String(config.wifi.staPassword);
    
    // Adiciona configuração RTC
    JsonObject rtcObj = doc.createNestedObject("rtc");
    rtcObj["enabled"] = config.rtc.enabled;
    rtcObj["timezone"] = config.rtc.timezone;
    rtcObj["ntpServer"] = config.rtc.ntpServer;
    rtcObj["ntpEnabled"] = config.rtc.ntpEnabled;
    rtcObj["epochTime"] = config.rtc.epochTime;
    
    // Adiciona código de cálculo
    doc["calculationCode"] = String(config.calculationCode);
    
    JsonArray devicesArray = doc.createNestedArray("devices");
    
    for (int i = 0; i < config.deviceCount; i++) {
        JsonObject deviceObj = devicesArray.createNestedObject();
        deviceObj["slaveAddress"] = config.devices[i].slaveAddress;
        deviceObj["enabled"] = config.devices[i].enabled;
        deviceObj["deviceName"] = String(config.devices[i].deviceName);
        
        JsonArray registersArray = deviceObj.createNestedArray("registers");
        
        // Salva todos os registros
        for (int j = 0; j < config.devices[i].registerCount; j++) {
            JsonObject regObj = registersArray.createNestedObject();
            regObj["address"] = config.devices[i].registers[j].address;
            regObj["isInput"] = config.devices[i].registers[j].isInput;
            regObj["isOutput"] = config.devices[i].registers[j].isOutput;
            regObj["readOnly"] = config.devices[i].registers[j].readOnly;
            regObj["variableName"] = String(config.devices[i].registers[j].variableName);
            regObj["gain"] = config.devices[i].registers[j].gain;
            regObj["offset"] = config.devices[i].registers[j].offset;
            regObj["kalmanEnabled"] = config.devices[i].registers[j].kalmanEnabled;
            regObj["kalmanQ"] = config.devices[i].registers[j].kalmanQ;
            regObj["kalmanR"] = config.devices[i].registers[j].kalmanR;
        }
        
        // IMPORTANTE: registerCount baseado no tamanho real do array
        deviceObj["registerCount"] = registersArray.size();
    }
    
    String response;
    serializeJson(doc, response);
    
    // Envia como download
    AsyncWebServerResponse *responseObj = request->beginResponse(200, "application/json", response);
    responseObj->addHeader("Content-Disposition", "attachment; filename=config.json");
    request->send(responseObj);
}

void handleImportConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
    if (data && len > 0) {
        String body = String((char*)data);
        
        // Usa a mesma lógica de handleSaveConfig para processar o JSON
        DynamicJsonDocument doc(24576);
        DeserializationError error = deserializeJson(doc, body);
        
        if (error) {
            request->send(400, "application/json", "{\"error\":\"JSON inválido\"}");
            return;
        }
        
        // Processa a configuração (mesma lógica de handleSaveConfig)
        uint32_t newBaudRate = doc["baudRate"] | MODBUS_SERIAL_BAUD;
        config.baudRate = newBaudRate;
        
        if (newBaudRate != currentBaudRate) {
            setupModbus(newBaudRate);
        }
        
        // Atualiza configuração MQTT
        if (doc.containsKey("mqtt")) {
            JsonObject mqttObj = doc["mqtt"];
            config.mqtt.enabled = mqttObj["enabled"] | false;
            strncpy(config.mqtt.server, mqttObj["server"] | "", sizeof(config.mqtt.server) - 1);
            config.mqtt.port = mqttObj["port"] | 1883;
            strncpy(config.mqtt.user, mqttObj["user"] | "", sizeof(config.mqtt.user) - 1);
            strncpy(config.mqtt.password, mqttObj["password"] | "", sizeof(config.mqtt.password) - 1);
            strncpy(config.mqtt.topic, mqttObj["topic"] | "esp32/modbus", sizeof(config.mqtt.topic) - 1);
            config.mqtt.interval = mqttObj["interval"] | 60;
        }
        
        // Atualiza configuração WiFi
        if (doc.containsKey("wifi")) {
            JsonObject wifiObj = doc["wifi"];
            const char* modeStr = wifiObj["mode"] | "ap";
            strncpy(config.wifi.mode, modeStr, sizeof(config.wifi.mode) - 1);
            config.wifi.mode[sizeof(config.wifi.mode) - 1] = '\0';
            
            const char* apSSIDStr = wifiObj["apSSID"] | AP_SSID;
            strncpy(config.wifi.apSSID, apSSIDStr, sizeof(config.wifi.apSSID) - 1);
            config.wifi.apSSID[sizeof(config.wifi.apSSID) - 1] = '\0';
            
            const char* apPasswordStr = wifiObj["apPassword"] | AP_PASSWORD;
            strncpy(config.wifi.apPassword, apPasswordStr, sizeof(config.wifi.apPassword) - 1);
            config.wifi.apPassword[sizeof(config.wifi.apPassword) - 1] = '\0';
            
            const char* staSSIDStr = wifiObj["staSSID"] | "";
            strncpy(config.wifi.staSSID, staSSIDStr, sizeof(config.wifi.staSSID) - 1);
            config.wifi.staSSID[sizeof(config.wifi.staSSID) - 1] = '\0';
            
            const char* staPasswordStr = wifiObj["staPassword"] | "";
            strncpy(config.wifi.staPassword, staPasswordStr, sizeof(config.wifi.staPassword) - 1);
            config.wifi.staPassword[sizeof(config.wifi.staPassword) - 1] = '\0';
        }
        
        // Atualiza configuração RTC
        if (doc.containsKey("rtc")) {
            JsonObject rtcObj = doc["rtc"];
            config.rtc.enabled = rtcObj["enabled"] | false;
            config.rtc.timezone = rtcObj["timezone"] | -3;
            const char* ntpServerStr = rtcObj["ntpServer"] | "pool.ntp.org";
            strncpy(config.rtc.ntpServer, ntpServerStr, sizeof(config.rtc.ntpServer) - 1);
            config.rtc.ntpServer[sizeof(config.rtc.ntpServer) - 1] = '\0';
            config.rtc.ntpEnabled = rtcObj["ntpEnabled"] | true;
        }
        
        // Atualiza código de cálculo
        if (doc.containsKey("calculationCode")) {
            const char* code = doc["calculationCode"] | "";
            strncpy(config.calculationCode, code, sizeof(config.calculationCode) - 1);
            config.calculationCode[sizeof(config.calculationCode) - 1] = '\0';
        }
        
        config.deviceCount = doc["deviceCount"] | 0;
        if (config.deviceCount > MAX_DEVICES) {
            config.deviceCount = MAX_DEVICES;
        }
        
        // Verifica se há array de dispositivos
        if (!doc.containsKey("devices") || !doc["devices"].is<JsonArray>()) {
            request->send(400, "application/json", "{\"error\":\"Array de dispositivos não encontrado\"}");
            return;
        }
        
        JsonArray devicesArray = doc["devices"].as<JsonArray>();
        int deviceCountFromJson = devicesArray.size();
        
        for (int i = 0; i < config.deviceCount && i < MAX_DEVICES && i < deviceCountFromJson; i++) {
            JsonObject deviceObj = devicesArray[i];
            if (!deviceObj) continue;
            
            config.devices[i].slaveAddress = deviceObj["slaveAddress"] | 1;
            config.devices[i].enabled = deviceObj["enabled"] | true;
            
            // Carrega nome do dispositivo
            const char* devName = deviceObj["deviceName"] | "";
            strncpy(config.devices[i].deviceName, devName, sizeof(config.devices[i].deviceName) - 1);
            config.devices[i].deviceName[sizeof(config.devices[i].deviceName) - 1] = '\0';
            
            // Verifica se há array de registros
            if (!deviceObj.containsKey("registers") || !deviceObj["registers"].is<JsonArray>()) {
                Serial.print("AVISO: Array de registros nao encontrado para dispositivo ");
                Serial.println(i);
                config.devices[i].registerCount = 0;
                continue;
            }
            
            JsonArray registersArray = deviceObj["registers"].as<JsonArray>();
            int registerCountFromJson = registersArray.size();
            
            // IMPORTANTE: registerCount deve ser baseado no tamanho real do array de registros
            config.devices[i].registerCount = registerCountFromJson;
            
            // Limita ao máximo permitido
            if (config.devices[i].registerCount > MAX_REGISTERS_PER_DEVICE) {
                config.devices[i].registerCount = MAX_REGISTERS_PER_DEVICE;
            }
            
            for (int j = 0; j < config.devices[i].registerCount && j < registerCountFromJson && j < MAX_REGISTERS_PER_DEVICE; j++) {
                JsonObject regObj = registersArray[j];
                if (!regObj) continue;
                
                config.devices[i].registers[j].address = regObj["address"] | 0;
                config.devices[i].registers[j].isInput = regObj["isInput"] | true;
                config.devices[i].registers[j].isOutput = regObj["isOutput"] | false;
                config.devices[i].registers[j].readOnly = regObj["readOnly"] | false;
                
                // IMPORTANTE: Inicializa valor com 0 mesmo sem leitura do dispositivo
                // Isso garante que a variável existe e pode ser usada nas expressões
                config.devices[i].registers[j].value = 0;
                
                const char* varName = regObj["variableName"] | "";
                strncpy(config.devices[i].registers[j].variableName, varName, sizeof(config.devices[i].registers[j].variableName) - 1);
                config.devices[i].registers[j].variableName[sizeof(config.devices[i].registers[j].variableName) - 1] = '\0';
                
                // Carrega ganho e offset (valores padrão se não especificados)
                config.devices[i].registers[j].gain = regObj["gain"] | 1.0f;
                config.devices[i].registers[j].offset = regObj["offset"] | 0.0f;
                
                // Carrega kalmanEnabled (padrão: false)
                config.devices[i].registers[j].kalmanEnabled = regObj["kalmanEnabled"] | false;
                
                // Carrega parâmetros do filtro de Kalman (valores padrão se não especificados)
                config.devices[i].registers[j].kalmanQ = regObj["kalmanQ"] | 0.01f;
                config.devices[i].registers[j].kalmanR = regObj["kalmanR"] | 0.1f;
            }
        }
        
        // Salva a configuração importada
        saveConfig();
        
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Configuração importada com sucesso\"}");
    } else {
        request->send(400, "application/json", "{\"error\":\"Dados não fornecidos\"}");
    }
}

void handleTestCalculation(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
    if (data && len > 0) {
        String body = String((char*)data);
        
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, body);
        
        if (error) {
            request->send(400, "application/json", "{\"error\":\"JSON inválido\"}");
            return;
        }
        
        const char* expression = doc["expression"] | "";
        if (strlen(expression) == 0) {
            request->send(400, "application/json", "{\"error\":\"Expressão não fornecida\"}");
            return;
        }
        
        // Prepara estrutura DeviceValues com todos os valores dos dispositivos
        // Aplica gain e offset antes de atribuir
        DeviceValues deviceValues;
        deviceValues.deviceCount = config.deviceCount;
        deviceValues.registerCounts = new int[config.deviceCount];
        deviceValues.values = new double*[config.deviceCount];
        
        for (int i = 0; i < config.deviceCount; i++) {
            deviceValues.registerCounts[i] = config.devices[i].registerCount;
            deviceValues.values[i] = new double[config.devices[i].registerCount];
            
            for (int j = 0; j < config.devices[i].registerCount; j++) {
                // Aplica gain e offset: valor_processado = (valor_raw * gain) + offset
                float rawValue = (float)config.devices[i].registers[j].value;
                float processedValue = (rawValue * config.devices[i].registers[j].gain) + config.devices[i].registers[j].offset;
                deviceValues.values[i][j] = (double)processedValue;
            }
        }
        
        // Processa múltiplas linhas
        DynamicJsonDocument responseDoc(4096);
        JsonArray results = responseDoc.createNestedArray("results");
        String codeStr = String(expression);
        int lineNumber = 1;
        int startPos = 0;
        bool hasErrors = false;
        
        while (startPos < codeStr.length()) {
            // Encontra o final da linha
            int endPos = codeStr.indexOf('\n', startPos);
            if (endPos == -1) {
                endPos = codeStr.length();
            }
            
            // Extrai a linha
            String line = codeStr.substring(startPos, endPos);
            line.trim();
            startPos = endPos + 1;
            
            // Ignora linhas vazias ou comentários
            if (line.length() == 0 || line.charAt(0) == '#') {
                continue;
            }
            
            // Converte String para char*
            char lineBuffer[1024];
            strncpy(lineBuffer, line.c_str(), sizeof(lineBuffer) - 1);
            lineBuffer[sizeof(lineBuffer) - 1] = '\0';
            
            // Processa esta linha
            JsonObject lineResult = results.createNestedObject();
            lineResult["lineNumber"] = lineNumber;
            lineResult["expression"] = lineBuffer;
            
            AssignmentInfo assignmentInfo;
            char errorMsg[256] = "";
            bool parseSuccess = parseAssignment(lineBuffer, &assignmentInfo, errorMsg, sizeof(errorMsg));
            
            if (!parseSuccess) {
                lineResult["status"] = "error";
                lineResult["error"] = errorMsg;
                hasErrors = true;
                freeAssignmentInfo(&assignmentInfo);
                lineNumber++;
                continue;
            }
            
            // Processa expressão
            const char* expressionToProcess = assignmentInfo.hasAssignment ? assignmentInfo.expression : lineBuffer;
            char processedExpression[2048];
            bool success = substituteDeviceValues(expressionToProcess, &deviceValues, processedExpression, sizeof(processedExpression), errorMsg, sizeof(errorMsg));
            
            if (!success) {
                lineResult["status"] = "error";
                lineResult["error"] = errorMsg;
                hasErrors = true;
                freeAssignmentInfo(&assignmentInfo);
                lineNumber++;
                continue;
            }
            
            // Avalia expressão
            double result = 0.0;
            Variable emptyVars[1];
            int emptyVarCount = 0;
            bool evalSuccess = evaluateExpression(processedExpression, emptyVars, emptyVarCount, &result, errorMsg, sizeof(errorMsg));
            
            if (!evalSuccess) {
                lineResult["status"] = "error";
                lineResult["error"] = errorMsg;
                hasErrors = true;
                freeAssignmentInfo(&assignmentInfo);
                lineNumber++;
                continue;
            }
            
            // Sucesso
            lineResult["status"] = "ok";
            lineResult["result"] = result;
            lineResult["processedExpression"] = processedExpression;
            
            if (assignmentInfo.hasAssignment) {
                lineResult["hasAssignment"] = true;
                lineResult["targetDevice"] = assignmentInfo.targetDeviceIndex;
                lineResult["targetRegister"] = assignmentInfo.targetRegisterIndex;
                
                // Calcula valor raw
                if (assignmentInfo.targetDeviceIndex >= 0 && 
                    assignmentInfo.targetDeviceIndex < config.deviceCount &&
                    assignmentInfo.targetRegisterIndex >= 0 &&
                    assignmentInfo.targetRegisterIndex < config.devices[assignmentInfo.targetDeviceIndex].registerCount) {
                    
                    ModbusRegister* targetReg = &config.devices[assignmentInfo.targetDeviceIndex].registers[assignmentInfo.targetRegisterIndex];
                    if (targetReg->gain != 0.0f) {
                        float rawValue = (result - targetReg->offset) / targetReg->gain;
                        lineResult["rawValue"] = rawValue;
                    }
                }
            }
            
            freeAssignmentInfo(&assignmentInfo);
            lineNumber++;
        }
        
        // Limpa memória
        for (int i = 0; i < config.deviceCount; i++) {
            delete[] deviceValues.values[i];
        }
        delete[] deviceValues.values;
        delete[] deviceValues.registerCounts;
        
        // Resposta geral
        responseDoc["status"] = hasErrors ? "partial" : "ok";
        responseDoc["totalLines"] = lineNumber - 1;
        
        String response;
        serializeJson(responseDoc, response);
        request->send(200, "application/json", response);
    } else {
        request->send(400, "application/json", "{\"error\":\"Dados não fornecidos\"}");
    }
}

void handleWriteVariable(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
    if (!data || len == 0) {
        request->send(400, "application/json", "{\"error\":\"Dados não fornecidos\"}");
        return;
    }
    
    String body = String((char*)data);
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, body);
    
    if (error) {
        request->send(400, "application/json", "{\"error\":\"JSON inválido\"}");
        return;
    }
    
    int deviceIndex = doc["deviceIndex"] | -1;
    int registerIndex = doc["registerIndex"] | -1;
    float value = doc["value"] | 0.0f;
    
    // Valida índices
    if (deviceIndex < 0 || deviceIndex >= config.deviceCount) {
        request->send(400, "application/json", "{\"error\":\"Índice de dispositivo inválido\"}");
        return;
    }
    
    if (registerIndex < 0 || registerIndex >= config.devices[deviceIndex].registerCount) {
        request->send(400, "application/json", "{\"error\":\"Índice de registro inválido\"}");
        return;
    }
    
    // Verifica se a variável não é somente leitura
    if (config.devices[deviceIndex].registers[registerIndex].readOnly) {
        request->send(400, "application/json", "{\"error\":\"Variável é somente leitura\"}");
        return;
    }
    
    // Verifica se é um Holding Register (pode ser escrito)
    if (!config.devices[deviceIndex].registers[registerIndex].isInput) {
        request->send(400, "application/json", "{\"error\":\"Apenas Holding Registers podem ser escritos\"}");
        return;
    }
    
    // Calcula valor raw (transformação inversa: raw = (value - offset) / gain)
    float gain = config.devices[deviceIndex].registers[registerIndex].gain;
    float offset = config.devices[deviceIndex].registers[registerIndex].offset;
    
    if (gain == 0.0f) {
        request->send(400, "application/json", "{\"error\":\"Gain não pode ser zero\"}");
        return;
    }
    
    float rawValue = (value - offset) / gain;
    uint16_t rawValueInt = (uint16_t)round(rawValue);
    
    // Escreve no Modbus
    uint8_t slaveAddr = config.devices[deviceIndex].slaveAddress;
    uint16_t regAddr = config.devices[deviceIndex].registers[registerIndex].address;
    
    node.begin(slaveAddr, Serial2);
    uint8_t result = node.writeSingleRegister(regAddr, rawValueInt);
    
    if (result == node.ku8MBSuccess) {
        // Atualiza valor na configuração
        config.devices[deviceIndex].registers[registerIndex].value = rawValueInt;
        
        String logMsg = "[Modbus] Escrito Dev " + String(slaveAddr) + 
                       " Reg " + String(regAddr) + 
                       ": " + String(value, 2) + 
                       " (raw: " + String(rawValueInt) + ")\r\n";
        consolePrint(logMsg);
        
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Valor escrito com sucesso\"}");
    } else {
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
        
        String logMsg = "[Modbus ERRO] Escrita Dev " + String(slaveAddr) + 
                       " Reg " + String(regAddr) + 
                       ": " + errorDesc + "\r\n";
        consolePrint(logMsg);
        
        request->send(500, "application/json", "{\"error\":\"" + errorDesc + "\"}");
    }
}

