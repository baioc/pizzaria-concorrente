#include "pizzeria.h"

#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stddef.h>

#include "helper.h"

static void* cozinhar(void* arg);


static bool pizzaria_fechada;
static sem_t forno_espacos;
static pthread_mutex_t pa;
static sem_t garcons;
static queue_t deck;
static queue_t pegadores;
static pthread_t* pizzaiolo;
static size_t pizzaiolo_size;


// RESTAURANTE

void pizzeria_init(int tam_forno, int n_pizzaiolos, int n_mesas,
                   int n_garcons, int tam_deck, int n_grupos) {
	assert(tam_forno > 0);
	assert(n_pizzaiolos > 0);
	assert(n_mesas > 0);
	assert(n_garcons > 0);
	assert(tam_deck > 0);
	assert(n_grupos > 0);

	pizzaria_fechada = false;

	// @TODO n_mesas

	sem_init(&forno_espacos, false, tam_forno);

	pthread_mutex_init(&pa, NULL);

	sem_init(&garcons, false, n_garcons);

	queue_init(&deck, tam_deck);

	queue_init(&pegadores, n_grupos);
	for (int i = 0; i < n_grupos; ++i) {
		pthread_mutex_t* pegador = malloc(sizeof(pthread_mutex_t));
		assert(pegador != NULL);
		pthread_mutex_init(pegador, NULL);
		queue_push_back(&pegadores, pegador);
	}

	pizzaiolo = malloc(sizeof(pthread_t) * n_pizzaiolos);
	assert(pizzaiolo != NULL);
	pizzaiolo_size = n_pizzaiolos;
	for (int i = 0; i < n_pizzaiolos; ++i)
		pthread_create(&pizzaiolo[i], NULL, cozinhar, NULL);
}

void pizzeria_close(void) {
	pizzaria_fechada = true;

	for (int i = 0; i < pizzaiolo_size; ++i)
		pthread_join(pizzaiolo[i], NULL);
}

void pizzeria_destroy(void) {
	free(pizzaiolo);

	while(!queue_empty(&pegadores))
		free(queue_wait(&pegadores));
	queue_destroy(&pegadores);

	queue_destroy(&deck);

	sem_destroy(&garcons);

	pthread_mutex_destroy(&pa);

	sem_destroy(&forno_espacos);
}


// PIZZAIOLOS

static void* cozinhar(void* arg) {
	while (!pizzaria_fechada) {
		pedido_t* pedido = queue_wait(&deck);

		pizza_t* pizza = pizzaiolo_montar_pizza(pedido);

		sem_wait(&forno_espacos);
		pthread_mutex_lock(&pa);
		pizzaiolo_colocar_forno(pizza);
		pthread_mutex_unlock(&pa);

		// @TODO: espera pizza assada

		pthread_mutex_lock(&pa);
		pizzaiolo_retirar_forno(pizza);
		pthread_mutex_unlock(&pa);
		sem_post(&forno_espacos);

		// @TODO: balcao ?
		pizza->pegador = queue_wait(&pegadores); // @FIXME e dps?

		garcom_entregar(pizza);
	}
	return NULL;
}


// CLIENTES

int pegar_mesas(int tam_grupo) {
	// @TODO check input, etc
	return -1;
}

void fazer_pedido(pedido_t* pedido) {
	queue_push_back(&deck, pedido);
}

int pizza_pegar_fatia(pizza_t* pizza) {
	pthread_mutex_lock(pizza->pegador);
	if (pizza->fatias >= 1) {
		pizza->fatias--;
		pthread_mutex_unlock(pizza->pegador);
		return 0;
	} else {
		pthread_mutex_unlock(pizza->pegador);
		return -1;
	}
}

void garcom_chamar(void) {
	sem_wait(&garcons);
}

void garcom_tchau(int tam_grupo) {
	sem_post(&garcons);
	// @TODO liberar mesas
}


// NARIZ DO PIZZAIOLO (?)

void pizza_assada(pizza_t* pizza) {
	// @TODO
}
