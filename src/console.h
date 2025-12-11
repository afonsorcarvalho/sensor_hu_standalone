/**
 * @file console.h
 * @brief Console WebSocket para interação com o sistema
 */

#ifndef CONSOLE_H
#define CONSOLE_H

#include <Arduino.h>
#include <AsyncWebSocket.h>

// Variáveis globais externas
extern AsyncWebSocket* webSocket;
extern String consoleBuffer;

/**
 * @brief Imprime mensagem no console WebSocket
 * @param message Mensagem a ser impressa
 */
void consolePrint(String message);

/**
 * @brief Processa comando do console
 * @param client Cliente WebSocket
 * @param command Comando a ser processado
 */
void processConsoleCommand(AsyncWebSocketClient* client, String command);

/**
 * @brief Inicializa o WebSocket do console
 */
void initConsoleWebSocket(AsyncWebSocket* ws);

#endif // CONSOLE_H

