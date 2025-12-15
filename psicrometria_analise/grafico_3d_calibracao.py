"""
Script para gerar gráfico 3D da calibração psicrométrica
Mostra os pontos de calibração e a superfície ajustada UR(TS, TU)
"""

import numpy as np
import matplotlib
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
from matplotlib import rcParams
import re

# Configura o backend de forma segura
backend_escolhido = None
try:
    import tkinter
    matplotlib.use('TkAgg', force=True)
    backend_escolhido = 'TkAgg'
except (ImportError, ValueError, RuntimeError, Exception):
    pass

if backend_escolhido is None:
    matplotlib.use('Agg', force=True)
    backend_escolhido = 'Agg'

# Configuração para suportar caracteres especiais (acentos)
rcParams['font.family'] = 'DejaVu Sans'
rcParams['axes.unicode_minus'] = False


def calcular_umidade_relativa(TS, TU, A, B, C, D):
    """
    Calcula a umidade relativa usando a fórmula psicrométrica com constantes específicas.
    
    Parâmetros:
    -----------
    TS : float ou array
        Temperatura do bulbo seco (temperatura seca) em graus Celsius
    TU : float ou array
        Temperatura do bulbo úmido (temperatura úmida) em graus Celsius
    A, B, C, D : float
        Constantes da fórmula psicrométrica
    
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


def carregar_pontos_de_arquivo(nome_arquivo):
    """
    Carrega pontos de calibração de um arquivo.
    
    Formato do arquivo:
    - Uma linha por ponto
    - Cada linha contém: TS TU UR (separados por espaço ou vírgula)
    - Linhas que começam com # são ignoradas (comentários)
    - Linhas vazias são ignoradas
    
    Parâmetros:
    -----------
    nome_arquivo : str
        Nome do arquivo a ser lido
    
    Retorna:
    --------
    list
        Lista de pontos no formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    """
    pontos = []
    
    try:
        with open(nome_arquivo, 'r', encoding='utf-8') as f:
            for num_linha, linha in enumerate(f, 1):
                linha = linha.strip()
                
                # Ignora linhas vazias e comentários
                if not linha or linha.startswith('#'):
                    continue
                
                # Tenta separar por espaço ou vírgula
                valores = None
                if ',' in linha:
                    valores = [v.strip() for v in linha.split(',')]
                else:
                    valores = linha.split()
                
                if len(valores) < 3:
                    print(f"Aviso: Linha {num_linha} ignorada (menos de 3 valores): {linha}")
                    continue
                
                try:
                    TS = float(valores[0])
                    TU = float(valores[1])
                    UR = float(valores[2])
                    pontos.append([TS, TU, UR])
                    print(f"  Ponto carregado: TS={TS}°C, TU={TU}°C, UR={UR}%")
                    
                except ValueError as e:
                    print(f"Erro na linha {num_linha}: valores inválidos - {linha}")
                    continue
        
        if len(pontos) == 0:
            raise ValueError("Nenhum ponto válido encontrado no arquivo!")
        
        return pontos
    
    except FileNotFoundError:
        raise FileNotFoundError(f"Arquivo não encontrado: {nome_arquivo}")
    except Exception as e:
        raise Exception(f"Erro ao ler arquivo: {e}")


def carregar_constantes_de_arquivo(nome_arquivo):
    """
    Carrega constantes A, B, C, D de um arquivo de constantes.
    
    Parâmetros:
    -----------
    nome_arquivo : str
        Nome do arquivo de constantes
    
    Retorna:
    --------
    dict
        Dicionário com as constantes {'A': float, 'B': float, 'C': float, 'D': float}
    """
    constantes = {}
    
    try:
        with open(nome_arquivo, 'r', encoding='utf-8') as f:
            for linha in f:
                linha = linha.strip()
                
                # Ignora comentários e linhas vazias
                if not linha or linha.startswith('#'):
                    continue
                
                # Procura por padrões A = valor, B = valor, etc.
                match = re.match(r'([A-D])\s*=\s*([0-9.]+)', linha, re.IGNORECASE)
                if match:
                    constante = match.group(1).upper()
                    valor = float(match.group(2))
                    constantes[constante] = valor
                    print(f"  Constante {constante} = {valor}")
        
        # Verifica se todas as constantes foram encontradas
        for c in ['A', 'B', 'C', 'D']:
            if c not in constantes:
                raise ValueError(f"Constante {c} não encontrada no arquivo!")
        
        return constantes
    
    except FileNotFoundError:
        raise FileNotFoundError(f"Arquivo não encontrado: {nome_arquivo}")
    except Exception as e:
        raise Exception(f"Erro ao ler arquivo de constantes: {e}")


def plotar_grafico_3d(pontos_calibracao, constantes, nome_arquivo_saida='calibracao_3d.png'):
    """
    Gera gráfico 3D mostrando os pontos de calibração e a superfície ajustada.
    
    Parâmetros:
    -----------
    pontos_calibracao : list
        Lista de pontos no formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    constantes : dict
        Dicionário com as constantes {'A': float, 'B': float, 'C': float, 'D': float}
    nome_arquivo_saida : str
        Nome do arquivo de saída para salvar o gráfico
    """
    pontos = np.array(pontos_calibracao)
    TS_pontos = pontos[:, 0]
    TU_pontos = pontos[:, 1]
    UR_pontos = pontos[:, 2]
    
    # Extrai constantes
    A = constantes['A']
    B = constantes['B']
    C = constantes['C']
    D = constantes['D']
    
    # Define limites para a superfície baseado nos pontos de calibração
    TS_min = max(15, np.min(TS_pontos) - 5)
    TS_max = min(50, np.max(TS_pontos) + 5)
    TU_min = max(15, np.min(TU_pontos) - 5)
    TU_max = min(40, np.max(TU_pontos) + 5)
    
    # Garante que TU <= TS (fisicamente correto)
    TU_max = min(TU_max, TS_max)
    
    # Cria malha de pontos para a superfície
    TS_superficie = np.linspace(TS_min, TS_max, 50)
    TU_superficie = np.linspace(TU_min, TU_max, 50)
    TS_grid, TU_grid = np.meshgrid(TS_superficie, TU_superficie)
    
    # Calcula UR para cada ponto da malha
    UR_superficie = calcular_umidade_relativa(TS_grid, TU_grid, A, B, C, D)
    
    # Cria figura 3D
    fig = plt.figure(figsize=(14, 10))
    ax = fig.add_subplot(111, projection='3d')
    
    # Plota a superfície ajustada
    surf = ax.plot_surface(TS_grid, TU_grid, UR_superficie, 
                          cmap='viridis', alpha=0.7, 
                          linewidth=0, antialiased=True,
                          shade=True)
    
    # Adiciona barra de cores
    fig.colorbar(surf, ax=ax, shrink=0.5, aspect=20, label='UR (%)')
    
    # Plota os pontos de calibração reais
    ax.scatter(TS_pontos, TU_pontos, UR_pontos, 
              color='red', s=200, marker='o', 
              edgecolors='black', linewidths=2, 
              label='Pontos de Calibração', zorder=5)
    
    # Calcula e plota os pontos calculados (sobre a superfície)
    UR_calculados = calcular_umidade_relativa(TS_pontos, TU_pontos, A, B, C, D)
    ax.scatter(TS_pontos, TU_pontos, UR_calculados, 
              color='yellow', s=150, marker='^', 
              edgecolors='black', linewidths=1.5, 
              label='Pontos Calculados', zorder=5)
    
    # Desenha linhas conectando pontos reais aos calculados
    for i in range(len(TS_pontos)):
        ax.plot([TS_pontos[i], TS_pontos[i]], 
               [TU_pontos[i], TU_pontos[i]], 
               [UR_pontos[i], UR_calculados[i]], 
               'r--', linewidth=1.5, alpha=0.6, zorder=4)
    
    # Adiciona anotações para cada ponto
    for i, (ts, tu, ur) in enumerate(zip(TS_pontos, TU_pontos, UR_pontos)):
        ax.text(ts, tu, ur, f'  P{i+1}', fontsize=8, 
               bbox=dict(boxstyle='round,pad=0.3', facecolor='yellow', alpha=0.7))
    
    # Configuração dos eixos
    ax.set_xlabel('Temperatura do Bulbo Seco - TS (°C)', fontsize=12, fontweight='bold', labelpad=10)
    ax.set_ylabel('Temperatura do Bulbo Úmido - TU (°C)', fontsize=12, fontweight='bold', labelpad=10)
    ax.set_zlabel('Umidade Relativa - UR (%)', fontsize=12, fontweight='bold', labelpad=10)
    
    # Título
    ax.set_title(f'Calibração Psicrométrica 3D\n'
                f'Constantes: A={A:.4f}, B={B:.4f}, C={C:.4f}, D={D:.4f}', 
                fontsize=14, fontweight='bold', pad=20)
    
    # Ajusta os limites dos eixos
    ax.set_xlim(TS_min, TS_max)
    ax.set_ylim(TU_min, TU_max)
    ax.set_zlim(0, 100)
    
    # Adiciona legenda
    ax.legend(loc='upper left', fontsize=10)
    
    # Ajusta o ângulo de visualização para melhor visualização
    ax.view_init(elev=25, azim=45)
    
    plt.tight_layout()
    
    # Salva o gráfico
    plt.savefig(nome_arquivo_saida, dpi=300, bbox_inches='tight')
    print(f"\nGráfico 3D salvo em: {nome_arquivo_saida}")
    
    # Tenta mostrar o gráfico
    try:
        backend_atual = matplotlib.get_backend()
        if backend_atual.lower() != 'agg':
            plt.show()
        else:
            print("Para visualizar, abra o arquivo '{}'".format(nome_arquivo_saida))
    except Exception as e:
        print(f"Aviso: Não foi possível exibir o gráfico interativamente: {e}")
        print("Para visualizar, abra o arquivo '{}'".format(nome_arquivo_saida))
    finally:
        plt.close()


def main():
    """
    Função principal do script.
    """
    import sys
    
    print("="*80)
    print("GERAÇÃO DE GRÁFICO 3D DA CALIBRAÇÃO PSICROMÉTRICA")
    print("="*80)
    print("\nEste script gera um gráfico 3D mostrando:")
    print("  - Os pontos de calibração reais")
    print("  - A superfície ajustada UR(TS, TU)")
    print("  - Os pontos calculados pela fórmula\n")
    
    # Valores padrão
    arquivo_pontos = "pontos_calibracao.txt"
    arquivo_constantes = "constantes_trf.txt"
    arquivo_saida = "calibracao_3d.png"
    
    # Aceita argumentos da linha de comando
    if len(sys.argv) > 1:
        arquivo_pontos = sys.argv[1]
    if len(sys.argv) > 2:
        arquivo_constantes = sys.argv[2]
    if len(sys.argv) > 3:
        arquivo_saida = sys.argv[3]
    
    # Se não houver argumentos, pergunta interativamente (se possível)
    if len(sys.argv) == 1:
        try:
            # Pergunta o arquivo de pontos
            entrada = input("Nome do arquivo com os pontos de calibração (padrão: pontos_calibracao.txt): ").strip()
            if entrada:
                arquivo_pontos = entrada
            
            # Pergunta o arquivo de constantes
            entrada = input("Nome do arquivo com as constantes (padrão: constantes_trf.txt): ").strip()
            if entrada:
                arquivo_constantes = entrada
            
            # Pergunta o arquivo de saída
            entrada = input("Nome do arquivo de saída (padrão: calibracao_3d.png): ").strip()
            if entrada:
                arquivo_saida = entrada
        except (EOFError, KeyboardInterrupt):
            # Se não houver entrada disponível, usa valores padrão
            print("\nUsando valores padrão (entrada não disponível)...")
            print(f"  Pontos: {arquivo_pontos}")
            print(f"  Constantes: {arquivo_constantes}")
            print(f"  Saída: {arquivo_saida}\n")
    
    try:
        print("\n" + "="*80)
        print("Carregando pontos de calibração...")
        print("="*80)
        pontos_calibracao = carregar_pontos_de_arquivo(arquivo_pontos)
        print(f"\nTotal de {len(pontos_calibracao)} pontos carregados.")
        
        print("\n" + "="*80)
        print("Carregando constantes...")
        print("="*80)
        constantes = carregar_constantes_de_arquivo(arquivo_constantes)
        print(f"\nConstantes carregadas: A={constantes['A']:.6f}, B={constantes['B']:.6f}, "
              f"C={constantes['C']:.6f}, D={constantes['D']:.6f}")
        
        print("\n" + "="*80)
        print("Gerando gráfico 3D...")
        print("="*80)
        plotar_grafico_3d(pontos_calibracao, constantes, arquivo_saida)
        
        print("\n" + "="*80)
        print("Processo concluído com sucesso!")
        print("="*80)
        
    except Exception as e:
        print(f"\nErro: {e}")
        import traceback
        traceback.print_exc()


if __name__ == "__main__":
    main()
