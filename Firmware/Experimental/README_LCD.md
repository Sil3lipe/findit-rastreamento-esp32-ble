# FINDIT – Sistema de Localização de Itens e Setores de Estoque

**FATEC Indaiatuba – Análise e Desenvolvimento de Sistemas**  
Grupo FINDIT: Isabely Victória, Rubens Bartolomeu, Clara Ferrari, Filipe Matos Silva, Vitor Hoshika

---

## Visão Geral

O FINDIT é um sistema de rastreamento por BLE (Bluetooth Low Energy) que auxilia funcionários a localizar prateleiras e setores em estoques dinâmicos. Utiliza dois ESP32 em conjunto:

| ESP32 | Arquivo | Função |
|-------|---------|--------|
| #1 Scanner | `esp32_scanner_ble.c` | Escaneia beacons BLE e envia RSSI via UART |
| #2 Display | `esp32_display_interface_lcd.c` | Recebe RSSI, exibe no LCD 16x2 e toca buzzer |

---

## Arquitetura do Sistema

```
[Beacon BLE]         [Beacon BLE]
     |                    |
     |   (BLE passivo)    |
     v                    v
[ ESP32 #1 - Scanner ]
     |
     | UART (TX → RX)
     v
[ ESP32 #2 - Display ]
     |           |         |
  [LCD 16x2]  [Buzzer]  [Teclado 4x4]
```

---

## Hardware Necessário

- 2x ESP32 DevKit
- Display LCD 16x2 com módulo I2C PCF8574
- Buzzer passivo
- Teclado matricial 4x4
- Beacons BLE (qualquer dispositivo BLE com MAC fixo)
- Fios jumper + protoboard

---

## Pinagem

### ESP32 #1 (Scanner)
| Pino | GPIO | Função |
|------|------|--------|
| TX   | 17   | Envia dados para ESP32 #2 |
| GND  | GND  | GND compartilhado |

> ⚠️ Altere `TX_PIN` no arquivo se usar outro GPIO.

### ESP32 #2 (Display LCD)
| Pino | GPIO | Função |
|------|------|--------|
| RX      | 16  | Recebe dados do ESP32 #1 |
| SDA     | 21  | I2C dados (LCD) |
| SCL     | 22  | I2C clock (LCD) |
| Buzzer  | 25  | Saída PWM |
| LCD VCC | 5V  | ⚠️ LCD 16x2 exige 5V (não 3.3V) |
| GND     | GND | GND compartilhado |

> ⚠️ O ESP32 opera em 3.3V, mas o PCF8574 aceita I2C em 3.3V mesmo alimentado a 5V.  
> Verifique o datasheet do seu módulo antes de ligar.

### Endereço I2C do LCD
| Jumper A0/A1/A2 | Endereço |
|-----------------|----------|
| Todos soltos (padrão) | `0x27` |
| A2 soldado | `0x3F` |

> Se o LCD não inicializar, tente trocar `LCD_I2C_ADDR` de `0x27` para `0x3F` no código.

### Teclado 4x4
| Tipo | GPIOs padrão | Como alterar |
|------|-------------|--------------|
| Linhas (ROW) | 32, 33, 34, 35 | Array `ROW_PINS[]` no código |
| Colunas (COL) | 26, 27, 14, 12 | Array `COL_PINS[]` no código |

> ⚠️ Não use GPIOs 6–11 (flash SPI). GPIOs 34–39 não têm pull-up interno.

---

## Biblioteca LCD necessária

Este código usa a biblioteca **ESP32-HD44780** (ou compatível).  
Recomendação: https://github.com/maxsydney/ESP32-HD44780

**Instalação:**
```bash
# Na pasta raiz do seu projeto ESP-IDF:
mkdir components
cd components
git clone https://github.com/maxsydney/ESP32-HD44780
```

Após instalar, inclua no código:
```c
#include "LCD_I2C.h"
```

> ⚠️ Se usar outra biblioteca LCD, ajuste as chamadas de função:
> - `lcd_init()`, `lcd_print_line()`, `lcd_set_cursor()`, `lcd_write_str()`, `lcd_clear()`, `lcd_backlight()`

---

## Layout das Telas no LCD 16x2

```
┌────────────────┐     ┌────────────────┐     ┌────────────────┐
│FINDIT          │     │Romance  -65dBm │     │Nome (#=OK      │
│1-Loc 2-Cad 3-Cf│     │########        │     │Ficcao_         │
└────────────────┘     └────────────────┘     └────────────────┘
   Menu Inicial           Rastreamento            Cadastro
```

Durante o rastreamento:
- **Linha 0:** nome do gênero (8 chars) + RSSI em dBm
- **Linha 1:** barra de `#` indicando proximidade (mais `#` = mais perto)

---

## Configuração dos Beacons (ESP32 #1)

No arquivo `esp32_scanner_ble.c`, substitua os MACs pelos dos seus beacons reais:

```c
struct BEACON BEACONS[] = {
    {{0x00, {0xXX, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX}}}, // Setor 0 - Ex: Romance
    {{0x00, {0xXX, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX}}}  // Setor 1 - Ex: Ficção
};
```

> ⚠️ O MAC BLE aparece **invertido** nos scanners de celular.  
> Exemplo: scanner mostra `C5:E5:A1:49:36:XX` → cadastre `{0xXX, 0x36, 0x49, 0xa1, 0xe5, 0xc5}`

---

## Como Compilar (ESP-IDF)

```bash
# Pré-requisito: ESP-IDF v5.x instalado
# https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/

idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## Licença

Código desenvolvido pelo Grupo FINDIT para fins acadêmicos.  
Bibliotecas de terceiros utilizadas possuem licenças Apache 2.0 / MIT.
