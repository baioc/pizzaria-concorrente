#ifndef __PIZZERIA_H_
#define __PIZZERIA_H_

#include <pthread.h>

#include "queue.h"


typedef struct cliente_s cliente_t;

typedef struct pedido_s {
    cliente_t* cliente;
    int id;
    int sabor;
	//
} pedido_t;

typedef struct pizza_s {
    int sabor;
    int fatias;
    pedido_t* pedido;
    struct timespec ts;
	//
	pthread_mutex_t* pegador;
} pizza_t;


/*
 * Inicializa quaisquer recursos e estruturas de dados que sejam necessários
 * antes da pizzeria poder receber clientes.
 * Chamada pela função main() antes de qualquer outra função.
 */
void pizzeria_init(int tam_forno, int n_pizzaiolos, int n_mesas,
                   int n_garcons, int tam_deck, int n_grupos);

/*
 * Impede que novos clientes sejam aceitos e bloqueia até que os clientes
 * dentro da pizzeria saiam voluntariamente.
 * Todo cliente que já estava sentado antes do fechamento, tem direito a
 * receber e comer pizzas pendentes e a fazer novos pedidos.
 * Clientes que ainda não se sentaram não conseguirão sentar pois pegar_mesas
 * retornará -1.
 * Chamada pela função main() antes de pizzeria_destroy() ao fim do programa.
 */
void pizzeria_close(void);

/*
 * Libera quaisquer recursos alocados por pizzeria_init().
 */
void pizzeria_destroy(void);

/*
 * Chama um garçom, bloqueia até o garçom chegar. Chamada pelo cliente líder.
 */
void garcom_chamar(void);

/*
 * Faz um pedido de pizza. O pedido aparece como uma smart ficha no smart deck.
 * Os clientes não fazem um novo pedido antes de receber a pizza.
 * Chamado pelo cliente líder.
 */
void fazer_pedido(pedido_t* pedido);

/*
 * Indica que o grupo vai embora. Chamada pelo cliente líder.
 */
void garcom_tchau(int tam_grupo);

/*
 * Pega uma fatia da pizza. Retorna 0 (sem erro) se conseguiu pegar a fatia,
 * ou -1 (erro) se a pizza já acabou. Chamada pelas threads clientes.
 */
int pizza_pegar_fatia(pizza_t* pizza);

/*
 * Indica que a pizza dada como argumento (previamente colocada no forno)
 * está pronta. Chamada pelo nariz do pizzaiolo.
 * A thread que chamará essa função será uma thread específica para esse fim,
 * criada nas profundezas do helper.c.
 */
void pizza_assada(pizza_t* pizza);

/*
 * Algoritmo para conseguir mesas suficientes para um grupo de tam_grupo pessoas.
 * Note que vários clientes podem chamar essa função ao mesmo tempo.
 * Deve retornar zero se não houve erro, ou -1 se a pizzaria já foi fechada
 * com pizzeria_close().
 * A implementação não precisa considerar o layout das mesas.
 * Chamada pelo cliente líder do grupo.
 */
int  pegar_mesas(int tam_grupo);

#endif /*__PIZZERIA_H_*/
