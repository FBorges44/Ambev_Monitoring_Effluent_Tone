#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/cyw43_arch.h"
#include "lwipopts.h"
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include "lwip/netdb.h"
#include "lwip/ip4_addr.h"
#include <string.h>
#include <stdbool.h>

#ifndef SOCK_STREAM
#define SOCK_STREAM 1
#endif

#ifndef AF_INET
#define AF_INET 2
#endif

// ==================== CONFIGURAÇÕES WI-FI ====================
#define WIFI_SSID "HELPJUN"
#define WIFI_PASS "22jun2012"
#define SERVER_IP "192.168.25.1"   // IP do PC rodando Flask
#define SERVER_PORT 5000

// ==================== CONFIGURAÇÕES I2C ====================
#define I2C_PORT i2c1
#define SDA_PIN 2
#define SCL_PIN 3
#define I2C_FREQ 100000  // 100kHz

// ==================== TCS34725 DEFINIÇÕES ====================
#define TCS34725_ADDR        0x29
#define TCS34725_COMMAND_BIT 0x80
#define TCS34725_ENABLE      0x00
#define TCS34725_ATIME       0x01
#define TCS34725_CONTROL     0x0F
#define TCS34725_ID          0x12
#define TCS34725_STATUS      0x13
#define TCS34725_CDATAL      0x14
#define TCS34725_RDATAL      0x16
#define TCS34725_GDATAL      0x18
#define TCS34725_BDATAL      0x1A
#define TCS34725_ENABLE_AEN  0x02
#define TCS34725_ENABLE_PON  0x01
#define TCS34725_GAIN_4X     0x01
#define TCS34725_INTEGRATIONTIME_101MS  0xD5

typedef struct {
    uint16_t clear;
    uint16_t red;
    uint16_t green;
    uint16_t blue;
    bool valid;
} color_data_t;

// ==================== FUNÇÕES DE I2C/SENSOR ====================
bool tcs34725_write_register(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {TCS34725_COMMAND_BIT | reg, value};
    return i2c_write_blocking(I2C_PORT, TCS34725_ADDR, buf, 2, false) == 2;
}

bool tcs34725_read_register(uint8_t reg, uint8_t *value) {
    uint8_t cmd = TCS34725_COMMAND_BIT | reg;
    if (i2c_write_blocking(I2C_PORT, TCS34725_ADDR, &cmd, 1, true) != 1) return false;
    return i2c_read_blocking(I2C_PORT, TCS34725_ADDR, value, 1, false) == 1;
}

bool tcs34725_read_16bit(uint8_t reg, uint16_t *value) {
    uint8_t buf[2];
    uint8_t cmd = TCS34725_COMMAND_BIT | reg;
    if (i2c_write_blocking(I2C_PORT, TCS34725_ADDR, &cmd, 1, true) != 1) return false;
    if (i2c_read_blocking(I2C_PORT, TCS34725_ADDR, buf, 2, false) != 2) return false;
    *value = buf[0] | (buf[1] << 8);
    return true;
}

bool tcs34725_init() {
    if (!tcs34725_write_register(TCS34725_ENABLE, TCS34725_ENABLE_PON)) return false;
    sleep_ms(3);
    if (!tcs34725_write_register(TCS34725_ENABLE, TCS34725_ENABLE_PON | TCS34725_ENABLE_AEN)) return false;
    if (!tcs34725_write_register(TCS34725_ATIME, TCS34725_INTEGRATIONTIME_101MS)) return false;
    if (!tcs34725_write_register(TCS34725_CONTROL, TCS34725_GAIN_4X)) return false;
    sleep_ms(120);
    return true;
}

bool tcs34725_data_ready() {
    uint8_t status;
    if (!tcs34725_read_register(TCS34725_STATUS, &status)) return false;
    return (status & 0x01) != 0;
}

bool tcs34725_read_color(color_data_t *data) {
    if (!tcs34725_data_ready()) { data->valid = false; return false; }
    bool ok = true;
    ok &= tcs34725_read_16bit(TCS34725_CDATAL, &data->clear);
    ok &= tcs34725_read_16bit(TCS34725_RDATAL, &data->red);
    ok &= tcs34725_read_16bit(TCS34725_GDATAL, &data->green);
    ok &= tcs34725_read_16bit(TCS34725_BDATAL, &data->blue);
    data->valid = ok;
    return ok;
}

void color_to_rgb(color_data_t *c, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (!c->valid || c->clear == 0) { *r=*g=*b=0; return; }
    *r = (uint8_t)((c->red * 255) / c->clear);
    *g = (uint8_t)((c->green * 255) / c->clear);
    *b = (uint8_t)((c->blue * 255) / c->clear);
}

// ==================== ENVIO PARA FLASK ====================
void send_data(uint8_t r, uint8_t g, uint8_t b) {
    char json[64];
    snprintf(json, sizeof(json), "{\"r\":%d,\"g\":%d,\"b\":%d}", r, g, b);

    int sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { 
        printf("❌ Erro ao criar socket: %d\n", sock); 
        return; 
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(SERVER_PORT);
    
    // Converte IP string para formato binário
    if (inet_aton(SERVER_IP, &dest.sin_addr) == 0) {
        printf("❌ IP inválido\n");
        lwip_close(sock);
        return;
    }

    if (lwip_connect(sock, (struct sockaddr*)&dest, sizeof(dest)) < 0) {
        printf("❌ Falha na conexão\n");
        lwip_close(sock);
        return;
    }

    char req[300];
    int json_len = strlen(json);
    snprintf(req, sizeof(req),
             "POST /dados HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n\r\n"
             "%s",
             SERVER_IP, SERVER_PORT, json_len, json);

    if (lwip_write(sock, req, strlen(req)) < 0) {
        printf("❌ Erro ao enviar dados\n");
    } else {
        printf("✅ Dados enviados: %s\n", json);
        
        // Lê resposta do servidor
        char buffer[256];
        int n = lwip_read(sock, buffer, sizeof(buffer)-1);
        if (n > 0) {
            buffer[n] = 0;
            // Procura pelo código de status HTTP
            char *status_line = strstr(buffer, "HTTP/1.");
            if (status_line) {
                char *end_line = strstr(status_line, "\r\n");
                if (end_line) {
                    *end_line = '\0';
                    printf("📨 Status: %s\n", status_line);
                }
            }
        }
    }

    lwip_close(sock);
}

// ==================== MAIN ====================
int main() {
    stdio_init_all();
    sleep_ms(2000);

    printf("🎨 Iniciando TCS34725 + Wi-Fi -> Flask\n");

    // Inicializa I2C
    i2c_init(I2C_PORT, I2C_FREQ);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    // Inicializa Wi-Fi
    if (cyw43_arch_init()) { 
        printf("❌ Erro inicializando Wi-Fi\n"); 
        return -1; 
    }
    
    cyw43_arch_enable_sta_mode();
    printf("⏳ Conectando ao Wi-Fi %s...\n", WIFI_SSID);
    
    if (cyw43_arch_wifi_connect_blocking(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK)) {
        printf("❌ Falha ao conectar Wi-Fi\n");
        return -1;
    }
    printf("✅ Conectado ao Wi-Fi!\n");

    // Aguarda um pouco para garantir conexão estável
    sleep_ms(1000);

    // Inicializa sensor
    printf("🔧 Inicializando sensor TCS34725...\n");
    if (!tcs34725_init()) {
        printf("❌ Falha inicializando sensor!\n");
        return -1;
    }
    printf("✅ Sensor inicializado!\n");

    color_data_t data;
    uint8_t r, g, b;
    int contador = 0;

    while (true) {
        if (tcs34725_read_color(&data)) {
            color_to_rgb(&data, &r, &g, &b);
            printf("[%d] RGB: %3d %3d %3d (Clear: %d)\n", ++contador, r, g, b, data.clear);

            // Envia para Flask a cada leitura
            send_data(r, g, b);
        } else {
            printf("⚠️ Aguardando dados do sensor...\n");
        }
        
        sleep_ms(2000);
    }

    // Cleanup (nunca alcançado neste exemplo)
    cyw43_arch_deinit();
    return 0;
}