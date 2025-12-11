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
        Serial.println("SSID STA nao configurado");
        return false;
    }
    
    Serial.println("Tentando conectar no modo Station...");
    Serial.print("SSID: ");
    Serial.println(config.wifi.staSSID);
    
    // Configura modo Station
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.wifi.staSSID, config.wifi.staPassword);
    
    // Tenta conectar até 3 vezes
    const int maxAttempts = 3;
    const int timeoutPerAttempt = 10000; // 10 segundos por tentativa
    
    for (int attempt = 1; attempt <= maxAttempts; attempt++) {
        Serial.print("Tentativa ");
        Serial.print(attempt);
        Serial.print(" de ");
        Serial.println(maxAttempts);
        
        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeoutPerAttempt) {
            delay(500);
            Serial.print(".");
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
            IPAddress IP = WiFi.localIP();
            Serial.println("Conectado com sucesso!");
            Serial.print("IP: ");
            Serial.println(IP);
            
            return true;
        } else {
            Serial.print("Falha na tentativa ");
            Serial.println(attempt);
            
            if (attempt < maxAttempts) {
                Serial.println("Tentando novamente...");
                WiFi.disconnect();
                delay(1000);
                WiFi.begin(config.wifi.staSSID, config.wifi.staPassword);
            }
        }
    }
    
    Serial.println("Nao foi possivel conectar apos 3 tentativas");
    
    return false;
}

