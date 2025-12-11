/**
 * @file calculations.cpp
 * @brief Implementação das funções de cálculo
 */

#include "calculations.h"
#include "modbus_handler.h"
#include "console.h"

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
            deviceValues.values[i][j] = (double)processedValue;
        }
    }
    
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
        char lineBuffer[1024];
        strncpy(lineBuffer, line.c_str(), sizeof(lineBuffer) - 1);
        lineBuffer[sizeof(lineBuffer) - 1] = '\0';
        
        // Processa esta linha
        AssignmentInfo assignmentInfo;
        char errorMsg[256] = "";
        bool parseSuccess = parseAssignment(lineBuffer, &assignmentInfo, errorMsg, sizeof(errorMsg));
        
        if (!parseSuccess) {
            // Log de erro no console (mas continua processando outras linhas)
            String logMsg = "[Linha " + String(lineNumber) + "] Erro ao processar: " + String(errorMsg);
            consolePrint(logMsg + "\r\n");
            lineNumber++;
            continue;
        }
        
        // Se há atribuição, processa expressão do segundo membro
        const char* expressionToProcess = assignmentInfo.hasAssignment ? assignmentInfo.expression : lineBuffer;
        
        // Substitui {d[i][j]} na expressão pelos valores
        char processedExpression[2048];
        bool success = substituteDeviceValues(expressionToProcess, &deviceValues, processedExpression, sizeof(processedExpression), errorMsg, sizeof(errorMsg));
        
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
        
        // Se há atribuição, escreve no registro de destino
        if (assignmentInfo.hasAssignment) {
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
}

