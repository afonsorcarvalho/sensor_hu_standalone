"""
Script para gerar Carta Psicrométrica
Mostra as curvas de umidade relativa constante usando a equação calibrada
e os pontos de calibração sobrepostos
"""

import numpy as np
import matplotlib
import matplotlib.pyplot as plt
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


def calcular_TU_para_UR(TS, UR_desejada, A, B, C, D, TU_inicial=None):
    """
    Calcula a temperatura de bulbo úmido (TU) para uma dada TS e UR desejada.
    Usa busca binária/bissecção para resolver a equação implicitamente.
    
    Parâmetros:
    -----------
    TS : float
        Temperatura do bulbo seco
    UR_desejada : float
        Umidade relativa desejada (%)
    A, B, C, D : float
        Constantes da fórmula psicrométrica
    TU_inicial : float
        Não usado, mantido para compatibilidade
    
    Retorna:
    --------
    float
        Temperatura de bulbo úmido (TU) que produz a UR desejada
    """
    # Limites físicos: TU deve estar entre TS-30 e TS
    TU_min = max(TS - 30, -10)
    TU_max = TS
    
    # Tenta usar scipy se disponível
    try:
        from scipy.optimize import fsolve
        def funcao_objetivo(TU):
            UR_calc = calcular_umidade_relativa(TS, TU, A, B, C, D)
            return UR_calc - UR_desejada
        
        TU_resultado = fsolve(funcao_objetivo, (TU_min + TU_max) / 2)[0]
        TU_resultado = np.clip(TU_resultado, TU_min, TU_max)
        return TU_resultado
    except ImportError:
        # Se scipy não estiver disponível, usa busca binária
        # UR aumenta quando TU aumenta (geralmente), então podemos fazer busca binária
        precisao = 0.01
        max_iteracoes = 100
        
        TU_baixo = TU_min
        TU_alto = TU_max
        UR_baixo = calcular_umidade_relativa(TS, TU_baixo, A, B, C, D)
        UR_alto = calcular_umidade_relativa(TS, TU_alto, A, B, C, D)
        
        # Se a UR desejada está fora dos limites, retorna o limite mais próximo
        if UR_desejada <= UR_baixo:
            return TU_baixo
        if UR_desejada >= UR_alto:
            return TU_alto
        
        # Busca binária
        for _ in range(max_iteracoes):
            TU_meio = (TU_baixo + TU_alto) / 2
            UR_meio = calcular_umidade_relativa(TS, TU_meio, A, B, C, D)
            
            if abs(UR_meio - UR_desejada) < precisao:
                return TU_meio
            
            if UR_meio < UR_desejada:
                TU_baixo = TU_meio
                UR_baixo = UR_meio
            else:
                TU_alto = TU_meio
                UR_alto = UR_meio
        
        return TU_meio


def carregar_pontos_de_arquivo(nome_arquivo):
    """
    Carrega pontos de calibração de um arquivo.
    """
    pontos = []
    
    try:
        with open(nome_arquivo, 'r', encoding='utf-8') as f:
            for num_linha, linha in enumerate(f, 1):
                linha = linha.strip()
                
                if not linha or linha.startswith('#'):
                    continue
                
                valores = None
                if ',' in linha:
                    valores = [v.strip() for v in linha.split(',')]
                else:
                    valores = linha.split()
                
                if len(valores) < 3:
                    continue
                
                try:
                    TS = float(valores[0])
                    TU = float(valores[1])
                    UR = float(valores[2])
                    pontos.append([TS, TU, UR])
                    
                except ValueError:
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
    """
    constantes = {}
    
    try:
        with open(nome_arquivo, 'r', encoding='utf-8') as f:
            for linha in f:
                linha = linha.strip()
                
                if not linha or linha.startswith('#'):
                    continue
                
                match = re.match(r'([A-D])\s*=\s*([0-9.]+)', linha, re.IGNORECASE)
                if match:
                    constante = match.group(1).upper()
                    valor = float(match.group(2))
                    constantes[constante] = valor
        
        for c in ['A', 'B', 'C', 'D']:
            if c not in constantes:
                raise ValueError(f"Constante {c} não encontrada no arquivo!")
        
        return constantes
    
    except FileNotFoundError:
        raise FileNotFoundError(f"Arquivo não encontrado: {nome_arquivo}")
    except Exception as e:
        raise Exception(f"Erro ao ler arquivo de constantes: {e}")


def plotar_carta_psicrometrica(pontos_calibracao, constantes, nome_arquivo_saida='carta_psicrometrica.png'):
    """
    Gera carta psicrométrica completa similar à padrão, mostrando:
    - Curvas de umidade relativa constante (UR) de 10% a 100%
    - Linhas de temperatura de bulbo úmido constante (TU)
    - Pontos de calibração sobrepostos
    - Linha de saturação (UR=100%)
    """
    pontos = np.array(pontos_calibracao)
    TS_pontos = pontos[:, 0]
    TU_pontos = pontos[:, 1]
    UR_pontos = pontos[:, 2]
    
    # Extrai constantes calibradas
    A = constantes['A']
    B = constantes['B']
    C = constantes['C']
    D = constantes['D']
    
    # Define limites do gráfico baseado nos pontos + margem
    # Para eixos logarítmicos, garantir que valores sejam > 0
    TS_min = max(1.0, int(np.min(TS_pontos)) - 5)
    TS_max = min(50, int(np.max(TS_pontos)) + 10)
    
    # Cria grade de TS para calcular as curvas
    TS_array = np.linspace(TS_min, TS_max, 200)
    
    # Valores de UR para curvas (de 10% em 10% de 10% a 100%)
    valores_UR = np.arange(10, 101, 10)
    
    # Cria figura
    fig, ax = plt.subplots(figsize=(16, 12))
    
    # Define cores para curvas de UR usando colormap
    cmap = plt.get_cmap('viridis')
    cores_UR = cmap(np.linspace(0.2, 0.95, len(valores_UR)))
    
    print("Gerando curvas de umidade relativa constante...")
    
    # Plota curvas de UR constante
    dados_curvas_UR = {}
    for UR_valor, cor in zip(valores_UR, cores_UR):
        TU_curva = []
        TS_curva = []
        
        # Para cada TS, encontra o TU que produz a UR desejada
        for TS in TS_array:
            # Varre TU de (TS-30) até TS com passo fino
            TU_teste = np.linspace(max(TS - 30, TS_min - 20), TS, 100)
            UR_teste = calcular_umidade_relativa(TS * np.ones_like(TU_teste), TU_teste, A, B, C, D)
            
            # Encontra o TU mais próximo da UR desejada
            difs = np.abs(UR_teste - UR_valor)
            idx = np.argmin(difs)
            
            if difs[idx] < 2.0:  # Aceita se erro < 2%
                TU_encontrado = TU_teste[idx]
                # Garante que TU <= TS (fisicamente correto)
                if TU_encontrado <= TS and TU_encontrado >= TS - 30:
                    TU_curva.append(TU_encontrado)
                    TS_curva.append(TS)
        
        if len(TS_curva) > 2:
            dados_curvas_UR[UR_valor] = (np.array(TS_curva), np.array(TU_curva))
            
            # Plota a curva com estilo diferente para 100% (saturação)
            if UR_valor == 100:
                ax.plot(TS_curva, TU_curva, color='black', linewidth=3, 
                       alpha=0.9, label='Linha de Saturação (UR=100%)', zorder=5)
            elif UR_valor % 20 == 0:  # Destaque para 20%, 40%, 60%, 80%
                ax.plot(TS_curva, TU_curva, color=cor, linewidth=2.5, 
                       alpha=0.85, linestyle='-', label=f'UR={UR_valor:.0f}%', zorder=4)
            else:
                ax.plot(TS_curva, TU_curva, color=cor, linewidth=1.5, 
                       alpha=0.7, linestyle='--', zorder=3)
            
            # Adiciona labels em TODAS as curvas de UR para facilitar leitura
            if len(TS_curva) > 5:
                # Label no meio da curva para facilitar identificação
                idx_label = len(TS_curva) // 2
                # Para UR 100%, coloca label mais visível
                if UR_valor == 100:
                    ax.text(TS_curva[idx_label], TU_curva[idx_label] + 1.0, 
                           f'UR={UR_valor:.0f}%', fontsize=11, color='black',
                           weight='bold', ha='center', va='bottom',
                           bbox=dict(boxstyle='round,pad=0.4', facecolor='white', 
                                    alpha=0.95, edgecolor='black', linewidth=2))
                elif UR_valor % 10 == 0:  # Labels para todas as curvas de 10 em 10%
                    ax.text(TS_curva[idx_label], TU_curva[idx_label] + 0.6, 
                           f'{UR_valor:.0f}%', fontsize=9, color=cor,
                           weight='bold', ha='center', va='bottom',
                           bbox=dict(boxstyle='round,pad=0.25', facecolor='white', 
                                    alpha=0.9, edgecolor=cor))
    
    # Plota linhas auxiliares de temperatura de bulbo úmido constante (TU constante)
    # Essas são linhas horizontais que ajudam a localizar o valor de TU no eixo Y
    print("Gerando linhas auxiliares de temperatura de bulbo úmido constante...")
    TU_min_aux = max(1.0, TS_min - 10)  # Para eixos logarítmicos, garantir > 0
    valores_TU = np.linspace(TU_min_aux, TS_max, 30)  # Mais pontos para escala log
    
    for TU_const in valores_TU:
        if TU_const >= TU_min_aux and TU_const <= TS_max and TU_const > 0:
            # Linha horizontal de TU constante
            ax.axhline(y=TU_const, color='lightgray', linewidth=0.5, 
                      alpha=0.3, linestyle='--', zorder=1)
    
    # Adiciona labels de TU no eixo Y direito para facilitar leitura (escala log)
    ax2 = ax.twinx()
    ax2.set_yscale('log')
    ax2.set_ylim(ax.get_ylim())
    # Para escala log, usar valores espaçados logaritmicamente
    valores_TU_labels = np.logspace(np.log10(max(1.0, TS_min - 10)), 
                                   np.log10(TS_max), 8)
    ax2.set_yticks(valores_TU_labels)
    ax2.set_yticklabels([f'{tu:.1f}°C' if tu < 10 else f'{tu:.0f}°C' 
                        for tu in valores_TU_labels], fontsize=9, alpha=0.6)
    ax2.set_ylabel('Temperatura de Bulbo Úmido - TU (°C)', fontsize=14, fontweight='bold', alpha=0.7)
    
    # Plota os pontos de calibração
    print("Plotando pontos de calibração...")
    for i, (ts, tu, ur) in enumerate(zip(TS_pontos, TU_pontos, UR_pontos)):
        # Calcula UR usando a fórmula para verificar
        ur_calc = calcular_umidade_relativa(ts, tu, A, B, C, D)
        
        # Plota ponto real
        ax.scatter(ts, tu, s=350, color='red', marker='o', 
                  edgecolors='black', linewidths=3, zorder=10,
                  label='Pontos de Calibração' if i == 0 else '')
        
        # Adiciona anotação
        ax.annotate(f'P{i+1}\nTS={ts:.1f}°C\nTU={tu:.1f}°C\nUR={ur:.1f}%', 
                   (ts, tu), 
                   xytext=(15, 15), textcoords='offset points', 
                   fontsize=10, fontweight='bold',
                   bbox=dict(boxstyle='round,pad=0.5', facecolor='yellow', 
                            alpha=0.95, edgecolor='black', linewidth=2),
                   arrowprops=dict(arrowstyle='->', color='black', lw=2), zorder=11)
    
    # Configuração dos eixos principais (TS no X, TU no Y esquerdo)
    # Usa escalas logarítmicas para melhor visualização das relações
    ax.set_xlabel('Temperatura do Bulbo Seco - TS (°C) [Escala Logarítmica]', fontsize=16, fontweight='bold')
    ax.set_ylabel('Temperatura do Bulbo Úmido - TU (°C) [Escala Logarítmica]', fontsize=16, fontweight='bold')
    ax.set_title(f'CARTA PSICROMÉTRICA - Equação Calibrada (Eixos Logarítmicos)\n'
                f'Constantes: A={A:.4f}, B={B:.4f}, C={C:.4f}, D={D:.4f}\n'
                f'Uso: Localize TS (eixo X) e TU (eixo Y) para encontrar a curva de UR correspondente\n'
                f'Pressão: 101.325 kPa (nível do mar)', 
                fontsize=16, fontweight='bold', pad=25)
    
    # Define limites para evitar valores negativos ou zero nos eixos logarítmicos
    TS_min_log = max(TS_min, 1.0)  # Evita zero ou negativo
    TS_max_log = TS_max
    TU_min_log = max(max(TS_min - 15, np.min(TU_pontos) - 5), 1.0)  # Evita zero ou negativo
    TU_max_log = TS_max + 2
    
    # Configura eixos logarítmicos
    ax.set_xscale('log')
    ax.set_yscale('log')
    
    # Grid principal (logarítmico)
    ax.grid(True, alpha=0.3, linestyle='--', linewidth=0.8, zorder=0, which='major')
    ax.grid(True, alpha=0.15, linestyle=':', linewidth=0.5, zorder=0, which='minor')
    ax.set_xlim(TS_min_log, TS_max_log)
    ax.set_ylim(TU_min_log, TU_max_log)
    
    # Legenda
    handles, labels = ax.get_legend_handles_labels()
    seen = set()
    handles_unicos = []
    labels_unicos = []
    for h, l in zip(handles, labels):
        if l not in seen:
            seen.add(l)
            handles_unicos.append(h)
            labels_unicos.append(l)
    
    ax.legend(handles=handles_unicos, loc='upper left', fontsize=11, 
             framealpha=0.95, fancybox=True, shadow=True)
    
    # Ajusta layout
    plt.tight_layout()
    
    # Salva o gráfico
    plt.savefig(nome_arquivo_saida, dpi=300, bbox_inches='tight')
    print(f"\nCarta psicrométrica salva em: {nome_arquivo_saida}")

    # Tenta mostrar
    try:
        backend_atual = matplotlib.get_backend()
        if backend_atual.lower() != 'agg':
            plt.show()
        else:
            print(f"Para visualizar, abra o arquivo '{nome_arquivo_saida}'")
    except Exception as e:
        print(f"Aviso: Não foi possível exibir o gráfico interativamente: {e}")
        print(f"Para visualizar, abra o arquivo '{nome_arquivo_saida}'")
    finally:
        plt.close()


def main():
    """
    Função principal do script.
    """
    import sys
    
    print("="*80)
    print("GERAÇÃO DE CARTA PSICROMÉTRICA")
    print("="*80)
    print("\nEste script gera uma carta psicrométrica mostrando:")
    print("  - Curvas de umidade relativa constante (UR)")
    print("  - Pontos de calibração sobrepostos")
    print("  - Equação calibrada aplicada\n")
    
    # Valores padrão
    arquivo_pontos = "pontos_calibracao.txt"
    arquivo_constantes = "constantes_trf.txt"
    arquivo_saida = "carta_psicrometrica.png"
    
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
            entrada = input("Nome do arquivo com os pontos de calibração (padrão: pontos_calibracao.txt): ").strip()
            if entrada:
                arquivo_pontos = entrada
            
            entrada = input("Nome do arquivo com as constantes (padrão: constantes_trf.txt): ").strip()
            if entrada:
                arquivo_constantes = entrada
            
            entrada = input("Nome do arquivo de saída (padrão: carta_psicrometrica.png): ").strip()
            if entrada:
                arquivo_saida = entrada
        except (EOFError, KeyboardInterrupt):
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
        print("Gerando carta psicrométrica...")
        print("="*80)
        plotar_carta_psicrometrica(pontos_calibracao, constantes, arquivo_saida)
        
        print("\n" + "="*80)
        print("Processo concluído com sucesso!")
        print("="*80)
        
    except Exception as e:
        print(f"\nErro: {e}")
        import traceback
        traceback.print_exc()


if __name__ == "__main__":
    main()
