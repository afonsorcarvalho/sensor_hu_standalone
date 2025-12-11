# Changelog - Sistema de Ganho e Offset

## Mudanças Implementadas

### 1. Estrutura ModbusRegister Atualizada
- Adicionados campos `gain` (float) e `offset` (float)
- Valores padrão: `gain = 1.0`, `offset = 0.0`

### 2. Aplicação de Ganho e Offset
- Fórmula aplicada: `valor_processado = (valor_raw * gain) + offset`
- Aplicado automaticamente ao ler registros Modbus
- Valores processados são usados nas expressões de cálculo

### 3. Funções Atualizadas
- `loadConfig()`: Carrega gain e offset do JSON
- `saveConfig()`: Salva gain e offset no JSON
- `handleGetConfig()`: Retorna gain e offset na API
- `handleSaveConfig()`: Aceita gain e offset na configuração
- `handleGetVariables()`: Retorna valor raw, valor processado, gain e offset
- `performCalculations()`: Usa valores processados (com gain/offset) nas expressões
- `handleTestCalculation()`: Usa valores processados nos testes

### 4. Funções Implementadas
- `handleSyncNTP()`: Sincronização NTP via API
- `handleExportConfig()`: Exporta configuração completa como JSON
- `handleImportConfig()`: Importa configuração de JSON

## Formato JSON dos Registros

```json
{
  "address": 0,
  "isInput": true,
  "isOutput": false,
  "variableName": "temperatura",
  "gain": 0.1,
  "offset": -10.0
}
```

## Uso

1. Configure o ganho e offset para cada registro na interface web
2. Os valores raw do Modbus são armazenados em `value`
3. Os valores processados (com gain/offset) são usados nas variáveis para cálculos
4. Fórmula: `variável = (valor_raw * gain) + offset`

## Compatibilidade

- Configurações antigas sem gain/offset usam valores padrão (gain=1.0, offset=0.0)
- Sistema retrocompatível com configurações existentes

