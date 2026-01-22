/**
 * @file config.cpp
 * @brief Implementação das variáveis globais de configuração
 */

#include "config.h"

// Variável global de configuração do sistema
SystemConfig config;

// Mutex recursivo global para proteger acesso ao config entre tasks
SemaphoreHandle_t g_configMutex = nullptr;

// Flags globais para coordenar pausa do ciclo (evita WDT e concorrência durante save/import/reset)
volatile bool g_processingPaused = false;
volatile bool g_cycleInProgress = false;

void initConfigMutex() {
    if (g_configMutex == nullptr) {
        // Mutex recursivo permite lock aninhado com segurança
        g_configMutex = xSemaphoreCreateRecursiveMutex();
    }
}

bool lockConfig(TickType_t timeoutTicks) {
    if (g_configMutex == nullptr) {
        initConfigMutex();
        if (g_configMutex == nullptr) {
            return false;
        }
    }
    return (xSemaphoreTakeRecursive(g_configMutex, timeoutTicks) == pdTRUE);
}

void unlockConfig() {
    if (g_configMutex != nullptr) {
        xSemaphoreGiveRecursive(g_configMutex);
    }
}
