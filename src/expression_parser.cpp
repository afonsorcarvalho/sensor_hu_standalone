/**
 * @file expression_parser.cpp
 * @brief Implementação do interpretador de expressões matemáticas
 */

#include "expression_parser.h"
#include <math.h>
#include <ctype.h>
#include <string.h>

// Função auxiliar para pular espaços
static void skipSpaces(const char** expr) {
    while (**expr == ' ' || **expr == '\t') {
        (*expr)++;
    }
}

// Função auxiliar para ler um número
static double parseNumber(const char** expr, bool* success) {
    *success = false;
    skipSpaces(expr);
    
    if (**expr == '\0') return 0.0;
    
    char* end;
    double value = strtod(*expr, &end);
    
    if (end == *expr) {
        return 0.0;
    }
    
    *expr = end;
    *success = true;
    return value;
}

// Função auxiliar para ler um identificador (nome de variável)
static String parseIdentifier(const char** expr) {
    skipSpaces(expr);
    String ident = "";
    
    if (isalpha(**expr) || **expr == '_') {
        while (isalnum(**expr) || **expr == '_') {
            ident += **expr;
            (*expr)++;
        }
    }
    
    return ident;
}

// Função auxiliar para ler uma variável
static double parseVariable(const char** expr, Variable* variables, int varCount, bool* success) {
    *success = false;
    skipSpaces(expr);
    
    String varName = parseIdentifier(expr);
    if (varName.length() == 0) {
        return 0.0;
    }
    
    for (int i = 0; i < varCount; i++) {
        if (strcmp(variables[i].name, varName.c_str()) == 0) {
            *success = true;
            return variables[i].value;
        }
    }
    
    return 0.0;
}

// Função auxiliar para avaliar uma expressão (recursiva)
static double evalExpression(const char** expr, Variable* variables, int varCount, bool* success, char* errorMsg, size_t errorMsgSize);

// Função para avaliar comparações (>, <, >=, <=, ==, !=)
static double evalComparison(const char** expr, Variable* variables, int varCount, bool* success, char* errorMsg, size_t errorMsgSize);

// Função para avaliar um termo (multiplicação, divisão, módulo)
static double evalTerm(const char** expr, Variable* variables, int varCount, bool* success, char* errorMsg, size_t errorMsgSize) {
    double result = 0.0;
    bool termSuccess = false;
    
    skipSpaces(expr);
    
    // Verifica se é um número
    if (isdigit(**expr) || **expr == '.' || **expr == '-' || **expr == '+') {
        result = parseNumber(expr, &termSuccess);
        if (!termSuccess) {
            if (errorMsg && errorMsgSize > 0) {
                snprintf(errorMsg, errorMsgSize, "Erro ao ler numero");
            }
            *success = false;
            return 0.0;
        }
    }
    // Verifica se é parêntese
    else if (**expr == '(') {
        (*expr)++;
        result = evalExpression(expr, variables, varCount, success, errorMsg, errorMsgSize);
        if (!*success) return 0.0;
        skipSpaces(expr);
        if (**expr != ')') {
            if (errorMsg && errorMsgSize > 0) {
                snprintf(errorMsg, errorMsgSize, "Parentese nao fechado");
            }
            *success = false;
            return 0.0;
        }
        (*expr)++;
    }
    // Verifica se é uma variável ou função (identificador)
    else if (isalpha(**expr) || **expr == '_') {
        // Lê o identificador
        String identifier = parseIdentifier(expr);
        if (identifier.length() > 0) {
            // Verifica se é uma variável conhecida
            bool isVariable = false;
            for (int i = 0; i < varCount; i++) {
                if (strcmp(variables[i].name, identifier.c_str()) == 0) {
                    result = variables[i].value;
                    termSuccess = true;
                    isVariable = true;
                    break;
                }
            }
            
            if (isVariable) {
                // É uma variável - já temos o resultado
                *success = true;
                return result;
            } else {
                // Não é variável, pode ser função - verifica se tem parêntese
                skipSpaces(expr);
                if (**expr == '(') {
                    // É uma função matemática
                    (*expr)++; // Pula o parêntese de abertura
                    
                    // Verifica se é a função if() que tem sintaxe especial
                    if (identifier == "if") {
                        // if(condição, valor_se_verdadeiro, valor_se_falso)
                        skipSpaces(expr);
                        
                        // Avalia a condição (pode conter comparações)
                        double condition = evalExpression(expr, variables, varCount, success, errorMsg, errorMsgSize);
                        if (!*success) return 0.0;
                        skipSpaces(expr);
                        
                        // Verifica vírgula após condição
                        if (**expr != ',') {
                            if (errorMsg && errorMsgSize > 0) {
                                snprintf(errorMsg, errorMsgSize, "Funcao if requer 3 argumentos separados por virgula");
                            }
                            *success = false;
                            return 0.0;
                        }
                        (*expr)++; // Pula a vírgula
                        skipSpaces(expr);
                        
                        // Avalia valor se verdadeiro
                        double trueValue = evalExpression(expr, variables, varCount, success, errorMsg, errorMsgSize);
                        if (!*success) return 0.0;
                        skipSpaces(expr);
                        
                        // Verifica vírgula após valor verdadeiro
                        if (**expr != ',') {
                            if (errorMsg && errorMsgSize > 0) {
                                snprintf(errorMsg, errorMsgSize, "Funcao if requer 3 argumentos separados por virgula");
                            }
                            *success = false;
                            return 0.0;
                        }
                        (*expr)++; // Pula a vírgula
                        skipSpaces(expr);
                        
                        // Avalia valor se falso
                        double falseValue = evalExpression(expr, variables, varCount, success, errorMsg, errorMsgSize);
                        if (!*success) return 0.0;
                        skipSpaces(expr);
                        
                        // Verifica parêntese de fechamento
                        if (**expr != ')') {
                            if (errorMsg && errorMsgSize > 0) {
                                snprintf(errorMsg, errorMsgSize, "Parentese nao fechado na funcao if");
                            }
                            *success = false;
                            return 0.0;
                        }
                        (*expr)++; // Pula o parêntese de fechamento
                        
                        // Retorna valor baseado na condição (qualquer valor != 0 é verdadeiro)
                        result = (fabs(condition) > 0.000001) ? trueValue : falseValue;
                        termSuccess = true;
                    } else if (identifier == "pow") {
                        // pow(base, expoente) - requer dois argumentos
                        skipSpaces(expr);
                        if (**expr != '(') {
                            if (errorMsg && errorMsgSize > 0) {
                                snprintf(errorMsg, errorMsgSize, "Funcao pow requer parentese");
                            }
                            *success = false;
                            return 0.0;
                        }
                        (*expr)++; // Pula o parêntese de abertura
                        skipSpaces(expr);
                        
                        // Avalia o primeiro argumento (base)
                        double base = evalExpression(expr, variables, varCount, success, errorMsg, errorMsgSize);
                        if (!*success) return 0.0;
                        skipSpaces(expr);
                        
                        // Verifica vírgula após base
                        if (**expr != ',') {
                            if (errorMsg && errorMsgSize > 0) {
                                snprintf(errorMsg, errorMsgSize, "Funcao pow requer 2 argumentos separados por virgula");
                            }
                            *success = false;
                            return 0.0;
                        }
                        (*expr)++; // Pula a vírgula
                        skipSpaces(expr);
                        
                        // Avalia o segundo argumento (expoente)
                        double exponent = evalExpression(expr, variables, varCount, success, errorMsg, errorMsgSize);
                        if (!*success) return 0.0;
                        skipSpaces(expr);
                        
                        // Verifica parêntese de fechamento
                        if (**expr != ')') {
                            if (errorMsg && errorMsgSize > 0) {
                                snprintf(errorMsg, errorMsgSize, "Parentese nao fechado na funcao pow");
                            }
                            *success = false;
                            return 0.0;
                        }
                        (*expr)++; // Pula o parêntese de fechamento
                        
                        // Calcula potência
                        result = pow(base, exponent);
                        termSuccess = true;
                    } else {
                        // Funções normais (sin, cos, etc.) - apenas 1 argumento
                        double arg = evalExpression(expr, variables, varCount, success, errorMsg, errorMsgSize);
                        if (!*success) return 0.0;
                        skipSpaces(expr);
                        if (**expr != ')') {
                            if (errorMsg && errorMsgSize > 0) {
                                snprintf(errorMsg, errorMsgSize, "Parentese nao fechado");
                            }
                            *success = false;
                            return 0.0;
                        }
                        (*expr)++;
                        
                        // Aplica a função
                        if (identifier == "sin") {
                            result = sin(arg);
                        } else if (identifier == "cos") {
                            result = cos(arg);
                        } else if (identifier == "tan") {
                            result = tan(arg);
                        } else if (identifier == "sqrt") {
                            if (arg < 0) {
                                if (errorMsg && errorMsgSize > 0) {
                                    snprintf(errorMsg, errorMsgSize, "Raiz quadrada de numero negativo");
                                }
                                *success = false;
                                return 0.0;
                            }
                            result = sqrt(arg);
                        } else if (identifier == "abs") {
                            result = fabs(arg);
                        } else if (identifier == "log") {
                            if (arg <= 0) {
                                if (errorMsg && errorMsgSize > 0) {
                                    snprintf(errorMsg, errorMsgSize, "Log de numero <= 0");
                                }
                                *success = false;
                                return 0.0;
                            }
                            result = log(arg);
                        } else if (identifier == "exp") {
                            result = exp(arg);
                        } else {
                            if (errorMsg && errorMsgSize > 0) {
                                snprintf(errorMsg, errorMsgSize, "Funcao desconhecida: %s", identifier.c_str());
                            }
                            *success = false;
                            return 0.0;
                        }
                        termSuccess = true;
                    }
                } else {
                    // Identificador não é variável nem função - erro
                    if (errorMsg && errorMsgSize > 0) {
                        snprintf(errorMsg, errorMsgSize, "Variavel ou funcao desconhecida: %s", identifier.c_str());
                    }
                    *success = false;
                    return 0.0;
                }
            }
        } else {
            if (errorMsg && errorMsgSize > 0) {
                snprintf(errorMsg, errorMsgSize, "Caractere inesperado: %c", **expr);
            }
            *success = false;
            return 0.0;
        }
    }
    
    // Processa potência primeiro (maior precedência, associatividade direita)
    skipSpaces(expr);
    if (**expr == '^') {
        (*expr)++; // Pula o ^
        double exponent = evalTerm(expr, variables, varCount, success, errorMsg, errorMsgSize); // Recursivo para associatividade direita
        if (!*success) return 0.0;
        result = pow(result, exponent);
    }
    
    // Processa multiplicação, divisão e módulo
    while (true) {
        skipSpaces(expr);
        char op = **expr;
        if (op == '*' || op == '/' || op == '%') {
            (*expr)++;
            double right = evalTerm(expr, variables, varCount, success, errorMsg, errorMsgSize);
            if (!*success) return 0.0;
            
            if (op == '*') {
                result *= right;
            } else if (op == '/') {
                if (right == 0.0) {
                    if (errorMsg && errorMsgSize > 0) {
                        snprintf(errorMsg, errorMsgSize, "Divisao por zero");
                    }
                    *success = false;
                    return 0.0;
                }
                result /= right;
            } else { // %
                result = fmod(result, right);
            }
        } else {
            break;
        }
    }
    
    *success = true;
    return result;
}

// Função para avaliar comparações (>, <, >=, <=, ==, !=)
static double evalComparison(const char** expr, Variable* variables, int varCount, bool* success, char* errorMsg, size_t errorMsgSize) {
    skipSpaces(expr);
    
    // Verifica sinal negativo no início
    bool negative = false;
    if (**expr == '-') {
        negative = true;
        (*expr)++;
    } else if (**expr == '+') {
        (*expr)++;
    }
    
    double left = evalTerm(expr, variables, varCount, success, errorMsg, errorMsgSize);
    if (!*success) return 0.0;
    
    if (negative) {
        left = -left;
    }
    
    // Processa soma e subtração primeiro
    while (true) {
        skipSpaces(expr);
        char op = **expr;
        if (op == '+' || op == '-') {
            (*expr)++;
            double right = evalTerm(expr, variables, varCount, success, errorMsg, errorMsgSize);
            if (!*success) return 0.0;
            
            if (op == '+') {
                left += right;
            } else {
                left -= right;
            }
        } else {
            break;
        }
    }
    
    // Agora verifica operadores de comparação
    skipSpaces(expr);
    
    if (**expr == '>' || **expr == '<' || **expr == '=' || **expr == '!') {
        char op1 = **expr;
        (*expr)++;
        char op2 = '\0';
        
        // Verifica se é operador de 2 caracteres (>=, <=, ==, !=)
        if ((op1 == '>' || op1 == '<' || op1 == '=' || op1 == '!') && **expr == '=') {
            op2 = **expr;
            (*expr)++;
        }
        
        // Avalia o lado direito
        skipSpaces(expr);
        bool rightNegative = false;
        if (**expr == '-') {
            rightNegative = true;
            (*expr)++;
        } else if (**expr == '+') {
            (*expr)++;
        }
        
        double right = evalTerm(expr, variables, varCount, success, errorMsg, errorMsgSize);
        if (!*success) return 0.0;
        
        if (rightNegative) {
            right = -right;
        }
        
        // Processa soma/subtração do lado direito
        while (true) {
            skipSpaces(expr);
            char op = **expr;
            if (op == '+' || op == '-') {
                (*expr)++;
                double rightTerm = evalTerm(expr, variables, varCount, success, errorMsg, errorMsgSize);
                if (!*success) return 0.0;
                
                if (op == '+') {
                    right += rightTerm;
                } else {
                    right -= rightTerm;
                }
            } else {
                break;
            }
        }
        
        // Realiza a comparação
        bool comparisonResult = false;
        if (op1 == '>' && op2 == '=') {
            comparisonResult = (left >= right);
        } else if (op1 == '<' && op2 == '=') {
            comparisonResult = (left <= right);
        } else if (op1 == '=' && op2 == '=') {
            // Comparação com tolerância para números de ponto flutuante
            comparisonResult = (fabs(left - right) < 0.000001);
        } else if (op1 == '!' && op2 == '=') {
            comparisonResult = (fabs(left - right) >= 0.000001);
        } else if (op1 == '>') {
            comparisonResult = (left > right);
        } else if (op1 == '<') {
            comparisonResult = (left < right);
        } else {
            if (errorMsg && errorMsgSize > 0) {
                snprintf(errorMsg, errorMsgSize, "Operador de comparacao invalido");
            }
            *success = false;
            return 0.0;
        }
        
        // Retorna 1.0 para verdadeiro, 0.0 para falso
        *success = true;
        return comparisonResult ? 1.0 : 0.0;
    }
    
    // Não há comparação, retorna o valor calculado
    *success = true;
    return left;
}

// Função principal para avaliar expressão (soma e subtração)
static double evalExpression(const char** expr, Variable* variables, int varCount, bool* success, char* errorMsg, size_t errorMsgSize) {
    // Usa evalComparison que já processa soma/subtração e comparações
    return evalComparison(expr, variables, varCount, success, errorMsg, errorMsgSize);
}

// Função pública principal
bool evaluateExpression(const char* expression, Variable* variables, int varCount, double* result, char* errorMsg, size_t errorMsgSize) {
    if (!expression || !result) {
        if (errorMsg && errorMsgSize > 0) {
            snprintf(errorMsg, errorMsgSize, "Parametros invalidos");
        }
        return false;
    }
    
    const char* expr = expression;
    bool success = false;
    *result = evalExpression(&expr, variables, varCount, &success, errorMsg, errorMsgSize);
    
    if (success) {
        skipSpaces(&expr);
        if (*expr != '\0') {
            if (errorMsg && errorMsgSize > 0) {
                snprintf(errorMsg, errorMsgSize, "Caracteres extras apos expressao");
            }
            return false;
        }
    }
    
    return success;
}

double getVariableValue(const char* varName, Variable* variables, int varCount) {
    if (!varName) return 0.0;
    
    for (int i = 0; i < varCount; i++) {
        if (strcmp(variables[i].name, varName) == 0) {
            return variables[i].value;
        }
    }
    
    return 0.0;
}

/**
 * @brief Substitui {d[i][j]} na expressão pelos valores correspondentes
 */
bool substituteDeviceValues(const char* expression, DeviceValues* deviceValues, char* output, size_t outputSize, char* errorMsg, size_t errorMsgSize) {
    if (!expression || !deviceValues || !output || outputSize == 0) {
        if (errorMsg && errorMsgSize > 0) {
            snprintf(errorMsg, errorMsgSize, "Parametros invalidos");
        }
        return false;
    }
    
    // Inicializa o buffer de saída
    output[0] = '\0';
    size_t outputPos = 0;
    
    const char* pos = expression;
    
    while (*pos != '\0') {
        // Procura por {
        if (*pos == '{') {
            const char* start = pos;
            pos++; // Pula o {
            
            // Verifica se começa com "d["
            if (*pos == 'd' && *(pos + 1) == '[') {
                pos += 2; // Pula "d["
                
                // Lê o índice do dispositivo
                int deviceIndex = 0;
                bool hasDeviceIndex = false;
                while (isdigit(*pos)) {
                    deviceIndex = deviceIndex * 10 + (*pos - '0');
                    hasDeviceIndex = true;
                    pos++;
                }
                
                // Verifica se tem ]
                if (*pos != ']') {
                    if (errorMsg && errorMsgSize > 0) {
                        snprintf(errorMsg, errorMsgSize, "Erro: esperado ] apos indice do dispositivo");
                    }
                    return false;
                }
                pos++; // Pula o ]
                
                // Verifica se tem [
                if (*pos != '[') {
                    if (errorMsg && errorMsgSize > 0) {
                        snprintf(errorMsg, errorMsgSize, "Erro: esperado [ apos indice do dispositivo");
                    }
                    return false;
                }
                pos++; // Pula o [
                
                // Lê o índice do registro
                int registerIndex = 0;
                bool hasRegisterIndex = false;
                while (isdigit(*pos)) {
                    registerIndex = registerIndex * 10 + (*pos - '0');
                    hasRegisterIndex = true;
                    pos++;
                }
                
                // Verifica se tem ]
                if (*pos != ']') {
                    if (errorMsg && errorMsgSize > 0) {
                        snprintf(errorMsg, errorMsgSize, "Erro: esperado ] apos indice do registro");
                    }
                    return false;
                }
                pos++; // Pula o ]
                
                // Verifica se tem }
                if (*pos != '}') {
                    if (errorMsg && errorMsgSize > 0) {
                        snprintf(errorMsg, errorMsgSize, "Erro: esperado } apos d[i][j]");
                    }
                    return false;
                }
                pos++; // Pula o }
                
                // Verifica se os índices são válidos
                if (!hasDeviceIndex || !hasRegisterIndex) {
                    if (errorMsg && errorMsgSize > 0) {
                        snprintf(errorMsg, errorMsgSize, "Erro: indices invalidos em d[%d][%d]", deviceIndex, registerIndex);
                    }
                    return false;
                }
                
                if (deviceIndex < 0 || deviceIndex >= deviceValues->deviceCount) {
                    if (errorMsg && errorMsgSize > 0) {
                        snprintf(errorMsg, errorMsgSize, "Erro: indice de dispositivo invalido: %d (max: %d)", deviceIndex, deviceValues->deviceCount - 1);
                    }
                    return false;
                }
                
                if (registerIndex < 0 || registerIndex >= deviceValues->registerCounts[deviceIndex]) {
                    if (errorMsg && errorMsgSize > 0) {
                        snprintf(errorMsg, errorMsgSize, "Erro: indice de registro invalido: %d (max: %d) para dispositivo %d", registerIndex, deviceValues->registerCounts[deviceIndex] - 1, deviceIndex);
                    }
                    return false;
                }
                
                // Obtém o valor
                double value = deviceValues->values[deviceIndex][registerIndex];
                
                // Converte para string e adiciona ao output
                char valueStr[32];
                snprintf(valueStr, sizeof(valueStr), "%.6f", value);
                
                // Remove zeros desnecessários no final
                size_t len = strlen(valueStr);
                while (len > 0 && valueStr[len - 1] == '0' && strchr(valueStr, '.') != nullptr) {
                    valueStr[len - 1] = '\0';
                    len--;
                }
                if (len > 0 && valueStr[len - 1] == '.') {
                    valueStr[len - 1] = '\0';
                    len--;
                }
                
                // Adiciona ao output
                size_t valueLen = strlen(valueStr);
                if (outputPos + valueLen >= outputSize - 1) {
                    if (errorMsg && errorMsgSize > 0) {
                        snprintf(errorMsg, errorMsgSize, "Erro: buffer de saida muito pequeno");
                    }
                    return false;
                }
                strcpy(output + outputPos, valueStr);
                outputPos += valueLen;
            } else {
                // Não é d[, copia o { literal
                if (outputPos >= outputSize - 1) {
                    if (errorMsg && errorMsgSize > 0) {
                        snprintf(errorMsg, errorMsgSize, "Erro: buffer de saida muito pequeno");
                    }
                    return false;
                }
                output[outputPos++] = '{';
                output[outputPos] = '\0';
            }
        } else {
            // Caractere normal, copia
            if (outputPos >= outputSize - 1) {
                if (errorMsg && errorMsgSize > 0) {
                    snprintf(errorMsg, errorMsgSize, "Erro: buffer de saida muito pequeno");
                }
                return false;
            }
            output[outputPos++] = *pos;
            output[outputPos] = '\0';
            pos++;
        }
    }
    
    return true;
}

/**
 * @brief Processa expressão com atribuição, separando destino e expressão
 */
bool parseAssignment(const char* expression, AssignmentInfo* assignmentInfo, char* errorMsg, size_t errorMsgSize) {
    // Inicializa estrutura
    assignmentInfo->hasAssignment = false;
    assignmentInfo->targetDeviceIndex = -1;
    assignmentInfo->targetRegisterIndex = -1;
    assignmentInfo->expression = nullptr;
    assignmentInfo->expressionSize = 0;
    
    // Procura por '=' na expressão, mas ignora '=' que fazem parte de operadores de comparação (==, >=, <=, !=)
    const char* equalsPos = nullptr;
    const char* searchPos = expression;
    
    while ((searchPos = strchr(searchPos, '=')) != nullptr) {
        // Verifica se é parte de operador de comparação
        if (searchPos > expression) {
            char prevChar = *(searchPos - 1);
            // Se o caractere anterior é =, <, >, ou !, então é operador de comparação
            if (prevChar == '=' || prevChar == '<' || prevChar == '>' || prevChar == '!') {
                searchPos++; // Continua procurando
                continue;
            }
        }
        
        // Verifica se é parte de operador de comparação olhando para frente
        if (*(searchPos + 1) == '=') {
            searchPos += 2; // Pula o ==, >=, <=, !=
            continue;
        }
        
        // Este é um = de atribuição
        equalsPos = searchPos;
        break;
    }
    
    if (equalsPos == nullptr) {
        // Não há atribuição, expressão é apenas cálculo
        return true;
    }
    
    // Há atribuição
    assignmentInfo->hasAssignment = true;
    
    // Primeira parte: destino (antes do =)
    size_t destLen = equalsPos - expression;
    char* destPart = (char*)malloc(destLen + 1);
    if (destPart == nullptr) {
        if (errorMsg && errorMsgSize > 0) {
            snprintf(errorMsg, errorMsgSize, "Erro: falha ao alocar memoria");
        }
        return false;
    }
    strncpy(destPart, expression, destLen);
    destPart[destLen] = '\0';
    
    // Remove espaços do destino
    String destStr = String(destPart);
    destStr.trim();
    free(destPart);
    
    // Verifica se o destino é do formato {d[i][j]}
    if (destStr.length() < 6 || destStr.charAt(0) != '{' || destStr.charAt(1) != 'd' || destStr.charAt(2) != '[') {
        if (errorMsg && errorMsgSize > 0) {
            snprintf(errorMsg, errorMsgSize, "Erro: destino deve ser no formato {d[device][register]}");
        }
        return false;
    }
    
    // Extrai índices do destino
    int deviceIndex = -1;
    int registerIndex = -1;
    
    // Procura por {d[
    int startIdx = destStr.indexOf("{d[");
    if (startIdx == -1) {
        if (errorMsg && errorMsgSize > 0) {
            snprintf(errorMsg, errorMsgSize, "Erro: formato de destino invalido");
        }
        return false;
    }
    
    // Extrai índice do dispositivo
    int bracket1 = destStr.indexOf('[', startIdx + 2);
    int bracket2 = destStr.indexOf(']', bracket1);
    if (bracket1 == -1 || bracket2 == -1) {
        if (errorMsg && errorMsgSize > 0) {
            snprintf(errorMsg, errorMsgSize, "Erro: formato de destino invalido");
        }
        return false;
    }
    
    String deviceIdxStr = destStr.substring(bracket1 + 1, bracket2);
    deviceIndex = deviceIdxStr.toInt();
    
    // Extrai índice do registro
    int bracket3 = destStr.indexOf('[', bracket2);
    int bracket4 = destStr.indexOf(']', bracket3);
    if (bracket3 == -1 || bracket4 == -1) {
        if (errorMsg && errorMsgSize > 0) {
            snprintf(errorMsg, errorMsgSize, "Erro: formato de destino invalido");
        }
        return false;
    }
    
    String registerIdxStr = destStr.substring(bracket3 + 1, bracket4);
    registerIndex = registerIdxStr.toInt();
    
    assignmentInfo->targetDeviceIndex = deviceIndex;
    assignmentInfo->targetRegisterIndex = registerIndex;
    
    // Segunda parte: expressão (após o =)
    const char* exprStart = equalsPos + 1;
    size_t exprLen = strlen(exprStart);
    
    // Remove espaços iniciais
    while (*exprStart == ' ' || *exprStart == '\t') {
        exprStart++;
        exprLen--;
    }
    
    if (exprLen == 0) {
        if (errorMsg && errorMsgSize > 0) {
            snprintf(errorMsg, errorMsgSize, "Erro: expressao vazia apos o =");
        }
        return false;
    }
    
    // Limita tamanho máximo da expressão para evitar problemas de memória
    const size_t MAX_EXPR_SIZE = 2048;
    if (exprLen > MAX_EXPR_SIZE) {
        if (errorMsg && errorMsgSize > 0) {
            snprintf(errorMsg, errorMsgSize, "Erro: expressao muito grande (max: %zu caracteres)", MAX_EXPR_SIZE);
        }
        return false;
    }
    
    // Aloca memória para a expressão
    assignmentInfo->expressionSize = exprLen + 1;
    assignmentInfo->expression = (char*)malloc(assignmentInfo->expressionSize);
    if (assignmentInfo->expression == nullptr) {
        if (errorMsg && errorMsgSize > 0) {
            snprintf(errorMsg, errorMsgSize, "Erro: falha ao alocar memoria para expressao");
        }
        return false;
    }
    
    strncpy(assignmentInfo->expression, exprStart, exprLen);
    assignmentInfo->expression[exprLen] = '\0';
    
    return true;
}

/**
 * @brief Libera memória alocada em AssignmentInfo
 */
void freeAssignmentInfo(AssignmentInfo* assignmentInfo) {
    if (assignmentInfo->expression != nullptr) {
        free(assignmentInfo->expression);
        assignmentInfo->expression = nullptr;
    }
    assignmentInfo->expressionSize = 0;
}

