/**
 * @file calculations.cpp
 * @brief Implementação das funções de cálculo
 */

#include "calculations.h"
#include "modbus_handler.h"
#include "console.h"
#include "kalman_filter.h"

void performCalculations() {
    // Verifica se há código de cálculo configurado
    if (strlen(config.calculationCode) == 0) {
        return;  // Nenhum cálculo configurado
    }
    
    // Prepara estrutura DeviceValues com todos os valores dos dispositivos
    // Aplica gain e offset antes de atribuir
    DeviceValues deviceValues;
    deviceValues.deviceCount = config.deviceCount;
    deviceValues.registerCounts = new int[config.deviceCount];
    deviceValues.values = new double*[config.deviceCount];
    
    for (int i = 0; i < config.deviceCount; i++) {
        deviceValues.registerCounts[i] = config.devices[i].registerCount;
        deviceValues.values[i] = new double[config.devices[i].registerCount];
        
        for (int j = 0; j < config.devices[i].registerCount; j++) {
            // Aplica gain e offset: valor_processado = (valor_raw * gain) + offset
            float rawValue = (float)config.devices[i].registers[j].value;
            float processedValue = (rawValue * config.devices[i].registers[j].gain) + config.devices[i].registers[j].offset;
            
            // Se o filtro de Kalman está habilitado e inicializado, usa o valor do Kalman
            if (config.devices[i].registers[j].kalmanEnabled && kalmanStates[i][j].initialized) {
                float kalmanValue = kalmanStates[i][j].estimate;
                processedValue = (kalmanValue * config.devices[i].registers[j].gain) + config.devices[i].registers[j].offset;
            }
            
            deviceValues.values[i][j] = (double)processedValue;
        }
    }
    
    // Arrays para armazenar variáveis temporárias (máximo 50 variáveis)
    // IMPORTANTE: Alocados no heap para evitar stack overflow
    // k[i] = nome da variável (máximo 5 caracteres), v[i] = valor
    const int MAX_TEMP_VARS = 50;
    char (*tempVarNames)[6] = new char[MAX_TEMP_VARS][6];  // 5 caracteres + null terminator
    double* tempVarValues = new double[MAX_TEMP_VARS];
    int tempVarCount = 0;
    
    // Converte para formato Variable para compatibilidade com substituteDeviceValues
    Variable* tempVariables = new Variable[MAX_TEMP_VARS];
    
    // Buffers alocados no heap para evitar stack overflow
    char* lineBuffer = new char[1024];
    char* processedExpression = new char[2048];
    char* errorMsg = new char[256];
    
    // Divide o código em linhas e processa cada uma separadamente
    String codeStr = String(config.calculationCode);
    int lineNumber = 1;
    int startPos = 0;
    
    while (startPos < codeStr.length()) {
        // Encontra o final da linha (caractere de nova linha ou fim da string)
        int endPos = codeStr.indexOf('\n', startPos);
        if (endPos == -1) {
            endPos = codeStr.length();
        }
        
        // Extrai a linha (remove espaços no início e fim)
        String line = codeStr.substring(startPos, endPos);
        line.trim();  // Remove espaços no início e fim
        
        // Avança para a próxima linha
        startPos = endPos + 1;
        
        // Ignora linhas vazias ou apenas com espaços
        if (line.length() == 0) {
            continue;
        }
        
        // Ignora linhas que começam com '#' (comentários)
        if (line.charAt(0) == '#') {
            continue;
        }
        
        // Converte String para char* para processar
        strncpy(lineBuffer, line.c_str(), 1023);
        lineBuffer[1023] = '\0';
        
        // Processa esta linha
        AssignmentInfo assignmentInfo;
        errorMsg[0] = '\0';  // Limpa buffer de erro
        bool parseSuccess = parseAssignment(lineBuffer, &assignmentInfo, errorMsg, 256);
        
        if (!parseSuccess) {
            // Log de erro no console (mas continua processando outras linhas)
            String logMsg = "[Linha " + String(lineNumber) + "] Erro ao processar: " + String(errorMsg);
            consolePrint(logMsg + "\r\n");
            lineNumber++;
            continue;
        }
        
        // Se há atribuição, processa expressão do segundo membro
        const char* expressionToProcess = assignmentInfo.hasAssignment ? assignmentInfo.expression : lineBuffer;
        
        // Substitui {d[i][j]} e variáveis temporárias na expressão pelos valores
        processedExpression[0] = '\0';  // Limpa buffer
        errorMsg[0] = '\0';  // Limpa buffer de erro
        bool success = substituteDeviceValues(expressionToProcess, &deviceValues, processedExpression, 2048, errorMsg, 256, tempVariables, tempVarCount);
        
        if (!success) {
            // Log de erro no console (mas continua processando outras linhas)
            String logMsg = "[Linha " + String(lineNumber) + "] Erro ao processar expressao: " + String(errorMsg);
            consolePrint(logMsg + "\r\n");
            freeAssignmentInfo(&assignmentInfo);
            lineNumber++;
            continue;
        }
        
        // Avalia a expressão processada (sem variáveis, apenas números e operadores)
        double result = 0.0;
        Variable emptyVars[1];
        int emptyVarCount = 0;
        bool evalSuccess = evaluateExpression(processedExpression, emptyVars, emptyVarCount, &result, errorMsg, sizeof(errorMsg));
        
        if (!evalSuccess) {
            // Log de erro no console (mas continua processando outras linhas)
            String logMsg = "[Linha " + String(lineNumber) + "] Erro ao avaliar expressao: " + String(errorMsg);
            consolePrint(logMsg + "\r\n");
            freeAssignmentInfo(&assignmentInfo);
            lineNumber++;
            continue;
        }
        
        // Limpa buffer de erro antes de próxima iteração
        errorMsg[0] = '\0';
        
        // Se há atribuição, processa
        if (assignmentInfo.hasAssignment) {
            // Se é atribuição a variável temporária
            if (assignmentInfo.isVariableAssignment) {
                // Limita nome a 5 caracteres
                char varName[6];
                strncpy(varName, assignmentInfo.targetVariable, 5);
                varName[5] = '\0';
                
                // Armazena ou atualiza a variável temporária nos arrays k[] e v[]
                bool varFound = false;
                for (int i = 0; i < tempVarCount; i++) {
                    if (strcmp(tempVarNames[i], varName) == 0) {
                        tempVarValues[i] = result;
                        // Atualiza também no array Variable para compatibilidade
                        tempVariables[i].value = result;
                        varFound = true;
                        break;
                    }
                }
                
                if (!varFound) {
                    // Adiciona nova variável temporária
                    if (tempVarCount < MAX_TEMP_VARS) {
                        strncpy(tempVarNames[tempVarCount], varName, 5);
                        tempVarNames[tempVarCount][5] = '\0';
                        tempVarValues[tempVarCount] = result;
                        
                        // Atualiza também no array Variable para compatibilidade
                        strncpy(tempVariables[tempVarCount].name, varName, sizeof(tempVariables[tempVarCount].name) - 1);
                        tempVariables[tempVarCount].name[sizeof(tempVariables[tempVarCount].name) - 1] = '\0';
                        tempVariables[tempVarCount].value = result;
                        
                        tempVarCount++;
                    } else {
                        String logMsg = "[Linha " + String(lineNumber) + "] Aviso: limite de variaveis temporarias atingido (max: " + String(MAX_TEMP_VARS) + ")";
                        consolePrint(logMsg + "\r\n");
                    }
                }
                
                // Log no console (sem mencionar {d[-1][-1]})
                String logMsg = "[Linha " + String(lineNumber) + "] Variavel temporaria: " + String(varName) + " = " + String(processedExpression) + " = " + String(result, 2);
                consolePrint(logMsg + "\r\n");
                
                freeAssignmentInfo(&assignmentInfo);
                lineNumber++;
                continue;
            }
            
            // Se é atribuição para {d[i][j]}, escreve no registro de destino
            // Valida índices do destino
            if (assignmentInfo.targetDeviceIndex < 0 || assignmentInfo.targetDeviceIndex >= config.deviceCount) {
                String logMsg = "[Linha " + String(lineNumber) + "] Erro: indice de dispositivo invalido: " + String(assignmentInfo.targetDeviceIndex);
                consolePrint(logMsg + "\r\n");
                freeAssignmentInfo(&assignmentInfo);
                lineNumber++;
                continue;
            }
            
            if (assignmentInfo.targetRegisterIndex < 0 || 
                assignmentInfo.targetRegisterIndex >= config.devices[assignmentInfo.targetDeviceIndex].registerCount) {
                String logMsg = "[Linha " + String(lineNumber) + "] Erro: indice de registro invalido: " + String(assignmentInfo.targetRegisterIndex);
                consolePrint(logMsg + "\r\n");
                freeAssignmentInfo(&assignmentInfo);
                lineNumber++;
                continue;
            }
            
            // Obtém referência ao registro de destino
            ModbusRegister* targetReg = &config.devices[assignmentInfo.targetDeviceIndex].registers[assignmentInfo.targetRegisterIndex];
            
            // Verifica se o registro não é somente leitura
            if (targetReg->readOnly) {
                String logMsg = "[Linha " + String(lineNumber) + "] Erro: registro destino e somente leitura";
                consolePrint(logMsg + "\r\n");
                freeAssignmentInfo(&assignmentInfo);
                lineNumber++;
                continue;
            }
            
            // Aplica transformação inversa de gain/offset antes de escrever
            // Se valor_processado = (valor_raw * gain) + offset
            // Então valor_raw = (valor_processado - offset) / gain
            float valueToWrite = result;
            
            // Verifica se gain é zero (evita divisão por zero)
            if (targetReg->gain == 0.0f) {
                String logMsg = "[Linha " + String(lineNumber) + "] Erro: gain zero no registro destino, nao e possivel aplicar transformacao inversa";
                consolePrint(logMsg + "\r\n");
                freeAssignmentInfo(&assignmentInfo);
                lineNumber++;
                continue;
            }
            
            // Aplica transformação inversa
            valueToWrite = (valueToWrite - targetReg->offset) / targetReg->gain;
            
            // Limita ao range de uint16_t (0-65535)
            if (valueToWrite < 0) valueToWrite = 0;
            if (valueToWrite > 65535) valueToWrite = 65535;
            
            // Atualiza valor no registro
            targetReg->value = (uint16_t)valueToWrite;
            
            // Escreve no Modbus
            uint8_t slaveAddr = config.devices[assignmentInfo.targetDeviceIndex].slaveAddress;
            node.begin(slaveAddr, Serial2);
            uint8_t writeResult = node.writeSingleRegister(targetReg->address, targetReg->value);
            
            if (writeResult == node.ku8MBSuccess) {
                // Log no console
                String logMsg = "[Linha " + String(lineNumber) + "] Atribuicao executada: {d[" + 
                               String(assignmentInfo.targetDeviceIndex) + "][" + 
                               String(assignmentInfo.targetRegisterIndex) + "]} = " + 
                               String(processedExpression) + " = " + String(result, 2) + 
                               " (raw: " + String(valueToWrite, 0) + ")";
                consolePrint(logMsg + "\r\n");
            } else {
                String logMsg = "[Linha " + String(lineNumber) + "] Erro ao escrever Modbus: dispositivo " + 
                               String(slaveAddr) + ", registro " + String(targetReg->address) + 
                               ", codigo: " + String(writeResult);
                consolePrint(logMsg + "\r\n");
            }
            
            freeAssignmentInfo(&assignmentInfo);
            lineNumber++;
            continue;
        }
        
        // Se não há atribuição, comportamento antigo: escreve no primeiro registro de saída
        // (mas apenas na primeira linha sem atribuição)
        bool foundOutput = false;
        for (int i = 0; i < config.deviceCount && !foundOutput; i++) {
            if (!config.devices[i].enabled) continue;
            
            for (int j = 0; j < config.devices[i].registerCount && !foundOutput; j++) {
                if (config.devices[i].registers[j].isOutput && !config.devices[i].registers[j].readOnly) {
                    // Limita resultado ao range de uint16_t (0-65535)
                    if (result < 0) result = 0;
                    if (result > 65535) result = 65535;
                    
                    config.devices[i].registers[j].value = (uint16_t)result;
                    
                    // Log no console
                    String logMsg = "[Linha " + String(lineNumber) + "] Calculo executado: " + 
                                   String(lineBuffer) + " = " + String(processedExpression) + 
                                   " = " + String(result, 2);
                    consolePrint(logMsg + "\r\n");
                    
                    foundOutput = true;
                }
            }
        }
        
        if (!foundOutput) {
            // Log de aviso se não encontrou registro de saída
            String logMsg = "[Linha " + String(lineNumber) + "] Aviso: expressao calculada mas nenhum registro de saida encontrado. Resultado: " + String(result, 2);
            consolePrint(logMsg + "\r\n");
        }
        
        lineNumber++;
    }
    
    // Limpa memória DeviceValues
    for (int i = 0; i < config.deviceCount; i++) {
        delete[] deviceValues.values[i];
    }
    delete[] deviceValues.values;
    delete[] deviceValues.registerCounts;
    
    // Limpa memória dos arrays alocados no heap
    delete[] tempVarNames;
    delete[] tempVarValues;
    delete[] tempVariables;
    delete[] lineBuffer;
    delete[] processedExpression;
    delete[] errorMsg;
}

