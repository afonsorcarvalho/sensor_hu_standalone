# Guia de Configuração - ESP32-S3-RS485-CAN

## Configuração de Pinos RS485

Os pinos RS485 podem variar conforme o hardware. Verifique a documentação da sua placa e ajuste em `src/main.cpp`:

```cpp
#define RS485_TX_PIN 17
#define RS485_RX_PIN 18
#define RS485_DE_RE_PIN 19
```

### Para ESP32-S3-RS485-CAN da Waveshare

Consulte a documentação oficial da Waveshare para confirmar os pinos corretos:
- [Waveshare ESP32-S3-RS485-CAN Wiki](https://www.waveshare.com/wiki/ESP32-S3-RS485-CAN)

## Configuração de Baud Rate

O baud rate padrão é 9600. Para alterar, edite em `src/main.cpp`:

```cpp
#define MODBUS_SERIAL_BAUD 9600
```

Baud rates comuns: 9600, 19200, 38400, 57600, 115200

## Personalização de Cálculos

Edite a função `performCalculations()` em `src/main.cpp` para implementar sua lógica:

```cpp
void performCalculations() {
    // Seu código aqui
    // Exemplo: média, soma, conversões, etc.
}
```

## Configuração WiFi

Para alterar SSID e senha do Access Point, edite em `src/main.cpp`:

```cpp
#define AP_SSID "ESP32-Modbus-Config"
#define AP_PASSWORD "12345678"
```

## Limites do Sistema

- Máximo de dispositivos: 10 (alterável em `MAX_DEVICES`)
- Máximo de registros por dispositivo: 20 (alterável em `MAX_REGISTERS_PER_DEVICE`)
- Intervalo de cálculos: 1000ms (alterável em `CALCULATION_INTERVAL_MS`)

## Exemplo de Uso da Interface Web

1. Conecte-se ao WiFi `ESP32-Modbus-Config`
2. Acesse `http://192.168.4.1`
3. Adicione dispositivos:
   - Endereço Modbus: 1-247
   - Adicione registros para cada dispositivo
   - Marque registros de saída para escrever resultados
4. Clique em "Salvar Configuração"
5. A configuração será salva permanentemente na EEPROM

## Troubleshooting

### Modbus não responde
- Verifique conexões RS485 (A, B, GND)
- Confirme endereços dos dispositivos escravos
- Verifique baud rate (deve ser igual em todos os dispositivos)
- Teste com um dispositivo de cada vez

### WiFi não conecta
- Verifique se o SSID aparece na lista de redes
- Tente reiniciar a placa
- Verifique se não há muitos dispositivos conectados ao AP

### Configuração não persiste
- Verifique espaço na EEPROM
- Tente salvar novamente
- Reinicie a placa e verifique se a configuração foi mantida

