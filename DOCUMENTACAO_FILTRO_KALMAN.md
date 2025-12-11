# Documentação: Filtro de Kalman

## Visão Geral

O filtro de Kalman é um algoritmo matemático usado para suavizar e filtrar sinais com ruído. No sistema Modbus, ele é aplicado aos valores lidos dos registros para reduzir variações indesejadas e ruídos, mantendo a precisão das medições.

## O que é o Filtro de Kalman?

O filtro de Kalman é um estimador recursivo que combina:
- **Predições** baseadas em um modelo do sistema
- **Medições** reais do sensor

O resultado é uma estimativa mais precisa e suave do valor real, eliminando ruídos e variações rápidas que não representam mudanças reais no processo.

### Vantagens

- ✅ **Reduz ruído** nas leituras dos sensores
- ✅ **Suaviza variações rápidas** que não são reais
- ✅ **Mantém responsividade** a mudanças reais do processo
- ✅ **Melhora a qualidade dos dados** para cálculos e tomadas de decisão
- ✅ **Configurável por registro** - cada sensor pode ter sua própria configuração

## Quando Usar o Filtro de Kalman?

Use o filtro de Kalman quando:

- 🔸 Sensores apresentam **ruído significativo** nas leituras
- 🔸 Valores **oscilam muito** sem motivo aparente
- 🔸 É necessário **suavizar dados** para cálculos ou visualização
- 🔸 O processo é **relativamente estável** (não muda muito rapidamente)

**Não use** quando:
- ❌ O processo muda **muito rapidamente** (o filtro pode atrasar a resposta)
- ❌ É necessário **detectar mudanças instantâneas** com precisão
- ❌ Os dados já são **muito precisos** e não precisam de suavização

## Parâmetros de Configuração

O filtro de Kalman possui dois parâmetros principais que controlam seu comportamento:

### Q (Process Noise) - Ruído do Processo

**O que representa:**
- Quanto o valor real do processo pode variar entre uma leitura e outra
- A incerteza sobre como o processo muda ao longo do tempo

**Valores recomendados:**
- **Padrão:** `0.01`
- **Faixa típica:** `0.001` a `0.1`

**Efeitos:**

| Valor de Q | Comportamento | Uso Recomendado |
|------------|---------------|-----------------|
| **Menor** (0.001) | Filtro mais responsivo<br>Menos suavização<br>Responde rápido a mudanças | Processos que mudam rapidamente<br>Sensores com pouco ruído |
| **Médio** (0.01) | Balanceado<br>Boa suavização e responsividade | **Uso geral (recomendado)** |
| **Maior** (0.1) | Filtro mais suave<br>Mais suavização<br>Responde mais devagar | Processos muito estáveis<br>Sensores com muito ruído |

**Exemplo prático:**
```
Q = 0.001 → Filtro "confia" que o valor não muda muito
            Resultado: Mais responsivo, menos suavização

Q = 0.1   → Filtro "espera" que o valor possa variar mais
            Resultado: Mais suave, mais suavização
```

### R (Measurement Noise) - Ruído da Medição

**O que representa:**
- Quanto o sensor é preciso
- A confiança nas leituras do sensor
- O nível de ruído nas medições

**Valores recomendados:**
- **Padrão:** `0.1`
- **Faixa típica:** `0.01` a `1.0`

**Efeitos:**

| Valor de R | Comportamento | Uso Recomendado |
|------------|---------------|-----------------|
| **Menor** (0.01) | Confia muito nas medições<br>Menos suavização<br>Valores mais próximos do sensor | Sensores muito precisos<br>Pouco ruído nas leituras |
| **Médio** (0.1) | Balanceado<br>Boa confiança nas medições | **Uso geral (recomendado)** |
| **Maior** (1.0) | Confia menos nas medições<br>Mais suavização<br>Valores mais suaves | Sensores com muito ruído<br>Leituras instáveis |

**Exemplo prático:**
```
R = 0.01 → Filtro "confia" muito no sensor
           Resultado: Valores próximos do sensor, menos suavização

R = 1.0  → Filtro "desconfia" do sensor
           Resultado: Valores mais suaves, mais suavização
```

## Como Configurar

### Passo a Passo

1. **Acesse a interface web** do sistema
2. **Navegue até a seção Modbus**
3. **Selecione um dispositivo** e abra um registro
4. **Marque o checkbox "Filtro de Kalman"**
5. **Ajuste os parâmetros Q e R** conforme necessário
6. **Salve a configuração**

### Configurações Recomendadas por Tipo de Sensor

#### Sensores de Temperatura
```
Q = 0.01
R = 0.1
```
Temperatura geralmente muda lentamente, então valores médios funcionam bem.

#### Sensores de Umidade
```
Q = 0.01
R = 0.2
```
Umidade pode ter mais ruído, então R um pouco maior ajuda.

#### Sensores de Pressão
```
Q = 0.005
R = 0.05
```
Pressão geralmente é mais estável, então valores menores funcionam.

#### Sensores com Muito Ruído
```
Q = 0.01
R = 0.5 a 1.0
```
Aumente R para mais suavização quando o sensor tem muito ruído.

#### Sensores Precisos (Pouco Ruído)
```
Q = 0.01
R = 0.01 a 0.05
```
Diminua R quando o sensor é muito preciso e você quer menos suavização.

## Ajuste Fino dos Parâmetros

### Método de Teste

1. **Configure valores padrão** (Q = 0.01, R = 0.1)
2. **Observe o comportamento** do valor filtrado
3. **Ajuste conforme necessário:**

   **Se o valor está muito "travado" (não responde a mudanças):**
   - Diminua Q (ex: 0.005)
   - Diminua R (ex: 0.05)

   **Se o valor ainda oscila muito (ruído visível):**
   - Aumente R (ex: 0.2 a 0.5)
   - Mantenha Q em 0.01

   **Se o valor responde muito rápido (pouca suavização):**
   - Aumente R (ex: 0.2)
   - Mantenha Q em 0.01

   **Se o valor responde muito devagar (muita suavização):**
   - Diminua R (ex: 0.05)
   - Diminua Q (ex: 0.005)

### Regra Geral

- **Q controla a responsividade** (quão rápido o filtro responde)
- **R controla a suavização** (quanto ruído é removido)

**Dica:** Comece com valores padrão e ajuste R primeiro, pois ele tem mais impacto na suavização.

## Exemplos Práticos

### Exemplo 1: Sensor de Temperatura com Ruído Moderado

**Situação:**
- Sensor lê valores entre 20°C e 25°C
- Valores oscilam ±0.5°C devido ao ruído
- Temperatura real muda lentamente

**Configuração:**
```
Q = 0.01
R = 0.15
```

**Resultado:**
- Ruído reduzido significativamente
- Valores mais estáveis
- Resposta adequada a mudanças reais de temperatura

### Exemplo 2: Sensor de Umidade com Muito Ruído

**Situação:**
- Sensor lê valores entre 40% e 60%
- Valores oscilam ±2% devido ao ruído
- Umidade real muda moderadamente

**Configuração:**
```
Q = 0.01
R = 0.5
```

**Resultado:**
- Ruído reduzido drasticamente
- Valores muito mais suaves
- Resposta um pouco mais lenta, mas aceitável

### Exemplo 3: Sensor Preciso de Pressão

**Situação:**
- Sensor muito preciso
- Pouco ruído nas leituras
- Precisa detectar mudanças rápidas

**Configuração:**
```
Q = 0.01
R = 0.05
```

**Resultado:**
- Suavização leve
- Resposta rápida a mudanças
- Valores próximos do sensor original

## Entendendo o Comportamento

### Como o Filtro Funciona

1. **Predição:** O filtro "prevê" o próximo valor baseado no valor anterior
2. **Medição:** O sensor fornece um novo valor (com ruído)
3. **Fusão:** O filtro combina predição e medição usando o ganho de Kalman
4. **Resultado:** Um valor mais preciso e suave

### Ganho de Kalman

O ganho de Kalman (K) determina quanto confiar na nova medição:

```
K = (P + Q) / (P + Q + R)

Onde:
- P = Covariância do erro anterior
- Q = Process noise
- R = Measurement noise
```

- **K próximo de 1:** Confia muito na medição (menos suavização)
- **K próximo de 0:** Confia pouco na medição (mais suavização)

## Limitações

⚠️ **Importante entender:**

1. **O filtro adiciona um pequeno atraso** - valores filtrados podem estar ligeiramente "atrasados" em relação ao valor real
2. **Não remove todos os ruídos** - ruídos muito grandes ainda podem aparecer
3. **Pode mascarar mudanças rápidas** - se o processo muda muito rápido, o filtro pode não acompanhar
4. **Configuração requer teste** - cada sensor pode precisar de ajustes diferentes

## Dicas Finais

✅ **Comece com valores padrão** (Q = 0.01, R = 0.1)

✅ **Ajuste R primeiro** - tem mais impacto na suavização

✅ **Teste em condições reais** - observe o comportamento durante o uso normal

✅ **Documente suas configurações** - anote os valores que funcionam bem para cada sensor

✅ **Use valores diferentes por registro** - cada sensor pode ter suas próprias necessidades

✅ **Monitore o comportamento** - se o filtro não está funcionando bem, ajuste os parâmetros

## Referências Técnicas

O filtro de Kalman implementado é um **filtro de Kalman 1D** (unidimensional) com modelo constante. Isso significa:

- Assume que o valor não muda entre leituras (modelo constante)
- Processa um valor por vez (1D)
- É otimizado para uso em microcontroladores (baixo uso de memória e processamento)

Para mais informações sobre o algoritmo de Kalman, consulte:
- [Filtro de Kalman - Wikipedia](https://pt.wikipedia.org/wiki/Filtro_de_Kalman)
- Documentação técnica sobre estimação de estados

---

**Versão:** 1.0  
**Data:** 2024  
**Autor:** Sistema Modbus RTU Master ESP32

