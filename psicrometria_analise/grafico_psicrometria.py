"""
Script para análise psicrométrica - Cálculo de umidade relativa
Este script gera gráficos para estudar as mudanças na umidade usando psicrometria.
A = 6.112
B = 17.67
C = 242.5
D = 1.8
Fórmula utilizada:
UR = ((A*exp((B*TU)/(TU+C))) - (D*(TS-TU))) / (A*exp((B*TS)/(TS+C)))

Onde:
- TS = Temperatura do bulbo seco (temperatura seca)
- TU = Temperatura do bulbo úmido (temperatura úmida)
- UR = Umidade relativa (resultado)
"""

import numpy as np
import matplotlib

# Configura o backend de forma segura
# Verifica quais módulos estão disponíveis ANTES de configurar o backend
backend_escolhido = None

# Tenta TkAgg primeiro (mais comum no Windows, geralmente já vem com Python)
try:
    import tkinter
    # Testa se realmente consegue usar o backend
    matplotlib.use('TkAgg', force=True)
    backend_escolhido = 'TkAgg'
except (ImportError, ValueError, RuntimeError, Exception):
    pass

# Se TkAgg não funcionou, NÃO tenta Qt5Agg (requer instalação adicional)
# Vai direto para Agg que sempre funciona

# Se nenhum backend interativo funcionou, usa Agg (sempre disponível)
if backend_escolhido is None:
    matplotlib.use('Agg', force=True)
    backend_escolhido = 'Agg'

import matplotlib.pyplot as plt
from matplotlib import rcParams

# Configuração para suportar caracteres especiais (acentos)
rcParams['font.family'] = 'DejaVu Sans'
rcParams['axes.unicode_minus'] = False

# Constantes da fórmula psicrométrica
A = 1
B = 25
C = 111
D = 5

def calcular_umidade_relativa(TS, TU):
    """
    Calcula a umidade relativa usando a fórmula psicrométrica.
    
    Parâmetros:
    -----------
    TS : float ou array
        Temperatura do bulbo seco (temperatura seca) em graus Celsius
    TU : float ou array
        Temperatura do bulbo úmido (temperatura úmida) em graus Celsius
    
    Retorna:
    --------
    float ou array
        Umidade relativa em porcentagem (0-100)
    """
    # Fórmula psicrométrica usando constantes A, B, C, D
    # Numerador: pressão de vapor atual
    numerador = (A * np.exp((B * TU) / (TU + C))) - (D * (TS - TU))
    
    # Denominador: pressão de vapor de saturação na temperatura do bulbo seco
    denominador = A * np.exp((B * TS) / (TS + C))
    
    # Umidade relativa em porcentagem
    UR = (numerador / denominador) * 100
    
    # Limita valores entre 0 e 100
    UR = np.clip(UR, 0, 100)
    
    return UR


def gerar_dados_psicrometria():
    """
    Gera os dados para análise psicrométrica conforme o padrão especificado:
    - TS fixo em 30°C, TU varia de 10 a 60°C
    - TS fixo em 35°C, TU varia de 10 a 60°C
    - TS fixo em 40°C, TU varia de 10 a 60°C
    - E assim por diante até TS = 60°C
    - Em todos os casos, TU varia de 10 a 60°C
    
    Retorna:
    --------
    dict
        Dicionário com TS como chave e tuplas (TU_array, UR_array) como valores
    """
    dados = {}
    
    # TU varia sempre de 10 a 60 graus para todos os valores de TS
    TU_min = 10
    TU_max = 60
    TU_array = np.arange(TU_min, TU_max + 0.5, 0.5)
    
    # Gera dados de 30 a 60 graus para TS (de 5 em 5 graus)
    for TS in range(30, 65, 5):
        # Calcula umidade relativa para cada combinação
        UR_array = calcular_umidade_relativa(TS, TU_array)
        
        dados[TS] = (TU_array, UR_array)
    
    return dados


def plotar_graficos(dados):
    """
    Gera gráficos dos dados psicrométricos.
    
    Parâmetros:
    -----------
    dados : dict
        Dicionário com os dados gerados por gerar_dados_psicrometria()
    """
    # Configuração da figura
    fig, axes = plt.subplots(2, 1, figsize=(12, 10))
    
    # Gráfico 1: Umidade relativa vs Temperatura úmida (TU) para cada TS
    ax1 = axes[0]
    for TS in sorted(dados.keys()):
        TU_array, UR_array = dados[TS]
        ax1.plot(TU_array, UR_array, marker='o', markersize=4, linewidth=2, 
                label=f'TS = {TS}°C', alpha=0.8)
    
    ax1.set_xlabel('Temperatura do Bulbo Úmido - TU (°C)', fontsize=12, fontweight='bold')
    ax1.set_ylabel('Umidade Relativa (%)', fontsize=12, fontweight='bold')
    ax1.set_title('Análise Psicrométrica: Umidade Relativa vs Temperatura Úmida', 
                  fontsize=14, fontweight='bold')
    ax1.grid(True, alpha=0.3, linestyle='--')
    ax1.legend(loc='best', fontsize=10, ncol=2)
    ax1.set_xlim(10, 60)
    ax1.set_ylim(0, 110)
    
    # Gráfico 2: Umidade relativa vs Diferença (TS - TU)
    ax2 = axes[1]
    for TS in sorted(dados.keys()):
        TU_array, UR_array = dados[TS]
        diferenca = TS - TU_array
        ax2.plot(diferenca, UR_array, marker='s', markersize=4, linewidth=2, 
                label=f'TS = {TS}°C', alpha=0.8)
    
    ax2.set_xlabel('Diferença de Temperatura (TS - TU) (°C)', fontsize=12, fontweight='bold')
    ax2.set_ylabel('Umidade Relativa (%)', fontsize=12, fontweight='bold')
    ax2.set_title('Análise Psicrométrica: Umidade Relativa vs Diferença de Temperatura', 
                  fontsize=14, fontweight='bold')

    # Mais divisões no grid: minor ticks/lines
    from matplotlib.ticker import MultipleLocator, AutoMinorLocator

    ax2.grid(True, alpha=0.3, linestyle='--', which='major')
    ax2.grid(True, alpha=0.15, linestyle=':', which='minor')

    # Define mais subdivisões no eixo x e y
    ax2.xaxis.set_major_locator(MultipleLocator(10))
    ax2.xaxis.set_minor_locator(MultipleLocator(2))
    ax2.yaxis.set_major_locator(MultipleLocator(10))
    ax2.yaxis.set_minor_locator(MultipleLocator(2))

    ax2.legend(loc='best', fontsize=10, ncol=2)
    # Diferença máxima: TS=60 e TU=10 → 50°C, diferença mínima: TS=30 e TU=60 → -30°C
    ax2.set_xlim(0, 35)
    ax2.set_ylim(0, 110)
    
    plt.tight_layout()
    
    # Salva o gráfico
    plt.savefig('grafico_psicrometria.png', dpi=300, bbox_inches='tight')
    print("Gráfico salvo em: grafico_psicrometria.png")
    
    # Tenta mostrar o gráfico apenas se o backend suportar exibição interativa
    try:
        backend_atual = matplotlib.get_backend()
        if backend_atual.lower() != 'agg':
            plt.show()
        else:
            print("Backend não-interativo detectado. O gráfico foi salvo em 'grafico_psicrometria.png'")
            print("Para visualizar, abra o arquivo 'grafico_psicrometria.png'")
    except Exception as e:
        print(f"Aviso: Não foi possível exibir o gráfico interativamente: {e}")
        print("O gráfico foi salvo com sucesso em 'grafico_psicrometria.png'")
        print("Para visualizar, abra o arquivo 'grafico_psicrometria.png'")
    finally:
        plt.close()  # Fecha a figura para liberar memória


def gerar_tabela_resultados(dados):
    """
    Gera uma tabela com os resultados principais.
    
    Parâmetros:
    -----------
    dados : dict
        Dicionário com os dados gerados por gerar_dados_psicrometria()
    """
    print("\n" + "="*80)
    print("RESULTADOS DA ANÁLISE PSICROMÉTRICA")
    print("="*80)
    print(f"{'TS (°C)':<10} {'TU (°C)':<15} {'TS-TU (°C)':<15} {'UR (%)':<15}")
    print("-"*80)
    
    for TS in sorted(dados.keys()):
        TU_array, UR_array = dados[TS]
        # Mostra alguns pontos representativos (início, meio e fim)
        indices = [0, len(TU_array)//2, len(TU_array)-1]
        for idx in indices:
            if idx < len(TU_array):
                TU = TU_array[idx]
                UR = UR_array[idx]
                diferenca = TS - TU
                print(f"{TS:<10.1f} {TU:<15.1f} {diferenca:<15.1f} {UR:<15.2f}")
        print("-"*80)


def main():
    """
    Função principal do script.
    """
    print("Gerando dados psicrométricos...")
    dados = gerar_dados_psicrometria()
    
    print("Gerando gráficos...")
    plotar_graficos(dados)
    
    print("Gerando tabela de resultados...")
    gerar_tabela_resultados(dados)
    
    print("\nAnálise concluída!")


if __name__ == "__main__":
    main()
