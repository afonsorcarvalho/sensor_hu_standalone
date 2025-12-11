# Instruções de Upload

## Passos para fazer upload do projeto

### 1. Compilar o código
```bash
pio run
```

### 2. Fazer upload do sistema de arquivos (LittleFS)
**IMPORTANTE**: Este passo é necessário para que a interface web funcione!

```bash
pio run --target uploadfs
```

Este comando faz o upload do arquivo `data/index.html` para o sistema de arquivos LittleFS do ESP32.

### 3. Fazer upload do firmware
```bash
pio run --target upload
```

Ou combine tudo em um comando:
```bash
pio run --target upload && pio run --target uploadfs
```

## Ordem recomendada na primeira vez

1. Primeiro, faça upload do sistema de arquivos:
   ```bash
   pio run --target uploadfs
   ```

2. Depois, faça upload do firmware:
   ```bash
   pio run --target upload
   ```

## Após alterações no HTML

Se você modificar o arquivo `data/index.html`, você precisa fazer upload novamente do sistema de arquivos:

```bash
pio run --target uploadfs
```

## Após alterações no código C++

Se você modificar o código em `src/main.cpp`, você precisa recompilar e fazer upload:

```bash
pio run --target upload
```

## Verificar se funcionou

Após fazer upload:
1. Conecte-se ao WiFi `ESP32-Modbus-Config` (senha: `12345678`)
2. Acesse `http://192.168.4.1` no navegador
3. A interface de configuração deve aparecer

Se a página não carregar, verifique:
- Se o LittleFS foi montado corretamente (veja no Serial Monitor)
- Se o arquivo `data/index.html` existe
- Se o upload do sistema de arquivos foi feito com sucesso

