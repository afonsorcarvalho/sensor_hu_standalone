/**
 * @file rtc_manager.cpp
 * @brief Implementação das funções RTC e NTP
 */

#include "rtc_manager.h"
#include "config.h"
#include "config_storage.h"
#include "console.h"
#include <WiFi.h>
#include <time.h>

// Variáveis globais
WiFiUDP ntpUDP;
bool rtcInitialized = false;
unsigned long lastNtpSync = 0;
const unsigned long NTP_SYNC_INTERVAL = 3600000;  // Sincronizar NTP a cada 1 hora

uint32_t getCurrentEpochTime() {
    if (!config.rtc.enabled || !rtcInitialized || config.rtc.epochTime == 0) {
        return 0;
    }
    
    // Calcula tempo decorrido desde bootTime
    unsigned long currentMillis = millis();
    unsigned long elapsedMillis = currentMillis - config.rtc.bootTime;
    unsigned long elapsedSeconds = elapsedMillis / 1000;
    
    // Retorna epoch time atual
    return config.rtc.epochTime + elapsedSeconds;
}

void formatDateTime(uint32_t epoch, char* dateStr, char* timeStr, int8_t timezone) {
    if (epoch == 0) {
        strcpy(dateStr, "0000-00-00");
        strcpy(timeStr, "00:00:00");
        return;
    }
    
    // Aplica fuso horário
    epoch += (timezone * 3600);
    
    // Calcula data e hora
    time_t rawTime = (time_t)epoch;
    struct tm* timeInfo = gmtime(&rawTime);
    
    // Formata data: YYYY-MM-DD
    snprintf(dateStr, 11, "%04d-%02d-%02d", 
             timeInfo->tm_year + 1900, 
             timeInfo->tm_mon + 1, 
             timeInfo->tm_mday);
    
    // Formata hora: HH:MM:SS
    snprintf(timeStr, 9, "%02d:%02d:%02d", 
             timeInfo->tm_hour, 
             timeInfo->tm_min, 
             timeInfo->tm_sec);
}

bool syncNTP() {
    if (!config.rtc.enabled || !config.rtc.ntpEnabled) {
        return false;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi nao conectado, nao e possivel sincronizar NTP");
        String logMsg = "[NTP] Erro: WiFi nao conectado, impossivel sincronizar\r\n";
        consolePrint(logMsg);
        return false;
    }
    
    Serial.println("Sincronizando com NTP...");
    Serial.print("Servidor: ");
    Serial.println(config.rtc.ntpServer);
    
    // Log no console web
    String logMsg = "[NTP] Iniciando sincronizacao com " + String(config.rtc.ntpServer) + "...\r\n";
    consolePrint(logMsg);
    
    // Configura servidor NTP
    configTime(config.rtc.timezone * 3600, 0, config.rtc.ntpServer);
    
    // Aguarda sincronização (até 10 segundos)
    int attempts = 0;
    while (time(nullptr) < 1000000000 && attempts < 20) {
        delay(500);
        attempts++;
    }
    
    time_t now = time(nullptr);
    
    if (now > 1000000000) {
        // Sincronização bem-sucedida
        config.rtc.epochTime = (uint32_t)now;
        config.rtc.bootTime = millis();
        
        // Salva na configuração
        saveConfig();
        
        rtcInitialized = true;
        lastNtpSync = millis();
        
        Serial.print("NTP sincronizado: ");
        Serial.println(now);
        
        char dateStr[11];
        char timeStr[9];
        formatDateTime(config.rtc.epochTime, dateStr, timeStr, config.rtc.timezone);
        
        Serial.print("Hora local: ");
        Serial.print(dateStr);
        Serial.print(" ");
        Serial.println(timeStr);
        
        // Log no console web
        String logMsg = "[NTP] Sincronizacao bem-sucedida com " + String(config.rtc.ntpServer) + "\r\n";
        logMsg += "[NTP] Data/Hora: " + String(dateStr) + " " + String(timeStr) + "\r\n";
        consolePrint(logMsg);
        
        return true;
    } else {
        Serial.println("Falha ao sincronizar NTP");
        
        // Log de erro no console web
        String logMsg = "[NTP] Erro: Falha ao sincronizar com " + String(config.rtc.ntpServer) + "\r\n";
        consolePrint(logMsg);
        
        return false;
    }
}

