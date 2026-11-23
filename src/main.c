#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>

#define LED2_NODE DT_ALIAS(led2)
#define LED1_NODE DT_ALIAS(led1)
#define LED0_NODE DT_ALIAS(led0)

#define ACTION_ON         0x01U
#define ACTION_OFF        0x02U
#define ACTION_TOGGLE     0x03U
#define ACTION_BLINK_1S   0x04U
#define ACTION_BLINK_2S   0x05U

#define BOARD_ID          0x01U
#define PACKET_SIZE       6U
#define RX_BUF_SIZE       256U
#define RX_TIMEOUT_MS     50    /* Timeout curto para detectar pacote truncado */

enum {
	IDX_ID = 0,
	IDX_R,
	IDX_G,
	IDX_B,
	IDX_ACTION,
	IDX_CHECK
};

LOG_MODULE_REGISTER(uart_comm, LOG_LEVEL_INF);

#define UART_COMM_NODE DT_ALIAS(uart_comm)

static const struct gpio_dt_spec ledVerde    = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec ledAzul     = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
static const struct gpio_dt_spec ledVermelho = GPIO_DT_SPEC_GET(LED2_NODE, gpios);

static const struct device *const uart_dev = DEVICE_DT_GET(UART_COMM_NODE);

RING_BUF_DECLARE(rx_ringbuf, RX_BUF_SIZE);

struct rgb_state {
	bool r;
	bool g;
	bool b;
	bool is_on;
	uint32_t blink_period_ms;
	int64_t last_blink_ms;
};

/* Aciona ou apaga os pinos digitais do LED RGB */
static void apply_rgb_gpio(const struct rgb_state *st)
{
	if (st->is_on) {
		gpio_pin_set_dt(&ledVermelho, st->r ? 1 : 0);
		gpio_pin_set_dt(&ledVerde, st->g ? 1 : 0);
		gpio_pin_set_dt(&ledAzul, st->b ? 1 : 0);
	} else {
		gpio_pin_set_dt(&ledVermelho, 0);
		gpio_pin_set_dt(&ledVerde, 0);
		gpio_pin_set_dt(&ledAzul, 0);
	}
}

static void uart_isr_callback(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);
	uint8_t chunk[32];

	if (!uart_irq_update(dev)) {
		return;
	}

	while (uart_irq_rx_ready(dev)) {
		int len = uart_fifo_read(dev, chunk, sizeof(chunk));
		if (len > 0) {
			(void)ring_buf_put(&rx_ringbuf, chunk, (size_t)len);
		}
	}
}

static uint8_t compute_xor(const uint8_t *data, size_t len)
{
	uint8_t xor_sum = 0U;

	for (size_t i = 0; i < len; i++) {
		xor_sum ^= data[i];
	}

	return xor_sum;
}

static void process_packet(const uint8_t pkt[PACKET_SIZE], struct rgb_state *st)
{
	uint8_t expected_check = compute_xor(pkt, PACKET_SIZE - 1U);

	if (pkt[IDX_CHECK] != expected_check) {
		LOG_WRN("Checksum XOR invalido! Recebido: 0x%02X | Esperado: 0x%02X",
			pkt[IDX_CHECK], expected_check);
		return;
	}

	if (pkt[IDX_ID] != BOARD_ID) {
		LOG_WRN("ID ignorado: 0x%02X (esperado 0x%02X)", pkt[IDX_ID], BOARD_ID);
		return;
	}

	/* Converte qualquer byte > 0 em 1 (ligado) e 0 em 0 (desligado) */
	bool r_on = (pkt[IDX_R] > 0U);
	bool g_on = (pkt[IDX_G] > 0U);
	bool b_on = (pkt[IDX_B] > 0U);
	uint8_t action = pkt[IDX_ACTION];

	switch (action) {
	case ACTION_ON:
		st->r = r_on;
		st->g = g_on;
		st->b = b_on;
		st->blink_period_ms = 0U;
		st->is_on = true;
		apply_rgb_gpio(st);
		LOG_INF("ACAO: LIGAR (R=%d, G=%d, B=%d)", st->r, st->g, st->b);
		break;

	case ACTION_OFF:
		st->blink_period_ms = 0U;
		st->is_on = false;
		apply_rgb_gpio(st);
		LOG_INF("ACAO: DESLIGAR");
		break;

	case ACTION_TOGGLE:
		if (r_on || g_on || b_on) {
			st->r = r_on;
			st->g = g_on;
			st->b = b_on;
		}
		st->blink_period_ms = 0U;
		st->is_on = !st->is_on;
		apply_rgb_gpio(st);
		LOG_INF("ACAO: TOGGLE -> %s (R=%d, G=%d, B=%d)",
			st->is_on ? "LIGADO" : "DESLIGADO", st->r, st->g, st->b);
		break;

	case ACTION_BLINK_1S:
		st->r = r_on;
		st->g = g_on;
		st->b = b_on;
		st->blink_period_ms = 1000U;
		st->is_on = true;
		st->last_blink_ms = k_uptime_get();
		apply_rgb_gpio(st);
		LOG_INF("ACAO: PISCA 1s (R=%d, G=%d, B=%d)", st->r, st->g, st->b);
		break;

	case ACTION_BLINK_2S:
		st->r = r_on;
		st->g = g_on;
		st->b = b_on;
		st->blink_period_ms = 2000U;
		st->is_on = true;
		st->last_blink_ms = k_uptime_get();
		apply_rgb_gpio(st);
		LOG_INF("ACAO: PISCA 2s (R=%d, G=%d, B=%d)", st->r, st->g, st->b);
		break;

	default:
		LOG_WRN("Acao desconhecida: 0x%02X", action);
		break;
	}
}

int main(void)
{
	const struct uart_config uart_cfg = {
		.baudrate = 115200U,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};
	(void)uart_configure(uart_dev, &uart_cfg);

	uart_irq_callback_user_data_set(uart_dev, uart_isr_callback, NULL);
	uart_irq_rx_enable(uart_dev);
    
    struct rgb_state led_state = {
		.r = true,
		.g = true,
		.b = true,
		.is_on = false,
		.blink_period_ms = 0U,
		.last_blink_ms = 0,
	};

    LOG_INF("Receptor RGB (GPIO + XOR) pronto! ID=0x%02X | 115200 8N1", BOARD_ID);

    gpio_pin_configure_dt(&ledVermelho,     GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&ledVerde,        GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&ledAzul,         GPIO_OUTPUT_INACTIVE);

   
    uint8_t pkt_buf[PACKET_SIZE];
	size_t pkt_pos = 0U;
	int64_t pkt_start_ms = 0;

	while (1) {
		uint8_t byte;

		/* 1. Drena bytes do ring buffer e monta o pacote de 6 bytes */
		while (ring_buf_get(&rx_ringbuf, &byte, 1U) == 1U) {
			if (pkt_pos == 0U) {
				pkt_start_ms = k_uptime_get();
			}

			pkt_buf[pkt_pos++] = byte;

			if (pkt_pos == PACKET_SIZE) {
				process_packet(pkt_buf, &led_state);
				pkt_pos = 0U;
			}
		}

		/* 2. Detecta pacote truncado via timeout curto */
		if (pkt_pos > 0U && (k_uptime_get() - pkt_start_ms) > RX_TIMEOUT_MS) {
			LOG_WRN("Timeout RX: pacote truncado (%zu/%u bytes) descartado",
				pkt_pos, PACKET_SIZE);
			pkt_pos = 0U;
		}

		/* 3. Atualiza pisca-pisca não-bloqueante (PISCA 1s / PISCA 2s) */
		if (led_state.blink_period_ms > 0U) {
			int64_t now = k_uptime_get();
			if ((now - led_state.last_blink_ms) >= (int64_t)led_state.blink_period_ms) {
				led_state.is_on = !led_state.is_on;
				led_state.last_blink_ms = now;
				apply_rgb_gpio(&led_state);
			}
		}

		k_msleep(5);
	}

	return 0;
}