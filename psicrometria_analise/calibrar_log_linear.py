"""
Script de Calibração pelo Método Log-Linear
Este script ajusta as constantes a e b do modelo log-linear usando pontos conhecidos.

Fórmula utilizada (modelo log-linear):
ln(UR) = a + b * ΔT

ou equivalentemente:
UR = exp(a + b * ΔT)

Onde:
- TS = Temperatura do bulbo seco (temperatura seca) em °C
- TU = Temperatura do bulbo úmido (temperatura úmida) em °C
- ΔT = TS - TU (diferença de temperatura)
- UR = Umidade relativa em porcentagem (0-100)
- a, b = Constantes a serem calibradas (apenas 2 constantes!)

Referência: METODO_EQUIVALENCIA_UR.md (seção 5 - Modelo log-linear)

Este método é muito robusto, monotônico, não oscila e tem comportamento físico plausível.
É excelente em prática e mais simples que outros modelos (apenas 2 constantes).
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


def calcular_umidade_relativa_log_linear(TS, TU, a, b):
    """
    Calcula a umidade relativa usando o modelo log-linear.
    
    Fórmula: UR = exp(a + b * ΔT)
    onde ΔT = TS - TU
    
    Parâmetros:
    -----------
    TS : float ou array
        Temperatura do bulbo seco (temperatura seca) em graus Celsius
    TU : float ou array
        Temperatura do bulbo úmido (temperatura úmida) em graus Celsius
    a, b : float
        Constantes do modelo log-linear
    
    Retorna:
    --------
    float ou array
        Umidade relativa em porcentagem (0-100)
    """
    # Calcula diferença de temperatura
    delta_T = TS - TU
    
    # Modelo log-linear: UR = exp(a + b * ΔT)
    # Trabalha com UR em escala decimal (0-1), depois multiplica por 100
    UR = np.exp(a + b * delta_T) * 100
    
    # Limita UR entre 0 e 100%
    UR = np.clip(UR, 0, 100)
    
    return UR


def funcao_residuo(constantes, pontos_calibracao):
    """
    Calcula o resíduo (erro) entre os valores de UR calculados e os valores reais.
    
    Parâmetros:
    -----------
    constantes : array
        Array com [a, b]
    pontos_calibracao : array
        Array com formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    
    Retorna:
    --------
    array
        Array com os resíduos (diferença entre UR calculado e UR real)
    """
    a, b = constantes
    
    residuos = []
    for TS, TU, UR_real in pontos_calibracao:
        # Calcula UR usando as constantes
        UR_calculado = calcular_umidade_relativa_log_linear(TS, TU, a, b)
        
        # Calcula o resíduo (diferença)
        residuo = UR_real - UR_calculado
        residuos.append(residuo)
    
    return np.array(residuos)


def funcao_para_curve_fit(pontos_TS_TU, a, b):
    """
    Função auxiliar para usar com curve_fit.
    
    Parâmetros:
    -----------
    pontos_TS_TU : array
        Array com formato [[TS1, TU1], [TS2, TU2], ...]
    a, b : float
        Constantes do modelo
    
    Retorna:
    --------
    array
        Array com UR calculado para cada ponto
    """
    UR_calculado = []
    for TS, TU in pontos_TS_TU:
        UR = calcular_umidade_relativa_log_linear(TS, TU, a, b)
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


def salvar_constantes(nome_arquivo, a, b, metodo='curve_fit'):
    """
    Salva as constantes a e b calibradas em um arquivo de texto.
    
    Parâmetros:
    -----------
    nome_arquivo : str
        Caminho do arquivo para salvar
    a, b : float
        Constantes calibradas
    metodo : str
        Nome do método usado para calibração
    """
    try:
        with open(nome_arquivo, 'w', encoding='utf-8') as f:
            f.write("# Constantes calibradas pelo Método Log-Linear\n")
            f.write(f"# Método utilizado: {metodo}\n")
            f.write(f"# Fórmula: UR = exp(a + b * ΔT) * 100\n")
            f.write(f"# onde ΔT = TS - TU (diferença entre temperatura seca e úmida)\n")
            f.write(f"# Equivalente a: ln(UR) = a + b * ΔT\n")
            f.write(f"\n")
            f.write(f"a = {a:.10f}\n")
            f.write(f"b = {b:.10f}\n")
    except Exception as e:
        print(f"Erro ao salvar arquivo '{nome_arquivo}': {e}")


def carregar_constantes_de_arquivo(nome_arquivo):
    """
    Carrega as constantes a e b de um arquivo de texto.
    
    Parâmetros:
    -----------
    nome_arquivo : str
        Caminho do arquivo
    
    Retorna:
    --------
    tuple
        (a, b) ou None se houver erro
    """
    try:
        a = b = None
        with open(nome_arquivo, 'r', encoding='utf-8') as f:
            for linha in f:
                linha = linha.strip()
                if linha.startswith('a =') or linha.startswith('a='):
                    try:
                        a = float(linha.split('=')[1].strip())
                    except (ValueError, IndexError):
                        continue
                elif linha.startswith('b =') or linha.startswith('b='):
                    try:
                        b = float(linha.split('=')[1].strip())
                    except (ValueError, IndexError):
                        continue
        
        if a is None or b is None:
            print(f"Erro: Constantes incompletas no arquivo '{nome_arquivo}'.")
            return None
        
        return (a, b)
    
    except FileNotFoundError:
        print(f"Erro: Arquivo '{nome_arquivo}' não encontrado.")
        return None
    except Exception as e:
        print(f"Erro ao ler arquivo '{nome_arquivo}': {e}")
        return None


def calibrar_usando_curve_fit(pontos_calibracao):
    """
    Calibra a e b usando curve_fit do scipy.
    
    Parâmetros:
    -----------
    pontos_calibracao : array
        Array com formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    
    Retorna:
    --------
    tuple
        ((a, b), parametros_covariancia) ou (None, None) se falhar
    """
    TS_TU = pontos_calibracao[:, :2]
    UR_real = pontos_calibracao[:, 2]
    
    # Valores iniciais para a e b
    # Para obter valores iniciais, faz regressão linear simples: ln(UR) = a + b * ΔT
    delta_T = pontos_calibracao[:, 0] - pontos_calibracao[:, 1]
    ln_UR = np.log(np.clip(UR_real / 100.0, 1e-6, 1.0))  # Evita ln(0) ou valores negativos
    
    # Regressão linear simples
    coef = np.polyfit(delta_T, ln_UR, 1)
    a_inicial = coef[1]  # Intercepto
    b_inicial = coef[0]  # Coeficiente angular
    
    constantes_iniciais = [a_inicial, b_inicial]
    
    # Limites razoáveis para as constantes
    # a: pode variar bastante (ln(UR) quando ΔT=0)
    # b: coeficiente angular típico entre -2 e 2
    limites_inferiores = [-10.0, -5.0]
    limites_superiores = [10.0, 5.0]
    
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
        
        a, b = popt
        return (a, b), pcov
    
    except Exception as e:
        print(f"Erro ao calibrar com curve_fit: {e}")
        return None, None


def calibrar_usando_least_squares(pontos_calibracao):
    """
    Calibra a e b usando least_squares do scipy.
    
    Parâmetros:
    -----------
    pontos_calibracao : array
        Array com formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    
    Retorna:
    --------
    tuple
        (a, b) ou None se falhar
    """
    # Obtém valores iniciais via regressão linear
    delta_T = pontos_calibracao[:, 0] - pontos_calibracao[:, 1]
    UR_real = pontos_calibracao[:, 2]
    ln_UR = np.log(np.clip(UR_real / 100.0, 1e-6, 1.0))
    
    coef = np.polyfit(delta_T, ln_UR, 1)
    a_inicial = coef[1]
    b_inicial = coef[0]
    
    constantes_iniciais = [a_inicial, b_inicial]
    
    # Limites
    limites_inferiores = [-10.0, -5.0]
    limites_superiores = [10.0, 5.0]
    
    try:
        resultado = least_squares(
            funcao_residuo,
            constantes_iniciais,
            args=(pontos_calibracao,),
            bounds=(limites_inferiores, limites_superiores),
            method='trf'  # Trust Region Reflective
        )
        
        if resultado.success:
            a, b = resultado.x
            return (a, b)
        else:
            print(f"Erro: Otimização não convergiu. Mensagem: {resultado.message}")
            return None
    
    except Exception as e:
        print(f"Erro ao calibrar com least_squares: {e}")
        return None


def calibrar_usando_differential_evolution(pontos_calibracao):
    """
    Calibra a e b usando differential_evolution do scipy.
    
    Parâmetros:
    -----------
    pontos_calibracao : array
        Array com formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    
    Retorna:
    --------
    tuple
        (a, b) ou None se falhar
    """
    def funcao_objetivo(constantes_array):
        """Função objetivo: soma dos quadrados dos resíduos"""
        a, b = constantes_array
        residuos = funcao_residuo([a, b], pontos_calibracao)
        return np.sum(residuos**2)
    
    # Limites para cada constante
    limites = [
        (-10.0, 10.0),  # a
        (-5.0, 5.0)     # b
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
            a, b = resultado.x
            return (a, b)
        else:
            print(f"Erro: Otimização não convergiu. Mensagem: {resultado.message}")
            return None
    
    except Exception as e:
        print(f"Erro ao calibrar com differential_evolution: {e}")
        return None


def calcular_metricas(pontos_calibracao, a, b):
    """
    Calcula métricas de qualidade da calibração.
    
    Parâmetros:
    -----------
    pontos_calibracao : array
        Array com formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    a, b : float
        Constantes calibradas
    
    Retorna:
    --------
    dict
        Dicionário com métricas (RMSE, MAE, R², etc.)
    """
    UR_real = pontos_calibracao[:, 2]
    UR_calculado = []
    
    for TS, TU, _ in pontos_calibracao:
        UR_calc = calcular_umidade_relativa_log_linear(TS, TU, a, b)
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


def plotar_comparacao(pontos_calibracao, a, b, nome_arquivo_saida='calibracao_log_linear.png'):
    """
    Plota gráficos de comparação entre valores reais e calculados.
    
    Parâmetros:
    -----------
    pontos_calibracao : array
        Array com formato [[TS1, TU1, UR1], [TS2, TU2, UR2], ...]
    a, b : float
        Constantes calibradas
    nome_arquivo_saida : str
        Nome do arquivo para salvar o gráfico
    """
    metricas = calcular_metricas(pontos_calibracao, a, b)
    
    UR_real = metricas['UR_real']
    UR_calculado = metricas['UR_calculado']
    residuos = metricas['residuos']
    delta_T = pontos_calibracao[:, 0] - pontos_calibracao[:, 1]
    
    # Cria figura com subplots
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle(f'Calibração pelo Método Log-Linear\nln(UR) = a + b * ΔT, onde a={a:.4f}, b={b:.4f}', 
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
    
    # Gráfico 4: Modelo log-linear (ln(UR) vs ΔT)
    ax4 = axes[1, 1]
    ln_UR_real = np.log(np.clip(UR_real / 100.0, 1e-6, 1.0))
    ln_UR_calculado = np.log(np.clip(UR_calculado / 100.0, 1e-6, 1.0))
    
    # Ordena para plotar linha suave
    indices_ordenados = np.argsort(delta_T)
    delta_T_ordenado = delta_T[indices_ordenados]
    ln_UR_calc_ordenado = ln_UR_calculado[indices_ordenados]
    
    ax4.scatter(delta_T, ln_UR_real, alpha=0.6, s=50, label='ln(UR) Real', color='blue')
    ax4.plot(delta_T_ordenado, ln_UR_calc_ordenado, 'r-', label='ln(UR) = a + b*ΔT', linewidth=2)
    ax4.set_xlabel('ΔT = TS - TU (°C)', fontsize=11)
    ax4.set_ylabel('ln(UR)', fontsize=11)
    ax4.set_title('Modelo Log-Linear', fontsize=11)
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
        arquivo_saida = 'constantes_log_linear.txt'
        if not os.path.exists(arquivo_saida):
            try:
                arquivo_saida = input("Digite o nome do arquivo de saída (ou Enter para 'constantes_log_linear.txt'): ").strip()
                if not arquivo_saida:
                    arquivo_saida = 'constantes_log_linear.txt'
            except (EOFError, KeyboardInterrupt):
                arquivo_saida = 'constantes_log_linear.txt'
    
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
    
    delta_T = pontos_calibracao[:, 0] - pontos_calibracao[:, 1]
    print(f"Faixa de ΔT: {delta_T.min():.2f} a {delta_T.max():.2f} °C\n")
    
    # Testa diferentes métodos de calibração
    print("=" * 70)
    print("CALIBRAÇÃO PELO MÉTODO LOG-LINEAR")
    print("=" * 70)
    print("\nTestando diferentes métodos de otimização...\n")
    
    resultados = {}
    
    # Método 1: curve_fit
    print("1. Calibrando com curve_fit...")
    resultado_cf = calibrar_usando_curve_fit(pontos_calibracao)
    if resultado_cf[0] is not None:
        a_cf, b_cf = resultado_cf[0]
        metricas_cf = calcular_metricas(pontos_calibracao, a_cf, b_cf)
        resultados['curve_fit'] = {'constantes': (a_cf, b_cf), 'metricas': metricas_cf}
        print(f"   a = {a_cf:.10f}")
        print(f"   b = {b_cf:.10f}")
        print(f"   RMSE = {metricas_cf['RMSE']:.4f} %UR")
        print(f"   R² = {metricas_cf['R2']:.4f}\n")
    else:
        print("   Falhou!\n")
    
    # Método 2: least_squares
    print("2. Calibrando com least_squares (TRF)...")
    resultado_ls = calibrar_usando_least_squares(pontos_calibracao)
    if resultado_ls is not None:
        a_ls, b_ls = resultado_ls
        metricas_ls = calcular_metricas(pontos_calibracao, a_ls, b_ls)
        resultados['least_squares'] = {'constantes': (a_ls, b_ls), 'metricas': metricas_ls}
        print(f"   a = {a_ls:.10f}")
        print(f"   b = {b_ls:.10f}")
        print(f"   RMSE = {metricas_ls['RMSE']:.4f} %UR")
        print(f"   R² = {metricas_ls['R2']:.4f}\n")
    else:
        print("   Falhou!\n")
    
    # Método 3: differential_evolution
    print("3. Calibrando com differential_evolution...")
    resultado_de = calibrar_usando_differential_evolution(pontos_calibracao)
    if resultado_de is not None:
        a_de, b_de = resultado_de
        metricas_de = calcular_metricas(pontos_calibracao, a_de, b_de)
        resultados['differential_evolution'] = {'constantes': (a_de, b_de), 'metricas': metricas_de}
        print(f"   a = {a_de:.10f}")
        print(f"   b = {b_de:.10f}")
        print(f"   RMSE = {metricas_de['RMSE']:.4f} %UR")
        print(f"   R² = {metricas_de['R2']:.4f}\n")
    else:
        print("   Falhou!\n")
    
    # Seleciona o melhor método (menor RMSE)
    if resultados:
        melhor_metodo = min(resultados.keys(), key=lambda m: resultados[m]['metricas']['RMSE'])
        melhor_constantes = resultados[melhor_metodo]['constantes']
        melhor_metricas = resultados[melhor_metodo]['metricas']
        a, b = melhor_constantes
        
        print("=" * 70)
        print(f"RESULTADO FINAL - Melhor método: {melhor_metodo.upper()}")
        print("=" * 70)
        print(f"a = {a:.10f}")
        print(f"b = {b:.10f}")
        print(f"\nFórmula: UR = exp(a + b * ΔT) * 100")
        print(f"Equivalente a: ln(UR) = a + b * ΔT")
        print(f"\nMétricas de qualidade:")
        print(f"  RMSE (Root Mean Square Error) = {melhor_metricas['RMSE']:.4f} %UR")
        print(f"  MAE (Mean Absolute Error)     = {melhor_metricas['MAE']:.4f} %UR")
        print(f"  R² (Coeficiente de determinação) = {melhor_metricas['R2']:.4f}")
        print(f"  Erro máximo                   = {melhor_metricas['erro_max']:.4f} %UR")
        print(f"  Erro médio                    = {melhor_metricas['erro_medio']:.4f} %UR")
        print("=" * 70)
        
        # Salva constantes
        salvar_constantes(arquivo_saida, a, b, metodo=melhor_metodo)
        print(f"\nConstantes salvas em: {arquivo_saida}")
        
        # Gera gráfico
        nome_grafico = arquivo_saida.replace('.txt', '.png')
        plotar_comparacao(pontos_calibracao, a, b, nome_grafico)
        
    else:
        print("Erro: Nenhum método de calibração funcionou!")
        return


if __name__ == "__main__":
    main()
