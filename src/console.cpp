/**
 * @file console.cpp
 * @brief Implementação do console WebSocket usando AsyncWebSocket
 */

#include "console.h"
#include "config.h"
#include "modbus_handler.h"
#include <WiFi.h>
#include <ESP.h>

// Variáveis globais
AsyncWebSocket* webSocket = nullptr;
String consoleBuffer = "";

/**
 * @brief Handler de eventos do WebSocket
 */
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        // Cliente conectado
        IPAddress ip = client->remoteIP();
        Serial.printf("[WebSocket] Cliente conectado de %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
        
        // Envia mensagem de boas-vindas
        client->text("=== Console Modbus RTU Master ===\r\n");
        client->text("Digite 'help' para ver comandos disponiveis.\r\n");
        
        // Envia buffer de mensagens acumuladas
        if (consoleBuffer.length() > 0) {
            client->text(consoleBuffer);
        }
    }
    else if (type == WS_EVT_DISCONNECT) {
        // Cliente desconectado
        Serial.printf("[WebSocket] Cliente desconectado\n");
    }
    else if (type == WS_EVT_DATA) {
        // Dados recebidos
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len) {
            // Mensagem completa recebida
            if (info->opcode == WS_TEXT) {
                String command = String((char*)data);
                command.trim();
                
                if (command.length() > 0) {
                    client->text("> " + command + "\r\n");
                    processConsoleCommand(client, command);
                }
            }
        }
    }
    else if (type == WS_EVT_ERROR) {
        // Erro no WebSocket
        Serial.printf("[WebSocket] Erro\n");
    }
}

void processConsoleCommand(AsyncWebSocketClient* client, String command) {
    if (!webSocket || !client) {
        return;
    }
    
    if (command == "help") {
        client->text("=== Comandos Disponiveis ===\r\n");
        client->text("help     - Mostra esta ajuda\r\n");
        client->text("status   - Status do sistema\r\n");
        client->text("reboot   - Reinicia o ESP32\r\n");
        client->text("heap     - Memoria livre\r\n");
        client->text("uptime   - Tempo de funcionamento\r\n");
        client->text("config   - Mostra configuracoes\r\n");
        client->text("modbus   - Status Modbus\r\n");
    }
    else if (command == "status") {
        client->text("=== Status do Sistema ===\r\n");
        if (WiFi.status() == WL_CONNECTED) {
            client->text("Modo: Station (STA)\r\n");
            client->text("IP: " + WiFi.localIP().toString() + "\r\n");
        } else {
            client->text("Modo: Access Point (AP)\r\n");
            client->text("IP: " + WiFi.softAPIP().toString() + "\r\n");
        }
        client->text("Dispositivos Modbus: " + String(config.deviceCount) + "\r\n");
        client->text("Baud Rate: " + String(config.baudRate) + "\r\n");
        client->text("MQTT: " + String(config.mqtt.enabled ? "Habilitado" : "Desabilitado") + "\r\n");
        client->text("RTC: " + String(config.rtc.enabled ? "Habilitado" : "Desabilitado") + "\r\n");
    }
    else if (command == "reboot") {
        client->text("Reiniciando em 2 segundos...\r\n");
        if (webSocket) {
            webSocket->textAll("Sistema reiniciando...\r\n");
        }
        delay(2000);
        ESP.restart();
    }
    else if (command == "heap") {
        client->text("Memoria livre: " + String(ESP.getFreeHeap()) + " bytes\r\n");
        client->text("Memoria total: " + String(ESP.getHeapSize()) + " bytes\r\n");
    }
    else if (command == "uptime") {
        unsigned long seconds = millis() / 1000;
        unsigned long minutes = seconds / 60;
        unsigned long hours = minutes / 60;
        unsigned long days = hours / 24;
        
        String uptime = "Uptime: ";
        if (days > 0) uptime += String(days) + "d ";
        if (hours > 0) uptime += String(hours % 24) + "h ";
        if (minutes > 0) uptime += String(minutes % 60) + "m ";
        uptime += String(seconds % 60) + "s\r\n";
        client->text(uptime);
    }
    else if (command == "config") {
        client->text("=== Configuracoes ===\r\n");
        client->text("Baud Rate: " + String(config.baudRate) + "\r\n");
        client->text("Dispositivos: " + String(config.deviceCount) + "\r\n");
        client->text("WiFi Mode: " + String(config.wifi.mode) + "\r\n");
    }
    else if (command == "modbus") {
        client->text("=== Status Modbus ===\r\n");
        client->text("Baud Rate: " + String(currentBaudRate) + "\r\n");
        client->text("Dispositivos configurados: " + String(config.deviceCount) + "\r\n");
        for (int i = 0; i < config.deviceCount; i++) {
            String msg = "  Dispositivo " + String(config.devices[i].slaveAddress) + ": ";
            msg += config.devices[i].enabled ? "Ativo" : "Inativo";
            msg += "\r\n";
            client->text(msg);
        }
    }
    else {
        client->text("Comando desconhecido. Digite 'help' para ver comandos disponiveis.\r\n");
    }
}

void initConsoleWebSocket(AsyncWebSocket* ws) {
    webSocket = ws;
    if (webSocket) {
        webSocket->onEvent(onWebSocketEvent);
        Serial.println("Console WebSocket inicializado na porta 81");
    }
}

void consolePrint(String message) {
    // Imprime na Serial também para debug
    Serial.print(message);
    // Força envio imediato do buffer Serial para garantir que apareça no monitor
    // Isso é importante porque o buffer pode não ser enviado automaticamente
    if (message.indexOf('\n') >= 0 || message.length() > 50) {
        Serial.flush(); // Flush quando há quebra de linha ou mensagens grandes
    }
    
    // Adiciona mensagem ao buffer
    consoleBuffer += message;
    
    // Envia para todos os clientes conectados
    // IMPORTANTE: textAll() é não bloqueante, mas yield() ajuda o WebSocket processar
    if (webSocket) {
        webSocket->textAll(message);
        // Pequeno yield para permitir que o WebSocket processe a mensagem
        // Isso evita que o buffer do WebSocket encha e cause desconexão
        yield();
    }
    
    // Limita o tamanho do buffer
    if (consoleBuffer.length() > 2000) {
        consoleBuffer = consoleBuffer.substring(consoleBuffer.length() - 1000);
    }
}
