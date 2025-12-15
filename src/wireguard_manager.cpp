/**
 * @file wireguard_manager.cpp
 * @brief Implementação das funções WireGuard VPN
 */

#include "wireguard_manager.h"
#include "console.h"
#include "rtc_manager.h"
#include <WireGuard-ESP32.h>
#include <time.h>

// Instância global do WireGuard
WireGuard wg;

bool isWireGuardConnected() {
    // Verifica se WireGuard está ativo
    // A biblioteca não expõe método direto, então verificamos se está configurado
    return config.wireguard.enabled && WiFi.status() == WL_CONNECTED;
}

bool setupWireGuard() {
    // Verifica se WireGuard está habilitado
    if (!config.wireguard.enabled) {
        Serial.println("[WireGuard] Desabilitado na configuracao");
        return false;
    }
    
    // Verifica se WiFi está conectado
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WireGuard] WiFi nao conectado, nao e possivel conectar VPN");
        String logMsg = "[WireGuard] Erro: WiFi nao conectado\r\n";
        consolePrint(logMsg);
        return false;
    }
    
    // Verifica se NTP está sincronizado (WireGuard requer tempo preciso)
    time_t now = time(nullptr);
    if (now < 1000000000) {  // Data muito antiga indica que NTP não sincronizou
        Serial.println("[WireGuard] NTP nao sincronizado, aguardando...");
        String logMsg = "[WireGuard] Aviso: NTP nao sincronizado. Aguardando sincronizacao...\r\n";
        consolePrint(logMsg);
        
        // Aguarda até 10 segundos por sincronização NTP
        int attempts = 0;
        while (time(nullptr) < 1000000000 && attempts < 20) {
            delay(500);
            attempts++;
            now = time(nullptr);
        }
        
        if (now < 1000000000) {
            Serial.println("[WireGuard] NTP nao sincronizado apos espera, cancelando conexao VPN");
            String logMsg = "[WireGuard] Erro: NTP nao sincronizado, impossivel conectar VPN\r\n";
            consolePrint(logMsg);
            return false;
        }
    }
    
    Serial.println("[WireGuard] Iniciando conexao VPN...");
    String logMsg = "[WireGuard] Iniciando conexao com servidor " + String(config.wireguard.serverAddress) + ":" + String(config.wireguard.serverPort) + "...\r\n";
    consolePrint(logMsg);
    
    // Valida configuração
    if (strlen(config.wireguard.privateKey) == 0) {
        Serial.println("[WireGuard] Erro: Chave privada nao configurada");
        String logMsg = "[WireGuard] Erro: Chave privada nao configurada\r\n";
        consolePrint(logMsg);
        return false;
    }
    
    if (strlen(config.wireguard.publicKey) == 0) {
        Serial.println("[WireGuard] Erro: Chave publica do servidor nao configurada");
        String logMsg = "[WireGuard] Erro: Chave publica do servidor nao configurada\r\n";
        consolePrint(logMsg);
        return false;
    }
    
    if (strlen(config.wireguard.serverAddress) == 0) {
        Serial.println("[WireGuard] Erro: Endereco do servidor nao configurado");
        String logMsg = "[WireGuard] Erro: Endereco do servidor nao configurado\r\n";
        consolePrint(logMsg);
        return false;
    }
    
    // Inicializa WireGuard
    // begin(localIP, privateKey, serverAddress, publicKey, serverPort)
    bool success = wg.begin(
        config.wireguard.localIP,
        config.wireguard.privateKey,
        config.wireguard.serverAddress,
        config.wireguard.publicKey,
        config.wireguard.serverPort
    );
    
    if (success) {
        Serial.println("[WireGuard] Conectado com sucesso!");
        Serial.print("[WireGuard] IP Local na VPN: ");
        Serial.println(config.wireguard.localIP);
        Serial.print("[WireGuard] Servidor: ");
        Serial.print(config.wireguard.serverAddress);
        Serial.print(":");
        Serial.println(config.wireguard.serverPort);
        
        logMsg = "[WireGuard] Conectado com sucesso!\r\n";
        logMsg += "[WireGuard] IP Local: " + config.wireguard.localIP.toString() + "\r\n";
        logMsg += "[WireGuard] Servidor: " + String(config.wireguard.serverAddress) + ":" + String(config.wireguard.serverPort) + "\r\n";
        consolePrint(logMsg);
        
        return true;
    } else {
        Serial.println("[WireGuard] Falha ao conectar");
        String logMsg = "[WireGuard] Erro: Falha ao conectar com servidor\r\n";
        consolePrint(logMsg);
        return false;
    }
}

void disconnectWireGuard() {
    if (config.wireguard.enabled) {
        Serial.println("[WireGuard] Desconectando...");
        wg.end();
        String logMsg = "[WireGuard] Desconectado\r\n";
        consolePrint(logMsg);
    }
}

String getWireGuardStatus() {
    String status = "";
    
    if (!config.wireguard.enabled) {
        status = "Desabilitado";
    } else if (WiFi.status() != WL_CONNECTED) {
        status = "Aguardando WiFi";
    } else if (time(nullptr) < 1000000000) {
        status = "Aguardando NTP";
    } else if (isWireGuardConnected()) {
        status = "Conectado - IP: " + config.wireguard.localIP.toString();
    } else {
        status = "Desconectado";
    }
    
    return status;
}
