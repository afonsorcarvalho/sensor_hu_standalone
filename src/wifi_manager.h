/**
 * @file wifi_manager.h
 * @brief Funções para gerenciamento WiFi
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"

/**
 * @brief Configura o WiFi em modo Access Point
 */
void setupWiFiAP();

/**
 * @brief Configura o WiFi em modo Station (conecta a uma rede)
 * @return true se conectou com sucesso, false caso contrário
 */
bool setupWiFiSTA();

#endif // WIFI_MANAGER_H

