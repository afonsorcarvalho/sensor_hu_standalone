"""
Script de Calibração pelo Método Psicrométrico Modificado
Este script ajusta as constantes A, B, C, D da fórmula psicrométrica modificada usando pontos conhecidos.

Fórmula utilizada (modelo psicrométrico modificado):
UR = (1/p_sat(T_ref)) * [A * exp(B * ΔT) + C * ΔT + D] * 100

Onde:
- TS = Temperatura do bulbo seco (temperatura seca) em °C
- TU = Temperatura do bulbo úmido (temperatura úmida) em °C
- ΔT = TS - TU (diferença de temperatura)
- UR = Umidade relativa em porcentagem (0-100)
- p_sat(T_ref) = Pressão de saturação do vapor d'água à temperatura de referência (55°C por padrão)
- A, B, C, D = Constantes a serem calibradas

Referência: METODO_EQUIVALENCIA_UR.md (seção 5 - Função recomendada semi-física)

Este método usa uma forma funcional semi-física que combina exponencial e linear.
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

# Temperatura de referência padrão (55°C conforme documento)
T_REFERENCIA = 55.0


def calcular_pressao_saturacao_agua(T):
    """
    Calcula a pressão de saturação do vapor d'água usando a equação de Magnus (formula simplificada).
    
    Parâmetros:
    -----------
    T : float ou array
        Temperatura em graus Celsius
    
    Retorna:
    --------
    float ou array
        Pressão de saturação em kPa
    
    Referência: Formula de Magnus para água (0-100°C):
    p_sat(T) = 0.61121 * exp((18.678 - T/234.5) * T / (T + 257.14))
    """
    # Constantes da equação de Magnus para água
    A = 18.678
    B = 234.5
    C = 257.14
    P0 = 0.61121  # kPa (pressão de referência a 0°C)
    
    # Equação de Magnus
    p_sat = P0 * np.exp(((A - T/B) * T) / (T + C))
    
    return p_sat


def calcular_umidade_relativa_psicrometrico_modificado(TS, TU, A, B, C, D, T_ref=T_REFERENCIA):
    """
    Calcula a umidade relativa usando o modelo psicrométrico modificado.
    
    Fórmula: UR = (1/p_sat(T_ref)) * [A * exp(B * ΔT) + C * ΔT + D] * 100
    
    Parâmetros:
    -----------
    TS : float ou array
        Temperatura do bulbo seco (temperatura seca) em graus Celsius
    TU : float ou array
        Temperatura do bulbo úmido (temperatura úmida) em graus Celsius
    A, B, C, D : float
        Constantes do modelo psicrométrico modificado
    T_ref : float
        Temperatura de referência para p_sat (padrão: 55°C)
    
    Retorna:
    --------
    float ou array
        Umidade relativa em porcentagem (0-100)
    """
    # Calcula diferença de temperatura
    delta_T = TS - TU
    
    # Calcula pressão de saturação na temperatura de referência
    p_sat_ref = calcular_pressao_saturacao_agua(T_ref)
    
    # Evita divisão por zero
    if p_sat_ref < 1e-10:
        p_sat_ref = 1e-10
    
    # Modelo psicrométrico modificado
    # UR = (1/p_sat(T_ref)) * [A * exp(B * ΔT) + C * ΔT + D] * 100
    termo_exponencial = A * np.exp(B * delta_T)
    termo_linear = C * delta_T
    termo_constante = D
    
    UR = ((termo_exponencial + termo_linear + termo_constante) / p_sat_ref) * 100
    
    # Limita UR entre 0 e 100%
    UR = np.clip(UR, 0, 100)
    
    return UR


def funcao_residuo(constantes, pontos_calibracao, T_ref=T_REFERENCIA):
    """
    Calcula o resíduo (erro) entre os valores de UR calculados e os valores reais.
    
    Parâmetros:
    -----------
    constantes : array
        Array com [A, B, C, D]
    pontos_calibracao : array
        Array com formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    T_ref : float
        Temperatura de referência para p_sat
    
    Retorna:
    --------
    array
        Array com os resíduos (diferença entre UR calculado e UR real)
    """
    A, B, C, D = constantes
    
    residuos = []
    for TS, TU, UR_real in pontos_calibracao:
        # Calcula UR usando as constantes
        UR_calculado = calcular_umidade_relativa_psicrometrico_modificado(TS, TU, A, B, C, D, T_ref)
        
        # Calcula o resíduo (diferença)
        residuo = UR_real - UR_calculado
        residuos.append(residuo)
    
    return np.array(residuos)


def funcao_para_curve_fit(pontos_TS_TU, A, B, C, D):
    """
    Função auxiliar para usar com curve_fit.
    
    Parâmetros:
    -----------
    pontos_TS_TU : array
        Array com formato [[TS1, TU1], [TS2, TU2], ...]
    A, B, C, D : float
        Constantes do modelo
    
    Retorna:
    --------
    array
        Array com UR calculado para cada ponto
    """
    UR_calculado = []
    for TS, TU in pontos_TS_TU:
        UR = calcular_umidade_relativa_psicrometrico_modificado(TS, TU, A, B, C, D)
        UR_calculado.append(UR)
    
    return np.array(UR_calculado)


def carregar_pontos_de_arquivo(nome_arquivo):
    """
    Carrega pontos de calibração de um arquivo de texto.
    
    Formato esperado: TS TU UR_real (valores separados por espaço ou vírgula)
    Linhas que começam com # são ignoradas (comentários)
    
    Parâmetros:
    -----------
    nome_arquivo : str
        Caminho do arquivo
    
    Retorna:
    --------
    array
        Array com formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    """
    pontos = []
    
    try:
        with open(nome_arquivo, 'r', encoding='utf-8') as f:
            for linha_num, linha in enumerate(f, 1):
                linha = linha.strip()
                
                # Ignora linhas vazias e comentários
                if not linha or linha.startswith('#'):
                    continue
                
                # Tenta separar por espaço ou vírgula
                valores = linha.replace(',', ' ').split()
                
                if len(valores) >= 3:
                    try:
                        TS = float(valores[0])
                        TU = float(valores[1])
                        UR = float(valores[2])
                        pontos.append([TS, TU, UR])
                    except ValueError:
                        print(f"Aviso: Linha {linha_num} ignorada (valores inválidos): {linha}")
                else:
                    print(f"Aviso: Linha {linha_num} ignorada (formato inválido): {linha}")
    
    except FileNotFoundError:
        print(f"Erro: Arquivo '{nome_arquivo}' não encontrado.")
        return None
    except Exception as e:
        print(f"Erro ao ler arquivo '{nome_arquivo}': {e}")
        return None
    
    if len(pontos) == 0:
        print(f"Erro: Nenhum ponto válido encontrado no arquivo '{nome_arquivo}'.")
        return None
    
    return np.array(pontos)


def salvar_constantes(nome_arquivo, A, B, C, D, metodo='curve_fit', T_ref=T_REFERENCIA):
    """
    Salva as constantes A, B, C, D calibradas em um arquivo de texto.
    
    Parâmetros:
    -----------
    nome_arquivo : str
        Caminho do arquivo para salvar
    A, B, C, D : float
        Constantes calibradas
    metodo : str
        Nome do método usado para calibração
    T_ref : float
        Temperatura de referência usada
    """
    try:
        with open(nome_arquivo, 'w', encoding='utf-8') as f:
            f.write("# Constantes calibradas pelo Método Psicrométrico Modificado\n")
            f.write(f"# Método utilizado: {metodo}\n")
            f.write(f"# Fórmula: UR = (1/p_sat({T_ref}°C)) * [A * exp(B * ΔT) + C * ΔT + D] * 100\n")
            f.write(f"# onde ΔT = TS - TU e p_sat(T) é calculado pela equação de Magnus\n")
            f.write(f"\n")
            f.write(f"A = {A:.10f}\n")
            f.write(f"B = {B:.10f}\n")
            f.write(f"C = {C:.10f}\n")
            f.write(f"D = {D:.10f}\n")
            f.write(f"T_ref = {T_ref:.2f}\n")
    except Exception as e:
        print(f"Erro ao salvar arquivo '{nome_arquivo}': {e}")


def carregar_constantes_de_arquivo(nome_arquivo):
    """
    Carrega as constantes A, B, C, D de um arquivo de texto.
    
    Parâmetros:
    -----------
    nome_arquivo : str
        Caminho do arquivo
    
    Retorna:
    --------
    tuple
        (A, B, C, D, T_ref) ou None se houver erro
    """
    try:
        A = B = C = D = T_ref = None
        with open(nome_arquivo, 'r', encoding='utf-8') as f:
            for linha in f:
                linha = linha.strip()
                if linha.startswith('A =') or linha.startswith('A='):
                    try:
                        A = float(linha.split('=')[1].strip())
                    except (ValueError, IndexError):
                        continue
                elif linha.startswith('B =') or linha.startswith('B='):
                    try:
                        B = float(linha.split('=')[1].strip())
                    except (ValueError, IndexError):
                        continue
                elif linha.startswith('C =') or linha.startswith('C='):
                    try:
                        C = float(linha.split('=')[1].strip())
                    except (ValueError, IndexError):
                        continue
                elif linha.startswith('D =') or linha.startswith('D='):
                    try:
                        D = float(linha.split('=')[1].strip())
                    except (ValueError, IndexError):
                        continue
                elif linha.startswith('T_ref =') or linha.startswith('T_ref='):
                    try:
                        T_ref = float(linha.split('=')[1].strip())
                    except (ValueError, IndexError):
                        continue
        
        if A is None or B is None or C is None or D is None:
            print(f"Erro: Constantes incompletas no arquivo '{nome_arquivo}'.")
            return None
        
        if T_ref is None:
            T_ref = T_REFERENCIA
        
        return (A, B, C, D, T_ref)
    
    except FileNotFoundError:
        print(f"Erro: Arquivo '{nome_arquivo}' não encontrado.")
        return None
    except Exception as e:
        print(f"Erro ao ler arquivo '{nome_arquivo}': {e}")
        return None


def calibrar_usando_curve_fit(pontos_calibracao, T_ref=T_REFERENCIA):
    """
    Calibra A, B, C, D usando curve_fit do scipy.
    
    Parâmetros:
    -----------
    pontos_calibracao : array
        Array com formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    T_ref : float
        Temperatura de referência
    
    Retorna:
    --------
    tuple
        ((A, B, C, D), parametros_covariancia) ou (None, None) se falhar
    """
    TS_TU = pontos_calibracao[:, :2]
    UR_real = pontos_calibracao[:, 2]
    
    # Valores iniciais para A, B, C, D
    # Estimativas baseadas em valores típicos
    p_sat_ref = calcular_pressao_saturacao_agua(T_ref)
    A_inicial = p_sat_ref * 0.1  # Estimativa inicial
    B_inicial = 0.1  # Coeficiente do exponencial
    C_inicial = -p_sat_ref * 0.01  # Coeficiente linear
    D_inicial = p_sat_ref * 0.5  # Termo constante
    
    constantes_iniciais = [A_inicial, B_inicial, C_inicial, D_inicial]
    
    # Limites razoáveis para as constantes
    limites_inferiores = [-p_sat_ref * 10, -10.0, -p_sat_ref * 10, -p_sat_ref * 10]
    limites_superiores = [p_sat_ref * 10, 10.0, p_sat_ref * 10, p_sat_ref * 10]
    
    try:
        popt, pcov = curve_fit(
            funcao_para_curve_fit,
            TS_TU,
            UR_real,
            p0=constantes_iniciais,
            bounds=(limites_inferiores, limites_superiores),
            method='trf',  # Trust Region Reflective
            maxfev=10000
        )
        
        A, B, C, D = popt
        return (A, B, C, D), pcov
    
    except Exception as e:
        print(f"Erro ao calibrar com curve_fit: {e}")
        return None, None


def calibrar_usando_least_squares(pontos_calibracao, T_ref=T_REFERENCIA):
    """
    Calibra A, B, C, D usando least_squares do scipy.
    
    Parâmetros:
    -----------
    pontos_calibracao : array
        Array com formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    T_ref : float
        Temperatura de referência
    
    Retorna:
    --------
    tuple
        (A, B, C, D) ou None se falhar
    """
    p_sat_ref = calcular_pressao_saturacao_agua(T_ref)
    
    # Valores iniciais
    A_inicial = p_sat_ref * 0.1
    B_inicial = 0.1
    C_inicial = -p_sat_ref * 0.01
    D_inicial = p_sat_ref * 0.5
    
    constantes_iniciais = [A_inicial, B_inicial, C_inicial, D_inicial]
    
    # Limites
    limites_inferiores = [-p_sat_ref * 10, -10.0, -p_sat_ref * 10, -p_sat_ref * 10]
    limites_superiores = [p_sat_ref * 10, 10.0, p_sat_ref * 10, p_sat_ref * 10]
    
    try:
        resultado = least_squares(
            funcao_residuo,
            constantes_iniciais,
            args=(pontos_calibracao, T_ref),
            bounds=(limites_inferiores, limites_superiores),
            method='trf'  # Trust Region Reflective
        )
        
        if resultado.success:
            A, B, C, D = resultado.x
            return (A, B, C, D)
        else:
            print(f"Erro: Otimização não convergiu. Mensagem: {resultado.message}")
            return None
    
    except Exception as e:
        print(f"Erro ao calibrar com least_squares: {e}")
        return None


def calibrar_usando_differential_evolution(pontos_calibracao, T_ref=T_REFERENCIA):
    """
    Calibra A, B, C, D usando differential_evolution do scipy.
    
    Parâmetros:
    -----------
    pontos_calibracao : array
        Array com formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    T_ref : float
        Temperatura de referência
    
    Retorna:
    --------
    tuple
        (A, B, C, D) ou None se falhar
    """
    p_sat_ref = calcular_pressao_saturacao_agua(T_ref)
    
    def funcao_objetivo(constantes_array):
        """Função objetivo: soma dos quadrados dos resíduos"""
        A, B, C, D = constantes_array
        residuos = funcao_residuo([A, B, C, D], pontos_calibracao, T_ref)
        return np.sum(residuos**2)
    
    # Limites para cada constante
    limites = [
        (-p_sat_ref * 10, p_sat_ref * 10),  # A
        (-10.0, 10.0),  # B
        (-p_sat_ref * 10, p_sat_ref * 10),  # C
        (-p_sat_ref * 10, p_sat_ref * 10)   # D
    ]
    
    try:
        resultado = differential_evolution(
            funcao_objetivo,
            limites,
            seed=42,
            maxiter=1000,
            popsize=15,
            atol=1e-8,
            tol=1e-8
        )
        
        if resultado.success:
            A, B, C, D = resultado.x
            return (A, B, C, D)
        else:
            print(f"Erro: Otimização não convergiu. Mensagem: {resultado.message}")
            return None
    
    except Exception as e:
        print(f"Erro ao calibrar com differential_evolution: {e}")
        return None


def calcular_metricas(pontos_calibracao, A, B, C, D, T_ref=T_REFERENCIA):
    """
    Calcula métricas de qualidade da calibração.
    
    Parâmetros:
    -----------
    pontos_calibracao : array
        Array com formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    A, B, C, D : float
        Constantes calibradas
    T_ref : float
        Temperatura de referência
    
    Retorna:
    --------
    dict
        Dicionário com métricas (RMSE, MAE, R², etc.)
    """
    UR_real = pontos_calibracao[:, 2]
    UR_calculado = []
    
    for TS, TU, _ in pontos_calibracao:
        UR_calc = calcular_umidade_relativa_psicrometrico_modificado(TS, TU, A, B, C, D, T_ref)
        UR_calculado.append(UR_calc)
    
    UR_calculado = np.array(UR_calculado)
    
    # Calcula métricas
    residuos = UR_real - UR_calculado
    RMSE = np.sqrt(np.mean(residuos**2))
    MAE = np.mean(np.abs(residuos))
    
    # R² (coeficiente de determinação)
    SSR = np.sum((UR_real - UR_calculado)**2)  # Soma dos quadrados dos resíduos
    SST = np.sum((UR_real - np.mean(UR_real))**2)  # Soma total dos quadrados
    R2 = 1 - (SSR / SST) if SST > 0 else 0
    
    # Erro máximo
    erro_max = np.max(np.abs(residuos))
    
    # Erro médio
    erro_medio = np.mean(residuos)
    
    return {
        'RMSE': RMSE,
        'MAE': MAE,
        'R2': R2,
        'erro_max': erro_max,
        'erro_medio': erro_medio,
        'residuos': residuos,
        'UR_real': UR_real,
        'UR_calculado': UR_calculado
    }


def plotar_comparacao(pontos_calibracao, A, B, C, D, nome_arquivo_saida='calibracao_psicrometrico_modificado.png', T_ref=T_REFERENCIA):
    """
    Plota gráficos de comparação entre valores reais e calculados.
    
    Parâmetros:
    -----------
    pontos_calibracao : array
        Array com formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    A, B, C, D : float
        Constantes calibradas
    nome_arquivo_saida : str
        Nome do arquivo para salvar o gráfico
    T_ref : float
        Temperatura de referência
    """
    metricas = calcular_metricas(pontos_calibracao, A, B, C, D, T_ref)
    
    UR_real = metricas['UR_real']
    UR_calculado = metricas['UR_calculado']
    residuos = metricas['residuos']
    
    # Cria figura com subplots
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle(f'Calibração pelo Método Psicrométrico Modificado\nA={A:.4f}, B={B:.4f}, C={C:.4f}, D={D:.4f}, T_ref={T_ref:.1f}°C', 
                 fontsize=14, fontweight='bold')
    
    # Gráfico 1: UR Real vs UR Calculado
    ax1 = axes[0, 0]
    min_ur = min(min(UR_real), min(UR_calculado))
    max_ur = max(max(UR_real), max(UR_calculado))
    ax1.plot([min_ur, max_ur], [min_ur, max_ur], 'r--', label='Linha ideal', linewidth=2)
    ax1.scatter(UR_real, UR_calculado, alpha=0.6, s=50)
    ax1.set_xlabel('UR Real (%)', fontsize=11)
    ax1.set_ylabel('UR Calculado (%)', fontsize=11)
    ax1.set_title(f'UR Real vs UR Calculado\nR² = {metricas["R2"]:.4f}', fontsize=11)
    ax1.grid(True, alpha=0.3)
    ax1.legend()
    
    # Gráfico 2: Resíduos
    ax2 = axes[0, 1]
    ax2.scatter(UR_real, residuos, alpha=0.6, s=50, color='green')
    ax2.axhline(y=0, color='r', linestyle='--', linewidth=2)
    ax2.set_xlabel('UR Real (%)', fontsize=11)
    ax2.set_ylabel('Resíduo (UR Real - UR Calculado)', fontsize=11)
    ax2.set_title(f'Resíduos\nRMSE = {metricas["RMSE"]:.4f} %UR', fontsize=11)
    ax2.grid(True, alpha=0.3)
    
    # Gráfico 3: Histograma dos resíduos
    ax3 = axes[1, 0]
    ax3.hist(residuos, bins=15, alpha=0.7, color='orange', edgecolor='black')
    ax3.axvline(x=0, color='r', linestyle='--', linewidth=2)
    ax3.set_xlabel('Resíduo (%UR)', fontsize=11)
    ax3.set_ylabel('Frequência', fontsize=11)
    ax3.set_title(f'Distribuição dos Resíduos\nMAE = {metricas["MAE"]:.4f} %UR', fontsize=11)
    ax3.grid(True, alpha=0.3, axis='y')
    
    # Gráfico 4: Valores ao longo do tempo/índice
    ax4 = axes[1, 1]
    indices = np.arange(len(UR_real))
    ax4.plot(indices, UR_real, 'o-', label='UR Real', linewidth=2, markersize=6)
    ax4.plot(indices, UR_calculado, 's-', label='UR Calculado', linewidth=2, markersize=6, alpha=0.7)
    ax4.set_xlabel('Índice do Ponto', fontsize=11)
    ax4.set_ylabel('UR (%)', fontsize=11)
    ax4.set_title('Comparação ao Longo dos Pontos', fontsize=11)
    ax4.grid(True, alpha=0.3)
    ax4.legend()
    
    plt.tight_layout()
    plt.savefig(nome_arquivo_saida, dpi=300, bbox_inches='tight')
    print(f"Gráfico salvo em: {nome_arquivo_saida}")
    plt.close()


def main():
    """
    Função principal do script.
    """
    import sys
    import os
    
    # Tenta obter parâmetros da linha de comando ou usa padrões
    if len(sys.argv) >= 2:
        arquivo_pontos = sys.argv[1]
    else:
        arquivo_pontos = 'pontos_calibracao.txt'
        if not os.path.exists(arquivo_pontos):
            try:
                arquivo_pontos = input("Digite o caminho do arquivo de pontos de calibração (ou Enter para 'pontos_calibracao.txt'): ").strip()
                if not arquivo_pontos:
                    arquivo_pontos = 'pontos_calibracao.txt'
            except (EOFError, KeyboardInterrupt):
                arquivo_pontos = 'pontos_calibracao.txt'
    
    if len(sys.argv) >= 3:
        arquivo_saida = sys.argv[2]
    else:
        arquivo_saida = 'constantes_psicrometrico_modificado.txt'
        if not os.path.exists(arquivo_saida):
            try:
                arquivo_saida = input("Digite o nome do arquivo de saída (ou Enter para 'constantes_psicrometrico_modificado.txt'): ").strip()
                if not arquivo_saida:
                    arquivo_saida = 'constantes_psicrometrico_modificado.txt'
            except (EOFError, KeyboardInterrupt):
                arquivo_saida = 'constantes_psicrometrico_modificado.txt'
    
    # Carrega pontos de calibração
    print(f"Carregando pontos de calibração de '{arquivo_pontos}'...")
    pontos_calibracao = carregar_pontos_de_arquivo(arquivo_pontos)
    
    if pontos_calibracao is None:
        print("Erro: Não foi possível carregar pontos de calibração.")
        return
    
    print(f"Pontos de calibração carregados: {len(pontos_calibracao)}")
    print(f"Faixa de TS: {pontos_calibracao[:, 0].min():.2f} a {pontos_calibracao[:, 0].max():.2f} °C")
    print(f"Faixa de TU: {pontos_calibracao[:, 1].min():.2f} a {pontos_calibracao[:, 1].max():.2f} °C")
    print(f"Faixa de UR: {pontos_calibracao[:, 2].min():.2f} a {pontos_calibracao[:, 2].max():.2f} %UR")
    print(f"Temperatura de referência: {T_REFERENCIA}°C (p_sat = {calcular_pressao_saturacao_agua(T_REFERENCIA):.4f} kPa)\n")
    
    # Testa diferentes métodos de calibração
    print("=" * 70)
    print("CALIBRAÇÃO PELO MÉTODO PSICROMÉTRICO MODIFICADO")
    print("=" * 70)
    print("\nTestando diferentes métodos de otimização...\n")
    
    resultados = {}
    
    # Método 1: curve_fit
    print("1. Calibrando com curve_fit...")
    resultado_cf = calibrar_usando_curve_fit(pontos_calibracao)
    if resultado_cf[0] is not None:
        A_cf, B_cf, C_cf, D_cf = resultado_cf[0]
        metricas_cf = calcular_metricas(pontos_calibracao, A_cf, B_cf, C_cf, D_cf)
        resultados['curve_fit'] = {'constantes': (A_cf, B_cf, C_cf, D_cf), 'metricas': metricas_cf}
        print(f"   A = {A_cf:.10f}")
        print(f"   B = {B_cf:.10f}")
        print(f"   C = {C_cf:.10f}")
        print(f"   D = {D_cf:.10f}")
        print(f"   RMSE = {metricas_cf['RMSE']:.4f} %UR")
        print(f"   R² = {metricas_cf['R2']:.4f}\n")
    else:
        print("   Falhou!\n")
    
    # Método 2: least_squares
    print("2. Calibrando com least_squares (TRF)...")
    resultado_ls = calibrar_usando_least_squares(pontos_calibracao)
    if resultado_ls is not None:
        A_ls, B_ls, C_ls, D_ls = resultado_ls
        metricas_ls = calcular_metricas(pontos_calibracao, A_ls, B_ls, C_ls, D_ls)
        resultados['least_squares'] = {'constantes': (A_ls, B_ls, C_ls, D_ls), 'metricas': metricas_ls}
        print(f"   A = {A_ls:.10f}")
        print(f"   B = {B_ls:.10f}")
        print(f"   C = {C_ls:.10f}")
        print(f"   D = {D_ls:.10f}")
        print(f"   RMSE = {metricas_ls['RMSE']:.4f} %UR")
        print(f"   R² = {metricas_ls['R2']:.4f}\n")
    else:
        print("   Falhou!\n")
    
    # Método 3: differential_evolution
    print("3. Calibrando com differential_evolution...")
    resultado_de = calibrar_usando_differential_evolution(pontos_calibracao)
    if resultado_de is not None:
        A_de, B_de, C_de, D_de = resultado_de
        metricas_de = calcular_metricas(pontos_calibracao, A_de, B_de, C_de, D_de)
        resultados['differential_evolution'] = {'constantes': (A_de, B_de, C_de, D_de), 'metricas': metricas_de}
        print(f"   A = {A_de:.10f}")
        print(f"   B = {B_de:.10f}")
        print(f"   C = {C_de:.10f}")
        print(f"   D = {D_de:.10f}")
        print(f"   RMSE = {metricas_de['RMSE']:.4f} %UR")
        print(f"   R² = {metricas_de['R2']:.4f}\n")
    else:
        print("   Falhou!\n")
    
    # Seleciona o melhor método (menor RMSE)
    if resultados:
        melhor_metodo = min(resultados.keys(), key=lambda m: resultados[m]['metricas']['RMSE'])
        melhor_constantes = resultados[melhor_metodo]['constantes']
        melhor_metricas = resultados[melhor_metodo]['metricas']
        A, B, C, D = melhor_constantes
        
        print("=" * 70)
        print(f"RESULTADO FINAL - Melhor método: {melhor_metodo.upper()}")
        print("=" * 70)
        print(f"A = {A:.10f}")
        print(f"B = {B:.10f}")
        print(f"C = {C:.10f}")
        print(f"D = {D:.10f}")
        print(f"T_ref = {T_REFERENCIA:.2f}°C")
        print(f"\nMétricas de qualidade:")
        print(f"  RMSE (Root Mean Square Error) = {melhor_metricas['RMSE']:.4f} %UR")
        print(f"  MAE (Mean Absolute Error)     = {melhor_metricas['MAE']:.4f} %UR")
        print(f"  R² (Coeficiente de determinação) = {melhor_metricas['R2']:.4f}")
        print(f"  Erro máximo                   = {melhor_metricas['erro_max']:.4f} %UR")
        print(f"  Erro médio                    = {melhor_metricas['erro_medio']:.4f} %UR")
        print("=" * 70)
        
        # Salva constantes
        salvar_constantes(arquivo_saida, A, B, C, D, metodo=melhor_metodo, T_ref=T_REFERENCIA)
        print(f"\nConstantes salvas em: {arquivo_saida}")
        
        # Gera gráfico
        nome_grafico = arquivo_saida.replace('.txt', '.png')
        plotar_comparacao(pontos_calibracao, A, B, C, D, nome_grafico, T_ref=T_REFERENCIA)
        
    else:
        print("Erro: Nenhum método de calibração funcionou!")
        return


if __name__ == "__main__":
    main()
