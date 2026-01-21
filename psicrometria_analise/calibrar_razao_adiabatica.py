"""
Script de Calibração pelo Método de Razão Adiabática Ajustada
Este script ajusta o fator de correção K da fórmula psicrométrica usando pontos conhecidos.

Fórmula utilizada (razão adiabática ajustada):
UR = (p_sat(TU) / p_sat(TS)) * K

Onde:
- TS = Temperatura do bulbo seco (temperatura seca) em °C
- TU = Temperatura do bulbo úmido (temperatura úmida) em °C
- UR = Umidade relativa em porcentagem (0-100)
- p_sat(T) = Pressão de saturação do vapor d'água à temperatura T
- K = Fator de correção empírico (constante a ser calibrada)

Referência: METODO_EQUIVALENCIA_UR.md

Este método é baseado em psicrometria física e é mais defensável tecnicamente.
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


def calcular_umidade_relativa_razao_adiabatica(TS, TU, K):
    """
    Calcula a umidade relativa usando o método de razão adiabática ajustada.
    
    Fórmula: UR = (p_sat(TU) / p_sat(TS)) * K * 100
    
    Parâmetros:
    -----------
    TS : float ou array
        Temperatura do bulbo seco (temperatura seca) em graus Celsius
    TU : float ou array
        Temperatura do bulbo úmido (temperatura úmida) em graus Celsius
    K : float
        Fator de correção empírico (constante a ser calibrada)
    
    Retorna:
    --------
    float ou array
        Umidade relativa em porcentagem (0-100)
    """
    # Calcula pressões de saturação
    p_sat_TU = calcular_pressao_saturacao_agua(TU)
    p_sat_TS = calcular_pressao_saturacao_agua(TS)
    
    # Evita divisão por zero
    p_sat_TS = np.where(p_sat_TS > 1e-10, p_sat_TS, 1e-10)
    
    # Razão adiabática ajustada
    UR = (p_sat_TU / p_sat_TS) * K * 100
    
    # Limita UR entre 0 e 100%
    UR = np.clip(UR, 0, 100)
    
    return UR


def funcao_residuo_K(K, pontos_calibracao):
    """
    Calcula o resíduo (erro) entre os valores de UR calculados e os valores reais.
    
    Parâmetros:
    -----------
    K : float
        Fator de correção empírico
    pontos_calibracao : array
        Array com formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    
    Retorna:
    --------
    array
        Array com os resíduos (diferença entre UR calculado e UR real)
    """
    residuos = []
    for TS, TU, UR_real in pontos_calibracao:
        # Calcula UR usando K
        UR_calculado = calcular_umidade_relativa_razao_adiabatica(TS, TU, K)
        
        # Calcula o resíduo (diferença)
        residuo = UR_real - UR_calculado
        residuos.append(residuo)
    
    return np.array(residuos)


def funcao_para_curve_fit(pontos_TS_TU, K):
    """
    Função auxiliar para usar com curve_fit.
    
    Parâmetros:
    -----------
    pontos_TS_TU : array
        Array com formato [[TS1, TU1], [TS2, TU2], ...]
    K : float
        Fator de correção empírico
    
    Retorna:
    --------
    array
        Array com UR calculado para cada ponto
    """
    UR_calculado = []
    for TS, TU in pontos_TS_TU:
        UR = calcular_umidade_relativa_razao_adiabatica(TS, TU, K)
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


def salvar_constantes(nome_arquivo, K, metodo='curve_fit'):
    """
    Salva a constante K calibrada em um arquivo de texto.
    
    Parâmetros:
    -----------
    nome_arquivo : str
        Caminho do arquivo para salvar
    K : float
        Fator de correção empírico calibrado
    metodo : str
        Nome do método usado para calibração
    """
    try:
        with open(nome_arquivo, 'w', encoding='utf-8') as f:
            f.write("# Constantes calibradas pelo método de Razão Adiabática Ajustada\n")
            f.write(f"# Método utilizado: {metodo}\n")
            f.write(f"# Fórmula: UR = (p_sat(TU) / p_sat(TS)) * K * 100\n")
            f.write(f"# onde p_sat(T) é calculado pela equação de Magnus\n")
            f.write(f"\n")
            f.write(f"K = {K:.10f}\n")
    except Exception as e:
        print(f"Erro ao salvar arquivo '{nome_arquivo}': {e}")


def carregar_constantes_de_arquivo(nome_arquivo):
    """
    Carrega a constante K de um arquivo de texto.
    
    Parâmetros:
    -----------
    nome_arquivo : str
        Caminho do arquivo
    
    Retorna:
    --------
    float
        Constante K, ou None se houver erro
    """
    try:
        with open(nome_arquivo, 'r', encoding='utf-8') as f:
            for linha in f:
                linha = linha.strip()
                if linha.startswith('K =') or linha.startswith('K='):
                    try:
                        K = float(linha.split('=')[1].strip())
                        return K
                    except (ValueError, IndexError):
                        continue
        print(f"Erro: Constante K não encontrada no arquivo '{nome_arquivo}'.")
        return None
    except FileNotFoundError:
        print(f"Erro: Arquivo '{nome_arquivo}' não encontrado.")
        return None
    except Exception as e:
        print(f"Erro ao ler arquivo '{nome_arquivo}': {e}")
        return None


def calibrar_usando_curve_fit(pontos_calibracao):
    """
    Calibra K usando curve_fit do scipy.
    
    Parâmetros:
    -----------
    pontos_calibracao : array
        Array com formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    
    Retorna:
    --------
    tuple
        (K_otimo, parametros_covariancia)
    """
    TS_TU = pontos_calibracao[:, :2]
    UR_real = pontos_calibracao[:, 2]
    
    # Valor inicial para K (razão adiabática ideal seria ~1.0, mas pode variar)
    K_inicial = 1.0
    
    # Limites para K (razoável entre 0.5 e 2.0)
    limites_K = (0.1, 5.0)
    
    try:
        popt, pcov = curve_fit(
            funcao_para_curve_fit,
            TS_TU,
            UR_real,
            p0=[K_inicial],
            bounds=limites_K,
            method='trf'  # Trust Region Reflective
        )
        
        K_otimo = popt[0]
        return K_otimo, pcov
    
    except Exception as e:
        print(f"Erro ao calibrar com curve_fit: {e}")
        return None, None


def calibrar_usando_least_squares(pontos_calibracao, K_inicial=1.0):
    """
    Calibra K usando least_squares do scipy.
    
    Parâmetros:
    -----------
    pontos_calibracao : array
        Array com formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    K_inicial : float
        Valor inicial para K
    
    Retorna:
    --------
    float
        K_otimo
    """
    # Limites para K
    limites = ([0.1], [5.0])
    
    try:
        resultado = least_squares(
            funcao_residuo_K,
            [K_inicial],
            args=(pontos_calibracao,),
            bounds=limites,
            method='trf'  # Trust Region Reflective
        )
        
        if resultado.success:
            K_otimo = resultado.x[0]
            return K_otimo
        else:
            print(f"Erro: Otimização não convergiu. Mensagem: {resultado.message}")
            return None
    
    except Exception as e:
        print(f"Erro ao calibrar com least_squares: {e}")
        return None


def calibrar_usando_differential_evolution(pontos_calibracao):
    """
    Calibra K usando differential_evolution do scipy.
    
    Parâmetros:
    -----------
    pontos_calibracao : array
        Array com formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    
    Retorna:
    --------
    float
        K_otimo
    """
    def funcao_objetivo(K_array):
        """Função objetivo: soma dos quadrados dos resíduos"""
        K = K_array[0]
        residuos = funcao_residuo_K(K, pontos_calibracao)
        return np.sum(residuos**2)
    
    # Limites para K
    limites = [(0.1, 5.0)]
    
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
            K_otimo = resultado.x[0]
            return K_otimo
        else:
            print(f"Erro: Otimização não convergiu. Mensagem: {resultado.message}")
            return None
    
    except Exception as e:
        print(f"Erro ao calibrar com differential_evolution: {e}")
        return None


def calcular_metricas(pontos_calibracao, K):
    """
    Calcula métricas de qualidade da calibração.
    
    Parâmetros:
    -----------
    pontos_calibracao : array
        Array com formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    K : float
        Fator de correção empírico
    
    Retorna:
    --------
    dict
        Dicionário com métricas (RMSE, MAE, R², etc.)
    """
    UR_real = pontos_calibracao[:, 2]
    UR_calculado = []
    
    for TS, TU, _ in pontos_calibracao:
        UR_calc = calcular_umidade_relativa_razao_adiabatica(TS, TU, K)
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


def plotar_comparacao(pontos_calibracao, K, nome_arquivo_saida='calibracao_razao_adiabatica.png'):
    """
    Plota gráficos de comparação entre valores reais e calculados.
    
    Parâmetros:
    -----------
    pontos_calibracao : array
        Array com formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    K : float
        Fator de correção empírico
    nome_arquivo_saida : str
        Nome do arquivo para salvar o gráfico
    """
    metricas = calcular_metricas(pontos_calibracao, K)
    
    UR_real = metricas['UR_real']
    UR_calculado = metricas['UR_calculado']
    residuos = metricas['residuos']
    
    # Cria figura com subplots
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle(f'Calibração pelo Método de Razão Adiabática Ajustada\nK = {K:.6f}', fontsize=14, fontweight='bold')
    
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
        arquivo_saida = 'constantes_razao_adiabatica.txt'
        if not os.path.exists(arquivo_saida):
            try:
                arquivo_saida = input("Digite o nome do arquivo de saída (ou Enter para 'constantes_razao_adiabatica.txt'): ").strip()
                if not arquivo_saida:
                    arquivo_saida = 'constantes_razao_adiabatica.txt'
            except (EOFError, KeyboardInterrupt):
                arquivo_saida = 'constantes_razao_adiabatica.txt'
    
    # Carrega pontos de calibração
    print(f"Carregando pontos de calibração de '{arquivo_pontos}'...")
    pontos_calibracao = carregar_pontos_de_arquivo(arquivo_pontos)
    
    if pontos_calibracao is None:
        print("Erro: Não foi possível carregar pontos de calibração.")
        return
    
    print(f"Pontos de calibração carregados: {len(pontos_calibracao)}")
    print(f"Faixa de TS: {pontos_calibracao[:, 0].min():.2f} a {pontos_calibracao[:, 0].max():.2f} °C")
    print(f"Faixa de TU: {pontos_calibracao[:, 1].min():.2f} a {pontos_calibracao[:, 1].max():.2f} °C")
    print(f"Faixa de UR: {pontos_calibracao[:, 2].min():.2f} a {pontos_calibracao[:, 2].max():.2f} %UR\n")
    
    # Testa diferentes métodos de calibração
    print("=" * 70)
    print("CALIBRAÇÃO PELO MÉTODO DE RAZÃO ADIABÁTICA AJUSTADA")
    print("=" * 70)
    print("\nTestando diferentes métodos de otimização...\n")
    
    resultados = {}
    
    # Método 1: curve_fit
    print("1. Calibrando com curve_fit...")
    K_cf, pcov_cf = calibrar_usando_curve_fit(pontos_calibracao)
    if K_cf is not None:
        metricas_cf = calcular_metricas(pontos_calibracao, K_cf)
        resultados['curve_fit'] = {'K': K_cf, 'metricas': metricas_cf}
        print(f"   K = {K_cf:.10f}")
        print(f"   RMSE = {metricas_cf['RMSE']:.4f} %UR")
        print(f"   R² = {metricas_cf['R2']:.4f}\n")
    else:
        print("   Falhou!\n")
    
    # Método 2: least_squares
    print("2. Calibrando com least_squares (TRF)...")
    K_ls = calibrar_usando_least_squares(pontos_calibracao, K_inicial=1.0)
    if K_ls is not None:
        metricas_ls = calcular_metricas(pontos_calibracao, K_ls)
        resultados['least_squares'] = {'K': K_ls, 'metricas': metricas_ls}
        print(f"   K = {K_ls:.10f}")
        print(f"   RMSE = {metricas_ls['RMSE']:.4f} %UR")
        print(f"   R² = {metricas_ls['R2']:.4f}\n")
    else:
        print("   Falhou!\n")
    
    # Método 3: differential_evolution
    print("3. Calibrando com differential_evolution...")
    K_de = calibrar_usando_differential_evolution(pontos_calibracao)
    if K_de is not None:
        metricas_de = calcular_metricas(pontos_calibracao, K_de)
        resultados['differential_evolution'] = {'K': K_de, 'metricas': metricas_de}
        print(f"   K = {K_de:.10f}")
        print(f"   RMSE = {metricas_de['RMSE']:.4f} %UR")
        print(f"   R² = {metricas_de['R2']:.4f}\n")
    else:
        print("   Falhou!\n")
    
    # Seleciona o melhor método (menor RMSE)
    if resultados:
        melhor_metodo = min(resultados.keys(), key=lambda m: resultados[m]['metricas']['RMSE'])
        melhor_K = resultados[melhor_metodo]['K']
        melhor_metricas = resultados[melhor_metodo]['metricas']
        
        print("=" * 70)
        print(f"RESULTADO FINAL - Melhor método: {melhor_metodo.upper()}")
        print("=" * 70)
        print(f"K = {melhor_K:.10f}")
        print(f"\nMétricas de qualidade:")
        print(f"  RMSE (Root Mean Square Error) = {melhor_metricas['RMSE']:.4f} %UR")
        print(f"  MAE (Mean Absolute Error)     = {melhor_metricas['MAE']:.4f} %UR")
        print(f"  R² (Coeficiente de determinação) = {melhor_metricas['R2']:.4f}")
        print(f"  Erro máximo                   = {melhor_metricas['erro_max']:.4f} %UR")
        print(f"  Erro médio                    = {melhor_metricas['erro_medio']:.4f} %UR")
        print("=" * 70)
        
        # Salva constante
        salvar_constantes(arquivo_saida, melhor_K, metodo=melhor_metodo)
        print(f"\nConstante K salva em: {arquivo_saida}")
        
        # Gera gráfico
        nome_grafico = arquivo_saida.replace('.txt', '.png')
        plotar_comparacao(pontos_calibracao, melhor_K, nome_grafico)
        
    else:
        print("Erro: Nenhum método de calibração funcionou!")
        return


if __name__ == "__main__":
    main()
