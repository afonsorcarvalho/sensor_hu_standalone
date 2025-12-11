/**
 * @file config_storage.cpp
 * @brief Implementação das funções de persistência de configuração
 */

#include "config_storage.h"
#include <ArduinoJson.h>
#include "config.h"

// Variável global
Preferences preferences;

void loadConfig() {
    preferences.begin("modbus", true); // Modo read-only
    
    // Tenta carregar configuração
    String configJson = preferences.getString("config", "{}");
    preferences.end();
    
    if (configJson == "{}") {
        // Configuração padrão vazia
        config.baudRate = MODBUS_SERIAL_BAUD;  // Usa valor padrão
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
            }
        }
        
        Serial.println("Nenhuma configuração encontrada, usando padrão");
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
        return;
    }
    
    // Log para debug
    Serial.print("JSON carregado com sucesso. Tamanho: ");
    Serial.print(configJson.length());
    Serial.println(" bytes");
    
    // Carrega configurações do sistema
    config.baudRate = doc["baudRate"] | MODBUS_SERIAL_BAUD;
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
    
    // Carrega configuração RTC
    if (doc.containsKey("rtc")) {
        JsonObject rtcObj = doc["rtc"];
        config.rtc.enabled = rtcObj["enabled"] | false;
        config.rtc.timezone = rtcObj["timezone"] | -3;
        strncpy(config.rtc.ntpServer, rtcObj["ntpServer"] | "pool.ntp.org", sizeof(config.rtc.ntpServer) - 1);
        config.rtc.ntpEnabled = rtcObj["ntpEnabled"] | true;
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
}

bool saveConfig() {
    preferences.begin("modbus", false); // Modo read-write
    
    // Cria JSON da configuração
    // Tamanho aumentado para suportar muitos dispositivos e registros
    // Estimativa: ~200 bytes por dispositivo + ~100 bytes por registro
    // Para 10 dispositivos com 20 registros cada: ~22KB
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
            // Não salva value aqui, pois será lido do Modbus ou inicializado com 0
        }
        
        // IMPORTANTE: Atualiza registerCount baseado no tamanho real do array salvo
        // Isso garante sincronização entre registerCount e o array de registros
        deviceObj["registerCount"] = registersArray.size();
        config.devices[i].registerCount = registersArray.size();
    }
    
    String configJson;
    size_t jsonSize = serializeJson(doc, configJson);
    
    // Verifica se o JSON foi serializado corretamente
    if (jsonSize == 0) {
        Serial.println("ERRO: Falha ao serializar configuração JSON");
        preferences.end();
        return false;
    }
    
    // Log do tamanho do JSON para debug
    Serial.print("Tamanho do JSON: ");
    Serial.print(jsonSize);
    Serial.println(" bytes");
    
    // Verifica se o JSON cabe no Preferences (limite de ~20KB por chave)
    if (configJson.length() > 20000) {
        Serial.println("AVISO: JSON muito grande, pode haver problemas ao salvar");
    }
    
    bool saved = preferences.putString("config", configJson);
    preferences.end();
    
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
        }
    }
    
    // Salva a configuração padrão
    bool saved = saveConfig();
    
    if (saved) {
        Serial.println("Configuração resetada e salva com sucesso");
        return true;
    } else {
        Serial.println("ERRO: Falha ao salvar configuração padrão após reset");
        return false;
    }
}

