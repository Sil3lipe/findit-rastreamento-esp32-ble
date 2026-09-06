/**
 * =============================================================================
 * FINDIT - ESP32 #2: DISPLAY, INTERFACE E CONTROLE
 * VERSÃO: LCD 16x2 com I2C (módulo PCF8574)
 * =============================================================================
 * Função: Recebe dados de RSSI do ESP32 #1 via UART, exibe no LCD 16x2,
 *         emite bipes sonoros proporcionais à proximidade e permite ao usuário
 *         navegar pelas funcionalidades via teclado matricial 4x4.
 *
 * Diferenças em relação à versão OLED:
 *   - Display LCD 16x2: 2 linhas de 16 caracteres (sem gráficos, só texto)
 *   - A barra de proximidade é feita com caracteres '#' na segunda linha
 *   - Biblioteca: esp32-smbus + esp-idf-lib (LiquidCrystal_I2C para ESP-IDF)
 *     OU você pode usar a biblioteca "hd44780" via componente ESP-IDF
 *   - Endereço I2C padrão do PCF8574: 0x27 (pode ser 0x3F em alguns módulos)
 *
 * Conexão física:
 *   LCD SDA  →  GPIO 21
 *   LCD SCL  →  GPIO 22
 *   LCD VCC  →  5V (atenção: LCD 16x2 geralmente exige 5V)
 *   LCD GND  →  GND
 *   ESP32 #2 RX  →  GPIO 16  →  TX do ESP32 #1
 *   Buzzer   →  GPIO 25
 *
 * ⚠️ O ESP32 opera em 3.3V mas o PCF8574 aceita comunicação I2C em 3.3V
 *    mesmo alimentado em 5V. Verifique o datasheet do seu módulo.
 * =============================================================================
 */

#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "nvs.h"

/**
 * Biblioteca LCD I2C para ESP-IDF:
 * Use o componente "esp_lcd_i2c" ou a lib "LiquidCrystal_I2C" portada.
 * Recomendação: https://github.com/maxsydney/ESP32-HD44780
 * Após clonar para /components, inclua o header abaixo:
 */
#include "LCD_I2C.h"  // ⚠️ Altere para o header da biblioteca LCD que você usar

// =============================================================================
// CONFIGURAÇÃO DE HARDWARE
// =============================================================================

// --- UART ---
#define UART_NUM    UART_NUM_1
#define RX_PIN      16           // ⚠️ GPIO RX vindo do TX do ESP32 #1

// --- I2C / LCD ---
#define I2C_SDA_PIN   21         // Pino SDA do barramento I2C
#define I2C_SCL_PIN   22         // Pino SCL do barramento I2C
#define I2C_FREQ_HZ   100000     // LCD 16x2 opera melhor em 100 kHz (modo padrão)
#define LCD_I2C_ADDR  0x27       // ⚠️ Endereço I2C do módulo PCF8574
                                 // Se não funcionar, tente 0x3F
#define LCD_COLS      16         // Número de colunas do LCD
#define LCD_ROWS      2          // Número de linhas do LCD

// --- BUZZER ---
#define BUZZER_PIN       25
#define LEDC_CHANNEL     LEDC_CHANNEL_0
#define LEDC_TIMER       LEDC_TIMER_0
#define LEDC_SPEED_MODE  LEDC_LOW_SPEED_MODE
#define LEDC_RESOLUTION  LEDC_TIMER_10_BIT

// =============================================================================
// CONFIGURAÇÃO DO TECLADO MATRICIAL 4x4
// =============================================================================

// ⚠️ Altere os GPIOs abaixo conforme sua montagem
static const int ROW_PINS[4] = {32, 33, 34, 35}; // Linhas (saída)
static const int COL_PINS[4] = {26, 27, 14, 12};  // Colunas (entrada com pull-up)

static const char KEY_MAP[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

// =============================================================================
// CONFIGURAÇÃO DE GÊNEROS / BEACONS
// =============================================================================

#define MAX_BEACONS   10
#define MAX_NAME_LEN  16         // 15 chars + '\0' (cabe exato em 1 linha do LCD)
#define NVS_NAMESPACE "findit"

// =============================================================================
// VARIÁVEIS GLOBAIS DE ESTADO
// =============================================================================

volatile int g_listen_index       = 0;
volatile int g_last_rssi          = 0;
volatile int g_seconds_since_last = 0;
volatile int g_num_beacons        = 0;
volatile int g_app_state          = 0;  // 0=menu, 1=rastreando, 2=cadastro

char g_beacon_names[MAX_BEACONS][MAX_NAME_LEN];

SemaphoreHandle_t g_state_mutex = NULL;
QueueHandle_t     g_key_queue   = NULL;

// Handle do LCD (tipo depende da biblioteca escolhida)
// Ajuste conforme a API da biblioteca que você instalar:
static lcd_handle_t lcd; // ⚠️ Troque pelo tipo correto da sua biblioteca

// =============================================================================
// FUNÇÕES AUXILIARES: LCD
// =============================================================================

/**
 * lcd_print_line
 * --------------
 * Escreve uma string em uma linha específica do LCD,
 * preenchendo o restante com espaços para limpar caracteres anteriores.
 *
 * Parâmetros:
 *   row - linha do LCD (0 = primeira, 1 = segunda)
 *   str - string a exibir (máx. 16 caracteres)
 */
static void lcd_print_line(int row, const char *str) {
    char buf[LCD_COLS + 1]; // Buffer de exatamente 16 chars + '\0'

    // Copia a string e preenche o restante com espaços
    // Isso garante que o LCD seja "limpo" naquela linha sem usar lcd_clear()
    // (lcd_clear() pisca o display inteiro, o que é visualmente ruim)
    snprintf(buf, sizeof(buf), "%-16s", str); // %-16s: alinha à esquerda, 16 chars

    // Posiciona o cursor e escreve
    // ⚠️ Ajuste a função abaixo conforme a API da sua biblioteca LCD:
    lcd_set_cursor(&lcd, 0, row); // (coluna=0, linha=row)
    lcd_write_str(&lcd, buf);     // Escreve os 16 caracteres
}

/**
 * draw_rssi_bar_lcd
 * -----------------
 * Exibe uma barra de proximidade na segunda linha do LCD usando caracteres.
 * Como o LCD não tem gráficos, usamos '#' para indicar intensidade.
 *
 * Exemplos de saída na linha 1 (segunda linha):
 *   Longe:      [##              ]  → 2 blocos
 *   Médio:      [########        ]  → 8 blocos
 *   Muito perto:[################]  → 16 blocos
 *
 * Parâmetro:
 *   rssi - valor do RSSI em dBm (negativo)
 */
static void draw_rssi_bar_lcd(int rssi) {
    if (rssi > -30)  rssi = -30;
    if (rssi < -100) rssi = -100;

    // Mapeia RSSI [-100, -30] para [0, 16] blocos
    int blocks = (int)(16.0f * (rssi + 100) / 70.0f);

    char bar[LCD_COLS + 1]; // Buffer da barra

    // Preenche com '#' até o número de blocos calculado
    for (int i = 0; i < LCD_COLS; i++) {
        bar[i] = (i < blocks) ? '#' : ' ';
    }
    bar[LCD_COLS] = '\0'; // Terminador de string

    // Escreve a barra na segunda linha do LCD
    lcd_set_cursor(&lcd, 0, 1);
    lcd_write_str(&lcd, bar);
}

// =============================================================================
// FUNÇÕES AUXILIARES: BUZZER
// =============================================================================

static void ledc_init(void) {
    ledc_timer_config_t timer_conf = {
        .speed_mode      = LEDC_SPEED_MODE,
        .duty_resolution = LEDC_RESOLUTION,
        .timer_num       = LEDC_TIMER,
        .freq_hz         = 2000,
        .clk_cfg         = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t ch_conf = {
        .gpio_num   = BUZZER_PIN,
        .speed_mode = LEDC_SPEED_MODE,
        .channel    = LEDC_CHANNEL,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0
    };
    ledc_channel_config(&ch_conf);
}

/**
 * beep_rssi
 * ---------
 * Emite bipe com frequência e duração proporcionais ao RSSI.
 * Mais perto = tom mais agudo e bipe mais curto.
 */
static void beep_rssi(int8_t rssi) {
    if (rssi > -30)  rssi = -30;
    if (rssi < -100) rssi = -100;

    // Frequência: mapeia [-100, -30] → [800, 3000] Hz
    int freq        = 800 + (3000 - 800) * (rssi + 100) / 70;

    // Duração: mapeia [-100, -30] → [600, 80] ms (inverso)
    int duration_ms = 600 - (600 - 80) * (rssi + 100) / 70;

    ledc_set_freq(LEDC_SPEED_MODE, LEDC_TIMER, freq);
    ledc_set_duty(LEDC_SPEED_MODE, LEDC_CHANNEL, 512); // 50% duty cycle
    ledc_update_duty(LEDC_SPEED_MODE, LEDC_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    ledc_set_duty(LEDC_SPEED_MODE, LEDC_CHANNEL, 0);   // Desliga o buzzer
    ledc_update_duty(LEDC_SPEED_MODE, LEDC_CHANNEL);
}

// =============================================================================
// FUNÇÕES DE NVS: PERSISTÊNCIA DE DADOS
// =============================================================================

static void nvs_load_beacons(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) { g_num_beacons = 0; return; }

    int32_t count = 0;
    nvs_get_i32(handle, "count", &count);
    g_num_beacons = (int)count;

    for (int i = 0; i < g_num_beacons && i < MAX_BEACONS; i++) {
        char key[8];
        snprintf(key, sizeof(key), "name%d", i);
        size_t sz = MAX_NAME_LEN;
        nvs_get_str(handle, key, g_beacon_names[i], &sz);
    }
    nvs_close(handle);
}

static void nvs_save_beacons(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return;

    nvs_set_i32(handle, "count", (int32_t)g_num_beacons);
    for (int i = 0; i < g_num_beacons; i++) {
        char key[8];
        snprintf(key, sizeof(key), "name%d", i);
        nvs_set_str(handle, key, g_beacon_names[i]);
    }
    nvs_commit(handle);
    nvs_close(handle);
}

// =============================================================================
// TECLADO MATRICIAL 4x4
// =============================================================================

static void keypad_init(void) {
    for (int r = 0; r < 4; r++) {
        gpio_reset_pin(ROW_PINS[r]);
        gpio_set_direction(ROW_PINS[r], GPIO_MODE_OUTPUT);
        gpio_set_level(ROW_PINS[r], 1);
    }
    for (int c = 0; c < 4; c++) {
        gpio_reset_pin(COL_PINS[c]);
        gpio_set_direction(COL_PINS[c], GPIO_MODE_INPUT);
        gpio_set_pull_mode(COL_PINS[c], GPIO_PULLUP_ONLY);
    }
}

static char keypad_scan(void) {
    for (int r = 0; r < 4; r++) {
        gpio_set_level(ROW_PINS[r], 0);
        vTaskDelay(pdMS_TO_TICKS(2));
        for (int c = 0; c < 4; c++) {
            if (gpio_get_level(COL_PINS[c]) == 0) {
                gpio_set_level(ROW_PINS[r], 1);
                while (gpio_get_level(COL_PINS[c]) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                return KEY_MAP[r][c];
            }
        }
        gpio_set_level(ROW_PINS[r], 1);
    }
    return '\0';
}

static void keypad_task(void *arg) {
    for (;;) {
        char key = keypad_scan();
        if (key != '\0') xQueueSend(g_key_queue, &key, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// =============================================================================
// TAREFA: MENU E INTERFACE
// =============================================================================
/**
 * Telas no LCD 16x2 (2 linhas × 16 colunas):
 *
 * MENU INICIAL:
 *   Linha 0: "FINDIT          "
 *   Linha 1: "1-Loc 2-Cad 3-Cfg"  (truncado em 16: "1-Loc 2-Cad 3-C ")
 *
 * RASTREAMENTO:
 *   Linha 0: nome do gênero (ex: "Romance         ")
 *   Linha 1: barra de proximidade (ex: "########        ")
 *
 * CADASTRO:
 *   Linha 0: "Nome:           "
 *   Linha 1: texto digitado   (ex: "Ficcao_         ")
 *
 * CONFIRMAÇÃO DE SALVO:
 *   Linha 0: "Salvo!          "
 *   Linha 1: nome cadastrado
 */
static void menu_task(void *arg) {
    char key;
    char input_buffer[MAX_NAME_LEN] = {0};
    int  input_len = 0;

    // Exibe menu inicial ao iniciar
    lcd_print_line(0, "FINDIT");
    lcd_print_line(1, "1-Loc 2-Cad 3-Cf");

    for (;;) {
        if (xQueueReceive(g_key_queue, &key, portMAX_DELAY)) {

            xSemaphoreTake(g_state_mutex, portMAX_DELAY);
            int state = g_app_state;
            xSemaphoreGive(g_state_mutex);

            // ------------------------------------------------------------------
            // ESTADO 0: MENU INICIAL
            // ------------------------------------------------------------------
            if (state == 0) {
                if (key == '1') {
                    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
                    int num = g_num_beacons;
                    g_app_state          = 1;
                    g_listen_index       = 0;
                    g_last_rssi          = 0;
                    g_seconds_since_last = 0;
                    xSemaphoreGive(g_state_mutex);

                    if (num == 0) {
                        // Nenhum gênero cadastrado: avisa e volta ao menu
                        lcd_print_line(0, "Sem generos!");
                        lcd_print_line(1, "Use opcao 2");
                        vTaskDelay(pdMS_TO_TICKS(2000));
                        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
                        g_app_state = 0;
                        xSemaphoreGive(g_state_mutex);
                        lcd_print_line(0, "FINDIT");
                        lcd_print_line(1, "1-Loc 2-Cad 3-Cf");
                    } else {
                        // Exibe o primeiro gênero cadastrado na linha 0
                        lcd_print_line(0, g_beacon_names[0]);
                        lcd_print_line(1, "Aguardando...   ");
                    }

                } else if (key == '2') {
                    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
                    g_app_state = 2;
                    xSemaphoreGive(g_state_mutex);

                    memset(input_buffer, 0, sizeof(input_buffer));
                    input_len = 0;

                    // Linha 0: instrução; Linha 1: campo de entrada vazio
                    lcd_print_line(0, "Nome (#=OK *=Canc");
                    lcd_print_line(1, "");  // Linha em branco = campo vazio

                } else if (key == '3') {
                    // Configurações: placeholder
                    lcd_print_line(0, "Configuracoes");
                    lcd_print_line(1, "Em breve...");
                    vTaskDelay(pdMS_TO_TICKS(1500));
                    lcd_print_line(0, "FINDIT");
                    lcd_print_line(1, "1-Loc 2-Cad 3-Cf");
                }

            // ------------------------------------------------------------------
            // ESTADO 1: RASTREAMENTO
            // ------------------------------------------------------------------
            } else if (state == 1) {
                if (key == '#') {
                    // Volta ao menu inicial
                    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
                    g_app_state = 0;
                    xSemaphoreGive(g_state_mutex);
                    lcd_print_line(0, "FINDIT");
                    lcd_print_line(1, "1-Loc 2-Cad 3-Cf");

                } else if (key == '*') {
                    // Navega para o próximo beacon
                    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
                    g_listen_index       = (g_listen_index + 1) % g_num_beacons;
                    g_last_rssi          = 0;
                    g_seconds_since_last = 0;
                    int idx = g_listen_index;
                    xSemaphoreGive(g_state_mutex);

                    // Linha 0: nome do novo gênero; Linha 1: aguardando sinal
                    lcd_print_line(0, g_beacon_names[idx]);
                    lcd_print_line(1, "Aguardando...   ");
                }

            // ------------------------------------------------------------------
            // ESTADO 2: CADASTRO
            // ------------------------------------------------------------------
            } else if (state == 2) {
                if (key == '*') {
                    // Cancela e volta ao menu
                    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
                    g_app_state = 0;
                    xSemaphoreGive(g_state_mutex);
                    lcd_print_line(0, "FINDIT");
                    lcd_print_line(1, "1-Loc 2-Cad 3-Cf");

                } else if (key == '#') {
                    // Confirma o cadastro
                    if (input_len > 0) {
                        xSemaphoreTake(g_state_mutex, portMAX_DELAY);

                        if (g_num_beacons < MAX_BEACONS) {
                            strncpy(g_beacon_names[g_num_beacons], input_buffer, MAX_NAME_LEN - 1);
                            g_beacon_names[g_num_beacons][MAX_NAME_LEN - 1] = '\0';
                            g_num_beacons++;
                            nvs_save_beacons();
                            xSemaphoreGive(g_state_mutex);

                            lcd_print_line(0, "Salvo!");
                            lcd_print_line(1, input_buffer); // Mostra o nome salvo
                        } else {
                            xSemaphoreGive(g_state_mutex);
                            lcd_print_line(0, "Limite atingido!");
                            lcd_print_line(1, "Max: 10 generos");
                        }

                        vTaskDelay(pdMS_TO_TICKS(1500));

                        // Volta ao menu após salvar
                        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
                        g_app_state = 0;
                        xSemaphoreGive(g_state_mutex);
                        lcd_print_line(0, "FINDIT");
                        lcd_print_line(1, "1-Loc 2-Cad 3-Cf");
                    }

                } else if (key >= '0' && key <= '9') {
                    // Adiciona dígito ao buffer de digitação
                    if (input_len < MAX_NAME_LEN - 1) {
                        input_buffer[input_len++] = key;
                        input_buffer[input_len]   = '\0';

                        // Atualiza a linha 1 com o texto digitado até agora
                        lcd_print_line(1, input_buffer);
                    }
                }
            }
        }
    }
}

// =============================================================================
// TAREFA: RECEPÇÃO UART
// =============================================================================

/**
 * uart_receiver_task
 * ------------------
 * Recebe pacote de 2 bytes do ESP32 #1:
 *   Byte 0: ID do beacon
 *   Byte 1: RSSI (int8_t codificado em uint8_t)
 */
static void uart_receiver_task(void *arg) {
    uint8_t data[2];

    for (;;) {
        int len = uart_read_bytes(UART_NUM, data, 2, portMAX_DELAY);

        if (len == 2) {
            uint8_t beacon_id = data[0];
            int8_t  rssi      = (int8_t)data[1]; // Restaura o sinal negativo

            xSemaphoreTake(g_state_mutex, portMAX_DELAY);
            int target_idx = g_listen_index;
            int state      = g_app_state;
            xSemaphoreGive(g_state_mutex);

            if (state == 1 && beacon_id == (uint8_t)target_idx) {
                beep_rssi(rssi);

                xSemaphoreTake(g_state_mutex, portMAX_DELAY);
                g_last_rssi          = rssi;
                g_seconds_since_last = 0;
                xSemaphoreGive(g_state_mutex);
            }
        }
    }
}

// =============================================================================
// TAREFA: ATUALIZAÇÃO DO DISPLAY (1x por segundo)
// =============================================================================

/**
 * display_update_task
 * -------------------
 * Atualiza o LCD a cada 1 segundo durante o rastreamento.
 *
 * Layout no LCD 16x2 durante rastreamento:
 *   Linha 0: "Romance  -65dBm "   → nome do gênero + RSSI compacto
 *   Linha 1: "########        "   → barra de proximidade com '#'
 *
 * Nota: O LCD tem apenas 16 colunas, então as informações são compactadas.
 * O nome é truncado em 8 chars para caber junto ao RSSI na mesma linha.
 */
static void display_update_task(void *arg) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    char line0[LCD_COLS + 1]; // Buffer da linha 0

    for (;;) {
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        int  rssi  = g_last_rssi;
        int  secs  = g_seconds_since_last;
        int  state = g_app_state;
        int  idx   = g_listen_index;
        g_seconds_since_last++;
        xSemaphoreGive(g_state_mutex);

        if (state == 1) {
            /**
             * Linha 0: nome (8 chars) + RSSI (8 chars)
             * Exemplo: "Romance -65dBm  "
             * O formato "%-8.8s%4ddBm " garante que:
             *   - Nome ocupa exatamente 8 chars (cortado se maior)
             *   - RSSI ocupa 4 chars + "dBm " = 8 chars
             *   - Total: 16 chars exatos
             */
            snprintf(line0, sizeof(line0), "%-8.8s%4ddBm ",
                     g_beacon_names[idx], rssi);
            lcd_print_line(0, line0);

            // Linha 1: barra de proximidade com '#' ou mensagem se sem sinal
            if (rssi == 0) {
                lcd_print_line(1, "Aguardando...   ");
            } else {
                draw_rssi_bar_lcd(rssi); // Desenha a barra de '#' na linha 1
            }
        }

        xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
    }
}

// =============================================================================
// PONTO DE ENTRADA PRINCIPAL
// =============================================================================

void app_main(void) {

    // Inicializa NVS (obrigatório para persistência e BLE)
    nvs_flash_init();

    // Cria mutex e fila de teclas
    g_state_mutex = xSemaphoreCreateMutex();
    g_key_queue   = xQueueCreate(10, sizeof(char));

    // Carrega gêneros salvos na flash
    nvs_load_beacons();

    // -------------------------------------------------------------------------
    // Configura I2C para o LCD
    // -------------------------------------------------------------------------
    i2c_config_t i2c_conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_SDA_PIN,
        .scl_io_num       = I2C_SCL_PIN,
        .master.clk_speed = I2C_FREQ_HZ   // 100 kHz: adequado para LCD HD44780
    };
    i2c_param_config(I2C_NUM_0, &i2c_conf);
    i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);

    // -------------------------------------------------------------------------
    // Inicializa o LCD 16x2 via I2C (PCF8574)
    // ⚠️ Ajuste conforme a API da biblioteca LCD escolhida
    // -------------------------------------------------------------------------
    lcd_init(&lcd, I2C_NUM_0, LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
    lcd_backlight(&lcd, true);   // Liga a luz de fundo do LCD
    lcd_clear(&lcd);             // Limpa o display

    // Exibe tela inicial
    lcd_print_line(0, "FINDIT");
    lcd_print_line(1, "1-Loc 2-Cad 3-Cf");

    // -------------------------------------------------------------------------
    // Inicializa buzzer
    // -------------------------------------------------------------------------
    ledc_init();

    // -------------------------------------------------------------------------
    // Configura UART para receber dados do ESP32 #1
    // -------------------------------------------------------------------------
    uart_config_t uart_config = {
        .baud_rate  = 115200,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, UART_PIN_NO_CHANGE, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM, 1024, 0, 0, NULL, 0);

    // -------------------------------------------------------------------------
    // Inicializa teclado 4x4
    // -------------------------------------------------------------------------
    keypad_init();

    // -------------------------------------------------------------------------
    // Cria tarefas FreeRTOS
    // -------------------------------------------------------------------------
    xTaskCreate(keypad_task,         "keypad",  2048, NULL, 10, NULL);
    xTaskCreate(uart_receiver_task,  "uart",    2048, NULL,  9, NULL);
    xTaskCreate(menu_task,           "menu",    3072, NULL,  8, NULL);
    xTaskCreate(display_update_task, "display", 2048, NULL,  5, NULL);
}
