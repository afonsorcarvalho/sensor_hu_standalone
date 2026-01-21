"""
Script de Calibração das Constantes Psicrométricas
Este script ajusta as constantes A, B, C, D da fórmula psicrométrica usando pontos conhecidos.

Fórmula utilizada:
UR = ((A*exp((B*TU)/(TU+C))) - (D*(TS-TU))) / (A*exp((B*TS)/(TS+C)))

Onde:
- TS = Temperatura do bulbo seco (temperatura seca)
- TU = Temperatura do bulbo úmido (temperatura úmida)
- UR = Umidade relativa (resultado)

Este script requer 4 pontos de calibração com valores conhecidos de TS, TU e UR.
"""

import numpy as np
from scipy.optimize import least_squares, curve_fit, differential_evolution
import matplotlib
import matplotlib.pyplot as plt
from matplotlib import rcParams

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
    
    return UR


def funcao_residuo(constantes, pontos_calibracao):
    """
    Calcula o resíduo (erro) entre os valores de UR calculados e os valores reais.
    
    Parâmetros:
    -----------
    constantes : array
        Array com [A, B, C, D]
    pontos_calibracao : array
        Array com formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    
    Retorna:
    --------
    array
        Array com os resíduos (diferença entre UR calculado e UR real)
    """
    A, B, C, D = constantes
    
    residuos = []
    for TS, TU, UR_real in pontos_calibracao:
        # Calcula UR usando as constantes
        UR_calculado = calcular_umidade_relativa(TS, TU, A, B, C, D)
        
        # Calcula o resíduo (diferença)
        residuo = UR_real - UR_calculado
        residuos.append(residuo)
    
    return np.array(residuos)


def funcao_para_curve_fit(pontos_TS_TU, A, B, C, D):
    """
    Função wrapper para curve_fit.
    Recebe TS e TU como arrays separados.
    """
    TS_array, TU_array = pontos_TS_TU
    UR_array = []
    for TS, TU in zip(TS_array, TU_array):
        UR = calcular_umidade_relativa(TS, TU, A, B, C, D)
        UR_array.append(UR)
    return np.array(UR_array)


def calibrar_constantes(pontos_calibracao, constantes_iniciais=None, metodo='trf'):
    """
    Ajusta as constantes A, B, C, D usando os pontos de calibração.
    
    Parâmetros:
    -----------
    pontos_calibracao : list
        Lista de tuplas ou listas no formato [(TS1, TU1, UR1), (TS2, TU2, UR2), ...]
        Deve conter pelo menos 4 pontos
    constantes_iniciais : list, optional
        Valores iniciais para as constantes [A, B, C, D]
        Se None, usa valores padrão: [6.112, 17.67, 242.5, 1.8]
    metodo : str, optional
        Método de otimização a ser usado:
        - 'trf': Trust Region Reflective (padrão, rápido e robusto)
        - 'lm': Levenberg-Marquardt (rápido, não suporta bounds)
        - 'dogbox': Dogbox (robusto para problemas com muitos pontos)
        - 'curve_fit': Usa curve_fit (baseado em leastsq)
        - 'differential_evolution': Evolução diferencial (mais lento, mais robusto)
    
    Retorna:
    --------
    dict
        Dicionário com as constantes ajustadas e informações sobre o ajuste
    """
    # Converte para array numpy
    pontos = np.array(pontos_calibracao)
    
    # Verifica se há pelo menos 4 pontos
    if len(pontos) < 4:
        raise ValueError("São necessários pelo menos 4 pontos de calibração!")
    
    # Define valores iniciais se não fornecidos
    if constantes_iniciais is None:
        constantes_iniciais = [6.112, 17.67, 243.5, 1.8]
    
    # Limites para as constantes (para evitar valores inválidos)
    # A > 0, B > 0, C > 0, D > 0
    limites_inferiores = [0.01, 0.01, 1.0, 0.0008]
    limites_superiores = [100.0, 100.0, 600.0, 5.0]
    
    resultado_otimizacao = None
    sucesso = True
    mensagem = ""
    
    try:
        if metodo == 'trf':
            # Trust Region Reflective - método padrão, rápido e robusto
            resultado_otimizacao = least_squares(
                funcao_residuo,
                constantes_iniciais,
                args=(pontos,),
                bounds=(limites_inferiores, limites_superiores),
                method='trf'
            )
            constantes_ajustadas = resultado_otimizacao.x
            sucesso = resultado_otimizacao.success
            mensagem = resultado_otimizacao.message
            
        elif metodo == 'lm':
            # Levenberg-Marquardt - rápido, mas não suporta bounds diretamente
            resultado_otimizacao = least_squares(
                funcao_residuo,
                constantes_iniciais,
                args=(pontos,),
                method='lm'
            )
            constantes_ajustadas = resultado_otimizacao.x
            # Aplica bounds manualmente (clipa valores)
            for i in range(len(constantes_ajustadas)):
                constantes_ajustadas[i] = np.clip(constantes_ajustadas[i], 
                                                 limites_inferiores[i], 
                                                 limites_superiores[i])
            sucesso = resultado_otimizacao.success
            mensagem = resultado_otimizacao.message
            
        elif metodo == 'dogbox':
            # Dogbox - robusto para muitos pontos
            resultado_otimizacao = least_squares(
                funcao_residuo,
                constantes_iniciais,
                args=(pontos,),
                bounds=(limites_inferiores, limites_superiores),
                method='dogbox'
            )
            constantes_ajustadas = resultado_otimizacao.x
            sucesso = resultado_otimizacao.success
            mensagem = resultado_otimizacao.message
            
        elif metodo == 'curve_fit':
            # curve_fit - método clássico baseado em leastsq
            TS_array = pontos[:, 0]
            TU_array = pontos[:, 1]
            UR_reais = pontos[:, 2]
            
            popt, pcov = curve_fit(
                funcao_para_curve_fit,
                (TS_array, TU_array),
                UR_reais,
                p0=constantes_iniciais,
                bounds=(limites_inferiores, limites_superiores),
                maxfev=10000
            )
            constantes_ajustadas = popt
            sucesso = True
            mensagem = "Curve fit convergiu"
            
        elif metodo == 'differential_evolution':
            # Evolução diferencial - mais lento mas muito robusto
            def objetivo(constantes):
                residuos = funcao_residuo(constantes, pontos)
                return np.sum(residuos**2)
            
            bounds_de = [(limites_inferiores[i], limites_superiores[i]) 
                        for i in range(4)]
            
            resultado_otimizacao = differential_evolution(
                objetivo,
                bounds_de,
                seed=42,
                maxiter=1000,
                popsize=15
            )
            constantes_ajustadas = resultado_otimizacao.x
            sucesso = resultado_otimizacao.success
            mensagem = resultado_otimizacao.message
            
        else:
            raise ValueError(f"Método desconhecido: {metodo}")
        
        A_ajustado, B_ajustado, C_ajustado, D_ajustado = constantes_ajustadas
        
    except Exception as e:
        # Em caso de erro, usa valores padrão
        print(f"Erro no método {metodo}: {e}")
        print("Usando valores iniciais como fallback...")
        A_ajustado, B_ajustado, C_ajustado, D_ajustado = constantes_iniciais
        sucesso = False
        mensagem = f"Erro: {str(e)}"
    
    # Calcula os erros para cada ponto
    erros = []
    UR_calculados = []
    for TS, TU, UR_real in pontos:
        UR_calc = calcular_umidade_relativa(TS, TU, A_ajustado, B_ajustado, C_ajustado, D_ajustado)
        UR_calculados.append(UR_calc)
        erro = abs(UR_real - UR_calc)
        erros.append(erro)
    
    # Calcula estatísticas
    erro_medio = np.mean(erros)
    erro_maximo = np.max(erros)
    erro_rms = np.sqrt(np.mean(np.array(erros)**2))
    
    return {
        'A': A_ajustado,
        'B': B_ajustado,
        'C': C_ajustado,
        'D': D_ajustado,
        'erro_medio': erro_medio,
        'erro_maximo': erro_maximo,
        'erro_rms': erro_rms,
        'erros_por_ponto': erros,
        'UR_calculados': UR_calculados,
        'sucesso': sucesso,
        'mensagem': mensagem,
        'metodo': metodo
    }


def plotar_calibracao_comparativa(pontos_calibracao, resultados_todos_metodos):
    """
    Gera gráfico comparativo mostrando UR real vs calculado e erro por ponto para todos os métodos.
    
    Parâmetros:
    -----------
    pontos_calibracao : list
        Lista de pontos no formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    resultados_todos_metodos : dict
        Dicionário com chaves = nomes dos métodos e valores = resultados da calibração
    """
    pontos = np.array(pontos_calibracao)
    UR_reais = pontos[:, 2]
    
    # Define cores diferentes para cada método
    cores_metodos = {
        'trf': 'blue',
        'lm': 'red',
        'dogbox': 'green',
        'curve_fit': 'orange',
        'differential_evolution': 'purple'
    }
    
    nomes_metodos = {
        'trf': 'TRF',
        'lm': 'LM',
        'dogbox': 'Dogbox',
        'curve_fit': 'Curve Fit',
        'differential_evolution': 'Diff. Evolution'
    }
    
    # Cria figura com subplots
    fig, axes = plt.subplots(1, 2, figsize=(16, 6))
    
    # Gráfico 1: UR Real vs UR Calculado (dispersão)
    ax1 = axes[0]
    
    # Linha de referência (y = x) - linha perfeita de 0 a 100% (UR não pode ser negativa nem acima de 100%)
    ax1.plot([0, 100], [0, 100], 'k--', linewidth=2, 
            label='Linha Perfeita (y=x)', alpha=0.5, zorder=1)
    
    # Plota pontos para cada método
    for metodo, resultado in resultados_todos_metodos.items():
        UR_calculados = np.array(resultado['UR_calculados'])
        cor = cores_metodos.get(metodo, 'gray')
        nome = nomes_metodos.get(metodo, metodo)
        
        ax1.scatter(UR_reais, UR_calculados, s=100, alpha=0.7, 
                   color=cor, edgecolors='black', linewidths=1.5, 
                   label=f'{nome} (Erro RMS: {resultado["erro_rms"]:.3f}%)', zorder=2)
    
    ax1.set_xlabel('Umidade Relativa Real (%)', fontsize=12, fontweight='bold')
    ax1.set_ylabel('Umidade Relativa Calculada (%)', fontsize=12, fontweight='bold')
    ax1.set_title('Comparação: UR Real vs UR Calculado (Todos os Métodos)', 
                  fontsize=13, fontweight='bold')
    ax1.grid(True, alpha=0.3, linestyle='--')
    ax1.legend(loc='best', fontsize=9)
    ax1.set_aspect('equal', adjustable='box')
    # Limita eixos entre 0 e 100% (UR não pode ser negativa nem acima de 100%)
    ax1.set_xlim(0, 100)
    ax1.set_ylim(0, 100)
    
    # Gráfico 2: Erro por ponto para cada método
    ax2 = axes[1]
    
    num_pontos = len(UR_reais)
    indices = np.arange(1, num_pontos + 1)
    largura_barra = 0.15  # Largura das barras
    
    # Plota barras agrupadas para cada método
    metodos_lista = list(resultados_todos_metodos.keys())
    x_posicoes = np.arange(num_pontos)
    
    for i, metodo in enumerate(metodos_lista):
        resultado = resultados_todos_metodos[metodo]
        erros = np.array(resultado['erros_por_ponto'])
        cor = cores_metodos.get(metodo, 'gray')
        nome = nomes_metodos.get(metodo, metodo)
        
        offset = (i - len(metodos_lista)/2 + 0.5) * largura_barra
        ax2.bar(x_posicoes + offset, erros, largura_barra, 
               label=nome, color=cor, alpha=0.7, edgecolor='black', linewidth=0.5)
    
    ax2.set_xlabel('Ponto de Calibração', fontsize=12, fontweight='bold')
    ax2.set_ylabel('Erro Absoluto (%)', fontsize=12, fontweight='bold')
    ax2.set_title('Erro por Ponto de Calibração (Todos os Métodos)', 
                  fontsize=13, fontweight='bold')
    ax2.set_xticks(x_posicoes)
    ax2.set_xticklabels([f'P{i+1}' for i in range(num_pontos)])
    ax2.grid(True, alpha=0.3, linestyle='--', axis='y')
    ax2.legend(loc='best', fontsize=9)
    
    # Adiciona informações no título geral
    # Calcula melhor método (menor erro RMS)
    melhor_metodo = min(resultados_todos_metodos.items(), 
                       key=lambda x: x[1]['erro_rms'])
    melhor_nome = nomes_metodos.get(melhor_metodo[0], melhor_metodo[0])
    
    fig.suptitle(
        f'Comparação de Métodos de Calibração Psicrométrica\n'
        f'Melhor método: {melhor_nome} (Erro RMS: {melhor_metodo[1]["erro_rms"]:.3f}%)',
        fontsize=14, fontweight='bold', y=0.98
    )
    
    plt.tight_layout(rect=[0, 0, 1, 0.96])
    
    # Salva o gráfico
    nome_arquivo = 'calibracao_comparativa_metodos.png'
    plt.savefig(nome_arquivo, dpi=300, bbox_inches='tight')
    print(f"\nGráfico comparativo salvo em: {nome_arquivo}")
    
    # Tenta mostrar o gráfico
    try:
        backend_atual = matplotlib.get_backend()
        if backend_atual.lower() != 'agg':
            plt.show()
        else:
            print("Para visualizar, abra o arquivo 'calibracao_comparativa_metodos.png'")
    except Exception as e:
        print(f"Aviso: Não foi possível exibir o gráfico interativamente: {e}")
        print("Para visualizar, abra o arquivo 'calibracao_comparativa_metodos.png'")
    finally:
        plt.close()


def plotar_calibracao(pontos_calibracao, resultado):
    """
    Gera gráfico comparando os pontos de calibração com os valores calculados.
    
    Parâmetros:
    -----------
    pontos_calibracao : list
        Lista de pontos no formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    resultado : dict
        Dicionário com os resultados da calibração
    """
    pontos = np.array(pontos_calibracao)
    TS_pontos = pontos[:, 0]
    TU_pontos = pontos[:, 1]
    UR_reais = pontos[:, 2]
    UR_calculados = np.array(resultado['UR_calculados'])
    
    # Cria figura com dois subplots
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    
    # Gráfico 1: UR Real vs UR Calculado (dispersão)
    ax1 = axes[0, 0]
    ax1.scatter(UR_reais, UR_calculados, s=100, alpha=0.7, color='blue', edgecolors='black', linewidths=2)
    
    # Linha de referência (y = x) - linha perfeita de 0 a 100% (UR não pode ser negativa nem acima de 100%)
    ax1.plot([0, 100], [0, 100], 'r--', linewidth=2, label='Linha Perfeita (y=x)')
    
    # Anota cada ponto com valores de TS, TU
    for i, (ur_real, ur_calc, ts, tu) in enumerate(zip(UR_reais, UR_calculados, TS_pontos, TU_pontos)):
        ax1.annotate(f'P{i+1}\nTS={ts:.1f}°C\nTU={tu:.1f}°C', 
                    (ur_real, ur_calc), 
                    xytext=(5, 5), textcoords='offset points', 
                    fontsize=8, bbox=dict(boxstyle='round,pad=0.3', facecolor='yellow', alpha=0.5))
    
    ax1.set_xlabel('Umidade Relativa Real (%)', fontsize=11, fontweight='bold')
    ax1.set_ylabel('Umidade Relativa Calculada (%)', fontsize=11, fontweight='bold')
    ax1.set_title('Comparação: UR Real vs UR Calculado', fontsize=12, fontweight='bold')
    ax1.grid(True, alpha=0.3, linestyle='--')
    ax1.legend()
    ax1.set_aspect('equal', adjustable='box')
    # Limita eixos entre 0 e 100% (UR não pode ser negativa nem acima de 100%)
    ax1.set_xlim(0, 100)
    ax1.set_ylim(0, 100)
    
    # Gráfico 2: Erro por ponto
    ax2 = axes[0, 1]
    erros = np.array(resultado['erros_por_ponto'])
    indices = np.arange(1, len(UR_reais) + 1)
    cores = ['green' if e < 1 else 'orange' if e < 3 else 'red' for e in erros]
    ax2.bar(indices, erros, color=cores, alpha=0.7, edgecolor='black', linewidth=1.5)
    
    # Adiciona valores nas barras e informações dos pontos
    for i, (idx, erro, ts, tu, ur_real) in enumerate(zip(indices, erros, TS_pontos, TU_pontos, UR_reais)):
        ax2.text(idx, erro + max(erros)*0.02, f'{erro:.2f}%', 
                ha='center', va='bottom', fontsize=9, fontweight='bold')
        # Adiciona informações do ponto abaixo da barra
        ax2.text(idx, -max(erros)*0.15, f'TS={ts:.1f}\nTU={tu:.1f}\nUR={ur_real:.1f}%', 
                ha='center', va='top', fontsize=7, rotation=0)
    
    ax2.set_xlabel('Ponto de Calibração', fontsize=11, fontweight='bold')
    ax2.set_ylabel('Erro Absoluto (%)', fontsize=11, fontweight='bold')
    ax2.set_title('Erro por Ponto de Calibração', fontsize=12, fontweight='bold')
    ax2.set_xticks(indices)
    ax2.set_xticklabels([f'P{i}' for i in indices])
    ax2.grid(True, alpha=0.3, linestyle='--', axis='y')
    ax2.axhline(y=resultado['erro_medio'], color='blue', linestyle='--', 
               linewidth=2, label=f'Erro Médio: {resultado["erro_medio"]:.2f}%')
    ax2.legend()
    
    # Gráfico 3: UR vs TS (mostrando pontos reais e calculados)
    ax3 = axes[1, 0]
    ax3.scatter(TS_pontos, UR_reais, s=150, marker='o', color='green', 
               label='UR Real', alpha=0.7, edgecolors='black', linewidths=2, zorder=3)
    ax3.scatter(TS_pontos, UR_calculados, s=100, marker='s', color='blue', 
               label='UR Calculado', alpha=0.7, edgecolors='black', linewidths=2, zorder=3)
    
    # Linhas conectando pontos reais e calculados e anotações
    for i, (ts, tu, ur_real, ur_calc) in enumerate(zip(TS_pontos, TU_pontos, UR_reais, UR_calculados)):
        ax3.plot([ts, ts], [ur_real, ur_calc], 'r--', alpha=0.5, linewidth=1, zorder=1)
        # Anota valores de TU próximo aos pontos
        ax3.annotate(f'P{i+1}\nTU={tu:.1f}°C', 
                    (ts, ur_real), 
                    xytext=(5, 5), textcoords='offset points', 
                    fontsize=8, bbox=dict(boxstyle='round,pad=0.3', facecolor='lightblue', alpha=0.7))
    
    ax3.set_xlabel('Temperatura do Bulbo Seco - TS (°C)', fontsize=11, fontweight='bold')
    ax3.set_ylabel('Umidade Relativa (%)', fontsize=11, fontweight='bold')
    ax3.set_title('UR vs TS (Comparação Real vs Calculado)', fontsize=12, fontweight='bold')
    ax3.grid(True, alpha=0.3, linestyle='--')
    ax3.legend()
    # Limita eixo Y entre 0 e 100% (UR não pode ser negativa nem acima de 100%)
    ax3.set_ylim(0, 100)
    
    # Gráfico 4: UR vs TU (mostrando pontos reais e calculados)
    ax4 = axes[1, 1]
    ax4.scatter(TU_pontos, UR_reais, s=150, marker='o', color='green', 
               label='UR Real', alpha=0.7, edgecolors='black', linewidths=2, zorder=3)
    ax4.scatter(TU_pontos, UR_calculados, s=100, marker='s', color='blue', 
               label='UR Calculado', alpha=0.7, edgecolors='black', linewidths=2, zorder=3)
    
    # Linhas conectando pontos reais e calculados e anotações
    for i, (ts, tu, ur_real, ur_calc) in enumerate(zip(TS_pontos, TU_pontos, UR_reais, UR_calculados)):
        ax4.plot([tu, tu], [ur_real, ur_calc], 'r--', alpha=0.5, linewidth=1, zorder=1)
        # Anota valores de TS próximo aos pontos
        ax4.annotate(f'P{i+1}\nTS={ts:.1f}°C', 
                    (tu, ur_real), 
                    xytext=(5, 5), textcoords='offset points', 
                    fontsize=8, bbox=dict(boxstyle='round,pad=0.3', facecolor='lightgreen', alpha=0.7))
    
    ax4.set_xlabel('Temperatura do Bulbo Úmido - TU (°C)', fontsize=11, fontweight='bold')
    ax4.set_ylabel('Umidade Relativa (%)', fontsize=11, fontweight='bold')
    ax4.set_title('UR vs TU (Comparação Real vs Calculado)', fontsize=12, fontweight='bold')
    ax4.grid(True, alpha=0.3, linestyle='--')
    ax4.legend()
    # Limita eixo Y entre 0 e 100% (UR não pode ser negativa nem acima de 100%)
    ax4.set_ylim(0, 100)
    
    # Adiciona informações das constantes no título geral
    metodo_nome = resultado.get('metodo', 'desconhecido')
    titulo_metodo = {
        'trf': 'TRF',
        'lm': 'LM',
        'dogbox': 'Dogbox',
        'curve_fit': 'Curve Fit',
        'differential_evolution': 'Differential Evolution'
    }.get(metodo_nome, metodo_nome.upper())
    
    fig.suptitle(
        f'Calibração Psicrométrica - Constantes Ajustadas (Método: {titulo_metodo})\n'
        f'A={resultado["A"]:.4f}, B={resultado["B"]:.4f}, C={resultado["C"]:.4f}, D={resultado["D"]:.4f}\n'
        f'Erro Médio: {resultado["erro_medio"]:.3f}% | Erro RMS: {resultado["erro_rms"]:.3f}%',
        fontsize=13, fontweight='bold', y=0.995
    )
    
    plt.tight_layout(rect=[0, 0, 1, 0.97])
    
    # Salva o gráfico
    nome_arquivo = 'calibracao_resultado.png'
    plt.savefig(nome_arquivo, dpi=300, bbox_inches='tight')
    print(f"\nGráfico salvo em: {nome_arquivo}")
    
    # Tenta mostrar o gráfico
    try:
        backend_atual = matplotlib.get_backend()
        if backend_atual.lower() != 'agg':
            plt.show()
        else:
            print("Para visualizar, abra o arquivo 'calibracao_resultado.png'")
    except Exception as e:
        print(f"Aviso: Não foi possível exibir o gráfico interativamente: {e}")
        print("Para visualizar, abra o arquivo 'calibracao_resultado.png'")
    finally:
        plt.close()


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
                    
                    # Valida os valores
                    if TS < -50 or TS > 100:
                        print(f"Aviso: Linha {num_linha} - TS fora da faixa recomendada: {TS}")
                    if TU < -50 or TU > 100:
                        print(f"Aviso: Linha {num_linha} - TU fora da faixa recomendada: {TU}")
                    if UR < 0 or UR > 100:
                        print(f"Aviso: Linha {num_linha} - UR fora da faixa (0-100): {UR}")
                    
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


def main():
    """
    Função principal do script de calibração.
    """
    print("="*80)
    print("CALIBRAÇÃO DAS CONSTANTES PSICROMÉTRICAS")
    print("="*80)
    print("\nEste script ajusta as constantes A, B, C, D da fórmula psicrométrica.")
    print("Você precisa fornecer pelo menos 4 pontos conhecidos (TS, TU, UR).\n")
    
    # Pergunta como carregar os pontos
    pontos_calibracao = []
    modo_carregamento = input("Como deseja fornecer os pontos?\n  1 - Digitar no prompt\n  2 - Carregar de arquivo\nEscolha (1 ou 2): ").strip()
    
    if modo_carregamento == '2':
        # Carregar de arquivo
        nome_arquivo = input("\nNome do arquivo com os pontos de calibração: ").strip()
        if not nome_arquivo:
            print("Nome de arquivo vazio. Voltando para modo de digitação...")
            modo_carregamento = '1'
        else:
            try:
                print(f"\nCarregando pontos do arquivo: {nome_arquivo}")
                pontos_calibracao = carregar_pontos_de_arquivo(nome_arquivo)
                print(f"\nTotal de {len(pontos_calibracao)} pontos carregados.")
            except Exception as e:
                print(f"\nErro ao carregar arquivo: {e}")
                resposta = input("Deseja continuar digitando os pontos? (s/n): ").strip().lower()
                if resposta != 's':
                    return
                pontos_calibracao = []
                modo_carregamento = '1'
    
    if modo_carregamento == '1' or len(pontos_calibracao) < 4:
        # Solicita os pontos de calibração via prompt
        if len(pontos_calibracao) > 0:
            print(f"\nVocê já tem {len(pontos_calibracao)} pontos. Adicione mais para completar os 4 mínimos.")
        else:
            print("\nDigite os pontos de calibração:")
        
        print("(Pressione Enter sem valores para finalizar)\n")
        
        # Pede os pontos até ter pelo menos 4 ou até o usuário pressionar Enter vazio
        ponto_num = len(pontos_calibracao) + 1
        
        while True:
            try:
                entrada = input(f"Ponto {ponto_num} (TS TU UR em °C, ou Enter para finalizar): ").strip()
                
                if not entrada:
                    if ponto_num > 4:
                        break
                    elif len(pontos_calibracao) >= 4:
                        break
                    else:
                        print(f"Aviso: São necessários pelo menos 4 pontos! Você forneceu {len(pontos_calibracao)}.")
                        continue
                
                # Divide a entrada em valores
                valores = entrada.split()
                if len(valores) != 3:
                    print("Erro: Digite 3 valores separados por espaço (TS TU UR)")
                    continue
                
                TS = float(valores[0])
                TU = float(valores[1])
                UR = float(valores[2])
                
                # Valida os valores
                if TS < -50 or TS > 100:
                    print("Aviso: TS deve estar entre -50 e 100°C")
                if TU < -50 or TU > 100:
                    print("Aviso: TU deve estar entre -50 e 100°C")
                if UR < 0 or UR > 100:
                    print("Aviso: UR deve estar entre 0 e 100%")
                
                pontos_calibracao.append([TS, TU, UR])
                print(f"  Ponto {ponto_num} adicionado: TS={TS}°C, TU={TU}°C, UR={UR}%\n")
                ponto_num += 1
                
                if len(pontos_calibracao) >= 4:
                    continuar = input("Deseja adicionar mais pontos? (s/n): ").strip().lower()
                    if continuar != 's':
                        break
            
            except ValueError:
                print("Erro: Digite valores numéricos válidos!")
            except KeyboardInterrupt:
                print("\n\nOperação cancelada pelo usuário.")
                return
            except Exception as e:
                print(f"Erro: {e}")
    
    if len(pontos_calibracao) < 4:
        print("\nErro: São necessários pelo menos 4 pontos para calibração!")
        return
    
    print("\n" + "="*80)
    print("Pontos de Calibração Fornecidos:")
    print("="*80)
    print(f"{'#':<5} {'TS (°C)':<12} {'TU (°C)':<12} {'UR (%)':<12}")
    print("-"*80)
    for i, (TS, TU, UR) in enumerate(pontos_calibracao, 1):
        print(f"{i:<5} {TS:<12.2f} {TU:<12.2f} {UR:<12.2f}")
    
    # Pergunta se quer executar todos os métodos ou apenas um
    print("\n" + "="*80)
    print("OPÇÕES DE CALIBRAÇÃO:")
    print("="*80)
    print("1. Executar TODOS os métodos e comparar resultados")
    print("2. Executar apenas UM método específico")
    print()
    
    opcao = input("Escolha a opção (1 ou 2, padrão=1): ").strip() or '1'
    
    executar_todos = (opcao == '1')
    
    if executar_todos:
        print("\nExecutando calibração com TODOS os métodos...")
        metodos_para_executar = ['trf', 'lm', 'dogbox', 'curve_fit', 'differential_evolution']
    else:
        # Pergunta qual método de calibração usar
        print("\n" + "="*80)
        print("MÉTODOS DE CALIBRAÇÃO DISPONÍVEIS:")
        print("="*80)
        print("1. TRF (Trust Region Reflective) - RECOMENDADO")
        print("   - Rápido e robusto")
        print("   - Bom para maioria dos casos")
        print("   - Suporta limites (bounds)")
        print()
        print("2. LM (Levenberg-Marquardt)")
        print("   - Muito rápido")
        print("   - Bom quando os valores iniciais estão próximos da solução")
        print("   - Não suporta limites diretamente")
        print()
        print("3. Dogbox")
        print("   - Robusto para muitos pontos")
        print("   - Bom para problemas com outliers")
        print("   - Suporta limites (bounds)")
        print()
        print("4. Curve Fit")
        print("   - Método clássico baseado em leastsq")
        print("   - Rápido e confiável")
        print("   - Suporta limites (bounds)")
        print()
        print("5. Differential Evolution")
        print("   - Mais robusto, mas mais lento")
        print("   - Bom quando outros métodos falham")
        print("   - Não depende tanto dos valores iniciais")
        print()
        
        metodo_escolhido = input("Escolha o método (1-5, padrão=1): ").strip()
        
        metodos = {
            '1': 'trf',
            '2': 'lm',
            '3': 'dogbox',
            '4': 'curve_fit',
            '5': 'differential_evolution'
        }
        
        metodo_selecionado = metodos.get(metodo_escolhido, 'trf')
        metodos_para_executar = [metodo_selecionado]
        
        nome_metodo = {
            'trf': 'Trust Region Reflective (TRF)',
            'lm': 'Levenberg-Marquardt (LM)',
            'dogbox': 'Dogbox',
            'curve_fit': 'Curve Fit',
            'differential_evolution': 'Differential Evolution'
        }[metodo_selecionado]
        
        print(f"\nMétodo selecionado: {nome_metodo}")
    
    # Solicita valores iniciais (opcional)
    print("\n" + "="*80)
    usar_padrao = input("Usar valores iniciais padrão? (s/n): ").strip().lower()
    constantes_iniciais = None
    
    if usar_padrao != 's':
        try:
            print("Digite os valores iniciais das constantes:")
            A_inicial = float(input("A (padrão 6.112): ") or "6.112")
            B_inicial = float(input("B (padrão 17.67): ") or "17.67")
            C_inicial = float(input("C (padrão 242.5): ") or "242.5")
            D_inicial = float(input("D (padrão 1.8): ") or "1.8")
            constantes_iniciais = [A_inicial, B_inicial, C_inicial, D_inicial]
        except ValueError:
            print("Valores inválidos, usando padrão...")
            constantes_iniciais = None
    
    # Realiza a calibração
    print("\n" + "="*80)
    
    nomes_metodos = {
        'trf': 'Trust Region Reflective (TRF)',
        'lm': 'Levenberg-Marquardt (LM)',
        'dogbox': 'Dogbox',
        'curve_fit': 'Curve Fit',
        'differential_evolution': 'Differential Evolution'
    }
    
    resultados_todos_metodos = {}
    
    try:
        # Executa calibração para cada método
        for metodo in metodos_para_executar:
            nome_metodo = nomes_metodos.get(metodo, metodo)
            print(f"Realizando calibração usando método: {nome_metodo}...")
            
            try:
                resultado = calibrar_constantes(pontos_calibracao, constantes_iniciais, metodo=metodo)
                resultados_todos_metodos[metodo] = resultado
                print(f"  ✓ {nome_metodo} concluído (Erro RMS: {resultado['erro_rms']:.4f}%)")
            except Exception as e:
                print(f"  ✗ Erro em {nome_metodo}: {e}")
        
        if len(resultados_todos_metodos) == 0:
            print("\nErro: Nenhum método conseguiu calibrar!")
            return
        
        # Exibe os resultados para cada método
        print("\n" + "="*80)
        print("RESULTADOS DA CALIBRAÇÃO - TODOS OS MÉTODOS")
        print("="*80)
        
        for metodo, resultado in resultados_todos_metodos.items():
            nome_metodo = nomes_metodos.get(metodo, metodo)
            print(f"\n{'='*80}")
            print(f"MÉTODO: {nome_metodo.upper()}")
            print(f"{'='*80}")
            print("\nConstantes Ajustadas:")
            print(f"  A = {resultado['A']:.6f}")
            print(f"  B = {resultado['B']:.6f}")
            print(f"  C = {resultado['C']:.6f}")
            print(f"  D = {resultado['D']:.6f}")
            
            print("\nEstatísticas do Ajuste:")
            print(f"  Erro médio absoluto: {resultado['erro_medio']:.4f}%")
            print(f"  Erro máximo absoluto: {resultado['erro_maximo']:.4f}%")
            print(f"  Erro RMS: {resultado['erro_rms']:.4f}%")
            print(f"  Convergência: {'Sucesso' if resultado['sucesso'] else 'Falhou'}")
            if resultado['mensagem']:
                print(f"  Mensagem: {resultado['mensagem']}")
            
            print("\nComparação dos Pontos:")
            print(f"{'#':<5} {'TS (°C)':<12} {'TU (°C)':<15} {'UR Real (%)':<15} {'UR Calc (%)':<15} {'Erro Abs (%)':<15}")
            print("-"*90)
            for i, ((TS, TU, UR_real), UR_calc, erro) in enumerate(
                zip(pontos_calibracao, resultado['UR_calculados'], resultado['erros_por_ponto']), 1
            ):
                print(f"{i:<5} {TS:<12.2f} {TU:<15.2f} {UR_real:<15.2f} {UR_calc:<15.2f} {erro:<15.4f}")
        
        # Se executou todos os métodos, gera gráfico comparativo
        if executar_todos:
            print("\n" + "="*80)
            print("Gerando gráfico comparativo de todos os métodos...")
            print("="*80)
            try:
                plotar_calibracao_comparativa(pontos_calibracao, resultados_todos_metodos)
            except Exception as e:
                print(f"Aviso: Erro ao gerar gráfico comparativo: {e}")
                import traceback
                traceback.print_exc()
        else:
            # Se executou apenas um método, gera gráfico individual
            resultado = list(resultados_todos_metodos.values())[0]
            print("\n" + "="*80)
            print("Gerando gráfico de calibração...")
            print("="*80)
            try:
                plotar_calibracao(pontos_calibracao, resultado)
            except Exception as e:
                print(f"Aviso: Erro ao gerar gráfico: {e}")
                import traceback
                traceback.print_exc()
        
        # Salva constantes em arquivos separados para cada método
        if executar_todos:
            print("\n" + "="*80)
            salvar_todos = input("Deseja salvar as constantes de TODOS os métodos em arquivos separados? (s/n): ").strip().lower()
            if salvar_todos == 's':
                prefixo = input("Prefixo para os nomes dos arquivos (padrão: constantes_): ").strip()
                if not prefixo:
                    prefixo = "constantes_"
                
                for metodo, resultado in resultados_todos_metodos.items():
                    nome_metodo = nomes_metodos.get(metodo, metodo)
                    # Cria nome de arquivo baseado no método
                    nome_arquivo = f"{prefixo}{metodo}.txt"
                    
                    with open(nome_arquivo, 'w', encoding='utf-8') as f:
                        f.write("# Constantes da fórmula psicrométrica (ajustadas por calibração)\n")
                        f.write(f"# Método de calibração: {nome_metodo}\n")
                        f.write(f"# Pontos de calibração: {len(pontos_calibracao)}\n")
                        f.write(f"# Erro médio absoluto: {resultado['erro_medio']:.4f}%\n")
                        f.write(f"# Erro máximo absoluto: {resultado['erro_maximo']:.4f}%\n")
                        f.write(f"# Erro RMS: {resultado['erro_rms']:.4f}%\n\n")
                        
                        f.write("# Constantes ajustadas:\n")
                        f.write("A = {:.6f}\n".format(resultado['A']))
                        f.write("B = {:.6f}\n".format(resultado['B']))
                        f.write("C = {:.6f}\n".format(resultado['C']))
                        f.write("D = {:.6f}\n\n".format(resultado['D']))
                        
                        # Escreve os pontos de calibração e erros
                        f.write("# Pontos de calibração utilizados:\n")
                        f.write("# TS = Temperatura do bulbo seco (°C)\n")
                        f.write("# TU = Temperatura do bulbo úmido (°C)\n")
                        f.write("# UR_Real = Umidade relativa real medida (%)\n")
                        f.write("# UR_Calculado = Umidade relativa calculada (%)\n")
                        f.write("# Erro_Absoluto = |UR_Real - UR_Calculado| (%)\n")
                        f.write(f"{'#':<5} {'TS (°C)':<15} {'TU (°C)':<15} {'UR_Real (%)':<18} {'UR_Calculado (%)':<20} {'Erro_Absoluto (%)':<20}\n")
                        
                        for i, ((TS, TU, UR_real), UR_calc, erro) in enumerate(
                            zip(pontos_calibracao, resultado['UR_calculados'], resultado['erros_por_ponto']), 1
                        ):
                            f.write(f"{i:<5} {TS:<15.6f} {TU:<15.6f} {UR_real:<18.6f} {UR_calc:<20.6f} {erro:<20.6f}\n")
                    
                    print(f"  Constantes salvas em: {nome_arquivo}")
        else:
            # Se executou apenas um método, pergunta se quer salvar
            resultado = list(resultados_todos_metodos.values())[0]
            metodo = list(resultados_todos_metodos.keys())[0]
            nome_metodo = nomes_metodos.get(metodo, metodo)
            
            # Gera código Python com as constantes
            print("\n" + "="*80)
            print("Código Python para usar as constantes:")
            print("="*80)
            print("# Constantes da fórmula psicrométrica (ajustadas)")
            print(f"A = {resultado['A']:.6f}")
            print(f"B = {resultado['B']:.6f}")
            print(f"C = {resultado['C']:.6f}")
            print(f"D = {resultado['D']:.6f}")
            
            # Salva em arquivo
            salvar = input("\nDeseja salvar as constantes em um arquivo? (s/n): ").strip().lower()
            if salvar == 's':
                nome_arquivo = input("Nome do arquivo (padrão: constantes_calibradas.txt): ").strip()
                if not nome_arquivo:
                    nome_arquivo = "constantes_calibradas.txt"
                
                with open(nome_arquivo, 'w', encoding='utf-8') as f:
                    titulo_metodo = nome_metodo
                    
                    f.write("# Constantes da fórmula psicrométrica (ajustadas por calibração)\n")
                    f.write(f"# Método de calibração: {titulo_metodo}\n")
                    f.write(f"# Pontos de calibração: {len(pontos_calibracao)}\n")
                    f.write(f"# Erro médio absoluto: {resultado['erro_medio']:.4f}%\n")
                    f.write(f"# Erro máximo absoluto: {resultado['erro_maximo']:.4f}%\n")
                    f.write(f"# Erro RMS: {resultado['erro_rms']:.4f}%\n\n")
                    
                    f.write("# Constantes ajustadas:\n")
                    f.write("A = {:.6f}\n".format(resultado['A']))
                    f.write("B = {:.6f}\n".format(resultado['B']))
                    f.write("C = {:.6f}\n".format(resultado['C']))
                    f.write("D = {:.6f}\n\n".format(resultado['D']))
                    
                    # Escreve os pontos de calibração e erros
                    f.write("# Pontos de calibração utilizados:\n")
                    f.write("# TS = Temperatura do bulbo seco (°C)\n")
                    f.write("# TU = Temperatura do bulbo úmido (°C)\n")
                    f.write("# UR_Real = Umidade relativa real medida (%)\n")
                    f.write("# UR_Calculado = Umidade relativa calculada (%)\n")
                    f.write("# Erro_Absoluto = |UR_Real - UR_Calculado| (%)\n")
                    f.write(f"{'#':<5} {'TS (°C)':<15} {'TU (°C)':<15} {'UR_Real (%)':<18} {'UR_Calculado (%)':<20} {'Erro_Absoluto (%)':<20}\n")
                    
                    for i, ((TS, TU, UR_real), UR_calc, erro) in enumerate(
                        zip(pontos_calibracao, resultado['UR_calculados'], resultado['erros_por_ponto']), 1
                    ):
                        f.write(f"{i:<5} {TS:<15.6f} {TU:<15.6f} {UR_real:<18.6f} {UR_calc:<20.6f} {erro:<20.6f}\n")
                
                print(f"\nConstantes salvas em: {nome_arquivo}")
        
        # Mostra resumo comparativo se executou todos
        if executar_todos and len(resultados_todos_metodos) > 1:
            print("\n" + "="*80)
            print("RESUMO COMPARATIVO - MELHOR MÉTODO")
            print("="*80)
            melhor_metodo = min(resultados_todos_metodos.items(), 
                               key=lambda x: x[1]['erro_rms'])
            melhor_nome = nomes_metodos.get(melhor_metodo[0], melhor_metodo[0])
            print(f"\nMelhor método: {melhor_nome}")
            print(f"  Erro RMS: {melhor_metodo[1]['erro_rms']:.4f}%")
            print(f"  Erro médio absoluto: {melhor_metodo[1]['erro_medio']:.4f}%")
            print(f"  Erro máximo absoluto: {melhor_metodo[1]['erro_maximo']:.4f}%")
        
    except Exception as e:
        print(f"\nErro durante a calibração: {e}")
        import traceback
        traceback.print_exc()


if __name__ == "__main__":
    main()
