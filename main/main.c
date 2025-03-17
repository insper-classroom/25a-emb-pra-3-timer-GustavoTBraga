/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <string.h> 
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "pico/util/datetime.h"
#include "hardware/rtc.h"

const int ECHO_PIN = 15;
const int TRIGGER_PIN = 14;

const int TRIGGER_PULSE_US = 10;
const int ECHO_TIMEOUT_US = 30000;

volatile bool sistema_ativo = false;
volatile bool echo_recebido = false;
volatile absolute_time_t echo_start_time;
volatile absolute_time_t echo_end_time;
volatile bool echo_timeout = false;
alarm_id_t echo_timeout_alarm = -1;

datetime_t current_time = {
    .year = 2025,
    .month = 3,
    .day = 16,
    .dotw = 0,
    .hour = 21,
    .min = 30,
    .sec = 0
};

float calcula_distancia_cm(uint64_t duracao_us) {
    // distancia = (duracao * velocidade_som) / 2
    // v_som = 343 m/s = 34300 cm / 1000000 us = 0.0343 cm/us
    return (duracao_us * 0.0343) / 2.0;
}

int64_t echo_timeout_callback(alarm_id_t id, void *user_data) {
    echo_timeout = true;
    echo_recebido = false;
    echo_timeout_alarm = -1;
    return 0;
}

void echo_isr(uint gpio, uint32_t events) {
    if (gpio == ECHO_PIN) {
        if (events & GPIO_IRQ_EDGE_RISE) {
            echo_start_time = get_absolute_time();
            echo_recebido = false;
            
            if (echo_timeout_alarm >= 0) {
                cancel_alarm(echo_timeout_alarm);
            }

            echo_timeout_alarm = add_alarm_in_us(ECHO_TIMEOUT_US, echo_timeout_callback, NULL, true);

        } else if (events & GPIO_IRQ_EDGE_FALL) {
            echo_end_time = get_absolute_time();
            echo_recebido = true;
            echo_timeout = false;
            
            if (echo_timeout_alarm >= 0) {
                cancel_alarm(echo_timeout_alarm);
                echo_timeout_alarm = -1;
            }
        }
    }
}

void disparar_medicao() {
    echo_recebido = false;
    echo_timeout = false;
    
    gpio_put(TRIGGER_PIN, 1);
    sleep_us(TRIGGER_PULSE_US);
    gpio_put(TRIGGER_PIN, 0);
}

void verifica_comando(char *cmd) {
    if (strcmp(cmd, "start") == 0 || strcmp(cmd, "START") == 0) {
        sistema_ativo = true;
        printf("Sistema iniciado. Medindo distancia.\n");
    } else if (strcmp(cmd, "stop") == 0 || strcmp(cmd, "STOP") == 0) {
        sistema_ativo = false;
        printf("Sistema pausado.\n");
    } else {
        printf("Comando desconhecido: %s\n", cmd);
        printf("Comandos disponíveis: 'start', 'stop'\n");
    }
}

void update_rtc_time() {
    current_time.sec++;
    if (current_time.sec >= 60) {
        current_time.sec = 0;
        current_time.min++;
        if (current_time.min >= 60) {
            current_time.min = 0;
            current_time.hour++;
            if (current_time.hour >= 24) {
                current_time.hour = 0;
            }
        }
    }
    rtc_set_datetime(&current_time);
}

void print_medicao(float distancia) {
    rtc_get_datetime(&current_time);
    
    if (echo_timeout) {
        printf("%02d:%02d:%02d - Falha\n", current_time.hour, current_time.min, current_time.sec);
    } else {
        printf("%02d:%02d:%02d - %.0f cm\n", current_time.hour, current_time.min, current_time.sec, distancia);
    }
}

int main() {
    stdio_init_all();
    
    gpio_init(ECHO_PIN);
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);
    gpio_put(TRIGGER_PIN, 0);
    
    rtc_init();
    rtc_set_datetime(&current_time);
    
    sleep_ms(2000);
    printf("Escreva um dos comandos: 'start', 'stop'\n");
    
    gpio_set_irq_enabled_with_callback(ECHO_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &echo_isr);
    
    char cmd_buffer[20] = {0};
    int cmd_index = 0;
    absolute_time_t ultima_medicao_tempo = get_absolute_time();
    absolute_time_t rtc_update_time = get_absolute_time();
    
    while (true) {
        int c = getchar_timeout_us(1000);
        
        if (absolute_time_diff_us(rtc_update_time, get_absolute_time()) >= 1000000) {
            update_rtc_time();
            rtc_update_time = get_absolute_time();
        }
        
        if (c != PICO_ERROR_TIMEOUT) {
            if (c == '\r' || c == '\n') {
                if (cmd_index > 0) {
                    cmd_buffer[cmd_index] = '\0';
                    verifica_comando(cmd_buffer);
                    cmd_index = 0;
                }
            } else if (cmd_index < sizeof(cmd_buffer) - 1) {
                cmd_buffer[cmd_index++] = (char) c;
            }
        }
        
        if (sistema_ativo) {
            if (absolute_time_diff_us(ultima_medicao_tempo, get_absolute_time()) >= 1000000) {
                disparar_medicao();
                
                sleep_ms(50);
                
                if (echo_recebido) {
                    int echo_duration_us = absolute_time_diff_us(echo_start_time, echo_end_time);
                    float distancia_cm = calcula_distancia_cm(echo_duration_us);
                    print_medicao(distancia_cm);
                } else if (echo_timeout) {
                    print_medicao(0);
                }
                
                ultima_medicao_tempo = get_absolute_time();
            }
        }
    }
    
    return 0;
}