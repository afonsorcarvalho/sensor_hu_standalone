# Análise Psicrométrica - Estudo de Umidade

Este script Python gera gráficos para análise psicrométrica, permitindo estudar as mudanças na umidade relativa utilizando a fórmula psicrométrica.

## Fórmula Utilizada

```
UR = ((A*exp((B*TU)/(TU+C))) - (D*(TS-TU))) / (A*exp((B*TS)/(TS+C)))
```

**Constantes:**
- A = 6.112
- B = 17.67
- C = 242.5
- D = 1.8

**Variáveis:**
- **TS** = Temperatura do bulbo seco (temperatura seca) em °C
- **TU** = Temperatura do bulbo úmido (temperatura úmida) em °C  
- **UR** = Umidade relativa em porcentagem (0-100%)

## Padrão de Análise

O script analisa os seguintes casos (TU varia de 10 a 60°C em todos):
- TS fixo em 30°C, TU varia de 10 a 60°C
- TS fixo em 35°C, TU varia de 10 a 60°C
- TS fixo em 40°C, TU varia de 10 a 60°C
- TS fixo em 45°C, TU varia de 10 a 60°C
- TS fixo em 50°C, TU varia de 10 a 60°C
- TS fixo em 55°C, TU varia de 10 a 60°C
- TS fixo em 60°C, TU varia de 10 a 60°C

## Instalação

1. Certifique-se de ter Python 3.7 ou superior instalado
2. Instale as dependências:

```bash
pip install -r requirements.txt
```

Ou manualmente:

```bash
pip install numpy matplotlib scipy
```

## Scripts Disponíveis

### 1. Geração de Gráficos (`grafico_psicrometria.py`)

Execute o script:

```bash
python grafico_psicrometria.py
```

O script irá:
1. Gerar os dados psicrométricos conforme o padrão especificado
2. Criar dois gráficos:
   - Umidade Relativa vs Temperatura Úmida (TU)
   - Umidade Relativa vs Diferença de Temperatura (TS - TU)
3. Exibir uma tabela com os resultados principais
4. Salvar o gráfico em `grafico_psicrometria.png`

### 2. Calibração de Constantes (`calibrar_constantes.py`)

Este script ajusta as constantes A, B, C, D da fórmula psicrométrica usando pontos de referência conhecidos.

**Requisitos:**
- Pelo menos 4 pontos conhecidos com valores de TS (temperatura seca), TU (temperatura úmida) e UR (umidade relativa real)

**Uso:**

```bash
python calibrar_constantes.py
```

O script irá:
1. Perguntar como fornecer os pontos (digitando ou carregando de arquivo)
2. Solicitar/carregar os pontos de calibração (TS, TU, UR)
3. **Perguntar qual método de calibração usar** (TRF, LM, Dogbox, Curve Fit, ou Differential Evolution)
4. Ajustar as constantes usando o método escolhido
5. Gerar gráfico comparando pontos reais vs calculados
6. Exibir as constantes ajustadas e estatísticas de erro
7. Comparar valores calculados vs reais para cada ponto
8. Opcionalmente, salvar as constantes em um arquivo

**Métodos de Calibração Disponíveis:**

1. **TRF (Trust Region Reflective)** - RECOMENDADO
   - Rápido e robusto
   - Bom para maioria dos casos
   - Suporta limites (bounds)

2. **LM (Levenberg-Marquardt)**
   - Muito rápido
   - Bom quando valores iniciais estão próximos da solução
   - Não suporta limites diretamente

3. **Dogbox**
   - Robusto para muitos pontos
   - Bom para problemas com outliers
   - Suporta limites (bounds)

4. **Curve Fit**
   - Método clássico baseado em leastsq
   - Rápido e confiável
   - Suporta limites (bounds)

5. **Differential Evolution**
   - Mais robusto, mas mais lento
   - Bom quando outros métodos falham
   - Não depende tanto dos valores iniciais

**Formas de fornecer os pontos:**

1. **Digitação interativa:**
```
Ponto 1 (TS TU UR em °C, ou Enter para finalizar): 30 25 70
Ponto 2 (TS TU UR em °C, ou Enter para finalizar): 35 28 65
Ponto 3 (TS TU UR em °C, ou Enter para finalizar): 40 32 60
Ponto 4 (TS TU UR em °C, ou Enter para finalizar): 45 35 55
```

2. **Carregar de arquivo:**
O script aceita arquivos de texto com os pontos de calibração. Formato do arquivo:
- Uma linha por ponto
- Cada linha contém: `TS TU UR` (separados por espaço ou vírgula)
- Linhas começando com `#` são comentários (ignoradas)
- Linhas vazias são ignoradas

**Exemplo de arquivo** (`pontos_calibracao_exemplo.txt`):
```
# Arquivo de exemplo para pontos de calibração
# TS TU UR
30.0 25.0 70.0
35.0 28.0 65.0
40.0 32.0 60.0
45.0 35.0 55.0
```

**Gráfico gerado:**
O script gera automaticamente um gráfico com 4 painéis mostrando:
- UR Real vs UR Calculado (com valores de TS e TU anotados)
- Erro por ponto de calibração (com informações de cada ponto)
- UR vs TS (comparação real vs calculado)
- UR vs TU (comparação real vs calculado)

**Saída:**
- Constantes ajustadas (A, B, C, D)
- Erro médio, erro máximo e erro RMS
- Tabela comparativa mostrando UR real vs UR calculado
- Código Python pronto para usar as constantes
- Opção de salvar as constantes em arquivo

## Saída

O script gera:
- Arquivo PNG com os gráficos: `grafico_psicrometria.png`
- Gráfico visual interativo (exibido na tela, se suportado pelo ambiente)
- Tabela de resultados no console

**Nota:** Se o gráfico não aparecer na tela, isso é normal em alguns ambientes. O arquivo PNG será sempre gerado e pode ser aberto manualmente para visualização.

## Solução de Problemas

### Gráfico não aparece na tela
Se você receber um aviso sobre backend não-interativo:
- O gráfico ainda será salvo em `grafico_psicrometria.png`
- Você pode abrir o arquivo PNG manualmente para visualizar
- Para exibição interativa, certifique-se de que o Python está rodando em um ambiente com suporte gráfico (não em servidores sem GUI)
