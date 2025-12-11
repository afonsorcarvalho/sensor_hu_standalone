/**
 * @file kalman_filter.h
 * @brief Implementação simples do filtro de Kalman 1D para suavização de valores
 */

#ifndef KALMAN_FILTER_H
#define KALMAN_FILTER_H

#include <Arduino.h>

/**
 * @struct KalmanState
 * @brief Estado interno do filtro de Kalman para um registro
 */
struct KalmanState {
    float estimate;      // Estimativa atual do valor
    float errorCov;      // Covariância do erro
    bool initialized;    // true se o filtro foi inicializado
};

/**
 * @brief Inicializa o estado do filtro de Kalman
 * @param state Ponteiro para o estado do filtro
 * @param initialValue Valor inicial para o filtro
 */
void kalmanInit(KalmanState* state, float initialValue);

/**
 * @brief Aplica o filtro de Kalman a um novo valor
 * @param state Ponteiro para o estado do filtro
 * @param measurement Valor medido (novo valor lido)
 * @param Q Process noise (ruído do processo) - quanto o valor real pode variar
 * @param R Measurement noise (ruído da medição) - quanto o sensor é preciso
 * @return Valor filtrado (suavizado)
 * 
 * Parâmetros do filtro:
 * - Q (process noise): ruído do processo (quanto o valor real pode variar)
 *   - Valores menores (ex: 0.001) = filtro mais responsivo (menos suavização)
 *   - Valores maiores (ex: 0.1) = filtro mais suave (mais suavização)
 * - R (measurement noise): ruído da medição (quanto o sensor é preciso)
 *   - Valores menores (ex: 0.01) = confia mais nas medições (menos suavização)
 *   - Valores maiores (ex: 1.0) = confia menos nas medições (mais suavização)
 * 
 * Valores padrão recomendados: Q = 0.01, R = 0.1
 */
float kalmanFilter(KalmanState* state, float measurement, float Q, float R);

/**
 * @brief Reseta o filtro de Kalman (útil quando o filtro é desabilitado e reabilitado)
 * @param state Ponteiro para o estado do filtro
 */
void kalmanReset(KalmanState* state);

#endif // KALMAN_FILTER_H

