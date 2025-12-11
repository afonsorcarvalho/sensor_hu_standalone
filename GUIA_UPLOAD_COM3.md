# Guia para Upload na COM3 - ESP32-S3

## Problema Comum
Após o primeiro upload, a COM3 pode não ser acessível. Isso acontece porque:
- A porta pode estar sendo usada por outro programa
- O ESP32 precisa entrar em modo bootloader
- O driver USB pode precisar ser reinicializado

## Solução Passo a Passo

### 1. Fechar Programas que Usam a Porta
- Feche o Serial Monitor do PlatformIO (se estiver aberto)
- Feche outros terminais ou programas que usem COM3
- Verifique no Gerenciador de Tarefas

### 2. Colocar ESP32 em Modo Bootloader

**Método Recomendado:**
1. Mantenha o botão **BOOT** pressionado na placa ESP32-S3
2. Pressione e solte o botão **RESET** (mantendo BOOT pressionado)
3. Solte o botão **BOOT**
4. Execute o upload **imediatamente** (dentro de 5 segundos)

**Método Alternativo:**
1. Desconecte o cabo USB
2. Mantenha o botão BOOT pressionado
3. Conecte o cabo USB novamente
4. Solte o botão BOOT
5. Execute o upload imediatamente

### 3. Ordem de Upload Recomendada

```bash
# 1. Primeiro, faça upload do firmware
pio run --target upload

# 2. Aguarde 2-3 segundos

# 3. Depois, faça upload do sistema de arquivos
pio run --target uploadfs
```

### 4. Se Ainda Não Funcionar

**Opção A: Reiniciar o Driver USB**
1. Abra o Gerenciador de Dispositivos
2. Encontre "Dispositivo Serial USB (COM3)"
3. Clique com botão direito → "Desinstalar dispositivo"
4. Marque "Tentar remover o driver"
5. Reconecte o cabo USB
6. Aguarde o Windows reinstalar o driver

**Opção B: Verificar se a Porta Mudou**
- Abra o Gerenciador de Dispositivos
- Verifique se ainda é COM3 ou se mudou (COM4, COM5, etc.)
- Se mudou, atualize o `platformio.ini` com a nova porta

**Opção C: Usar Upload Forçado**
```bash
pio run --target uploadfs --upload-port COM3
```

### 5. Verificar se Funcionou

Após o upload bem-sucedido:
1. Abra o Serial Monitor: `pio device monitor`
2. Você deve ver mensagens como:
   - "LittleFS montado com sucesso"
   - "AP IP address: 192.168.4.1"
   - "Servidor web iniciado na porta 80"

## Dicas Importantes

- **Sempre feche o Serial Monitor antes de fazer upload**
- **O ESP32-S3 precisa estar em modo bootloader para upload**
- **Execute o upload rapidamente após entrar em modo bootloader**
- **Se a porta mudar, atualize o `platformio.ini`**

## Comandos Úteis

```bash
# Listar portas disponíveis
pio device list

# Monitor serial
pio device monitor

# Upload apenas do firmware
pio run --target upload

# Upload apenas do filesystem
pio run --target uploadfs

# Upload forçado em porta específica
pio run --target uploadfs --upload-port COM3
```

