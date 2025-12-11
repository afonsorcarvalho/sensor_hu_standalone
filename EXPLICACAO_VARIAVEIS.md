# Explicação: Sistema de Variáveis com Estrutura d[device][register]

## Nova Abordagem Implementada

O sistema foi completamente reformulado para usar uma estrutura bidimensional simples e intuitiva, onde todos os valores lidos dos dispositivos Modbus são organizados em uma variável chamada `d` com índices bidimensionais.

## Estrutura de Dados

### Formato: `d[deviceIndex][registerIndex]`

- **`d[0][0]`** = Primeiro dispositivo (índice 0), primeiro registro adicionado (índice 0)
- **`d[0][1]`** = Primeiro dispositivo (índice 0), segundo registro adicionado (índice 1)
- **`d[1][0]`** = Segundo dispositivo (índice 1), primeiro registro adicionado (índice 0)
- **`d[1][1]`** = Segundo dispositivo (índice 1), segundo registro adicionado (índice 1)
- E assim por diante...

### Índices

- **Índice do Dispositivo**: Baseado na ordem de adição dos dispositivos (começa em 0)
  - Dispositivo 1 = índice 0
  - Dispositivo 2 = índice 1
  - Dispositivo 3 = índice 2
  - etc.

- **Índice do Registro**: Baseado na ordem de adição dos registros dentro de cada dispositivo (começa em 0)
  - Primeiro registro adicionado = índice 0
  - Segundo registro adicionado = índice 1
  - Terceiro registro adicionado = índice 2
  - etc.

## Uso nas Expressões

### Sintaxe

Na expressão, use a sintaxe `{d[i][j]}` onde:
- `i` = índice do dispositivo (0, 1, 2, ...)
- `j` = índice do registro (0, 1, 2, ...)

### Expressões com Atribuição

O sistema suporta atribuições no formato: `{d[i][j]}={expressão}`

**Formato:**
- **Primeiro membro** (antes do `=`): Registro de destino onde o resultado será escrito
- **Segundo membro** (após o `=`): Expressão matemática a ser calculada

**Comportamento:**
1. O segundo membro é calculado normalmente
2. O resultado é aplicado com transformação inversa de gain/offset
3. O valor raw resultante é escrito no registro Modbus de destino

**Transformação Inversa:**
- Se `valor_processado = (valor_raw * gain) + offset`
- Então `valor_raw = (valor_processado - offset) / gain`
- O valor raw é então escrito no Modbus

### Exemplos

#### Expressões Simples (sem atribuição)

```
{d[0][0]} * 2
```
Multiplica o valor do primeiro registro do primeiro dispositivo por 2.

```
{d[0][0]} * 1.8 + 32
```
Converte temperatura de Celsius para Fahrenheit (primeiro registro do primeiro dispositivo).

```
{d[0][0]} + {d[0][1]}
```
Soma o primeiro e segundo registro do primeiro dispositivo.

```
({d[0][0]} + {d[1][0]}) / 2
```
Calcula a média entre o primeiro registro do primeiro dispositivo e o primeiro registro do segundo dispositivo.

```
sqrt({d[0][0]} * {d[0][0]} + {d[0][1]} * {d[0][1]})
```
Calcula a raiz quadrada da soma dos quadrados (norma euclidiana).

#### Expressões com Atribuição

```
{d[1][0]}={d[2][0]} + exp({d[1][0]})
```
- Calcula: `{d[2][0]} + exp({d[1][0]})`
- Escreve o resultado no registro `d[1][0]` (dispositivo 1, registro 0)
- Aplica transformação inversa de gain/offset antes de escrever

```
{d[0][2]}={d[0][0]} * {d[0][1]} / 1000
```
- Calcula: `{d[0][0]} * {d[0][1]} / 1000`
- Escreve o resultado no registro `d[0][2]` (dispositivo 0, registro 2)

```
{d[1][1]}=({d[0][0]} + {d[0][1]}) / 2
```
- Calcula a média de dois registros
- Escreve o resultado no registro `d[1][1]`

## Processamento de Valores

### Aplicação de Gain e Offset

Antes de usar nas expressões, cada valor raw do Modbus passa por processamento:

```
valor_processado = (valor_raw * gain) + offset
```

Onde:
- **valor_raw**: Valor lido diretamente do Modbus (uint16_t, 0-65535)
- **gain**: Ganho configurado para o registro (padrão: 1.0)
- **offset**: Offset configurado para o registro (padrão: 0.0)
- **valor_processado**: Valor usado nas expressões (double)

### Exemplo de Processamento

Se um registro Modbus retorna o valor raw `1000`:
- Com `gain = 0.1` e `offset = -10.0`
- O valor processado será: `(1000 * 0.1) + (-10.0) = 90.0`
- Nas expressões, `{d[0][0]}` retornará `90.0`

## Fluxo de Processamento

### 1. Leitura dos Registros Modbus

```
readAllDevices() → Lê valores raw de todos os dispositivos
```

### 2. Preparação da Estrutura DeviceValues

```
DeviceValues {
    deviceCount: número de dispositivos
    registerCounts[]: número de registros por dispositivo
    values[][]: valores processados (com gain/offset aplicado)
}
```

### 3. Substituição na Expressão

```
Expressão original: "{d[0][0]} * 2 + {d[0][1]}"
                    ↓
substituteDeviceValues()
                    ↓
Expressão processada: "90.0 * 2 + 150.5"
```

### 4. Avaliação da Expressão

```
Expressão processada: "90.0 * 2 + 150.5"
                    ↓
evaluateExpression()
                    ↓
Resultado: 330.5
```

## Funções Implementadas

### `substituteDeviceValues()`

Substitui todas as ocorrências de `{d[i][j]}` na expressão pelos valores correspondentes.

**Parâmetros:**
- `expression`: Expressão original com `{d[i][j]}`
- `deviceValues`: Estrutura com valores dos dispositivos
- `output`: Buffer para expressão processada
- `outputSize`: Tamanho do buffer
- `errorMsg`: Buffer para mensagens de erro

**Retorna:** `true` se sucesso, `false` se erro

**Exemplo:**
```cpp
char processed[2048];
substituteDeviceValues("{d[0][0]} * 2", &deviceValues, processed, sizeof(processed), errorMsg, sizeof(errorMsg));
// processed = "90.0 * 2"
```

### `handleTestCalculation()`

Testa uma expressão e retorna o resultado.

**Endpoint:** `POST /api/calc/test`

**Body:**
```json
{
  "expression": "{d[0][0]} * 2"
}
```

**Resposta (sucesso):**
```json
{
  "status": "ok",
  "result": 180.0,
  "processedExpression": "90.0 * 2"
}
```

**Resposta (erro):**
```json
{
  "status": "error",
  "error": "Erro: indice de dispositivo invalido: 5 (max: 2)"
}
```

### `handleGetVariables()`

Retorna todos os valores disponíveis em formato bidimensional.

**Endpoint:** `GET /api/calc/variables`

**Resposta:**
```json
{
  "devices": [
    [
      {
        "valueRaw": 1000,
        "value": 90.0,
        "gain": 0.1,
        "offset": -10.0,
        "address": 0,
        "enabled": true,
        "isOutput": false
      },
      {
        "valueRaw": 2000,
        "value": 150.5,
        "gain": 0.075,
        "offset": 0.5,
        "address": 1,
        "enabled": true,
        "isOutput": false
      }
    ],
    [
      {
        "valueRaw": 500,
        "value": 50.0,
        "gain": 0.1,
        "offset": 0.0,
        "address": 0,
        "enabled": true,
        "isOutput": false
      }
    ]
  ],
  "structure": "d[deviceIndex][registerIndex]",
  "deviceCount": 2
}
```

### `performCalculations()`

Executa os cálculos configurados periodicamente (a cada 1 segundo).

1. Prepara estrutura `DeviceValues` com todos os valores
2. Substitui `{d[i][j]}` na expressão configurada
3. Avalia a expressão processada
4. Escreve o resultado no primeiro registro de saída encontrado

## Interface Web

### Seção de Cálculos

A interface web mostra:
- Instruções sobre como usar `{d[i][j]}`
- Campo de texto para inserir a expressão
- Botão "Testar Calculo" para validar a expressão
- Botão "Carregar Variaveis Disponiveis" para ver todos os valores

### Tabela de Variáveis Disponíveis

Ao clicar em "Carregar Variaveis Disponiveis", é exibida uma tabela com:
- **Referencia**: `{d[i][j]}` para usar na expressão
- **Valor**: Valor processado (com gain/offset)
- **Raw**: Valor raw do Modbus
- **Gain**: Ganho aplicado
- **Offset**: Offset aplicado
- **Endereco**: Endereço Modbus do registro

## Vantagens da Nova Abordagem

1. **Simplicidade**: Não precisa configurar nomes de variáveis
2. **Organização**: Estrutura clara e previsível
3. **Flexibilidade**: Acessa qualquer registro de qualquer dispositivo
4. **Rastreabilidade**: Fácil identificar qual dispositivo/registro está sendo usado
5. **Menos Configuração**: Não precisa nomear cada variável

## Exemplos Práticos

### Exemplo 1: Média de Dois Sensores

```
({d[0][0]} + {d[0][1]}) / 2
```

### Exemplo 2: Conversão de Temperatura

```
{d[0][0]} * 1.8 + 32
```

### Exemplo 3: Cálculo de Potência

```
{d[0][0]} * {d[0][1]} / 1000
```

### Exemplo 4: Média Ponderada

```
({d[0][0]} * 0.6 + {d[1][0]} * 0.4)
```

### Exemplo 5: Cálculo com Funções Matemáticas

```
sqrt({d[0][0]} * {d[0][0]} + {d[0][1]} * {d[0][1]})
```

## Tratamento de Erros

### Erros Comuns

1. **Índice de dispositivo inválido**
   - Erro: `"Erro: indice de dispositivo invalido: 5 (max: 2)"`
   - Solução: Verifique quantos dispositivos estão configurados

2. **Índice de registro inválido**
   - Erro: `"Erro: indice de registro invalido: 3 (max: 1) para dispositivo 0"`
   - Solução: Verifique quantos registros o dispositivo tem

3. **Sintaxe incorreta**
   - Erro: `"Erro: esperado ] apos indice do dispositivo"`
   - Solução: Use a sintaxe correta `{d[i][j]}`

4. **Expressão inválida após substituição**
   - Erro: `"Divisao por zero"` ou `"Caracteres extras apos expressao"`
   - Solução: Verifique a expressão matemática

## Compatibilidade

- **Gain e Offset**: Continuam funcionando normalmente
- **Registros de Saída**: Continuam sendo escritos com os resultados
- **Configuração**: Não precisa mais configurar nomes de variáveis (mas o campo ainda existe para referência)

## Mudanças Técnicas Implementadas

### Arquivos Modificados

1. **`src/expression_parser.h`**
   - Adicionada estrutura `DeviceValues`
   - Adicionada função `substituteDeviceValues()`

2. **`src/expression_parser.cpp`**
   - Implementada função `substituteDeviceValues()`
   - Processa `{d[i][j]}` e substitui pelos valores

3. **`src/main.cpp`**
   - `handleTestCalculation()`: Usa nova abordagem
   - `performCalculations()`: Usa nova abordagem
   - `handleGetVariables()`: Retorna estrutura bidimensional

4. **`data/index.html`**
   - Interface atualizada com instruções sobre `{d[i][j]}`
   - Função `loadVariables()` atualizada para mostrar estrutura bidimensional

## Inicialização de Valores

### Valores Inicializados com 0

**IMPORTANTE**: Quando um registro é salvo, ele é automaticamente inicializado com valor 0, mesmo sem realizar a leitura do dispositivo Modbus. Isso garante que:

1. **A variável existe**: O registro está disponível para uso nas expressões imediatamente após salvar
2. **Sem erros de índice**: Não haverá erro "índice de registro inválido" se o registro foi salvo
3. **Valor inicial conhecido**: O valor inicial é sempre 0 até a primeira leitura do Modbus

### Processo de Inicialização

```
1. Usuário adiciona registro na interface web
2. Usuário clica em "Salvar" no dispositivo
3. Sistema salva o registro com:
   - address: endereço Modbus configurado
   - value: 0 (inicializado)
   - gain: 1.0 (padrão)
   - offset: 0.0 (padrão)
4. Registro fica disponível para uso: {d[i][j]} = 0.0
5. Quando readAllDevices() é executado, o valor é atualizado com leitura Modbus
```

### Sincronização de registerCount

**IMPORTANTE**: O `registerCount` é sempre baseado no tamanho real do array de registros:

- Ao salvar: `registerCount = registersArray.size()`
- Ao carregar: `registerCount = registersArray.size()`
- Isso garante que não há discrepância entre `registerCount` e o número real de registros

## Atribuições com Transformação Inversa

### Processo de Escrita

Quando uma expressão contém atribuição (`{d[i][j]}={expressão}`):

1. **Cálculo**: O segundo membro é calculado normalmente
2. **Transformação Inversa**: O resultado processado é convertido para valor raw:
   ```
   valor_raw = (valor_processado - offset) / gain
   ```
3. **Validação**: Verifica se `gain != 0` (evita divisão por zero)
4. **Limitação**: Valor raw é limitado ao range de uint16_t (0-65535)
5. **Escrita Modbus**: O valor raw é escrito no registro Modbus de destino

### Exemplo Completo

**Expressão:** `{d[1][0]}={d[2][0]} + exp({d[1][0]})`

**Valores:**
- `d[2][0]` = 150.5 (processado)
- `d[1][0]` = 90.0 (processado)
- Registro destino `d[1][0]`: gain = 0.1, offset = -10.0

**Processamento:**
1. Calcula: `150.5 + exp(90.0)` = 1.234e+39
2. Aplica transformação inversa: `(1.234e+39 - (-10.0)) / 0.1` = 1.234e+40
3. Limita a 65535 (range uint16_t)
4. Escreve 65535 no registro Modbus `d[1][0]`

## Resumo

O sistema agora usa uma abordagem mais simples e direta:
- Todos os valores em `d[deviceIndex][registerIndex]`
- Uso na expressão: `{d[0][0]} * 2`
- **Suporte a atribuições**: `{d[1][0]}={d[2][0]} + exp({d[1][0]})`
- Gain e offset aplicados automaticamente na leitura
- **Transformação inversa aplicada automaticamente na escrita**
- Interface mostra todas as referências disponíveis
- Menos configuração necessária
- **Valores inicializados com 0 ao salvar** (não precisa ler Modbus primeiro)
- **registerCount sincronizado** com o tamanho real do array de registros
