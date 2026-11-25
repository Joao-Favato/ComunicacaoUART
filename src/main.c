#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/sys/crc.h>

#include "pwm_z42.h"

#define ACTION_ON         0x01U
#define ACTION_OFF        0x02U
#define ACTION_TOGGLE     0x03U
#define ACTION_BLINK_1S   0x04U
#define ACTION_BLINK_2S   0x05U

#define BOARD_ID          0x01U
#define PACKET_SIZE       6U
#define RX_BUF_SIZE       256U
#define RX_TIMEOUT_MS     50    /* Timeout curto para detectar pacote truncado */

/* Configuração do PWM (Resolucao de 8 bits: 0 a 255) */
#define PWM_MODULO        255U
#define PWM_POLARITY      TPM_PWM_L  /* Use TPM_PWM_H se o LED for ativo em nivel alto */

/* Mapeamento hardware FRDM-KL25Z (MKL25Z4) */
#define LED_R_TPM         TPM2
#define LED_R_CH          0U
#define LED_R_GPIO        GPIOB
#define LED_R_PIN         18U

#define LED_G_TPM         TPM2
#define LED_G_CH          1U
#define LED_G_GPIO        GPIOB
#define LED_G_PIN         19U

#define LED_B_TPM         TPM0
#define LED_B_CH          1U
#define LED_B_GPIO        GPIOD
#define LED_B_PIN         1U

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
static const struct device *const uart_dev = DEVICE_DT_GET(UART_COMM_NODE);

RING_BUF_DECLARE(rx_ringbuf, RX_BUF_SIZE);

struct rgb_state {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	bool is_on;
	uint32_t blink_period_ms;
	int64_t last_blink_ms;
};

/* Converte 0..255 para o registrador CnV (255 garante 100% do ciclo ativo) */
static inline uint16_t intensity_to_cnv(uint8_t val)
{
	return (val == 255U) ? (PWM_MODULO + 1U) : (uint16_t)val;
}

/* Atualiza o duty cycle PWM dos 3 canais RGB */
static void apply_rgb_pwm(const struct rgb_state *st)
{
	if (st->is_on) {
		pwm_tpm_CnV(LED_R_TPM, LED_R_CH, intensity_to_cnv(st->r));
		pwm_tpm_CnV(LED_G_TPM, LED_G_CH, intensity_to_cnv(st->g));
		pwm_tpm_CnV(LED_B_TPM, LED_B_CH, intensity_to_cnv(st->b));
	} else {
		pwm_tpm_CnV(LED_R_TPM, LED_R_CH, 0U);
		pwm_tpm_CnV(LED_G_TPM, LED_G_CH, 0U);
		pwm_tpm_CnV(LED_B_TPM, LED_B_CH, 0U);
	}
}

static bool init_rgb_pwm(void)
{
	/* Garante limpeza dos bits TPMSRC antes de selecionar TPM_PLLFLL */
	SIM->SOPT2 &= ~SIM_SOPT2_TPMSRC_MASK;

	/* Inicializa TPM2 (Vermelho e Verde) e TPM0 (Azul)
	 * Clock = MCGFLLCLK/MCGPLLCLK/2 (48 MHz), Prescaler = 16, MOD = 255 -> ~11,7 kHz */
	if (!pwm_tpm_Init(TPM2, TPM_PLLFLL, PWM_MODULO, TPM_CLK, PS_16, EDGE_PWM)) {
		return false;
	}
	if (!pwm_tpm_Init(TPM0, TPM_PLLFLL, PWM_MODULO, TPM_CLK, PS_16, EDGE_PWM)) {
		return false;
	}

	/* Configura os pinos e canais PWM */
	if (!pwm_tpm_Ch_Init(LED_R_TPM, LED_R_CH, PWM_POLARITY, LED_R_GPIO, LED_R_PIN)) {
		return false;
	}
	if (!pwm_tpm_Ch_Init(LED_G_TPM, LED_G_CH, PWM_POLARITY, LED_G_GPIO, LED_G_PIN)) {
		return false;
	}
	if (!pwm_tpm_Ch_Init(LED_B_TPM, LED_B_CH, PWM_POLARITY, LED_B_GPIO, LED_B_PIN)) {
		return false;
	}

	return true;
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


static void process_packet(const uint8_t pkt[PACKET_SIZE], struct rgb_state *st)
{
	uint8_t crc_check = crc8(pkt, PACKET_SIZE - 1U, 0x07U, 0x00U, false);

	if (pkt[IDX_CHECK] != crc_check) {
		LOG_WRN("Checksum CRC8 invalido! Recebido: 0x%02X | Esperado: 0x%02X",
			pkt[IDX_CHECK], crc_check);
		return;
	}

	if (pkt[IDX_ID] != BOARD_ID) {
		LOG_WRN("ID ignorado: 0x%02X (esperado 0x%02X)", pkt[IDX_ID], BOARD_ID);
		return;
	}

	/* Lê diretamente a intensidade (0 a 255) de cada canal */
	uint8_t r_val = pkt[IDX_R];
	uint8_t g_val = pkt[IDX_G];
	uint8_t b_val = pkt[IDX_B];
	uint8_t action = pkt[IDX_ACTION];

	switch (action) {
	case ACTION_ON:
		st->r = r_val;
		st->g = g_val;
		st->b = b_val;
		st->blink_period_ms = 0U;
		st->is_on = true;
		apply_rgb_pwm(st);
		LOG_INF("ACAO: LIGAR PWM (R=%u, G=%u, B=%u)", st->r, st->g, st->b);
		break;

	case ACTION_OFF:
		st->blink_period_ms = 0U;
		st->is_on = false;
		apply_rgb_pwm(st);
		LOG_INF("ACAO: DESLIGAR PWM");
		break;

	case ACTION_TOGGLE:
		if (r_val > 0U || g_val > 0U || b_val > 0U) {
			st->r = r_val;
			st->g = g_val;
			st->b = b_val;
		}
		st->blink_period_ms = 0U;
		st->is_on = !st->is_on;
		apply_rgb_pwm(st);
		LOG_INF("ACAO: TOGGLE -> %s (R=%u, G=%u, B=%u)",
			st->is_on ? "LIGADO" : "DESLIGADO", st->r, st->g, st->b);
		break;

	case ACTION_BLINK_1S:
		st->r = r_val;
		st->g = g_val;
		st->b = b_val;
		st->blink_period_ms = 1000U;
		st->is_on = true;
		st->last_blink_ms = k_uptime_get();
		apply_rgb_pwm(st);
		LOG_INF("ACAO: PISCA 1s (R=%u, G=%u, B=%u)", st->r, st->g, st->b);
		break;

	case ACTION_BLINK_2S:
		st->r = r_val;
		st->g = g_val;
		st->b = b_val;
		st->blink_period_ms = 2000U;
		st->is_on = true;
		st->last_blink_ms = k_uptime_get();
		apply_rgb_pwm(st);
		LOG_INF("ACAO: PISCA 2s (R=%u, G=%u, B=%u)", st->r, st->g, st->b);
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
		.r = 255U,
		.g = 255U,
		.b = 255U,
		.is_on = false,
		.blink_period_ms = 0U,
		.last_blink_ms = 0,
	};

	if (!init_rgb_pwm()) {
		LOG_ERR("Falha ao inicializar modulos TPM para PWM!");
		return -1;
	}

	apply_rgb_pwm(&led_state);
	LOG_INF("Receptor RGB (PWM + XOR) pronto! ID=0x%02X | 115200 8N1", BOARD_ID);

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
				apply_rgb_pwm(&led_state);
			}
		}

		k_msleep(5);
	}

	return 0;
}