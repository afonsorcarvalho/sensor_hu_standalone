/**
 * @file expression_parser.h
 * @brief Interpretador simples de expressões matemáticas com suporte a variáveis
 */

#ifndef EXPRESSION_PARSER_H
#define EXPRESSION_PARSER_H

#include <Arduino.h>

// Estrutura para armazenar uma variável (mantida para compatibilidade)
struct Variable {
    char name[32];
    double value;
};

// Estrutura para armazenar valores dos dispositivos em formato bidimensional
// d[deviceIndex][registerIndex] = valor processado
struct DeviceValues {
    double** values;      // Array bidimensional de valores
    int deviceCount;      // Número de dispositivos
    int* registerCounts; // Número de registros por dispositivo
};

/**
 * @brief Substitui {d[i][j]} na expressão pelos valores correspondentes
 * @param expression Expressão original (ex: "{d[0][0]} * 2 + {d[0][1]}")
 * @param deviceValues Estrutura com valores dos dispositivos
 * @param output Buffer para expressão processada
 * @param outputSize Tamanho do buffer de saída
 * @param errorMsg Buffer para mensagem de erro (opcional)
 * @param errorMsgSize Tamanho do buffer de erro
 * @return true se a substituição foi bem-sucedida, false caso contrário
 */
bool substituteDeviceValues(const char* expression, DeviceValues* deviceValues, char* output, size_t outputSize, char* errorMsg = nullptr, size_t errorMsgSize = 0);

/**
 * @brief Avalia uma expressão matemática com variáveis
 * @param expression Expressão a ser avaliada (ex: "a + b * 2")
 * @param variables Array de variáveis disponíveis
 * @param varCount Número de variáveis no array
 * @param result Ponteiro para armazenar o resultado
 * @param errorMsg Buffer para mensagem de erro (opcional)
 * @param errorMsgSize Tamanho do buffer de erro
 * @return true se a expressão foi avaliada com sucesso, false caso contrário
 */
bool evaluateExpression(const char* expression, Variable* variables, int varCount, double* result, char* errorMsg = nullptr, size_t errorMsgSize = 0);

/**
 * @brief Encontra o valor de uma variável pelo nome
 * @param varName Nome da variável
 * @param variables Array de variáveis
 * @param varCount Número de variáveis
 * @return Valor da variável ou 0.0 se não encontrada
 */
double getVariableValue(const char* varName, Variable* variables, int varCount);

/**
 * @brief Estrutura para armazenar informações de atribuição
 * Exemplo: {d[1][0]}={d[2][0]} + exp({d[1][0]})
 */
struct AssignmentInfo {
    bool hasAssignment;      // true se a expressão contém atribuição (=)
    int targetDeviceIndex;   // Índice do dispositivo de destino
    int targetRegisterIndex; // Índice do registro de destino
    char* expression;        // Expressão do segundo membro (após o =)
    size_t expressionSize;  // Tamanho alocado para expression
};

/**
 * @brief Processa expressão com atribuição, separando destino e expressão
 * @param expression Expressão completa (ex: "{d[1][0]}={d[2][0]} + exp({d[1][0]})")
 * @param assignmentInfo Estrutura para armazenar informações da atribuição
 * @param errorMsg Buffer para mensagem de erro (opcional)
 * @param errorMsgSize Tamanho do buffer de erro
 * @return true se processamento foi bem-sucedido, false caso contrário
 */
bool parseAssignment(const char* expression, AssignmentInfo* assignmentInfo, char* errorMsg = nullptr, size_t errorMsgSize = 0);

/**
 * @brief Libera memória alocada em AssignmentInfo
 * @param assignmentInfo Estrutura a ser liberada
 */
void freeAssignmentInfo(AssignmentInfo* assignmentInfo);

#endif // EXPRESSION_PARSER_H

