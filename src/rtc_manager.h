/**
 * @file rtc_manager.h
 * @brief Funções para gerenciamento RTC e NTP
 */

#ifndef RTC_MANAGER_H
#define RTC_MANAGER_H

#include <Arduino.h>
#include <WiFiUdp.h>
#include "config.h"

// Variáveis globais externas
extern WiFiUDP ntpUDP;
extern bool rtcInitialized;
extern unsigned long lastNtpSync;
extern const unsigned long NTP_SYNC_INTERVAL;

/**
 * @brief Obtém o tempo atual em epoch (Unix timestamp)
 * @return Epoch time atual ou 0 se RTC não inicializado
 */
uint32_t getCurrentEpochTime();

/**
 * @brief Formata epoch time em data e hora
 * @param epoch Epoch time (Unix timestamp)
 * @param dateStr Buffer para string de data (YYYY-MM-DD)
 * @param timeStr Buffer para string de hora (HH:MM:SS)
 * @param timezone Fuso horário (UTC offset)
 */
void formatDateTime(uint32_t epoch, char* dateStr, char* timeStr, int8_t timezone);

/**
 * @brief Sincroniza com servidor NTP
 * @return true se sincronização foi bem-sucedida
 */
bool syncNTP();

#endif // RTC_MANAGER_H

