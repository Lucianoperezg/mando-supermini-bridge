#pragma once

/**
 * @brief Inicializa el buffer del CLI (si es necesario)
 */
void cliInit();

/**
 * @brief Realiza la lectura periódica de Serial y procesa comandos entrantes
 */
void cliPoll();
