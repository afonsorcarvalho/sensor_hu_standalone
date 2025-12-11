/**
 * @file web_server.h
 * @brief Handlers HTTP do servidor web usando AsyncWebServer
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <LittleFS.h>
#include "config.h"

// Variáveis globais externas
extern AsyncWebServer server;
extern AsyncWebSocket* consoleWebSocket;

/**
 * @brief Inicializa o sistema de arquivos LittleFS
 * @return true se inicializado com sucesso
 */
bool initLittleFS();

/**
 * @brief Configura o servidor web e rotas
 */
void setupWebServer();

/**
 * @brief Handler para página principal (HTML)
 */
void handleRoot(AsyncWebServerRequest *request);

/**
 * @brief Handler para obter configuração atual (GET /api/config)
 */
void handleGetConfig(AsyncWebServerRequest *request);

/**
 * @brief Handler para salvar configuração (POST /api/config)
 */
void handleSaveConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len);

/**
 * @brief Handler para leitura manual de registros (GET /api/read)
 */
void handleReadRegisters(AsyncWebServerRequest *request);

/**
 * @brief Handler para reboot do sistema (POST /api/reboot)
 */
void handleReboot(AsyncWebServerRequest *request);

/**
 * @brief Handler para obter horário atual do RTC (GET /api/rtc/current)
 */
void handleGetCurrentTime(AsyncWebServerRequest *request);

/**
 * @brief Handler para definir data/hora manualmente (POST /api/rtc/set)
 */
void handleSetTime(AsyncWebServerRequest *request, uint8_t *data, size_t len);

/**
 * @brief Handler para sincronizar NTP agora (POST /api/rtc/sync)
 */
void handleSyncNTP(AsyncWebServerRequest *request);

/**
 * @brief Handler para scan de redes WiFi (GET /api/wifi/scan)
 */
void handleWiFiScan(AsyncWebServerRequest *request);

/**
 * @brief Restaura o estado WiFi original após operações como scan
 * @param originalMode Modo WiFi original (AP, STA, AP_STA)
 * @param wasConnected Se estava conectado a uma rede antes
 * @param originalSSID SSID da rede original (se estava conectado)
 */
void restoreWiFiState(WiFiMode_t originalMode, bool wasConnected, const String& originalSSID);

/**
 * @brief Handler para testar cálculo/expressão (POST /api/calc/test)
 */
void handleTestCalculation(AsyncWebServerRequest *request, uint8_t *data, size_t len);

/**
 * @brief Handler para obter variáveis disponíveis (GET /api/calc/variables)
 */
void handleGetVariables(AsyncWebServerRequest *request);

/**
 * @brief Handler para exportar configurações (GET /api/config/export)
 */
void handleExportConfig(AsyncWebServerRequest *request);

/**
 * @brief Handler para importar configurações (POST /api/config/import)
 */
void handleImportConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len);

/**
 * @brief Handler para resetar configurações para valores padrão (POST /api/config/reset)
 */
void handleResetConfig(AsyncWebServerRequest *request);

/**
 * @brief Handler para escrever valor de variável (POST /api/variable/write)
 */
void handleWriteVariable(AsyncWebServerRequest *request, uint8_t *data, size_t len);

#endif // WEB_SERVER_H

