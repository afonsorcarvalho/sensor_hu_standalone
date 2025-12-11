/**
 * @file kalman_filter.cpp
 * @brief Implementação do filtro de Kalman 1D
 */

#include "kalman_filter.h"

// Valores padrão do filtro de Kalman (usados apenas se não especificados)
#define KALMAN_Q_DEFAULT 0.01f  // Process noise padrão
#define KALMAN_R_DEFAULT 0.1f   // Measurement noise padrão

void kalmanInit(KalmanState* state, float initialValue) {
    if (!state) return;
    
    state->estimate = initialValue;
    state->errorCov = 1.0f;  // Covariância inicial
    state->initialized = true;
}

float kalmanFilter(KalmanState* state, float measurement, float Q, float R) {
    if (!state) return measurement;
    
    // Valida e usa valores padrão se parâmetros inválidos
    if (Q <= 0.0f) Q = KALMAN_Q_DEFAULT;
    if (R <= 0.0f) R = KALMAN_R_DEFAULT;
    
    // Se não foi inicializado, inicializa com o valor medido
    if (!state->initialized) {
        kalmanInit(state, measurement);
        return measurement;
    }
    
    // Predição (Prediction)
    // Neste caso simples, assumimos que o valor não muda (modelo constante)
    // estimate_k = estimate_k-1 (sem mudança esperada)
    // errorCov_k = errorCov_k-1 + Q (aumenta incerteza)
    float predErrorCov = state->errorCov + Q;
    
    // Atualização (Update)
    // Ganho de Kalman: quanto confiar na nova medição
    float kalmanGain = predErrorCov / (predErrorCov + R);
    
    // Nova estimativa: combina predição e medição
    state->estimate = state->estimate + kalmanGain * (measurement - state->estimate);
    
    // Atualiza covariância do erro
    state->errorCov = (1.0f - kalmanGain) * predErrorCov;
    
    return state->estimate;
}

void kalmanReset(KalmanState* state) {
    if (!state) return;
    
    state->initialized = false;
    state->estimate = 0.0f;
    state->errorCov = 1.0f;
}

