/**
 * @file config_storage.cpp
 * @brief Implementação das funções de persistência de configuração
 */

#include "config_storage.h"
#include <ArduinoJson.h>
#include "config.h"
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Variável global
Preferences preferences;

void loadConfig() {
    // CRÍTICO: protege acesso ao config durante load (evita concorrência com loop/web)
    (void)lockConfig(portMAX_DELAY);

    preferences.begin("modbus", true); // Modo read-only
    
    // Tenta carregar configuração
    String configJson = preferences.getString("config", "{}");
    preferences.end();
    
    if (configJson == "{}") {
        // Configuração padrão vazia
        config.baudRate = MODBUS_SERIAL_BAUD;  // Usa valor padrão
        // Configurações seriais padrão do Modbus RTU
        config.dataBits = MODBUS_DATA_BITS_DEFAULT;
        config.stopBits = MODBUS_STOP_BITS_DEFAULT;
        config.parity = MODBUS_PARITY_NONE;
        config.startBits = MODBUS_START_BITS_DEFAULT;
        config.timeout = 50;  // Timeout padrão: 50ms
        config.deviceCount = 0;
        
        // Configurações padrão MQTT
        config.mqtt.enabled = false;
        strcpy(config.mqtt.server, "");
        config.mqtt.port = 1883;
        strcpy(config.mqtt.user, "");
        strcpy(config.mqtt.password, "");
        strcpy(config.mqtt.topic, "esp32/modbus");
        config.mqtt.interval = 60;
        
        // Configurações padrão WiFi
        strcpy(config.wifi.mode, "ap");
        strcpy(config.wifi.apSSID, AP_SSID);
        strcpy(config.wifi.apPassword, AP_PASSWORD);
        strcpy(config.wifi.staSSID, "");
        strcpy(config.wifi.staPassword, "");
        
        // Configurações padrão RTC
        config.rtc.enabled = false;
        config.rtc.timezone = -3;  // UTC-3 (Brasília)
        strcpy(config.rtc.ntpServer, "pool.ntp.org");
        config.rtc.ntpEnabled = true;
        config.rtc.epochTime = 0;  // Não inicializado
        config.rtc.bootTime = 0;
        
        // Configurações padrão WireGuard
        config.wireguard.enabled = false;
        strcpy(config.wireguard.privateKey, "");
        strcpy(config.wireguard.publicKey, "");
        strcpy(config.wireguard.serverAddress, "");
        config.wireguard.serverPort = 51820;
        config.wireguard.localIP = IPAddress(10, 10, 0, 2);
        config.wireguard.gatewayIP = IPAddress(10, 10, 0, 1);
        config.wireguard.subnetMask = IPAddress(255, 255, 255, 0);
        
        // Código de cálculo vazio por padrão
        config.calculationCode[0] = '\0';
        
        // Inicializa todos os campos padrão
        for (int i = 0; i < MAX_DEVICES; i++) {
            config.devices[i].deviceName[0] = '\0';
            for (int j = 0; j < MAX_REGISTERS_PER_DEVICE; j++) {
                config.devices[i].registers[j].variableName[0] = '\0';
                config.devices[i].registers[j].gain = 1.0f;      // Ganho padrão: 1.0
                config.devices[i].registers[j].offset = 0.0f;   // Offset padrão: 0.0
                config.devices[i].registers[j].readOnly = false; // Padrão: leitura e escrita
                config.devices[i].registers[j].kalmanEnabled = false; // Padrão: filtro desabilitado
                config.devices[i].registers[j].kalmanQ = 0.01f; // Process noise padrão
                config.devices[i].registers[j].kalmanR = 0.1f;  // Measurement noise padrão
                config.devices[i].registers[j].generateGraph = false; // Padrão: não gerar gráfico
            }
        }
        
        Serial.println("Nenhuma configuração encontrada, usando padrão");
        unlockConfig();
        return;
    }
    
    // Parse do JSON
    // Tamanho aumentado para suportar muitos dispositivos e registros
    DynamicJsonDocument doc(24576);  // 24KB para garantir espaço suficiente
    DeserializationError error = deserializeJson(doc, configJson);
    
    if (error) {
        Serial.print("Erro ao carregar configuração: ");
        Serial.println(error.c_str());
        Serial.print("Tamanho do JSON carregado: ");
        Serial.print(configJson.length());
        Serial.println(" bytes");
        config.deviceCount = 0;
        unlockConfig();
        return;
    }
    
    // Log para debug
    Serial.print("JSON carregado com sucesso. Tamanho: ");
    Serial.print(configJson.length());
    Serial.println(" bytes");
    
    // Carrega configurações do sistema
    config.baudRate = doc["baudRate"] | MODBUS_SERIAL_BAUD;
    // Configurações seriais (com fallback para valores padrão)
    config.dataBits = doc["dataBits"] | MODBUS_DATA_BITS_DEFAULT;
    config.stopBits = doc["stopBits"] | MODBUS_STOP_BITS_DEFAULT;
    config.parity = doc["parity"] | MODBUS_PARITY_NONE;
    config.startBits = doc["startBits"] | MODBUS_START_BITS_DEFAULT;
    config.timeout = doc["timeout"] | 50;  // Timeout padrão: 50ms
    config.deviceCount = doc["deviceCount"] | 0;
    
    // Carrega configuração MQTT
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
    
    // Carrega configuração WiFi
    if (doc.containsKey("wifi")) {
        JsonObject wifiObj = doc["wifi"];
        const char* modeStr = wifiObj["mode"] | "ap";
        // Normaliza o modo para minúsculas para evitar problemas de comparação
        String modeStrLower = String(modeStr);
        modeStrLower.toLowerCase();
        strncpy(config.wifi.mode, modeStrLower.c_str(), sizeof(config.wifi.mode) - 1);
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
        
        Serial.print("WiFi carregado - Mode: '");
        Serial.print(config.wifi.mode);
        Serial.print("', STA SSID: '");
        Serial.print(config.wifi.staSSID);
        Serial.print("', STA Password length: ");
        Serial.println(strlen(config.wifi.staPassword));
    }
    
    // Carrega configuração RTC
    if (doc.containsKey("rtc")) {
        JsonObject rtcObj = doc["rtc"];
        config.rtc.enabled = rtcObj["enabled"] | false;
        config.rtc.timezone = rtcObj["timezone"] | -3;
        strncpy(config.rtc.ntpServer, rtcObj["ntpServer"] | "pool.ntp.org", sizeof(config.rtc.ntpServer) - 1);
        config.rtc.ntpEnabled = rtcObj["ntpEnabled"] | true;
    }
    
    // Carrega configuração WireGuard
    if (doc.containsKey("wireguard")) {
        JsonObject wgObj = doc["wireguard"];
        config.wireguard.enabled = wgObj["enabled"] | false;
        strncpy(config.wireguard.privateKey, wgObj["privateKey"] | "", sizeof(config.wireguard.privateKey) - 1);
        config.wireguard.privateKey[sizeof(config.wireguard.privateKey) - 1] = '\0';
        strncpy(config.wireguard.publicKey, wgObj["publicKey"] | "", sizeof(config.wireguard.publicKey) - 1);
        config.wireguard.publicKey[sizeof(config.wireguard.publicKey) - 1] = '\0';
        strncpy(config.wireguard.serverAddress, wgObj["serverAddress"] | "", sizeof(config.wireguard.serverAddress) - 1);
        config.wireguard.serverAddress[sizeof(config.wireguard.serverAddress) - 1] = '\0';
        config.wireguard.serverPort = wgObj["serverPort"] | 51820;
        
        // Carrega IPs (formato: "10.0.0.2")
        if (wgObj.containsKey("localIP")) {
            const char* localIPStr = wgObj["localIP"] | "10.0.0.2";
            config.wireguard.localIP.fromString(localIPStr);
        } else {
            config.wireguard.localIP = IPAddress(10, 0, 0, 2);
        }
        
        if (wgObj.containsKey("gatewayIP")) {
            const char* gatewayIPStr = wgObj["gatewayIP"] | "10.0.0.1";
            config.wireguard.gatewayIP.fromString(gatewayIPStr);
        } else {
            config.wireguard.gatewayIP = IPAddress(10, 0, 0, 1);
        }
        
        if (wgObj.containsKey("subnetMask")) {
            const char* subnetMaskStr = wgObj["subnetMask"] | "255.255.255.0";
            config.wireguard.subnetMask.fromString(subnetMaskStr);
        } else {
            config.wireguard.subnetMask = IPAddress(255, 255, 255, 0);
        }
    } else {
        // Configurações padrão se não existir no JSON
        config.wireguard.enabled = false;
        strcpy(config.wireguard.privateKey, "");
        strcpy(config.wireguard.publicKey, "");
        strcpy(config.wireguard.serverAddress, "");
        config.wireguard.serverPort = 51820;
        config.wireguard.localIP = IPAddress(10, 0, 0, 2);
        config.wireguard.gatewayIP = IPAddress(10, 0, 0, 1);
        config.wireguard.subnetMask = IPAddress(255, 255, 255, 0);
    }
    
    // Carrega código de cálculo
    if (doc.containsKey("calculationCode")) {
        const char* code = doc["calculationCode"] | "";
        strncpy(config.calculationCode, code, sizeof(config.calculationCode) - 1);
        config.calculationCode[sizeof(config.calculationCode) - 1] = '\0';
    } else {
        config.calculationCode[0] = '\0';
    }
    
    // Verifica se há array de dispositivos
    if (!doc.containsKey("devices") || !doc["devices"].is<JsonArray>()) {
        Serial.println("AVISO: Array de dispositivos nao encontrado no JSON");
        config.deviceCount = 0;
        return;
    }
    
    JsonArray devicesArray = doc["devices"].as<JsonArray>();
    int deviceCountFromJson = devicesArray.size();
    
    Serial.print("Dispositivos no JSON: ");
    Serial.println(deviceCountFromJson);
    
    for (int i = 0; i < config.deviceCount && i < MAX_DEVICES && i < deviceCountFromJson; i++) {
        JsonObject deviceObj = devicesArray[i];
        if (!deviceObj) {
            Serial.print("AVISO: Dispositivo ");
            Serial.print(i);
            Serial.println(" nao encontrado no JSON");
            continue;
        }
        
        config.devices[i].slaveAddress = deviceObj["slaveAddress"] | 0;
        config.devices[i].enabled = deviceObj["enabled"] | false;
        config.devices[i].registerCount = deviceObj["registerCount"] | 0;
        
        // Carrega nome do dispositivo
        const char* devName = deviceObj["deviceName"] | "";
        strncpy(config.devices[i].deviceName, devName, sizeof(config.devices[i].deviceName) - 1);
        config.devices[i].deviceName[sizeof(config.devices[i].deviceName) - 1] = '\0';
        
        Serial.print("Carregando dispositivo ");
        Serial.print(i);
        Serial.print(": ");
        Serial.print(config.devices[i].registerCount);
        Serial.println(" registros");
        
        // Verifica se há array de registros
        if (!deviceObj.containsKey("registers") || !deviceObj["registers"].is<JsonArray>()) {
            Serial.print("AVISO: Array de registros nao encontrado para dispositivo ");
            Serial.println(i);
            config.devices[i].registerCount = 0;
            continue;
        }
        
        JsonArray registersArray = deviceObj["registers"].as<JsonArray>();
        int registerCountFromJson = registersArray.size();
        
        // Limita ao menor valor entre o configurado, o máximo permitido e o que está no JSON
        if (config.devices[i].registerCount > registerCountFromJson) {
            Serial.print("AVISO: registerCount maior que registros no JSON. Ajustando de ");
            Serial.print(config.devices[i].registerCount);
            Serial.print(" para ");
            Serial.println(registerCountFromJson);
            config.devices[i].registerCount = registerCountFromJson;
        }
        
        if (config.devices[i].registerCount > MAX_REGISTERS_PER_DEVICE) {
            config.devices[i].registerCount = MAX_REGISTERS_PER_DEVICE;
        }
        
        for (int j = 0; j < config.devices[i].registerCount && j < MAX_REGISTERS_PER_DEVICE; j++) {
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
            
            // Carrega generateGraph (padrão: false)
            config.devices[i].registers[j].generateGraph = regObj["generateGraph"] | false;
            
            // Carrega registerType (padrão: 2 - Leitura e Escrita)
            if (regObj.containsKey("registerType")) {
                config.devices[i].registers[j].registerType = regObj["registerType"] | 2;
            } else {
                // Compatibilidade: se registerType não existe, calcula baseado nos campos antigos
                if (regObj["isInput"] | true) {
                    if (regObj["readOnly"] | false) {
                        config.devices[i].registers[j].registerType = 0; // somente leitura
                    } else {
                        config.devices[i].registers[j].registerType = 2; // leitura e escrita
                    }
                } else {
                    config.devices[i].registers[j].registerType = 0; // Input Register = somente leitura
                }
            }
            
            // Carrega registerCount (padrão: 1)
            config.devices[i].registers[j].registerCount = regObj["registerCount"] | 1;
            
            Serial.print("  Registro ");
            Serial.print(j);
            Serial.print(": endereco=");
            Serial.print(config.devices[i].registers[j].address);
            Serial.print(", variavel=");
            Serial.print(config.devices[i].registers[j].variableName);
            Serial.print(", valor inicial=");
            Serial.println(config.devices[i].registers[j].value);
        }
    }
    
    Serial.print("Configuracao carregada: ");
    Serial.print(config.deviceCount);
    Serial.println(" dispositivos");

    unlockConfig();
}

bool saveConfig() {
    // CRÍTICO: protege acesso ao config durante save (evita concorrência com loop/web)
    (void)lockConfig(portMAX_DELAY);

    preferences.begin("modbus", false); // Modo read-write
    
    // Cria JSON da configuração
    // Tamanho aumentado para suportar muitos dispositivos e registros
    // Estimativa: ~200 bytes por dispositivo + ~100 bytes por registro
    // Para 10 dispositivos com 20 registros cada: ~22KB
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
    
    // Adiciona código de cálculo
    doc["calculationCode"] = String(config.calculationCode);
    
    JsonArray devicesArray = doc.createNestedArray("devices");
    
    for (int i = 0; i < config.deviceCount; i++) {
        JsonObject deviceObj = devicesArray.createNestedObject();
        deviceObj["slaveAddress"] = config.devices[i].slaveAddress;
        deviceObj["enabled"] = config.devices[i].enabled;
        deviceObj["deviceName"] = String(config.devices[i].deviceName);
        
        JsonArray registersArray = deviceObj.createNestedArray("registers");
        
        // IMPORTANTE: Salva todos os registros e atualiza registerCount baseado no tamanho real
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
            regObj["generateGraph"] = config.devices[i].registers[j].generateGraph;
            regObj["registerType"] = config.devices[i].registers[j].registerType;
            regObj["registerCount"] = config.devices[i].registers[j].registerCount;
            // Não salva value aqui, pois será lido do Modbus ou inicializado com 0
        }
        
        // IMPORTANTE: Atualiza registerCount baseado no tamanho real do array salvo
        // Isso garante sincronização entre registerCount e o array de registros
        deviceObj["registerCount"] = registersArray.size();
        // NÃO altera config.devices[i].registerCount aqui: salvar não deve mutar o config.
    }
    
    // CRÍTICO: Reserva espaço para a string JSON antes de serializar para evitar fragmentação
    // Estima tamanho baseado no documento (pode ser menor que o tamanho do doc)
    String configJson;
    configJson.reserve(20000);  // Reserva espaço para evitar realocações
    
    size_t jsonSize = serializeJson(doc, configJson);
    
    // Verifica se o JSON foi serializado corretamente
    if (jsonSize == 0) {
        Serial.println("ERRO: Falha ao serializar configuração JSON");
        preferences.end();
        unlockConfig();
        return false;
    }
    
    // Log do tamanho do JSON para debug
    Serial.print("Tamanho do JSON: ");
    Serial.print(jsonSize);
    Serial.println(" bytes");
    
    // Verifica se o JSON cabe no Preferences (limite de ~20KB por chave)
    if (configJson.length() > 20000) {
        Serial.println("ERRO: JSON muito grande para salvar no Preferences (limite: 20KB)");
        preferences.end();
        unlockConfig();
        return false;
    }
    
    // CRÍTICO: Verifica se a string é válida antes de salvar
    if (configJson.length() == 0 || configJson.c_str() == nullptr) {
        Serial.println("ERRO: String JSON inválida ou vazia");
        preferences.end();
        unlockConfig();
        return false;
    }
    
    // CRÍTICO: Dá tempo ao sistema antes de salvar (evita problemas de timing)
    yield();
    vTaskDelay(10 / portTICK_PERIOD_MS); // Aumentado de 5ms para 10ms
    
    // CRÍTICO: Cria uma cópia do ponteiro e valida antes de usar
    // Isso garante que o ponteiro não seja invalidado durante a operação
    bool saved = false;
    const char* jsonCStr = configJson.c_str();
    
    // Validações adicionais antes de salvar
    if (jsonCStr == nullptr) {
        Serial.println("ERRO: Ponteiro JSON é nullptr");
        preferences.end();
        unlockConfig();
        return false;
    }
    
    size_t jsonLen = strlen(jsonCStr);
    if (jsonLen == 0) {
        Serial.println("ERRO: String JSON está vazia");
        preferences.end();
        unlockConfig();
        return false;
    }
    
    if (jsonLen > 20000) {
        Serial.println("ERRO: JSON muito grande para salvar");
        preferences.end();
        unlockConfig();
        return false;
    }
    
    // CRÍTICO: Verifica se o ponteiro aponta para memória válida
    // Tenta acessar o primeiro e último caractere para validar
    if (jsonCStr[0] != '{' || jsonCStr[jsonLen - 1] != '}') {
        Serial.println("ERRO: JSON não parece válido (não começa/termina com {})");
        preferences.end();
        unlockConfig();
        return false;
    }
    
    // CRÍTICO: Salva no Preferences usando const char* diretamente
    // Adiciona delay adicional antes da operação crítica
    yield();
    vTaskDelay(5 / portTICK_PERIOD_MS);
    
    // Tenta salvar
    saved = preferences.putString("config", jsonCStr);
    
    if (!saved) {
        Serial.println("ERRO: putString retornou false");
        preferences.end();
        unlockConfig();
        return false;
    }
    
    // CRÍTICO: Libera a string imediatamente após salvar para liberar memória
    configJson = "";
    
    // Dá mais um tempo para garantir que a escrita foi concluída
    yield();
    vTaskDelay(10 / portTICK_PERIOD_MS); // Aumentado de 0 para 10ms
    
    preferences.end();
    unlockConfig();
    
    if (saved) {
        Serial.println("Configuração salva com sucesso");
        Serial.print("Dispositivos salvos: ");
        Serial.println(config.deviceCount);
        for (int i = 0; i < config.deviceCount; i++) {
            Serial.print("  Dispositivo ");
            Serial.print(i);
            Serial.print(": ");
            Serial.print(config.devices[i].registerCount);
            Serial.println(" registros");
        }
        return true;
    } else {
        Serial.println("ERRO: Falha ao salvar configuração no Preferences");
        return false;
    }
}

bool resetConfig() {
    Serial.println("Resetando configurações para valores padrão...");

    // CRÍTICO: protege acesso ao config durante reset
    (void)lockConfig(portMAX_DELAY);
    
    // Limpa a configuração salva
    preferences.begin("modbus", false); // Modo read-write
    bool cleared = preferences.remove("config");
    preferences.end();
    
    if (!cleared) {
        Serial.println("AVISO: Não foi possível limpar configuração antiga (pode não existir)");
    } else {
        Serial.println("Configuração antiga removida");
    }
    
    // Reseta a estrutura config para valores padrão
    config.baudRate = MODBUS_SERIAL_BAUD;
    // Configurações seriais padrão do Modbus RTU
    config.dataBits = MODBUS_DATA_BITS_DEFAULT;
    config.stopBits = MODBUS_STOP_BITS_DEFAULT;
    config.parity = MODBUS_PARITY_NONE;
    config.startBits = MODBUS_START_BITS_DEFAULT;
    config.timeout = 50;  // Timeout padrão: 50ms
    config.deviceCount = 0;
    
    // Configurações padrão MQTT
    config.mqtt.enabled = false;
    strcpy(config.mqtt.server, "");
    config.mqtt.port = 1883;
    strcpy(config.mqtt.user, "");
    strcpy(config.mqtt.password, "");
    strcpy(config.mqtt.topic, "esp32/modbus");
    config.mqtt.interval = 60;
    
    // Configurações padrão WiFi
    strcpy(config.wifi.mode, "ap");
    strcpy(config.wifi.apSSID, AP_SSID);
    strcpy(config.wifi.apPassword, AP_PASSWORD);
    strcpy(config.wifi.staSSID, "");
    strcpy(config.wifi.staPassword, "");
    
    // Configurações padrão RTC
    config.rtc.enabled = false;
    config.rtc.timezone = -3;  // UTC-3 (Brasília)
    strcpy(config.rtc.ntpServer, "pool.ntp.org");
    config.rtc.ntpEnabled = true;
    config.rtc.epochTime = 0;
    config.rtc.bootTime = 0;
    
    // Configurações padrão WireGuard
    config.wireguard.enabled = false;
    strcpy(config.wireguard.privateKey, "");
    strcpy(config.wireguard.publicKey, "");
    strcpy(config.wireguard.serverAddress, "");
    config.wireguard.serverPort = 51820;
    config.wireguard.localIP = IPAddress(10, 10, 0, 2);
    config.wireguard.gatewayIP = IPAddress(10, 10, 0, 1);
    config.wireguard.subnetMask = IPAddress(255, 255, 255, 0);
    
    // Código de cálculo vazio por padrão
    config.calculationCode[0] = '\0';
    
    // Inicializa todos os campos padrão
    for (int i = 0; i < MAX_DEVICES; i++) {
        config.devices[i].slaveAddress = 0;
        config.devices[i].enabled = false;
        config.devices[i].registerCount = 0;
        config.devices[i].deviceName[0] = '\0';
        for (int j = 0; j < MAX_REGISTERS_PER_DEVICE; j++) {
            config.devices[i].registers[j].address = 0;
            config.devices[i].registers[j].value = 0;
            config.devices[i].registers[j].isInput = true;
            config.devices[i].registers[j].isOutput = false;
            config.devices[i].registers[j].readOnly = false;
            config.devices[i].registers[j].variableName[0] = '\0';
            config.devices[i].registers[j].gain = 1.0f;
            config.devices[i].registers[j].offset = 0.0f;
            config.devices[i].registers[j].kalmanEnabled = false;
            config.devices[i].registers[j].kalmanQ = 0.01f;
            config.devices[i].registers[j].kalmanR = 0.1f;
            config.devices[i].registers[j].generateGraph = false;
            config.devices[i].registers[j].writeFunction = 0x06;
            config.devices[i].registers[j].writeRegisterCount = 1;
            config.devices[i].registers[j].registerType = 2; // padrão: Leitura e Escrita
            config.devices[i].registers[j].registerCount = 1; // padrão: 1 registrador
        }
    }
    
    // Salva a configuração padrão
    bool saved = saveConfig();
    
    if (saved) {
        Serial.println("Configuração resetada e salva com sucesso");
        unlockConfig();
        return true;
    } else {
        Serial.println("ERRO: Falha ao salvar configuração padrão após reset");
        unlockConfig();
        return false;
    }
}

