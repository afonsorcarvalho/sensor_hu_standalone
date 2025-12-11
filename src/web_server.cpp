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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

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
    
    // Rota para resetar configurações para valores padrão (deve vir antes de /api/config)
    server.on("/api/config/reset", HTTP_POST, [](AsyncWebServerRequest *request){
        if (request) {
            handleResetConfig(request);
        }
    });
    
    // Rota para exportar configurações (deve vir antes de /api/config)
    server.on("/api/config/export", HTTP_GET, [](AsyncWebServerRequest *request){
        handleExportConfig(request);
    });
    
    // Rota para importar configurações (deve vir antes de /api/config)
    // Usa handler de body para acumular todos os chunks antes de processar
    server.on("/api/config/import", HTTP_POST,
        [](AsyncWebServerRequest *request){
            // Handler de início - inicializa buffer se necessário
            request->_tempObject = new String();
        },
        NULL,  // onUpload - não usado
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
            // Handler de body - acumula chunks
            if (request->_tempObject) {
                String* bodyBuffer = (String*)request->_tempObject;
                // Adiciona chunk atual ao buffer
                for (size_t i = 0; i < len; i++) {
                    *bodyBuffer += (char)data[i];
                }
                
                // Se recebeu todos os chunks (este é o último chunk), processa
                // Verifica se recebemos todos os bytes esperados
                if (index + len >= total && bodyBuffer->length() >= total) {
                    handleImportConfig(request, (uint8_t*)bodyBuffer->c_str(), bodyBuffer->length());
                    delete bodyBuffer;
                    request->_tempObject = nullptr;
                }
            }
        });
    
    // Rota para obter configuração atual (JSON)
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request){
        handleGetConfig(request);
    });
    
    // Rota para salvar nova configuração (POST JSON)
    // Usa handler de body para acumular todos os chunks antes de processar
    server.on("/api/config", HTTP_POST, 
        [](AsyncWebServerRequest *request){
            // Handler de início - inicializa buffer se necessário
            Serial.println("[Config] POST /api/config recebido");
            
            // Verifica se há Content-Length
            if (request->hasHeader("Content-Length")) {
                const AsyncWebHeader* header = request->getHeader("Content-Length");
                if (header) {
                    int contentLength = atoi(header->value().c_str());
                    Serial.print("[Config] Content-Length: ");
                    Serial.println(contentLength);
                    if (contentLength > 0) {
                        request->_tempObject = new String();
                        // Reserva espaço para evitar realocações
                        ((String*)request->_tempObject)->reserve(contentLength + 1);
                        Serial.println("[Config] Buffer inicializado");
                    } else {
                        // Body vazio - retorna erro imediatamente
                        Serial.println("[Config] Body vazio - retornando erro");
                        request->send(400, "application/json", "{\"error\":\"Body vazio\"}");
                        return;
                    }
                } else {
                    // Header não encontrado - inicializa buffer de qualquer forma
                    Serial.println("[Config] Header Content-Length não encontrado - inicializando buffer");
                    request->_tempObject = new String();
                }
            } else {
                // Sem Content-Length - pode ser que não haja body
                // Inicializa buffer de qualquer forma
                Serial.println("[Config] Sem Content-Length - inicializando buffer");
                request->_tempObject = new String();
            }
        },
        NULL,  // onUpload - não usado
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
            // Handler de body - acumula chunks
            Serial.print("[Config] Body handler chamado: len=");
            Serial.print(len);
            Serial.print(", index=");
            Serial.print(index);
            Serial.print(", total=");
            Serial.println(total);
            
            // Se total é 0, não há body - retorna erro
            if (total == 0) {
                Serial.println("[Config] ERRO: total é 0 - sem body");
                request->send(400, "application/json", "{\"error\":\"Body vazio\"}");
                return;
            }
            
            // IMPORTANTE: O handler de body pode ser chamado ANTES do handler de request
            // Então precisamos inicializar o buffer aqui se ele não existir
            if (!request->_tempObject) {
                Serial.println("[Config] Buffer não inicializado - inicializando no handler de body");
                request->_tempObject = new String();
                // Reserva espaço baseado no total esperado
                ((String*)request->_tempObject)->reserve(total + 1);
            }
            
            if (request->_tempObject) {
                String* bodyBuffer = (String*)request->_tempObject;
                // Adiciona chunk atual ao buffer
                for (size_t i = 0; i < len; i++) {
                    *bodyBuffer += (char)data[i];
                }
                
                // Debug: mostra progresso (apenas a cada 500 bytes ou no último chunk)
                if (bodyBuffer->length() % 500 < 100 || index + len >= total) {
                    Serial.print("[Config] Chunk: index=");
                    Serial.print(index);
                    Serial.print(", len=");
                    Serial.print(len);
                    Serial.print(", total=");
                    Serial.print(total);
                    Serial.print(", buffer=");
                    Serial.println(bodyBuffer->length());
                }
                
                // Verifica se recebeu todos os chunks
                // O AsyncWebServer pode chamar múltiplas vezes, então verificamos se:
                // 1. Este é o último chunk (index + len >= total)
                // 2. O buffer tem pelo menos o tamanho total esperado
                // 3. Ainda não processamos (request->_tempObject não é nullptr)
                if (index + len >= total && bodyBuffer->length() >= total && request->_tempObject != nullptr) {
                    Serial.print("[Config] Todos os chunks recebidos. Processando JSON de ");
                    Serial.print(bodyBuffer->length());
                    Serial.println(" bytes");
                    
                    // Processa a configuração (handleSaveConfig envia a resposta)
                    handleSaveConfig(request, (uint8_t*)bodyBuffer->c_str(), bodyBuffer->length());
                    
                    // Limpa o buffer
                    delete bodyBuffer;
                    request->_tempObject = nullptr;
                }
                // Se ainda estamos recebendo chunks, apenas continua acumulando
                // Não precisa fazer nada aqui - o AsyncWebServer continuará chamando este handler
            } else {
                // request->_tempObject é nullptr mesmo após tentar inicializar - erro crítico
                Serial.println("[Config] ERRO CRÍTICO: Não foi possível inicializar buffer");
                request->send(500, "application/json", "{\"error\":\"Erro interno: falha ao inicializar buffer\"}");
            }
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
    Serial.println("[Config] handleSaveConfig chamado");
    Serial.print("[Config] Dados recebidos: len=");
    Serial.print(len);
    Serial.println(" bytes");
    
    if (!data || len == 0) {
        Serial.println("[Config] ERRO: Dados vazios ou nulos");
        request->send(400, "application/json", "{\"error\":\"Dados não fornecidos\"}");
        return;
    }
    
    // Cria string do body (garante terminação nula)
    String body;
    body.reserve(len + 1);  // Reserva espaço para evitar realocações
    for (size_t i = 0; i < len; i++) {
        body += (char)data[i];
    }
    
    Serial.print("[Config] Body criado: ");
    Serial.print(body.length());
    Serial.println(" caracteres");
    
    // Verifica se o JSON parece válido (começa com { e termina com })
    if (body.length() < 2 || body.charAt(0) != '{' || body.charAt(body.length() - 1) != '}') {
        Serial.println("[Config] ERRO: JSON não parece válido (não começa/termina com {})");
        Serial.print("[Config] Primeiros 100 caracteres: ");
        Serial.println(body.substring(0, 100));
        request->send(400, "application/json", "{\"error\":\"JSON inválido - formato incorreto\"}");
        return;
    }
    
    // Tamanho aumentado para suportar muitos dispositivos e registros
    DynamicJsonDocument doc(24576);  // 24KB para garantir espaço suficiente
    DeserializationError error = deserializeJson(doc, body);
    
    if (error) {
        Serial.print("[Config] ERRO ao deserializar JSON: ");
        Serial.println(error.c_str());
        Serial.print("[Config] Tamanho do body: ");
        Serial.print(body.length());
        Serial.println(" bytes");
        Serial.print("[Config] Primeiros 200 caracteres: ");
        Serial.println(body.substring(0, 200));
        Serial.print("[Config] Últimos 200 caracteres: ");
        int start = (body.length() > 200) ? body.length() - 200 : 0;
        Serial.println(body.substring(start));
        
        String errorMsg = "{\"error\":\"JSON inválido: ";
        errorMsg += error.c_str();
        errorMsg += "\"}";
        request->send(400, "application/json", errorMsg);
        return;
    }
    
    Serial.println("[Config] JSON deserializado com sucesso");
    
    // Log para debug
    Serial.print("[Config] JSON recebido com sucesso. Tamanho: ");
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
        
        // Processa modo WiFi com validação e normalização para minúsculas
        if (wifiObj.containsKey("mode") && wifiObj["mode"].is<const char*>()) {
            const char* modeStr = wifiObj["mode"].as<const char*>();
            if (modeStr && strlen(modeStr) > 0) {
                // Normaliza para minúsculas para evitar problemas de comparação
                String modeStrLower = String(modeStr);
                modeStrLower.toLowerCase();
                strncpy(config.wifi.mode, modeStrLower.c_str(), sizeof(config.wifi.mode) - 1);
                config.wifi.mode[sizeof(config.wifi.mode) - 1] = '\0';
            } else {
                strcpy(config.wifi.mode, "ap");
            }
        } else {
            strcpy(config.wifi.mode, "ap");
        }
        
        // Processa AP SSID com validação
        if (wifiObj.containsKey("apSSID") && wifiObj["apSSID"].is<const char*>()) {
            const char* apSSIDStr = wifiObj["apSSID"].as<const char*>();
            if (apSSIDStr) {
                strncpy(config.wifi.apSSID, apSSIDStr, sizeof(config.wifi.apSSID) - 1);
                config.wifi.apSSID[sizeof(config.wifi.apSSID) - 1] = '\0';
            } else {
                strcpy(config.wifi.apSSID, AP_SSID);
            }
        } else {
            strcpy(config.wifi.apSSID, AP_SSID);
        }
        
        // Processa AP Password com validação
        if (wifiObj.containsKey("apPassword") && wifiObj["apPassword"].is<const char*>()) {
            const char* apPasswordStr = wifiObj["apPassword"].as<const char*>();
            if (apPasswordStr) {
                strncpy(config.wifi.apPassword, apPasswordStr, sizeof(config.wifi.apPassword) - 1);
                config.wifi.apPassword[sizeof(config.wifi.apPassword) - 1] = '\0';
            } else {
                strcpy(config.wifi.apPassword, AP_PASSWORD);
            }
        } else {
            strcpy(config.wifi.apPassword, AP_PASSWORD);
        }
        
        // Processa STA SSID com validação
        if (wifiObj.containsKey("staSSID") && wifiObj["staSSID"].is<const char*>()) {
            const char* staSSIDStr = wifiObj["staSSID"].as<const char*>();
            if (staSSIDStr) {
                strncpy(config.wifi.staSSID, staSSIDStr, sizeof(config.wifi.staSSID) - 1);
                config.wifi.staSSID[sizeof(config.wifi.staSSID) - 1] = '\0';
            } else {
                config.wifi.staSSID[0] = '\0';
            }
        } else {
            config.wifi.staSSID[0] = '\0';
        }
        
        // Processa STA Password com validação
        if (wifiObj.containsKey("staPassword") && wifiObj["staPassword"].is<const char*>()) {
            const char* staPasswordStr = wifiObj["staPassword"].as<const char*>();
            if (staPasswordStr) {
                strncpy(config.wifi.staPassword, staPasswordStr, sizeof(config.wifi.staPassword) - 1);
                config.wifi.staPassword[sizeof(config.wifi.staPassword) - 1] = '\0';
            } else {
                config.wifi.staPassword[0] = '\0';
            }
        } else {
            config.wifi.staPassword[0] = '\0';
        }
        
        Serial.print("[Config] WiFi configurado - Mode: '");
        Serial.print(config.wifi.mode);
        Serial.print("', AP SSID: '");
        Serial.print(config.wifi.apSSID);
        Serial.print("', STA SSID: '");
        Serial.print(config.wifi.staSSID);
        Serial.print("', STA Password length: ");
        Serial.println(strlen(config.wifi.staPassword));
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
        Serial.println("[Config] ERRO: Array de dispositivos nao encontrado no JSON recebido");
        request->send(400, "application/json", "{\"error\":\"Array de dispositivos não encontrado\"}");
        return;
    }
    
    JsonArray devicesArray = doc["devices"].as<JsonArray>();
    int deviceCountFromJson = devicesArray.size();
    
    Serial.print("[Config] Dispositivos recebidos no JSON: ");
    Serial.println(deviceCountFromJson);
    
    for (int i = 0; i < config.deviceCount && i < MAX_DEVICES && i < deviceCountFromJson; i++) {
        JsonObject deviceObj = devicesArray[i];
        if (!deviceObj) {
            Serial.print("[Config] AVISO: Dispositivo ");
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
            Serial.print("[Config] AVISO: Array de registros nao encontrado para dispositivo ");
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
        
        Serial.print("[Config] Processando dispositivo ");
        Serial.print(i);
        Serial.print(": ");
        Serial.print(config.devices[i].registerCount);
        Serial.println(" registros");
        
        for (int j = 0; j < config.devices[i].registerCount && j < registerCountFromJson; j++) {
            JsonObject regObj = registersArray[j];
            if (!regObj) {
                Serial.print("[Config] AVISO: Registro ");
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
            
            // Carrega ganho e offset (valores padrão se não especificados) com validação
            if (regObj.containsKey("gain") && regObj["gain"].is<float>()) {
                config.devices[i].registers[j].gain = regObj["gain"].as<float>();
            } else {
                config.devices[i].registers[j].gain = 1.0f;
            }
            
            if (regObj.containsKey("offset") && regObj["offset"].is<float>()) {
                config.devices[i].registers[j].offset = regObj["offset"].as<float>();
            } else {
                config.devices[i].registers[j].offset = 0.0f;
            }
            
            // Carrega kalmanEnabled (padrão: false) com validação
            if (regObj.containsKey("kalmanEnabled") && regObj["kalmanEnabled"].is<bool>()) {
                config.devices[i].registers[j].kalmanEnabled = regObj["kalmanEnabled"].as<bool>();
            } else {
                config.devices[i].registers[j].kalmanEnabled = false;
            }
            
            // Carrega parâmetros do filtro de Kalman (valores padrão se não especificados) com validação
            if (regObj.containsKey("kalmanQ") && regObj["kalmanQ"].is<float>()) {
                float kalmanQ = regObj["kalmanQ"].as<float>();
                if (isnan(kalmanQ) || isinf(kalmanQ) || kalmanQ <= 0.0f) {
                    config.devices[i].registers[j].kalmanQ = 0.01f;
                } else {
                    config.devices[i].registers[j].kalmanQ = kalmanQ;
                }
            } else {
                config.devices[i].registers[j].kalmanQ = 0.01f;
            }
            
            if (regObj.containsKey("kalmanR") && regObj["kalmanR"].is<float>()) {
                float kalmanR = regObj["kalmanR"].as<float>();
                if (isnan(kalmanR) || isinf(kalmanR) || kalmanR <= 0.0f) {
                    config.devices[i].registers[j].kalmanR = 0.1f;
                } else {
                    config.devices[i].registers[j].kalmanR = kalmanR;
                }
            } else {
                config.devices[i].registers[j].kalmanR = 0.1f;
            }
            
            Serial.print("[Config]   Registro ");
            Serial.print(j);
            Serial.print(": endereco=");
            Serial.print(config.devices[i].registers[j].address);
            Serial.print(", variavel=");
            Serial.print(config.devices[i].registers[j].variableName);
            Serial.print(", kalman=");
            Serial.print(config.devices[i].registers[j].kalmanEnabled ? "sim" : "nao");
            if (config.devices[i].registers[j].kalmanEnabled) {
                Serial.print(", Q=");
                Serial.print(config.devices[i].registers[j].kalmanQ);
                Serial.print(", R=");
                Serial.print(config.devices[i].registers[j].kalmanR);
            }
            Serial.println();
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
    Serial.println("[Config] Salvando configuração na memória não volátil...");
    consolePrint("[Acao] Botao 'Salvar Todas as Configuracoes' clicado\r\n");
    
    bool saveSuccess = saveConfig();
    
    if (saveSuccess) {
        Serial.println("[Config] Configuração salva com sucesso!");
        Serial.print("[Config] WiFi Mode salvo: '");
        Serial.print(config.wifi.mode);
        Serial.print("', STA SSID: '");
        Serial.print(config.wifi.staSSID);
        Serial.print("', STA Password length: ");
        Serial.println(strlen(config.wifi.staPassword));
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Configuração salva com sucesso\"}");
    } else {
        Serial.println("[Config] ERRO: Falha ao salvar configuração na memória");
        request->send(500, "application/json", "{\"error\":\"Erro ao salvar configuração na memória\"}");
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
    // Salva a configuração antes de reiniciar (garante que está persistida)
    Serial.println("Salvando configuração antes do reboot...");
    bool saved = saveConfig();
    if (saved) {
        Serial.println("Configuração salva com sucesso!");
    } else {
        Serial.println("AVISO: Falha ao salvar configuração antes do reboot");
    }
    
    // Envia resposta imediata para o cliente
    request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Configuracao salva! Reiniciando em 10 segundos...\"}");
    
    Serial.println("Reboot solicitado via web interface");
    Serial.println("Configuração salva! Reiniciando em 10 segundos...");
    
    // Aguarda 10 segundos antes de reiniciar
    // Isso dá tempo para o cliente receber a resposta e mostrar a mensagem
    for (int i = 10; i > 0; i--) {
        Serial.print("Reiniciando em ");
        Serial.print(i);
        Serial.println(" segundos...");
        delay(1000);
    }
    
    Serial.println("Reiniciando agora...");
    Serial.flush();
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
    Serial.flush();
    
    DynamicJsonDocument doc(4096);
    JsonArray networks = doc.createNestedArray("networks");
    
    // Salva o estado WiFi atual para restaurar depois
    WiFiMode_t originalMode = WiFi.getMode();
    bool wasConnectedSTA = false;
    String originalSSID = "";
    
    // IMPORTANTE: Verifica se está conectado como STA (não AP)
    // Se estiver em modo AP, o cliente está conectado ao nosso AP e NÃO devemos desconectar
    if (originalMode == WIFI_STA || originalMode == WIFI_AP_STA) {
        if (WiFi.status() == WL_CONNECTED) {
            wasConnectedSTA = true;
            originalSSID = WiFi.SSID();
        }
    }
    
    try {
        // Para fazer scan, precisa estar em modo AP_STA ou STA
        // Se estiver apenas em AP, muda temporariamente para AP_STA
        // IMPORTANTE: Ao mudar de AP para AP_STA, o AP continua funcionando
        // então o cliente conectado ao AP NÃO perde conexão
        if (originalMode == WIFI_AP) {
            Serial.println("Mudando de AP para AP_STA para fazer scan (AP continua ativo)...");
            Serial.flush();
            
            // Salva configuração do AP antes de mudar
            const char* apSSID = (strlen(config.wifi.apSSID) > 0) ? config.wifi.apSSID : AP_SSID;
            const char* apPassword = (strlen(config.wifi.apPassword) > 0) ? config.wifi.apPassword : AP_PASSWORD;
            
            // Muda para AP_STA - o AP continua funcionando, cliente não perde conexão
            WiFi.mode(WIFI_AP_STA);
            yield();
            delay(100);  // Aguarda estabilização mínima
            yield();
            
            // Garante que o AP continua configurado (pode não ser necessário, mas é seguro)
            WiFi.softAP(apSSID, apPassword);
            yield();
            delay(50);
            yield();
        } else if (originalMode == WIFI_STA) {
            // Se está em modo STA e conectado a outra rede, desconecta temporariamente
            // para fazer o scan mais rápido (mas isso pode desconectar o cliente se ele
            // estiver usando o ESP32 como gateway - então vamos evitar isso)
            // Na verdade, não precisamos desconectar - o scan funciona mesmo conectado
            Serial.println("Modo STA - scan será feito sem desconectar");
            Serial.flush();
        } else if (originalMode == WIFI_AP_STA) {
            // Já está no modo correto, apenas garante que AP está ativo
            Serial.println("Já está em modo AP_STA - scan será feito");
            Serial.flush();
        }
        
        // Se estava conectado como STA a outra rede, podemos desconectar temporariamente
        // para fazer scan mais rápido, mas isso é opcional
        // Vamos deixar conectado para não perder conexão do cliente
        
        // Limpa scans anteriores
        WiFi.scanDelete();
        yield();
        delay(50);
        yield();
        
        // CRÍTICO: Remove a task atual do watchdog temporariamente durante o scan
        // Isso evita que o ESP32 reinicie durante o scan WiFi
        // Nota: NULL remove a task atual (a task que está executando este código)
        Serial.println("Removendo task do watchdog temporariamente para scan WiFi...");
        Serial.flush();
        esp_err_t wdt_result = esp_task_wdt_delete(NULL);
        if (wdt_result != ESP_OK && wdt_result != ESP_ERR_NOT_FOUND) {
            Serial.print("AVISO: Não foi possível remover task do watchdog: ");
            Serial.println(wdt_result);
            Serial.print("Tentando continuar mesmo assim...");
            Serial.flush();
        } else {
            Serial.println("Task removida do watchdog com sucesso");
            Serial.flush();
        }
        
        // Inicia scan (modo assíncrono, não mostra redes ocultas)
        Serial.println("Iniciando scan assíncrono de redes...");
        Serial.flush();
        int scanResult = WiFi.scanNetworks(true, false);  // async=true, show_hidden=false
        
        if (scanResult == WIFI_SCAN_FAILED) {
            Serial.println("Erro ao iniciar scan WiFi");
            Serial.flush();
            
            // CRÍTICO: Reabilita o watchdog ANTES de retornar
            Serial.println("Reabilitando watchdog (adicionando task de volta)...");
            Serial.flush();
            esp_err_t wdt_restore = esp_task_wdt_add(NULL);
            if (wdt_restore != ESP_OK && wdt_restore != ESP_ERR_INVALID_STATE) {
                esp_task_wdt_reset(); // Fallback
            }
            
            doc["status"] = "error";
            doc["message"] = "Falha ao iniciar scan WiFi. Verifique se o WiFi está habilitado.";
            
            // Restaura estado original ANTES de enviar resposta
            restoreWiFiState(originalMode, wasConnectedSTA, originalSSID);
            
            // Envia resposta como HTTP 200 (não 500) para que o frontend possa processar o JSON
            String response;
            if (serializeJson(doc, response) == 0) {
                // Se falhar a serialização, envia resposta simples
                response = "{\"status\":\"error\",\"message\":\"Erro ao processar resposta\"}";
            }
            request->send(200, "application/json", response);
            return;
        }
        
        // Aguarda o scan completar (timeout de 10 segundos)
        // Com watchdog desabilitado, podemos usar um timeout maior
        unsigned long startTime = millis();
        int n = -1;
        const unsigned long SCAN_TIMEOUT = 10000; // 10 segundos (aumentado já que watchdog está desabilitado)
        
        Serial.println("Aguardando scan completar (watchdog desabilitado)...");
        Serial.flush();
        
        // Loop de espera - watchdog está desabilitado, então não precisa de yield() tão frequente
        // Mas ainda usamos yield() para manter responsividade do servidor web
        while (n < 0 && (millis() - startTime) < SCAN_TIMEOUT) {
            yield(); // Mantém servidor web responsivo
            delay(100); // Delay de 100ms entre verificações
            yield();
            n = WiFi.scanComplete();
            
            // Log de progresso a cada segundo
            unsigned long elapsed = millis() - startTime;
            if (elapsed > 0 && elapsed % 1000 < 150) {
                Serial.print("Scan em andamento... ");
                Serial.print(elapsed / 1000);
                Serial.println("s");
                Serial.flush();
            }
        }
        
        // CRÍTICO: Reabilita o watchdog imediatamente após o scan (ou timeout)
        // Adiciona a task atual de volta ao watchdog
        Serial.println("Reabilitando watchdog (adicionando task de volta)...");
        Serial.flush();
        wdt_result = esp_task_wdt_add(NULL);
        if (wdt_result != ESP_OK && wdt_result != ESP_ERR_INVALID_STATE) {
            Serial.print("AVISO: Não foi possível reabilitar watchdog: ");
            Serial.println(wdt_result);
            Serial.print("Tentando resetar watchdog...");
            Serial.flush();
            // Tenta resetar o watchdog como fallback
            esp_task_wdt_reset();
        } else {
            Serial.println("Watchdog reabilitado com sucesso");
            Serial.flush();
        }
        
        // Verifica se o scan completou ou se houve timeout
        if (n < 0) {
            unsigned long elapsed = millis() - startTime;
            Serial.print("Timeout ao escanear redes WiFi após ");
            Serial.print(elapsed);
            Serial.println("ms");
            Serial.flush();
            
            // Prepara resposta de erro (HTTP 200 com status de erro para o frontend processar)
            doc["status"] = "error";
            doc["message"] = "Timeout ao escanear redes WiFi. O scan demorou mais de 10 segundos. Tente novamente.";
            doc["timeout"] = true;
            
            // Restaura estado original ANTES de enviar resposta
            restoreWiFiState(originalMode, wasConnectedSTA, originalSSID);
            
            // Envia resposta como HTTP 200 (não 500) para que o frontend possa processar o JSON
            String response;
            if (serializeJson(doc, response) == 0) {
                // Se falhar a serialização, envia resposta simples
                response = "{\"status\":\"error\",\"message\":\"Erro ao processar resposta\"}";
            }
            request->send(200, "application/json", response);
            
            // Limpa resultados do scan
            WiFi.scanDelete();
            yield();
            return;
        }
        
        // Processa resultados do scan
        if (n == 0) {
            Serial.println("Nenhuma rede encontrada");
            doc["status"] = "no_networks";
            doc["message"] = "Nenhuma rede encontrada";
        } else {
            Serial.print(n);
            Serial.println(" redes encontradas");
            
            // Adiciona todas as redes encontradas
            for (int i = 0; i < n; ++i) {
                yield(); // Permite que outras tarefas executem durante o processamento
                
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
                
                // Yield a cada 5 redes processadas para não bloquear muito tempo
                if (i % 5 == 0) {
                    yield();
                }
            }
            
            doc["status"] = "success";
            doc["count"] = n;
        }
        
        // Restaura estado WiFi original
        restoreWiFiState(originalMode, wasConnectedSTA, originalSSID);
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
        
        // Limpa resultados do scan
        WiFi.scanDelete();
        yield();
        
    } catch (...) {
        Serial.println("Erro inesperado no scan WiFi");
        Serial.flush();
        
        // CRÍTICO: Garante que o watchdog seja reabilitado mesmo em caso de exceção
        Serial.println("Reabilitando watchdog após exceção (adicionando task de volta)...");
        Serial.flush();
        esp_err_t wdt_restore = esp_task_wdt_add(NULL);
        if (wdt_restore != ESP_OK && wdt_restore != ESP_ERR_INVALID_STATE) {
            esp_task_wdt_reset(); // Fallback
        }
        
        doc["status"] = "error";
        doc["message"] = "Erro interno ao escanear redes. Tente novamente.";
        
        // Tenta restaurar estado original mesmo em caso de erro
        restoreWiFiState(originalMode, wasConnectedSTA, originalSSID);
        
        // Envia resposta como HTTP 200 (não 500) para que o frontend possa processar o JSON
        String response;
        if (serializeJson(doc, response) == 0) {
            // Se falhar a serialização, envia resposta simples
            response = "{\"status\":\"error\",\"message\":\"Erro ao processar resposta\"}";
        }
        request->send(200, "application/json", response);
    }
}

// Função auxiliar para restaurar o estado WiFi original
void restoreWiFiState(WiFiMode_t originalMode, bool wasConnectedSTA, const String& originalSSID) {
    Serial.println("Restaurando estado WiFi original...");
    Serial.flush();
    
    yield();
    
    // Restaura o modo WiFi original
    if (originalMode == WIFI_AP) {
        // Se estava em AP, volta para AP (cliente conectado ao AP não perde conexão)
        // Ao mudar de AP_STA para AP, o AP continua funcionando
        WiFi.mode(WIFI_AP);
        yield();
        delay(50);  // Delay mínimo para estabilização
        yield();
        
        // Garante que o AP está configurado (pode não ser necessário, mas é seguro)
        const char* apSSID = (strlen(config.wifi.apSSID) > 0) ? config.wifi.apSSID : AP_SSID;
        const char* apPassword = (strlen(config.wifi.apPassword) > 0) ? config.wifi.apPassword : AP_PASSWORD;
        WiFi.softAP(apSSID, apPassword);
        yield();
    } else if (originalMode == WIFI_STA) {
        // Se estava em STA, volta para STA
        WiFi.mode(WIFI_STA);
        yield();
        delay(50);
        yield();
        
        // Se estava conectado como STA, tenta reconectar (não-bloqueante)
        if (wasConnectedSTA && originalSSID.length() > 0) {
            WiFi.begin(originalSSID.c_str());
            yield();
        }
    } else if (originalMode == WIFI_AP_STA) {
        // Se já estava em AP_STA, não precisa mudar nada
        // Apenas garante que o AP está configurado
        const char* apSSID = (strlen(config.wifi.apSSID) > 0) ? config.wifi.apSSID : AP_SSID;
        const char* apPassword = (strlen(config.wifi.apPassword) > 0) ? config.wifi.apPassword : AP_PASSWORD;
        WiFi.softAP(apSSID, apPassword);
        yield();
        
        // Se estava conectado como STA, tenta reconectar (não-bloqueante)
        if (wasConnectedSTA && originalSSID.length() > 0) {
            WiFi.begin(originalSSID.c_str());
            yield();
        }
    }
    
    yield();
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
            
            // Se o filtro de Kalman está habilitado e inicializado, usa o valor do Kalman
            if (config.devices[i].registers[j].kalmanEnabled && kalmanStates[i][j].initialized) {
                // Aplica gain e offset ao valor do Kalman
                float kalmanValue = kalmanStates[i][j].estimate;
                processedValue = (kalmanValue * config.devices[i].registers[j].gain) + config.devices[i].registers[j].offset;
            }
            
            reg["value"] = processedValue;  // Valor processado usado nas expressões (com Kalman se habilitado)
            reg["kalmanEnabled"] = config.devices[i].registers[j].kalmanEnabled;
            
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
        
        // Atualiza configuração WiFi (usa mesma lógica de handleSaveConfig)
        if (doc.containsKey("wifi")) {
            JsonObject wifiObj = doc["wifi"];
            
            // Processa modo WiFi com validação e normalização para minúsculas
            if (wifiObj.containsKey("mode") && wifiObj["mode"].is<const char*>()) {
                const char* modeStr = wifiObj["mode"].as<const char*>();
                if (modeStr && strlen(modeStr) > 0) {
                    // Normaliza para minúsculas para evitar problemas de comparação
                    String modeStrLower = String(modeStr);
                    modeStrLower.toLowerCase();
                    strncpy(config.wifi.mode, modeStrLower.c_str(), sizeof(config.wifi.mode) - 1);
                    config.wifi.mode[sizeof(config.wifi.mode) - 1] = '\0';
                } else {
                    strcpy(config.wifi.mode, "ap");
                }
            } else {
                strcpy(config.wifi.mode, "ap");
            }
            
            // Processa AP SSID com validação
            if (wifiObj.containsKey("apSSID") && wifiObj["apSSID"].is<const char*>()) {
                const char* apSSIDStr = wifiObj["apSSID"].as<const char*>();
                if (apSSIDStr) {
                    strncpy(config.wifi.apSSID, apSSIDStr, sizeof(config.wifi.apSSID) - 1);
                    config.wifi.apSSID[sizeof(config.wifi.apSSID) - 1] = '\0';
                } else {
                    strcpy(config.wifi.apSSID, AP_SSID);
                }
            } else {
                strcpy(config.wifi.apSSID, AP_SSID);
            }
            
            // Processa AP Password com validação
            if (wifiObj.containsKey("apPassword") && wifiObj["apPassword"].is<const char*>()) {
                const char* apPasswordStr = wifiObj["apPassword"].as<const char*>();
                if (apPasswordStr) {
                    strncpy(config.wifi.apPassword, apPasswordStr, sizeof(config.wifi.apPassword) - 1);
                    config.wifi.apPassword[sizeof(config.wifi.apPassword) - 1] = '\0';
                } else {
                    strcpy(config.wifi.apPassword, AP_PASSWORD);
                }
            } else {
                strcpy(config.wifi.apPassword, AP_PASSWORD);
            }
            
            // Processa STA SSID com validação
            if (wifiObj.containsKey("staSSID") && wifiObj["staSSID"].is<const char*>()) {
                const char* staSSIDStr = wifiObj["staSSID"].as<const char*>();
                if (staSSIDStr) {
                    strncpy(config.wifi.staSSID, staSSIDStr, sizeof(config.wifi.staSSID) - 1);
                    config.wifi.staSSID[sizeof(config.wifi.staSSID) - 1] = '\0';
                } else {
                    config.wifi.staSSID[0] = '\0';
                }
            } else {
                config.wifi.staSSID[0] = '\0';
            }
            
            // Processa STA Password com validação
            if (wifiObj.containsKey("staPassword") && wifiObj["staPassword"].is<const char*>()) {
                const char* staPasswordStr = wifiObj["staPassword"].as<const char*>();
                if (staPasswordStr) {
                    strncpy(config.wifi.staPassword, staPasswordStr, sizeof(config.wifi.staPassword) - 1);
                    config.wifi.staPassword[sizeof(config.wifi.staPassword) - 1] = '\0';
                } else {
                    config.wifi.staPassword[0] = '\0';
                }
            } else {
                config.wifi.staPassword[0] = '\0';
            }
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
                if (regObj.containsKey("gain") && regObj["gain"].is<float>()) {
                    config.devices[i].registers[j].gain = regObj["gain"].as<float>();
                } else {
                    config.devices[i].registers[j].gain = 1.0f;
                }
                
                if (regObj.containsKey("offset") && regObj["offset"].is<float>()) {
                    config.devices[i].registers[j].offset = regObj["offset"].as<float>();
                } else {
                    config.devices[i].registers[j].offset = 0.0f;
                }
                
                // Carrega kalmanEnabled (padrão: false) com validação
                if (regObj.containsKey("kalmanEnabled") && regObj["kalmanEnabled"].is<bool>()) {
                    config.devices[i].registers[j].kalmanEnabled = regObj["kalmanEnabled"].as<bool>();
                } else {
                    config.devices[i].registers[j].kalmanEnabled = false;
                }
                
                // Carrega parâmetros do filtro de Kalman (valores padrão se não especificados) com validação
                if (regObj.containsKey("kalmanQ") && regObj["kalmanQ"].is<float>()) {
                    float kalmanQ = regObj["kalmanQ"].as<float>();
                    if (isnan(kalmanQ) || isinf(kalmanQ) || kalmanQ <= 0.0f) {
                        config.devices[i].registers[j].kalmanQ = 0.01f;
                    } else {
                        config.devices[i].registers[j].kalmanQ = kalmanQ;
                    }
                } else {
                    config.devices[i].registers[j].kalmanQ = 0.01f;
                }
                
                if (regObj.containsKey("kalmanR") && regObj["kalmanR"].is<float>()) {
                    float kalmanR = regObj["kalmanR"].as<float>();
                    if (isnan(kalmanR) || isinf(kalmanR) || kalmanR <= 0.0f) {
                        config.devices[i].registers[j].kalmanR = 0.1f;
                    } else {
                        config.devices[i].registers[j].kalmanR = kalmanR;
                    }
                } else {
                    config.devices[i].registers[j].kalmanR = 0.1f;
                }
            }
        }
        
        // Valida valores do Kalman antes de salvar (mesma validação de handleSaveConfig)
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
        
        // Salva a configuração importada
        saveConfig();
        
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Configuração importada com sucesso\"}");
    } else {
        request->send(400, "application/json", "{\"error\":\"Dados não fornecidos\"}");
    }
}

void handleResetConfig(AsyncWebServerRequest *request) {
    if (!request) {
        return;
    }
    
    Serial.println("Reset de configuração solicitado via API");
    consolePrint("[Acao] Reset de configuracoes solicitado\r\n");
    
    bool success = resetConfig();
    
    if (success) {
        Serial.println("Configuração resetada com sucesso");
        consolePrint("[Sucesso] Configuracoes resetadas para valores padrao\r\n");
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Configuracao resetada para valores padrao\"}");
    } else {
        Serial.println("ERRO: Falha ao resetar configuração");
        consolePrint("[Erro] Falha ao resetar configuracoes\r\n");
        request->send(500, "application/json", "{\"status\":\"error\",\"error\":\"Falha ao resetar configuracao\"}");
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
        
        // Arrays para armazenar variáveis temporárias (máximo 50 variáveis)
        // k[i] = nome da variável (máximo 5 caracteres), v[i] = valor
        const int MAX_TEMP_VARS = 50;
        char tempVarNames[MAX_TEMP_VARS][6];  // 5 caracteres + null terminator
        double tempVarValues[MAX_TEMP_VARS];
        int tempVarCount = 0;
        
        // Converte para formato Variable para compatibilidade com substituteDeviceValues
        Variable tempVariables[MAX_TEMP_VARS];
        
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
            bool success = substituteDeviceValues(expressionToProcess, &deviceValues, processedExpression, sizeof(processedExpression), errorMsg, sizeof(errorMsg), tempVariables, tempVarCount);
            
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
                // Se é atribuição a variável temporária
                if (assignmentInfo.isVariableAssignment) {
                    // Limita nome a 5 caracteres
                    char varName[6];
                    strncpy(varName, assignmentInfo.targetVariable, 5);
                    varName[5] = '\0';
                    
                    // Armazena ou atualiza a variável temporária nos arrays k[] e v[]
                    bool varFound = false;
                    for (int i = 0; i < tempVarCount; i++) {
                        if (strcmp(tempVarNames[i], varName) == 0) {
                            tempVarValues[i] = result;
                            // Atualiza também no array Variable para compatibilidade
                            tempVariables[i].value = result;
                            varFound = true;
                            break;
                        }
                    }
                    
                    if (!varFound) {
                        // Adiciona nova variável temporária
                        if (tempVarCount < MAX_TEMP_VARS) {
                            strncpy(tempVarNames[tempVarCount], varName, 5);
                            tempVarNames[tempVarCount][5] = '\0';
                            tempVarValues[tempVarCount] = result;
                            
                            // Atualiza também no array Variable para compatibilidade
                            strncpy(tempVariables[tempVarCount].name, varName, sizeof(tempVariables[tempVarCount].name) - 1);
                            tempVariables[tempVarCount].name[sizeof(tempVariables[tempVarCount].name) - 1] = '\0';
                            tempVariables[tempVarCount].value = result;
                            
                            tempVarCount++;
                        }
                    }
                    
                    lineResult["hasAssignment"] = true;
                    lineResult["isVariableAssignment"] = true;
                    lineResult["targetVariable"] = varName;
                    lineResult["message"] = "Variavel temporaria armazenada";
                } else {
                    lineResult["hasAssignment"] = true;
                    lineResult["isVariableAssignment"] = false;
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

