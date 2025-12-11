/**
 * @file config_storage.h
 * @brief Funções para persistência de configuração
 */

#ifndef CONFIG_STORAGE_H
#define CONFIG_STORAGE_H

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

// Variável global externa
extern Preferences preferences;

/**
 * @brief Carrega configuração da EEPROM/Preferences
 */
void loadConfig();

/**
 * @brief Salva configuração na EEPROM/Preferences
 * @return true se salvou com sucesso, false caso contrário
 */
bool saveConfig();

#endif // CONFIG_STORAGE_H

