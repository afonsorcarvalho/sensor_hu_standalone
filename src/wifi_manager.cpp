/**
 * @file wifi_manager.cpp
 * @brief Implementação das funções WiFi
 */

#include "wifi_manager.h"
#include "config.h"

void setupWiFiAP() {
    Serial.println("Configurando Access Point...");
    
    // Usa SSID e senha da configuração, ou valores padrão
    const char* apSSID = (strlen(config.wifi.apSSID) > 0) ? config.wifi.apSSID : AP_SSID;
    const char* apPassword = (strlen(config.wifi.apPassword) > 0) ? config.wifi.apPassword : AP_PASSWORD;
    
    // Configura o Access Point
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSSID, apPassword);
    
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);
    Serial.print("SSID: ");
    Serial.println(apSSID);
    Serial.print("Password: ");
    Serial.println(apPassword);
}

bool setupWiFiSTA() {
    // Verifica se há configuração STA válida
    if (strlen(config.wifi.staSSID) == 0) {
        Serial.println("[WiFi] SSID STA nao configurado - usando modo AP");
        return false;
    }
    
    Serial.println("[WiFi] Tentando conectar no modo Station (STA)...");
    Serial.print("[WiFi] SSID: '");
    Serial.print(config.wifi.staSSID);
    Serial.print("', Senha length: ");
    Serial.println(strlen(config.wifi.staPassword));
    
    // Configura modo Station
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.wifi.staSSID, config.wifi.staPassword);
    
    // Tenta conectar até 3 vezes (conforme solicitado)
    const int maxAttempts = 3;
    const int timeoutPerAttempt = 10000; // 10 segundos por tentativa
    
    for (int attempt = 1; attempt <= maxAttempts; attempt++) {
        Serial.print("[WiFi] Tentativa ");
        Serial.print(attempt);
        Serial.print(" de ");
        Serial.print(maxAttempts);
        Serial.print(" (timeout: ");
        Serial.print(timeoutPerAttempt / 1000);
        Serial.println("s)...");
        
        unsigned long startTime = millis();
        int dotCount = 0;
        while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeoutPerAttempt) {
            delay(500);
            Serial.print(".");
            dotCount++;
            if (dotCount % 20 == 0) {
                Serial.println();
            }
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
            IPAddress IP = WiFi.localIP();
            Serial.println("[WiFi] Conectado com sucesso!");
            Serial.print("[WiFi] IP: ");
            Serial.println(IP);
            Serial.print("[WiFi] Gateway: ");
            Serial.println(WiFi.gatewayIP());
            Serial.print("[WiFi] Subnet: ");
            Serial.println(WiFi.subnetMask());
            
            return true;
        } else {
            Serial.print("[WiFi] Falha na tentativa ");
            Serial.print(attempt);
            Serial.print(" - Status: ");
            Serial.println(WiFi.status());
            
            if (attempt < maxAttempts) {
                Serial.println("[WiFi] Aguardando 1 segundo antes da proxima tentativa...");
                WiFi.disconnect();
                delay(1000);
                Serial.println("[WiFi] Tentando novamente...");
                WiFi.begin(config.wifi.staSSID, config.wifi.staPassword);
            }
        }
    }
    
    Serial.println("[WiFi] Nao foi possivel conectar apos 3 tentativas");
    Serial.println("[WiFi] Voltando para modo AP (fallback)");
    
    return false;
}

