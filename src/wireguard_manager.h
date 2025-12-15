/**
 * @file wireguard_manager.h
 * @brief Funções para gerenciamento WireGuard VPN
 */

#ifndef WIREGUARD_MANAGER_H
#define WIREGUARD_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"

/**
 * @brief Inicializa e conecta ao WireGuard VPN
 * @return true se conectou com sucesso, false caso contrário
 * 
 * Requisitos:
 * - WiFi deve estar conectado
 * - NTP deve estar sincronizado (time() deve retornar valor válido)
 * - Configuração WireGuard deve estar válida
 */
bool setupWireGuard();

/**
 * @brief Desconecta do WireGuard VPN
 */
void disconnectWireGuard();

/**
 * @brief Verifica se WireGuard está conectado
 * @return true se conectado, false caso contrário
 */
bool isWireGuardConnected();

/**
 * @brief Obtém o status atual do WireGuard
 * @return String com informações de status
 */
String getWireGuardStatus();

#endif // WIREGUARD_MANAGER_H
