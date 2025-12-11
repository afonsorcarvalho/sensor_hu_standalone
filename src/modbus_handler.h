/**
 * @file modbus_handler.h
 * @brief Funções para comunicação Modbus RTU
 */

#ifndef MODBUS_HANDLER_H
#define MODBUS_HANDLER_H

#include <Arduino.h>
#include <ModbusMaster.h>
#include "config.h"
#include "kalman_filter.h"

// Variáveis globais externas
extern ModbusMaster node;
extern uint32_t currentBaudRate;
extern KalmanState kalmanStates[MAX_DEVICES][MAX_REGISTERS_PER_DEVICE];

/**
 * @brief Callback antes da transmissão Modbus (habilita transmissão RS485)
 */
void preTransmission();

/**
 * @brief Callback após transmissão Modbus (habilita recepção RS485)
 */
void postTransmission();

/**
 * @brief Configura a comunicação Modbus RTU
 * @param baudRate Velocidade de comunicação (se 0, usa o valor da configuração)
 */
void setupModbus(uint32_t baudRate = 0);

/**
 * @brief Lê todos os registros de todos os dispositivos configurados
 */
void readAllDevices();

/**
 * @brief Escreve valores em registros de saída
 */
void writeOutputRegisters();

#endif // MODBUS_HANDLER_H

