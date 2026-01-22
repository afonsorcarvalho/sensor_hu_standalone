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
#include "wireguard_manager.h"
#include <ArduinoJson.h>
#include <ESP.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

// Variáveis globais
AsyncWebServer server(WEB_SERVER_PORT);
AsyncWebSocket* consoleWebSocket = nullptr;

// Controle de conexões simultâneas
#define MAX_CONCURRENT_CONNECTIONS 4  // Limite de conexões simultâneas (recomendado: 4-5 para ESP32)
static volatile int activeConnections = 0;
static SemaphoreHandle_t connectionsMutex = nullptr;

// Função auxiliar para verificar e incrementar conexões
static bool tryAcquireConnection() {
    if (connectionsMutex == nullptr) {
        connectionsMutex = xSemaphoreCreateMutex();
        if (connectionsMutex == nullptr) {
            return false;
        }
    }
    
    if (xSemaphoreTake(connectionsMutex, portMAX_DELAY) == pdTRUE) {
        if (activeConnections < MAX_CONCURRENT_CONNECTIONS) {
            activeConnections++;
            xSemaphoreGive(connectionsMutex);
            return true;
        }
        xSemaphoreGive(connectionsMutex);
    }
    return false;
}

// Função auxiliar para liberar conexão
static void releaseConnection() {
    if (connectionsMutex != nullptr) {
        if (xSemaphoreTake(connectionsMutex, portMAX_DELAY) == pdTRUE) {
            if (activeConnections > 0) {
                activeConnections--;
            }
            xSemaphoreGive(connectionsMutex);
        }
    }
}

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
        if (!tryAcquireConnection()) {
            request->send(503, "text/html", "<html><body><h1>Servidor Ocupado</h1><p>Limite de " + String(MAX_CONCURRENT_CONNECTIONS) + " conexoes simultaneas atingido. Tente novamente em alguns instantes.</p></body></html>");
            return;
        }
        handleRoot(request);
        releaseConnection();
    });
    
    // Rota para resetar configurações para valores padrão (deve vir antes de /api/config)
    server.on("/api/config/reset", HTTP_POST, [](AsyncWebServerRequest *request){
        if (!request) return;
        if (!tryAcquireConnection()) {
            request->send(503, "application/json", "{\"error\":\"Servidor ocupado. Limite de " + String(MAX_CONCURRENT_CONNECTIONS) + " conexoes simultaneas atingido. Tente novamente em alguns instantes.\"}");
            return;
        }
        handleResetConfig(request);
        releaseConnection();
    });
    
    // Rota para exportar configurações (deve vir antes de /api/config)
    server.on("/api/config/export", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!tryAcquireConnection()) {
            request->send(503, "application/json", "{\"error\":\"Servidor ocupado. Limite de " + String(MAX_CONCURRENT_CONNECTIONS) + " conexoes simultaneas atingido. Tente novamente em alguns instantes.\"}");
            return;
        }
        handleExportConfig(request);
        releaseConnection();
    });
    
    // Rota para importar configurações (deve vir antes de /api/config)
    // Usa handler de body para acumular todos os chunks antes de processar
    server.on("/api/config/import", HTTP_POST,
        [](AsyncWebServerRequest *request){
            // Verifica limite de conexões no início
            if (!tryAcquireConnection()) {
                request->send(503, "application/json", "{\"error\":\"Servidor ocupado. Limite de " + String(MAX_CONCURRENT_CONNECTIONS) + " conexoes simultaneas atingido. Tente novamente em alguns instantes.\"}");
                return;
            }
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
                    releaseConnection(); // Libera conexão após processar
                }
            }
        });
    
    // Rota para obter configuração atual (JSON)
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!tryAcquireConnection()) {
            request->send(503, "application/json", "{\"error\":\"Servidor ocupado. Limite de " + String(MAX_CONCURRENT_CONNECTIONS) + " conexoes simultaneas atingido. Tente novamente em alguns instantes.\"}");
            return;
        }
        handleGetConfig(request);
        releaseConnection();
    });
    
    // Rota para salvar nova configuração (POST JSON)
    // Usa handler de body para acumular todos os chunks antes de processar
    server.on("/api/config", HTTP_POST, 
        [](AsyncWebServerRequest *request){
            // Verifica limite de conexões no início
            if (!tryAcquireConnection()) {
                request->send(503, "application/json", "{\"error\":\"Servidor ocupado. Limite de " + String(MAX_CONCURRENT_CONNECTIONS) + " conexoes simultaneas atingido. Tente novamente em alguns instantes.\"}");
                return;
            }
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
                releaseConnection(); // Libera conexão em caso de erro
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
                    releaseConnection(); // Libera conexão após processar
                }
                // Se ainda estamos recebendo chunks, apenas continua acumulando
                // Não precisa fazer nada aqui - o AsyncWebServer continuará chamando este handler
            } else {
                // request->_tempObject é nullptr mesmo após tentar inicializar - erro crítico
                Serial.println("[Config] ERRO CRÍTICO: Não foi possível inicializar buffer");
                request->send(500, "application/json", "{\"error\":\"Erro interno: falha ao inicializar buffer\"}");
                releaseConnection(); // Libera conexão em caso de erro
            }
        });
    
    // Rota para leitura manual de registros
    server.on("/api/read", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!tryAcquireConnection()) {
            request->send(503, "application/json", "{\"error\":\"Servidor ocupado. Limite de " + String(MAX_CONCURRENT_CONNECTIONS) + " conexoes simultaneas atingido. Tente novamente em alguns instantes.\"}");
            return;
        }
        handleReadRegisters(request);
        releaseConnection();
    });
    
    // Rota para reboot do sistema
    server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request){
        if (!tryAcquireConnection()) {
            request->send(503, "application/json", "{\"error\":\"Servidor ocupado. Limite de " + String(MAX_CONCURRENT_CONNECTIONS) + " conexoes simultaneas atingido. Tente novamente em alguns instantes.\"}");
            return;
        }
        handleReboot(request);
        // Não libera conexão aqui pois o sistema vai reiniciar
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
    
    // Rota para obter status do WireGuard
    server.on("/api/wireguard/status", HTTP_GET, [](AsyncWebServerRequest *request){
        handleWireGuardStatus(request);
    });
    
    // Rota para conectar WireGuard
    server.on("/api/wireguard/connect", HTTP_POST, [](AsyncWebServerRequest *request){
        handleWireGuardConnect(request);
    });
    
    // Rota para desconectar WireGuard
    server.on("/api/wireguard/disconnect", HTTP_POST, [](AsyncWebServerRequest *request){
        handleWireGuardDisconnect(request);
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
        if (!tryAcquireConnection()) {
            request->send(503, "application/json", "{\"error\":\"Servidor ocupado. Limite de " + String(MAX_CONCURRENT_CONNECTIONS) + " conexoes simultaneas atingido. Tente novamente em alguns instantes.\"}");
            return;
        }
        handleGetVariables(request);
        releaseConnection();
    });
    
    // Rota para escrever valor de variável
    server.on("/api/variable/write", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
            handleWriteVariable(request, data, len);
        });
    
    // Rota para listar arquivos do filesystem
    server.on("/api/filesystem/list", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!tryAcquireConnection()) {
            request->send(503, "application/json", "{\"error\":\"Servidor ocupado. Limite de " + String(MAX_CONCURRENT_CONNECTIONS) + " conexoes simultaneas atingido. Tente novamente em alguns instantes.\"}");
            return;
        }
        handleListFiles(request);
        releaseConnection();
    });
    
    // Rota para baixar arquivo do filesystem
    server.on("/api/filesystem/download", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!tryAcquireConnection()) {
            request->send(503, "application/json", "{\"error\":\"Servidor ocupado. Limite de " + String(MAX_CONCURRENT_CONNECTIONS) + " conexoes simultaneas atingido. Tente novamente em alguns instantes.\"}");
            return;
        }
        handleDownloadFile(request);
        releaseConnection();
    });
    
    // Rota para deletar arquivo do filesystem
    server.on("/api/filesystem/delete", HTTP_POST, 
        [](AsyncWebServerRequest *request){
            // Verifica limite de conexões no início
            if (!tryAcquireConnection()) {
                request->send(503, "application/json", "{\"error\":\"Servidor ocupado. Limite de " + String(MAX_CONCURRENT_CONNECTIONS) + " conexoes simultaneas atingido. Tente novamente em alguns instantes.\"}");
                return;
            }
            // Handler de início - inicializa buffer se necessário
            request->_tempObject = new String();
        },
        NULL,  // onUpload - não usado
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
            // Handler de body - acumula chunks
            if (request->_tempObject) {
                String* bodyBuffer = (String*)request->_tempObject;
                for (size_t i = 0; i < len; i++) {
                    *bodyBuffer += (char)data[i];
                }
                
                // Se recebeu todos os chunks, processa
                if (index + len >= total && bodyBuffer->length() >= total) {
                    handleDeleteFile(request, (uint8_t*)bodyBuffer->c_str(), bodyBuffer->length());
                    delete bodyBuffer;
                    request->_tempObject = nullptr;
                    releaseConnection(); // Libera conexão após processar
                }
            }
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
    // Configurações seriais do Modbus
    doc["dataBits"] = config.dataBits;
    doc["stopBits"] = config.stopBits;
    doc["parity"] = config.parity;
    doc["startBits"] = config.startBits;
    doc["timeout"] = config.timeout;
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
    
    // Adiciona configuração WireGuard
    JsonObject wgObj = doc.createNestedObject("wireguard");
    wgObj["enabled"] = config.wireguard.enabled;
    wgObj["privateKey"] = String(config.wireguard.privateKey);
    wgObj["publicKey"] = String(config.wireguard.publicKey);
    wgObj["serverAddress"] = String(config.wireguard.serverAddress);
    wgObj["serverPort"] = config.wireguard.serverPort;
    wgObj["localIP"] = config.wireguard.localIP.toString();
    wgObj["gatewayIP"] = config.wireguard.gatewayIP.toString();
    wgObj["subnetMask"] = config.wireguard.subnetMask.toString();
    wgObj["status"] = getWireGuardStatus();
    
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
            regObj["writeFunction"] = config.devices[i].registers[j].writeFunction;
            regObj["writeRegisterCount"] = config.devices[i].registers[j].writeRegisterCount;
            regObj["registerType"] = config.devices[i].registers[j].registerType;
            regObj["registerCount"] = config.devices[i].registers[j].registerCount;
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
    
    // CRÍTICO: pausa o processamento para evitar concorrência durante salvar config
    g_processingPaused = true;

    // Espera o ciclo atual terminar (até 2s) para evitar tocar no config no meio do uso
    unsigned long waitStart = millis();
    while (g_cycleInProgress && (millis() - waitStart) < 2000) {
        yield();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Serializa múltiplos saves/imports com mutex (timeout curto para não travar async_tcp)
    if (!lockConfig(pdMS_TO_TICKS(100))) {
        g_processingPaused = false;
        request->send(503, "application/json", "{\"error\":\"Sistema ocupado salvando/atualizando configuracao. Tente novamente.\"}");
        return;
    }

    // CRÍTICO: Processa o JSON em um escopo separado para garantir que o DynamicJsonDocument
    // seja destruído antes de chamar saveConfig(), evitando corrupção de heap
    // (saveConfig() também cria um DynamicJsonDocument de 24KB)
    {
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
                unlockConfig();
                g_processingPaused = false;
            return;
        }
        
        Serial.println("[Config] JSON deserializado com sucesso");
        
        // Log para debug
        Serial.print("[Config] JSON recebido com sucesso. Tamanho: ");
        Serial.print(body.length());
        Serial.println(" bytes");
    
    // Atualiza configuração do sistema
    uint32_t newBaudRate = doc["baudRate"] | MODBUS_SERIAL_BAUD;
    uint8_t newDataBits = doc["dataBits"] | MODBUS_DATA_BITS_DEFAULT;
    uint8_t newStopBits = doc["stopBits"] | MODBUS_STOP_BITS_DEFAULT;
    uint8_t newParity = doc["parity"] | MODBUS_PARITY_NONE;
    uint8_t newStartBits = doc["startBits"] | MODBUS_START_BITS_DEFAULT;
    
    // Sanitiza valores para manter compatibilidade com UART do ESP32
    if (newDataBits != 7 && newDataBits != 8) newDataBits = MODBUS_DATA_BITS_DEFAULT;
    if (newStopBits != 1 && newStopBits != 2) newStopBits = MODBUS_STOP_BITS_DEFAULT;
    if (newParity != MODBUS_PARITY_NONE && newParity != MODBUS_PARITY_EVEN && newParity != MODBUS_PARITY_ODD) {
        newParity = MODBUS_PARITY_NONE;
    }
    // Start bit não é configurável em UART, mantém 1
    newStartBits = MODBUS_START_BITS_DEFAULT;
    uint16_t newTimeout = doc["timeout"] | 50;  // Timeout padrão: 50ms
    
    // Sanitiza timeout (10-1000ms)
    if (newTimeout < 10) newTimeout = 10;
    if (newTimeout > 1000) newTimeout = 1000;
    
    config.baudRate = newBaudRate;
    config.dataBits = newDataBits;
    config.stopBits = newStopBits;
    config.parity = newParity;
    config.startBits = newStartBits;
    config.timeout = newTimeout;
    
    // Se parâmetros seriais mudaram, reconfigura o Modbus
    uint32_t newSerialConfig = buildSerialConfig(newDataBits, newParity, newStopBits);
    if (newBaudRate != currentBaudRate || newSerialConfig != currentSerialConfig) {
        setupModbus(newBaudRate, newSerialConfig);
    } else {
        // Se apenas o timeout mudou, atualiza o timeout do Serial2
        // (setupModbus já atualiza o timeout, mas só é chamado se outros parâmetros mudarem)
        if (Serial2) {
            Serial2.setTimeout(newTimeout);
        }
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
    
    // Atualiza configuração WireGuard
    bool wireguardWasEnabled = config.wireguard.enabled;
    if (doc.containsKey("wireguard")) {
        JsonObject wgObj = doc["wireguard"];
        config.wireguard.enabled = wgObj["enabled"] | false;
        
        const char* privateKeyStr = wgObj["privateKey"] | "";
        strncpy(config.wireguard.privateKey, privateKeyStr, sizeof(config.wireguard.privateKey) - 1);
        config.wireguard.privateKey[sizeof(config.wireguard.privateKey) - 1] = '\0';
        
        const char* publicKeyStr = wgObj["publicKey"] | "";
        strncpy(config.wireguard.publicKey, publicKeyStr, sizeof(config.wireguard.publicKey) - 1);
        config.wireguard.publicKey[sizeof(config.wireguard.publicKey) - 1] = '\0';
        
        const char* serverAddressStr = wgObj["serverAddress"] | "";
        strncpy(config.wireguard.serverAddress, serverAddressStr, sizeof(config.wireguard.serverAddress) - 1);
        config.wireguard.serverAddress[sizeof(config.wireguard.serverAddress) - 1] = '\0';
        
        config.wireguard.serverPort = wgObj["serverPort"] | 51820;
        
        // Processa IPs
        if (wgObj.containsKey("localIP")) {
            const char* localIPStr = wgObj["localIP"] | "10.0.0.2";
            config.wireguard.localIP.fromString(localIPStr);
        }
        
        if (wgObj.containsKey("gatewayIP")) {
            const char* gatewayIPStr = wgObj["gatewayIP"] | "10.0.0.1";
            config.wireguard.gatewayIP.fromString(gatewayIPStr);
        }
        
        if (wgObj.containsKey("subnetMask")) {
            const char* subnetMaskStr = wgObj["subnetMask"] | "255.255.255.0";
            config.wireguard.subnetMask.fromString(subnetMaskStr);
        }
        
        Serial.print("[Config] WireGuard configurado - Enabled: ");
        Serial.print(config.wireguard.enabled);
        Serial.print(", Server: ");
        Serial.print(config.wireguard.serverAddress);
        Serial.print(":");
        Serial.println(config.wireguard.serverPort);
        
        // Se foi habilitado e estava desabilitado antes, tenta conectar
        if (config.wireguard.enabled && !wireguardWasEnabled && WiFi.status() == WL_CONNECTED) {
            Serial.println("[Config] WireGuard habilitado, tentando conectar...");
        }
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
            
            // Carrega writeFunction (padrão: 0x06 - Write Single Register)
            if (regObj.containsKey("writeFunction")) {
                config.devices[i].registers[j].writeFunction = regObj["writeFunction"] | 0x06;
            } else {
                config.devices[i].registers[j].writeFunction = 0x06;
            }
            
            // Carrega writeRegisterCount (padrão: 1)
            if (regObj.containsKey("writeRegisterCount")) {
                config.devices[i].registers[j].writeRegisterCount = regObj["writeRegisterCount"] | 1;
            } else {
                config.devices[i].registers[j].writeRegisterCount = 1;
            }
            
            // Carrega registerType (padrão: 2 - Leitura e Escrita)
            if (regObj.containsKey("registerType")) {
                config.devices[i].registers[j].registerType = regObj["registerType"] | 2;
            } else {
                // Migração: converte campos antigos para novo formato
                if (!config.devices[i].registers[j].isInput) {
                    config.devices[i].registers[j].registerType = 0; // Input Register = somente leitura
                } else if (config.devices[i].registers[j].readOnly) {
                    config.devices[i].registers[j].registerType = 0; // somente leitura
                } else if (config.devices[i].registers[j].isOutput && !config.devices[i].registers[j].readOnly) {
                    config.devices[i].registers[j].registerType = 1; // somente escrita
                } else {
                    config.devices[i].registers[j].registerType = 2; // leitura e escrita
                }
            }
            
            // Carrega registerCount (padrão: 1)
            if (regObj.containsKey("registerCount")) {
                config.devices[i].registers[j].registerCount = regObj["registerCount"] | 1;
            } else {
                // Usa writeRegisterCount se registerCount não existir (migração)
                config.devices[i].registers[j].registerCount = config.devices[i].registers[j].writeRegisterCount;
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
    } // FIM DO ESCOPO DO DOCUMENTO JSON - doc será destruído aqui
    
    // CRÍTICO: Libera também a string body para liberar memória antes de saveConfig()
    body = "";
    
    // CRÍTICO: Dá tempo ao sistema para liberar memória e fazer garbage collection
    // antes de alocar novamente em saveConfig() (que cria um DynamicJsonDocument de 24KB)
    yield();
    vTaskDelay(20 / portTICK_PERIOD_MS); // Aumentado de 10ms para 20ms
    
    // Força garbage collection adicional
    yield();
    vTaskDelay(10 / portTICK_PERIOD_MS);
    
    // Salva na EEPROM (memória não volátil)
    Serial.println("[Config] Salvando configuração na memória não volátil...");
    consolePrint("[Acao] Botao 'Salvar Todas as Configuracoes' clicado\r\n");
    
    bool saveSuccess = saveConfig();

    // Libera o mutex do config após salvar (independente do resultado)
    unlockConfig();
    g_processingPaused = false;
    
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
    // CRÍTICO: Permite que outras tarefas executem durante a leitura Modbus
    // Isso evita que o webserver trave completamente se houver timeout no Modbus
    yield();

    // CRÍTICO: protege acesso ao config (e ao Modbus/ciclo) durante leitura manual
    if (!lockConfig(pdMS_TO_TICKS(2000))) {
        request->send(503, "application/json", "{\"error\":\"Sistema ocupado. Tente novamente.\"}");
        return;
    }
    
    readAllDevices();
    
    // CRÍTICO: Yield após leitura para garantir responsividade do webserver
    yield();
    
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

    unlockConfig();
    
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
        // CRÍTICO: data NÃO é garantido ser nulo-terminado. Monta o body usando len.
        String body;
        body.reserve(len + 1);
        for (size_t i = 0; i < len; i++) {
            body += (char)data[i];
        }
        
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

void handleWireGuardStatus(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(512);
    doc["enabled"] = config.wireguard.enabled;
    doc["status"] = getWireGuardStatus();
    doc["connected"] = isWireGuardConnected();
    
    if (config.wireguard.enabled) {
        doc["localIP"] = config.wireguard.localIP.toString();
        doc["serverAddress"] = String(config.wireguard.serverAddress);
        doc["serverPort"] = config.wireguard.serverPort;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleWireGuardConnect(AsyncWebServerRequest *request) {
    // Tenta conectar
    if (!config.wireguard.enabled) {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"WireGuard não está habilitado\"}");
        return;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"WiFi não está conectado\"}");
        return;
    }
    
    bool success = setupWireGuard();
    
    if (success) {
        DynamicJsonDocument doc(256);
        doc["status"] = "ok";
        doc["message"] = "WireGuard conectado com sucesso";
        doc["localIP"] = config.wireguard.localIP.toString();
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    } else {
        request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Falha ao conectar WireGuard. Verifique configuração e logs.\"}");
    }
}

void handleWireGuardDisconnect(AsyncWebServerRequest *request) {
    if (isWireGuardConnected()) {
        disconnectWireGuard();
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"WireGuard desconectado\"}");
    } else {
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"WireGuard já estava desconectado\"}");
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
    vTaskDelay(10 / portTICK_PERIOD_MS); // Delay para estabilização
    
    // CRÍTICO: Copia o SSID para uma variável local antes de usar
    // Isso evita problemas de memória se a String original for destruída
    char ssidBuffer[33] = {0}; // WiFi SSID máximo é 32 caracteres + null terminator
    if (wasConnectedSTA && originalSSID.length() > 0 && originalSSID.length() < 33) {
        strncpy(ssidBuffer, originalSSID.c_str(), sizeof(ssidBuffer) - 1);
        ssidBuffer[sizeof(ssidBuffer) - 1] = '\0'; // Garante null terminator
    }
    
    yield();
    vTaskDelay(10 / portTICK_PERIOD_MS);
    
    // Restaura o modo WiFi original
    if (originalMode == WIFI_AP) {
        // Se estava em AP, volta para AP (cliente conectado ao AP não perde conexão)
        // Ao mudar de AP_STA para AP, o AP continua funcionando
        WiFi.mode(WIFI_AP);
        yield();
        vTaskDelay(50 / portTICK_PERIOD_MS);  // Delay para estabilização
        yield();
        
        // Garante que o AP está configurado (pode não ser necessário, mas é seguro)
        const char* apSSID = (strlen(config.wifi.apSSID) > 0) ? config.wifi.apSSID : AP_SSID;
        const char* apPassword = (strlen(config.wifi.apPassword) > 0) ? config.wifi.apPassword : AP_PASSWORD;
        
        // Valida ponteiros antes de usar
        if (apSSID != nullptr && apPassword != nullptr) {
            WiFi.softAP(apSSID, apPassword);
            yield();
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    } else if (originalMode == WIFI_STA) {
        // Se estava em STA, volta para STA
        WiFi.mode(WIFI_STA);
        yield();
        vTaskDelay(100 / portTICK_PERIOD_MS); // Delay maior para estabilização após mudança de modo
        yield();
        
        // Se estava conectado como STA, tenta reconectar (não-bloqueante)
        // Usa o buffer local ao invés de c_str() diretamente
        if (wasConnectedSTA && strlen(ssidBuffer) > 0) {
            // Verifica se já está conectado antes de tentar reconectar
            if (WiFi.status() != WL_CONNECTED) {
                // CRÍTICO: Desconecta explicitamente antes de reconectar para limpar estado
                WiFi.disconnect(false); // false = não apaga credenciais salvas
                yield();
                vTaskDelay(50 / portTICK_PERIOD_MS);
                yield();
                
                // CRÍTICO: Verifica se o buffer é válido antes de usar
                // CRÍTICO: Usa a senha da configuração se o SSID corresponder
                const char* password = "";
                if (strcmp(ssidBuffer, config.wifi.staSSID) == 0) {
                    // Se o SSID corresponde ao configurado, usa a senha salva
                    password = (strlen(config.wifi.staPassword) > 0) ? config.wifi.staPassword : "";
                }
                
                if (strlen(password) > 0) {
                    WiFi.begin(ssidBuffer, password);
                    Serial.print("[WiFi] Tentando reconectar a rede: ");
                    Serial.println(ssidBuffer);
                } else {
                    // Tenta sem senha (para redes abertas ou se senha não estiver configurada)
                    WiFi.begin(ssidBuffer);
                    Serial.print("[WiFi] Tentando reconectar a rede (sem senha): ");
                    Serial.println(ssidBuffer);
                }
                yield();
                vTaskDelay(50 / portTICK_PERIOD_MS);
            } else {
                Serial.println("[WiFi] Ja conectado, nao precisa reconectar");
            }
        }
    } else if (originalMode == WIFI_AP_STA) {
        // Se já estava em AP_STA, não precisa mudar nada
        // Apenas garante que o AP está configurado
        const char* apSSID = (strlen(config.wifi.apSSID) > 0) ? config.wifi.apSSID : AP_SSID;
        const char* apPassword = (strlen(config.wifi.apPassword) > 0) ? config.wifi.apPassword : AP_PASSWORD;
        
        // Valida ponteiros antes de usar
        if (apSSID != nullptr && apPassword != nullptr) {
            WiFi.softAP(apSSID, apPassword);
            yield();
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        
        // Se estava conectado como STA, tenta reconectar (não-bloqueante)
        // Usa o buffer local ao invés de c_str() diretamente
        if (wasConnectedSTA && strlen(ssidBuffer) > 0) {
            // Verifica se já está conectado antes de tentar reconectar
            if (WiFi.status() != WL_CONNECTED) {
                // CRÍTICO: Desconecta explicitamente antes de reconectar para limpar estado
                WiFi.disconnect(false); // false = não apaga credenciais salvas
                yield();
                vTaskDelay(50 / portTICK_PERIOD_MS);
                yield();
                
                // CRÍTICO: Verifica se o buffer é válido antes de usar
                // CRÍTICO: Usa a senha da configuração se o SSID corresponder
                const char* password = "";
                if (strcmp(ssidBuffer, config.wifi.staSSID) == 0) {
                    // Se o SSID corresponde ao configurado, usa a senha salva
                    password = (strlen(config.wifi.staPassword) > 0) ? config.wifi.staPassword : "";
                }
                
                if (strlen(password) > 0) {
                    WiFi.begin(ssidBuffer, password);
                    Serial.print("[WiFi] Tentando reconectar a rede: ");
                    Serial.println(ssidBuffer);
                } else {
                    // Tenta sem senha (para redes abertas ou se senha não estiver configurada)
                    WiFi.begin(ssidBuffer);
                    Serial.print("[WiFi] Tentando reconectar a rede (sem senha): ");
                    Serial.println(ssidBuffer);
                }
                yield();
                vTaskDelay(50 / portTICK_PERIOD_MS);
            } else {
                Serial.println("[WiFi] Ja conectado, nao precisa reconectar");
            }
        }
    }
    
    yield();
    vTaskDelay(10 / portTICK_PERIOD_MS);
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
            reg["generateGraph"] = config.devices[i].registers[j].generateGraph;
            
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
    // Configurações seriais do Modbus
    doc["dataBits"] = config.dataBits;
    doc["stopBits"] = config.stopBits;
    doc["parity"] = config.parity;
    doc["startBits"] = config.startBits;
    doc["timeout"] = config.timeout;
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
    
    // Adiciona configuração WireGuard
    JsonObject wgObj = doc.createNestedObject("wireguard");
    wgObj["enabled"] = config.wireguard.enabled;
    wgObj["privateKey"] = String(config.wireguard.privateKey);
    wgObj["publicKey"] = String(config.wireguard.publicKey);
    wgObj["serverAddress"] = String(config.wireguard.serverAddress);
    wgObj["serverPort"] = config.wireguard.serverPort;
    wgObj["localIP"] = config.wireguard.localIP.toString();
    wgObj["gatewayIP"] = config.wireguard.gatewayIP.toString();
    wgObj["subnetMask"] = config.wireguard.subnetMask.toString();
    
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
            regObj["writeFunction"] = config.devices[i].registers[j].writeFunction;
            regObj["writeRegisterCount"] = config.devices[i].registers[j].writeRegisterCount;
            regObj["registerType"] = config.devices[i].registers[j].registerType;
            regObj["registerCount"] = config.devices[i].registers[j].registerCount;
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
        // CRÍTICO: pausa o processamento para evitar concorrência durante import
        g_processingPaused = true;
        unsigned long waitStart = millis();
        while (g_cycleInProgress && (millis() - waitStart) < 2000) {
            yield();
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // Timeout curto para não travar async_tcp
        if (!lockConfig(pdMS_TO_TICKS(100))) {
            g_processingPaused = false;
            request->send(503, "application/json", "{\"error\":\"Sistema ocupado salvando/atualizando configuracao. Tente novamente.\"}");
            return;
        }

        // CRÍTICO: data NÃO é garantido ser nulo-terminado. Monta o body usando len.
        String body;
        body.reserve(len + 1);
        for (size_t i = 0; i < len; i++) {
            body += (char)data[i];
        }
        
        // Usa a mesma lógica de handleSaveConfig para processar o JSON
        DynamicJsonDocument doc(24576);
        DeserializationError error = deserializeJson(doc, body);
        
        if (error) {
            request->send(400, "application/json", "{\"error\":\"JSON inválido\"}");
            unlockConfig();
            g_processingPaused = false;
            return;
        }
        
        // Processa a configuração (mesma lógica de handleSaveConfig)
        uint32_t newBaudRate = doc["baudRate"] | MODBUS_SERIAL_BAUD;
        uint8_t newDataBits = doc["dataBits"] | MODBUS_DATA_BITS_DEFAULT;
        uint8_t newStopBits = doc["stopBits"] | MODBUS_STOP_BITS_DEFAULT;
        uint8_t newParity = doc["parity"] | MODBUS_PARITY_NONE;
        uint8_t newStartBits = doc["startBits"] | MODBUS_START_BITS_DEFAULT;
        
        // Sanitiza valores para manter compatibilidade com UART do ESP32
        if (newDataBits != 7 && newDataBits != 8) newDataBits = MODBUS_DATA_BITS_DEFAULT;
        if (newStopBits != 1 && newStopBits != 2) newStopBits = MODBUS_STOP_BITS_DEFAULT;
        if (newParity != MODBUS_PARITY_NONE && newParity != MODBUS_PARITY_EVEN && newParity != MODBUS_PARITY_ODD) {
            newParity = MODBUS_PARITY_NONE;
        }
        // Start bit não é configurável em UART, mantém 1
        newStartBits = MODBUS_START_BITS_DEFAULT;
        uint16_t newTimeout = doc["timeout"] | 50;  // Timeout padrão: 50ms
        
        // Sanitiza timeout (10-1000ms)
        if (newTimeout < 10) newTimeout = 10;
        if (newTimeout > 1000) newTimeout = 1000;
        
        config.baudRate = newBaudRate;
        config.dataBits = newDataBits;
        config.stopBits = newStopBits;
        config.parity = newParity;
        config.startBits = newStartBits;
        config.timeout = newTimeout;
        
        uint32_t newSerialConfig = buildSerialConfig(newDataBits, newParity, newStopBits);
        if (newBaudRate != currentBaudRate || newSerialConfig != currentSerialConfig) {
            setupModbus(newBaudRate, newSerialConfig);
        } else {
            // Se apenas o timeout mudou, atualiza o timeout do Serial2
            // (setupModbus já atualiza o timeout, mas só é chamado se outros parâmetros mudarem)
            if (Serial2) {
                Serial2.setTimeout(newTimeout);
            }
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
                
                // Carrega writeFunction (padrão: 0x06 - Write Single Register)
                if (regObj.containsKey("writeFunction")) {
                    config.devices[i].registers[j].writeFunction = regObj["writeFunction"] | 0x06;
                } else {
                    config.devices[i].registers[j].writeFunction = 0x06;
                }
                
                // Carrega writeRegisterCount (padrão: 1)
                if (regObj.containsKey("writeRegisterCount")) {
                    config.devices[i].registers[j].writeRegisterCount = regObj["writeRegisterCount"] | 1;
                } else {
                    config.devices[i].registers[j].writeRegisterCount = 1;
                }
                
                // Carrega registerType (padrão: 2 - Leitura e Escrita)
                if (regObj.containsKey("registerType")) {
                    config.devices[i].registers[j].registerType = regObj["registerType"] | 2;
                } else {
                    // Migração: converte campos antigos para novo formato
                    if (!config.devices[i].registers[j].isInput) {
                        config.devices[i].registers[j].registerType = 0; // Input Register = somente leitura
                    } else if (config.devices[i].registers[j].readOnly) {
                        config.devices[i].registers[j].registerType = 0; // somente leitura
                    } else if (config.devices[i].registers[j].isOutput && !config.devices[i].registers[j].readOnly) {
                        config.devices[i].registers[j].registerType = 1; // somente escrita
                    } else {
                        config.devices[i].registers[j].registerType = 2; // leitura e escrita
                    }
                }
                
                // Carrega registerCount (padrão: 1)
                if (regObj.containsKey("registerCount")) {
                    config.devices[i].registers[j].registerCount = regObj["registerCount"] | 1;
                } else {
                    // Usa writeRegisterCount se registerCount não existir (migração)
                    config.devices[i].registers[j].registerCount = config.devices[i].registers[j].writeRegisterCount;
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
        unlockConfig();
        g_processingPaused = false;
        
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

    // CRÍTICO: pausa o processamento para evitar concorrência durante reset
    g_processingPaused = true;
    unsigned long waitStart = millis();
    while (g_cycleInProgress && (millis() - waitStart) < 2000) {
        yield();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (!lockConfig(pdMS_TO_TICKS(100))) {
        g_processingPaused = false;
        request->send(503, "application/json", "{\"status\":\"error\",\"error\":\"Sistema ocupado. Tente novamente.\"}");
        return;
    }
    
    bool success = resetConfig();
    unlockConfig();
    g_processingPaused = false;
    
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
        // CRÍTICO: data NÃO é garantido ser nulo-terminado. Monta o body usando len.
        String body;
        body.reserve(len + 1);
        for (size_t i = 0; i < len; i++) {
            body += (char)data[i];
        }
        
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

        // Desabilita efeitos colaterais durante o teste (ex: escrita Modbus)
        setExpressionSideEffectsEnabled(false);
        
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
        // IMPORTANTE: Alocados no heap para evitar stack overflow
        // k[i] = nome da variável (máximo 5 caracteres), v[i] = valor
        const int MAX_TEMP_VARS = 50;
        char (*tempVarNames)[6] = new char[MAX_TEMP_VARS][6];  // 5 caracteres + null terminator
        double* tempVarValues = new double[MAX_TEMP_VARS];
        int tempVarCount = 0;
        
        // Converte para formato Variable para compatibilidade com substituteDeviceValues
        Variable* tempVariables = new Variable[MAX_TEMP_VARS];
        
        // Buffers alocados no heap para evitar stack overflow
        char* lineBuffer = new char[1024];
        char* processedExpression = new char[2048];
        char* errorMsg = new char[256];
        
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
            strncpy(lineBuffer, line.c_str(), 1023);
            lineBuffer[1023] = '\0';
            
            // Processa esta linha
            JsonObject lineResult = results.createNestedObject();
            lineResult["lineNumber"] = lineNumber;
            lineResult["expression"] = lineBuffer;
            
            AssignmentInfo assignmentInfo;
            errorMsg[0] = '\0';  // Limpa buffer de erro
            bool parseSuccess = parseAssignment(lineBuffer, &assignmentInfo, errorMsg, 256);
            
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
            processedExpression[0] = '\0';  // Limpa buffer
            errorMsg[0] = '\0';  // Limpa buffer de erro
            bool success = substituteDeviceValues(expressionToProcess, &deviceValues, processedExpression, 2048, errorMsg, 256, tempVariables, tempVarCount);
            
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
            errorMsg[0] = '\0';  // Limpa buffer de erro antes de avaliar
            bool evalSuccess = evaluateExpression(processedExpression, emptyVars, emptyVarCount, &result, errorMsg, 256);
            
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
        
        // Limpa memória DeviceValues
        for (int i = 0; i < config.deviceCount; i++) {
            delete[] deviceValues.values[i];
        }
        delete[] deviceValues.values;
        delete[] deviceValues.registerCounts;
        
        // Limpa memória dos arrays alocados no heap
        delete[] tempVarNames;
        delete[] tempVarValues;
        delete[] tempVariables;
        delete[] lineBuffer;
        delete[] processedExpression;
        delete[] errorMsg;
        
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
    
    // CRÍTICO: data NÃO é garantido ser nulo-terminado. Monta o body usando len.
    String body;
    body.reserve(len + 1);
    for (size_t i = 0; i < len; i++) {
        body += (char)data[i];
    }
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
    
    // Verifica se o registro pode ser escrito baseado no registerType
    uint8_t registerType = config.devices[deviceIndex].registers[registerIndex].registerType;
    bool canWrite = (registerType == 1 || registerType == 2); // Escrita ou Leitura e Escrita
    
    // Compatibilidade: verifica campos antigos se registerType não estiver definido
    if (registerType == 0) {
        if (config.devices[deviceIndex].registers[registerIndex].readOnly) {
            canWrite = false;
        } else if (config.devices[deviceIndex].registers[registerIndex].isInput) {
            canWrite = true;
        } else {
            canWrite = false;
        }
    }
    
    if (!canWrite) {
        request->send(400, "application/json", "{\"error\":\"Registro configurado apenas para leitura\"}");
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
    uint32_t rawValueInt = (uint32_t)round(rawValue);  // Mudado para uint32_t para suportar valores maiores
    
    // Escreve no Modbus
    uint8_t slaveAddr = config.devices[deviceIndex].slaveAddress;
    uint16_t regAddr = config.devices[deviceIndex].registers[registerIndex].address;
    uint8_t registerCount = config.devices[deviceIndex].registers[registerIndex].registerCount;
    if (registerCount == 0) registerCount = 1; // Garante mínimo de 1
    
    // CRÍTICO: Yield antes de operação Modbus para manter webserver responsivo
    yield();
    
    node.begin(slaveAddr, Serial2);
    uint8_t result = node.ku8MBSuccess;
    
    // Determina função Modbus automaticamente baseado na quantidade de registros
    // Padrão Modbus: 0x06 para 1 registrador, 0x10 para múltiplos
    if (registerCount == 1) {
        // Write Single Register (0x06)
        result = node.writeSingleRegister(regAddr, (uint16_t)(rawValueInt & 0xFFFF));
    } else {
        // Write Multiple Registers (0x10)
        // Divide o valor inteiro em múltiplos registros (16 bits cada)
        // O valor mais significativo vai no primeiro registrador
        for (int i = registerCount - 1; i >= 0; i--) {
            uint16_t regValue = (rawValueInt >> (i * 16)) & 0xFFFF;
            node.setTransmitBuffer((registerCount - 1) - i, regValue);
        }
        result = node.writeMultipleRegisters(regAddr, registerCount);
    }
    
    // CRÍTICO: Yield após operação Modbus para manter webserver responsivo
    yield();
    
    if (result == node.ku8MBSuccess) {
        // Atualiza valor na configuração (armazena apenas os 16 bits menos significativos)
        config.devices[deviceIndex].registers[registerIndex].value = (uint16_t)(rawValueInt & 0xFFFF);
        
        String logMsg = "[Modbus] Escrito Dev " + String(slaveAddr) + 
                       " Reg " + String(regAddr);
        
        if (registerCount > 1) {
            logMsg += " (funcao 0x10, " + String(registerCount) + " registros)";
        } else {
            logMsg += " (funcao 0x06)";
        }
        
        logMsg += ": " + String(value, 2) + 
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

void handleListFiles(AsyncWebServerRequest *request) {
    Serial.println("[Filesystem] Listando arquivos...");
    
    if (!LittleFS.begin(true)) {
        Serial.println("[Filesystem] ERRO: Falha ao montar LittleFS");
        request->send(500, "application/json", "{\"error\":\"Falha ao montar filesystem\"}");
        return;
    }
    
    DynamicJsonDocument doc(4096);
    JsonArray filesArray = doc.createNestedArray("files");
    
    size_t totalSize = 0;
    int fileCount = 0;
    
    // Lista todos os arquivos no filesystem
    File root = LittleFS.open("/");
    if (!root) {
        Serial.println("[Filesystem] ERRO: Falha ao abrir diretório raiz");
        request->send(500, "application/json", "{\"error\":\"Falha ao abrir diretório raiz\"}");
        return;
    }
    
    if (!root.isDirectory()) {
        Serial.println("[Filesystem] ERRO: Raiz não é um diretório");
        root.close();
        request->send(500, "application/json", "{\"error\":\"Raiz não é um diretório\"}");
        return;
    }
    
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            JsonObject fileObj = filesArray.createNestedObject();
            String fileName = String(file.name());
            // Remove o "/" inicial se existir
            if (fileName.startsWith("/")) {
                fileName = fileName.substring(1);
            }
            fileObj["name"] = fileName;
            fileObj["size"] = file.size();
            totalSize += file.size();
            fileCount++;
            
            Serial.print("[Filesystem] Arquivo encontrado: ");
            Serial.print(fileName);
            Serial.print(" (");
            Serial.print(file.size());
            Serial.println(" bytes)");
        }
        file = root.openNextFile();
    }
    root.close();
    
    // Obtém informações do filesystem
    // Nota: LittleFS não fornece FSInfo diretamente, então calculamos manualmente
    // O tamanho total do LittleFS é tipicamente ~1.5MB no ESP32-S3
    size_t totalBytes = 1536000; // Aproximação: 1.5MB (valor típico)
    size_t usedBytes = totalSize; // Usa o tamanho total dos arquivos como aproximação
    
    doc["totalSize"] = totalSize;
    doc["freeSpace"] = (totalBytes > usedBytes) ? (totalBytes - usedBytes) : 0;
    doc["totalSpace"] = totalBytes;
    doc["usedSpace"] = usedBytes;
    doc["fileCount"] = fileCount;
    
    String response;
    serializeJson(doc, response);
    
    Serial.print("[Filesystem] Total de arquivos: ");
    Serial.print(fileCount);
    Serial.print(", Espaço usado: ");
    Serial.print(usedBytes);
    Serial.print(" bytes, Espaço livre: ");
    Serial.print((totalBytes > usedBytes) ? (totalBytes - usedBytes) : 0);
    Serial.println(" bytes");
    
    request->send(200, "application/json", response);
}

void handleDownloadFile(AsyncWebServerRequest *request) {
    // Obtém o nome do arquivo do parâmetro de query
    if (!request->hasParam("file")) {
        request->send(400, "application/json", "{\"error\":\"Parâmetro 'file' não fornecido\"}");
        return;
    }
    
    String filename = "/" + request->getParam("file")->value();
    
    // Sanitiza o nome do arquivo (remove ".." e "/" extras para segurança)
    filename.replace("..", "");
    if (!filename.startsWith("/")) {
        filename = "/" + filename;
    }
    
    Serial.print("[Filesystem] Baixando arquivo: ");
    Serial.println(filename);
    
    if (!LittleFS.begin(true)) {
        Serial.println("[Filesystem] ERRO: Falha ao montar LittleFS");
        request->send(500, "application/json", "{\"error\":\"Falha ao montar filesystem\"}");
        return;
    }
    
    if (!LittleFS.exists(filename)) {
        Serial.println("[Filesystem] ERRO: Arquivo não encontrado");
        request->send(404, "application/json", "{\"error\":\"Arquivo não encontrado\"}");
        return;
    }
    
    File file = LittleFS.open(filename, "r");
    if (!file) {
        Serial.println("[Filesystem] ERRO: Falha ao abrir arquivo");
        request->send(500, "application/json", "{\"error\":\"Falha ao abrir arquivo\"}");
        return;
    }
    
    // Determina o tipo MIME baseado na extensão
    String contentType = "application/octet-stream";
    if (filename.endsWith(".html")) {
        contentType = "text/html";
    } else if (filename.endsWith(".css")) {
        contentType = "text/css";
    } else if (filename.endsWith(".js")) {
        contentType = "application/javascript";
    } else if (filename.endsWith(".json")) {
        contentType = "application/json";
    } else if (filename.endsWith(".txt")) {
        contentType = "text/plain";
    } else if (filename.endsWith(".png")) {
        contentType = "image/png";
    } else if (filename.endsWith(".jpg") || filename.endsWith(".jpeg")) {
        contentType = "image/jpeg";
    }
    
    // Envia o arquivo
    request->send(LittleFS, filename, contentType);
    
    Serial.print("[Filesystem] Arquivo enviado: ");
    Serial.print(filename);
    Serial.print(" (");
    Serial.print(file.size());
    Serial.println(" bytes)");
    
    file.close();
}

void handleDeleteFile(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
    if (!data || len == 0) {
        request->send(400, "application/json", "{\"error\":\"Body não fornecido\"}");
        return;
    }
    
    // Cria string do body
    String body;
    body.reserve(len + 1);
    for (size_t i = 0; i < len; i++) {
        body += (char)data[i];
    }
    
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, body);
    
    if (error) {
        request->send(400, "application/json", "{\"error\":\"JSON inválido\"}");
        return;
    }
    
    if (!doc.containsKey("filename")) {
        request->send(400, "application/json", "{\"error\":\"Campo 'filename' não fornecido\"}");
        return;
    }
    
    String filename = "/" + doc["filename"].as<String>();
    
    // Sanitiza o nome do arquivo (remove ".." e "/" extras para segurança)
    filename.replace("..", "");
    if (!filename.startsWith("/")) {
        filename = "/" + filename;
    }
    
    Serial.print("[Filesystem] Deletando arquivo: ");
    Serial.println(filename);
    
    if (!LittleFS.begin(true)) {
        Serial.println("[Filesystem] ERRO: Falha ao montar LittleFS");
        request->send(500, "application/json", "{\"error\":\"Falha ao montar filesystem\"}");
        return;
    }
    
    if (!LittleFS.exists(filename)) {
        Serial.println("[Filesystem] ERRO: Arquivo não encontrado");
        request->send(404, "application/json", "{\"error\":\"Arquivo não encontrado\"}");
        return;
    }
    
    // Proteção: não permite deletar index.html (arquivo crítico)
    if (filename == "/index.html" || filename == "index.html") {
        Serial.println("[Filesystem] ERRO: Não é permitido deletar index.html");
        request->send(403, "application/json", "{\"error\":\"Não é permitido deletar index.html\"}");
        return;
    }
    
    bool deleted = LittleFS.remove(filename);
    
    if (deleted) {
        Serial.println("[Filesystem] Arquivo deletado com sucesso");
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Arquivo deletado com sucesso\"}");
    } else {
        Serial.println("[Filesystem] ERRO: Falha ao deletar arquivo");
        request->send(500, "application/json", "{\"error\":\"Falha ao deletar arquivo\"}");
    }
}
